
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

/* Wait longer 1/3 longer than it would take for a full nonce range */
#define TIMEOUT 2000
#define MAX_RESULTS 16 // max results from a single chip

#define drvlog(prio, fmt, ...) do { \
	if (opt_debug || prio != LOG_DEBUG) { \
		if (use_syslog || opt_log_output || prio <= opt_log_level) { \
			char tmp42[LOGBUFSIZ]; \
			snprintf(tmp42, sizeof(tmp42), "%s %d: "fmt,    \
			drillbit->drv->name, drillbit->device_id, ##__VA_ARGS__);       \
			_applog(prio, tmp42, false); \
		} \
	} \
} while (0)


/* Request and response structsfor firmware */

typedef struct
{
  uint16_t chip_id;
  uint8_t midstate[32];
  uint8_t data[12];
} WorkRequest;

#define SZ_SERIALISED_WORKREQUEST 46
static void serialise_work_request(uint8_t *buf, uint16_t chip_id, const struct work *wr);

typedef struct
{
  uint16_t chip_id;
  uint8_t num_nonces;
  uint8_t is_idle;
  uint32_t nonce[MAX_RESULTS];
} WorkResult;

#define SZ_SERIALISED_WORKRESULT (4+4*MAX_RESULTS)
static void deserialise_work_result(WorkResult *work_result, const uint8_t *buf);

#define CONFIG_PW1 (1<<0)
#define CONFIG_PW2 (1<<1)

// Possible core voltage settings on PW1 & PW2
#define CONFIG_CORE_065V 0
#define CONFIG_CORE_075V CONFIG_PW2
#define CONFIG_CORE_085V CONFIG_PW1
#define CONFIG_CORE_095V (CONFIG_PW1|CONFIG_PW2)

typedef struct
{
  uint8_t core_voltage; // Set to flags defined above
  uint8_t int_clock_level; // Clock level (30-48 without divider), see asic.c for details
  uint8_t clock_div2;      // Apply the /2 clock divider (both internal and external)
  uint8_t use_ext_clock; // Ignored on boards without external clocks
  uint16_t ext_clock_freq;
} BoardConfig;

#define SZ_SERIALISED_BOARDCONFIG 6
static void serialise_board_config(uint8_t *buf, const BoardConfig *boardconfig);

typedef struct
{
    uint8_t protocol_version;
    uint8_t product[8];
    uint32_t serial;
    uint8_t num_chips;
    uint16_t capabilities;
} Identity;

#define SZ_SERIALISED_IDENTITY 16
static void deserialise_identity(Identity *identity, const uint8_t *buf);

// Hashable structure of per-device config settings
typedef struct {
  uint8_t key[9];
  BoardConfig config;
  UT_hash_handle hh;
} config_setting;

/* Comparatively modest default settings */
static config_setting default_settings = {
 key: { 0 },
 config: {
  core_voltage: CONFIG_CORE_085V,
  use_ext_clock: 0,
  int_clock_level: 40,
  clock_div2: 0,
  ext_clock_freq: 200
 },
};

static config_setting *settings;

/* Return a pointer to the chip_info structure for a given chip id, or NULL otherwise */
static struct drillbit_chip_info *find_chip(struct drillbit_info *info, uint16_t chip_id) {
  int i;
  for(i = 0; i < info->num_chips; i++) {
    if(info->chips[i].chip_id == chip_id)
      return &info->chips[i];
  }
  return NULL;
}

/* Read a fixed size buffer back from USB, returns true on success */
static bool usb_read_fixed_size(struct cgpu_info *drillbit, void *result, size_t result_size, int timeout, enum usb_cmds command_name) {
  uint8_t *res = (uint8_t *)result;
  char *hex;
  int count, ms_left;
  struct timeval tv_now, tv_start;
  int amount;

  cgtime(&tv_start);
  ms_left = timeout;

  amount = 1;
  count = 0;
  while(count < result_size && ms_left > 0) {
    usb_read_timeout(drillbit, &res[count], result_size-count, &amount, ms_left, command_name);
    count += amount;
    cgtime(&tv_now);
    ms_left = timeout - ms_tdiff(&tv_now, &tv_start);
  }
  if(count == result_size) {
    return true;
  }
  drvlog(LOG_ERR, "Read incomplete fixed size packet - got %d bytes / %lu (timeout %d)", count, result_size, timeout);
  hex = bin2hex(res, count);
  drvlog(LOG_DEBUG, "%s", hex);
  free(hex);
  return false;
}

