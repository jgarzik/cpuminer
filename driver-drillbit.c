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

typedef struct __attribute__((packed)) Identity
{
    uint8_t protocol_version;
    uint8_t product[8];
    uint32_t serial;
    uint8_t num_chips;
} Identity;

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
static bool usb_read_fixed_size(struct cgpu_info *bitfury, void *result, size_t result_size, int timeout, enum usb_cmds command_name) {
  uint8_t *buf[result_size];
  char *hex;
  int count;
  int amount;

  amount = 1;
  count = 0;
  while(amount > 0 && count < result_size) { // TODO: decrement timeout appropriately
    usb_read_once_timeout(bitfury, (char *)&buf[count], result_size-count, &amount, timeout, command_name);
    count += amount;
  }
  if(amount > 0) {
    memcpy(result, buf, result_size);
    return true;
  }
  if(count > 0) {
    applog(LOG_ERR, "Read incomplete fixed size packet - got %d bytes / %lu", count, result_size);
    hex = bin2hex(result, count);
    applog(LOG_DEBUG, "%s", hex);
    free(hex);
  }
  return false;
}

static bool usb_read_simple_response(struct cgpu_info *bitfury, char command, enum usb_cmds command_name);

/* Write a simple one-byte command and expect a simple one-byte response
   Returns true on success
*/
static bool usb_send_simple_command(struct cgpu_info *bitfury, char command, enum usb_cmds command_name) {
  int amount;
  usb_write(bitfury, &command, 1, &amount, C_BF_REQWORK);
  if(amount != 1) {
    applog(LOG_ERR, "Failed to write command %c",command);
    return false;
  }
  return usb_read_simple_response(bitfury, command, command_name);
}


