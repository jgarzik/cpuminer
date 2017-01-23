/*
 * Copyright 2013-2015 Con Kolivas <kernel@kolivas.org>
 * Copyright 2012-2014 Xiangfu <xiangfu@openmobilefree.com>
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
  #include <io.h>
#endif

#include "elist.h"
#include "miner.h"
#include "driver-hashratio.h"
#include "crc.h"
#include "usbutils.h"

static int opt_hashratio_fan_min = HRTO_DEFAULT_FAN_MIN;
static int opt_hashratio_fan_max = HRTO_DEFAULT_FAN_MAX;

static int hashratio_freq = HRTO_DEFAULT_FREQUENCY;

//static int get_fan_pwm(int temp) {
//	int pwm;
//	uint8_t fan_pwm_arr[] = {30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30,
//		30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30,
//		30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30,
//		30, 37, 49, 61, 73, 85, 88, 91, 94, 97, 100, 100, 100, 100, 100, 100,
//		100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100,
//		100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100,
//		100, 100, 100, 100, 100, 100, 100};
//	if (temp < 0 || temp >= sizeof(fan_pwm_arr)/sizeof(fan_pwm_arr[0]) ||
//		fan_pwm_arr[temp] > opt_hashratio_fan_max) {
//		return opt_hashratio_fan_max;
//	}
//	pwm = HRTO_PWM_MAX - fan_pwm_arr[temp] * HRTO_PWM_MAX / 100;
//
//	if (pwm < opt_hashratio_fan_min) {
//		return opt_hashratio_fan_min;
//	}
//	if (pwm > opt_hashratio_fan_max) {
//		return opt_hashratio_fan_max;
//	}
//	return pwm;
//}

char *set_hashratio_freq(char *arg)
{
	int val, ret;
	
	ret = sscanf(arg, "%d", &val);
	if (ret != 1)
		return "No values passed to hashratio-freq";
	
	if (val < HRTO_DEFAULT_FREQUENCY_MIN || val > HRTO_DEFAULT_FREQUENCY_MAX)
		return "Invalid value passed to hashratio-freq";

	hashratio_freq = val;
	
	return NULL;
}

char *set_hashratio_fan(char *arg)
{
	int val1, val2, ret;

	ret = sscanf(arg, "%d-%d", &val1, &val2);
	if (ret < 1)
		return "No values passed to hashratio-fan";
	if (ret == 1)
		val2 = val1;

	if (val1 < 0 || val1 > 100 || val2 < 0 || val2 > 100 || val2 < val1)
		return "Invalid value passed to hashratio-fan";

	opt_hashratio_fan_min = val1 * HRTO_PWM_MAX / 100;
	opt_hashratio_fan_max = val2 * HRTO_PWM_MAX / 100;

	return NULL;
}

static int hashratio_init_pkg(struct hashratio_pkg *pkg, uint8_t type,
							  uint8_t idx, uint8_t cnt)
{
	unsigned short crc;

	pkg->head[0] = HRTO_H1;
	pkg->head[1] = HRTO_H2;

	pkg->type = type;
	pkg->idx = idx;
	pkg->cnt = cnt;

	crc = crc16(pkg->data, HRTO_P_DATA_LEN);

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
	crc_expect = crc16((const unsigned char *)pool_job_id, job_id_len);

	crc = job_id[0] << 8 | job_id[1];

	if (crc_expect == crc)
		return 0;

	applog(LOG_DEBUG, "Hashratio: job_id not match! [%04x:%04x (%s)]",
	       crc, crc_expect, pool_job_id);

	return 1;
}

static int decode_pkg(struct thr_info *thr, struct hashratio_ret *ar, uint8_t *pkg)
{
	struct cgpu_info *hashratio = thr->cgpu;
	struct hashratio_info *info = hashratio->device_data;
	struct pool *pool, *real_pool, *pool_stratum = &info->pool;

	unsigned int expected_crc;
	unsigned int actual_crc;
	uint32_t nonce, nonce2, miner;
	int pool_no;
	uint8_t job_id[4];
	int tmp;

	int type = HRTO_GETS_ERROR;

	memcpy((uint8_t *)ar, pkg, HRTO_READ_SIZE);

//	applog(LOG_DEBUG, "pkg.type, hex: %02x, dec: %d", ar->type, ar->type);
	
	if (ar->head[0] == HRTO_H1 && ar->head[1] == HRTO_H2) {
		expected_crc = crc16(ar->data, HRTO_P_DATA_LEN);
		actual_crc = (ar->crc[0] & 0xff) |
			((ar->crc[1] & 0xff) << 8);

		type = ar->type;
		applog(LOG_DEBUG, "hashratio: %d: expected crc(%04x), actual_crc(%04x)", type, expected_crc, actual_crc);
		if (expected_crc != actual_crc)
			goto out;

		switch(type) {
		case HRTO_P_NONCE:
			applog(LOG_DEBUG, "Hashratio: HRTO_P_NONCE");
			memcpy(&miner,   ar->data + 0, 4);
			memcpy(&pool_no, ar->data + 4, 4);
			memcpy(&nonce2,  ar->data + 8, 4);
			/* Calc time    ar->data + 12 */
			memcpy(&nonce, ar->data + 12, 4);
			memcpy(job_id, ar->data + 16, 4);

			miner = be32toh(miner);
			pool_no = be32toh(pool_no);
			if (miner >= HRTO_DEFAULT_MINERS || pool_no >= total_pools || pool_no < 0) {
				applog(LOG_DEBUG, "hashratio: Wrong miner/pool/id no %d,%d", miner, pool_no);
				break;
			} else
				info->matching_work[miner]++;
			nonce2 = be32toh(nonce2);
			nonce = be32toh(nonce);

			applog(LOG_DEBUG, "hashratio: Found! [%s] %d:(%08x) (%08x)",
			       job_id, pool_no, nonce2, nonce);

			real_pool = pool = pools[pool_no];
			if (job_idcmp(job_id, pool->swork.job_id)) {
				if (!job_idcmp(job_id, pool_stratum->swork.job_id)) {
					applog(LOG_DEBUG, "Hashratio: Match to previous stratum! (%s)", pool_stratum->swork.job_id);
					pool = pool_stratum;
				} else {
					applog(LOG_DEBUG, "Hashratio Cannot match to any stratum! (%s)", pool->swork.job_id);
					break;
				}
			}
			submit_nonce2_nonce(thr, pool, real_pool, nonce2, nonce, 0);
			break;
		case HRTO_P_STATUS:
			applog(LOG_DEBUG, "Hashratio: HRTO_P_STATUS");
			memcpy(&tmp, ar->data, 4);
			tmp = be32toh(tmp);
			info->temp = (tmp & 0x00f0) >> 8;
			if (info->temp_max < info->temp) {
				info->temp_max = info->temp;
			}
