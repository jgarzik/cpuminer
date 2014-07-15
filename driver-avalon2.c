/*
 * Copyright 2013 Con Kolivas <kernel@kolivas.org>
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
#include <sys/select.h>
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
#include "hexdump.c"

#define ASSERT1(condition) __maybe_unused static char sizeof_uint32_t_must_be_4[(condition)?1:-1]
ASSERT1(sizeof(uint32_t) == 4);

#define get_fan_pwm(v)	(AVA2_PWM_MAX - (v) * AVA2_PWM_MAX / 100)

int opt_avalon2_freq_min = AVA2_DEFAULT_FREQUENCY;
int opt_avalon2_freq_max = AVA2_DEFAULT_FREQUENCY_MAX;

int opt_avalon2_fan_min = AVA2_DEFAULT_FAN_PWM;
int opt_avalon2_fan_max = AVA2_DEFAULT_FAN_MAX;

int opt_avalon2_voltage_min = AVA2_DEFAULT_VOLTAGE;
int opt_avalon2_voltage_max = AVA2_DEFAULT_VOLTAGE_MAX;

int opt_avalon2_overheat = AVALON2_TEMP_OVERHEAT;
enum avalon2_fan_fixed opt_avalon2_fan_fixed = FAN_AUTO;

static struct pool pool_stratum;

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

	opt_avalon2_fan_min = get_fan_pwm(val1);
	opt_avalon2_fan_max = get_fan_pwm(val2);

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
	crc_expect = crc16(pool_job_id, job_id_len);

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

	if (unlikely(info->first < 2) || opt_avalon2_fan_fixed == FAN_FIXED) {
		info->fan_pwm = opt_avalon2_fan_min;
		return;
	}

	t = get_current_temp_max(info);

	/* TODO: Add options for temperature range and fan adjust function */
	if (t < 50) {
		info->fan_pwm = get_fan_pwm(40);
		return;
	}

	if (t > 70) {
		info->fan_pwm = get_fan_pwm(100);
		return;
	}

	info->fan_pwm = get_fan_pwm(3 * t - 110);
}

static inline int mm_cmp_1404(struct avalon2_info *info, int modular)
{
	char *mm_1404 = "1404";
	return strncmp(info->mm_version[modular] + 2, mm_1404, 4) > 0 ? 0 : 1;
}

extern void submit_nonce2_nonce(struct thr_info *thr, struct pool *pool, struct pool *real_pool, uint32_t nonce2, uint32_t nonce);
static int decode_pkg(struct thr_info *thr, struct avalon2_ret *ar, uint8_t *pkg)
{
	struct cgpu_info *avalon2;
	struct avalon2_info *info;
	struct pool *pool, *real_pool;

	unsigned int expected_crc;
	unsigned int actual_crc;
	uint32_t nonce, nonce2, miner, modular_id;
	int pool_no;
	uint8_t job_id[4];
	int tmp;

	int type = AVA2_GETS_ERROR;

	if (thr) {
		avalon2 = thr->cgpu;
		info = avalon2->device_data;
	}

	memcpy((uint8_t *)ar, pkg, AVA2_READ_SIZE);

	if (ar->head[0] == AVA2_H1 && ar->head[1] == AVA2_H2) {
		expected_crc = crc16(ar->data, AVA2_P_DATA_LEN);
		actual_crc = (ar->crc[0] & 0xff) |
			((ar->crc[1] & 0xff) << 8);

		type = ar->type;
		applog(LOG_DEBUG, "Avalon2: %d: expected crc(%04x), actural_crc(%04x)", type, expected_crc, actual_crc);
		if (expected_crc != actual_crc)
			goto out;

		memcpy(&modular_id, ar->data + 28, 4);
		modular_id = be32toh(modular_id);
		if (modular_id > 3)
			modular_id = 0;

		switch(type) {
		case AVA2_P_NONCE:
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
				if (!job_idcmp(job_id, pool_stratum.swork.job_id)) {
					applog(LOG_ERR, "Avalon2: Match to previous stratum! (%s)", pool_stratum.swork.job_id);
					pool = &pool_stratum;
				} else {
					applog(LOG_ERR, "Avalon2: Cannot match to any stratum! (%s)", pool->swork.job_id);
					break;
				}
			}

			if (thr && info->first)
				submit_nonce2_nonce(thr, pool, real_pool, nonce2, nonce);
			break;
		case AVA2_P_STATUS:
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
			break;
		case AVA2_P_ACK:
			break;
		case AVA2_P_NAK:
			break;
		default:
			type = AVA2_GETS_ERROR;
			break;
		}
	}

