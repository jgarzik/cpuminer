/*
 * Copyright 2013-2014 Con Kolivas <kernel@kolivas.org>
 * Copyright 2013 Hashfast Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 3 of the License, or (at your option)
 * any later version.  See COPYING for more details.
 */

#include "config.h"

#include <stdbool.h>
#include <math.h>

#include "miner.h"
#include "usbutils.h"

#include "driver-hashfast.h"

int opt_hfa_ntime_roll = 1;
int opt_hfa_hash_clock = HFA_CLOCK_DEFAULT;
int opt_hfa_overheat = HFA_TEMP_OVERHEAT;
int opt_hfa_target = HFA_TEMP_TARGET;
bool opt_hfa_pll_bypass;
bool opt_hfa_dfu_boot;
int opt_hfa_fan_default = HFA_FAN_DEFAULT;
int opt_hfa_fan_max = HFA_FAN_MAX;
int opt_hfa_fan_min = HFA_FAN_MIN;
int opt_hfa_fail_drop = 10;
bool opt_hfa_noshed;

char *opt_hfa_name;
char *opt_hfa_options;

////////////////////////////////////////////////////////////////////////////////
// Support for the CRC's used in header (CRC-8) and packet body (CRC-32)
////////////////////////////////////////////////////////////////////////////////

#define GP8  0x107   /* x^8 + x^2 + x + 1 */
#define DI8  0x07

static bool hfa_crc8_set;

char *set_hfa_fan(char *arg)
{
	int val1, val2, ret;

	ret = sscanf(arg, "%d-%d", &val1, &val2);
	if (ret < 1)
		return "No values passed to hfa-fan";
	if (ret == 1)
		val2 = val1;

	if (val1 < 0 || val1 > 100 || val2 < 0 || val2 > 100 || val2 < val1)
		return "Invalid value passed to hfa-fan";

	opt_hfa_fan_min = val1;
	opt_hfa_fan_max = val2;
	if (opt_hfa_fan_min > opt_hfa_fan_default)
		opt_hfa_fan_default = opt_hfa_fan_min;
	if (opt_hfa_fan_max < opt_hfa_fan_default)
		opt_hfa_fan_default = opt_hfa_fan_max;

	return NULL;
}

static unsigned char crc8_table[256];	/* CRC-8 table */

static void hfa_init_crc8(void)
{
	int i,j;
	unsigned char crc;

	hfa_crc8_set = true;
	for (i = 0; i < 256; i++) {
		crc = i;
		for (j = 0; j < 8; j++)
			crc = (crc << 1) ^ ((crc & 0x80) ? DI8 : 0);
		crc8_table[i] = crc & 0xFF;
	}
}

static unsigned char hfa_crc8(unsigned char *h)
{
	int i;
	unsigned char crc;

	h++;	// Preamble not included
	for (i = 1, crc = 0xff; i < 7; i++)
		crc = crc8_table[crc ^ *h++];

	return crc;
}

struct hfa_cmd {
	uint8_t cmd;
	char *cmd_name;
	enum usb_cmds usb_cmd;
};

/* Entries in this array need to align with the actual op values specified
 * in hf_protocol.h */
#define C_NULL C_MAX
static const struct hfa_cmd hfa_cmds[] = {
	{OP_NULL, "OP_NULL", C_NULL},				// 0
	{OP_ROOT, "OP_ROOT", C_NULL},
	{OP_RESET, "OP_RESET", C_HF_RESET},
	{OP_PLL_CONFIG, "OP_PLL_CONFIG", C_HF_PLL_CONFIG},
	{OP_ADDRESS, "OP_ADDRESS", C_HF_ADDRESS},
	{OP_READDRESS, "OP_READDRESS", C_NULL},
	{OP_HIGHEST, "OP_HIGHEST", C_NULL},
	{OP_BAUD, "OP_BAUD", C_HF_BAUD},
	{OP_UNROOT, "OP_UNROOT", C_NULL},			// 8
	{OP_HASH, "OP_HASH", C_HF_HASH},
	{OP_NONCE, "OP_NONCE", C_HF_NONCE},
	{OP_ABORT, "OP_ABORT", C_HF_ABORT},
	{OP_STATUS, "OP_STATUS", C_HF_STATUS},
	{OP_GPIO, "OP_GPIO", C_NULL},
	{OP_CONFIG, "OP_CONFIG", C_HF_CONFIG},
	{OP_STATISTICS, "OP_STATISTICS", C_HF_STATISTICS},
	{OP_GROUP, "OP_GROUP", C_NULL},				// 16
	{OP_CLOCKGATE, "OP_CLOCKGATE", C_HF_CLOCKGATE},

	{OP_USB_INIT, "OP_USB_INIT", C_HF_USB_INIT},		// 18
	{OP_GET_TRACE, "OP_GET_TRACE", C_NULL},
	{OP_LOOPBACK_USB, "OP_LOOPBACK_USB", C_NULL},
	{OP_LOOPBACK_UART, "OP_LOOPBACK_UART", C_NULL},
	{OP_DFU, "OP_DFU", C_HF_DFU},
	{OP_USB_SHUTDOWN, "OP_USB_SHUTDOWN", C_NULL},
	{OP_DIE_STATUS, "OP_DIE_STATUS", C_HF_DIE_STATUS},	// 24
	{OP_GWQ_STATUS, "OP_GWQ_STATUS", C_HF_GWQ_STATUS},
	{OP_WORK_RESTART, "OP_WORK_RESTART", C_HF_WORK_RESTART},
	{OP_USB_STATS1, "OP_USB_STATS1", C_NULL},
	{OP_USB_GWQSTATS, "OP_USB_GWQSTATS", C_HF_GWQSTATS},
	{OP_USB_NOTICE, "OP_USB_NOTICE", C_HF_NOTICE},
	{OP_PING, "OP_PING", C_HF_PING},
	{OP_CORE_MAP, "OP_CORE_MAP", C_NULL},
	{OP_VERSION, "OP_VERSION", C_NULL},			// 32
	{OP_FAN, "OP_FAN", C_HF_FAN},
	{OP_NAME, "OP_NAME", C_OP_NAME}
};

#define HF_USB_CMD_OFFSET (128 - 18)
#define HF_USB_CMD(X) (X - HF_USB_CMD_OFFSET)

/* Send an arbitrary frame, consisting of an 8 byte header and an optional
 * packet body. */
static bool __hfa_send_frame(struct cgpu_info *hashfast, uint8_t opcode, int tx_length,
			     uint8_t *packet)
{
	struct hashfast_info *info = hashfast->device_data;
	int ret, amount;
	bool retried = false;

	if (unlikely(hashfast->usbinfo.nodev))
		return false;

	info->last_send = time(NULL);
	applog(LOG_DEBUG, "%s %s: Sending %s frame", hashfast->drv->name, hashfast->unique_id, hfa_cmds[opcode].cmd_name);
retry:
	ret = usb_write(hashfast, (char *)packet, tx_length, &amount,
			hfa_cmds[opcode].usb_cmd);
	if (unlikely(ret < 0 || amount != tx_length)) {
		if (hashfast->usbinfo.nodev)
			return false;
		if (!retried) {
			applog(LOG_ERR, "%s %s: hfa_send_frame: USB Send error, ret %d amount %d vs. tx_length %d, retrying",
			       hashfast->drv->name, hashfast->unique_id, ret, amount, tx_length);
			retried = true;
			goto retry;
		}
		applog(LOG_ERR, "%s %s: hfa_send_frame: USB Send error, ret %d amount %d vs. tx_length %d",
		       hashfast->drv->name, hashfast->unique_id, ret, amount, tx_length);
		return false;
	}

	if (retried)
		applog(LOG_WARNING, "%s %s: hfa_send_frame: recovered OK", hashfast->drv->name, hashfast->unique_id);

	return true;
}

static bool hfa_send_generic_frame(struct cgpu_info *hashfast, uint8_t opcode, uint8_t chip_address,
				   uint8_t core_address, uint16_t hdata, uint8_t *data, int len)
{
	uint8_t packet[256];
	struct hf_header *p = (struct hf_header *)packet;
	int tx_length, ret, amount;

	p->preamble = HF_PREAMBLE;
	p->operation_code = opcode;
	p->chip_address = chip_address;
	p->core_address = core_address;
	p->hdata = htole16(hdata);
	p->data_length = len / 4;
	p->crc8 = hfa_crc8(packet);

	if (len)
		memcpy(&packet[sizeof(struct hf_header)], data, len);
	tx_length = sizeof(struct hf_header) + len;

	ret = usb_write(hashfast, (char *)packet, tx_length, &amount, C_NULL);

	return ((ret >= 0) && (amount == tx_length));
}

static bool hfa_send_frame(struct cgpu_info *hashfast, uint8_t opcode, uint16_t hdata,
			   uint8_t *data, int len)
{
	uint8_t packet[256];
	struct hf_header *p = (struct hf_header *)packet;
	int tx_length;

	p->preamble = HF_PREAMBLE;
	p->operation_code = hfa_cmds[opcode].cmd;
	p->chip_address = HF_GWQ_ADDRESS;
	p->core_address = 0;
	p->hdata = htole16(hdata);
	p->data_length = len / 4;
	p->crc8 = hfa_crc8(packet);

	if (len)
		memcpy(&packet[sizeof(struct hf_header)], data, len);
	tx_length = sizeof(struct hf_header) + len;

	return (__hfa_send_frame(hashfast, opcode, tx_length, packet));
}

/* Send an already assembled packet, consisting of an 8 byte header which may
 * or may not be followed by a packet body. */

static bool hfa_send_packet(struct cgpu_info *hashfast, struct hf_header *h, int cmd)
{
	int amount, ret, len;

	if (unlikely(hashfast->usbinfo.nodev))
		return false;

	len = sizeof(*h) + h->data_length * 4;
	ret = usb_write(hashfast, (char *)h, len, &amount, hfa_cmds[cmd].usb_cmd);
	if (ret < 0 || amount != len) {
		applog(LOG_WARNING, "%s %s: send_packet: %s USB Send error, ret %d amount %d vs. length %d",
		       hashfast->drv->name, hashfast->unique_id, hfa_cmds[cmd].cmd_name, ret, amount, len);
		return false;
	}
	return true;
}

#define HFA_GET_HEADER_BUFSIZE 512