/* Read a simple single-byte response and check it matches the correct command character
   Return true on success
*/
static bool usb_read_simple_response(struct cgpu_info *bitfury, char command, enum usb_cmds command_name) {
  int amount;
  char response;
  /* Expect a single byte, matching the command, as acknowledgement */
  usb_read_once_timeout(bitfury, &response, 1, &amount, TIMEOUT, C_BF_GETRES);
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

static void bitfury_empty_buffer(struct cgpu_info *bitfury)
{
	char buf[512];
	int amount;

	do {
		usb_read_once(bitfury, buf, 512, &amount, C_BF_FLUSH);
	} while (amount);
}

static void bitfury_open(struct cgpu_info *bitfury)
{
	uint32_t buf[2];

	bitfury_empty_buffer(bitfury);
	/* Magic sequence to reset device only really needed for windows but
	 * harmless on linux. */
        /*
	buf[0] = 0x80250000;
	buf[1] = 0x00000800;
	usb_transfer(bitfury, 0, 9, 1, 0, C_BF_RESET);
	usb_transfer(bitfury, 0x21, 0x22, 0, 0, C_BF_OPEN);
	usb_transfer_data(bitfury, 0x21, 0x20, 0x0000, 0, buf, 7, C_BF_INIT);
        */
}

static void bitfury_close(struct cgpu_info *bitfury)
{
  struct drillbit_info *info = bitfury->device_data;
  bitfury_empty_buffer(bitfury);
  if(info->chips)
    free(info->chips);
}

static void bitfury_identify(struct cgpu_info *bitfury)
{
	int amount;

        usb_send_simple_command(bitfury, 'L', C_BF_IDENTIFY);
}

static bool bitfury_getinfo(struct cgpu_info *bitfury, struct drillbit_info *info)
{
	int amount, err;
        Identity identity;

	err = usb_write(bitfury, "I", 1, &amount, C_BF_REQINFO);
	if (err) {
		applog(LOG_INFO, "%s %d: Failed to write REQINFO",
		       bitfury->drv->name, bitfury->device_id);
		return false;
	}
        // can't call usb_read_fixed_size here as stats not initialised
	err = usb_read(bitfury, (void *)&identity, sizeof(Identity), &amount, C_BF_GETINFO);
	if (err) {
		applog(LOG_INFO, "%s %d: Failed to read GETINFO",
		       bitfury->drv->name, bitfury->device_id);
		return false;
	}
	if (amount != sizeof(Identity)) {
		applog(LOG_INFO, "%s %d: Getinfo received %d bytes instead of %lu",
		       bitfury->drv->name, bitfury->device_id, amount, sizeof(Identity));
		return false;
	}
	info->version = identity.protocol_version;
	memcpy(&info->product, identity.product, sizeof(identity.product));
        info->serial = identity.serial;
        info->num_chips = identity.num_chips;

	applog(LOG_INFO, "%s %d: Getinfo returned version %d, product %s serial %08x num_chips %d", bitfury->drv->name,
	       bitfury->device_id, info->version, info->product, info->serial, info->num_chips);
	bitfury_empty_buffer(bitfury);
	return true;
}

static bool bitfury_reset(struct cgpu_info *bitfury)
{
	int amount, err;
        if(!usb_send_simple_command(bitfury, 'R', C_BF_REQRESET))
          return false;
        
	bitfury_empty_buffer(bitfury);
	return true;
}

static bool bitfury_detect_one(struct libusb_device *dev, struct usb_find_devices *found)
{
	struct cgpu_info *bitfury;
	struct drillbit_info *info;
        int i;

	bitfury = usb_alloc_cgpu(&bitfury_drv, 1);

	if (!usb_init(bitfury, dev, found))
		goto out;
	applog(LOG_INFO, "%s %d: Found at %s", bitfury->drv->name,
	       bitfury->device_id, bitfury->device_path);

	info = calloc(sizeof(struct drillbit_info), 1);
	if (!info)
		quit(1, "Failed to calloc info in bitfury_detect_one");
	bitfury->device_data = info;

	usb_buffer_enable(bitfury);

	bitfury_open(bitfury);

	/* Send getinfo request */
	if (!bitfury_getinfo(bitfury, info))
		goto out_close;

	/* Send reset request */
	if (!bitfury_reset(bitfury))
		goto out_close;

	bitfury_identify(bitfury);
	bitfury_empty_buffer(bitfury);

        /* TODO: Add detection for actual chip ids based on command/response,
           not prefill assumption about chip layout based on info structure */
        info->chips = calloc(sizeof(struct drillbit_chip_info), info->num_chips);
        for(i = 0; i < info->num_chips; i++) {
          info->chips[i].chip_id = i;
        }

	if (!add_cgpu(bitfury))
		goto out_close;

	update_usb_stats(bitfury);
	applog(LOG_INFO, "%s %d: Successfully initialised %s",
	       bitfury->drv->name, bitfury->device_id, bitfury->device_path);

	return true;
out_close:
	bitfury_close(bitfury);
	usb_uninit(bitfury);
out:
	bitfury = usb_free_cgpu(bitfury);
	return false;
}

static void bitfury_detect(bool __maybe_unused hotplug)
{
	usb_detect(&bitfury_drv, bitfury_detect_one);
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

static bool bitfury_checkresults(struct thr_info *thr, struct work *work, uint32_t nonce)
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
static bool check_for_results(struct thr_info *thr)
{
  struct cgpu_info *bitfury = thr->cgpu;
  struct drillbit_info *info = bitfury->device_data;
  struct drillbit_chip_info *chip;
  char cmd;
  int amount, i, j, k;
  int successful_results, found;
  uint32_t result_count;
  WorkResult response;

  successful_results = 0;

  // Send request for completed work
  cmd = 'E';
  usb_write(bitfury, &cmd, 1, &amount, C_BF_GETRES);

  // Receive count for work results
  if(!usb_read_fixed_size(bitfury, &result_count, sizeof(result_count), TIMEOUT, C_BF_GETRES)) {
    applog(LOG_ERR, "Got no response to request for work results");
    return false;
  }
  if(result_count)
    applog(LOG_DEBUG, "Result count %d",result_count);

  // Receive work results (0 or more)
  for(j = 0; j < result_count; j++) {

    if(!usb_read_fixed_size(bitfury, &response, sizeof(WorkResult), TIMEOUT, C_BF_GETRES)) {
      applog(LOG_ERR, "Failed to read response data packet idx %d count %d", j, result_count);
      return false;
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
        if (chip->current_work[k] && bitfury_checkresults(thr, chip->current_work[k], response.nonce[i])) {
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

static int64_t bitfury_scanhash(struct thr_info *thr, struct work *work,
				int64_t __maybe_unused max_nonce)
{
	struct cgpu_info *bitfury = thr->cgpu;
	struct drillbit_info *info = bitfury->device_data;
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
	usb_write(bitfury, &cmd, 1, &amount, C_BF_REQWORK);
	usb_write(bitfury, (void *)&request, sizeof(request), &amount, C_BF_REQWORK);

	if (unlikely(thr->work_restart))
		goto cascade;

        /* Expect a single 'W' byte as acknowledgement */
        usb_read_simple_response(bitfury, 'W', C_BF_REQWORK); // TODO: verify response
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
        bitfury_empty_buffer(bitfury);
	work->blk.nonce = 0xffffffff;

	if (unlikely(bitfury->usbinfo.nodev)) {
		applog(LOG_WARNING, "%s %d: Device disappeared, disabling thread",
		       bitfury->drv->name, bitfury->device_id);
		return -1;
	}
	return (uint64_t)result_count * 0xffffffffULL;
}

static struct api_data *bitfury_api_stats(struct cgpu_info *cgpu)
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

static void bitfury_init(struct cgpu_info  *bitfury)
{
	bitfury_close(bitfury);
	bitfury_open(bitfury);
	bitfury_reset(bitfury);
}

static void bitfury_shutdown(struct thr_info *thr)
{
	struct cgpu_info *bitfury = thr->cgpu;

	bitfury_close(bitfury);
}

/* Currently hardcoded to BF1 devices */
struct device_drv bitfury_drv = {
	.drv_id = DRIVER_drillbit,
	.dname = "Drillbit",
	.name = "Drillbit",
	.drv_detect = bitfury_detect,
	.scanhash = bitfury_scanhash,
	.get_api_stats = bitfury_api_stats,
	.reinit_device = bitfury_init,
	.thread_shutdown = bitfury_shutdown,
	.identify_device = bitfury_identify
};
