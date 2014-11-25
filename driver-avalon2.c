/*
 * Copyright 2013-2014 Con Kolivas <kernel@kolivas.org>
 * Copyright 2012-2014 Xiangfu <xiangfu@openmobilefree.com>
 * Copyright 2012 Luke Dashjr
 * Copyright 2012 Andrew Smith
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 3 of the License, or (at your option)
 * any later version.  See COPYING for more details.
 */

#include "config.h"

#include <limits.h>
#include <pthread.h>
#include <stdio.h>
#include <sys/time.h>
#include <sys/types.h>
#include <dirent.h>
#include <unistd.h>
#ifndef WIN32
  #include <termios.h>
  #include <sys/stat.h>
  #include <fcntl.h>
  #ifndef O_CLOEXEC
    #define O_CLOEXEC 0
  #endif
#else
  #include <windows.h>
  #include <io.h>
#endif

#include "elist.h"
#include "miner.h"
#include "fpgautils.h"
#include "driver-avalon2.h"
#include "crc.h"
#include "sha2.h"

#define ASSERT1(condition) __maybe_unused static char sizeof_uint32_t_must_be_4[(condition)?1:-1]
ASSERT1(sizeof(uint32_t) == 4);

#define get_fan_pwm(v)	(AVA2_PWM_MAX - (v) * AVA2_PWM_MAX / 100)

int opt_avalon2_freq_min;
int opt_avalon2_freq_max;

int opt_avalon2_fan_min = AVA2_DEFAULT_FAN_MIN;
int opt_avalon2_fan_max = AVA2_DEFAULT_FAN_MAX;
static int avalon2_fan_min = get_fan_pwm(AVA2_DEFAULT_FAN_MIN);
static int avalon2_fan_max = get_fan_pwm(AVA2_DEFAULT_FAN_MAX);

int opt_avalon2_voltage_min;
int opt_avalon2_voltage_max;

int opt_avalon2_overheat = AVALON2_TEMP_OVERHEAT;
int opt_avalon2_polling_delay = AVALON2_DEFAULT_POLLING_DELAY;

enum avalon2_fan_fixed opt_avalon2_fan_fixed = FAN_AUTO;

#define UNPACK32(x, str)			\
{						\
	*((str) + 3) = (uint8_t) ((x)      );	\
	*((str) + 2) = (uint8_t) ((x) >>  8);	\
	*((str) + 1) = (uint8_t) ((x) >> 16);	\
	*((str) + 0) = (uint8_t) ((x) >> 24);	\
}

static void sha256_prehash(const unsigned char *message, unsigned int len, unsigned char *digest)
{
	sha256_ctx ctx;
	int i;
	sha256_init(&ctx);
	sha256_update(&ctx, message, len);

	for (i = 0; i < 8; i++) {
		UNPACK32(ctx.h[i], &digest[i << 2]);
	}
}

static inline uint8_t rev8(uint8_t d)
{
	int i;
	uint8_t out = 0;

	/* (from left to right) */
	for (i = 0; i < 8; i++)
		if (d & (1 << i))
		out |= (1 << (7 - i));

	return out;
}

char *set_avalon2_fan(char *arg)
{
	int val1, val2, ret;

	ret = sscanf(arg, "%d-%d", &val1, &val2);
	if (ret < 1)
		return "No values passed to avalon2-fan";
	if (ret == 1)
		val2 = val1;

	if (val1 < 0 || val1 > 100 || val2 < 0 || val2 > 100 || val2 < val1)
		return "Invalid value passed to avalon2-fan";

	opt_avalon2_fan_min = val1;
	opt_avalon2_fan_max = val2;
	avalon2_fan_min = get_fan_pwm(val1);
	avalon2_fan_max = get_fan_pwm(val2);

	return NULL;
}

char *set_avalon2_fixed_speed(enum avalon2_fan_fixed *f)
{
	*f = FAN_FIXED;
	return NULL;
}

char *set_avalon2_freq(char *arg)
{
	int val1, val2, ret;

	ret = sscanf(arg, "%d-%d", &val1, &val2);
	if (ret < 1)
		return "No values passed to avalon2-freq";
	if (ret == 1)
		val2 = val1;

	if (val1 < AVA2_DEFAULT_FREQUENCY_MIN || val1 > AVA2_DEFAULT_FREQUENCY_MAX ||
	    val2 < AVA2_DEFAULT_FREQUENCY_MIN || val2 > AVA2_DEFAULT_FREQUENCY_MAX ||
	    val2 < val1)
		return "Invalid value passed to avalon2-freq";

	opt_avalon2_freq_min = val1;
	opt_avalon2_freq_max = val2;

	return NULL;
}

char *set_avalon2_voltage(char *arg)
{
	int val1, val2, ret;

	ret = sscanf(arg, "%d-%d", &val1, &val2);
	if (ret < 1)
		return "No values passed to avalon2-voltage";
	if (ret == 1)
		val2 = val1;

	if (val1 < AVA2_DEFAULT_VOLTAGE_MIN || val1 > AVA2_DEFAULT_VOLTAGE_MAX ||
	    val2 < AVA2_DEFAULT_VOLTAGE_MIN || val2 > AVA2_DEFAULT_VOLTAGE_MAX ||
	    val2 < val1)
		return "Invalid value passed to avalon2-voltage";

	opt_avalon2_voltage_min = val1;
	opt_avalon2_voltage_max = val2;

	return NULL;
}