//			info->temp[1] = tmp & 0xffff;

			memcpy(&tmp, ar->data + 4, 4);
			tmp = be32toh(tmp);
			info->fan[0] = tmp >> 16;
			info->fan[1] = tmp & 0xffff;

			// local_work
			memcpy(&tmp, ar->data + 8, 4);
			tmp = be32toh(tmp);
			info->local_work = tmp;
			info->local_works += tmp;
			
			// hw_work
			memcpy(&tmp, ar->data + 12, 4);
			tmp = be32toh(tmp);
			info->hw_works += tmp;
			
			hashratio->temp = info->temp;
			break;
		case HRTO_P_ACKDETECT:
			applog(LOG_DEBUG, "Hashratio: HRTO_P_ACKDETECT");
			break;
		case HRTO_P_ACK:
			applog(LOG_DEBUG, "Hashratio: HRTO_P_ACK");
			break;
		case HRTO_P_NAK:
			applog(LOG_DEBUG, "Hashratio: HRTO_P_NAK");
			break;
		default:
			applog(LOG_DEBUG, "Hashratio: HRTO_GETS_ERROR");
			type = HRTO_GETS_ERROR;
			break;
		}
	}

out:
	return type;
}

static inline int hashratio_gets(struct cgpu_info *hashratio, uint8_t *buf)
{
	int i;
	int read_amount = HRTO_READ_SIZE;
	uint8_t buf_tmp[HRTO_READ_SIZE];
	uint8_t buf_copy[2 * HRTO_READ_SIZE];
	uint8_t *buf_back = buf;
	int ret = 0;

	while (true) {
		int err;

		do {
			memset(buf, 0, read_amount);
			err = usb_read(hashratio, (char *)buf, read_amount, &ret, C_HRO_READ);
			if (unlikely(err < 0 || ret != read_amount)) {
				applog(LOG_ERR, "hashratio: Error on read in hashratio_gets got %d", ret);
				return HRTO_GETS_ERROR;
			}
			if (likely(ret >= read_amount)) {
				for (i = 1; i < read_amount; i++) {
					if (buf_back[i - 1] == HRTO_H1 && buf_back[i] == HRTO_H2)
						break;
				}
				i -= 1;
				if (i) {
					err = usb_read(hashratio, (char *)buf, read_amount, &ret, C_HRO_READ);
					if (unlikely(err < 0 || ret != read_amount)) {
						applog(LOG_ERR, "hashratio: Error on 2nd read in hashratio_gets got %d", ret);
						return HRTO_GETS_ERROR;
					}
					memcpy(buf_copy, buf_back + i, HRTO_READ_SIZE - i);
					memcpy(buf_copy + HRTO_READ_SIZE - i, buf_tmp, i);
					memcpy(buf_back, buf_copy, HRTO_READ_SIZE);
				}
				return HRTO_GETS_OK;
			}
			buf += ret;
			read_amount -= ret;
			continue;
		} while (ret > 0);

		return HRTO_GETS_TIMEOUT;
	}
}