static bool hfa_get_header(struct cgpu_info *hashfast, struct hf_header *h, uint8_t *computed_crc)
{
	int amount, ret, orig_len, len, ofs = 0;
	cgtimer_t ts_start;
	char buf[HFA_GET_HEADER_BUFSIZE];
	char *header;

	if (unlikely(hashfast->usbinfo.nodev))
		return false;

	orig_len = len = sizeof(*h);

	/* Read for up to 500ms till we find the first occurrence of HF_PREAMBLE
	 * though it should be the first byte unless we get woefully out of
	 * sync. */
	cgtimer_time(&ts_start);
	do {
		cgtimer_t ts_now, ts_diff;

		cgtimer_time(&ts_now);
		cgtimer_sub(&ts_now, &ts_start, &ts_diff);
		if (cgtimer_to_ms(&ts_diff) > 500)
			return false;

		if (unlikely(hashfast->usbinfo.nodev))
			return false;
		if(ofs + len > HFA_GET_HEADER_BUFSIZE) {
			// Not expected to happen.
			applog(LOG_WARNING, "hfa_get_header() tried to overflow buf[].");
			return false;
		}
		ret = usb_read(hashfast, buf + ofs, len, &amount, C_HF_GETHEADER);

		if (unlikely(ret && ret != LIBUSB_ERROR_TIMEOUT))
			return false;
		ofs += amount;
		header = memchr(buf, HF_PREAMBLE, ofs);
		if (header) {
			/* Toss any leading data we can't use */
			if (header != buf) {
				memmove(buf, header, ofs);
				ofs -= header - buf;
			}
			len -= ofs;
		}
		else {
			/* HF_PREAMBLE not found, toss all the useless leading data. */
			ofs = 0;
			len = sizeof(*h);
		}
	} while (len > 0);

	memcpy(h, header, orig_len);
	*computed_crc = hfa_crc8((uint8_t *)h);

	return true;
}

static bool hfa_get_data(struct cgpu_info *hashfast, char *buf, int len4)
{
	int amount, ret, len = len4 * 4;

	if (unlikely(hashfast->usbinfo.nodev))
		return false;
	ret = usb_read(hashfast, buf, len, &amount, C_HF_GETDATA);
	if (ret)
		return false;
	if (amount != len) {
		applog(LOG_WARNING, "%s %s: get_data: Strange amount returned %d vs. expected %d",
		       hashfast->drv->name, hashfast->unique_id, amount, len);
		return false;
	}
	return true;
}

static const char *hf_usb_init_errors[] = {
	"Success",
	"Reset timeout",
	"Address cycle timeout",
	"Clockgate operation timeout",
	"Configuration operation timeout",
	"Excessive core failures",
	"All cores failed diagnostics",
	"Too many groups configured - increase ntime roll amount",
	"Chaining connections detected but secondary board(s) did not respond",
	"Secondary board communication error",
	"Main board 12V power is bad",
	"Secondary board(s) 12V power is bad",
	"Main board FPGA programming error",
	"Main board FPGA SPI read timeout",
	"Main board FPGA Bad magic number",
	"Main board FPGA SPI write timeout",
	"Main board FPGA register read/write test failed",
	"ASIC core power fault",
	"Dynamic baud rate change timeout",
	"Address failure",
	"Regulator programming error",
	"Address range inconsistent after mixed reconfiguration",
	"Timeout after mixed reconfiguration"
};

static bool hfa_clear_readbuf(struct cgpu_info *hashfast);

struct op_nameframe {
	struct hf_header h;
	char name[32];
} __attribute__((packed));

static void hfa_write_opname(struct cgpu_info *hashfast, struct hashfast_info *info)
{
	const uint8_t opcode = HF_USB_CMD(OP_NAME);
	struct op_nameframe nameframe;
	struct hf_header *h = (struct hf_header *)&nameframe;
	const int tx_length = sizeof(struct op_nameframe);

	memset(&nameframe, 0, sizeof(nameframe));
	strncpy(nameframe.name, info->op_name, 30);
	h->preamble = HF_PREAMBLE;
	h->operation_code = hfa_cmds[opcode].cmd;
	h->core_address = 1;
	h->data_length = 32 / 4;
	h->crc8 = hfa_crc8((unsigned char *)h);
	applog(LOG_DEBUG, "%s %d: Opname being set to %s", hashfast->drv->name,
	       hashfast->device_id, info->op_name);
	__hfa_send_frame(hashfast, opcode, tx_length, (uint8_t *)&nameframe);
}

/* If no opname or an invalid opname is set, change it to the serial number if
 * it exists, or a random name based on timestamp if not. */
static void hfa_choose_opname(struct cgpu_info *hashfast, struct hashfast_info *info)
{
	uint64_t usecs;

	if (info->serial_number)
		sprintf(info->op_name, "%08x", info->serial_number);
	else {
		struct timeval tv_now;

		cgtime(&tv_now);
		usecs = (uint64_t)(tv_now.tv_sec) * (uint64_t)1000000 + (uint64_t)tv_now.tv_usec;
		sprintf(info->op_name, "%lx", (long unsigned int)usecs);
	}
	hfa_write_opname(hashfast, info);
}

// Generic setting header
struct hf_settings_data {
	uint8_t revision;
	uint8_t ref_frequency;
	uint16_t magic;
	uint16_t frequency0;
	uint16_t voltage0;
	uint16_t frequency1;
	uint16_t voltage1;
	uint16_t frequency2;
	uint16_t voltage2;
	uint16_t frequency3;
	uint16_t voltage3;
} __attribute__((packed,aligned(4)));

static bool hfa_set_voltages(struct cgpu_info *hashfast, struct hashfast_info *info)
{
	struct hf_settings_data op_settings_data;

	op_settings_data.revision = 1;
	op_settings_data.ref_frequency = 25;
	op_settings_data.magic = HFA_MAGIC_SETTINGS_VALUE;

	op_settings_data.frequency0 = info->hash_clock_rate;
	op_settings_data.voltage0 = info->hash_voltage;
	op_settings_data.frequency1 = info->hash_clock_rate;
	op_settings_data.voltage1 = info->hash_voltage;
	op_settings_data.frequency2 = info->hash_clock_rate;
	op_settings_data.voltage2 = info->hash_voltage;
	op_settings_data.frequency3 = info->hash_clock_rate;
	op_settings_data.voltage3 = info->hash_voltage;

	hfa_send_generic_frame(hashfast, OP_SETTINGS, 0x00, 0x01, HFA_MAGIC_SETTINGS_VALUE,
		(uint8_t *)&op_settings_data, sizeof(op_settings_data));
	// reset the board once to switch to new voltage settings
	hfa_send_generic_frame(hashfast, OP_POWER, 0xff, 0x00, 0x1, NULL, 0);
	hfa_send_generic_frame(hashfast, OP_POWER, 0xff, 0x00, 0x2, NULL, 0);

	return true;
}

static bool hfa_send_shutdown(struct cgpu_info *hashfast);