out:
	return type;
}

static inline int avalon2_gets(int fd, uint8_t *buf)
{
	int i;
	int read_amount = AVA2_READ_SIZE;
	uint8_t buf_tmp[AVA2_READ_SIZE];
	uint8_t buf_copy[2 * AVA2_READ_SIZE];
	uint8_t *buf_back = buf;
	ssize_t ret = 0;

	while (true) {
		struct timeval timeout;
		fd_set rd;

		timeout.tv_sec = 0;
		timeout.tv_usec = 100000;

		FD_ZERO(&rd);
		FD_SET(fd, &rd);
		ret = select(fd + 1, &rd, NULL, NULL, &timeout);
		if (unlikely(ret < 0)) {
			applog(LOG_ERR, "Avalon2: Error %d on select in avalon_gets", errno);
			return AVA2_GETS_ERROR;
		}
		if (ret) {
			memset(buf, 0, read_amount);
			ret = read(fd, buf, read_amount);
			if (unlikely(ret < 0)) {
				applog(LOG_ERR, "Avalon2: Error %d on read in avalon_gets", errno);
				return AVA2_GETS_ERROR;
			}
			if (likely(ret >= read_amount)) {
				for (i = 1; i < read_amount; i++) {
					if (buf_back[i - 1] == AVA2_H1 && buf_back[i] == AVA2_H2)
						break;
				}
				i -= 1;
				if (i) {
					ret = read(fd, buf_tmp, i);
					if (unlikely(ret != i)) {
						applog(LOG_ERR, "Avalon2: Error %d on read in avalon_gets", errno);
						return AVA2_GETS_ERROR;
					}
					memcpy(buf_copy, buf_back + i, AVA2_READ_SIZE - i);
					memcpy(buf_copy + AVA2_READ_SIZE - i, buf_tmp, i);
					memcpy(buf_back, buf_copy, AVA2_READ_SIZE);
				}
				return AVA2_GETS_OK;
			}
			buf += ret;
			read_amount -= ret;
			continue;
		}

		return AVA2_GETS_TIMEOUT;
	}
}

static int avalon2_send_pkg(int fd, const struct avalon2_pkg *pkg,
			    struct thr_info __maybe_unused *thr)
{
	int ret;
	uint8_t buf[AVA2_WRITE_SIZE];
	int nr_len = AVA2_WRITE_SIZE;

	memcpy(buf, pkg, AVA2_WRITE_SIZE);
	if (opt_debug) {
		applog(LOG_DEBUG, "Avalon2: Sent(%d):", nr_len);
		hexdump((uint8_t *)buf, nr_len);
	}

	ret = write(fd, buf, nr_len);
	if (unlikely(ret != nr_len)) {
		applog(LOG_DEBUG, "Avalon2: Send(%d)!", ret);
		return AVA2_SEND_ERROR;
	}

	cgsleep_ms(20);
#if 0
	ret = avalon2_gets(fd, result);
	if (ret != AVA2_GETS_OK) {
		applog(LOG_DEBUG, "Avalon2: Get(%d)!", ret);
		return AVA2_SEND_ERROR;
	}

	ret = decode_pkg(thr, &ar, result);
	if (ret != AVA2_P_ACK) {
		applog(LOG_DEBUG, "Avalon2: PKG(%d)!", ret);
		hexdump((uint8_t *)result, AVA2_READ_SIZE);
		return AVA2_SEND_ERROR;
	}
#endif

	return AVA2_SEND_OK;
}