static int hashratio_send_pkg(struct cgpu_info *hashratio, const struct hashratio_pkg *pkg)
{
	int err, amount;
	uint8_t buf[HRTO_WRITE_SIZE];
	int nr_len = HRTO_WRITE_SIZE;

	memcpy(buf, pkg, HRTO_WRITE_SIZE);
//	if (opt_debug) {
//		applog(LOG_DEBUG, "hashratio: Sent(%d):", nr_len);
//		hexdump((uint8_t *)buf, nr_len);
//	}

	if (unlikely(hashratio->usbinfo.nodev))
		return HRTO_SEND_ERROR;

	err = usb_write(hashratio, (char *)buf, nr_len, &amount, C_HRO_WRITE);
	if (err || amount != nr_len) {
		applog(LOG_DEBUG, "hashratio: Send(%d)!", amount);
		return HRTO_SEND_ERROR;
	}

	return HRTO_SEND_OK;
}

static int hashratio_send_pkgs(struct cgpu_info *hashratio, const struct hashratio_pkg *pkg)
{
	int ret;

	do {
		if (unlikely(hashratio->usbinfo.nodev))
			return -1;
		ret = hashratio_send_pkg(hashratio, pkg);
	} while (ret != HRTO_SEND_OK);
	return 0;
}

static void hashratio_stratum_pkgs(struct cgpu_info *hashratio, struct pool *pool)
{
	const int merkle_offset = 36;
	struct hashratio_pkg pkg;
	int i, a, b, tmp;
	unsigned char target[32];
	int job_id_len;
	unsigned short crc;

	/* Send out the first stratum message STATIC */
	applog(LOG_DEBUG, "hashratio: Pool stratum message STATIC: %d, %d, %d, %d, %d, %d",
	       pool->coinbase_len,
	       pool->nonce2_offset,
	       pool->n2size,
	       merkle_offset,
	       pool->merkles,
		   pool->pool_no);
	memset(pkg.data, 0, HRTO_P_DATA_LEN);
	tmp = be32toh(pool->coinbase_len);
	memcpy(pkg.data, &tmp, 4);

	tmp = be32toh(pool->nonce2_offset);
	memcpy(pkg.data + 4, &tmp, 4);

	tmp = be32toh(pool->n2size);
	memcpy(pkg.data + 8, &tmp, 4);

	tmp = be32toh(merkle_offset);
	memcpy(pkg.data + 12, &tmp, 4);

	tmp = be32toh(pool->merkles);
	memcpy(pkg.data + 16, &tmp, 4);

	tmp = be32toh((int)pool->sdiff);
	memcpy(pkg.data + 20, &tmp, 4);

	tmp = be32toh((int)pool->pool_no);
	memcpy(pkg.data + 24, &tmp, 4);

	hashratio_init_pkg(&pkg, HRTO_P_STATIC, 1, 1);
	if (hashratio_send_pkgs(hashratio, &pkg))
		return;

	set_target(target, pool->sdiff);
	memcpy(pkg.data, target, 32);
	if (opt_debug) {
		char *target_str;
		target_str = bin2hex(target, 32);
		applog(LOG_DEBUG, "hashratio: Pool stratum target: %s", target_str);
		free(target_str);
	}
	hashratio_init_pkg(&pkg, HRTO_P_TARGET, 1, 1);
	if (hashratio_send_pkgs(hashratio, &pkg))
		return;

	applog(LOG_DEBUG, "hashratio: Pool stratum message JOBS_ID: %s",
	       pool->swork.job_id);
	memset(pkg.data, 0, HRTO_P_DATA_LEN);

	job_id_len = strlen(pool->swork.job_id);
	crc = crc16((const unsigned char *)pool->swork.job_id, job_id_len);
	pkg.data[0] = (crc & 0xff00) >> 8;
	pkg.data[1] = crc & 0x00ff;
	hashratio_init_pkg(&pkg, HRTO_P_JOB_ID, 1, 1);
	if (hashratio_send_pkgs(hashratio, &pkg))
		return;

	a = pool->coinbase_len / HRTO_P_DATA_LEN;
	b = pool->coinbase_len % HRTO_P_DATA_LEN;
	applog(LOG_DEBUG, "pool->coinbase_len: %d", pool->coinbase_len);
	applog(LOG_DEBUG, "hashratio: Pool stratum message COINBASE: %d %d", a, b);
	for (i = 0; i < a; i++) {
		memcpy(pkg.data, pool->coinbase + i * 32, 32);
		hashratio_init_pkg(&pkg, HRTO_P_COINBASE, i + 1, a + (b ? 1 : 0));
		if (hashratio_send_pkgs(hashratio, &pkg))
			return;
		if (i % 25 == 0) {
			cgsleep_ms(2);
		}
	}
	if (b) {
		memset(pkg.data, 0, HRTO_P_DATA_LEN);
		memcpy(pkg.data, pool->coinbase + i * 32, b);
		hashratio_init_pkg(&pkg, HRTO_P_COINBASE, i + 1, i + 1);
		if (hashratio_send_pkgs(hashratio, &pkg))
			return;
	}

	b = pool->merkles;
	applog(LOG_DEBUG, "hashratio: Pool stratum message MERKLES: %d", b);
	for (i = 0; i < b; i++) {
		memset(pkg.data, 0, HRTO_P_DATA_LEN);
		memcpy(pkg.data, pool->swork.merkle_bin[i], 32);
		hashratio_init_pkg(&pkg, HRTO_P_MERKLES, i + 1, b);
		if (hashratio_send_pkgs(hashratio, &pkg))
			return;
	}

	applog(LOG_DEBUG, "hashratio: Pool stratum message HEADER: 4");
	for (i = 0; i < 4; i++) {
		memset(pkg.data, 0, HRTO_P_HEADER);
		memcpy(pkg.data, pool->header_bin + i * 32, 32);
		hashratio_init_pkg(&pkg, HRTO_P_HEADER, i + 1, 4);
		if (hashratio_send_pkgs(hashratio, &pkg))
			return;

	}
}