static int avalon2_init_pkg(struct avalon2_pkg *pkg, uint8_t type, uint8_t idx, uint8_t cnt)
{
	unsigned short crc;

	pkg->head[0] = AVA2_H1;
	pkg->head[1] = AVA2_H2;

	pkg->type = type;
	pkg->idx = idx;
	pkg->cnt = cnt;

	crc = crc16(pkg->data, AVA2_P_DATA_LEN);

	pkg->crc[0] = (crc & 0xff00) >> 8;
	pkg->crc[1] = crc & 0x00ff;
	return 0;
}

static int job_idcmp(uint8_t *job_id, char *pool_job_id)
{
	int job_id_len;
	unsigned short crc, crc_expect;

	if (!pool_job_id)
		return 1;

	job_id_len = strlen(pool_job_id);
	crc_expect = crc16((unsigned char *)pool_job_id, job_id_len);

	crc = job_id[0] << 8 | job_id[1];

	if (crc_expect == crc)
		return 0;

	applog(LOG_DEBUG, "Avalon2: job_id not match! [%04x:%04x (%s)]",
	       crc, crc_expect, pool_job_id);

	return 1;
}

static inline int get_temp_max(struct avalon2_info *info)
{
	int i;
	for (i = 0; i < 2 * AVA2_DEFAULT_MODULARS; i++) {
		if (info->temp_max <= info->temp[i])
			info->temp_max = info->temp[i];
	}
	return info->temp_max;
}

static inline int get_current_temp_max(struct avalon2_info *info)
{
	int i;
	int t = info->temp[0];

	for (i = 1; i < 2 * AVA2_DEFAULT_MODULARS; i++) {
		if (info->temp[i] > t)
			t = info->temp[i];
	}
	return t;
}

/* http://www.onsemi.com/pub_link/Collateral/ADP3208D.PDF */
static inline uint32_t encode_voltage(uint32_t v)
{
	return rev8((0x78 - v / 125) << 1 | 1) << 8;
}

static inline uint32_t decode_voltage(uint32_t v)
{
	return (0x78 - (rev8(v >> 8) >> 1)) * 125;
}

static void adjust_fan(struct avalon2_info *info)
{
	int t;

	if (opt_avalon2_fan_fixed == FAN_FIXED) {
		info->fan_pct = opt_avalon2_fan_min;
		info->fan_pwm = get_fan_pwm(info->fan_pct);
		return;
	}

	t = get_current_temp_max(info);

	/* TODO: Add options for temperature range and fan adjust function */
	if (t < 60)
		info->fan_pct = opt_avalon2_fan_min;
	else if (t > 80)
		info->fan_pct = opt_avalon2_fan_max;
	else
		info->fan_pct = (t - 60) * (opt_avalon2_fan_max - opt_avalon2_fan_min) / 20 + opt_avalon2_fan_min;

	info->fan_pwm = get_fan_pwm(info->fan_pct);
}

static inline int mm_cmp_1404(struct avalon2_info *info, int modular)
{
	/* <= 1404 return 1 */
	char *mm_1404 = "1404";
	return strncmp(info->mm_version[modular] + 2, mm_1404, 4) > 0 ? 0 : 1;
}

static inline int mm_cmp_1406(struct avalon2_info *info)
{
	/* <= 1406 return 1 */
	char *mm_1406 = "1406";
	int i;
	for (i = 0; i < AVA2_DEFAULT_MODULARS; i++) {
		if (info->enable[i] &&
		    strncmp(info->mm_version[i] + 2, mm_1406, 4) <= 0)
			return 1;
	}

	return 0;
}