static int avalon2_stratum_pkgs(int fd, struct pool *pool, struct thr_info *thr)
{
	const int merkle_offset = 36;
	struct avalon2_pkg pkg;
	int i, a, b, tmp;
	unsigned char target[32];
	int job_id_len;
	unsigned short crc;

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

	tmp = be32toh(pool->n2size);
	memcpy(pkg.data + 8, &tmp, 4);

	tmp = be32toh(merkle_offset);
	memcpy(pkg.data + 12, &tmp, 4);

	tmp = be32toh(pool->merkles);
	memcpy(pkg.data + 16, &tmp, 4);

	tmp = be32toh((int)pool->swork.diff);
	memcpy(pkg.data + 20, &tmp, 4);

	tmp = be32toh((int)pool->pool_no);
	memcpy(pkg.data + 24, &tmp, 4);

	avalon2_init_pkg(&pkg, AVA2_P_STATIC, 1, 1);
	while (avalon2_send_pkg(fd, &pkg, thr) != AVA2_SEND_OK)
		;

	set_target(target, pool->sdiff);
	memcpy(pkg.data, target, 32);
	if (opt_debug) {
		char *target_str;
		target_str = bin2hex(target, 32);
		applog(LOG_DEBUG, "Avalon2: Pool stratum target: %s", target_str);
		free(target_str);
	}
	avalon2_init_pkg(&pkg, AVA2_P_TARGET, 1, 1);
	while (avalon2_send_pkg(fd, &pkg, thr) != AVA2_SEND_OK)
		;


	applog(LOG_DEBUG, "Avalon2: Pool stratum message JOBS_ID: %s",
	       pool->swork.job_id);
	memset(pkg.data, 0, AVA2_P_DATA_LEN);

	job_id_len = strlen(pool->swork.job_id);
	crc = crc16(pool->swork.job_id, job_id_len);
	pkg.data[0] = (crc & 0xff00) >> 8;
	pkg.data[1] = crc & 0x00ff;
	avalon2_init_pkg(&pkg, AVA2_P_JOB_ID, 1, 1);
	while (avalon2_send_pkg(fd, &pkg, thr) != AVA2_SEND_OK)
		;

	a = pool->coinbase_len / AVA2_P_DATA_LEN;
	b = pool->coinbase_len % AVA2_P_DATA_LEN;
	applog(LOG_DEBUG, "Avalon2: Pool stratum message COINBASE: %d %d", a, b);
	for (i = 0; i < a; i++) {
		memcpy(pkg.data, pool->coinbase + i * 32, 32);
		avalon2_init_pkg(&pkg, AVA2_P_COINBASE, i + 1, a + (b ? 1 : 0));
		while (avalon2_send_pkg(fd, &pkg, thr) != AVA2_SEND_OK)
			;
	}
	if (b) {
		memset(pkg.data, 0, AVA2_P_DATA_LEN);
		memcpy(pkg.data, pool->coinbase + i * 32, b);
		avalon2_init_pkg(&pkg, AVA2_P_COINBASE, i + 1, i + 1);
		while (avalon2_send_pkg(fd, &pkg, thr) != AVA2_SEND_OK)
			;
	}

	b = pool->merkles;
	applog(LOG_DEBUG, "Avalon2: Pool stratum message MERKLES: %d", b);
	for (i = 0; i < b; i++) {
		memset(pkg.data, 0, AVA2_P_DATA_LEN);
		memcpy(pkg.data, pool->swork.merkle_bin[i], 32);
		avalon2_init_pkg(&pkg, AVA2_P_MERKLES, i + 1, b);
		while (avalon2_send_pkg(fd, &pkg, thr) != AVA2_SEND_OK)
			;
	}

	applog(LOG_DEBUG, "Avalon2: Pool stratum message HEADER: 4");
	for (i = 0; i < 4; i++) {
		memset(pkg.data, 0, AVA2_P_HEADER);
		memcpy(pkg.data, pool->header_bin + i * 32, 32);
		avalon2_init_pkg(&pkg, AVA2_P_HEADER, i + 1, 4);
		while (avalon2_send_pkg(fd, &pkg, thr) != AVA2_SEND_OK)
			;

	}
	return 0;
}

static int avalon2_get_result(struct thr_info *thr, int fd_detect, struct avalon2_ret *ar)
{
	struct cgpu_info *avalon2;
	struct avalon2_info *info;
	int fd;

	fd = fd_detect;
	if (thr) {
		avalon2 = thr->cgpu;
		info = avalon2->device_data;
		fd = info->fd;
	}

	uint8_t result[AVA2_READ_SIZE];
	int ret;

	memset(result, 0, AVA2_READ_SIZE);

	ret = avalon2_gets(fd, result);
	if (ret != AVA2_GETS_OK)
		return ret;

	if (opt_debug) {
		applog(LOG_DEBUG, "Avalon2: Get(ret = %d):", ret);
		hexdump((uint8_t *)result, AVA2_READ_SIZE);
	}

	return decode_pkg(thr, ar, result);
}