static bool hfa_reset(struct cgpu_info *hashfast, struct hashfast_info *info)
{
	struct hf_usb_init_header usb_init[2], *hu = usb_init;
	struct hf_usb_init_base *db;
        struct hf_usb_init_options *ho;
	int retries = 0, i;
	bool ret = false;
	char buf[1024];
	struct hf_header *h = (struct hf_header *)buf;
	uint8_t hcrc;

	/* Hash clock rate in Mhz. Set to opt_hfa_hash_clock if it has not
	 * been inherited across a restart. */
	if (!info->hash_clock_rate)
		info->hash_clock_rate = opt_hfa_hash_clock;
	info->group_ntime_roll = opt_hfa_ntime_roll;
	info->core_ntime_roll = 1;

	// Assemble the USB_INIT request
	memset(hu, 0, sizeof(*hu));
	hu->preamble = HF_PREAMBLE;
	hu->operation_code = OP_USB_INIT;
	hu->protocol = PROTOCOL_GLOBAL_WORK_QUEUE;	// Protocol to use
	if (!opt_hfa_noshed)
		hu->shed_supported = true;
	// Force PLL bypass
	hu->pll_bypass = opt_hfa_pll_bypass;
	hu->hash_clock = info->hash_clock_rate;		// Hash clock rate in Mhz
	if (info->group_ntime_roll > 1 && info->core_ntime_roll) {
		ho = (struct hf_usb_init_options *)(hu + 1);
		memset(ho, 0, sizeof(*ho));
		ho->group_ntime_roll = info->group_ntime_roll;
		ho->core_ntime_roll = info->core_ntime_roll;
		hu->data_length = sizeof(*ho) / 4;
	}
	hu->crc8 = hfa_crc8((uint8_t *)hu);
	applog(LOG_INFO, "%s %s: Sending OP_USB_INIT with GWQ protocol specified",
	       hashfast->drv->name, hashfast->unique_id);
resend:
	if (unlikely(hashfast->usbinfo.nodev))
		goto out;

	if (!hfa_clear_readbuf(hashfast))
		goto out;

	if (!hfa_send_packet(hashfast, (struct hf_header *)hu, HF_USB_CMD(OP_USB_INIT)))
		goto out;

	// Check for the correct response.
	// We extend the normal timeout - a complete device initialization, including
	// bringing power supplies up from standby, etc., can take over a second.
tryagain:
	for (i = 0; i < 10; i++) {
		ret = hfa_get_header(hashfast, h, &hcrc);
		if (unlikely(hashfast->usbinfo.nodev))
			goto out;
		if (ret)
			break;
	}
	if (!ret) {
		if (retries++ < 3)
			goto resend;
		applog(LOG_WARNING, "%s %s: OP_USB_INIT failed!", hashfast->drv->name, hashfast->unique_id);
		goto out;
	}
	if (h->crc8 != hcrc) {
		applog(LOG_WARNING, "%s %s: OP_USB_INIT failed! CRC mismatch", hashfast->drv->name, hashfast->unique_id);
		ret = false;
		goto out;
	}
	if (h->operation_code != OP_USB_INIT) {
		// This can happen if valid packet(s) were in transit *before* the OP_USB_INIT arrived
		// at the device, so we just toss the packets and keep looking for the response.
		applog(LOG_WARNING, "%s %s: OP_USB_INIT: Tossing packet, valid but unexpected type %d",
                       hashfast->drv->name, hashfast->unique_id, h->operation_code);
		hfa_get_data(hashfast, buf, h->data_length);
		if (retries++ < 3)
			goto tryagain;
		ret = false;
		goto out;
	}

	applog(LOG_DEBUG, "%s %s: Good reply to OP_USB_INIT", hashfast->drv->name, hashfast->unique_id);
	applog(LOG_DEBUG, "%s %s: OP_USB_INIT: %d die in chain, %d cores, device_type %d, refclk %d Mhz",
	       hashfast->drv->name, hashfast->unique_id, h->chip_address, h->core_address, h->hdata & 0xff, (h->hdata >> 8) & 0xff);

	// Save device configuration
	info->asic_count = h->chip_address;
	info->core_count = h->core_address;
	info->device_type = (uint8_t)h->hdata;
	info->ref_frequency = (uint8_t)(h->hdata >> 8);
	info->hash_sequence_head = 0;
	info->hash_sequence_tail = 0;
	info->device_sequence_tail = 0;

	if (info->asic_count == 12)
		hashfast->drv->name = "HFS";
	else if (info->asic_count == 4)
		hashfast->drv->name = "HFB";

	// Size in bytes of the core bitmap in bytes
	info->core_bitmap_size = (((info->asic_count * info->core_count) + 31) / 32) * 4;

	// Get the usb_init_base structure
	if (!hfa_get_data(hashfast, (char *)&info->usb_init_base, U32SIZE(info->usb_init_base))) {
		applog(LOG_WARNING, "%s %s: OP_USB_INIT failed! Failure to get usb_init_base data",
		       hashfast->drv->name, hashfast->unique_id);
		ret = false;
		goto out;
	}
	db = &info->usb_init_base;
	info->firmware_version = ((db->firmware_rev >> 8) & 0xff) + (double)(db->firmware_rev & 0xff) / 10.0;
	info->hardware_version = ((db->hardware_rev >> 8) & 0xff) + (double)(db->hardware_rev & 0xff) / 10.0;
	applog(LOG_INFO, "%s %s:      firmware_rev:    %.1f", hashfast->drv->name, hashfast->unique_id,
	       info->firmware_version);
	applog(LOG_INFO, "%s %s:      hardware_rev:    %.1f", hashfast->drv->name, hashfast->unique_id,
	       info->hardware_version);
	applog(LOG_INFO, "%s %s:      serial number:   %08x", hashfast->drv->name, hashfast->unique_id,
	       db->serial_number);
	applog(LOG_INFO, "%s %s:      hash clockrate:  %d Mhz", hashfast->drv->name, hashfast->unique_id,
	       db->hash_clockrate);
	applog(LOG_INFO, "%s %s:      inflight_target: %d", hashfast->drv->name, hashfast->unique_id,
	       db->inflight_target);
	applog(LOG_INFO, "%s %s:      sequence_modulus: %d", hashfast->drv->name, hashfast->unique_id,
	       db->sequence_modulus);

	// Now a copy of the config data used
	if (!hfa_get_data(hashfast, (char *)&info->config_data, U32SIZE(info->config_data))) {
		applog(LOG_WARNING, "%s %s: OP_USB_INIT failed! Failure to get config_data",
		       hashfast->drv->name, hashfast->unique_id);
		ret = false;
		goto out;
	}

	// Now the core bitmap
	info->core_bitmap = malloc(info->core_bitmap_size);
	if (!info->core_bitmap)
		quit(1, "Failed to malloc info core bitmap in hfa_reset");
	if (!hfa_get_data(hashfast, (char *)info->core_bitmap, info->core_bitmap_size / 4)) {
		applog(LOG_WARNING, "%s %s: OP_USB_INIT failed! Failure to get core_bitmap", hashfast->drv->name, hashfast->unique_id);
		ret = false;
		goto out;
	}

	// See if the initialization suceeded
	if (db->operation_status) {
		applog(LOG_ERR, "%s %s: OP_USB_INIT failed! Operation status %d (%s)",
		       hashfast->drv->name, hashfast->unique_id, db->operation_status,
			(db->operation_status < sizeof(hf_usb_init_errors)/sizeof(hf_usb_init_errors[0])) ?
			hf_usb_init_errors[db->operation_status] : "Unknown error code");
		ret = false;
		switch (db->operation_status) {
			case E_CORE_POWER_FAULT:
				for (i = 0; i < 4; i++) {
					if (((db->extra_status_1 >> i) & 0x11) == 0x1) {
						applog(LOG_ERR, "%s %s: OP_USB_INIT: Quadrant %d (of 4) regulator failure",
						       hashfast->drv->name, hashfast->unique_id, i + 1);
					}
				}
				break;
			default:
				break;
		}
		goto out;
	}

	if (!db->hash_clockrate) {
		applog(LOG_INFO, "%s %s: OP_USB_INIT failed! Clockrate reported as zero",
		       hashfast->drv->name, hashfast->unique_id);
		ret = false;
		goto out;
	}
	info->num_sequence = db->sequence_modulus;
	info->serial_number = db->serial_number;
	info->base_clock = db->hash_clockrate;

	ret = hfa_clear_readbuf(hashfast);
out:
	if (!ret) {
		hfa_send_shutdown(hashfast);
		usb_nodev(hashfast);
	}
	return ret;
}

static bool hfa_clear_readbuf(struct cgpu_info *hashfast)
{
	int amount, ret = 0;
	char buf[512];

	do {
		if (hashfast->usbinfo.nodev) {
			ret = LIBUSB_ERROR_NO_DEVICE;
			break;
		}
		ret = usb_read(hashfast, buf, 512, &amount, C_HF_CLEAR_READ);
	} while (!ret && amount);

	if (ret && ret != LIBUSB_ERROR_TIMEOUT)
		return false;
	return true;
}

static bool hfa_send_shutdown(struct cgpu_info *hashfast)
{
	bool ret = false;

	if (hashfast->usbinfo.nodev)
		return ret;
	/* Send a restart before the shutdown frame to tell the device to
	 * discard any work it thinks is in flight for a cleaner restart. */
	if (!hfa_send_frame(hashfast, HF_USB_CMD(OP_WORK_RESTART), 0, (uint8_t *)NULL, 0))
		return ret;
	if (hfa_send_frame(hashfast, HF_USB_CMD(OP_USB_SHUTDOWN), 0, NULL, 0)) {
		/* Wait to allow device to properly shut down. */
		cgsleep_ms(1000);
		ret = true;
	}
	return ret;
}

static struct cgpu_info *hfa_old_device(struct cgpu_info *hashfast, struct hashfast_info *info)
{
	struct cgpu_info *cgpu, *found = NULL;
	struct hashfast_info *cinfo = NULL;
	int i;

	/* See if we can find a zombie instance of the same device */
	for (i = 0; i < mining_threads; i++) {
		cgpu = mining_thr[i]->cgpu;
		if (!cgpu)
			continue;
		if (cgpu == hashfast)
			continue;
		if (cgpu->drv->drv_id != DRIVER_hashfast)
			continue;
		if (!cgpu->usbinfo.nodev)
			continue;
		cinfo = cgpu->device_data;
		if (!cinfo)
			continue;
		if (info->op_name[0] != '\0' && !strncmp(info->op_name, cinfo->op_name, 32)) {
			found = cgpu;
			break;
		}
		if (info->serial_number && info->serial_number == cinfo->serial_number) {
			found = cgpu;
			break;
		}
	}
	return found;
}

static void hfa_set_clock(struct cgpu_info *hashfast, struct hashfast_info *info)
{
	uint16_t hdata;
	int i;

	hdata = (WR_CLOCK_VALUE << WR_COMMAND_SHIFT) | info->hash_clock_rate;

	hfa_send_frame(hashfast, HF_USB_CMD(OP_WORK_RESTART), hdata, (uint8_t *)NULL, 0);
	/* We won't know what the real clock is in this case without a
	 * usb_init_base message so we have to assume it's what we asked. */
	info->base_clock = info->hash_clock_rate;
	for (i = 0; i < info->asic_count; i++)
		info->die_data[i].hash_clock = info->base_clock;
}

/* Look for an op name match and apply any options to its first attempted
 * init sequence. This function allows any arbitrary number of extra parameters
 * to be added in the future. */
static void hfa_check_options(struct hashfast_info *info)
{
	char *p, *options, *found = NULL, *marker;
	int maxlen, option = 0;

	if (!opt_hfa_options)
		return;

	if (!info->op_name)
		return;

	maxlen = strlen(info->op_name);

	options = strdup(opt_hfa_options);
	for (p = strtok(options, ","); p; p = strtok(NULL, ",")) {
		int cmplen = strlen(p);

		if (maxlen < cmplen)
			cmplen = maxlen;
		if (cmplen < maxlen)
			continue;
		if (!strncmp(info->op_name, p, cmplen)) {
			found = strdup(p);
			break;
		}
	}
	free(options);
	if (!found)
		return;

	for (p = strtok(found, ":"); p; p = strtok(NULL, ":")) {
		long lval;

		/* Parse each option in order, leaving room to add more */
		switch(option++) {
			default:
				break;
			case 1:
				lval = strtol(p, NULL, 10);
				if (lval < HFA_CLOCK_MIN || lval > HFA_CLOCK_MAX) {
					applog(LOG_ERR, "Invalid clock speed %ld set with hashfast option for %s",
					       lval, info->op_name);
					break;
				}
				info->hash_clock_rate = lval;
				marker = strchr(p,'@');
				if (marker != NULL) {
					lval = strtol(marker+1, NULL, 10);
					if (lval < HFA_VOLTAGE_MIN || lval > HFA_VOLTAGE_MAX) {
						applog(LOG_ERR, "Invalid core voltage %ld set with hashfast option for %s",
						       lval, info->op_name);
						break;
					}
					info->hash_voltage = lval;
				}
				break;
		}
	}
	free(found);
}