static int decode_pkg(struct thr_info *thr, struct avalon2_ret *ar, uint8_t *pkg)
{
	struct cgpu_info *avalon2 = thr->cgpu;
	struct avalon2_info *info = avalon2->device_data;
	struct pool *pool, *real_pool, *pool_stratum = &info->pool;

	unsigned int expected_crc;
	unsigned int actual_crc;
	uint32_t nonce, nonce2, miner, modular_id;
	int pool_no;
	uint8_t job_id[4];
	int tmp;

	int type = AVA2_GETS_ERROR;

	memcpy((uint8_t *)ar, pkg, AVA2_READ_SIZE);

	if (ar->head[0] == AVA2_H1 && ar->head[1] == AVA2_H2) {
		expected_crc = crc16(ar->data, AVA2_P_DATA_LEN);
		actual_crc = (ar->crc[0] & 0xff) |
			((ar->crc[1] & 0xff) << 8);

		type = ar->type;
		applog(LOG_DEBUG, "Avalon2: %d: expected crc(%04x), actual_crc(%04x)",
		       type, expected_crc, actual_crc);
		if (expected_crc != actual_crc)
			goto out;

		memcpy(&modular_id, ar->data + 28, 4);
		modular_id = be32toh(modular_id);
		if (modular_id > 3)
			modular_id = 0;

		switch(type) {
		case AVA2_P_NONCE:
			applog(LOG_DEBUG, "Avalon2: AVA2_P_NONCE");
			memcpy(&miner, ar->data + 0, 4);
			memcpy(&pool_no, ar->data + 4, 4);
			memcpy(&nonce2, ar->data + 8, 4);
			/* Calc time    ar->data + 12 */
			memcpy(&nonce, ar->data + 16, 4);
			memcpy(job_id, ar->data + 20, 4);

			miner = be32toh(miner);
			pool_no = be32toh(pool_no);
			if (miner >= AVA2_DEFAULT_MINERS ||
			    modular_id >= AVA2_DEFAULT_MINERS ||
			    pool_no >= total_pools ||
			    pool_no < 0) {
				applog(LOG_DEBUG, "Avalon2: Wrong miner/pool/id no %d,%d,%d", miner, pool_no, modular_id);
				break;
			} else
				info->matching_work[modular_id * AVA2_DEFAULT_MINERS + miner]++;
			nonce2 = be32toh(nonce2);
			nonce = be32toh(nonce);
			nonce -= 0x180;

			applog(LOG_DEBUG, "Avalon2: Found! %d: (%08x) (%08x)",
			       pool_no, nonce2, nonce);

			real_pool = pool = pools[pool_no];
			if (job_idcmp(job_id, pool->swork.job_id)) {
				if (!job_idcmp(job_id, pool_stratum->swork.job_id)) {
					applog(LOG_DEBUG, "Avalon2: Match to previous stratum! (%s)", pool_stratum->swork.job_id);
					pool = pool_stratum;
				} else {
					applog(LOG_ERR, "Avalon2: Cannot match to any stratum! (%s)", pool->swork.job_id);
					break;
				}
			}

			if (submit_nonce2_nonce(thr, pool, real_pool, nonce2, nonce))
				info->failing = false;
			break;
		case AVA2_P_STATUS:
			applog(LOG_DEBUG, "Avalon2: AVA2_P_STATUS");
			memcpy(&tmp, ar->data, 4);
			tmp = be32toh(tmp);
			info->temp[0 + modular_id * 2] = tmp >> 16;
			info->temp[1 + modular_id * 2] = tmp & 0xffff;

			memcpy(&tmp, ar->data + 4, 4);
			tmp = be32toh(tmp);
			info->fan[0 + modular_id * 2] = tmp >> 16;
			info->fan[1 + modular_id * 2] = tmp & 0xffff;

			memcpy(&(info->get_frequency[modular_id]), ar->data + 8, 4);
			memcpy(&(info->get_voltage[modular_id]), ar->data + 12, 4);
			memcpy(&(info->local_work[modular_id]), ar->data + 16, 4);
			memcpy(&(info->hw_work[modular_id]), ar->data + 20, 4);
			memcpy(&(info->power_good[modular_id]), ar->data + 24, 4);

			info->get_frequency[modular_id] = be32toh(info->get_frequency[modular_id]);
			if (info->dev_type[modular_id] == AVA2_ID_AVA3)
				info->get_frequency[modular_id] = info->get_frequency[modular_id] * 768 / 65;
			info->get_voltage[modular_id] = be32toh(info->get_voltage[modular_id]);
			info->local_work[modular_id] = be32toh(info->local_work[modular_id]);
			info->hw_work[modular_id] = be32toh(info->hw_work[modular_id]);

			info->local_works[modular_id] += info->local_work[modular_id];
			info->hw_works[modular_id] += info->hw_work[modular_id];

			info->get_voltage[modular_id] = decode_voltage(info->get_voltage[modular_id]);
			info->power_good[modular_id] = info->power_good[modular_id]  >> 24;

			avalon2->temp = get_temp_max(info);
			break;
		case AVA2_P_ACKDETECT:
			applog(LOG_DEBUG, "Avalon2: AVA2_P_ACKDETECT");
			break;
		case AVA2_P_ACK:
			applog(LOG_DEBUG, "Avalon2: AVA2_P_ACK");
			break;
		case AVA2_P_NAK:
			applog(LOG_DEBUG, "Avalon2: AVA2_P_NAK");
			break;
		default:
			applog(LOG_DEBUG, "Avalon2: Unknown response");
			type = AVA2_GETS_ERROR;
			break;
		}
	}

out:
	return type;
}

static inline int avalon2_gets(struct cgpu_info *avalon2, uint8_t *buf)
{
	int read_amount = AVA2_READ_SIZE, ret = 0;
	uint8_t *buf_back = buf;

	while (true) {
		int err;

		do {
			memset(buf, 0, read_amount);
			err = usb_read(avalon2, (char *)buf, read_amount, &ret, C_AVA2_READ);
			if (unlikely(err && err != LIBUSB_ERROR_TIMEOUT)) {
				applog(LOG_ERR, "Avalon2: Error %d on read in avalon_gets got %d", err, ret);
				return AVA2_GETS_ERROR;
			}
			if (likely(ret >= read_amount)) {
				if (unlikely(buf_back[0] != AVA2_H1 || buf_back[1] != AVA2_H2))
					return AVA2_GETS_ERROR;
				return AVA2_GETS_OK;
			}
			buf += ret;
			read_amount -= ret;
		} while (ret > 0);

		return AVA2_GETS_TIMEOUT;
	}
}

static int avalon2_send_pkg(struct cgpu_info *avalon2, const struct avalon2_pkg *pkg)
{
	int err, amount;
	uint8_t buf[AVA2_WRITE_SIZE];
	int nr_len = AVA2_WRITE_SIZE;

	if (unlikely(avalon2->usbinfo.nodev))
		return AVA2_SEND_ERROR;

	memcpy(buf, pkg, AVA2_WRITE_SIZE);
	err = usb_write(avalon2, (char *)buf, nr_len, &amount, C_AVA2_WRITE);
	if (err || amount != nr_len) {
		applog(LOG_DEBUG, "Avalon2: Send(%d)!", amount);
		usb_nodev(avalon2);
		return AVA2_SEND_ERROR;
	}

	return AVA2_SEND_OK;
}

