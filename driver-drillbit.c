/*
 * Copyright 2013 Con Kolivas
 * Copyright 2013 Angus Gratton
 * Copyright 2013 James Nichols
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 3 of the License, or (at your option)
 * any later version.  See COPYING for more details.
 */

#include "config.h"

#include "miner.h"
#include "driver-drillbit.h"
#include "sha2.h"

#define TIMEOUT 3000
#define RESULT_TIMEOUT 5000
#define MAX_RESULTS 16 // max results from a single chip

#define drvlog(prio, fmt, ...) do { \
	if (drillbit->device_id == -1) { \
		applog(prio, "%s: "fmt, \
			     drillbit->drv->dname, \
			     ##__VA_ARGS__); \
	} else { \
		applog(prio, "%s %d: "fmt, \
			     drillbit->drv->name, \
			     drillbit->device_id, \
			     ##__VA_ARGS__); \
	} \
} while (0)

/* Request and response structs for firmware */

typedef struct {
	uint16_t chip_id;
	uint8_t midstate[32];
	uint8_t data[12];
} WorkRequest;

#define SZ_SERIALISED_WORKREQUEST 46
static void serialise_work_request(char *buf, uint16_t chip_id, const struct work *wr);

typedef struct {
	uint16_t chip_id;
	uint8_t num_nonces;
	uint8_t is_idle;
	uint32_t nonce[MAX_RESULTS];
} WorkResult;

#define SZ_SERIALISED_WORKRESULT (4+4*MAX_RESULTS)
static void deserialise_work_result(WorkResult *work_result, const char *buf);

/* V4 config is the preferred one, used internally, non-ASIC-specific */
typedef struct {
	uint16_t core_voltage; // Millivolts
	uint16_t clock_freq; // Clock frequency in MHz (or clock level 30-48 for Bitfury internal clock level)
	uint8_t clock_div2;	 // Apply the /2 clock divider (both internal and external), where available
	uint8_t use_ext_clock; // Flag. Ignored on boards without external clocks
} BoardConfig;

typedef struct
{
	uint16_t chip_id;
	uint8_t increase_clock;
} AutoTuneRequest;

#define SZ_SERIALISED_AUTOTUNEREQUEST 3
static void serialise_autotune_request(char *buf, AutoTuneRequest *aq);

#define CONFIG_PW1 (1<<0)
#define CONFIG_PW2 (1<<1)

// Possible core voltage settings on PW1 & PW2, used by legacy V3 config only
#define CONFIG_CORE_065V 0
#define CONFIG_CORE_075V CONFIG_PW2
#define CONFIG_CORE_085V CONFIG_PW1
#define CONFIG_CORE_095V (CONFIG_PW1|CONFIG_PW2)

/* V3 config is for backwards compatibility with older firmwares */
typedef struct {
	uint8_t core_voltage; // Set to flags defined above
	uint8_t int_clock_level; // Clock level (30-48 without divider), see asic.c for details
	uint8_t clock_div2;	// Apply the /2 clock divider (both internal and external)
	uint8_t use_ext_clock; // Ignored on boards without external clocks
	uint16_t ext_clock_freq;
 } BoardConfigV3;

#define SZ_SERIALISED_BOARDCONFIG 6
static void serialise_board_configV4(char *buf, BoardConfig *boardconfig);
static void serialise_board_configV3(char *buf, BoardConfigV3 *boardconfig);

typedef struct {
	uint8_t protocol_version;
	char product[8];
	uint32_t serial;
	uint8_t num_chips;
	uint16_t capabilities;
} Identity;

/* Capabilities flags known to cgminer */
#define CAP_TEMP (1<<0)
#define CAP_EXT_CLOCK (1<<1)
#define CAP_IS_AVALON (1<<2)
#define CAP_LIMITER_REMOVED (1<<3)

#define SZ_SERIALISED_IDENTITY 16
static void deserialise_identity(Identity *identity, const char *buf);

// Hashable structure of per-device config settings
typedef struct {
	char key[9];
	BoardConfig config;
	UT_hash_handle hh;
} config_setting;

static config_setting *settings;

static void drillbit_empty_buffer(struct cgpu_info *drillbit);

/* Automatic tuning parameters */
static uint32_t auto_every = 100;
static uint32_t auto_good = 1;
static uint32_t auto_bad = 3;
static uint32_t auto_max = 10;

/* Return a pointer to the chip_info structure for a given chip id, or NULL otherwise */
static struct drillbit_chip_info *find_chip(struct drillbit_info *info, uint16_t chip_id) {
	int i;

	for (i = 0; i < info->num_chips; i++) {
		if (info->chips[i].chip_id == chip_id)
			return &info->chips[i];
	}
	return NULL;
}

/* Read a fixed size buffer back from USB, returns true on success */
static bool usb_read_fixed_size(struct cgpu_info *drillbit, void *result, size_t result_size, int timeout, enum usb_cmds command_name) {
	char *res = (char *)result;
	int ms_left;
	size_t count;
	struct timeval tv_now, tv_start;
	int amount;

	cgtime(&tv_start);
	ms_left = timeout;

	amount = 1;
	count = 0;
	while (count < result_size && ms_left > 0) {
		usb_read_timeout(drillbit, &res[count], result_size-count, &amount, ms_left, command_name);
		count += amount;
		cgtime(&tv_now);
		ms_left = timeout - ms_tdiff(&tv_now, &tv_start);
	}
	if (count == result_size) {
		return true;
	}
	drvlog(LOG_ERR, "Read incomplete fixed size packet - got %d bytes / %d (timeout %d)",
			(int)count, (int)result_size, timeout);
	drillbit_empty_buffer(drillbit);
	return false;
}

static bool usb_read_simple_response(struct cgpu_info *drillbit, char command, enum usb_cmds command_name);

/* Write a simple one-byte command and expect a simple one-byte response
   Returns true on success
*/
static bool usb_send_simple_command(struct cgpu_info *drillbit, char command, enum usb_cmds command_name) {
	int amount;

	usb_write_timeout(drillbit, &command, 1, &amount, TIMEOUT, C_BF_REQWORK);
	if (amount != 1) {
		drvlog(LOG_ERR, "Failed to write command %c", command);
		return false;
	}
	return usb_read_simple_response(drillbit, command, command_name);
}

/* Read a simple single-byte response and check it matches the correct command character
   Return true on success
*/
static bool usb_read_simple_response(struct cgpu_info *drillbit, char command, enum usb_cmds command_name) {
	int amount;
	char response;
	/* Expect a single byte, matching the command, as acknowledgement */
	usb_read_timeout(drillbit, &response, 1, &amount, TIMEOUT, command_name);
	if (amount != 1) {
		drvlog(LOG_ERR, "Got no response to command %c", command);
		return false;
	}
	if (response != command) {
		drvlog(LOG_ERR, "Got unexpected response %c to command %c", response, command);	   
		return false;
	}
	return true;
}

#define EMPTY_TIMEOUT 5

static void drillbit_empty_buffer(struct cgpu_info *drillbit)
{
	char buf[512];
	int amount;

	do {
		usb_read_timeout(drillbit, buf, sizeof(buf), &amount, EMPTY_TIMEOUT, C_BF_FLUSH);
	} while (amount);
}

static void drillbit_open(struct cgpu_info *drillbit)
{
	drillbit_empty_buffer(drillbit);
}

static void drillbit_close(struct cgpu_info *drillbit)
{
	struct drillbit_info *info = drillbit->device_data;
	drillbit_empty_buffer(drillbit);
	if (info->chips)
		free(info->chips);
}

static void drillbit_identify(struct cgpu_info *drillbit)
{
	usb_send_simple_command(drillbit, 'L', C_BF_IDENTIFY);
}

#define ID_TIMEOUT 1000

static bool drillbit_getinfo(struct cgpu_info *drillbit, struct drillbit_info *info)
{
	int err;
	int amount;
	char buf[SZ_SERIALISED_IDENTITY];
	Identity identity;

	drillbit_empty_buffer(drillbit);
	err = usb_write_timeout(drillbit, "I", 1, &amount, TIMEOUT, C_BF_REQINFO);
	if (err) {
		drvlog(LOG_INFO, "Failed to write REQINFO");
		return false;
	}
	// can't call usb_read_fixed_size here as stats not initialised
	err = usb_read_timeout(drillbit, buf, SZ_SERIALISED_IDENTITY, &amount, ID_TIMEOUT, C_BF_GETINFO);
	if (err) {
		drvlog(LOG_ERR, "Failed to read GETINFO");
		return false;
	}
	if (amount != SZ_SERIALISED_IDENTITY) {
		drvlog(LOG_ERR, "Getinfo received %d bytes instead of %d",
		       amount, (int)sizeof(Identity));
		return false;
	}
	deserialise_identity(&identity, buf);

	// sanity checks on the identity buffer we get back
	if (strlen(identity.product) == 0 || identity.serial == 0 || identity.num_chips == 0) {
		drvlog(LOG_ERR, "Got invalid contents for GETINFO identity response");
		return false;
	}

	const int MIN_VERSION = 2;
	const int MAX_VERSION = 4;
	if (identity.protocol_version < MIN_VERSION) {
		drvlog(LOG_ERR, "Unknown device protocol version %d.", identity.protocol_version);
		return false;
	}
	if (identity.protocol_version > MAX_VERSION) {
		drvlog(LOG_ERR, "Device firmware uses newer Drillbit protocol %d. We only support up to %d. Find a newer cgminer!", identity.protocol_version, MAX_VERSION);
		return false;
	}

	if (identity.protocol_version == 2 && identity.num_chips == 1) {
		// Production firmware Thumbs don't set any capability bits, so fill in the EXT_CLOCK one
		identity.capabilities = CAP_EXT_CLOCK;
	}

	// load identity data into device info structure
	info->protocol_version = identity.protocol_version;
	if (strncmp(identity.product, "DRILLBIT", sizeof(identity.product)) == 0) {
		// Hack: first production firmwares all described themselves as DRILLBIT, so fill in the gaps
		if (identity.num_chips == 1)
			strcpy(info->product, "Thumb");
		else
			strcpy(info->product, "Eight");
	} else {
		memcpy(info->product, identity.product, sizeof(identity.product));
	}
	info->serial = identity.serial;
	info->num_chips = identity.num_chips;
	info->capabilities = identity.capabilities;

	drvlog(LOG_INFO, "Getinfo returned version %d, product %s serial %08x num_chips %d",
	       info->protocol_version, info->product, info->serial, info->num_chips);

	drillbit_empty_buffer(drillbit);
	return true;
}

static bool drillbit_reset(struct cgpu_info *drillbit)
{
	struct drillbit_info *info = drillbit->device_data;
	struct drillbit_chip_info *chip;
	int i, k, res;

	res = usb_send_simple_command(drillbit, 'R', C_BF_REQRESET);

	for (i = 0; i < info->num_chips; i++) {
		chip = &info->chips[i];
		chip->state = IDLE;
		chip->work_sent_count = 0;
		for (k = 0; k < WORK_HISTORY_LEN-1; k++) {
			if (chip->current_work[k]) {
				work_completed(drillbit, chip->current_work[k]);
				chip->current_work[k] = NULL;
			}
		}
	}

	drillbit_empty_buffer(drillbit);
	return res;
}

static config_setting *find_settings(struct cgpu_info *drillbit)
{
	struct drillbit_info *info = drillbit->device_data;
	config_setting *setting;
	char search_key[9];

	if (!settings) {
		drvlog(LOG_INFO, "Keeping onboard defaults for device %s (serial %08x)",
			info->product, info->serial);
		return NULL;
	}

	// Search by serial
	sprintf(search_key, "%08x", info->serial);
	HASH_FIND_STR(settings, search_key, setting);
	if (setting) {
		drvlog(LOG_INFO, "Using serial specific settings for serial %s", search_key);
		return setting;
	}

	// Search by DRBxxx
	snprintf(search_key, 9, "DRB%d", drillbit->device_id);
	HASH_FIND_STR(settings, search_key, setting);
	if (setting) {
		drvlog(LOG_INFO, "Using device_id specific settings for device");
		return setting;
	}

	// Failing that, search by product name
	HASH_FIND_STR(settings, info->product, setting);
	if (setting) {
		drvlog(LOG_INFO, "Using product-specific settings for device %s", info->product);
		return setting;
	}

	// Search by "short" product name
	snprintf(search_key, 9, "%c%d", info->product[0], info->num_chips);
	HASH_FIND_STR(settings, search_key, setting);
	if (setting) {
		drvlog(LOG_INFO, "Using product-specific settings for device %s", info->product);
		return setting;
	}

	// Check for a generic/catchall drillbit-options argument (key set to NULL)
	search_key[0] = 0;
	HASH_FIND_STR(settings, search_key, setting);
	if (setting) {
		drvlog(LOG_INFO, "Using non-specific settings for device %s (serial %08x)", info->product,
			info->serial);
		return setting;
	}

	drvlog(LOG_WARNING, "Keeping onboard defaults for device %s (serial %08x)",
		info->product, info->serial);
	return NULL;
}

static void drillbit_send_config(struct cgpu_info *drillbit)
{
	struct drillbit_info *info = drillbit->device_data;
	int amount;
	char buf[SZ_SERIALISED_BOARDCONFIG+1];
	config_setting *setting;
	BoardConfigV3 v3_config;

	// Find the relevant board config
	setting = find_settings(drillbit);
	if (!setting)
		return; // Don't update board config from defaults
	drvlog(LOG_NOTICE, "Config: %s:%d:%d:%d Serial: %08x",
	       setting->config.use_ext_clock ? "ext" : "int",
	       setting->config.clock_freq,
	       setting->config.clock_div2 ? 2 : 1,
	       setting->config.core_voltage,
	       info->serial);

	if (setting->config.use_ext_clock && !(info->capabilities & CAP_EXT_CLOCK)) {
		drvlog(LOG_WARNING, "Chosen configuration specifies external clock but this device (serial %08x) has no external clock!", info->serial);
	}

	if (info->protocol_version <= 3) {
		/* Make up a backwards compatible V3 config structure to send to the miner */
		if (setting->config.core_voltage >= 950)
			v3_config.core_voltage = CONFIG_CORE_095V;
		else if (setting->config.core_voltage >= 850)
			v3_config.core_voltage = CONFIG_CORE_085V;
		else if (setting->config.core_voltage >= 750)
			v3_config.core_voltage = CONFIG_CORE_075V;
		else
			v3_config.core_voltage = CONFIG_CORE_065V;
		if (setting->config.clock_freq > 64)
			v3_config.int_clock_level = setting->config.clock_freq / 5;
		else
			v3_config.int_clock_level = setting->config.clock_freq;
		v3_config.clock_div2 = setting->config.clock_div2;
		v3_config.use_ext_clock = setting->config.use_ext_clock;
		v3_config.ext_clock_freq = setting->config.clock_freq;
		serialise_board_configV3(&buf[1], &v3_config);
	} else {
		serialise_board_configV4(&buf[1], &setting->config);
	}
	buf[0] = 'C';
	usb_write_timeout(drillbit, buf, sizeof(buf), &amount, TIMEOUT, C_BF_CONFIG);

	/* Expect a single 'C' byte as acknowledgement */
	usb_read_simple_response(drillbit, 'C', C_BF_CONFIG); // TODO: verify response
}

static void drillbit_updatetemps(struct thr_info *thr)
{
	struct cgpu_info *drillbit = thr->cgpu;
	struct drillbit_info *info = drillbit->device_data;
	char cmd;
	int amount;
	uint16_t temp;
	struct timeval tv_now;

	if (!(info->capabilities & CAP_TEMP))
		return;

	cgtime(&tv_now);
	if (ms_tdiff(&tv_now, &info->tv_lasttemp) < 1000)
		return; // Only update temps once a second
	info->tv_lasttemp = tv_now;

	cmd = 'T';
	usb_write_timeout(drillbit, &cmd, 1, &amount, TIMEOUT, C_BF_GETTEMP);

	if (!usb_read_fixed_size(drillbit, &temp, sizeof(temp), TIMEOUT, C_BF_GETTEMP)) {
		drvlog(LOG_ERR, "Got no response to request for current temperature");
		return;
	}

	drvlog(LOG_INFO, "Got temperature reading %d.%dC", temp/10, temp%10);
	info->temp = temp;
	if (temp > info->max_temp)
		info->max_temp = temp;
}

static void drillbit_get_statline_before(char *buf, size_t bufsiz, struct cgpu_info *drillbit)
{
	struct drillbit_info *info = drillbit->device_data;

	if ((info->capabilities & CAP_TEMP) && info->temp != 0) {
		tailsprintf(buf, bufsiz, "%c%2d %.1fC max%.1fC",
					 info->product[0],
					 info->num_chips,
					 (float)(info->temp/10.0),
					 (float)(info->max_temp/10.0));
	} else {
		tailsprintf(buf, bufsiz, "%c%2d",
					 info->product[0],
					 info->num_chips);
	}
}


static bool drillbit_parse_options(__maybe_unused struct cgpu_info *drillbit)
{
	/* Read configuration options (currently global not per-ASIC or per-board) */
	if (settings != NULL)
		return true; // Already initialised

	char *next_opt = opt_drillbit_options;
	while (next_opt && strlen(next_opt)) {
		BoardConfig parsed_config;
		config_setting *new_setting;
		char key[9];
		int count, freq, clockdiv, voltage;
		char clksrc[4];

		// Try looking for an option tagged with a key, first
		count = sscanf(next_opt, "%8[^:]:%3s:%d:%d:%d", key,
			       clksrc, &freq, &clockdiv, &voltage);
		if (count < 5) {
			key[0] = 0;
			count = sscanf(next_opt, "%3s:%d:%d:%d",
				       clksrc, &freq, &clockdiv, &voltage);
			if (count < 4) {
				quithere(1, "Failed to parse drillbit-options. Invalid options string: '%s'", next_opt);
			}
		}

		if (clockdiv != 1 && clockdiv != 2) {
			quithere(1, "Invalid clock divider value %d. Valid values are 1 & 2.", clockdiv);
		}
		parsed_config.clock_div2 = count > 2 && clockdiv == 2;

		if (!strcmp("int",clksrc)) {
			parsed_config.use_ext_clock = 0;
		}
		else if (!strcmp("ext", clksrc)) {
			parsed_config.use_ext_clock = 1;
		} else
			quithere(1, "Invalid clock source. Valid choices are int, ext.");

		parsed_config.clock_freq = freq;
		parsed_config.core_voltage = voltage;

		// Add the new set of settings to the configuration choices hash table
		new_setting = (config_setting *)calloc(sizeof(config_setting), 1);
		memcpy(&new_setting->config, &parsed_config, sizeof(BoardConfig));
		memcpy(&new_setting->key, key, 8);
		config_setting *ignore;
		HASH_REPLACE_STR(settings, key, new_setting, ignore);

		// Look for next comma-delimited Drillbit option
		next_opt = strstr(next_opt, ",");
		if (next_opt)
			next_opt++;
	}

	if (opt_drillbit_auto) {
		sscanf(opt_drillbit_auto, "%d:%d:%d:%d",
			&auto_every, &auto_good, &auto_bad, &auto_max);
		if (auto_max < auto_bad) {
			quithere(1, "Bad drillbit-auto: MAX limit must be greater than BAD limit");
		}
		if (auto_bad < auto_good) {
			quithere(1, "Bad drillbit-auto: GOOD limit must be greater than BAD limit");
		}
	}

	return true;
}

static struct cgpu_info *drillbit_detect_one(struct libusb_device *dev, struct usb_find_devices *found)
{
	struct cgpu_info *drillbit;
	struct drillbit_info *info;
	int i;

	drillbit = usb_alloc_cgpu(&drillbit_drv, 1);
	drillbit->device_id = -1; // so drvlog() prints dname

	if (!drillbit_parse_options(drillbit))
		goto out;

	if (!usb_init(drillbit, dev, found))
		goto out;

	drvlog(LOG_INFO, "Device found at %s", drillbit->device_path);

	info = calloc(sizeof(struct drillbit_info), 1);
	if (!info)
		quit(1, "Failed to calloc info in %s", __func__);
	drillbit->device_data = info;

	drillbit_open(drillbit);

	/* Send getinfo request */
	if (!drillbit_getinfo(drillbit, info))
		goto out_close;

	/* TODO: Add detection for actual chip ids based on command/response,
	   not prefill assumption about chip layout based on info structure */
	info->chips = calloc(sizeof(struct drillbit_chip_info), info->num_chips);
	for (i = 0; i < info->num_chips; i++) {
		info->chips[i].chip_id = i;
		info->chips[i].auto_max = 999;
	}

	/* Send reset request */
	if (!drillbit_reset(drillbit))
		goto out_close;

	drillbit_identify(drillbit);
	drillbit_empty_buffer(drillbit);

	cgtime(&info->tv_lastchipinfo);

	if (!add_cgpu(drillbit))
		goto out_close;

	update_usb_stats(drillbit);

	if (info->capabilities & CAP_LIMITER_REMOVED) {
		drvlog(LOG_WARNING, "Recommended limits have been disabled on this board, take care when changing settings.");
	}

	drillbit_send_config(drillbit);

	drvlog(LOG_INFO, "Successfully initialised %s",
	       drillbit->device_path);

	return drillbit;
out_close:
	drillbit_close(drillbit);
	usb_uninit(drillbit);
out:
	drillbit = usb_free_cgpu(drillbit);
	return drillbit;
}

static void drillbit_detect(bool __maybe_unused hotplug)
{
	usb_detect(&drillbit_drv, drillbit_detect_one);
}

static uint32_t decnonce(uint32_t in)
{
	uint32_t out;

	/* First part load */
	out = (in & 0xFF) << 24; in >>= 8;

	/* Byte reversal */
	in = (((in & 0xaaaaaaaa) >> 1) | ((in & 0x55555555) << 1));
	in = (((in & 0xcccccccc) >> 2) | ((in & 0x33333333) << 2));
	in = (((in & 0xf0f0f0f0) >> 4) | ((in & 0x0f0f0f0f) << 4));

	out |= (in >> 2)&0x3FFFFF;

	/* Extraction */
	if (in & 1) out |= (1 << 23);
	if (in & 2) out |= (1 << 22);

	out -= 0x800004;
	return out;
}

#define BF_OFFSETS 3
static const uint32_t bf_offsets[] = {-0x800000, 0, -0x400000};

static bool drillbit_checkresults(struct thr_info *thr, struct work *work, uint32_t nonce)
{
	struct cgpu_info *drillbit = thr->cgpu;
	struct drillbit_info *info = drillbit->device_data;
	int i;

	if (info->capabilities & CAP_IS_AVALON) {
		if (test_nonce(work, nonce)) {
			submit_tested_work(thr, work);
			return true;
		}
	}
	else { /* Bitfury */
		nonce = decnonce(nonce);
		for (i = 0; i < BF_OFFSETS; i++) {
			if (test_nonce(work, nonce + bf_offsets[i])) {
				submit_tested_work(thr, work);
				return true;
			}
		}
	}
	return false;
}

/* Check if this ASIC should be tweaked up or down in clock speed */
static void drillbit_check_auto(struct thr_info *thr, struct drillbit_chip_info *chip)
{
	struct cgpu_info *drillbit = thr->cgpu;
	AutoTuneRequest request;
	char buf[SZ_SERIALISED_AUTOTUNEREQUEST+1];
	int amount;
	bool tune_up, tune_down;

	/*
	  Only check automatic tuning every "auto_every" work units,
	  or if the error count exceeds the 'max' count
	*/
	if (chip->success_auto + chip->error_auto < auto_every &&
	    (chip->error_auto < auto_max))
		return;

	tune_up = chip->error_auto < auto_good && chip->auto_delta < chip->auto_max;
	tune_down = chip->error_auto > auto_bad;


	drvlog(tune_up||tune_down ? LOG_NOTICE : LOG_DEBUG,
		"Chip id %d has %d/%d error rate %s", chip->chip_id, chip->error_auto,
		chip->error_auto + chip->success_auto,
		tune_up ? " - tuning up" : tune_down ? " - tuning down" : " - no change");

	if (tune_up || tune_down) {
		/* Value should be tweaked */
		buf[0] = 'A';
		request.chip_id = chip->chip_id;
		request.increase_clock = tune_up;
		serialise_autotune_request(&buf[1], &request);
		usb_write_timeout(drillbit, buf, sizeof(buf), &amount, TIMEOUT, C_BF_AUTOTUNE);
		usb_read_simple_response(drillbit, 'A', C_BF_AUTOTUNE);
		if (tune_up) {
			chip->auto_delta++;
		} else {
			chip->auto_delta--;
			if (chip->error_auto >= auto_max
				&& chip->success_count + chip->error_count > auto_every) {
				drvlog(LOG_ERR, "Chip id %d capping auto delta at max %d",chip->chip_id,
					chip->auto_delta);
				chip->auto_max = chip->auto_delta;
			}
		}
	}

	chip->success_auto = 0;
	chip->error_auto = 0;
}

// Check and submit back any pending work results from firmware,
// returns number of successful results found
static int check_for_results(struct thr_info *thr)
{
	struct cgpu_info *drillbit = thr->cgpu;
	struct drillbit_info *info = drillbit->device_data;
	struct drillbit_chip_info *chip;
	char cmd;
	int amount, i, k, found;
	uint8_t j;
	int successful_results = 0;
	uint32_t result_count;
	char buf[SZ_SERIALISED_WORKRESULT];
	WorkResult *responses = NULL;
	WorkResult *response;

	if (unlikely(thr->work_restart))
		goto cleanup;

	// Send request for completed work
	cmd = 'E';
	usb_write_timeout(drillbit, &cmd, 1, &amount, TIMEOUT, C_BF_GETRES);

	// Receive count for work results
	if (!usb_read_fixed_size(drillbit, &result_count, sizeof(result_count), TIMEOUT, C_BF_GETRES)) {
		drvlog(LOG_ERR, "Got no response to request for work results");
		goto cleanup;
	}
	if (unlikely(drillbit->usbinfo.nodev))
		goto cleanup;
	if (result_count)
		drvlog(LOG_DEBUG, "Result count %d",result_count);

	if (result_count > 1024) {
		drvlog(LOG_ERR, "Got implausible result count %d - treating as error!", result_count);
		goto cleanup;
	}

	if (result_count == 0) {
		// Short circuit reading any work results
		return 0;
	}

	responses = calloc(result_count, sizeof(WorkResult));

	// Receive work results (0 or more) into buffer
	for (j = 0; j < result_count; j++) {
		if (unlikely(drillbit->usbinfo.nodev))
			goto cleanup;
		if (!usb_read_fixed_size(drillbit, buf, SZ_SERIALISED_WORKRESULT, TIMEOUT, C_BF_GETRES)) {
			drvlog(LOG_ERR, "Failed to read response data packet idx %d count 0x%x", j, result_count);
			drillbit_empty_buffer(drillbit);
			goto cleanup;
		}
		deserialise_work_result(&responses[j], buf);
	}

	for (j = 0; j < result_count; j++) {
		if (unlikely(thr->work_restart))
			goto cleanup;

		response = &responses[j];
		drvlog(LOG_DEBUG, "Got response packet chip_id %d nonces %d is_idle %d", response->chip_id, response->num_nonces, response->is_idle);
		chip = find_chip(info, response->chip_id);
		if (!chip) {
			drvlog(LOG_ERR, "Got work result for unknown chip id %d", response->chip_id);
			drillbit_empty_buffer(drillbit);
			continue;
		}
		if (chip->state == IDLE) {
			drvlog(LOG_WARNING, "Got spurious work results for idle ASIC %d", response->chip_id);
		}
		if (response->num_nonces > MAX_RESULTS) {
			drvlog(LOG_ERR, "Got invalid number of result nonces (%d) for chip id %d", response->num_nonces, response->chip_id);
			drillbit_empty_buffer(drillbit);
			goto cleanup;
		}

		found = false;
		for (i = 0; i < response->num_nonces; i++) {
			if (unlikely(thr->work_restart))
				goto cleanup;
			for (k = 0; k < WORK_HISTORY_LEN; k++) {
				/* NB we deliberately check all results against all work because sometimes ASICs seem to give multiple "valid" nonces,
				   and this seems to avoid some result that would otherwise be rejected by the pool.
				*/
				if (chip->current_work[k] && drillbit_checkresults(thr, chip->current_work[k], response->nonce[i])) {
					chip->success_count++;
					chip->success_auto++;
					successful_results++;
					found = true;
				}
			}
		}
		drvlog(LOG_DEBUG, "%s nonce %08x", (found ? "Good":"Bad"), response->num_nonces ? response->nonce[0] : 0);
		if (!found && chip->state != IDLE && response->num_nonces > 0) {
			/* all nonces we got back from this chip were invalid */
			inc_hw_errors(thr);
			chip->error_count++;
			chip->error_auto++;
		}
		if (chip->state == WORKING_QUEUED && !response->is_idle)
			chip->state = WORKING_NOQUEUED; // Time to queue up another piece of "next work"
		else
			chip->state = IDLE; // Uh-oh, we're totally out of work for this ASIC!

		if (opt_drillbit_auto && info->protocol_version >= 4)
			drillbit_check_auto(thr, chip);
	}

cleanup:
	if (responses)
		free(responses);
	return successful_results;
}

static void drillbit_send_work_to_chip(struct thr_info *thr, struct drillbit_chip_info *chip)
{
	struct cgpu_info *drillbit = thr->cgpu;
	struct work *work;
	char buf[SZ_SERIALISED_WORKREQUEST+1];
	int amount, i;

	/* Get some new work for the chip */
	work = get_queue_work(thr, drillbit, thr->id);
	if (unlikely(thr->work_restart)) {
		work_completed(drillbit, work);
		return;
	}

	drvlog(LOG_DEBUG, "Sending work to chip_id %d", chip->chip_id);
	serialise_work_request(&buf[1], chip->chip_id, work);

	/* Send work to cgminer */
	buf[0] = 'W';
	usb_write_timeout(drillbit, buf, sizeof(buf), &amount, TIMEOUT, C_BF_REQWORK);

	/* Expect a single 'W' byte as acknowledgement */
	usb_read_simple_response(drillbit, 'W', C_BF_REQWORK);
	if (chip->state == WORKING_NOQUEUED)
		chip->state = WORKING_QUEUED;
	else
		chip->state = WORKING_NOQUEUED;

	if (unlikely(thr->work_restart)) {
		work_completed(drillbit, work);
		return;
	}

	// Read into work history
	if (chip->current_work[0])
		work_completed(drillbit, chip->current_work[0]);
	for (i = 0; i < WORK_HISTORY_LEN-1; i++)
		chip->current_work[i] = chip->current_work[i+1];
	chip->current_work[WORK_HISTORY_LEN-1] = work;
	cgtime(&chip->tv_start);

	chip->work_sent_count++;
}

static int64_t drillbit_scanwork(struct thr_info *thr)
{
	struct cgpu_info *drillbit = thr->cgpu;
	struct drillbit_info *info = drillbit->device_data;
	struct drillbit_chip_info *chip;
	struct timeval tv_now;
	int amount, i, j, ms_diff, result_count = 0, sent_count = 0;;
	char buf[200];

	/* send work to an any chip without queued work */
	for (i = 0; i < info->num_chips && sent_count < 8; i++) {
		if (info->chips[i].state != WORKING_QUEUED) {
			drillbit_send_work_to_chip(thr, &info->chips[i]);
			sent_count++;
		}
		if (unlikely(thr->work_restart) || unlikely(drillbit->usbinfo.nodev))
			goto cascade;
	}

	/* check for any chips that have timed out on sending results */
	cgtime(&tv_now);
	for (i = 0; i < info->num_chips; i++) {
		if (info->chips[i].state == IDLE)
			continue;
		ms_diff = ms_tdiff(&tv_now, &info->chips[i].tv_start);
		if (ms_diff > RESULT_TIMEOUT) {
			if (info->chips[i].work_sent_count > 4) {
				/* Only count ASIC timeouts after the pool has started to send work in earnest,
				   some pools can create unusual delays early on */
				drvlog(LOG_ERR, "Timing out unresponsive ASIC %d", info->chips[i].chip_id);
				info->chips[i].timeout_count++;
				info->chips[i].error_auto++;
			}
			info->chips[i].state = IDLE;
			drillbit_send_work_to_chip(thr, &info->chips[i]);
		}
		if (unlikely(thr->work_restart) || unlikely(drillbit->usbinfo.nodev))
			goto cascade;
	}

	/* Check for results */
	result_count = check_for_results(thr);

	/* Print a per-chip info line every 30 seconds */
	cgtime(&tv_now);
	if (opt_log_level <= LOG_INFO && ms_tdiff(&tv_now, &info->tv_lastchipinfo) > 30000) {
		/* TODO: this output line may get truncated (max debug is 256 bytes) once we get more
		   chips in a single device
		*/
		amount = sprintf(buf, "%s %d: S/E/T", drillbit->drv->name, drillbit->device_id);
		if (amount > 0) {
			for (i = 0; i < info->num_chips; i++) {
				chip= &info->chips[i];
				j = snprintf(&buf[amount], sizeof(buf)-(size_t)amount, "%u:%u/%u/%u",
					     chip->chip_id, chip->success_count, chip->error_count,
					     chip->timeout_count);
				if (j < 0)
					break;
				amount += j;
				if ((size_t)amount >= sizeof(buf))
					break;
			}
			drvlog(LOG_INFO, "%s", buf);
			cgtime(&info->tv_lastchipinfo);
		}
	}

	drillbit_updatetemps(thr);

cascade:
	if (unlikely(drillbit->usbinfo.nodev)) {
		drvlog(LOG_WARNING, "Device disappeared, disabling thread");
		return -1;
	}

	if (unlikely(thr->work_restart)) {
		/* Issue an ASIC reset as we won't be coming back for any of these results */
		drvlog(LOG_DEBUG, "Received work restart, resetting ASIC");
		drillbit_reset(drillbit);
	}

	return 0xffffffffULL * result_count;
}

static struct api_data *drillbit_api_stats(struct cgpu_info *cgpu)
{
	struct drillbit_info *info = cgpu->device_data;
	struct api_data *root = NULL;
	char serial[16];
	int version;

	version = info->protocol_version;
	root = api_add_int(root, "Protocol Version", &version, true);
	root = api_add_string(root, "Product", info->product, false);
	sprintf(serial, "%08x", info->serial);
	root = api_add_string(root, "Serial", serial, true);
	root = api_add_uint8(root, "ASIC Count", &info->num_chips, true);
	if (info->capabilities & CAP_TEMP) {
		float temp = (float)info->temp/10;
		root = api_add_temp(root, "Temp", &temp, true);
		temp = (float)info->max_temp/10;
		root = api_add_temp(root, "Temp Max", &temp, true);
	}

	return root;
}

static void drillbit_reinit(struct cgpu_info  *drillbit)
{
	drillbit_close(drillbit);
	drillbit_open(drillbit);
	drillbit_reset(drillbit);
}

static void drillbit_shutdown(struct thr_info *thr)
{
	struct cgpu_info *drillbit = thr->cgpu;

	drillbit_close(drillbit);
}

/* Currently hardcoded to BF1 devices */
struct device_drv drillbit_drv = {
	.drv_id = DRIVER_drillbit,
	.dname = "Drillbit",
	.name = "DRB",
	.drv_detect = drillbit_detect,
	.hash_work = &hash_driver_work,
	.scanwork = drillbit_scanwork,
	.get_api_stats = drillbit_api_stats,
	.get_statline_before = drillbit_get_statline_before,
	.reinit_device = drillbit_reinit,
	.thread_shutdown = drillbit_shutdown,
	.identify_device = drillbit_identify,
};


/* Structure serialisation/deserialisation */

#define SERIALISE(FIELD) do {					\
		memcpy(&buf[offset], &FIELD, sizeof(FIELD));	\
		offset += sizeof(FIELD);			\
	} while (0)

