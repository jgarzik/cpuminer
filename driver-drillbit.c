/*
 * Copyright 2013 Con Kolivas
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

/* Request and response structsfor firmware */

typedef struct __attribute__((packed))
{
  uint16_t chip_id;
  uint8_t midstate[32];
  uint8_t data[12];
} WorkRequest;

typedef struct __attribute__((packed))
{
  uint16_t chip_id;
  uint8_t num_nonces;
  uint8_t is_idle;
  uint32_t nonce[MAX_RESULTS];
} WorkResult;

#define CONFIG_PW1 (1<<0)
#define CONFIG_PW2 (1<<1)

// Possible core voltage settings on PW1 & PW2
#define CONFIG_CORE_065V 0
#define CONFIG_CORE_075V CONFIG_PW2
#define CONFIG_CORE_085V CONFIG_PW1
#define CONFIG_CORE_095V (CONFIG_PW1|CONFIG_PW2)

typedef struct __attribute__((packed))
{
  uint8_t core_voltage; // Set to flags defined above
  uint8_t int_clock_level; // Clock level (30-48 without divider), see asic.c for details
  uint8_t clock_div2;      // Apply the /2 clock divider (both internal and external)
  uint8_t use_ext_clock; // Ignored on boards without external clocks
  uint16_t ext_clock_freq;
} BoardConfig;

typedef struct __attribute__((packed)) Identity
{
    uint8_t protocol_version;
    uint8_t product[8];
    uint32_t serial;
    uint8_t num_chips;
} Identity;

/* Comparatively modest default settings */
static BoardConfig drillbit_config = {
 core_voltage: CONFIG_CORE_085V,
 use_ext_clock: 0,
 int_clock_level: 40,
 clock_div2: 0,
 ext_clock_freq: 200
};

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
    usb_read_once_timeout(drillbit, &res[count], result_size-count, &amount, ms_left, command_name);
    count += amount;
    cgtime(&tv_now);
    ms_left = timeout - ms_tdiff(&tv_now, &tv_start);
  }
  if(count == result_size) {
    return true;
  }
  applog(LOG_ERR, "Read incomplete fixed size packet - got %d bytes / %lu (timeout %d)", count, result_size, timeout);
  hex = bin2hex(res, count);
  applog(LOG_DEBUG, "%s", hex);
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
    applog(LOG_ERR, "Failed to write command %c",command);
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
  usb_read_once_timeout(drillbit, &response, 1, &amount, TIMEOUT, C_BF_GETRES);
  if(amount != 1) {
    applog(LOG_ERR, "Got no response to command %c",command);
    return false;
  }
  if(response != command) {
    applog(LOG_ERR, "Got unexpected response %c to command %c", response, command);    
    return false;
  }
  return true;
}

static void drillbit_empty_buffer(struct cgpu_info *drillbit)
{
	char buf[512];
	int amount;

	do {
		usb_read_once(drillbit, buf, 512, &amount, C_BF_FLUSH);
	} while (amount);
}