static bool avalon2_detect_one(const char *devpath)
{
	struct avalon2_info *info;
	int ackdetect;
	int fd;
	int tmp, i, j, modular[AVA2_DEFAULT_MODULARS];
	char mm_version[AVA2_DEFAULT_MODULARS][16];

	struct cgpu_info *avalon2;
	struct avalon2_pkg detect_pkg;
	struct avalon2_ret ret_pkg;

	applog(LOG_DEBUG, "Avalon2 Detect: Attempting to open %s", devpath);

	fd = avalon2_open(devpath, AVA2_IO_SPEED, true);
	if (unlikely(fd == -1)) {
		applog(LOG_ERR, "Avalon2 Detect: Failed to open %s", devpath);
		return false;
	}
	tcflush(fd, TCIOFLUSH);

	for (i = 0; i < AVA2_DEFAULT_MODULARS; i++)
		modular[i] = 0;

	for (j = 0; j < 2; j++) {
		for (i = 0; i < AVA2_DEFAULT_MODULARS; i++) {
			strcpy(mm_version[i], AVA2_MM_VERNULL);
			/* Send out detect pkg */
			memset(detect_pkg.data, 0, AVA2_P_DATA_LEN);
			tmp = be32toh(i);
			memcpy(detect_pkg.data + 28, &tmp, 4);

			avalon2_init_pkg(&detect_pkg, AVA2_P_DETECT, 1, 1);
			avalon2_send_pkg(fd, &detect_pkg, NULL);
			ackdetect = avalon2_get_result(NULL, fd, &ret_pkg);
			applog(LOG_DEBUG, "Avalon2 Detect ID[%d]: %d", i, ackdetect);
			if (ackdetect != AVA2_P_ACKDETECT && modular[i] == 0)
				continue;
			modular[i] = 1;
			memcpy(mm_version[i], ret_pkg.data, 15);
			mm_version[i][15] = '\0';
		}
	}
	avalon2_close(fd);
	if (!modular[0] && !modular[1] && !modular[2] && !modular[3])
		return false;

	/* We have a real Avalon! */
	avalon2 = calloc(1, sizeof(struct cgpu_info));
	avalon2->drv = &avalon2_drv;
	avalon2->device_path = strdup(devpath);
	avalon2->threads = AVA2_MINER_THREADS;
	add_cgpu(avalon2);

	applog(LOG_INFO, "Avalon2 Detect: Found at %s, mark as %d",
	       devpath, avalon2->device_id);

	avalon2->device_data = calloc(sizeof(struct avalon2_info), 1);
	if (unlikely(!(avalon2->device_data)))
		quit(1, "Failed to malloc avalon2_info");

	info = avalon2->device_data;

	info->fd = -1;
	info->baud = AVA2_IO_SPEED;
	info->fan_pwm = AVA2_DEFAULT_FAN_PWM;
	info->set_voltage = AVA2_DEFAULT_VOLTAGE_MIN;
	info->set_frequency = AVA2_DEFAULT_FREQUENCY;
	info->temp_max = 0;

	for (i = 0; i < AVA2_DEFAULT_MODULARS; i++) {
		strcpy(info->mm_version[i], mm_version[i]);
		info->modulars[i] = modular[i];	/* Enable modular */
		info->enable[i] = modular[i];
		info->dev_type[i] = AVA2_ID_AVAX;

		if (!strncmp((char *)&(info->mm_version[i]), AVA2_FW2_PREFIXSTR, 2))
			info->dev_type[i] = AVA2_ID_AVA2;
		if (!strncmp((char *)&(info->mm_version[i]), AVA2_FW3_PREFIXSTR, 2))
			info->dev_type[i] = AVA2_ID_AVA3;
	}

	pool_stratum.swork.job_id = NULL;
	pool_stratum.merkles = 0;
	return true;
}

static inline void avalon2_detect(bool __maybe_unused hotplug)
{
	serial_detect(&avalon2_drv, avalon2_detect_one);
}

static void avalon2_init(struct cgpu_info *avalon2)
{
	int fd;
	struct avalon2_info *info = avalon2->device_data;

	fd = avalon2_open(avalon2->device_path, info->baud, true);
	if (unlikely(fd == -1)) {
		applog(LOG_ERR, "Avalon2: Failed to open on %s", avalon2->device_path);
		return;
	}
	applog(LOG_DEBUG, "Avalon2: Opened on %s", avalon2->device_path);

	info->fd = fd;
}

static bool avalon2_prepare(struct thr_info *thr)
{
	struct cgpu_info *avalon2 = thr->cgpu;
	struct avalon2_info *info = avalon2->device_data;

	free(avalon2->works);
	avalon2->works = calloc(sizeof(struct work *), 2);
	if (!avalon2->works)
		quit(1, "Failed to calloc avalon2 works in avalon2_prepare");

	if (info->fd == -1)
		avalon2_init(avalon2);

	info->first = 0;

	cglock_init(&(pool_stratum.data_lock));

	return true;
}