static bool hfa_detect_common(struct cgpu_info *hashfast)
{
	struct hashfast_info *info;
	char buf[1024];
	struct hf_header *h = (struct hf_header *)buf;
	uint8_t hcrc;
	bool ret;
	int i;

	info = calloc(sizeof(struct hashfast_info), 1);
	if (!info)
		quit(1, "Failed to calloc hashfast_info in hfa_detect_common");
	hashfast->device_data = info;

	/* Try sending and receiving an OP_NAME */
	ret = hfa_send_frame(hashfast, HF_USB_CMD(OP_NAME), 0, (uint8_t *)NULL, 0);
	if (hashfast->usbinfo.nodev) {
		ret = false;
		goto out;
	}
	if (!ret) {
		applog(LOG_WARNING, "%s %d: Failed to send OP_NAME!", hashfast->drv->name,
		       hashfast->device_id);
		goto out;
	}
	ret = hfa_get_header(hashfast, h, &hcrc);
	if (hashfast->usbinfo.nodev) {
		ret = false;
		goto out;
	}
	if (!ret) {
		/* We should receive a valid header even if OP_NAME isn't
		 * supported by the firmware. */
		applog(LOG_NOTICE, "%s %d: No response to name query - failed init or firmware upgrade required.",
			hashfast->drv->name, hashfast->device_id);
		ret = true;
	} else {
		/* Only try to parse the name if the firmware supports OP_NAME */
		if (h->operation_code == OP_NAME) {
			if (!hfa_get_data(hashfast, info->op_name, 32 / 4)) {
				applog(LOG_WARNING, "%s %d: OP_NAME failed! Failure to get op_name data",
			       	hashfast->drv->name, hashfast->device_id);
				goto out;
			}
			info->has_opname = info->opname_valid = true;
			applog(LOG_DEBUG, "%s: Returned an OP_NAME", hashfast->drv->name);
			for (i = 0; i < 32; i++) {
				if (i > 0 && info->op_name[i] == '\0')
					break;
				/* Make sure the op_name is valid ascii only */
				if (info->op_name[i] < 32 || info->op_name[i] > 126) {
					info->opname_valid = false;
					break;
				}
			}
		}
	}

	info->cgpu = hashfast;
	/* Look for a matching zombie instance and inherit values from it if it
	 * exists. */
	if (info->has_opname && info->opname_valid) {
		info->old_cgpu = hfa_old_device(hashfast, info);
		if (info->old_cgpu) {
			struct hashfast_info *cinfo = info->old_cgpu->device_data;

			applog(LOG_NOTICE, "%s: Found old instance by op name %s at device %d",
			hashfast->drv->name, info->op_name, info->old_cgpu->device_id);
			info->resets = ++cinfo->resets;
			info->hash_clock_rate = cinfo->hash_clock_rate;
		} else {
			applog(LOG_NOTICE, "%s: Found device with name %s", hashfast->drv->name,
			       info->op_name);
			hfa_check_options(info);
		}
	}

out:
	if (!ret) {
		if (!hashfast->usbinfo.nodev)
			hfa_clear_readbuf(hashfast);
		hashfast->device_data = NULL;
		free(info);
	}
	return ret;
}

static bool hfa_initialise(struct cgpu_info *hashfast)
{
	int err = 7;

	if (hashfast->usbinfo.nodev)
		return false;

	if (!hfa_clear_readbuf(hashfast))
		return false;
#ifdef WIN32
	err = usb_transfer(hashfast, 0, 9, 1, 0, C_ATMEL_RESET);
	if (!err)
		err = usb_transfer(hashfast, 0x21, 0x22, 0, 0, C_ATMEL_OPEN);
	if (!err) {
		uint32_t buf[2];

		/* Magic sequence to reset device only really needed for windows
		 * but harmless on linux. */
		buf[0] = 0x80250000;
		buf[1] = 0x00000800;
		err = usb_transfer_data(hashfast, 0x21, 0x20, 0x0000, 0, buf,
					7, C_ATMEL_INIT);
	}
	if (err < 0) {
		applog(LOG_INFO, "%s %s: Failed to open with error %s",
		       hashfast->drv->name, hashfast->unique_id, libusb_error_name(err));
	}
#endif
	/* Must have transmitted init sequence sized buffer */
	return (err == 7);
}

static void hfa_dfu_boot(struct cgpu_info *hashfast)
{
	bool ret;

	if (unlikely(hashfast->usbinfo.nodev))
		return;

	ret = hfa_send_frame(hashfast, HF_USB_CMD(OP_DFU), 0, NULL, 0);
	applog(LOG_WARNING, "%s %s: %03d:%03d DFU Boot %s", hashfast->drv->name, hashfast->unique_id,
	       hashfast->usbinfo.bus_number, hashfast->usbinfo.device_address,
	       ret ? "Succeeded" : "Failed");
}

static struct cgpu_info *hfa_detect_one(libusb_device *dev, struct usb_find_devices *found)
{
	struct cgpu_info *hashfast;

	hashfast = usb_alloc_cgpu(&hashfast_drv, HASHFAST_MINER_THREADS);
	if (!hashfast)
		quit(1, "Failed to usb_alloc_cgpu hashfast");
	hashfast->unique_id = "";

	if (!usb_init(hashfast, dev, found)) {
		hashfast = usb_free_cgpu(hashfast);
		return NULL;
	}

	hashfast->usbdev->usb_type = USB_TYPE_STD;

	if (!hfa_initialise(hashfast)) {
		hashfast = usb_free_cgpu(hashfast);
		return NULL;
	}
	if (opt_hfa_dfu_boot) {
		hfa_dfu_boot(hashfast);
		hashfast = usb_free_cgpu(hashfast);
		opt_hfa_dfu_boot = false;
		return NULL;
	}
	if (!hfa_detect_common(hashfast)) {
		usb_uninit(hashfast);
		hashfast = usb_free_cgpu(hashfast);
		return NULL;
	}
	if (!add_cgpu(hashfast))
		return NULL;

	if (opt_hfa_name) {
		struct hashfast_info *info = hashfast->device_data;

		strncpy(info->op_name, opt_hfa_name, 30);
		applog(LOG_NOTICE, "%s %d %03d:%03d: Writing name %s", hashfast->drv->name,
		       hashfast->device_id, hashfast->usbinfo.bus_number, hashfast->usbinfo.device_address,
		       info->op_name);
		hfa_write_opname(hashfast, info);
		opt_hfa_name = NULL;
	}

	return hashfast;
}

static void hfa_detect(bool __maybe_unused hotplug)
{
	/* Set up the CRC tables only once. */
	if (!hfa_crc8_set)
		hfa_init_crc8();
	usb_detect(&hashfast_drv, hfa_detect_one);
}

static bool hfa_get_packet(struct cgpu_info *hashfast, struct hf_header *h)
{
	uint8_t hcrc;
	bool ret;

	if (unlikely(hashfast->usbinfo.nodev))
		return false;

	ret = hfa_get_header(hashfast, h, &hcrc);
	if (unlikely(!ret))
		goto out;
	if (unlikely(h->crc8 != hcrc)) {
		applog(LOG_WARNING, "%s %s: Bad CRC %d vs %d, discarding packet",
		       hashfast->drv->name, hashfast->unique_id, h->crc8, hcrc);
		ret = false;
		goto out;
	}
	if (h->data_length > 0)
		ret = hfa_get_data(hashfast, (char *)(h + 1), h->data_length);
	if (unlikely(!ret)) {
		applog(LOG_WARNING, "%s %s: Failed to get data associated with header",
		       hashfast->drv->name, hashfast->unique_id);
	}

out:
	return ret;
}

static void hfa_running_shutdown(struct cgpu_info *hashfast, struct hashfast_info *info);

static void hfa_parse_gwq_status(struct cgpu_info *hashfast, struct hashfast_info *info,
				 struct hf_header *h)
{
	struct hf_gwq_data *g = (struct hf_gwq_data *)(h + 1);
	struct work *work;

	applog(LOG_DEBUG, "%s %s: OP_GWQ_STATUS, device_head %4d tail %4d my tail %4d shed %3d inflight %4d",
	       hashfast->drv->name, hashfast->unique_id, g->sequence_head, g->sequence_tail, info->hash_sequence_tail,
	       g->shed_count, HF_SEQUENCE_DISTANCE(info->hash_sequence_head,g->sequence_tail));

	/* This is a special flag that the thermal overload has been tripped */
	if (unlikely(h->core_address & 0x80)) {
		applog(LOG_ERR, "%s %s: Thermal overload tripped! Shutting down device",
		       hashfast->drv->name, hashfast->unique_id);
		hfa_running_shutdown(hashfast, info);
		usb_nodev(hashfast);
		return;
	}

	mutex_lock(&info->lock);
	info->raw_hashes += g->hash_count;
	info->device_sequence_head = g->sequence_head;
	info->device_sequence_tail = g->sequence_tail;
	info->shed_count = g->shed_count;
	/* Free any work that is no longer required */
	while (info->device_sequence_tail != info->hash_sequence_tail) {
		if (++info->hash_sequence_tail >= info->num_sequence)
			info->hash_sequence_tail = 0;
		if (unlikely(!(work = info->works[info->hash_sequence_tail]))) {
			applog(LOG_ERR, "%s %s: Bad work sequence tail %d head %d devhead %d devtail %d sequence %d",
			       hashfast->drv->name, hashfast->unique_id, info->hash_sequence_tail,
			       info->hash_sequence_head, info->device_sequence_head,
			       info->device_sequence_tail, info->num_sequence);
			hashfast->shutdown = true;
			usb_nodev(hashfast);
			break;
		}
		applog(LOG_DEBUG, "%s %s: Completing work on hash_sequence_tail %d",
		       hashfast->drv->name, hashfast->unique_id, info->hash_sequence_tail);
		free_work(work);
		info->works[info->hash_sequence_tail] = NULL;
	}
	mutex_unlock(&info->lock);
}

/* Board temperature conversion */
static float board_temperature(uint16_t adc)
{
	float t, r, f, b;

	if (adc < 40 || adc > 650)
		return((float) 0.0);	// Bad count

	b = 3590.0;
	f = (float)adc / 1023.0;
	r = 1.0 / (1.0 / f - 1.0);
	t = log(r) / b;
	t += 1.0 / (25.0 + 273.15);
	t = 1.0 / t - 273.15;

	return t;
}

static void hfa_update_die_status(struct cgpu_info *hashfast, struct hashfast_info *info,
				  struct hf_header *h)
{
	struct hf_g1_die_data *d = (struct hf_g1_die_data *)(h + 1), *ds;
	int num_included = (h->data_length * 4) / sizeof(struct hf_g1_die_data);
	int i, j, die = h->chip_address;

	float die_temperature, board_temp;
	float core_voltage[6];

	// Copy in the data. They're numbered sequentially from the starting point
	ds = info->die_status + h->chip_address;
	for (i = 0; i < num_included; i++)
		memcpy(ds++, d++, sizeof(struct hf_g1_die_data));

	for (i = 0, d = &info->die_status[h->chip_address]; i < num_included; i++, d++) {
		die += i;
		die_temperature = GN_DIE_TEMPERATURE(d->die.die_temperature);
		/* Sanity checking */
		if (unlikely(die_temperature > 255))
			die_temperature = info->die_data[die].temp;
		else
			info->die_data[die].temp = die_temperature;
		board_temp = board_temperature(d->temperature);
		if (unlikely(board_temp > 255))
			board_temp = info->die_data[die].board_temp;
		else
			info->die_data[die].board_temp = board_temp;
		for (j = 0; j < 6; j++)
			core_voltage[j] = GN_CORE_VOLTAGE(d->die.core_voltage[j]);

		applog(LOG_DEBUG, "%s %s: die %2d: OP_DIE_STATUS Temps die %.1fC board %.1fC vdd's %.2f %.2f %.2f %.2f %.2f %.2f",
			hashfast->drv->name, hashfast->unique_id, die, die_temperature, board_temp,
			core_voltage[0], core_voltage[1], core_voltage[2],
			core_voltage[3], core_voltage[4], core_voltage[5]);
		// XXX Convert board phase currents, voltage, temperature
	}
	if (die == info->asic_count - 1) {
		/* We have a full set of die temperatures, find the highest
		 * current temperature. */
		float max_temp = 0;

		info->temp_updates++;

		for (die = 0; die < info->asic_count; die++) {
			if (info->die_data[die].temp > max_temp)
				max_temp = info->die_data[die].temp;
			if (info->die_data[die].board_temp > max_temp)
				max_temp = info->die_data[die].board_temp;
		}
		/* Exponentially change the max_temp to smooth out troughs. */
		hashfast->temp = hashfast->temp * 0.63 + max_temp * 0.37;
	}