static void avalon2_stratum_pkgs(struct cgpu_info *avalon2, struct pool *pool)
{
	const int merkle_offset = 36;
	struct avalon2_pkg pkg;
	int i, a, b, tmp;
	unsigned char target[32];
	int job_id_len, n2size;
	unsigned short crc;
	int diff;

	/* Cap maximum diff in order to still get shares */
	diff = pool->swork.diff;
	if (diff > 64)
		diff = 64;
	else if (unlikely(diff < 1))
		diff = 1;

	/* Send out the first stratum message STATIC */
	applog(LOG_DEBUG, "Avalon2: Pool stratum message STATIC: %d, %d, %d, %d, %d",
	       pool->coinbase_len,
	       pool->nonce2_offset,
	       pool->n2size,
	       merkle_offset,
	       pool->merkles);
	memset(pkg.data, 0, AVA2_P_DATA_LEN);
	tmp = be32toh(pool->coinbase_len);
	memcpy(pkg.data, &tmp, 4);

	tmp = be32toh(pool->nonce2_offset);
	memcpy(pkg.data + 4, &tmp, 4);

	n2size = pool->n2size >= 4 ? 4 : pool->n2size;
	tmp = be32toh(n2size);
	memcpy(pkg.data + 8, &tmp, 4);

	tmp = be32toh(merkle_offset);
	memcpy(pkg.data + 12, &tmp, 4);

	tmp = be32toh(pool->merkles);
	memcpy(pkg.data + 16, &tmp, 4);

	tmp = be32toh(diff);
	memcpy(pkg.data + 20, &tmp, 4);

	tmp = be32toh((int)pool->pool_no);
	memcpy(pkg.data + 24, &tmp, 4);

	avalon2_init_pkg(&pkg, AVA2_P_STATIC, 1, 1);
	if (avalon2_send_pkg(avalon2, &pkg))
		return;

	set_target(target, pool->sdiff);
	memcpy(pkg.data, target, 32);
	if (opt_debug) {
		char *target_str;
		target_str = bin2hex(target, 32);
		applog(LOG_DEBUG, "Avalon2: Pool stratum target: %s", target_str);
		free(target_str);
	}
	avalon2_init_pkg(&pkg, AVA2_P_TARGET, 1, 1);
	if (avalon2_send_pkg(avalon2, &pkg))
		return;

	applog(LOG_DEBUG, "Avalon2: Pool stratum message JOBS_ID: %s",
	       pool->swork.job_id);
	memset(pkg.data, 0, AVA2_P_DATA_LEN);

	job_id_len = strlen(pool->swork.job_id);
	crc = crc16((unsigned char *)pool->swork.job_id, job_id_len);
	pkg.data[0] = (crc & 0xff00) >> 8;
	pkg.data[1] = crc & 0x00ff;
	avalon2_init_pkg(&pkg, AVA2_P_JOB_ID, 1, 1);
	if (avalon2_send_pkg(avalon2, &pkg))
		return;

	if (pool->coinbase_len > AVA2_P_COINBASE_SIZE) {
		int coinbase_len_posthash, coinbase_len_prehash;
		uint8_t coinbase_prehash[32];
		coinbase_len_prehash = pool->nonce2_offset - (pool->nonce2_offset % SHA256_BLOCK_SIZE);
		coinbase_len_posthash = pool->coinbase_len - coinbase_len_prehash;
		sha256_prehash(pool->coinbase, coinbase_len_prehash, coinbase_prehash);

		a = (coinbase_len_posthash / AVA2_P_DATA_LEN) + 1;
		b = coinbase_len_posthash % AVA2_P_DATA_LEN;
	        memcpy(pkg.data, coinbase_prehash, 32);
	        avalon2_init_pkg(&pkg, AVA2_P_COINBASE, 1, a + (b ? 1 : 0));
	        if (avalon2_send_pkg(avalon2, &pkg))
			return;
	        applog(LOG_DEBUG, "Avalon2: Pool stratum message modified COINBASE: %d %d", a, b);
	        for (i = 1; i < a; i++) {
	                memcpy(pkg.data, pool->coinbase + coinbase_len_prehash + i * 32 - 32, 32);
	                avalon2_init_pkg(&pkg, AVA2_P_COINBASE, i + 1, a + (b ? 1 : 0));
			if (avalon2_send_pkg(avalon2, &pkg))
	                        return;
	        }
	        if (b) {
	                memset(pkg.data, 0, AVA2_P_DATA_LEN);
			memcpy(pkg.data, pool->coinbase + coinbase_len_prehash + i * 32 - 32, b);
	                avalon2_init_pkg(&pkg, AVA2_P_COINBASE, i + 1, i + 1);
	                if (avalon2_send_pkg(avalon2, &pkg))
	                        return;
	        }
	} else {
		a = pool->coinbase_len / AVA2_P_DATA_LEN;
		b = pool->coinbase_len % AVA2_P_DATA_LEN;
		applog(LOG_DEBUG, "Avalon2: Pool stratum message COINBASE: %d %d", a, b);
		for (i = 0; i < a; i++) {
			memcpy(pkg.data, pool->coinbase + i * 32, 32);
			avalon2_init_pkg(&pkg, AVA2_P_COINBASE, i + 1, a + (b ? 1 : 0));
			if (avalon2_send_pkg(avalon2, &pkg))
				return;
		}
		if (b) {
			memset(pkg.data, 0, AVA2_P_DATA_LEN);
			memcpy(pkg.data, pool->coinbase + i * 32, b);
			avalon2_init_pkg(&pkg, AVA2_P_COINBASE, i + 1, i + 1);
			if (avalon2_send_pkg(avalon2, &pkg))
				return;
		}
	}


	b = pool->merkles;
	applog(LOG_DEBUG, "Avalon2: Pool stratum message MERKLES: %d", b);
	for (i = 0; i < b; i++) {
		memset(pkg.data, 0, AVA2_P_DATA_LEN);
		memcpy(pkg.data, pool->swork.merkle_bin[i], 32);
		avalon2_init_pkg(&pkg, AVA2_P_MERKLES, i + 1, b);
		if (avalon2_send_pkg(avalon2, &pkg))
			return;
	}

	applog(LOG_DEBUG, "Avalon2: Pool stratum message HEADER: 4");
	for (i = 0; i < 4; i++) {
		memset(pkg.data, 0, AVA2_P_HEADER);
		memcpy(pkg.data, pool->header_bin + i * 32, 32);
		avalon2_init_pkg(&pkg, AVA2_P_HEADER, i + 1, 4);
		if (avalon2_send_pkg(avalon2, &pkg))
			return;
	}
}