static int hashratio_get_result(struct thr_info *thr, struct hashratio_ret *ar)
{
	struct cgpu_info *hashratio = thr->cgpu;
	uint8_t result[HRTO_READ_SIZE];
	int ret;

	memset(result, 0, HRTO_READ_SIZE);

	ret = hashratio_gets(hashratio, result);
	if (ret != HRTO_GETS_OK)
		return ret;

//	if (opt_debug) {
//		applog(LOG_DEBUG, "hashratio: Get(ret = %d):", ret);
//		hexdump((uint8_t *)result, HRTO_READ_SIZE);
//	}

	return decode_pkg(thr, ar, result);
}

#define HASHRATIO_LATENCY 5

static void hashratio_initialise(struct cgpu_info *hashratio)
{
	int err, interface;

	if (hashratio->usbinfo.nodev)
		return;

	interface = usb_interface(hashratio);
	// Reset
	err = usb_transfer(hashratio, FTDI_TYPE_OUT, FTDI_REQUEST_RESET,
				FTDI_VALUE_RESET, interface, C_RESET);

	applog(LOG_DEBUG, "%s%i: reset got err %d",
		hashratio->drv->name, hashratio->device_id, err);

	if (hashratio->usbinfo.nodev)
		return;

	// Set latency
	err = usb_transfer(hashratio, FTDI_TYPE_OUT, FTDI_REQUEST_LATENCY,
			   HASHRATIO_LATENCY, interface, C_LATENCY);

	applog(LOG_DEBUG, "%s%i: latency got err %d",
		hashratio->drv->name, hashratio->device_id, err);

	if (hashratio->usbinfo.nodev)
		return;

	// Set data
	err = usb_transfer(hashratio, FTDI_TYPE_OUT, FTDI_REQUEST_DATA,
				FTDI_VALUE_DATA_AVA, interface, C_SETDATA);

	applog(LOG_DEBUG, "%s%i: data got err %d",
		hashratio->drv->name, hashratio->device_id, err);

	if (hashratio->usbinfo.nodev)
		return;

	// Set the baud
	err = usb_transfer(hashratio, FTDI_TYPE_OUT, FTDI_REQUEST_BAUD, FTDI_VALUE_BAUD_AVA,
				(FTDI_INDEX_BAUD_AVA & 0xff00) | interface,
				C_SETBAUD);

	applog(LOG_DEBUG, "%s%i: setbaud got err %d",
		hashratio->drv->name, hashratio->device_id, err);

	if (hashratio->usbinfo.nodev)
		return;

	// Set Modem Control
	err = usb_transfer(hashratio, FTDI_TYPE_OUT, FTDI_REQUEST_MODEM,
				FTDI_VALUE_MODEM, interface, C_SETMODEM);

	applog(LOG_DEBUG, "%s%i: setmodemctrl got err %d",
		hashratio->drv->name, hashratio->device_id, err);

	if (hashratio->usbinfo.nodev)
		return;

	// Set Flow Control
	err = usb_transfer(hashratio, FTDI_TYPE_OUT, FTDI_REQUEST_FLOW,
				FTDI_VALUE_FLOW, interface, C_SETFLOW);

	applog(LOG_DEBUG, "%s%i: setflowctrl got err %d",
		hashratio->drv->name, hashratio->device_id, err);

	if (hashratio->usbinfo.nodev)
		return;

	/* hashratio repeats the following */
	// Set Modem Control
	err = usb_transfer(hashratio, FTDI_TYPE_OUT, FTDI_REQUEST_MODEM,
				FTDI_VALUE_MODEM, interface, C_SETMODEM);

	applog(LOG_DEBUG, "%s%i: setmodemctrl 2 got err %d",
		hashratio->drv->name, hashratio->device_id, err);

	if (hashratio->usbinfo.nodev)
		return;

	// Set Flow Control
	err = usb_transfer(hashratio, FTDI_TYPE_OUT, FTDI_REQUEST_FLOW,
				FTDI_VALUE_FLOW, interface, C_SETFLOW);

	applog(LOG_DEBUG, "%s%i: setflowctrl 2 got err %d",
		hashratio->drv->name, hashratio->device_id, err);
}