	if (unlikely(hashfast->temp >= opt_hfa_overheat)) {
		/* -1 means new overheat condition */
		if (!info->overheat)
			info->overheat = -1;
	} else if (unlikely(info->overheat && hashfast->temp < opt_hfa_overheat - HFA_TEMP_HYSTERESIS))
		info->overheat = 0;
}

static void hfa_parse_nonce(struct thr_info *thr, struct cgpu_info *hashfast,
			    struct hashfast_info *info, struct hf_header *h)
{
	struct hf_candidate_nonce *n = (struct hf_candidate_nonce *)(h + 1);
	int i, num_nonces = h->data_length / U32SIZE(sizeof(struct hf_candidate_nonce));

	applog(LOG_DEBUG, "%s %s: OP_NONCE: %2d/%2d:, num_nonces %d hdata 0x%04x",
	       hashfast->drv->name, hashfast->unique_id, h->chip_address, h->core_address, num_nonces, h->hdata);
	for (i = 0; i < num_nonces; i++, n++) {
		struct work *work = NULL;

		applog(LOG_DEBUG, "%s %s: OP_NONCE: %2d: %2d: ntime %2d sequence %4d nonce 0x%08x",
		       hashfast->drv->name, hashfast->unique_id, h->chip_address, i, n->ntime & HF_NTIME_MASK, n->sequence, n->nonce);

		if (n->sequence < info->usb_init_base.sequence_modulus) {
			// Find the job from the sequence number
			mutex_lock(&info->lock);
			work = info->works[n->sequence];
			mutex_unlock(&info->lock);
		} else {
			applog(LOG_INFO, "%s %s: OP_NONCE: Sequence out of range %4d max %4d",
			       hashfast->drv->name, hashfast->unique_id, n->sequence, info->usb_init_base.sequence_modulus);
		}

		if (unlikely(!work)) {
			info->no_matching_work++;
			applog(LOG_INFO, "%s %s: No matching work!", hashfast->drv->name, hashfast->unique_id);
		} else {
			applog(LOG_DEBUG, "%s %s: OP_NONCE: sequence %d: submitting nonce 0x%08x ntime %d",
			       hashfast->drv->name, hashfast->unique_id, n->sequence, n->nonce, n->ntime & HF_NTIME_MASK);
			if (submit_noffset_nonce(thr, work, n->nonce, n->ntime & HF_NTIME_MASK)) {
				mutex_lock(&info->lock);
				info->hash_count += 0xffffffffull * work->device_diff;
				mutex_unlock(&info->lock);
			}
#if 0	/* Not used */
			if (unlikely(n->ntime & HF_NONCE_SEARCH)) {
				/* This tells us there is another share in the
				 * next 128 nonces */
				applog(LOG_DEBUG, "%s %s: OP_NONCE: SEARCH PROXIMITY EVENT FOUND",
				       hashfast->drv->name, hashfast->unique_id);
			}
#endif
		}
	}
}

static void hfa_update_die_statistics(struct hashfast_info *info, struct hf_header *h)
{
	struct hf_statistics *s = (struct hf_statistics *)(h + 1);
	struct hf_long_statistics *l;

	// Accumulate the data
	l = info->die_statistics + h->chip_address;

	l->rx_header_crc += s->rx_header_crc;
	l->rx_body_crc += s->rx_body_crc;
	l->rx_header_timeouts += s->rx_header_timeouts;
	l->rx_body_timeouts += s->rx_body_timeouts;
	l->core_nonce_fifo_full += s->core_nonce_fifo_full;
	l->array_nonce_fifo_full += s->array_nonce_fifo_full;
	l->stats_overrun += s->stats_overrun;
}

static void hfa_update_stats1(struct cgpu_info *hashfast, struct hashfast_info *info,
			      struct hf_header *h)
{
	struct hf_long_usb_stats1 *s1 = &info->stats1;
	struct hf_usb_stats1 *sd = (struct hf_usb_stats1 *)(h + 1);

	s1->usb_rx_preambles += sd->usb_rx_preambles;
	s1->usb_rx_receive_byte_errors += sd->usb_rx_receive_byte_errors;
	s1->usb_rx_bad_hcrc += sd->usb_rx_bad_hcrc;

	s1->usb_tx_attempts += sd->usb_tx_attempts;
	s1->usb_tx_packets += sd->usb_tx_packets;
	s1->usb_tx_timeouts += sd->usb_tx_timeouts;
	s1->usb_tx_incompletes += sd->usb_tx_incompletes;
	s1->usb_tx_endpointstalled += sd->usb_tx_endpointstalled;
	s1->usb_tx_disconnected += sd->usb_tx_disconnected;
	s1->usb_tx_suspended += sd->usb_tx_suspended;
#if 0
	/* We don't care about UART stats so they're not in our struct */
	s1->uart_tx_queue_dma += sd->uart_tx_queue_dma;
	s1->uart_tx_interrupts += sd->uart_tx_interrupts;

	s1->uart_rx_preamble_ints += sd->uart_rx_preamble_ints;
	s1->uart_rx_missed_preamble_ints += sd->uart_rx_missed_preamble_ints;
	s1->uart_rx_header_done += sd->uart_rx_header_done;
	s1->uart_rx_data_done += sd->uart_rx_data_done;
	s1->uart_rx_bad_hcrc += sd->uart_rx_bad_hcrc;
	s1->uart_rx_bad_dma += sd->uart_rx_bad_dma;
	s1->uart_rx_short_dma += sd->uart_rx_short_dma;
	s1->uart_rx_buffers_full += sd->uart_rx_buffers_full;
#endif
	if (sd->max_tx_buffers >  s1->max_tx_buffers)
		s1->max_tx_buffers = sd->max_tx_buffers;
	if (sd->max_rx_buffers >  s1->max_rx_buffers)
		s1->max_rx_buffers = sd->max_rx_buffers;

	applog(LOG_DEBUG, "%s %s: OP_USB_STATS1:", hashfast->drv->name, hashfast->unique_id);
	applog(LOG_DEBUG, "      usb_rx_preambles:             %6d", sd->usb_rx_preambles);
	applog(LOG_DEBUG, "      usb_rx_receive_byte_errors:   %6d", sd->usb_rx_receive_byte_errors);
	applog(LOG_DEBUG, "      usb_rx_bad_hcrc:              %6d", sd->usb_rx_bad_hcrc);

	applog(LOG_DEBUG, "      usb_tx_attempts:              %6d", sd->usb_tx_attempts);
	applog(LOG_DEBUG, "      usb_tx_packets:               %6d", sd->usb_tx_packets);
	applog(LOG_DEBUG, "      usb_tx_timeouts:              %6d", sd->usb_tx_timeouts);
	applog(LOG_DEBUG, "      usb_tx_incompletes:           %6d", sd->usb_tx_incompletes);
	applog(LOG_DEBUG, "      usb_tx_endpointstalled:       %6d", sd->usb_tx_endpointstalled);
	applog(LOG_DEBUG, "      usb_tx_disconnected:          %6d", sd->usb_tx_disconnected);
	applog(LOG_DEBUG, "      usb_tx_suspended:             %6d", sd->usb_tx_suspended);
#if 0
	applog(LOG_DEBUG, "      uart_tx_queue_dma:            %6d", sd->uart_tx_queue_dma);
	applog(LOG_DEBUG, "      uart_tx_interrupts:           %6d", sd->uart_tx_interrupts);

	applog(LOG_DEBUG, "      uart_rx_preamble_ints:        %6d", sd->uart_rx_preamble_ints);
	applog(LOG_DEBUG, "      uart_rx_missed_preamble_ints: %6d", sd->uart_rx_missed_preamble_ints);
	applog(LOG_DEBUG, "      uart_rx_header_done:          %6d", sd->uart_rx_header_done);
	applog(LOG_DEBUG, "      uart_rx_data_done:            %6d", sd->uart_rx_data_done);
	applog(LOG_DEBUG, "      uart_rx_bad_hcrc:             %6d", sd->uart_rx_bad_hcrc);
	applog(LOG_DEBUG, "      uart_rx_bad_dma:              %6d", sd->uart_rx_bad_dma);
	applog(LOG_DEBUG, "      uart_rx_short_dma:            %6d", sd->uart_rx_short_dma);
	applog(LOG_DEBUG, "      uart_rx_buffers_full:         %6d", sd->uart_rx_buffers_full);
#endif
	applog(LOG_DEBUG, "      max_tx_buffers:               %6d", sd->max_tx_buffers);
	applog(LOG_DEBUG, "      max_rx_buffers:               %6d", sd->max_rx_buffers);
}

static void hfa_parse_notice(struct cgpu_info *hashfast, struct hf_header *h)
{
	struct hf_usb_notice_data *d;

	if (h->data_length == 0) {
		applog(LOG_DEBUG, "%s %s: Received OP_USB_NOTICE with zero data length",
		       hashfast->drv->name, hashfast->unique_id);
		return;
	}
	d = (struct hf_usb_notice_data *)(h + 1);
	/* FIXME Do something with the notification code d->extra_data here */
	applog(LOG_NOTICE, "%s %s NOTICE: %s", hashfast->drv->name, hashfast->unique_id, d->message);
}