static void avalon2_initialise(struct cgpu_info *avalon2)
{
	uint32_t ava2_data[2] = { PL2303_VALUE_LINE0, PL2303_VALUE_LINE1 };
	int interface;

	if (avalon2->usbinfo.nodev)
		return;

	interface = usb_interface(avalon2);
	// Set Data Control
	usb_transfer(avalon2, PL2303_VENDOR_OUT, PL2303_REQUEST_VENDOR, 8,
		     interface, C_VENDOR);
	if (avalon2->usbinfo.nodev)
		return;

	usb_transfer(avalon2, PL2303_VENDOR_OUT, PL2303_REQUEST_VENDOR, 9,
		     interface, C_VENDOR);

	if (avalon2->usbinfo.nodev)
		return;

	// Set Line Control
	usb_transfer_data(avalon2, PL2303_CTRL_OUT, PL2303_REQUEST_LINE, PL2303_VALUE_LINE,
			  interface, ava2_data, PL2303_VALUE_LINE_SIZE, C_SETLINE);
	if (avalon2->usbinfo.nodev)
		return;

	// Vendor
	usb_transfer(avalon2, PL2303_VENDOR_OUT, PL2303_REQUEST_VENDOR, PL2303_VALUE_VENDOR,
		     interface, C_VENDOR);

	if (avalon2->usbinfo.nodev)
		return;

	// Set More Line Control ?
	usb_transfer(avalon2, PL2303_CTRL_OUT, PL2303_REQUEST_CTRL, 3, interface, C_SETLINE);
}

static struct cgpu_info *avalon2_detect_one(struct libusb_device *dev, struct usb_find_devices *found)
{
	struct avalon2_info *info;
	int ackdetect;
	int err, amount;
	int tmp, i, j, modular[AVA2_DEFAULT_MODULARS] = {};
	char mm_version[AVA2_DEFAULT_MODULARS][16];

	struct cgpu_info *avalon2 = usb_alloc_cgpu(&avalon2_drv, 1);
	struct avalon2_pkg detect_pkg;
	struct avalon2_ret ret_pkg;

	if (!usb_init(avalon2, dev, found)) {
		applog(LOG_ERR, "Avalon2 failed usb_init");
		avalon2 = usb_free_cgpu(avalon2);
		return NULL;
	}
	avalon2_initialise(avalon2);

	for (j = 0; j < 2; j++) {
		for (i = 0; i < AVA2_DEFAULT_MODULARS; i++) {
			strcpy(mm_version[i], AVA2_MM_VERNULL);
			/* Send out detect pkg */
			memset(detect_pkg.data, 0, AVA2_P_DATA_LEN);
			tmp = be32toh(i);
			memcpy(detect_pkg.data + 28, &tmp, 4);

			avalon2_init_pkg(&detect_pkg, AVA2_P_DETECT, 1, 1);
			avalon2_send_pkg(avalon2, &detect_pkg);
			err = usb_read(avalon2, (char *)&ret_pkg, AVA2_READ_SIZE, &amount, C_AVA2_READ);
			if (err < 0 || amount != AVA2_READ_SIZE) {
				applog(LOG_DEBUG, "%s %d: Avalon2 failed usb_read with err %d amount %d",
				       avalon2->drv->name, avalon2->device_id, err, amount);
				continue;
			}
			ackdetect = ret_pkg.type;
			applog(LOG_DEBUG, "Avalon2 Detect ID[%d]: %d", i, ackdetect);
			if (ackdetect != AVA2_P_ACKDETECT && modular[i] == 0)
				continue;
			modular[i] = 1;
			memcpy(mm_version[i], ret_pkg.data, 15);
			mm_version[i][15] = '\0';
		}
	}
	if (!modular[0] && !modular[1] && !modular[2] && !modular[3]) {
		applog(LOG_DEBUG, "Not an Avalon2 device");
		usb_uninit(avalon2);
		usb_free_cgpu(avalon2);
		return NULL;
	}

	/* We have a real Avalon! */
	avalon2->threads = AVA2_MINER_THREADS;
	add_cgpu(avalon2);

	update_usb_stats(avalon2);

	applog(LOG_INFO, "%s %d: Found at %s", avalon2->drv->name, avalon2->device_id,
	       avalon2->device_path);

	avalon2->device_data = calloc(sizeof(struct avalon2_info), 1);
	if (unlikely(!(avalon2->device_data)))
		quit(1, "Failed to calloc avalon2_info");