#define DESERIALISE(FIELD) do {					\
		memcpy(&FIELD, &buf[offset], sizeof(FIELD));	\
		offset += sizeof(FIELD);			\
	} while (0)

static void serialise_work_request(char *buf, uint16_t chip_id, const struct work *work)
{
	size_t offset = 0;
	SERIALISE(chip_id);
	memcpy(&buf[offset], work->midstate, 32);
	offset += 32;
	memcpy(&buf[offset], work->data + 64, 12);
	//offset += 12;
}

static void deserialise_work_result(WorkResult *wr, const char *buf)
{
	int i;
	size_t offset = 0;
	DESERIALISE(wr->chip_id);
	DESERIALISE(wr->num_nonces);
	DESERIALISE(wr->is_idle);
	for (i = 0; i < MAX_RESULTS; i++)
		DESERIALISE(wr->nonce[i]);
}

static void serialise_board_configV3(char *buf, BoardConfigV3 *bc)
{
	size_t offset = 0;
	SERIALISE(bc->core_voltage);
	SERIALISE(bc->int_clock_level);
	SERIALISE(bc->clock_div2);
	SERIALISE(bc->use_ext_clock);
	SERIALISE(bc->ext_clock_freq);
}

static void serialise_board_configV4(char *buf, BoardConfig *bc)
{
	size_t offset = 0;
	SERIALISE(bc->core_voltage);
	SERIALISE(bc->clock_freq);
	SERIALISE(bc->clock_div2);
	SERIALISE(bc->use_ext_clock);
}

static void serialise_autotune_request(char *buf, AutoTuneRequest *aq)
{
	size_t offset = 0;
	SERIALISE(aq->chip_id);
	SERIALISE(aq->increase_clock);
}

static void deserialise_identity(Identity *id, const char *buf)
{
	size_t offset = 0;
	DESERIALISE(id->protocol_version);
	DESERIALISE(id->product);
	DESERIALISE(id->serial);
	DESERIALISE(id->num_chips);
	DESERIALISE(id->capabilities);
}