static struct cgpu_info *hashratio_detect_one(struct libusb_device *dev, struct usb_find_devices *found)
{
	struct hashratio_info *info;
	int err, amount;
	int ackdetect;
	char mm_version[16];

	struct cgpu_info *hashratio = usb_alloc_cgpu(&hashratio_drv, 1);
	struct hashratio_pkg detect_pkg;
	struct hashratio_ret ret_pkg;

	if (!usb_init(hashratio, dev, found)) {
		applog(LOG_ERR, "Hashratio failed usb_init");
		hashratio = usb_free_cgpu(hashratio);
		return NULL;
	}

	hashratio_initialise(hashratio);

	strcpy(mm_version, "NONE");
	/* Send out detect pkg */
	memset(detect_pkg.data, 0, HRTO_P_DATA_LEN);

	hashratio_init_pkg(&detect_pkg, HRTO_P_DETECT, 1, 1);
	hashratio_send_pkg(hashratio, &detect_pkg);
	err = usb_read(hashratio, (char *)&ret_pkg, HRTO_READ_SIZE, &amount, C_HRO_READ);
	if (err || amount != HRTO_READ_SIZE) {
		applog(LOG_ERR, "%s %d: Hashratio failed usb_read with err %d amount %d",
		       hashratio->drv->name, hashratio->device_id, err, amount);
		usb_uninit(hashratio);
		usb_free_cgpu(hashratio);
		return NULL;
	}