	info = avalon2->device_data;

	info->fan_pwm = get_fan_pwm(AVA2_DEFAULT_FAN_PWM);
	info->temp_max = 0;

	for (i = 0; i < AVA2_DEFAULT_MODULARS; i++) {
		strcpy(info->mm_version[i], mm_version[i]);
		info->modulars[i] = modular[i];	/* Enable modular */
		info->enable[i] = modular[i];
		info->dev_type[i] = AVA2_ID_AVAX;

		if (!strncmp((char *)&(info->mm_version[i]), AVA2_FW2_PREFIXSTR, 2)) {
			info->dev_type[i] = AVA2_ID_AVA2;
			info->set_voltage = AVA2_DEFAULT_VOLTAGE_MIN;
			info->set_frequency = AVA2_DEFAULT_FREQUENCY;
		}
		if (!strncmp((char *)&(info->mm_version[i]), AVA2_FW3_PREFIXSTR, 2)) {
			info->dev_type[i] = AVA2_ID_AVA3;
			info->set_voltage = AVA2_AVA3_VOLTAGE;
			info->set_frequency = AVA2_AVA3_FREQUENCY;
		}
	}

	if (!opt_avalon2_voltage_min)
		opt_avalon2_voltage_min = opt_avalon2_voltage_max = info->set_voltage;
	if (!opt_avalon2_freq_min)
		opt_avalon2_freq_min = opt_avalon2_freq_max = info->set_frequency;

	return avalon2;
}

static inline void avalon2_detect(bool __maybe_unused hotplug)
{
	usb_detect(&avalon2_drv, avalon2_detect_one);
}

static bool avalon2_prepare(struct thr_info *thr)
{
	struct cgpu_info *avalon2 = thr->cgpu;
	struct avalon2_info *info = avalon2->device_data;

	cglock_init(&info->pool.data_lock);

	return true;
}

static int polling(struct thr_info *thr, struct cgpu_info *avalon2, struct avalon2_info *info)
{
	struct avalon2_pkg send_pkg;
	struct avalon2_ret ar;
	int i, tmp;

	for (i = 0; i < AVA2_DEFAULT_MODULARS; i++) {
		if (info->modulars[i] && info->enable[i]) {
			uint8_t result[AVA2_READ_SIZE];
			int ret;

			cgsleep_ms(opt_avalon2_polling_delay);
			memset(send_pkg.data, 0, AVA2_P_DATA_LEN);

			tmp = be32toh(info->led_red[i]); /* RED LED */
			memcpy(send_pkg.data + 12, &tmp, 4);

			tmp = be32toh(i); /* ID */
			memcpy(send_pkg.data + 28, &tmp, 4);
			if (info->led_red[i] && mm_cmp_1404(info, i)) {
				avalon2_init_pkg(&send_pkg, AVA2_P_TEST, 1, 1);
				avalon2_send_pkg(avalon2, &send_pkg);
				info->enable[i] = 0;
				continue;
			} else
				avalon2_init_pkg(&send_pkg, AVA2_P_POLLING, 1, 1);

			avalon2_send_pkg(avalon2, &send_pkg);
			ret = avalon2_gets(avalon2, result);
			if (ret == AVA2_GETS_OK)
				decode_pkg(thr, &ar, result);
		}
	}

	return 0;
}

static void copy_pool_stratum(struct avalon2_info *info, struct pool *pool)
{
	int i;
	int merkles = pool->merkles;
	size_t coinbase_len = pool->coinbase_len;
	struct pool *pool_stratum = &info->pool;

	if (!job_idcmp((unsigned char *)pool->swork.job_id, pool_stratum->swork.job_id))
		return;

	cg_wlock(&pool_stratum->data_lock);
	free(pool_stratum->swork.job_id);
	free(pool_stratum->nonce1);
	free(pool_stratum->coinbase);

	align_len(&coinbase_len);
	pool_stratum->coinbase = calloc(coinbase_len, 1);
	if (unlikely(!pool_stratum->coinbase))
		quit(1, "Failed to calloc pool_stratum coinbase in avalon2");
	memcpy(pool_stratum->coinbase, pool->coinbase, coinbase_len);


	for (i = 0; i < pool_stratum->merkles; i++)
		free(pool_stratum->swork.merkle_bin[i]);
	if (merkles) {
		pool_stratum->swork.merkle_bin = realloc(pool_stratum->swork.merkle_bin,
						 sizeof(char *) * merkles + 1);
		for (i = 0; i < merkles; i++) {
			pool_stratum->swork.merkle_bin[i] = malloc(32);
			if (unlikely(!pool_stratum->swork.merkle_bin[i]))
				quit(1, "Failed to malloc pool_stratum swork merkle_bin");
			memcpy(pool_stratum->swork.merkle_bin[i], pool->swork.merkle_bin[i], 32);
		}
	}

	pool_stratum->sdiff = pool->sdiff;
	pool_stratum->coinbase_len = pool->coinbase_len;
	pool_stratum->nonce2_offset = pool->nonce2_offset;
	pool_stratum->n2size = pool->n2size;
	pool_stratum->merkles = pool->merkles;

	pool_stratum->swork.job_id = strdup(pool->swork.job_id);
	pool_stratum->nonce1 = strdup(pool->nonce1);

	memcpy(pool_stratum->ntime, pool->ntime, sizeof(pool_stratum->ntime));
	memcpy(pool_stratum->header_bin, pool->header_bin, sizeof(pool_stratum->header_bin));
	cg_wunlock(&pool_stratum->data_lock);
}