static void hfa_parse_settings(struct cgpu_info *hashfast, struct hf_header *h)
{
	struct hashfast_info *info = hashfast->device_data;
	struct hf_settings_data *op_settings_data = (struct hf_settings_data *)(h + 1);

	// Check if packet size, revision and magic are matching
	if ((h->data_length * 4 == sizeof(struct hf_settings_data)) &&
	    (h->core_address == 0) &&
	    (op_settings_data->revision == 1) &&
	    (op_settings_data->magic == HFA_MAGIC_SETTINGS_VALUE))
	{
		applog(LOG_NOTICE, "%s: Device settings (%dMHz@%dmV,%dMHz@%dmV,%dMHz@%dmV,%dMHz@%dmV)", hashfast->drv->name,
			op_settings_data->frequency0, op_settings_data->voltage0,
			op_settings_data->frequency1, op_settings_data->voltage1,
			op_settings_data->frequency2, op_settings_data->voltage2,
			op_settings_data->frequency3, op_settings_data->voltage3);
		// Set voltage only when current voltage values are different
		if ((info->hash_voltage != 0) &&
		    ((op_settings_data->voltage0 != info->hash_voltage) ||
		     (op_settings_data->voltage1 != info->hash_voltage) ||
		     (op_settings_data->voltage2 != info->hash_voltage) ||
		     (op_settings_data->voltage3 != info->hash_voltage))) {
			applog(LOG_NOTICE, "%s: Setting default clock and voltage to %dMHz@%dmV",
				hashfast->drv->name, info->hash_clock_rate, info->hash_voltage);
			hfa_set_voltages(hashfast, info);
		}
	}
}

static void *hfa_read(void *arg)
{
	struct thr_info *thr = (struct thr_info *)arg;
	struct cgpu_info *hashfast = thr->cgpu;
	struct hashfast_info *info = hashfast->device_data;
	char threadname[16];

	snprintf(threadname, sizeof(threadname), "%d/%sRead", hashfast->device_id, hashfast->drv->name);
	RenameThread(threadname);

	while (likely(!hashfast->shutdown)) {
		char buf[512];
		struct hf_header *h = (struct hf_header *)buf;
		bool ret;

		mutex_lock(&info->rlock);
		ret = hfa_get_packet(hashfast, h);
		mutex_unlock(&info->rlock);

		if (unlikely(hashfast->usbinfo.nodev))
			break;

		if (unlikely(!ret))
			continue;

		switch (h->operation_code) {
			case OP_GWQ_STATUS:
				hfa_parse_gwq_status(hashfast, info, h);
				break;
			case OP_DIE_STATUS:
				hfa_update_die_status(hashfast, info, h);
				break;
			case OP_NONCE:
				hfa_parse_nonce(thr, hashfast, info, h);
				break;
			case OP_STATISTICS:
				hfa_update_die_statistics(info, h);
				break;
			case OP_USB_STATS1:
				hfa_update_stats1(hashfast, info, h);
				break;
			case OP_USB_NOTICE:
				hfa_parse_notice(hashfast, h);
				break;
			case OP_SETTINGS:
				hfa_parse_settings(hashfast, h);
				break;
			case OP_POWER:
			case OP_PING:
				/* Do nothing */
				break;
			default:
				if (h->operation_code == OP_FAN) {
					applog(LOG_NOTICE, "%s %s: Firmware upgrade required to support fan control",
					       hashfast->drv->name, hashfast->unique_id);
					opt_hfa_target = 0;
					break;
				}
				applog(LOG_WARNING, "%s %s: Unhandled operation code %d",
				       hashfast->drv->name, hashfast->unique_id, h->operation_code);
				break;
		}
		/* Make sure we send something to the device at least every 5
		 * seconds so it knows the driver is still alive for when we
		 * run out of work. The read thread never blocks so is the
		 * best place to do this. */
		if (time(NULL) - info->last_send > 5)
			hfa_send_frame(hashfast, HF_USB_CMD(OP_PING), 0, NULL, 0);
	}
	applog(LOG_DEBUG, "%s %s: Shutting down read thread", hashfast->drv->name, hashfast->unique_id);

	return NULL;
}

static void hfa_set_fanspeed(struct cgpu_info *hashfast, struct hashfast_info *info,
			     int fanspeed);

static bool hfa_init(struct thr_info *thr)
{
	struct cgpu_info *hashfast = thr->cgpu;
	struct hashfast_info *info = hashfast->device_data;
	struct timeval now;
	bool ret;
	int i;

	if (hashfast->usbinfo.nodev)
		return false;

	/* hashfast_reset should fill in details for info */
	ret = hfa_reset(hashfast, info);

	// The per-die status array
	info->die_status = calloc(info->asic_count, sizeof(struct hf_g1_die_data));
	if (unlikely(!(info->die_status)))
		quit(1, "Failed to calloc die_status");

	info->die_data = calloc(info->asic_count, sizeof(struct hf_die_data));
	if (unlikely(!(info->die_data)))
		quit(1, "Failed to calloc die_data");
	for (i = 0; i < info->asic_count; i++)
		info->die_data[i].hash_clock = info->base_clock;

	// The per-die statistics array
	info->die_statistics = calloc(info->asic_count, sizeof(struct hf_long_statistics));
	if (unlikely(!(info->die_statistics)))
		quit(1, "Failed to calloc die_statistics");

	info->works = calloc(sizeof(struct work *), info->num_sequence);
	if (!info->works)
		quit(1, "Failed to calloc info works in hfa_detect_common");
	if (!ret)
		goto out;

	/* We will have extracted the serial number by now */
	if (info->has_opname && !info->opname_valid)
		hfa_choose_opname(hashfast, info);

	/* Use the opname as the displayed unique identifier */
	hashfast->unique_id = info->op_name;

	/* Inherit the old device id */
	if (info->old_cgpu)
		hashfast->device_id = info->old_cgpu->device_id;

	/* If we haven't found a matching old instance, we might not have
	 * a valid op_name yet or lack support so try to match based on
	 * serial number. */
	if (!info->old_cgpu)
		info->old_cgpu = hfa_old_device(hashfast, info);

	if (!info->has_opname && info->old_cgpu) {
		struct hashfast_info *cinfo = info->old_cgpu->device_data;

		applog(LOG_NOTICE, "%s: Found old instance by serial number %08x at device %d",
		       hashfast->drv->name, info->serial_number, info->old_cgpu->device_id);
		info->resets = ++cinfo->resets;
		/* Set the device with the last hash_clock_rate if it's
		 * different. */
		if (info->hash_clock_rate != cinfo->hash_clock_rate) {
			info->hash_clock_rate = cinfo->hash_clock_rate;
			hfa_set_clock(hashfast, info);
		}
	}

	// Read current device settings if voltage was set in options
	if (info->hash_voltage != 0)
		hfa_send_generic_frame(hashfast, OP_SETTINGS, 0x00, 0x00, HFA_MAGIC_SETTINGS_VALUE, NULL, 0);

	mutex_init(&info->lock);
	mutex_init(&info->rlock);
	if (pthread_create(&info->read_thr, NULL, hfa_read, (void *)thr))
		quit(1, "Failed to pthread_create read thr in hfa_prepare");

	cgtime(&now);
	get_datestamp(hashfast->init, sizeof(hashfast->init), &now);
	hashfast->last_device_valid_work = time(NULL);
	hfa_set_fanspeed(hashfast, info, opt_hfa_fan_default);
out:
	if (hashfast->usbinfo.nodev)
		ret = false;

	if (!ret) {
		hfa_clear_readbuf(hashfast);
		free(info);
		hashfast->device_data = NULL;
		usb_nodev(hashfast);
	}

	return ret;
}

/* If this ever returns 0 it means we have shed all the cores which will lead
 * to no work being done which will trigger the watchdog. */
static inline int hfa_basejobs(struct hashfast_info *info)
{
	return info->usb_init_base.inflight_target - info->shed_count;
}

/* Figure out how many jobs to send. */
static int hfa_jobs(struct cgpu_info *hashfast, struct hashfast_info *info)
{
	int ret = 0;

	if (unlikely(info->overheat)) {
		/* Acknowledge and notify of new condition.*/
		if (info->overheat < 0) {
			applog(LOG_WARNING, "%s %s: Hit overheat temp %.1f, throttling!",
			       hashfast->drv->name, hashfast->unique_id, hashfast->temp);
			/* Value of 1 means acknowledged overheat */
			info->overheat = 1;
		}
		goto out;
	}

	mutex_lock(&info->lock);
	ret = hfa_basejobs(info) - HF_SEQUENCE_DISTANCE(info->hash_sequence_head, info->device_sequence_tail);
	/* Place an upper limit on how many jobs to queue to prevent sending
	 * more work than the device can use after a period of outage. */
	if (ret > hfa_basejobs(info))
		ret = hfa_basejobs(info);
	mutex_unlock(&info->lock);

	if (unlikely(ret < 0))
		ret = 0;

out:
	return ret;
}

static void hfa_set_fanspeed(struct cgpu_info *hashfast, struct hashfast_info *info,
			     int fandiff)
{
	const uint8_t opcode = HF_USB_CMD(OP_FAN);
	uint8_t packet[256];
	struct hf_header *p = (struct hf_header *)packet;
	const int tx_length = sizeof(struct hf_header);
	uint16_t hdata;
	int fandata;

	info->fanspeed += fandiff;
	if (info->fanspeed > opt_hfa_fan_max)
		info->fanspeed = opt_hfa_fan_max;
	else if (info->fanspeed < opt_hfa_fan_min)
		info->fanspeed = opt_hfa_fan_min;
	fandata = info->fanspeed * 255 / 100; // Fanspeed is in percent, hdata 0-255
	hdata = fandata; // Use an int first to avoid overflowing uint16_t
	p->preamble = HF_PREAMBLE;
	p->operation_code = hfa_cmds[opcode].cmd;
	p->chip_address = 0xff;
	p->core_address = 1;
	p->hdata = htole16(hdata);
	p->data_length = 0;
	p->crc8 = hfa_crc8(packet);

	__hfa_send_frame(hashfast, opcode, tx_length, packet);
}

static void hfa_increase_clock(struct cgpu_info *hashfast, struct hashfast_info *info,
			       int die)
{
	int i, high_clock = 0, low_clock = info->hash_clock_rate;
	struct hf_die_data *hdd = &info->die_data[die];
	uint32_t diebit = 0x00000001ul << die;
	uint16_t hdata, increase = 10;

	if (hdd->hash_clock + increase > info->hash_clock_rate)
		increase = info->hash_clock_rate - hdd->hash_clock;
	hdd->hash_clock += increase;
	hdata = (WR_MHZ_INCREASE << 12) | increase;
	if (info->clock_offset) {
		for (i = 0; i < info->asic_count; i++) {
			if (info->die_data[i].hash_clock > high_clock)
				high_clock = info->die_data[i].hash_clock;
			if (info->die_data[i].hash_clock < low_clock)
				low_clock = info->die_data[i].hash_clock;
		}
		if (info->firmware_version < 0.5 && low_clock + HFA_CLOCK_MAXDIFF > high_clock) {
			/* We can increase all clocks again */
			for (i = 0; i < info->asic_count; i++) {
				if (i == die) /* We've already added to this die */
					continue;
				info->die_data[i].hash_clock += increase;
			}
			applog(LOG_INFO, "%s %s: Die %d temp below range %.1f, increasing ALL dies by %d",
			       hashfast->drv->name, hashfast->unique_id, die, info->die_data[die].temp, increase);
			hfa_send_frame(hashfast, HF_USB_CMD(OP_WORK_RESTART), hdata, (uint8_t *)NULL, 0);
			info->clock_offset -= increase;
			return;
		}
	}
	applog(LOG_INFO, "%s %s: Die temp below range %.1f, increasing die %d clock to %d",
	       hashfast->drv->name, hashfast->unique_id, info->die_data[die].temp, die, hdd->hash_clock);
	hfa_send_frame(hashfast, HF_USB_CMD(OP_WORK_RESTART), hdata, (uint8_t *)&diebit, 4);
}