static int polling(struct thr_info *thr)
{
	int i, tmp;

	struct avalon2_pkg send_pkg;
	struct avalon2_ret ar;

	struct cgpu_info *avalon2 = thr->cgpu;
	struct avalon2_info *info = avalon2->device_data;

	static int pre_led_red[AVA2_DEFAULT_MODULARS];
	for (i = 0; i < AVA2_DEFAULT_MODULARS; i++) {
		if (info->modulars[i] && info->enable[i]) {
			cgsleep_ms(20);
			memset(send_pkg.data, 0, AVA2_P_DATA_LEN);

			tmp = be32toh(info->led_red[i]); /* RED LED */
			memcpy(send_pkg.data + 12, &tmp, 4);

			tmp = be32toh(i); /* ID */
			memcpy(send_pkg.data + 28, &tmp, 4);
			if (info->led_red[i] && mm_cmp_1404(info, i)) {
				avalon2_init_pkg(&send_pkg, AVA2_P_TEST, 1, 1);
				while (avalon2_send_pkg(info->fd, &send_pkg, thr) != AVA2_SEND_OK)
					;
				info->enable[i] = 0;
				continue;
			} else
				avalon2_init_pkg(&send_pkg, AVA2_P_POLLING, 1, 1);

			while (avalon2_send_pkg(info->fd, &send_pkg, thr) != AVA2_SEND_OK)
				;
			avalon2_get_result(thr, info->fd, &ar);
		}
	}

	return 0;
}

static void copy_pool_stratum(struct pool *pool)
{
	int i;
	int merkles = pool->merkles;
	size_t coinbase_len = pool->coinbase_len;

	if (!job_idcmp(pool->swork.job_id, pool_stratum.swork.job_id))
		return;

	cg_wlock(&(pool_stratum.data_lock));
	free(pool_stratum.swork.job_id);
	free(pool_stratum.nonce1);
	free(pool_stratum.coinbase);

	align_len(&coinbase_len);
	pool_stratum.coinbase = calloc(coinbase_len, 1);
	if (unlikely(!pool_stratum.coinbase))
		quit(1, "Failed to calloc pool_stratum coinbase in avalon2");
	memcpy(pool_stratum.coinbase, pool->coinbase, coinbase_len);


	for (i = 0; i < pool_stratum.merkles; i++)
		free(pool_stratum.swork.merkle_bin[i]);
	if (merkles) {
		pool_stratum.swork.merkle_bin = realloc(pool_stratum.swork.merkle_bin,
						 sizeof(char *) * merkles + 1);
		for (i = 0; i < merkles; i++) {
			pool_stratum.swork.merkle_bin[i] = malloc(32);
			if (unlikely(!pool_stratum.swork.merkle_bin[i]))
				quit(1, "Failed to malloc pool_stratum swork merkle_bin");
			memcpy(pool_stratum.swork.merkle_bin[i], pool->swork.merkle_bin[i], 32);
		}
	}

	pool_stratum.sdiff = pool->sdiff;
	pool_stratum.coinbase_len = pool->coinbase_len;
	pool_stratum.nonce2_offset = pool->nonce2_offset;
	pool_stratum.n2size = pool->n2size;
	pool_stratum.merkles = pool->merkles;

	pool_stratum.swork.job_id = strdup(pool->swork.job_id);
	pool_stratum.nonce1 = strdup(pool->nonce1);

	memcpy(pool_stratum.ntime, pool->ntime, sizeof(pool_stratum.ntime));
	memcpy(pool_stratum.header_bin, pool->header_bin, sizeof(pool_stratum.header_bin));
	cg_wunlock(&(pool_stratum.data_lock));
}