static void avalon2_update(struct cgpu_info *avalon2)
{
	struct avalon2_info *info = avalon2->device_data;
	struct thr_info *thr = avalon2->thr[0];
	struct avalon2_pkg send_pkg;
	uint32_t tmp, range, start;
	struct work *work;
	struct pool *pool;

	applog(LOG_DEBUG, "Avalon2: New stratum: restart: %d, update: %d",
	       thr->work_restart, thr->work_update);
	thr->work_update = false;
	thr->work_restart = false;

	work = get_work(thr, thr->id); /* Make sure pool is ready */
	discard_work(work); /* Don't leak memory */

	pool = current_pool();
	if (!pool->has_stratum)
		quit(1, "Avalon2: MM have to use stratum pool");

	if (pool->coinbase_len > AVA2_P_COINBASE_SIZE) {
		applog(LOG_INFO, "Avalon2: MM pool coinbase length(%d) is more than %d",
		       pool->coinbase_len, AVA2_P_COINBASE_SIZE);
		if (mm_cmp_1406(info)) {
			applog(LOG_ERR, "Avalon2: MM version less then 1406");
			return;
		}
		if ((pool->coinbase_len - pool->nonce2_offset + 64) > AVA2_P_COINBASE_SIZE) {
			applog(LOG_ERR, "Avalon2: MM pool modified coinbase length(%d) is more than %d",
			       pool->coinbase_len - pool->nonce2_offset + 64, AVA2_P_COINBASE_SIZE);
			return;
		}
	}
	if (pool->merkles > AVA2_P_MERKLES_COUNT) {
		applog(LOG_ERR, "Avalon2: MM merkles have to less then %d", AVA2_P_MERKLES_COUNT);
		return;
	}
	if (pool->n2size < 3) {
		applog(LOG_ERR, "Avalon2: MM nonce2 size have to >= 3 (%d)", pool->n2size);
		return;
	}

	cgtime(&info->last_stratum);
	cg_rlock(&pool->data_lock);
	info->pool_no = pool->pool_no;
	copy_pool_stratum(info, pool);
	avalon2_stratum_pkgs(avalon2, pool);
	cg_runlock(&pool->data_lock);

	/* Configuer the parameter from outside */
	adjust_fan(info);
	info->set_voltage = opt_avalon2_voltage_min;
	info->set_frequency = opt_avalon2_freq_min;

	/* Set the Fan, Voltage and Frequency */
	memset(send_pkg.data, 0, AVA2_P_DATA_LEN);

	tmp = be32toh(info->fan_pwm);
	memcpy(send_pkg.data, &tmp, 4);

	applog(LOG_INFO, "Avalon2: Temp max: %d, Cut off temp: %d",
	       get_current_temp_max(info), opt_avalon2_overheat);
	if (get_current_temp_max(info) >= opt_avalon2_overheat)
		tmp = encode_voltage(0);
	else
		tmp = encode_voltage(info->set_voltage);
	tmp = be32toh(tmp);
	memcpy(send_pkg.data + 4, &tmp, 4);

	tmp = be32toh(info->set_frequency);
	memcpy(send_pkg.data + 8, &tmp, 4);

	/* Configure the nonce2 offset and range */
	if (pool->n2size == 3)
		range = 0xffffff / (total_devices + 1);
	else
		range = 0xffffffff / (total_devices + 1);
	start = range * (avalon2->device_id + 1);

	tmp = be32toh(start);
	memcpy(send_pkg.data + 12, &tmp, 4);

	tmp = be32toh(range);
	memcpy(send_pkg.data + 16, &tmp, 4);

	/* Package the data */
	avalon2_init_pkg(&send_pkg, AVA2_P_SET, 1, 1);
	avalon2_send_pkg(avalon2, &send_pkg);
}

static int64_t avalon2_scanhash(struct thr_info *thr)
{
	struct timeval current_stratum;
	struct cgpu_info *avalon2 = thr->cgpu;
	struct avalon2_info *info = avalon2->device_data;
	int stdiff;
	int64_t h;
	int i;

	if (unlikely(avalon2->usbinfo.nodev)) {
		applog(LOG_ERR, "%s %d: Device disappeared, shutting down thread",
		       avalon2->drv->name, avalon2->device_id);
		return -1;
	}

	/* Stop polling the device if there is no stratum in 3 minutes, network is down */
	cgtime(&current_stratum);
	if (tdiff(&current_stratum, &(info->last_stratum)) > (double)(3.0 * 60.0))
		return 0;

	polling(thr, avalon2, info);

	stdiff = share_work_tdiff(avalon2);
	if (unlikely(info->failing)) {
		if (stdiff > 120) {
			applog(LOG_ERR, "%s %d: No valid shares for over 2 minutes, shutting down thread",
			       avalon2->drv->name, avalon2->device_id);
			return -1;
		}
	} else if (stdiff > 60) {
		applog(LOG_ERR, "%s %d: No valid shares for over 1 minute, issuing a USB reset",
		       avalon2->drv->name, avalon2->device_id);
		usb_reset(avalon2);
		info->failing = true;

	}

	h = 0;
	for (i = 0; i < AVA2_DEFAULT_MODULARS; i++) {
		h += info->enable[i] ? (info->local_work[i] - info->hw_work[i]) : 0;
	}
	return h * 0xffffffff;
}