static void hfa_decrease_clock(struct cgpu_info *hashfast, struct hashfast_info *info,
			       int die)
{
	struct hf_die_data *hdd = &info->die_data[die];
	uint32_t diebit = 0x00000001ul << die;
	uint16_t hdata, decrease = 20;
	int i, high_clock = 0;

	/* Find the fastest die for comparison */
	for (i = 0; i < info->asic_count; i++) {
		if (info->die_data[i].hash_clock > high_clock)
			high_clock = info->die_data[i].hash_clock;
	}
	if (hdd->hash_clock - decrease < HFA_CLOCK_MIN)
		decrease = hdd->hash_clock - HFA_CLOCK_MIN;
	hdata = (WR_MHZ_DECREASE << 12) | decrease;
	if (info->firmware_version < 0.5 && high_clock >= hdd->hash_clock + HFA_CLOCK_MAXDIFF) {
		/* We can't have huge differences in clocks as it will lead to
		 * starvation of the faster cores so we have no choice but to
		 * slow down all dies to tame this one. */
		for (i = 0; i < info->asic_count; i++)
			info->die_data[i].hash_clock -= decrease;
		applog(LOG_INFO, "%s %s: Die %d temp above range %.1f, decreasing ALL die clocks by %d",
		       hashfast->drv->name, hashfast->unique_id, die, info->die_data[die].temp, decrease);
		hfa_send_frame(hashfast, HF_USB_CMD(OP_WORK_RESTART), hdata, (uint8_t *)NULL, 0);
		info->clock_offset += decrease;
		return;

	}
	hdd->hash_clock -= decrease;
	applog(LOG_INFO, "%s %s: Die temp above range %.1f, decreasing die %d clock to %d",
	       hashfast->drv->name, hashfast->unique_id, info->die_data[die].temp, die, hdd->hash_clock);
	hfa_send_frame(hashfast, HF_USB_CMD(OP_WORK_RESTART), hdata, (uint8_t *)&diebit, 4);
}

/* Adjust clock according to temperature if need be by changing the clock
 * setting and issuing a work restart with the new clock speed. */
static void hfa_temp_clock(struct cgpu_info *hashfast, struct hashfast_info *info)
{
	int temp_change, i, low_clock;
	time_t now_t = time(NULL);
	bool throttled = false;

	if (!opt_hfa_target)
		return;

	/* First find out if any dies are throttled before trying to optimise
	 * fanspeed, and find the slowest clock. */
	low_clock = info->hash_clock_rate;
	for (i = 0; i < info->asic_count ; i++) {
		struct hf_die_data *hdd = &info->die_data[i];

		if (hdd->hash_clock < info->hash_clock_rate)
			throttled = true;
		if (hdd->hash_clock < low_clock)
			low_clock = hdd->hash_clock;
	}

	/* Find the direction of temperature change since we last checked */
	if (info->temp_updates < 5)
		goto dies_only;
	info->temp_updates = 0;
	temp_change = hashfast->temp - info->last_max_temp;
	info->last_max_temp = hashfast->temp;

	/* Adjust fanspeeds first if possible before die speeds, increasing
	 * speed quickly and lowering speed slowly */
	if (hashfast->temp > opt_hfa_target ||
	    (throttled && hashfast->temp >= opt_hfa_target - HFA_TEMP_HYSTERESIS)) {
		/* We should be trying to decrease temperature, if it's not on
		 * its way down. */
		if (info->fanspeed < opt_hfa_fan_max) {
			if (!temp_change)
				hfa_set_fanspeed(hashfast, info, 5);
			else if (temp_change > 0)
				hfa_set_fanspeed(hashfast, info, 10);
		}
	} else if (hashfast->temp >= opt_hfa_target - HFA_TEMP_HYSTERESIS) {
		/* In optimal range, try and maintain the same temp */
		if (temp_change > 0) {
			/* Temp rising, tweak fanspeed up */
			if (info->fanspeed < opt_hfa_fan_max)
				hfa_set_fanspeed(hashfast, info, 2);
		} else if (temp_change < 0) {
			/* Temp falling, tweak fanspeed down */
			if (info->fanspeed > opt_hfa_fan_min)
				hfa_set_fanspeed(hashfast, info, -1);
		}
	} else {
		/* Below optimal range, try and increase temp */
		if (temp_change <= 0 && !throttled) {
			if (info->fanspeed > opt_hfa_fan_min)
				hfa_set_fanspeed(hashfast, info, -1);
		}
	}

dies_only:
	/* Do no restarts at all if there has been one less than 15 seconds
	 * ago */
	if (now_t - info->last_restart < 15)
		return;

	for (i = 1; i <= info->asic_count ; i++) {
		int die = (info->last_die_adjusted + i) % info->asic_count;
		struct hf_die_data *hdd = &info->die_data[die];

		/* Sanity check */
		if (unlikely(hdd->temp == 0.0 || hdd->temp > 255))
			continue;

		/* In target temperature */
		if (hdd->temp >= opt_hfa_target - HFA_TEMP_HYSTERESIS && hdd->temp <= opt_hfa_target)
			continue;

		if (hdd->temp > opt_hfa_target) {
			/* Temp above target range */

			/* Already at min speed */
			if (hdd->hash_clock == HFA_CLOCK_MIN)
				continue;
			/* Have some leeway before throttling speed */
			if (hdd->temp < opt_hfa_target + HFA_TEMP_HYSTERESIS)
				break;
			hfa_decrease_clock(hashfast, info, die);
		} else {
			/* Temp below target range. Only send a restart to
			 * increase speed no more than every 60 seconds. */
			if (now_t - hdd->last_restart < 60)
				continue;

			/* Already at max speed */
			if (hdd->hash_clock == info->hash_clock_rate)
				continue;
			/* Do not increase the clocks on any dies if we have
			 * a forced offset due to wild differences in clocks,
			 * unless this is the slowest one. */
			if (info->clock_offset && hdd->hash_clock > low_clock)
				continue;
			hfa_increase_clock(hashfast, info, die);
		}
		/* Keep track of the last die adjusted since we only adjust
		 * one at a time to ensure we end up iterating over all of
		 * them. */
		info->last_restart = hdd->last_restart = now_t;
		info->last_die_adjusted = die;
		break;
	}
}

static void hfa_running_shutdown(struct cgpu_info *hashfast, struct hashfast_info *info)
{
	int iruntime = cgpu_runtime(hashfast);

	/* If the device has already disapperaed, don't drop the clock in case
	 * it was just unplugged as opposed to a failure. */
	if (hashfast->usbinfo.nodev)
		return;

	/* Only decrease the clock speed if the device has run at this speed
	 * for less than an hour before failing, otherwise the hashrate gains
	 * are worth the occasional restart which takes at most a minute. */
	if (iruntime < 3600 && info->hash_clock_rate > HFA_CLOCK_DEFAULT && opt_hfa_fail_drop) {
		info->hash_clock_rate -= opt_hfa_fail_drop;
		if (info->hash_clock_rate < HFA_CLOCK_DEFAULT)
			info->hash_clock_rate = HFA_CLOCK_DEFAULT;
		if (info->old_cgpu && info->old_cgpu->device_data) {
			struct hashfast_info *cinfo = info->old_cgpu->device_data;

			/* Set the master device's clock speed if this is a copy */
			cinfo->hash_clock_rate = info->hash_clock_rate;
		}
		applog(LOG_WARNING, "%s %s: Decreasing clock speed to %d with reset",
			hashfast->drv->name, hashfast->unique_id, info->hash_clock_rate);
	}

	if (!hfa_send_shutdown(hashfast))
		return;

	if (hashfast->usbinfo.nodev)
		return;

	mutex_lock(&info->rlock);
	hfa_clear_readbuf(hashfast);
	mutex_unlock(&info->rlock);

	usb_nodev(hashfast);
}