static int64_t avalon2_scanhash(struct thr_info *thr)
{
	struct avalon2_pkg send_pkg;
	struct timeval current_stratum;

	struct pool *pool;
	struct cgpu_info *avalon2 = thr->cgpu;
	struct avalon2_info *info = avalon2->device_data;

	int64_t h;
	uint32_t tmp, range, start;
	int i;

	if (thr->work_restart || thr->work_update || !info->first) {
		applog(LOG_DEBUG, "Avalon2: New stratum: restart: %d, update: %d, first: %d",
		       thr->work_restart, thr->work_update, info->first);
		thr->work_update = false;
		thr->work_restart = false;
		get_work(thr, thr->id); /* Make sure pool is ready */

		pool = current_pool();
		if (!pool->has_stratum)
			quit(1, "Avalon2: Miner Manager have to use stratum pool");
		if (pool->coinbase_len > AVA2_P_COINBASE_SIZE) {
			applog(LOG_ERR, "Avalon2: Miner Manager pool coinbase length have to less then %d", AVA2_P_COINBASE_SIZE);
			return 0;
		}
		if (pool->merkles > AVA2_P_MERKLES_COUNT) {
			applog(LOG_ERR, "Avalon2: Miner Manager merkles have to less then %d", AVA2_P_MERKLES_COUNT);
			return 0;
		}

		cgtime(&info->last_stratum);
		cg_rlock(&pool->data_lock);
		info->pool_no = pool->pool_no;
		copy_pool_stratum(pool);
		avalon2_stratum_pkgs(info->fd, pool, thr);
		cg_runlock(&pool->data_lock);

		/* Configuer the parameter from outside */
		adjust_fan(info);
		info->set_voltage = opt_avalon2_voltage_min;
		info->set_frequency = opt_avalon2_freq_min;

		/* Set the Fan, Voltage and Frequency */
		memset(send_pkg.data, 0, AVA2_P_DATA_LEN);

		tmp = be32toh(info->fan_pwm);
		memcpy(send_pkg.data, &tmp, 4);

		applog(LOG_ERR, "Avalon2: Temp max: %d, Cut off temp: %d",
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
		range = 0xffffffff / total_devices;
		start = range * avalon2->device_id;

		tmp = be32toh(start);
		memcpy(send_pkg.data + 12, &tmp, 4);

		tmp = be32toh(range);
		memcpy(send_pkg.data + 16, &tmp, 4);

		/* Package the data */
		avalon2_init_pkg(&send_pkg, AVA2_P_SET, 1, 1);
		while (avalon2_send_pkg(info->fd, &send_pkg, thr) != AVA2_SEND_OK)
			;

		if (unlikely(info->first < 2))
			info->first++;
	}

	/* Stop polling the device if there is no stratum in 3 minutes, network is down */
	cgtime(&current_stratum);
	if (tdiff(&current_stratum, &(info->last_stratum)) > (double)(3.0 * 60.0))
		return 0;

	polling(thr);

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
		root = api_add_string(root, buf, &(info->mm_version[i]), false);
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

	return root;
}

static char *avalon2_set_device(struct cgpu_info *avalon2, char *option, char *setting, char *replybuf)
{
	int val;
	struct avalon2_info *info;

	if (strcasecmp(option, "help") == 0) {
		sprintf(replybuf, "led: module_id");
		return replybuf;
	}

	if (strcasecmp(option, "led") == 0) {
		if (!setting || !*setting) {
			sprintf(replybuf, "missing module_id setting");
			return replybuf;
		}

		val = atoi(setting);
		if (val < 1 || val > AVA2_DEFAULT_MODULARS) {
			sprintf(replybuf, "invalid module_id: %d, valid range 1-%d", val, AVA2_DEFAULT_MODULARS);
			return replybuf;
		}

		val -= 1;

		info = avalon2->device_data;
		info->led_red[val] = !info->led_red[val];
		if (!info->led_red[val] && mm_cmp_1404(info, val)) {
			applog(LOG_ERR, "Avalon2: MM early then MM_XX1404, enable module:%d", val + 1);
			info->enable[val] = 1;
		}

		applog(LOG_ERR, "Avalon2: Module:%d, LED: %s", val + 1, info->led_red[val] ? "on" : "off");
		return NULL;
	}
	/* TODO: Add other commands */

	sprintf(replybuf, "Unknown option: %s", option);
	return replybuf;
}

static void avalon2_shutdown(struct thr_info *thr)
{
	struct cgpu_info *avalon = thr->cgpu;

	free(avalon->works);
	avalon->works = NULL;
}

struct device_drv avalon2_drv = {
	.drv_id = DRIVER_avalon2,
	.dname = "avalon2",
	.name = "AV2",
	.get_api_stats = avalon2_api_stats,
	.set_device = avalon2_set_device,
	.drv_detect = avalon2_detect,
	.reinit_device = avalon2_init,
	.thread_prepare = avalon2_prepare,
	.hash_work = hash_driver_work,
	.scanwork = avalon2_scanhash,
	.thread_shutdown = avalon2_shutdown,
};