	ackdetect = ret_pkg.type;
	applog(LOG_DEBUG, "hashratio Detect ID: %d", ackdetect);
	
	if (ackdetect != HRTO_P_ACKDETECT) {
		applog(LOG_DEBUG, "Not a hashratio device");
		usb_uninit(hashratio);
		usb_free_cgpu(hashratio);
		return NULL;
	}

	memcpy(mm_version, ret_pkg.data, 15);
	mm_version[15] = '\0';

	/* We have a real Hashratio! */
	hashratio->threads = HRTO_MINER_THREADS;
	add_cgpu(hashratio);

	update_usb_stats(hashratio);

	applog(LOG_INFO, "%s%d: Found at %s", hashratio->drv->name, hashratio->device_id,
	       hashratio->device_path);

	hashratio->device_data = cgcalloc(sizeof(struct hashratio_info), 1);

	info = hashratio->device_data;

	strcpy(info->mm_version, mm_version);

	info->fan_pwm  = HRTO_DEFAULT_FAN / 100 * HRTO_PWM_MAX;
	info->temp_max = 0;
	info->temp_history_index = 0;
	info->temp_sum = 0;
	info->temp_old = 0;
	info->default_freq = hashratio_freq;

	return hashratio;
}

static inline void hashratio_detect(bool __maybe_unused hotplug)
{
	usb_detect(&hashratio_drv, hashratio_detect_one);
}

static bool hashratio_prepare(struct thr_info *thr)
{
	struct cgpu_info *hashratio = thr->cgpu;
	struct hashratio_info *info = hashratio->device_data;

	cglock_init(&info->pool.data_lock);

	return true;
}