static void drillbit_open(struct cgpu_info *drillbit)
{
	uint32_t buf[2];

	drillbit_empty_buffer(drillbit);
	/* Magic sequence to reset device only really needed for windows but
	 * harmless on linux. */
        /*
	buf[0] = 0x80250000;
	buf[1] = 0x00000800;
	usb_transfer(drillbit, 0, 9, 1, 0, C_BF_RESET);
	usb_transfer(drillbit, 0x21, 0x22, 0, 0, C_BF_OPEN);
	usb_transfer_data(drillbit, 0x21, 0x20, 0x0000, 0, buf, 7, C_BF_INIT);
        */
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
        Identity identity;

	err = usb_write(drillbit, "I", 1, &amount, C_BF_REQINFO);
	if (err) {
		applog(LOG_INFO, "%s %d: Failed to write REQINFO",
		       drillbit->drv->name, drillbit->device_id);
		return false;
	}
        // can't call usb_read_fixed_size here as stats not initialised
	err = usb_read(drillbit, (void *)&identity, sizeof(Identity), &amount, C_BF_GETINFO);
	if (err) {
		applog(LOG_INFO, "%s %d: Failed to read GETINFO",
		       drillbit->drv->name, drillbit->device_id);
		return false;
	}
	if (amount != sizeof(Identity)) {
		applog(LOG_INFO, "%s %d: Getinfo received %d bytes instead of %lu",
		       drillbit->drv->name, drillbit->device_id, amount, sizeof(Identity));
		return false;
	}
	info->version = identity.protocol_version;
	memcpy(&info->product, identity.product, sizeof(identity.product));
        info->serial = identity.serial;
        info->num_chips = identity.num_chips;

	applog(LOG_INFO, "%s %d: Getinfo returned version %d, product %s serial %08x num_chips %d", drillbit->drv->name,
	       drillbit->device_id, info->version, info->product, info->serial, info->num_chips);
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

static void drillbit_send_config(struct cgpu_info *drillbit, const BoardConfig *config)
{
  char cmd;
  int amount;
  applog(LOG_INFO, "Sending board configuration voltage=%d use_ext_clock=%d int_clock_level=%d clock_div2=%d ext_clock_freq=%d",
         config->core_voltage, config->use_ext_clock, config->int_clock_level,
         config->clock_div2, config->ext_clock_freq);
  cmd = 'C';
  usb_write(drillbit, &cmd, 1, &amount, C_BF_REQWORK);
  usb_write(drillbit, (void *)config, sizeof(config), &amount, C_BF_CONFIG);

  /* Expect a single 'C' byte as acknowledgement */
  usb_read_simple_response(drillbit, 'C', C_BF_CONFIG); // TODO: verify response
}

static bool drillbit_parse_options()
{
  /* Read configuration options (currently global not per-ASIC or per-board) */
  if (opt_drillbit_options != NULL) {
    int count, freq, clockdiv, voltage;
    char clksrc[4];

    count = sscanf(opt_drillbit_options, "%3s:%d:%d:%d",
                   clksrc, &freq, &clockdiv, &voltage);

    if(count < 2) {
      applog(LOG_ERR, "Failed to parse drillbit-options. Invalid options string: '%s'", opt_drillbit_options);
      return false;
    }

    if(count > 2 && clockdiv != 1 && clockdiv != 2) {
      applog(LOG_ERR, "drillbit-options: Invalid clock divider value %d. Valid values are 1 & 2.", clockdiv);
      return false;
    }
    drillbit_config.clock_div2 = count > 2 && clockdiv == 2;

    if(!strcmp("int",clksrc)) {
      drillbit_config.use_ext_clock = 0;
      if(freq < 0 || freq > 63) {
        applog(LOG_ERR, "drillbit-options: Invalid internal oscillator level %d. Recommended range is %s for this clock divider (possible is 0-63)", freq, drillbit_config.clock_div2 ? "48-57":"30-48");
        return false;
      }
      if(drillbit_config.clock_div2 && (freq < 48 || freq > 57)) {
        applog(LOG_WARNING, "drillbit-options: Internal oscillator level %d outside recommended range 48-57.", freq);
      }
      if(!drillbit_config.clock_div2 && (freq < 30 || freq > 48)) {
        applog(LOG_WARNING, "drillbit-options: Internal oscillator level %d outside recommended range 30-48.", freq);
      }
      drillbit_config.int_clock_level = freq;
    }
    else if (!strcmp("ext", clksrc)) {
      drillbit_config.use_ext_clock = 1;
      drillbit_config.ext_clock_freq = freq;
      if(freq < 80 || freq > 230) {
        applog(LOG_WARNING, "drillbit-options: Warning: recommended external clock frequencies are 80-230MHz. Value %d may produce unexpected results.", freq);
      }
    }
    else {
      applog(LOG_ERR, "drillbit-options: Invalid clock source. Valid choices are int, ext.");
      return false;
    }

    if(count > 3) {
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
      drillbit_config.core_voltage = voltage;
    }
  }
  return true;
}

static bool drillbit_detect_one(struct libusb_device *dev, struct usb_find_devices *found)
{
	struct cgpu_info *drillbit;
	struct drillbit_info *info;
        int i;

        if (!drillbit_parse_options())
          return; // Bit of a hack doing this here, should do it somewhere else

	drillbit = usb_alloc_cgpu(&drillbit_drv, 1);

	if (!usb_init(drillbit, dev, found))
		goto out;
	applog(LOG_INFO, "%s %d: Found at %s", drillbit->drv->name,
	       drillbit->device_id, drillbit->device_path);

	info = calloc(sizeof(struct drillbit_info), 1);
	if (!info)
		quit(1, "Failed to calloc info in drillbit_detect_one");
	drillbit->device_data = info;

	usb_buffer_enable(drillbit);

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

	if (!add_cgpu(drillbit))
		goto out_close;

	update_usb_stats(drillbit);

        drillbit_send_config(drillbit, &drillbit_config);

	applog(LOG_INFO, "%s %d: Successfully initialised %s",
	       drillbit->drv->name, drillbit->device_id, drillbit->device_path);

	return true;
out_close:
	drillbit_close(drillbit);
	usb_uninit(drillbit);
out:
	drillbit = usb_free_cgpu(drillbit);
	return false;
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
  WorkResult response;

  successful_results = 0;

  // Send request for completed work
  cmd = 'E';
  usb_write(drillbit, &cmd, 1, &amount, C_BF_GETRES);

  // Receive count for work results
  if(!usb_read_fixed_size(drillbit, &result_count, sizeof(result_count), TIMEOUT, C_BF_GETRES)) {
    applog(LOG_ERR, "Got no response to request for work results");
    return false;
  }
  if(result_count)
    applog(LOG_DEBUG, "Result count %d",result_count);

  // Receive work results (0 or more)
  for(j = 0; j < result_count; j++) {

    if(!usb_read_fixed_size(drillbit, &response, sizeof(WorkResult), TIMEOUT, C_BF_GETRES)) {
      applog(LOG_ERR, "Failed to read response data packet idx %d count 0x%x", j, result_count);
      return 0;
    }

    if (unlikely(thr->work_restart))
      goto cleanup;
    applog(LOG_DEBUG, "Got response packet chip_id %d nonces %d is_idle %d", response.chip_id, response.num_nonces, response.is_idle);
    chip = find_chip(info, response.chip_id);
    if(!chip) {
      applog(LOG_ERR, "Got work result for unknown chip id %d", response.chip_id);
      continue;
    }
    if(chip->state == IDLE) {
      applog(LOG_WARNING, "Got spurious work results for idle ASIC %d", response.chip_id);
    }
    for(i = 0; i < response.num_nonces; i++) {
      found = false;
      for(k = 0; k < WORK_HISTORY_LEN; k++) {
        if (chip->current_work[k] && drillbit_checkresults(thr, chip->current_work[k], response.nonce[i])) {
          successful_results++;
          found = true;
          break;
        }
      }
      if(!found && chip->state != IDLE) {
        /* all nonces we got back from this chip were invalid */
        inc_hw_errors(thr);
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

static int64_t drillbit_scanhash(struct thr_info *thr, struct work *work,
				int64_t __maybe_unused max_nonce)
{
	struct cgpu_info *drillbit = thr->cgpu;
	struct drillbit_info *info = drillbit->device_data;
        struct drillbit_chip_info *chip;
	struct timeval tv_now, tv_start;
	int amount, i, j;
	int ms_diff;
        uint8_t cmd;
        int result_count;
        WorkRequest request;
        char *tmp;

        result_count = 0;

        chip = NULL;
	cgtime(&tv_start);
        ms_diff = TIMEOUT;

        // check for results, repeat if necessry until we see a free chip
        while(chip == NULL) {
          result_count += check_for_results(thr);
          for(i = 0; i < info->num_chips; i++) {
            if(info->chips[i].state != WORKING_QUEUED) {
              chip = &info->chips[i];
              break;
            }
          }

          cgtime(&tv_now);
          ms_diff = ms_tdiff(&tv_now, &tv_start);
          if(ms_diff > TIMEOUT) {
            applog(LOG_ERR, "Timed out waiting for any results to come back from ASICs.");
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
            applog(LOG_ERR, "Timing out unresponsive ASIC %d", info->chips[i].chip_id);
            info->chips[i].state = IDLE;
            chip = &info->chips[i];
          }
        }

        if(chip == NULL) {
          goto cascade;
        }

        applog(LOG_DEBUG, "Sending work to chip_id %d", chip->chip_id);
        request.chip_id = chip->chip_id;
	memcpy(&request.midstate, work->midstate, 32);
	memcpy(&request.data, work->data + 64, 12);

	/* Send work to cgminer */
        cmd = 'W';
	usb_write(drillbit, &cmd, 1, &amount, C_BF_REQWORK);
	usb_write(drillbit, (void *)&request, sizeof(request), &amount, C_BF_REQWORK);

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
          free_work(chip->current_work[0]);
        for(i = 0; i < WORK_HISTORY_LEN-1; i++)
          chip->current_work[i] = chip->current_work[i+1];
        chip->current_work[WORK_HISTORY_LEN-1] = copy_work(work);
        cgtime(&chip->tv_start);

cascade:
        drillbit_empty_buffer(drillbit);
	work->blk.nonce = 0xffffffff;

	if (unlikely(drillbit->usbinfo.nodev)) {
		applog(LOG_WARNING, "%s %d: Device disappeared, disabling thread",
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
	.scanhash = drillbit_scanhash,
	.get_api_stats = drillbit_api_stats,
	.reinit_device = drillbit_reinit,
	.thread_shutdown = drillbit_shutdown,
	.identify_device = drillbit_identify,
};