static int64_t hfa_scanwork(struct thr_info *thr)
{
	struct cgpu_info *hashfast = thr->cgpu;
	struct hashfast_info *info = hashfast->device_data;
	struct work *base_work = NULL;
	int jobs, ret, cycles = 0;
	double fail_time;
	int64_t hashes;

	if (unlikely(hashfast->usbinfo.nodev)) {
		applog(LOG_WARNING, "%s %s: device disappeared, disabling",
		       hashfast->drv->name, hashfast->unique_id);
		return -1;
	}

	/* Base the fail time on no valid nonces for 25 full nonce ranges at
	 * the current expected hashrate. */
	fail_time = 25.0 * (double)hashfast->drv->max_diff * 0xffffffffull /
		(double)(info->base_clock * 1000000) / hfa_basejobs(info);
	if (unlikely(share_work_tdiff(hashfast) > fail_time)) {
		applog(LOG_WARNING, "%s %s: No valid hashes for over %.0f seconds, shutting down thread",
		       hashfast->drv->name, hashfast->unique_id, fail_time);
		hfa_running_shutdown(hashfast, info);
		return -1;
	}

	if (unlikely(thr->work_restart)) {
restart:
		info->last_restart = time(NULL);
		thr->work_restart = false;
		ret = hfa_send_frame(hashfast, HF_USB_CMD(OP_WORK_RESTART), 0, (uint8_t *)NULL, 0);
		if (unlikely(!ret)) {
			hfa_running_shutdown(hashfast, info);
			return -1;
		}
		/* Give a full allotment of jobs after a restart, not waiting
		 * for the status update telling us how much to give. */
		jobs = hfa_basejobs(info);
	} else {
		/* Only adjust die clocks if there's no restart since two
		 * restarts back to back get ignored. */
		hfa_temp_clock(hashfast, info);
		jobs = hfa_jobs(hashfast, info);
	}

	/* Wait on restart_wait for up to 0.5 seconds or submit jobs as soon as
	 * they're required. */
	while (!jobs && ++cycles < 5) {
		ret = restart_wait(thr, 100);
		if (unlikely(!ret))
			goto restart;
		jobs = hfa_jobs(hashfast, info);
	}

	if (jobs) {
		applog(LOG_DEBUG, "%s %s: Sending %d new jobs", hashfast->drv->name, hashfast->unique_id,
		       jobs);
	}

	while (jobs-- > 0) {
		struct hf_hash_usb op_hash_data;
		struct work *work;
		uint64_t intdiff;
		int i, sequence;
		uint32_t *p;

		/* This is a blocking function if there's no work */
		if (!base_work)
			base_work = get_work(thr, thr->id);

		/* HFA hardware actually had ntime rolling disabled so we
		 * can roll the work ourselves here to minimise the amount of
		 * work we need to generate. */
		if (base_work->drv_rolllimit > jobs) {
			base_work->drv_rolllimit--;
			roll_work(base_work);
			work = make_clone(base_work);
		} else {
			work = base_work;
			base_work = NULL;
		}

		/* Assemble the data frame and send the OP_HASH packet */
		memcpy(op_hash_data.midstate, work->midstate, sizeof(op_hash_data.midstate));
		memcpy(op_hash_data.merkle_residual, work->data + 64, 4);
		p = (uint32_t *)(work->data + 64 + 4);
		op_hash_data.timestamp = *p++;
		op_hash_data.bits = *p++;
		op_hash_data.starting_nonce = 0;
		op_hash_data.nonce_loops = 0;
		op_hash_data.ntime_loops = 0;

		/* Set the number of leading zeroes to look for based on diff.
		 * Diff 1 = 32, Diff 2 = 33, Diff 4 = 34 etc. */
		intdiff = (uint64_t)work->device_diff;
		for (i = 31; intdiff; i++, intdiff >>= 1);
		op_hash_data.search_difficulty = i;
		op_hash_data.group = 0;
		if ((sequence = info->hash_sequence_head + 1) >= info->num_sequence)
			sequence = 0;
		ret = hfa_send_frame(hashfast, OP_HASH, sequence, (uint8_t *)&op_hash_data, sizeof(op_hash_data));
		if (unlikely(!ret)) {
			free_work(work);
			if (base_work)
				free_work(base_work);
			hfa_running_shutdown(hashfast, info);
			return -1;
		}

		mutex_lock(&info->lock);
		info->hash_sequence_head = sequence;
		info->works[info->hash_sequence_head] = work;
		mutex_unlock(&info->lock);

		applog(LOG_DEBUG, "%s %s: OP_HASH sequence %d search_difficulty %d work_difficulty %g",
		       hashfast->drv->name, hashfast->unique_id, info->hash_sequence_head,
		       op_hash_data.search_difficulty, work->work_difficulty);
	}

	if (base_work)
		free_work(base_work);

	/* Only count 2/3 of the hashes to smooth out the hashrate for cycles
	 * that have no hashes added. */
	mutex_lock(&info->lock);
	hashes = info->hash_count / 3 * 2;
	info->calc_hashes += hashes;
	info->hash_count -= hashes;
	mutex_unlock(&info->lock);

	return hashes;
}

static struct api_data *hfa_api_stats(struct cgpu_info *cgpu)
{
	struct hashfast_info *info;
	struct hf_long_usb_stats1 *s1;
	struct api_data *root = NULL;
	struct hf_usb_init_base *db;
	int varint, i;
	char buf[64];

	info = cgpu->device_data;
	if (!info)
		return NULL;

	root = api_add_int(root, "asic count", &info->asic_count, false);
	root = api_add_int(root, "core count", &info->core_count, false);

	root = api_add_double(root, "firmware rev", &info->firmware_version, false);
	root = api_add_double(root, "hardware rev", &info->hardware_version, false);
	db = &info->usb_init_base;
	root = api_add_hex32(root, "serial number", &db->serial_number, true);
	varint = db->hash_clockrate;
	root = api_add_int(root, "base clockrate", &varint, true);
	varint = db->inflight_target;
	root = api_add_int(root, "inflight target", &varint, true);
	varint = db->sequence_modulus;
	root = api_add_int(root, "sequence modulus", &varint, true);
	root = api_add_int(root, "fan percent", &info->fanspeed, false);
	if (info->op_name[0] != '\0')
		root = api_add_string(root, "op name", info->op_name, false);

	s1 = &info->stats1;
	root = api_add_uint64(root, "rx preambles", &s1->usb_rx_preambles, false);
	root = api_add_uint64(root, "rx rcv byte err", &s1->usb_rx_receive_byte_errors, false);
	root = api_add_uint64(root, "rx bad hcrc", &s1->usb_rx_bad_hcrc, false);
	root = api_add_uint64(root, "tx attempts", &s1->usb_tx_attempts, false);
	root = api_add_uint64(root, "tx packets", &s1->usb_tx_packets, false);
	root = api_add_uint64(root, "tx incompletes", &s1->usb_tx_incompletes, false);
	root = api_add_uint64(root, "tx ep stalled", &s1->usb_tx_endpointstalled, false);
	root = api_add_uint64(root, "tx disconnect", &s1->usb_tx_disconnected, false);
	root = api_add_uint64(root, "tx suspend", &s1->usb_tx_suspended, false);
	varint = s1->max_tx_buffers;
	root = api_add_int(root, "max tx buf", &varint, true);
	varint = s1->max_rx_buffers;
	root = api_add_int(root, "max rx buf", &varint, true);

	for (i = 0; i < info->asic_count; i++) {
		struct hf_long_statistics *l;
		struct hf_g1_die_data *d;
		char which[16];
		double val;
		int j;

		if (!info->die_statistics || !info->die_status)
			continue;
		l = &info->die_statistics[i];
		if (!l)
			continue;
		d = &info->die_status[i];
		if (!d)
			continue;
		snprintf(which, sizeof(which), "Asic%d", i);

		snprintf(buf, sizeof(buf), "%s hash clockrate", which);
		root = api_add_int(root, buf, &(info->die_data[i].hash_clock), false);
		snprintf(buf, sizeof(buf), "%s die temperature", which);
		val = GN_DIE_TEMPERATURE(d->die.die_temperature);
		root = api_add_double(root, buf, &val, true);
		snprintf(buf, sizeof(buf), "%s board temperature", which);
		val = board_temperature(d->temperature);
		root = api_add_double(root, buf, &val, true);
		for (j = 0; j < 6; j++) {
			snprintf(buf, sizeof(buf), "%s voltage %d", which, j);
			val = GN_CORE_VOLTAGE(d->die.core_voltage[j]);
			root = api_add_utility(root, buf, &val, true);
		}
		snprintf(buf, sizeof(buf), "%s rx header crc", which);
		root = api_add_uint64(root, buf, &l->rx_header_crc, false);
		snprintf(buf, sizeof(buf), "%s rx body crc", which);
		root = api_add_uint64(root, buf, &l->rx_body_crc, false);
		snprintf(buf, sizeof(buf), "%s rx header to", which);
		root = api_add_uint64(root, buf, &l->rx_header_timeouts, false);
		snprintf(buf, sizeof(buf), "%s rx body to", which);
		root = api_add_uint64(root, buf, &l->rx_body_timeouts, false);
		snprintf(buf, sizeof(buf), "%s cn fifo full", which);
		root = api_add_uint64(root, buf, &l->core_nonce_fifo_full, false);
		snprintf(buf, sizeof(buf), "%s an fifo full", which);
		root = api_add_uint64(root, buf, &l->array_nonce_fifo_full, false);
		snprintf(buf, sizeof(buf), "%s stats overrun", which);
		root = api_add_uint64(root, buf, &l->stats_overrun, false);
	}

	root = api_add_uint64(root, "raw hashcount", &info->raw_hashes, false);
	root = api_add_uint64(root, "calc hashcount", &info->calc_hashes, false);
	root = api_add_int(root, "no matching work", &info->no_matching_work, false);
	root = api_add_uint16(root, "shed count", &info->shed_count, false);
	root = api_add_int(root, "resets", &info->resets, false);

	return root;
}

static void hfa_statline_before(char *buf, size_t bufsiz, struct cgpu_info *hashfast)
{
	struct hashfast_info *info;
	struct hf_g1_die_data *d;
	double max_volt;
	int i;

	if (!hashfast->device_data)
		return;
	info = hashfast->device_data;
	/* Can happen during init sequence */
	if (!info->die_status)
		return;
	max_volt = 0.0;

	for (i = 0; i < info->asic_count; i++) {
		int j;

		d = &info->die_status[i];
		for (j = 0; j < 6; j++) {
			double volt = GN_CORE_VOLTAGE(d->die.core_voltage[j]);

			if (volt > max_volt)
				max_volt = volt;
		}
	}

	tailsprintf(buf, bufsiz, "%3dMHz %3.0fC %3d%% %3.2fV", info->base_clock,
		    hashfast->temp, info->fanspeed, max_volt);
}

/* We cannot re-initialise so just shut down the device for it to hotplug
 * again. */
static void hfa_reinit(struct cgpu_info *hashfast)
{
	if (hashfast && hashfast->device_data)
		hfa_running_shutdown(hashfast, hashfast->device_data);
}

static void hfa_free_all_work(struct hashfast_info *info)
{
	while (info->device_sequence_tail != info->hash_sequence_head) {
		struct work *work;

		if (++info->hash_sequence_tail >= info->num_sequence)
			info->hash_sequence_tail = 0;
		if (unlikely(!(work = info->works[info->hash_sequence_tail])))
			break;
		free_work(work);
		info->works[info->hash_sequence_tail] = NULL;
	}
}

static void hfa_shutdown(struct thr_info *thr)
{
	struct cgpu_info *hashfast = thr->cgpu;
	struct hashfast_info *info = hashfast->device_data;

	hfa_send_shutdown(hashfast);
	pthread_join(info->read_thr, NULL);
	hfa_free_all_work(info);
	hfa_clear_readbuf(hashfast);
	free(info->works);
	free(info->die_statistics);
	free(info->die_status);
	free(info->die_data);
	/* Keep the device data intact to allow new instances to match old
	 * ones. */
}

struct device_drv hashfast_drv = {
	.drv_id = DRIVER_hashfast,
	.dname = "Hashfast",
	.name = "HFA",
	.max_diff = 32.0, // Limit max diff to get some nonces back regardless
	.drv_detect = hfa_detect,
	.thread_init = hfa_init,
	.hash_work = &hash_driver_work,
	.scanwork = hfa_scanwork,
	.get_api_stats = hfa_api_stats,
	.get_statline_before = hfa_statline_before,
	.reinit_device = hfa_reinit,
	.thread_shutdown = hfa_shutdown,
};