static void copy_pool_stratum(struct hashratio_info *info, struct pool *pool)
{
	int i;
	int merkles = pool->merkles;
	size_t coinbase_len = pool->coinbase_len;
	struct pool *pool_stratum = &info->pool;

	if (!job_idcmp((uint8_t *)pool->swork.job_id, pool_stratum->swork.job_id))
		return;

	cg_wlock(&(pool_stratum->data_lock));
	free(pool_stratum->swork.job_id);
	free(pool_stratum->nonce1);
	free(pool_stratum->coinbase);

	pool_stratum->coinbase = cgcalloc(coinbase_len, 1);
	memcpy(pool_stratum->coinbase, pool->coinbase, coinbase_len);

	for (i = 0; i < pool_stratum->merkles; i++)
		free(pool_stratum->swork.merkle_bin[i]);
	if (merkles) {
		pool_stratum->swork.merkle_bin = cgrealloc(pool_stratum->swork.merkle_bin,
							   sizeof(char *) * merkles + 1);
		for (i = 0; i < merkles; i++) {
			pool_stratum->swork.merkle_bin[i] = cgmalloc(32);
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
	cg_wunlock(&(pool_stratum->data_lock));
}

static void hashratio_update_work(struct cgpu_info *hashratio)
{
	struct hashratio_info *info = hashratio->device_data;
	struct thr_info *thr = hashratio->thr[0];
	struct hashratio_pkg send_pkg;
	uint32_t tmp, range, start;
	struct work *work;
	struct pool *pool;

	applog(LOG_DEBUG, "hashratio: New stratum: restart: %d, update: %d",
		thr->work_restart, thr->work_update);
	thr->work_update = false;
	thr->work_restart = false;

	work = get_work(thr, thr->id); /* Make sure pool is ready */
	discard_work(work); /* Don't leak memory */

	pool = current_pool();
	if (!pool->has_stratum)
		quit(1, "hashratio: Miner Manager have to use stratum pool");
	if (pool->coinbase_len > HRTO_P_COINBASE_SIZE)
		quit(1, "hashratio: Miner Manager pool coinbase length have to less then %d", HRTO_P_COINBASE_SIZE);
	if (pool->merkles > HRTO_P_MERKLES_COUNT)
		quit(1, "hashratio: Miner Manager merkles have to less then %d", HRTO_P_MERKLES_COUNT);

	info->pool_no = pool->pool_no;

	cgtime(&info->last_stratum);
	cg_rlock(&pool->data_lock);
	info->pool_no = pool->pool_no;
	copy_pool_stratum(info, pool);
	hashratio_stratum_pkgs(hashratio, pool);
	cg_runlock(&pool->data_lock);

	/* Configure the parameter from outside */
	memset(send_pkg.data, 0, HRTO_P_DATA_LEN);

	// fan. We're not measuring temperature so set a safe but not max value
	info->fan_pwm = HRTO_PWM_MAX * 2 / 3;
	tmp = be32toh(info->fan_pwm);
	memcpy(send_pkg.data, &tmp, 4);

	// freq
	tmp = be32toh(info->default_freq);
	memcpy(send_pkg.data + 4, &tmp, 4);
	applog(LOG_DEBUG, "set freq: %d", info->default_freq);

	/* Configure the nonce2 offset and range */
	range = 0xffffffff / (total_devices + 1);
	start = range * (hashratio->device_id + 1);

	tmp = be32toh(start);
	memcpy(send_pkg.data + 8, &tmp, 4);

	tmp = be32toh(range);
	memcpy(send_pkg.data + 12, &tmp, 4);

	/* Package the data */
	hashratio_init_pkg(&send_pkg, HRTO_P_SET, 1, 1);
	hashratio_send_pkgs(hashratio, &send_pkg);
}

static int64_t hashratio_scanhash(struct thr_info *thr)
{
	struct cgpu_info *hashratio = thr->cgpu;
	struct hashratio_info *info = hashratio->device_data;
	struct hashratio_pkg send_pkg;
	struct hashratio_ret ar;

	memset(send_pkg.data, 0, HRTO_P_DATA_LEN);
	hashratio_init_pkg(&send_pkg, HRTO_P_POLLING, 1, 1);

	if (unlikely(hashratio->usbinfo.nodev || hashratio_send_pkgs(hashratio, &send_pkg))) {
		applog(LOG_ERR, "%s%d: Device disappeared, shutting down thread",
		       hashratio->drv->name, hashratio->device_id);
		return -1;
	}
	hashratio_get_result(thr, &ar);

	return (int64_t)info->local_work * 64 * 0xffffffff;
}

static struct api_data *hashratio_api_stats(struct cgpu_info *cgpu)
{
	struct api_data *root = NULL;
	struct hashratio_info *info = cgpu->device_data;
	char buf[24];
	char buf2[256];
	double hwp;
	int i;

	// mm version
	sprintf(buf, "MM Version");
	root = api_add_string(root, buf, info->mm_version, false);
	
	// asic freq
	sprintf(buf, "Asic Freq (MHz)");
	root = api_add_int(root, buf, &(info->default_freq), false);
	
	// match work count
	for (i = 0; i < HRTO_DEFAULT_MODULARS; i++) {
		sprintf(buf, "Match work Modular %02d", i + 1);
		memset(buf2, 0, sizeof(buf2));
		snprintf(buf2, sizeof(buf2),
				 "%02d:%08d %02d:%08d %02d:%08d %02d:%08d "
				 "%02d:%08d %02d:%08d %02d:%08d %02d:%08d "
				 "%02d:%08d %02d:%08d %02d:%08d %02d:%08d "
				 "%02d:%08d %02d:%08d %02d:%08d %02d:%08d",
				i*16 + 1, info->matching_work[i*16 + 0],
				i*16 + 2, info->matching_work[i*16 + 1],
				i*16 + 3, info->matching_work[i*16 + 2],
				i*16 + 4, info->matching_work[i*16 + 3],
				i*16 + 5, info->matching_work[i*16 + 4],
				i*16 + 6, info->matching_work[i*16 + 5],
				i*16 + 7, info->matching_work[i*16 + 6],
				i*16 + 8, info->matching_work[i*16 + 7],
				i*16 + 9, info->matching_work[i*16 + 8],
				i*16 + 10, info->matching_work[i*16 + 9],
				i*16 + 11, info->matching_work[i*16 + 10],
				i*16 + 12, info->matching_work[i*16 + 11],
				i*16 + 13, info->matching_work[i*16 + 12],
				i*16 + 14, info->matching_work[i*16 + 13],
				i*16 + 15, info->matching_work[i*16 + 14],
				i*16 + 16, info->matching_work[i*16 + 15]);
		root = api_add_string(root, buf, buf2, true);
	}
	
	// local works
	sprintf(buf, "Local works");
	root = api_add_int(root, buf, &(info->local_works), false);
	
	// hardware error works
	sprintf(buf, "Hardware error works");
	root = api_add_int(root, buf, &(info->hw_works), false);
	
	// device hardware error %
	hwp = info->local_works ? ((double)info->hw_works / (double)info->local_works) : 0;
	sprintf(buf, "Device hardware error%%");
	root = api_add_percent(root, buf, &hwp, true);
	
	// Temperature
	sprintf(buf, "Temperature");
	root = api_add_int(root, buf, &(info->temp), false);

	// Fan
	for (i = 0; i < HRTO_FAN_COUNT; i++) {
		sprintf(buf, "Fan%d", i+1);
		root = api_add_int(root, buf, &(info->fan[i]), false);
	}

	return root;
}

static void hashratio_shutdown(struct thr_info __maybe_unused *thr)
{
}

struct device_drv hashratio_drv = {
	.drv_id = DRIVER_hashratio,
	.dname = "hashratio",
	.name = "HRO",
	.get_api_stats   = hashratio_api_stats,
	.drv_detect      = hashratio_detect,
	.thread_prepare  = hashratio_prepare,
	.hash_work       = hash_driver_work,
	.scanwork        = hashratio_scanhash,
	.flush_work      = hashratio_update_work,
	.update_work     = hashratio_update_work,
	.thread_shutdown = hashratio_shutdown,
};