static struct api_data *avalon2_api_stats(struct cgpu_info *cgpu)
{
	struct api_data *root = NULL;
	struct avalon2_info *info = cgpu->device_data;
	int i, j, a, b;
	char buf[24];
	double hwp;
	int minerindex, minercount;

	for (i = 0; i < AVA2_DEFAULT_MODULARS; i++) {
		if(info->dev_type[i] == AVA2_ID_AVAX)
			continue;
		sprintf(buf, "ID%d MM Version", i + 1);
		root = api_add_string(root, buf, (char *)&(info->mm_version[i]), false);
	}

	minerindex = 0;
	minercount = 0;
	for (i = 0; i < AVA2_DEFAULT_MODULARS; i++) {
		if (info->dev_type[i] == AVA2_ID_AVAX) {
			minerindex += AVA2_DEFAULT_MINERS;
			continue;
		}

		if (info->dev_type[i] == AVA2_ID_AVA2)
			minercount = AVA2_DEFAULT_MINERS;

		if (info->dev_type[i] == AVA2_ID_AVA3)
			minercount = AVA2_AVA3_MINERS;

		for (j = minerindex; j < (minerindex + minercount); j++) {
			sprintf(buf, "Match work count%02d", j+1);
			root = api_add_int(root, buf, &(info->matching_work[j]), false);
		}
		minerindex += AVA2_DEFAULT_MINERS;
	}

	for (i = 0; i < AVA2_DEFAULT_MODULARS; i++) {
		if(info->dev_type[i] == AVA2_ID_AVAX)
			continue;
		sprintf(buf, "Local works%d", i + 1);
		root = api_add_int(root, buf, &(info->local_works[i]), false);
	}
	for (i = 0; i < AVA2_DEFAULT_MODULARS; i++) {
		if(info->dev_type[i] == AVA2_ID_AVAX)
			continue;
		sprintf(buf, "Hardware error works%d", i + 1);
		root = api_add_int(root, buf, &(info->hw_works[i]), false);
	}
	for (i = 0; i < AVA2_DEFAULT_MODULARS; i++) {
		if(info->dev_type[i] == AVA2_ID_AVAX)
			continue;
		a = info->hw_works[i];
		b = info->local_works[i];
		hwp = b ? ((double)a / (double)b) : 0;

		sprintf(buf, "Device hardware error%d%%", i + 1);
		root = api_add_percent(root, buf, &hwp, true);
	}
	for (i = 0; i < 2 * AVA2_DEFAULT_MODULARS; i++) {
		if(info->dev_type[i/2] == AVA2_ID_AVAX)
			continue;
		sprintf(buf, "Temperature%d", i + 1);
		root = api_add_int(root, buf, &(info->temp[i]), false);
	}
	for (i = 0; i < 2 * AVA2_DEFAULT_MODULARS; i++) {
		if(info->dev_type[i/2] == AVA2_ID_AVAX)
			continue;
		sprintf(buf, "Fan%d", i + 1);
		root = api_add_int(root, buf, &(info->fan[i]), false);
	}
	for (i = 0; i < AVA2_DEFAULT_MODULARS; i++) {
		if(info->dev_type[i] == AVA2_ID_AVAX)
			continue;
		sprintf(buf, "Voltage%d", i + 1);
		root = api_add_int(root, buf, &(info->get_voltage[i]), false);
	}
	for (i = 0; i < AVA2_DEFAULT_MODULARS; i++) {
		if(info->dev_type[i] == AVA2_ID_AVAX)
			continue;
		sprintf(buf, "Frequency%d", i + 1);
		root = api_add_int(root, buf, &(info->get_frequency[i]), false);
	}
	for (i = 0; i < AVA2_DEFAULT_MODULARS; i++) {
		if(info->dev_type[i] == AVA2_ID_AVAX)
			continue;
		sprintf(buf, "Power good %02x", i + 1);
		root = api_add_int(root, buf, &(info->power_good[i]), false);
	}
	for (i = 0; i < AVA2_DEFAULT_MODULARS; i++) {
		if(info->dev_type[i] == AVA2_ID_AVAX)
			continue;
		sprintf(buf, "Led %02x", i + 1);
		root = api_add_int(root, buf, &(info->led_red[i]), false);
	}

	return root;
}

static void avalon2_statline_before(char *buf, size_t bufsiz, struct cgpu_info *avalon2)
{
	struct avalon2_info *info = avalon2->device_data;
	int temp = get_current_temp_max(info);
	float volts = (float)info->set_voltage / 10000;

	tailsprintf(buf, bufsiz, "%4dMhz %2dC %3d%% %.3fV", info->set_frequency,
		    temp, info->fan_pct, volts);
}

static void avalon2_shutdown(struct thr_info *thr)
{
	struct cgpu_info *avalon2 = thr->cgpu;
	int interface = usb_interface(avalon2);

	usb_transfer(avalon2, PL2303_CTRL_OUT, PL2303_REQUEST_CTRL, 0, interface, C_SETLINE);
}

struct device_drv avalon2_drv = {
	.drv_id = DRIVER_avalon2,
	.dname = "avalon2",
	.name = "AV2",
	.get_api_stats = avalon2_api_stats,
	.get_statline_before = avalon2_statline_before,
	.drv_detect = avalon2_detect,
	.thread_prepare = avalon2_prepare,
	.hash_work = hash_driver_work,
	.flush_work = avalon2_update,
	.update_work = avalon2_update,
	.scanwork = avalon2_scanhash,
	.thread_shutdown = avalon2_shutdown,
};