static bool usb_read_simple_response(struct cgpu_info *drillbit, char command, enum usb_cmds command_name);

/* Write a simple one-byte command and expect a simple one-byte response
   Returns true on success
*/
static bool usb_send_simple_command(struct cgpu_info *drillbit, char command, enum usb_cmds command_name) {
  int amount;
  usb_write(drillbit, &command, 1, &amount, C_BF_REQWORK);
  if(amount != 1) {
    drvlog(LOG_ERR, "Failed to write command %c",command);
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
  usb_read_timeout(drillbit, &response, 1, &amount, TIMEOUT, C_BF_GETRES);
  if(amount != 1) {
    drvlog(LOG_ERR, "Got no response to command %c",command);
    return false;
  }
  if(response != command) {
    drvlog(LOG_ERR, "Got unexpected response %c to command %c", response, command);    
    return false;
  }
  return true;
}

static void drillbit_empty_buffer(struct cgpu_info *drillbit)
{
	char buf[512];
	int amount;

	do {
          usb_read_timeout(drillbit, buf, 512, &amount, 5, C_BF_FLUSH);
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
  if(info->chips)
    free(info->chips);
}

static void drillbit_identify(struct cgpu_info *drillbit)
{
	int amount;

        usb_send_simple_command(drillbit, 'L', C_BF_IDENTIFY);
}

static bool drillbit_getinfo(struct cgpu_info *drillbit, struct drillbit_info *info)
{
	int amount, err;
        uint8_t buf[SZ_SERIALISED_IDENTITY];
        Identity identity;

	drillbit_empty_buffer(drillbit);
	err = usb_write(drillbit, "I", 1, &amount, C_BF_REQINFO);
	if (err) {
                drvlog(LOG_INFO, "Failed to write REQINFO");
                return false;
	}
        // can't call usb_read_fixed_size here as stats not initialised
	err = usb_read_timeout(drillbit, buf, SZ_SERIALISED_IDENTITY, &amount, 1000, C_BF_GETINFO);
	if (err) {
		drvlog(LOG_ERR, "Failed to read GETINFO");
		return false;
	}
	if (amount != SZ_SERIALISED_IDENTITY) {
		drvlog(LOG_ERR, "Getinfo received %d bytes instead of %lu",
                        amount, sizeof(Identity));
		return false;
	}
        deserialise_identity(&identity, buf);

        // sanity checks on the identity buffer we get back
        if(strlen(identity.product) == 0 || identity.serial == 0 || identity.num_chips == 0) {
          drvlog(LOG_ERR, "Got invalid contents for GETINFO identity response");
          return false;
        }

        const int MIN_VERSION = 2;
        const int MAX_VERSION = 2;
        if(identity.protocol_version < MIN_VERSION) {
          drvlog(LOG_ERR, "Unknown device protocol version %d.", identity.protocol_version);
          return false;
        }
        if(identity.protocol_version > MAX_VERSION) {
          drvlog(LOG_ERR, "Device firmware uses newer Drillbit protocol %d. We only support up to %d. Find a newer cgminer!", identity.protocol_version, MAX_VERSION);
          return false;
        }

        // load identity data into device info structure
	info->version = identity.protocol_version;
        if(strncmp(identity.product, "DRILLBIT", sizeof(identity.product)) == 0) {
          // Hack: first production firmwares all described themselves as DRILLBIT, so fill in the gaps
          if(identity.num_chips == 1)
            strcpy(info->product, "Thumb");
          else
            strcpy(info->product, "Eight");
        }
        else {
          memcpy(info->product, identity.product, sizeof(identity.product));
        }
        info->serial = identity.serial;
        info->num_chips = identity.num_chips;

	drvlog(LOG_INFO, "Getinfo returned version %d, product %s serial %08x num_chips %d",
                info->version, info->product, info->serial, info->num_chips);
	drillbit_empty_buffer(drillbit);
	return true;
}

static bool drillbit_reset(struct cgpu_info *drillbit)
{
	int amount, err;
        if(!usb_send_simple_command(drillbit, 'R', C_BF_REQRESET))
          return false;
        
	drillbit_empty_buffer(drillbit);
	return true;
}

static void drillbit_send_config(struct cgpu_info *drillbit)
{
  struct drillbit_info *info = drillbit->device_data;
  char cmd;
  int amount;
  uint8_t search_key[9];
  uint8_t buf[SZ_SERIALISED_BOARDCONFIG];
  config_setting *setting;

  // Find the relevant board config

  // Search by serial (8 character hex string)
  sprintf(search_key, "%08x", info->serial);
  HASH_FIND_STR(settings, search_key, setting);
  if(setting)  {
    drvlog(LOG_INFO, "Using unit-specific settings for serial %s", search_key);
  } 
  else {
    // Failing that, search by product name
    HASH_FIND_STR(settings, info->product, setting);
    if(setting) {
      drvlog(LOG_INFO, "Using product-specific settings for device %s", info->product);
    }
    else {
      // Failing that, return default/generic config (null key)
      search_key[0] = 0;
      HASH_FIND_STR(settings, search_key, setting);
      drvlog(LOG_INFO, "Using non-specific settings for device %s (serial %08x)", info->product,
             info->serial);
    }
  }

  drvlog(LOG_INFO, "Sending board configuration voltage=%d use_ext_clock=%d int_clock_level=%d clock_div2=%d ext_clock_freq=%d",
         setting->config.core_voltage, setting->config.use_ext_clock,
         setting->config.int_clock_level,
         setting->config.clock_div2, setting->config.ext_clock_freq);
  cmd = 'C';
  usb_write(drillbit, &cmd, 1, &amount, C_BF_REQWORK);

  serialise_board_config(buf, &setting->config);
  usb_write(drillbit, buf, SZ_SERIALISED_BOARDCONFIG, &amount, C_BF_CONFIG);

  /* Expect a single 'C' byte as acknowledgement */
  usb_read_simple_response(drillbit, 'C', C_BF_CONFIG); // TODO: verify response
}

static bool drillbit_parse_options()
{
  /* Read configuration options (currently global not per-ASIC or per-board) */
  if(settings != NULL)
    return true; // Already initialised

  // Start with the system-wide defaults
  HASH_ADD_STR( settings, key, (&default_settings) );

  char *next_opt = opt_drillbit_options;
  while (next_opt && strlen(next_opt)) {
    BoardConfig parsed_config;
    config_setting *new_setting;
    uint8_t key[9];
    int count, freq, clockdiv, voltage;
    char clksrc[4];

    // Try looking for an option tagged with a key, first
    count = sscanf(next_opt, "%8[^:]:%3s:%d:%d:%d", key,
                   clksrc, &freq, &clockdiv, &voltage);
    if(count < 5) {
      key[0] = 0;
      count = sscanf(next_opt, "%3s:%d:%d:%d",
                     clksrc, &freq, &clockdiv, &voltage);
      if(count < 4) {
        applog(LOG_ERR, "Failed to parse drillbit-options. Invalid options string: '%s'", next_opt);
        settings = NULL;
        return false;
      }
    }

    if(clockdiv != 1 && clockdiv != 2) {
      applog(LOG_ERR, "drillbit-options: Invalid clock divider value %d. Valid values are 1 & 2.", clockdiv);
      settings = NULL;
      return false;
    }
    parsed_config.clock_div2 = count > 2 && clockdiv == 2;

    if(!strcmp("int",clksrc)) {
      parsed_config.use_ext_clock = 0;
      if(freq < 0 || freq > 63) {
        applog(LOG_ERR, "drillbit-options: Invalid internal oscillator level %d. Recommended range is %s for this clock divider (possible is 0-63)", freq, parsed_config.clock_div2 ? "48-57":"30-48");
        settings = NULL;
        return false;
      }
      if(parsed_config.clock_div2 && (freq < 48 || freq > 57)) {
        applog(LOG_WARNING, "drillbit-options: Internal oscillator level %d outside recommended range 48-57.", freq);
      }
      if(!parsed_config.clock_div2 && (freq < 30 || freq > 48)) {
        applog(LOG_WARNING, "drillbit-options: Internal oscillator level %d outside recommended range 30-48.", freq);
      }
      parsed_config.int_clock_level = freq;
    }
    else if (!strcmp("ext", clksrc)) {
      parsed_config.use_ext_clock = 1;
      parsed_config.ext_clock_freq = freq;
      if(freq < 80 || freq > 230) {
        applog(LOG_WARNING, "drillbit-options: Warning: recommended external clock frequencies are 80-230MHz. Value %d may produce unexpected results.", freq);
      }
    }
    else {
      applog(LOG_ERR, "drillbit-options: Invalid clock source. Valid choices are int, ext.");
      return false;
    }

    switch(voltage) {
    case 650:
      voltage = CONFIG_CORE_065V;
      break;
    case 750:
      voltage = CONFIG_CORE_075V;
      break;
    case 850:
      voltage = CONFIG_CORE_085V;
      break;
    case 950:
      voltage = CONFIG_CORE_095V;
      break;
    default:
      applog(LOG_ERR, "drillbit-options: Invalid core voltage %d. Valid values 650,750,850,950mV)", voltage);
      return false;
    }
    parsed_config.core_voltage = voltage;

    // Add the new set of settings to the configuration choices hash table
    new_setting = (config_setting *)calloc(sizeof(config_setting), 1);
    memcpy(&new_setting->config, &parsed_config, sizeof(BoardConfig));
    memcpy(&new_setting->key, key, 8);
    config_setting *ignore;
    HASH_REPLACE_STR(settings, key, new_setting, ignore);

    // Look for next comma-delimited Drillbit option
    next_opt = strstr(next_opt, ",");
    if(next_opt)
      next_opt++;
  }
  return true;
}

static struct cgpu_info *drillbit_detect_one(struct libusb_device *dev, struct usb_find_devices *found)
{
	struct cgpu_info *drillbit;
	struct drillbit_info *info;
        int i;

        if (!drillbit_parse_options())
          return false; // Bit of a hack doing this here, should do it somewhere else

	drillbit = usb_alloc_cgpu(&drillbit_drv, 1);

	if (!usb_init(drillbit, dev, found))
		goto out;
	drvlog(LOG_INFO, "Found at %s", drillbit->device_path);

	info = calloc(sizeof(struct drillbit_info), 1);
	if (!info)
		quit(1, "Failed to calloc info in drillbit_detect_one");
	drillbit->device_data = info;

	drillbit_open(drillbit);

	/* Send getinfo request */
	if (!drillbit_getinfo(drillbit, info))
		goto out_close;

	/* Send reset request */
	if (!drillbit_reset(drillbit))
		goto out_close;

	drillbit_identify(drillbit);
	drillbit_empty_buffer(drillbit);

        /* TODO: Add detection for actual chip ids based on command/response,
           not prefill assumption about chip layout based on info structure */
        info->chips = calloc(sizeof(struct drillbit_chip_info), info->num_chips);
        for(i = 0; i < info->num_chips; i++) {
          info->chips[i].chip_id = i;
        }
        cgtime(&info->tv_lastchipinfo);

	if (!add_cgpu(drillbit))
		goto out_close;

	update_usb_stats(drillbit);

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

#define BT_OFFSETS 3
const uint32_t bf_offsets[] = {-0x800000, 0, -0x400000};

static bool drillbit_checkresults(struct thr_info *thr, struct work *work, uint32_t nonce)
{
	int i;

        nonce = decnonce(nonce);
	for (i = 0; i < BT_OFFSETS; i++) {
		if (test_nonce(work, nonce + bf_offsets[i])) {
			submit_tested_work(thr, work);
			return true;
		}
	}
	return false;
}

// Check and submit back any pending work results from firmware,
// returns number of successful results found
static int check_for_results(struct thr_info *thr)
{
  struct cgpu_info *drillbit = thr->cgpu;
  struct drillbit_info *info = drillbit->device_data;
  struct drillbit_chip_info *chip;
  char cmd;
  int amount, i, j, k;
  int successful_results, found;
  uint32_t result_count;
  uint8_t buf[SZ_SERIALISED_WORKRESULT];
  WorkResult response;

  successful_results = 0;

  // Send request for completed work
  cmd = 'E';
  usb_write(drillbit, &cmd, 1, &amount, C_BF_GETRES);

  // Receive count for work results
  if(!usb_read_fixed_size(drillbit, &result_count, sizeof(result_count), TIMEOUT, C_BF_GETRES)) {
    drvlog(LOG_ERR, "Got no response to request for work results");
    return false;
  }
  if(result_count)
    drvlog(LOG_DEBUG, "Result count %d",result_count);

  // Receive work results (0 or more)
  for(j = 0; j < result_count; j++) {

    if(!usb_read_fixed_size(drillbit, buf, SZ_SERIALISED_WORKRESULT, TIMEOUT, C_BF_GETRES)) {
      drvlog(LOG_ERR, "Failed to read response data packet idx %d count 0x%x", j, result_count);
      return 0;
    }
    deserialise_work_result(&response, buf);

    if (unlikely(thr->work_restart))
      goto cleanup;
    drvlog(LOG_DEBUG, "Got response packet chip_id %d nonces %d is_idle %d", response.chip_id, response.num_nonces, response.is_idle);
    chip = find_chip(info, response.chip_id);
    if(!chip) {
      drvlog(LOG_ERR, "Got work result for unknown chip id %d", response.chip_id);
      continue;
    }
    if(chip->state == IDLE) {
      drvlog(LOG_WARNING, "Got spurious work results for idle ASIC %d", response.chip_id);
    }
    for(i = 0; i < response.num_nonces; i++) {
      found = false;
      for(k = 0; k < WORK_HISTORY_LEN; k++) {
        if (chip->current_work[k] && drillbit_checkresults(thr, chip->current_work[k], response.nonce[i])) {
          chip->success_count++;
          successful_results++;
          found = true;
          break;
        }
      }
      if(!found && chip->state != IDLE) {
        /* all nonces we got back from this chip were invalid */
        inc_hw_errors(thr);
        chip->error_count++;
      }
    }
    if(chip->state == WORKING_QUEUED && !response.is_idle)
      chip->state = WORKING_NOQUEUED; // Time to queue up another piece of "next work"
    else
      chip->state = IDLE; // Uh-oh, we're totally out of work for this ASIC!
  }

 cleanup:
  return successful_results;
}

static int64_t drillbit_scanwork(struct thr_info *thr)
{
	struct cgpu_info *drillbit = thr->cgpu;
	struct drillbit_info *info = drillbit->device_data;
        struct work *work;
        struct drillbit_chip_info *chip = NULL;
	struct timeval tv_now, tv_start;
	int amount, i, j;
	int ms_diff;
        uint8_t cmd;
        int result_count;
        uint8_t buf[200]; // also larger than SZ_SERIALISED_WORKREQUEST
        char *tmp;

        // check for outstanding results
        result_count = check_for_results(thr);
        for(i = 0; i < info->num_chips; i++) {
                if(info->chips[i].state != WORKING_QUEUED) {
                        chip = &info->chips[i];
                        break;
                }
        }

        // check for any chips that have timed out on sending results
        cgtime(&tv_now);
        for(i = 0; i < info->num_chips; i++) {
          if(info->chips[i].state == IDLE)
            continue;
          ms_diff = ms_tdiff(&tv_now, &info->chips[i].tv_start);
          if(ms_diff > TIMEOUT) {
            drvlog(LOG_ERR, "Timing out unresponsive ASIC %d", info->chips[i].chip_id);
            info->chips[i].state = IDLE;
            info->chips[i].timeout_count++;
            chip = &info->chips[i];
          }
        }

        if(chip == NULL) { // nothing available to send work to!
          goto cascade;
        }

        /* Get some new work for the chip */
	work = get_queue_work(thr, drillbit, thr->id);
	if (unlikely(thr->work_restart)) {
		work_completed(drillbit, work);
		goto cascade;
	}

        drvlog(LOG_DEBUG, "Sending work to chip_id %d", chip->chip_id);
        serialise_work_request(buf, chip->chip_id, work);

	/* Send work to cgminer */
        cmd = 'W';
	usb_write(drillbit, &cmd, 1, &amount, C_BF_REQWORK);
	usb_write(drillbit, buf, SZ_SERIALISED_WORKREQUEST, &amount, C_BF_REQWORK);

	if (unlikely(thr->work_restart))
		goto cascade;

        /* Expect a single 'W' byte as acknowledgement */
        usb_read_simple_response(drillbit, 'W', C_BF_REQWORK);
        if(chip->state == WORKING_NOQUEUED)
          chip->state = WORKING_QUEUED;
        else
          chip->state = WORKING_NOQUEUED;

        // Read into work history
        if(chip->current_work[0])
          work_completed(drillbit, chip->current_work[0]);
        for(i = 0; i < WORK_HISTORY_LEN-1; i++)
          chip->current_work[i] = chip->current_work[i+1];
        chip->current_work[WORK_HISTORY_LEN-1] = copy_work(work);
        cgtime(&chip->tv_start);

        /* Print a per-chip info line every 30 seconds */
        cgtime(&tv_now);
        if(opt_log_level <= LOG_INFO && ms_tdiff(&tv_now, &info->tv_lastchipinfo) > 30000) {
          /* TODO: this output line may get truncated (max debug is 256 bytes) once we get more
             chips in a single device
          */
          amount = sprintf(buf, "%s %d: S/E/T", drillbit->drv->name, drillbit->device_id);
          for(i = 0; i < info->num_chips; i++) {
            chip= &info->chips[i];
            j = snprintf(buf+amount, sizeof(buf)-amount, " %d:%d/%d/%d",
                         chip->chip_id, chip->success_count, chip->error_count,
                         chip->timeout_count);
            if(j < 0)
              break;
            amount += j;
            if(amount >= sizeof(buf))
               break;
          }
          drvlog(LOG_INFO, "%s", buf);
          cgtime(&info->tv_lastchipinfo);
        }

cascade:
        drillbit_empty_buffer(drillbit);

	if (unlikely(drillbit->usbinfo.nodev)) {
		drvlog(LOG_WARNING, "%s %d: Device disappeared, disabling thread",
		       drillbit->drv->name, drillbit->device_id);
		return -1;
	}
	return 0xffffffffULL * result_count;
}

static struct api_data *drillbit_api_stats(struct cgpu_info *cgpu)
{
	struct drillbit_info *info = cgpu->device_data;
	struct api_data *root = NULL;
	char serial[16];
	int version;

	version = info->version;
	root = api_add_int(root, "Version", &version, true);
	root = api_add_string(root, "Product", info->product, false);
	sprintf(serial, "%08x", info->serial);
	root = api_add_string(root, "Serial", serial, true);

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
	.name = "Drillbit",
	.drv_detect = drillbit_detect,
        .hash_work = &hash_driver_work,
	.scanwork = drillbit_scanwork,
	.get_api_stats = drillbit_api_stats,
	.reinit_device = drillbit_reinit,
	.thread_shutdown = drillbit_shutdown,
	.identify_device = drillbit_identify,
};


/* Structure serialisation/deserialisation */

#define SERIALISE(FIELD) do {                    \
    memcpy(&buf[offset], &FIELD, sizeof(FIELD)); \
    offset += sizeof(FIELD);                     \
  } while(0)

#define DESERIALISE(FIELD) do {                   \
    memcpy(&FIELD, &buf[offset], sizeof(FIELD));  \
    offset += sizeof(FIELD);                      \
  } while(0)

static void serialise_work_request(uint8_t *buf, uint16_t chip_id, const struct work *work)
{
  size_t offset = 0;
  SERIALISE(chip_id);
  memcpy(&buf[offset], work->midstate, 32);
  offset += 32;
  memcpy(&buf[offset], work->data + 64, 12);
  //offset += 12;
}

static void deserialise_work_result(WorkResult *wr, const uint8_t *buf)
{
  int i;
  size_t offset = 0;
  DESERIALISE(wr->chip_id);
  DESERIALISE(wr->num_nonces);
  DESERIALISE(wr->is_idle);
  for(i = 0; i < MAX_RESULTS; i++)
    DESERIALISE(wr->nonce[i]);
}

static void serialise_board_config(uint8_t *buf, const BoardConfig *bc)
{
  size_t offset = 0;
  SERIALISE(bc->core_voltage);
  SERIALISE(bc->int_clock_level);
  SERIALISE(bc->clock_div2);
  SERIALISE(bc->use_ext_clock);
  SERIALISE(bc->ext_clock_freq);
}

static void deserialise_identity(Identity *id, const uint8_t *buf)
{
  size_t offset = 0;
  DESERIALISE(id->protocol_version);
  DESERIALISE(id->product);
  DESERIALISE(id->serial);
  DESERIALISE(id->num_chips);
  DESERIALISE(id->capabilities);
}
