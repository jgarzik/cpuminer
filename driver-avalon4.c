/*
 * Copyright 2014-2015 Mikeqin <Fengling.Qin@gmail.com>
 * Copyright 2013-2015 Con Kolivas <kernel@kolivas.org>
 * Copyright 2012-2015 Xiangfu <xiangfu@openmobilefree.com>
 * Copyright 2012 Luke Dashjr
 * Copyright 2012 Andrew Smith
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 3 of the License, or (at your option)
 * any later version.  See COPYING for more details.
 */
#include <math.h>
#include "config.h"

#include "miner.h"
#include "driver-avalon4.h"
#include "crc.h"
#include "sha2.h"
#include "hexdump.c"

#define get_fan_pwm(v)	(AVA4_PWM_MAX - (v) * AVA4_PWM_MAX / 100)

int opt_avalon4_temp_target = AVA4_DEFAULT_TEMP_TARGET;
int opt_avalon4_overheat = AVA4_DEFAULT_TEMP_OVERHEAT;

int opt_avalon4_fan_min = AVA4_DEFAULT_FAN_MIN;
int opt_avalon4_fan_max = AVA4_DEFAULT_FAN_MAX;

bool opt_avalon4_autov;
bool opt_avalon4_freezesafe;
int opt_avalon4_voltage_min = AVA4_DEFAULT_VOLTAGE;
int opt_avalon4_voltage_max = AVA4_DEFAULT_VOLTAGE;
unsigned int opt_avalon4_freq[3] = {AVA4_DEFAULT_FREQUENCY,
			   AVA4_DEFAULT_FREQUENCY,
			   AVA4_DEFAULT_FREQUENCY};

int opt_avalon4_polling_delay = AVA4_DEFAULT_POLLING_DELAY;

int opt_avalon4_aucspeed = AVA4_AUC_SPEED;
int opt_avalon4_aucxdelay = AVA4_AUC_XDELAY;

int opt_avalon4_ntime_offset = AVA4_DEFAULT_ASIC_MAX;
int opt_avalon4_miningmode = AVA4_MOD_CUSTOM;

int opt_avalon4_ntcb = AVA4_DEFAULT_NTCB;
int opt_avalon4_freq_min = AVA4_DEFAULT_FREQUENCY_MIN;
int opt_avalon4_freq_max = AVA4_DEFAULT_FREQUENCY_MAX;
bool opt_avalon4_noncecheck = AVA4_DEFAULT_NCHECK;
int opt_avalon4_smart_speed = AVA4_DEFAULT_SMART_SPEED;
/*
 * smart speed have 3 modes
 * 1. auto speed by A3218 chips
 * 2. option 1 + adjust by least pll count
 *    option 1 + adjust by most pll count
 * 3. option 1 + adjust by average frequency
 */
int opt_avalon4_least_pll_check = AVA4_DEFAULT_LEAST_PLL;
int opt_avalon4_most_pll_check = AVA4_DEFAULT_MOST_PLL;
int opt_avalon4_speed_bingo = AVA4_DEFAULT_SPEED_BINGO;
int opt_avalon4_speed_error = AVA4_DEFAULT_SPEED_ERROR;
bool opt_avalon4_iic_detect = AVA4_DEFAULT_IIC_DETECT;
int opt_avalon4_freqadj_time = AVA4_DEFAULT_FREQADJ_TIME;
int opt_avalon4_delta_temp = AVA4_DEFAULT_DELTA_T;
int opt_avalon4_delta_freq = AVA4_DEFAULT_DELTA_FREQ;
int opt_avalon4_freqadj_temp = AVA4_MM60_TEMP_FREQADJ;

static uint8_t avalon4_freezsafemode = 0;
/* Only for Avalon4 */
static uint32_t g_freq_array[][2] = {
	{100, 0x1e678447},
	{113, 0x22688447},
	{125, 0x1c470447},
	{138, 0x2a6a8447},
	{150, 0x22488447},
	{163, 0x326c8447},
	{175, 0x1a268447},
	{188, 0x1c270447},
	{200, 0x1e278447},
	{213, 0x20280447},
	{225, 0x22288447},
	{238, 0x24290447},
	{250, 0x26298447},
	{263, 0x282a0447},
	{275, 0x2a2a8447},
	{288, 0x2c2b0447},
	{300, 0x2e2b8447},
	{313, 0x302c0447},
	{325, 0x322c8447},
	{338, 0x342d0447},
	{350, 0x1a068447},
	{363, 0x382e0447},
	{375, 0x1c070447},
	{388, 0x3c2f0447},
	{400, 0x1e078447},
	{413, 0x40300447},
	{425, 0x20080447},
	{438, 0x44310447},
	{450, 0x22088447},
	{463, 0x48320447},
	{475, 0x24090447},
	{488, 0x4c330447},
	{500, 0x26098447},
	{513, 0x50340447},
	{525, 0x280a0447},
	{538, 0x54350447},
	{550, 0x2a0a8447},
	{563, 0x58360447},
	{575, 0x2c0b0447},
	{588, 0x5c370447},
	{600, 0x2e0b8447},
	{613, 0x60380447},
	{625, 0x300c0447},
	{638, 0x64390447},
	{650, 0x320c8447},
	{663, 0x683a0447},
	{675, 0x340d0447},
	{688, 0x6c3b0447},
	{700, 0x360d8447},
	{713, 0x703c0447},
	{725, 0x380e0447},
	{738, 0x743d0447},
	{750, 0x3a0e8447},
	{763, 0x783e0447},
	{775, 0x3c0f0447},
	{788, 0x7c3f0447},
	{800, 0x3e0f8447},
	{813, 0x3e0f8447},
	{825, 0x40100447},
	{838, 0x40100447},
	{850, 0x42108447},
	{863, 0x42108447},
	{875, 0x44110447},
	{888, 0x44110447},
	{900, 0x46118447},
	{913, 0x46118447},
	{925, 0x48120447},
	{938, 0x48120447},
	{950, 0x4a128447},
	{963, 0x4a128447},
	{975, 0x4c130447},
	{988, 0x4c130447},
	{1000, 0x4e138447}
};

#define UNPACK32(x, str)			\
{						\
	*((str) + 3) = (uint8_t) ((x)      );	\
	*((str) + 2) = (uint8_t) ((x) >>  8);	\
	*((str) + 1) = (uint8_t) ((x) >> 16);	\
	*((str) + 0) = (uint8_t) ((x) >> 24);	\
}

static inline void sha256_prehash(const unsigned char *message, unsigned int len, unsigned char *digest)
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

#define R_REF	10000
#define R0	10000
#define T0	25
static float convert_temp(uint16_t adc)
{
	float ret, resistance;

	if (!adc || adc >= AVA4_ADC_MAX)
		return -273.15;

	resistance = (AVA4_ADC_MAX * 1.0 / adc) - 1;
	resistance = R_REF / resistance;
	ret = resistance / R0;
	ret = logf(ret);
	ret /= opt_avalon4_ntcb;
	ret += 1.0 / (T0 + 273.15);
	ret = 1.0 / ret;
	ret -= 273.15;

	return ret;
}

#define V_REF	3.3
static float convert_voltage(uint16_t adc, float percent)
{
	float voltage;

	voltage = adc * V_REF * 1.0 / AVA4_ADC_MAX / percent;
	return voltage + 0.4;
}

char *set_avalon4_fan(char *arg)
{
	int val1, val2, ret;

	ret = sscanf(arg, "%d-%d", &val1, &val2);
	if (ret < 1)
		return "No values passed to avalon4-fan";
	if (ret == 1)
		val2 = val1;

	if (val1 < 0 || val1 > 100 || val2 < 0 || val2 > 100 || val2 < val1)
		return "Invalid value passed to avalon4-fan";

	opt_avalon4_fan_min = val1;
	opt_avalon4_fan_max = val2;

	return NULL;
}

char *set_avalon4_freq(char *arg)
{
	char *colon1, *colon2;
	int val1 = 0, val2 = 0, val3 = 0;

	if (!(*arg))
		return NULL;

	colon1 = strchr(arg, ':');
	if (colon1)
		*(colon1++) = '\0';

	if (*arg) {
		val1 = atoi(arg);
		if (val1 < AVA4_DEFAULT_FREQUENCY_MIN || val1 > AVA4_DEFAULT_FREQUENCY_MAX)
			return "Invalid value1 passed to avalon4-freq";
	}

	if (colon1 && *colon1) {
		colon2 = strchr(colon1, ':');
		if (colon2)
			*(colon2++) = '\0';

		if (*colon1) {
			val2 = atoi(colon1);
			if (val2 < AVA4_DEFAULT_FREQUENCY_MIN || val2 > AVA4_DEFAULT_FREQUENCY_MAX)
				return "Invalid value2 passed to avalon4-freq";
		}

		if (colon2 && *colon2) {
			val3 = atoi(colon2);
			if (val3 < AVA4_DEFAULT_FREQUENCY_MIN || val3 > AVA4_DEFAULT_FREQUENCY_MAX)
				return "Invalid value3 passed to avalon4-freq";
		}
	}

	if (!val1)
		val3 = val2 = val1 = AVA4_DEFAULT_FREQUENCY;

	if (!val2)
		val3 = val2 = val1;

	if (!val3)
		val3 = val2;

	opt_avalon4_freq[0] = val1;
	opt_avalon4_freq[1] = val2;
	opt_avalon4_freq[2] = val3;

	return NULL;
}

char *set_avalon4_voltage(char *arg)
{
	int val1, val2, ret;

	ret = sscanf(arg, "%d-%d", &val1, &val2);
	if (ret < 1)
		return "No values passed to avalon4-voltage";
	if (ret == 1)
		val2 = val1;

	if (val1 < AVA4_DEFAULT_VOLTAGE_MIN || val1 > AVA4_DEFAULT_VOLTAGE_MAX ||
	    val2 < AVA4_DEFAULT_VOLTAGE_MIN || val2 > AVA4_DEFAULT_VOLTAGE_MAX ||
	    val2 < val1)
		return "Invalid value passed to avalon4-voltage";

	opt_avalon4_voltage_min = val1;
	opt_avalon4_voltage_max = val2;

	return NULL;
}

static int avalon4_init_pkg(struct avalon4_pkg *pkg, uint8_t type, uint8_t idx, uint8_t cnt)
{
	unsigned short crc;

	pkg->head[0] = AVA4_H1;
	pkg->head[1] = AVA4_H2;

	pkg->type = type;
	pkg->opt = 0;
	pkg->idx = idx;
	pkg->cnt = cnt;

	crc = crc16(pkg->data, AVA4_P_DATA_LEN);

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

	applog(LOG_DEBUG, "Avalon4: job_id doesn't match! [%04x:%04x (%s)]",
	       crc, crc_expect, pool_job_id);

	return 1;
}

static inline int get_current_temp_max(struct avalon4_info *info)
{
	int i;
	int t = info->temp[0];

	for (i = 1; i < AVA4_DEFAULT_MODULARS; i++) {
		if (info->enable[i] && info->temp[i] > t)
			t = info->temp[i];
	}
	return t;
}

static inline int get_temp_max(struct avalon4_info *info, int addr)
{
	int i;
	int t = -273, t1 = -273;

	if (info->enable[addr]) {
		t = info->temp[addr];

		for (i = 0; i < 2; i++) {
			t1 = convert_temp(info->adc[addr][i]);
			if (t1 > t)
				t = t1;
		}
	}
	return t;
}

/* http://www.onsemi.com/pub_link/Collateral/ADP3208D.PDF */
static uint32_t encode_voltage_adp3208d(uint32_t v)
{
	return rev8((0x78 - v / 125) << 1 | 1) << 8;
}

static uint32_t decode_voltage_adp3208d(uint32_t v)
{
	return (0x78 - (rev8(v >> 8) >> 1)) * 125;
}

/* http://www.onsemi.com/pub/Collateral/NCP5392P-D.PDF */
static uint32_t encode_voltage_ncp5392p(uint32_t v)
{
	if (v == 0)
		return 0xff00;

	return rev8(((0x59 - (v - 5000) / 125) & 0xff) << 1 | 1) << 8;
}

static uint32_t decode_voltage_ncp5392p(uint32_t v)
{
	if (v == 0xff00)
		return 0;

	return (0x59 - (rev8(v >> 8) >> 1)) * 125 + 5000;
}

static inline uint32_t adjust_fan(struct avalon4_info *info, int id)
{
	uint32_t pwm;
	int t = info->temp[id], diff, fandiff = opt_avalon4_fan_max - opt_avalon4_fan_min;;

	if (info->mod_type[id] == AVA4_TYPE_MM60) {
		int t1, t2;
		t1 = (int)convert_temp(info->adc[id][0]),
		t2 = (int)convert_temp(info->adc[id][1]),

		t = t > t1 ? t : t1;
		t = t > t2 ? t : t2;
	}

	if (avalon4_freezsafemode) {
		pwm = get_fan_pwm(AVA4_FREEZESAFE_FAN);
		return pwm;
	}

	/* Scale fan% non linearly relatively to target temperature. It will
	 * not try to keep the temperature at temp_target that accurately but
	 * avoids temperature overshoot in both directions. */
	diff = t - opt_avalon4_temp_target + 30;
	if (diff > 32)
		diff = 32;
	else if (diff < 0)
		diff = 0;
	diff *= diff;
	fandiff = fandiff * diff / 1024;
	info->fan_pct[id] = opt_avalon4_fan_min + fandiff;

	pwm = get_fan_pwm(info->fan_pct[id]);

	if ((info->mod_type[id] == AVA4_TYPE_MM60) &&
		(info->freq_mode[id] == AVA4_FREQ_TEMPADJ_MODE))
		pwm = get_fan_pwm(opt_avalon4_fan_max);

	if (info->cutoff[id])
		pwm = get_fan_pwm(opt_avalon4_fan_max);

	applog(LOG_DEBUG, "[%d], Adjust_fan: %dC-%d%%(%03x)", id, t, info->fan_pct[id], pwm);

	return pwm;
}

static int decode_pkg(struct thr_info *thr, struct avalon4_ret *ar, int modular_id)
{
	struct cgpu_info *avalon4 = thr->cgpu;
	struct avalon4_info *info = avalon4->device_data;
	struct pool *pool, *real_pool;
	struct pool *pool_stratum0 = &info->pool0;
	struct pool *pool_stratum1 = &info->pool1;
	struct pool *pool_stratum2 = &info->pool2;

	unsigned int expected_crc;
	unsigned int actual_crc;
	uint32_t nonce, nonce2, ntime, miner, chip_id, volt, tmp;
	uint8_t job_id[4];
	int pool_no, i;
	uint32_t val[AVA4_DEFAULT_MINER_MAX];

	if (ar->head[0] != AVA4_H1 && ar->head[1] != AVA4_H2) {
		applog(LOG_DEBUG, "%s-%d-%d: H1 %02x, H2 %02x",
				avalon4->drv->name, avalon4->device_id, modular_id,
				ar->head[0], ar->head[1]);
		hexdump(ar->data, 32);
		return 1;
	}

	expected_crc = crc16(ar->data, AVA4_P_DATA_LEN);
	actual_crc = (ar->crc[0] & 0xff) | ((ar->crc[1] & 0xff) << 8);
	if (expected_crc != actual_crc) {
		applog(LOG_DEBUG, "%s-%d-%d: %02x: expected crc(%04x), actual_crc(%04x)",
		       avalon4->drv->name, avalon4->device_id, modular_id,
		       ar->type, expected_crc, actual_crc);
		return 1;
	}

	switch(ar->type) {
	case AVA4_P_NONCE:
		applog(LOG_DEBUG, "%s-%d-%d: AVA4_P_NONCE", avalon4->drv->name, avalon4->device_id, modular_id);
		memcpy(&miner, ar->data + 0, 4);
		memcpy(&pool_no, ar->data + 4, 4);
		memcpy(&nonce2, ar->data + 8, 4);
		memcpy(&ntime, ar->data + 12, 4);
		memcpy(&nonce, ar->data + 16, 4);
		memcpy(job_id, ar->data + 20, 4);

		miner = be32toh(miner);
		chip_id = (miner >> 16) & 0xffff;
		miner &= 0xffff;
		pool_no = be32toh(pool_no);
		ntime = be32toh(ntime);
		if (miner >= info->miner_count[modular_id] ||
		    pool_no >= total_pools || pool_no < 0) {
			applog(LOG_DEBUG, "%s-%d-%d: Wrong miner/pool_no %d/%d",
					avalon4->drv->name, avalon4->device_id, modular_id,
					miner, pool_no);
			break;
		}
		nonce2 = be32toh(nonce2);
		nonce = be32toh(nonce);

		if ((info->mod_type[modular_id] == AVA4_TYPE_MM40) ||
			(info->mod_type[modular_id] == AVA4_TYPE_MM41) ||
			(info->mod_type[modular_id] == AVA4_TYPE_MM50))
			nonce -= 0x4000;

		applog(LOG_DEBUG, "%s-%d-%d: Found! P:%d - N2:%08x N:%08x NR:%d [M:%d - MW: %d(%d,%d,%d,%d)]",
		       avalon4->drv->name, avalon4->device_id, modular_id,
		       pool_no, nonce2, nonce, ntime,
		       miner, info->matching_work[modular_id][miner],
		       info->chipmatching_work[modular_id][miner][0],
		       info->chipmatching_work[modular_id][miner][1],
		       info->chipmatching_work[modular_id][miner][2],
		       info->chipmatching_work[modular_id][miner][3]);

		real_pool = pool = pools[pool_no];
		if (job_idcmp(job_id, pool->swork.job_id)) {
			if (!job_idcmp(job_id, pool_stratum0->swork.job_id)) {
				applog(LOG_DEBUG, "%s-%d-%d: Match to previous stratum0! (%s)",
						avalon4->drv->name, avalon4->device_id, modular_id,
						pool_stratum0->swork.job_id);
				pool = pool_stratum0;
			} else if (!job_idcmp(job_id, pool_stratum1->swork.job_id)) {
				applog(LOG_DEBUG, "%s-%d-%d: Match to previous stratum1! (%s)",
						avalon4->drv->name, avalon4->device_id, modular_id,
						pool_stratum1->swork.job_id);
				pool = pool_stratum1;
			} else if (!job_idcmp(job_id, pool_stratum2->swork.job_id)) {
				applog(LOG_DEBUG, "%s-%d-%d: Match to previous stratum2! (%s)",
						avalon4->drv->name, avalon4->device_id, modular_id,
						pool_stratum2->swork.job_id);
				pool = pool_stratum2;
			} else {
				applog(LOG_ERR, "%s-%d-%d: Cannot match to any stratum! (%s)",
						avalon4->drv->name, avalon4->device_id, modular_id,
						pool->swork.job_id);
				inc_hw_errors(thr);
				if (info->mod_type[modular_id] == AVA4_TYPE_MM60) {
					info->hw_works_i[modular_id][miner]++;
					info->hw5_i[modular_id][miner][info->i_5s]++;
				}
				break;
			}
		}

		if (!submit_nonce2_nonce(thr, pool, real_pool, nonce2, nonce, ntime)) {
			if (info->mod_type[modular_id] == AVA4_TYPE_MM60) {
				info->hw_works_i[modular_id][miner]++;
				info->hw5_i[modular_id][miner][info->i_5s]++;
			}
		} else {
			info->matching_work[modular_id][miner]++;
			info->chipmatching_work[modular_id][miner][chip_id]++;
		}
		break;
	case AVA4_P_STATUS:
		applog(LOG_DEBUG, "%s-%d-%d: AVA4_P_STATUS", avalon4->drv->name, avalon4->device_id, modular_id);
		hexdump(ar->data, 32);
		memcpy(&tmp, ar->data, 4);
		tmp = be32toh(tmp);
		info->temp[modular_id] = tmp;

		memcpy(&tmp, ar->data + 4, 4);
		tmp = be32toh(tmp);
		info->fan[modular_id] = tmp;

		memcpy(&(info->get_frequency[modular_id]), ar->data + 8, 4);
		memcpy(&(info->get_voltage[modular_id]), ar->data + 12, 4);
		memcpy(&(info->local_work[modular_id]), ar->data + 16, 4);
		memcpy(&(info->hw_work[modular_id]), ar->data + 20, 4);
		memcpy(&(info->power_good[modular_id]), ar->data + 24, 4);
		memcpy(&(info->error_code[modular_id]), ar->data + 28, 4);

		if (info->mod_type[modular_id] == AVA4_TYPE_MM60) {
			if (info->total_asics[modular_id])
				info->get_frequency[modular_id] = be32toh(info->get_frequency[modular_id]) / info->total_asics[modular_id];
		} else
			info->get_frequency[modular_id] = be32toh(info->get_frequency[modular_id]) * 3968 / 65;
		info->get_voltage[modular_id] = be32toh(info->get_voltage[modular_id]);
		info->local_work[modular_id] = be32toh(info->local_work[modular_id]);
		info->hw_work[modular_id] = be32toh(info->hw_work[modular_id]);
		info->power_good[modular_id] = be32toh(info->power_good[modular_id]);
		info->error_code[modular_id] = be32toh(info->error_code[modular_id]);

		volt = info->get_voltage[modular_id];
		if (info->mod_type[modular_id] == AVA4_TYPE_MM40)
			tmp = decode_voltage_adp3208d(volt);
		if (info->mod_type[modular_id] == AVA4_TYPE_MM41)
			tmp = decode_voltage_ncp5392p(volt);
		if (info->mod_type[modular_id] == AVA4_TYPE_MM50)
			tmp = decode_voltage_ncp5392p(volt);
		/* TODO: fix it with the actual board */
		if (info->mod_type[modular_id] == AVA4_TYPE_MM60)
			tmp = decode_voltage_ncp5392p(volt);

		info->get_voltage[modular_id] = tmp;

		info->local_works[modular_id] += info->local_work[modular_id];
		info->hw_works[modular_id] += info->hw_work[modular_id];

		info->lw5[modular_id][info->i_5s] += info->local_work[modular_id];
		info->hw5[modular_id][info->i_5s] += info->hw_work[modular_id];

		avalon4->temp = get_current_temp_max(info);
		break;
	case AVA4_P_STATUS_LW:
		applog(LOG_DEBUG, "%s-%d-%d: AVA4_P_STATUS_LW", avalon4->drv->name, avalon4->device_id, modular_id);
		for (i = 0; i < info->miner_count[modular_id]; i++) {
			info->local_works_i[modular_id][i] += ((ar->data[i * 3] << 16) |
							    (ar->data[i * 3 + 1] << 8) |
							    (ar->data[i * 3 + 2]));
			info->lw5_i[modular_id][i][info->i_5s] += ((ar->data[i * 3] << 16) |
							    (ar->data[i * 3 + 1] << 8) |
							    (ar->data[i * 3 + 2]));
		}
		break;
	case AVA4_P_STATUS_HW:
		applog(LOG_DEBUG, "%s-%d-%d: AVA4_P_STATUS_HW", avalon4->drv->name, avalon4->device_id, modular_id);
		if (info->mod_type[modular_id] == AVA4_TYPE_MM60) {
			applog(LOG_NOTICE, "%s-%d-%d: AVA4_P_STATUS_HW found on Avalon6!", avalon4->drv->name, avalon4->device_id, modular_id);
			break;
		}

		for (i = 0; i < info->miner_count[modular_id]; i++) {
			info->hw_works_i[modular_id][i] += ((ar->data[i * 3] << 16) |
							    (ar->data[i * 3 + 1] << 8) |
							    (ar->data[i * 3 + 2]));
			info->hw5_i[modular_id][i][info->i_5s] += ((ar->data[i * 3] << 16) |
							    (ar->data[i * 3 + 1] << 8) |
							    (ar->data[i * 3 + 2]));
		}
		break;
	case AVA4_P_ACKDETECT:
		applog(LOG_DEBUG, "%s-%d-%d: AVA4_P_ACKDETECT", avalon4->drv->name, avalon4->device_id, modular_id);
		break;
	case AVA4_P_STATUS_VOLT:
		applog(LOG_DEBUG, "%s-%d-%d: AVA4_P_STATUS_VOLT", avalon4->drv->name, avalon4->device_id, modular_id);
		hexdump(ar->data, 32);
		for(i = 0; i < info->miner_count[modular_id]; i++) {
			tmp = (ar->data[i * 2] << 8) + ar->data[i * 2 + 1];

			if (info->mod_type[modular_id] == AVA4_TYPE_MM40)
				tmp = decode_voltage_adp3208d(tmp);
			if (info->mod_type[modular_id] == AVA4_TYPE_MM41)
				tmp = decode_voltage_ncp5392p(tmp);
			if (info->mod_type[modular_id] == AVA4_TYPE_MM50)
				tmp = decode_voltage_ncp5392p(tmp);
			/* TODO: fix it with the actual board */
			if (info->mod_type[modular_id] == AVA4_TYPE_MM60)
				tmp = decode_voltage_ncp5392p(tmp);

			if (tmp < AVA4_DEFAULT_VOLTAGE_MIN || tmp > AVA4_DEFAULT_VOLTAGE_MAX) {
				applog(LOG_DEBUG, "%s-%d-%d: AVA4_P_STATUS_VOLT invalid voltage %d", avalon4->drv->name, avalon4->device_id, modular_id, tmp);
				return 1;
			}
			val[((4 + i / 5 * 5) - i + (i / 5 * 5))] = tmp;
		}

		if (i == info->miner_count[modular_id]) {
			for (i = 0; i < info->miner_count[modular_id]; i++) {
				info->get_voltage_i[modular_id][i] = val[i];
			}
		}
		break;
	case AVA4_P_STATUS_MA:
		applog(LOG_DEBUG, "%s-%d-%d: AVA4_P_STATUS_MA", avalon4->drv->name, avalon4->device_id, modular_id);
		for (i = 0; i < info->asic_count[modular_id]; i++)
			info->ma_sum[modular_id][ar->opt][i] = ar->data[i];
		break;
	case AVA4_P_STATUS_M:
		applog(LOG_DEBUG, "%s-%d-%d: AVA4_P_STATUS_M", avalon4->drv->name, avalon4->device_id, modular_id);
		for (i = 0; i < AVA4_DEFAULT_ADC_MAX; i++)
			info->adc[modular_id][i] = (ar->data[2 * i] << 8) | ar->data[(2 * i) + 1];

		/* MCU LED status --> data[2 * AVA4_DEFAULT_ADC : 2 * AVA4_DEFAULT_ADC_MAX + 3] */
		for (i = 0; i < AVA4_DEFAULT_PLL_MAX; i++)
			info->pll_sel[modular_id][i] = (ar->data[2 * (AVA4_DEFAULT_ADC_MAX + 2 + i)] << 8) |
				ar->data[(2 * (AVA4_DEFAULT_ADC_MAX + 2 + i)) + 1];
		break;
	default:
		applog(LOG_DEBUG, "%s-%d-%d: Unknown response", avalon4->drv->name, avalon4->device_id, modular_id);
		break;
	}
	return 0;
}

/*
 #  IIC packet format: length[1]+transId[1]+sesId[1]+req[1]+data[60]
 #  length: 4+len(data)
 #  transId: 0
 #  sesId: 0
 #  req: checkout the header file
 #  data:
 #    INIT: clock_rate[4] + reserved[4] + payload[52]
 #    XFER: txSz[1]+rxSz[1]+options[1]+slaveAddr[1] + payload[56]
 */
static int avalon4_auc_init_pkg(uint8_t *iic_pkg, struct avalon4_iic_info *iic_info, uint8_t *buf, int wlen, int rlen)
{
	memset(iic_pkg, 0, AVA4_AUC_P_SIZE);

	switch (iic_info->iic_op) {
	case AVA4_IIC_INIT:
		iic_pkg[0] = 12;	/* 4 bytes IIC header + 4 bytes speed + 4 bytes xfer delay */
		iic_pkg[3] = AVA4_IIC_INIT;
		iic_pkg[4] = iic_info->iic_param.aucParam[0] & 0xff;
		iic_pkg[5] = (iic_info->iic_param.aucParam[0] >> 8) & 0xff;
		iic_pkg[6] = (iic_info->iic_param.aucParam[0] >> 16) & 0xff;
		iic_pkg[7] = iic_info->iic_param.aucParam[0] >> 24;
		iic_pkg[8] = iic_info->iic_param.aucParam[1] & 0xff;
		iic_pkg[9] = (iic_info->iic_param.aucParam[1] >> 8) & 0xff;
		iic_pkg[10] = (iic_info->iic_param.aucParam[1] >> 16) & 0xff;
		iic_pkg[11] = iic_info->iic_param.aucParam[1] >> 24;
		break;
	case AVA4_IIC_XFER:
		iic_pkg[0] = 8 + wlen;
		iic_pkg[3] = AVA4_IIC_XFER;
		iic_pkg[4] = wlen;
		iic_pkg[5] = rlen;
		iic_pkg[7] = iic_info->iic_param.slave_addr;
		if (buf && wlen)
			memcpy(iic_pkg + 8, buf, wlen);
		break;
	case AVA4_IIC_RESET:
	case AVA4_IIC_DEINIT:
	case AVA4_IIC_INFO:
		iic_pkg[0] = 4;
		iic_pkg[3] = iic_info->iic_op;
		break;

	default:
		break;
	}

	return 0;
}

static int avalon4_iic_xfer(struct cgpu_info *avalon4, uint8_t slave_addr,
			    uint8_t *wbuf, int wlen,
			    uint8_t *rbuf, int rlen)
{
	struct avalon4_info *info = avalon4->device_data;
	struct i2c_ctx *pctx = NULL;
	int err = 1;
	bool ret = false;

	pctx = info->i2c_slaves[slave_addr];
	if (!pctx) {
		applog(LOG_ERR, "%s-%d: IIC xfer i2c slaves null!", avalon4->drv->name, avalon4->device_id);
		goto out;
	}

	if (wbuf) {
		ret = pctx->write_raw(pctx, wbuf, wlen);
		if (!ret) {
			applog(LOG_DEBUG, "%s-%d: IIC xfer write raw failed!", avalon4->drv->name, avalon4->device_id);
			goto out;
		}
	}

	cgsleep_ms(5);

	if (rbuf) {
		ret = pctx->read_raw(pctx, rbuf, rlen);
		if (!ret) {
			applog(LOG_DEBUG, "%s-%d: IIC xfer read raw failed!", avalon4->drv->name, avalon4->device_id);
			hexdump(rbuf, rlen);
			goto out;
		}
	}

	return 0;
out:
	return err;
}

static int avalon4_auc_xfer(struct cgpu_info *avalon4,
			    uint8_t *wbuf, int wlen, int *write,
			    uint8_t *rbuf, int rlen, int *read)
{
	int err = -1;

	if (unlikely(avalon4->usbinfo.nodev))
		goto out;

	usb_buffer_clear(avalon4);
	err = usb_write(avalon4, (char *)wbuf, wlen, write, C_AVA4_WRITE);
	if (err || *write != wlen) {
		applog(LOG_DEBUG, "%s-%d: AUC xfer %d, w(%d-%d)!", avalon4->drv->name, avalon4->device_id, err, wlen, *write);
		usb_nodev(avalon4);
		goto out;
	}

	cgsleep_ms(opt_avalon4_aucxdelay / 4800 + 1);

	rlen += 4;		/* Add 4 bytes IIC header */
	err = usb_read(avalon4, (char *)rbuf, rlen, read, C_AVA4_READ);
	if (err || *read != rlen || *read != rbuf[0]) {
		applog(LOG_DEBUG, "%s-%d: AUC xfer %d, r(%d-%d-%d)!", avalon4->drv->name, avalon4->device_id, err, rlen - 4, *read, rbuf[0]);
		hexdump(rbuf, rlen);
		return -1;
	}
	*read = rbuf[0] - 4;	/* Remove 4 bytes IIC header */
out:
	return err;
}

static int avalon4_auc_init(struct cgpu_info *avalon4, char *ver)
{
	struct avalon4_iic_info iic_info;
	int err, wlen, rlen;
	uint8_t wbuf[AVA4_AUC_P_SIZE];
	uint8_t rbuf[AVA4_AUC_P_SIZE];

	if (unlikely(avalon4->usbinfo.nodev))
		return 1;

	/* Try to clean the AUC buffer */
	usb_buffer_clear(avalon4);
	err = usb_read(avalon4, (char *)rbuf, AVA4_AUC_P_SIZE, &rlen, C_AVA4_READ);
	applog(LOG_DEBUG, "%s-%d: AUC usb_read %d, %d!", avalon4->drv->name, avalon4->device_id, err, rlen);
	hexdump(rbuf, AVA4_AUC_P_SIZE);

	/* Reset */
	iic_info.iic_op = AVA4_IIC_RESET;
	rlen = 0;
	avalon4_auc_init_pkg(wbuf, &iic_info, NULL, 0, rlen);

	memset(rbuf, 0, AVA4_AUC_P_SIZE);
	err = avalon4_auc_xfer(avalon4, wbuf, AVA4_AUC_P_SIZE, &wlen, rbuf, rlen, &rlen);
	if (err) {
		applog(LOG_ERR, "%s-%d: Failed to reset Avalon USB2IIC Converter", avalon4->drv->name, avalon4->device_id);
		return 1;
	}

	/* Deinit */
	iic_info.iic_op = AVA4_IIC_DEINIT;
	rlen = 0;
	avalon4_auc_init_pkg(wbuf, &iic_info, NULL, 0, rlen);

	memset(rbuf, 0, AVA4_AUC_P_SIZE);
	err = avalon4_auc_xfer(avalon4, wbuf, AVA4_AUC_P_SIZE, &wlen, rbuf, rlen, &rlen);
	if (err) {
		applog(LOG_ERR, "%s-%d: Failed to deinit Avalon USB2IIC Converter", avalon4->drv->name, avalon4->device_id);
		return 1;
	}

	/* Init */
	iic_info.iic_op = AVA4_IIC_INIT;
	iic_info.iic_param.aucParam[0] = opt_avalon4_aucspeed;
	iic_info.iic_param.aucParam[1] = opt_avalon4_aucxdelay;
	rlen = AVA4_AUC_VER_LEN;
	avalon4_auc_init_pkg(wbuf, &iic_info, NULL, 0, rlen);

	memset(rbuf, 0, AVA4_AUC_P_SIZE);
	err = avalon4_auc_xfer(avalon4, wbuf, AVA4_AUC_P_SIZE, &wlen, rbuf, rlen, &rlen);
	if (err) {
		applog(LOG_ERR, "%s-%d: Failed to init Avalon USB2IIC Converter", avalon4->drv->name, avalon4->device_id);
		return 1;
	}

	hexdump(rbuf, AVA4_AUC_P_SIZE);

	memcpy(ver, rbuf + 4, AVA4_AUC_VER_LEN);
	ver[AVA4_AUC_VER_LEN] = '\0';

	applog(LOG_DEBUG, "%s-%d: USB2IIC Converter version: %s!", avalon4->drv->name, avalon4->device_id, ver);
	return 0;
}

static int avalon4_auc_getinfo(struct cgpu_info *avalon4)
{
	struct avalon4_iic_info iic_info;
	int err, wlen, rlen;
	uint8_t wbuf[AVA4_AUC_P_SIZE];
	uint8_t rbuf[AVA4_AUC_P_SIZE];
	uint8_t *pdata = rbuf + 4;
	uint16_t adc_val;
	struct avalon4_info *info = avalon4->device_data;

	iic_info.iic_op = AVA4_IIC_INFO;
	/* Device info: (9 bytes)
	 * tempadc(2), reqRdIndex, reqWrIndex,
	 * respRdIndex, respWrIndex, tx_flags, state
	 * */
	rlen = 7;
	avalon4_auc_init_pkg(wbuf, &iic_info, NULL, 0, rlen);

	memset(rbuf, 0, AVA4_AUC_P_SIZE);
	err = avalon4_auc_xfer(avalon4, wbuf, AVA4_AUC_P_SIZE, &wlen, rbuf, rlen, &rlen);
	if (err) {
		applog(LOG_ERR, "%s-%d: AUC Failed to get info ", avalon4->drv->name, avalon4->device_id);
		return 1;
	}

	applog(LOG_DEBUG, "%s-%d: AUC tempADC(%03d), reqcnt(%d), respcnt(%d), txflag(%d), state(%d)",
			avalon4->drv->name, avalon4->device_id,
			pdata[1] << 8 | pdata[0],
			pdata[2],
			pdata[3],
			pdata[5] << 8 | pdata[4],
			pdata[6]);

	adc_val = pdata[1] << 8 | pdata[0];

	info->auc_temp = 3.3 * adc_val * 10000 / 1023;
	return 0;
}

static int avalon4_iic_xfer_pkg(struct cgpu_info *avalon4, uint8_t slave_addr,
				const struct avalon4_pkg *pkg, struct avalon4_ret *ret)
{
	struct avalon4_iic_info iic_info;
	int err, wcnt, rcnt, rlen = 0;
	uint8_t wbuf[AVA4_AUC_P_SIZE];
	uint8_t rbuf[AVA4_AUC_P_SIZE];

	struct avalon4_info *info = avalon4->device_data;

	if (ret)
		rlen = AVA4_READ_SIZE;

	if (info->connecter == AVA4_CONNECTER_AUC) {
		if (unlikely(avalon4->usbinfo.nodev))
			return AVA4_SEND_ERROR;

		iic_info.iic_op = AVA4_IIC_XFER;
		iic_info.iic_param.slave_addr = slave_addr;

		avalon4_auc_init_pkg(wbuf, &iic_info, (uint8_t *)pkg, AVA4_WRITE_SIZE, rlen);
		err = avalon4_auc_xfer(avalon4, wbuf, wbuf[0], &wcnt, rbuf, rlen, &rcnt);
		if ((pkg->type != AVA4_P_DETECT) && err == -7 && !rcnt && rlen) {
			avalon4_auc_init_pkg(wbuf, &iic_info, NULL, 0, rlen);
			err = avalon4_auc_xfer(avalon4, wbuf, wbuf[0], &wcnt, rbuf, rlen, &rcnt);
			applog(LOG_DEBUG, "%s-%d-%d: AUC read again!(type:0x%x, err:%d)", avalon4->drv->name, avalon4->device_id, slave_addr, pkg->type, err);
		}
		if (err || rcnt != rlen) {
			if (info->xfer_err_cnt++ == 100) {
				applog(LOG_DEBUG, "%s-%d-%d: AUC xfer_err_cnt reach err = %d, rcnt = %d, rlen = %d",
						avalon4->drv->name, avalon4->device_id, slave_addr,
						err, rcnt, rlen);

				cgsleep_ms(5 * 1000); /* Wait MM reset */
				avalon4_auc_init(avalon4, info->auc_version);
			}
			return AVA4_SEND_ERROR;
		}

		if (ret)
			memcpy((char *)ret, rbuf + 4, AVA4_READ_SIZE);

		info->xfer_err_cnt = 0;
	}

	if (info->connecter == AVA4_CONNECTER_IIC) {
		err = avalon4_iic_xfer(avalon4, slave_addr, (uint8_t *)pkg, AVA4_WRITE_SIZE, (uint8_t *)ret, AVA4_READ_SIZE);
		if ((pkg->type != AVA4_P_DETECT) && err) {
			err = avalon4_iic_xfer(avalon4, slave_addr, (uint8_t *)pkg, AVA4_WRITE_SIZE, (uint8_t *)ret, AVA4_READ_SIZE);
			applog(LOG_DEBUG, "%s-%d-%d: IIC read again!(type:0x%x, err:%d)", avalon4->drv->name, avalon4->device_id, slave_addr, pkg->type, err);
		}
		if (err) {
			/* FIXME: Don't care broadcast message with no reply, or it will block other thread when called by avalon4_send_bc_pkgs */
			if ((pkg->type != AVA4_P_DETECT) && (slave_addr == AVA4_MODULE_BROADCAST))
				return AVA4_SEND_OK;

			if (info->xfer_err_cnt++ == 100) {
				info->xfer_err_cnt = 0;
				applog(LOG_DEBUG, "%s-%d-%d: IIC xfer_err_cnt reach err = %d, rcnt = %d, rlen = %d",
						avalon4->drv->name, avalon4->device_id, slave_addr,
						err, rcnt, rlen);

				cgsleep_ms(5 * 1000); /* Wait MM reset */
			}
			return AVA4_SEND_ERROR;
		}

		info->xfer_err_cnt = 0;
	}
	return AVA4_SEND_OK;
}

static int avalon4_send_bc_pkgs(struct cgpu_info *avalon4, const struct avalon4_pkg *pkg)
{
	int ret;

	do {
		ret = avalon4_iic_xfer_pkg(avalon4, AVA4_MODULE_BROADCAST, pkg, NULL);
	} while (ret != AVA4_SEND_OK);

	return 0;
}

static void avalon4_stratum_pkgs(struct cgpu_info *avalon4, struct pool *pool)
{
	struct avalon4_info *info = avalon4->device_data;
	const int merkle_offset = 36;
	struct avalon4_pkg pkg;
	int i, a, b, tmp;
	unsigned char target[32];
	int job_id_len, n2size;
	unsigned short crc;

	int coinbase_len_posthash, coinbase_len_prehash;
	uint8_t coinbase_prehash[32];

	/* Send out the first stratum message STATIC */
	applog(LOG_DEBUG, "%s-%d: Pool stratum message STATIC: %d, %d, %d, %d, %d",
	       avalon4->drv->name, avalon4->device_id,
	       pool->coinbase_len,
	       pool->nonce2_offset,
	       pool->n2size,
	       merkle_offset,
	       pool->merkles);
	memset(pkg.data, 0, AVA4_P_DATA_LEN);
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

	tmp = be32toh((int)pool->swork.diff);
	memcpy(pkg.data + 20, &tmp, 4);

	tmp = be32toh((int)pool->pool_no);
	memcpy(pkg.data + 24, &tmp, 4);

	avalon4_init_pkg(&pkg, AVA4_P_STATIC, 1, 1);
	if (avalon4_send_bc_pkgs(avalon4, &pkg))
		return;

	if (pool->sdiff <= AVA4_DRV_DIFFMAX)
		set_target(target, pool->sdiff);
	else
		set_target(target, AVA4_DRV_DIFFMAX);

	memcpy(pkg.data, target, 32);
	if (opt_debug) {
		char *target_str;
		target_str = bin2hex(target, 32);
		applog(LOG_DEBUG, "%s-%d: Pool stratum target: %s", avalon4->drv->name, avalon4->device_id, target_str);
		free(target_str);
	}
	avalon4_init_pkg(&pkg, AVA4_P_TARGET, 1, 1);
	if (avalon4_send_bc_pkgs(avalon4, &pkg))
		return;

	memset(pkg.data, 0, AVA4_P_DATA_LEN);

	job_id_len = strlen(pool->swork.job_id);
	crc = crc16((unsigned char *)pool->swork.job_id, job_id_len);
	applog(LOG_DEBUG, "%s-%d: Pool stratum message JOBS_ID[%04x]: %s",
	       avalon4->drv->name, avalon4->device_id,
	       crc, pool->swork.job_id);

	pkg.data[0] = (crc & 0xff00) >> 8;
	pkg.data[1] = crc & 0x00ff;
	avalon4_init_pkg(&pkg, AVA4_P_JOB_ID, 1, 1);
	if (avalon4_send_bc_pkgs(avalon4, &pkg))
		return;

	coinbase_len_prehash = pool->nonce2_offset - (pool->nonce2_offset % SHA256_BLOCK_SIZE);
	coinbase_len_posthash = pool->coinbase_len - coinbase_len_prehash;
	sha256_prehash(pool->coinbase, coinbase_len_prehash, coinbase_prehash);

	a = (coinbase_len_posthash / AVA4_P_DATA_LEN) + 1;
	b = coinbase_len_posthash % AVA4_P_DATA_LEN;
	memcpy(pkg.data, coinbase_prehash, 32);
	avalon4_init_pkg(&pkg, AVA4_P_COINBASE, 1, a + (b ? 1 : 0));
	if (avalon4_send_bc_pkgs(avalon4, &pkg))
		return;
	applog(LOG_DEBUG, "%s-%d: Pool stratum message modified COINBASE: %d %d",
			avalon4->drv->name, avalon4->device_id,
			a, b);
	for (i = 1; i < a; i++) {
		memcpy(pkg.data, pool->coinbase + coinbase_len_prehash + i * 32 - 32, 32);
		avalon4_init_pkg(&pkg, AVA4_P_COINBASE, i + 1, a + (b ? 1 : 0));
		if (avalon4_send_bc_pkgs(avalon4, &pkg))
			return;
	}
	if (b) {
		memset(pkg.data, 0, AVA4_P_DATA_LEN);
		memcpy(pkg.data, pool->coinbase + coinbase_len_prehash + i * 32 - 32, b);
		avalon4_init_pkg(&pkg, AVA4_P_COINBASE, i + 1, i + 1);
		if (avalon4_send_bc_pkgs(avalon4, &pkg))
			return;
	}

	b = pool->merkles;
	applog(LOG_DEBUG, "%s-%d: Pool stratum message MERKLES: %d", avalon4->drv->name, avalon4->device_id, b);
	for (i = 0; i < b; i++) {
		memset(pkg.data, 0, AVA4_P_DATA_LEN);
		memcpy(pkg.data, pool->swork.merkle_bin[i], 32);
		avalon4_init_pkg(&pkg, AVA4_P_MERKLES, i + 1, b);
		if (avalon4_send_bc_pkgs(avalon4, &pkg))
			return;
	}

	applog(LOG_DEBUG, "%s-%d: Pool stratum message HEADER: 4", avalon4->drv->name, avalon4->device_id);
	for (i = 0; i < 4; i++) {
		memset(pkg.data, 0, AVA4_P_DATA_LEN);
		memcpy(pkg.data, pool->header_bin + i * 32, 32);
		avalon4_init_pkg(&pkg, AVA4_P_HEADER, i + 1, 4);
		if (avalon4_send_bc_pkgs(avalon4, &pkg))
			return;
	}

	if (info->connecter == AVA4_CONNECTER_AUC)
		avalon4_auc_getinfo(avalon4);
}

static struct cgpu_info *avalon4_iic_detect(void)
{
	int i;
	struct avalon4_info *info;
	struct cgpu_info *avalon4 = NULL;
	struct i2c_ctx *i2c_slave = NULL;

	i2c_slave = i2c_slave_open(I2C_BUS, 0);
	if (!i2c_slave) {
		applog(LOG_ERR, "Avalon4 init iic failed\n");
		return NULL;
	}

	i2c_slave->exit(i2c_slave);
	i2c_slave = NULL;

	avalon4 = cgcalloc(1, sizeof(*avalon4));
	avalon4->drv = &avalon4_drv;
	avalon4->deven = DEV_ENABLED;
	avalon4->threads = 1;
	add_cgpu(avalon4);

	applog(LOG_INFO, "%s-%d: Found at %s", avalon4->drv->name, avalon4->device_id,
	       I2C_BUS);

	avalon4->device_data = cgcalloc(sizeof(struct avalon4_info), 1);
	info = avalon4->device_data;

	info->polling_first = 1;
	info->newnonce = 0;

	for (i = 0; i < AVA4_DEFAULT_MODULARS; i++) {
		info->enable[i] = 0;
		info->mod_type[i] = AVA4_TYPE_NULL;
		info->fan_pct[i] = AVA4_DEFAULT_FAN_START;
		info->set_voltage[i] = opt_avalon4_voltage_min;
		memcpy(info->set_smart_frequency[i], opt_avalon4_freq, sizeof(opt_avalon4_freq));
		memcpy(info->set_frequency[i], opt_avalon4_freq, sizeof(opt_avalon4_freq));
		info->i2c_slaves[i] = i2c_slave_open(I2C_BUS, i);
		if (!info->i2c_slaves[i]) {
			applog(LOG_ERR, "Avalon4 init i2c slaves failed\n");
			free(avalon4->device_data);
			avalon4->device_data = NULL;
			free(avalon4);
			avalon4 = NULL;
			return NULL;
		}
	}

	info->enable[0] = 1;
	info->mod_type[0] = AVA4_TYPE_MM40;
	info->temp[0] = -273;

	info->speed_bingo[0] = opt_avalon4_speed_bingo;
	info->speed_error[0] = opt_avalon4_speed_error;
	info->connecter = AVA4_CONNECTER_IIC;

	return avalon4;
}

static struct cgpu_info *avalon4_auc_detect(struct libusb_device *dev, struct usb_find_devices *found)
{
	int i;
	struct avalon4_info *info;
	struct cgpu_info *avalon4 = usb_alloc_cgpu(&avalon4_drv, 1);
	char auc_ver[AVA4_AUC_VER_LEN];

	if (!usb_init(avalon4, dev, found)) {
		applog(LOG_ERR, "Avalon4 failed usb_init");
		avalon4 = usb_free_cgpu(avalon4);
		return NULL;
	}

	/* Avalon4 prefers not to use zero length packets */
	avalon4->nozlp = true;

	/* We try twice on AUC init */
	if (avalon4_auc_init(avalon4, auc_ver) && avalon4_auc_init(avalon4, auc_ver))
		return NULL;

	/* We have an Avalon4 AUC connected */
	avalon4->threads = 1;
	add_cgpu(avalon4);

	update_usb_stats(avalon4);
	applog(LOG_INFO, "%s-%d: Found at %s", avalon4->drv->name, avalon4->device_id,
	       avalon4->device_path);

	avalon4->device_data = cgcalloc(sizeof(struct avalon4_info), 1);
	info = avalon4->device_data;
	memcpy(info->auc_version, auc_ver, AVA4_AUC_VER_LEN);
	info->auc_version[AVA4_AUC_VER_LEN] = '\0';
	info->auc_speed = opt_avalon4_aucspeed;
	info->auc_xdelay = opt_avalon4_aucxdelay;

	info->polling_first = 1;
	info->newnonce = 0;

	for (i = 0; i < AVA4_DEFAULT_MODULARS; i++) {
		info->enable[i] = 0;
		info->mod_type[i] = AVA4_TYPE_NULL;
		info->fan_pct[i] = AVA4_DEFAULT_FAN_START;
		info->set_voltage[i] = opt_avalon4_voltage_min;
		memcpy(info->set_smart_frequency[i], opt_avalon4_freq, sizeof(opt_avalon4_freq));
		memcpy(info->set_frequency[i], opt_avalon4_freq, sizeof(opt_avalon4_freq));
	}

	info->enable[0] = 1;
	info->mod_type[0] = AVA4_TYPE_MM40;
	info->temp[0] = -273;

	info->speed_bingo[0] = opt_avalon4_speed_bingo;
	info->speed_error[0] = opt_avalon4_speed_error;
	info->connecter = AVA4_CONNECTER_AUC;
	return avalon4;
}

static inline void avalon4_detect(bool __maybe_unused hotplug)
{
	usb_detect(&avalon4_drv, avalon4_auc_detect);
	if (!hotplug && opt_avalon4_iic_detect)
		avalon4_iic_detect();
}

static bool avalon4_prepare(struct thr_info *thr)
{
	int i;
	struct cgpu_info *avalon4 = thr->cgpu;
	struct avalon4_info *info = avalon4->device_data;

	info->polling_first = 1;

	memset(&(info->firsthash), 0, sizeof(info->firsthash));
	cgtime(&(info->last_fan));
	cgtime(&(info->last_30s));
	cgtime(&(info->last_5s));
	cgtime(&info->last_stratum);
	cgtime(&info->last_fadj);
	cgtime(&info->last_tcheck);

	cglock_init(&info->update_lock);
	cglock_init(&info->pool0.data_lock);
	cglock_init(&info->pool1.data_lock);
	cglock_init(&info->pool2.data_lock);

	for (i = 0; i < AVA4_DEFAULT_MODULARS; i++)
		info->fan_pct[i] = AVA4_DEFAULT_FAN_START;

	switch (opt_avalon4_miningmode) {
		case AVA4_MOD_ECO:
			opt_avalon4_fan_min = 20;
			opt_avalon4_overheat = 60;
			break;
		case AVA4_MOD_NORMAL:
			opt_avalon4_fan_min = 30;
			opt_avalon4_overheat = 50;
			break;
		case AVA4_MOD_TURBO:
			opt_avalon4_fan_min = 60;
			opt_avalon4_overheat = 49;
			break;
		default:
			break;
	}

	return true;
}

static int check_module_exits(struct cgpu_info *avalon4, uint8_t mm_dna[AVA4_MM_DNA_LEN + 1])
{
	struct avalon4_info *info = avalon4->device_data;
	int i;

	for (i = 0; i < AVA4_DEFAULT_MODULARS; i++) {
		if (info->enable[i]) {
			/* last byte is \0 */
			if (!memcmp(info->mm_dna[i], mm_dna, AVA4_MM_DNA_LEN))
				return 1;
		}
	}

	return 0;
}

static void detect_modules(struct cgpu_info *avalon4)
{
	struct avalon4_info *info = avalon4->device_data;
	struct thr_info *thr = avalon4->thr[0];

	struct avalon4_pkg send_pkg;
	struct avalon4_ret ret_pkg;
	uint32_t tmp;
	int i, j, k, err;

	/* Detect new modules here */
	for (i = 1; i < AVA4_DEFAULT_MODULARS; i++) {
		if (info->enable[i])
			continue;

		/* Send out detect pkg */
		applog(LOG_DEBUG, "%s-%d: AVA4_P_DETECT ID[%d]",
		       avalon4->drv->name, avalon4->device_id, i);
		memset(send_pkg.data, 0, AVA4_P_DATA_LEN);
		tmp = be32toh(opt_avalon4_freq_min);
		memcpy(send_pkg.data, &tmp, 4);
		tmp = be32toh(opt_avalon4_freq_max);
		memcpy(send_pkg.data + 4, &tmp, 4);
		send_pkg.data[8] = opt_avalon4_ntcb >> 8;
		send_pkg.data[9] = opt_avalon4_ntcb & 0xff;
		tmp = be32toh(i); /* ID */
		memcpy(send_pkg.data + 28, &tmp, 4);
		avalon4_init_pkg(&send_pkg, AVA4_P_DETECT, 1, 1);
		err = avalon4_iic_xfer_pkg(avalon4, AVA4_MODULE_BROADCAST, &send_pkg, &ret_pkg);
		if (err == AVA4_SEND_OK) {
			if (decode_pkg(thr, &ret_pkg, AVA4_MODULE_BROADCAST)) {
				applog(LOG_DEBUG, "%s-%d: Should be AVA4_P_ACKDETECT(%d), but %d",
				       avalon4->drv->name, avalon4->device_id, AVA4_P_ACKDETECT, ret_pkg.type);
				continue;
			}
		}

		if (err != AVA4_SEND_OK) {
			applog(LOG_DEBUG, "%s-%d: AVA4_P_DETECT: Failed AUC xfer data with err %d",
					avalon4->drv->name, avalon4->device_id, err);
			break;
		}

		applog(LOG_DEBUG, "%s-%d: Module detect ID[%d]: %d",
		       avalon4->drv->name, avalon4->device_id, i, ret_pkg.type);
		if (ret_pkg.type != AVA4_P_ACKDETECT)
			break;

		if (check_module_exits(avalon4, ret_pkg.data))
			continue;

		cgtime(&info->elapsed[i]);
		cgtime(&info->last_finc[i]);
		cgtime(&info->last_fdec[i]);
		cgtime(&info->last_favg[i]);
		info->enable[i] = 1;
		memcpy(info->mm_dna[i], ret_pkg.data, AVA4_MM_DNA_LEN);
		info->mm_dna[i][AVA4_MM_DNA_LEN] = '\0';
		memcpy(info->mm_version[i], ret_pkg.data + AVA4_MM_DNA_LEN, AVA4_MM_VER_LEN);
		memcpy(&tmp, ret_pkg.data + AVA4_MM_DNA_LEN + AVA4_MM_VER_LEN, 4);
		tmp = be32toh(tmp);
		info->mm_version[i][AVA4_MM_VER_LEN] = '\0';
		info->miner_count[i] = AVA4_DEFAULT_MINER_CNT;
		info->asic_count[i] = AVA4_DEFAULT_ASIC_CNT;
		info->total_asics[i] = tmp;
		info->autov[i] = opt_avalon4_autov;
		info->toverheat[i] = opt_avalon4_overheat;
		if (info->toverheat[i] > AVA4_MM40_TEMP_OVERHEAT)
			info->toverheat[i] = AVA4_MM40_TEMP_OVERHEAT;
		info->temp_target[i] = opt_avalon4_temp_target;
		if (info->temp_target[i] > AVA4_MM40_TEMP_TARGET)
			info->temp_target[i] = AVA4_MM40_TEMP_TARGET;

		if (!strncmp((char *)&(info->mm_version[i]), AVA4_MM40_PREFIXSTR, 2))
			info->mod_type[i] = AVA4_TYPE_MM40;
		if (!strncmp((char *)&(info->mm_version[i]), AVA4_MM41_PREFIXSTR, 2))
			info->mod_type[i] = AVA4_TYPE_MM41;
		if (!strncmp((char *)&(info->mm_version[i]), AVA4_MM50_PREFIXSTR, 2)) {
			info->miner_count[i] = AVA4_MM50_MINER_CNT;
			info->asic_count[i] = AVA4_MM50_ASIC_CNT;
			if (opt_avalon4_autov)
				applog(LOG_NOTICE, "%s-%d-%d: Module do not support autov",
				       avalon4->drv->name, avalon4->device_id, i);
			info->autov[i] = false;
			info->mod_type[i] = AVA4_TYPE_MM50;
		}
		if (!strncmp((char *)&(info->mm_version[i]), AVA4_MM60_PREFIXSTR, 2)) {
			info->miner_count[i] = AVA4_MM60_MINER_CNT;
			info->asic_count[i] = AVA4_MM60_ASIC_CNT;
			if (opt_avalon4_autov)
				applog(LOG_NOTICE, "%s-%d-%d: Module do not support autov",
				       avalon4->drv->name, avalon4->device_id, i);
			info->autov[i] = false;
			info->toverheat[i] = opt_avalon4_overheat;
			if (info->toverheat[i] > AVA4_DEFAULT_TEMP_OVERHEAT)
				info->toverheat[i] = AVA4_DEFAULT_TEMP_OVERHEAT;

			info->temp_target[i] = opt_avalon4_temp_target;
			if (info->temp_target[i] > AVA4_DEFAULT_TEMP_TARGET)
				info->temp_target[i] = AVA4_DEFAULT_TEMP_TARGET;
			info->mod_type[i] = AVA4_TYPE_MM60;
		}
		info->ntime_offset[i] = (opt_avalon4_ntime_offset > info->asic_count[i]) ? info->asic_count[i] : opt_avalon4_ntime_offset;
		info->fan_pct[i] = AVA4_DEFAULT_FAN_START;
		info->set_voltage[i] = opt_avalon4_voltage_min;
		for (j = 0; j < info->miner_count[i]; j++) {
			info->set_voltage_i[i][j] = opt_avalon4_voltage_min;
			info->set_voltage_offset[i][j] = 0;
			info->adjflag[i][j] = 0;
			memset(info->ma_sum[i][j], 0, sizeof(uint8_t) * info->asic_count[i]);

			for (k = 0; k < info->asic_count[i]; k++)
				memset(info->set_frequency_i[i][j][k], 0, sizeof(int) * 3);
		}
		info->led_red[i] = 0;
		for (j = 0; j < AVA4_DEFAULT_ADC_MAX; j++)
			info->adc[i][j] = AVA4_ADC_MAX;
		memset(info->pll_sel, 0, sizeof(info->pll_sel));
		info->saved[i] = 0;
		info->cutoff[i] = 0;
		info->get_frequency[i] = 0;
		memcpy(info->set_smart_frequency[i], opt_avalon4_freq, sizeof(opt_avalon4_freq));
		info->speed_bingo[i] = opt_avalon4_speed_bingo;
		info->speed_error[i] = opt_avalon4_speed_error;
		info->freq_mode[i] = AVA4_FREQ_INIT_MODE;
		applog(LOG_NOTICE, "%s-%d: New module detect! ID[%d]",
		       avalon4->drv->name, avalon4->device_id, i);

		if (opt_avalon4_miningmode != AVA4_MOD_CUSTOM) {
			applog(LOG_DEBUG, "%s-%d-%d: Load mm config",
					avalon4->drv->name, avalon4->device_id, i);

			memset(send_pkg.data, 0, AVA4_P_DATA_LEN);
			avalon4_init_pkg(&send_pkg, AVA4_P_GET_VOLT, 1, 1);
			send_pkg.opt = ((1 << 4) | opt_avalon4_miningmode);
			err = avalon4_iic_xfer_pkg(avalon4, i, &send_pkg, &ret_pkg);
			if (err == AVA4_SEND_OK) {
				err = decode_pkg(thr, &ret_pkg, i);
				if (err == 0 && ret_pkg.type == AVA4_P_STATUS_VOLT) {
					applog(LOG_DEBUG, "%s-%d-%d: Load mm config success",
							avalon4->drv->name, avalon4->device_id, i);

					for (j = 0; j < info->miner_count[i]; j++) {
						applog(LOG_DEBUG, "%s-%d-%d: vol-%d = %d",
							avalon4->drv->name, avalon4->device_id, i,
							j, info->get_voltage_i[i][j]);

						info->set_voltage_i[i][j] = info->get_voltage_i[i][j];
					}
				} else {
					applog(LOG_DEBUG, "%s-%d-%d: Load mm config invalid! err = %d, ret_pkg.type 0x%x",
							avalon4->drv->name, avalon4->device_id, i,
							err, ret_pkg.type);
				}
			} else {
				applog(LOG_DEBUG, "%s-%d-%d: Load mm config failed!",
						avalon4->drv->name, avalon4->device_id, i);
			}
		}
	}
}

static void detach_module(struct cgpu_info *avalon4, int addr)
{
	struct avalon4_info *info = avalon4->device_data;
	int i, j;

	info->polling_err_cnt[addr] = 0;
	info->mod_type[addr] = AVA4_TYPE_NULL;
	info->enable[addr] = 0;
	info->get_voltage[addr] = 0;
	info->get_frequency[addr] = 0;
	info->power_good[addr] = 0;
	info->error_code[addr] = 0;
	info->local_work[addr] = 0;
	info->local_works[addr] = 0;
	info->hw_work[addr] = 0;
	info->hw_works[addr] = 0;
	info->total_asics[addr] = 0;
	info->toverheat[addr] = opt_avalon4_overheat;
	info->temp_target[addr] = opt_avalon4_temp_target;
	info->speed_bingo[addr] = opt_avalon4_speed_bingo;
	info->speed_error[addr] = opt_avalon4_speed_error;
	memset(info->set_frequency[addr], 0, sizeof(int) * 3);
	for (i = 0; i < AVA4_DEFAULT_ADJ_TIMES; i++) {
		info->lw5[addr][i] = 0;
		info->hw5[addr][i] = 0;
	}

	for (i = 0; i < info->miner_count[addr]; i++) {
		info->matching_work[addr][i] = 0;
		memset(info->chipmatching_work[addr][i], 0, sizeof(int) * info->asic_count[addr]);
		info->local_works_i[addr][i] = 0;
		info->hw_works_i[addr][i] = 0;
		memset(info->lw5_i[addr][i], 0, AVA4_DEFAULT_ADJ_TIMES * sizeof(uint32_t));
		memset(info->hw5_i[addr][i], 0, AVA4_DEFAULT_ADJ_TIMES * sizeof(uint32_t));
		memset(info->ma_sum[addr][i], 0, sizeof(uint8_t) * info->asic_count[addr]);
		for (j = 0; j < info->asic_count[addr]; j++)
			memset(info->set_frequency_i[addr][i][j], 0, sizeof(int) * 3);
	}
	info->freq_mode[addr] = AVA4_FREQ_INIT_MODE;
	applog(LOG_NOTICE, "%s-%d: Module detached! ID[%d]",
			avalon4->drv->name, avalon4->device_id, addr);
}

static int polling(struct cgpu_info *avalon4)
{
	struct avalon4_info *info = avalon4->device_data;
	struct thr_info *thr = avalon4->thr[0];
	struct avalon4_pkg send_pkg;
	struct avalon4_ret ar;
	int i, tmp, ret, decode_err = 0, do_polling = 0;
	struct timeval current_fan;
	int do_adjust_fan = 0;
	uint32_t fan_pwm;
	double device_tdiff;

	if (info->polling_first) {
		cgsleep_ms(600);
		info->polling_first = 0;
	}

	cgtime(&current_fan);
	device_tdiff = tdiff(&current_fan, &(info->last_fan));
	if (device_tdiff > 2.0 || device_tdiff < 0) {
		cgtime(&info->last_fan);
		do_adjust_fan = 1;
	}

	for (i = 1; i < AVA4_DEFAULT_MODULARS; i++) {
		if (!info->enable[i])
			continue;

		do_polling = 1;
		cgsleep_ms(opt_avalon4_polling_delay);

		memset(send_pkg.data, 0, AVA4_P_DATA_LEN);
		/* Red LED */
		tmp = be32toh(info->led_red[i]);
		memcpy(send_pkg.data, &tmp, 4);

		/* Adjust fan every 10 seconds*/
		if (do_adjust_fan) {
			fan_pwm = adjust_fan(info, i);
			fan_pwm |= 0x80000000;
			tmp = be32toh(fan_pwm);
			memcpy(send_pkg.data + 4, &tmp, 4);
		}

		avalon4_init_pkg(&send_pkg, AVA4_P_POLLING, 1, 1);
		ret = avalon4_iic_xfer_pkg(avalon4, i, &send_pkg, &ar);
		if (ret == AVA4_SEND_OK)
			decode_err = decode_pkg(thr, &ar, i);

		if (ret != AVA4_SEND_OK || decode_err) {
			info->polling_err_cnt[i]++;
			memset(send_pkg.data, 0, AVA4_P_DATA_LEN);
			avalon4_init_pkg(&send_pkg, AVA4_P_RSTMMTX, 1, 1);
			avalon4_iic_xfer_pkg(avalon4, i, &send_pkg, NULL);
			if (info->polling_err_cnt[i] >= 4)
				detach_module(avalon4, i);
		}

		if (ret == AVA4_SEND_OK && !decode_err) {
			info->polling_err_cnt[i] = 0;

			if (info->mm_dna[i][AVA4_MM_DNA_LEN - 1] != ar.opt) {
				applog(LOG_ERR, "%s-%d-%d: Dup address found %d-%d",
						avalon4->drv->name, avalon4->device_id, i,
						info->mm_dna[i][AVA4_MM_DNA_LEN - 1], ar.opt);
				hexdump((uint8_t *)&ar, sizeof(ar));
				detach_module(avalon4, i);
			}
		}
	}

	if (!do_polling)
		detect_modules(avalon4);

	return 0;
}

static void copy_pool_stratum(struct pool *pool_stratum, struct pool *pool)
{
	int i;
	int merkles = pool->merkles, job_id_len;
	size_t coinbase_len = pool->coinbase_len;
	unsigned short crc;

	if (!pool->swork.job_id)
		return;

	if (pool_stratum->swork.job_id) {
		job_id_len = strlen(pool->swork.job_id);
		crc = crc16((unsigned char *)pool->swork.job_id, job_id_len);
		job_id_len = strlen(pool_stratum->swork.job_id);

		if (crc16((unsigned char *)pool_stratum->swork.job_id, job_id_len) == crc)
			return;
	}

	cg_wlock(&pool_stratum->data_lock);
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
	cg_wunlock(&pool_stratum->data_lock);
}

static inline int mm_cmp_1512(struct avalon4_info *info, int addr)
{
	/* >= 1512 return 1 */
	return strncmp(info->mm_version[addr] + 2, "1512", 4) >= 0 ? 1 : 0;
}

static inline int mm_cmp_1501(struct avalon4_info *info, int addr)
{
	/* >= 1501 return 1 */
	return strncmp(info->mm_version[addr] + 2, "1501", 4) >= 0 ? 1 : 0;
}

static inline int mm_cmp_d17f4a(struct avalon4_info *info, int addr)
{
	/* == d17f4a return 1 */
	return strncmp(info->mm_version[addr] + 7, "d17f4a", 6) == 0 ? 1 : 0;
}

static void avalon4_set_voltage(struct cgpu_info *avalon4, int addr, int opt)
{
	struct avalon4_info *info = avalon4->device_data;
	struct avalon4_pkg send_pkg;
	uint16_t tmp;
	uint8_t i;

	memset(send_pkg.data, 0, AVA4_P_DATA_LEN);

	/* Use shifter to set voltage */
	for (i = 0; i < info->miner_count[addr]; i++) {
		tmp = info->set_voltage_i[addr][i] + info->set_voltage_offset[addr][i];
		if (avalon4_freezsafemode)
			tmp = AVA4_FREEZESAFE_VOLTAGE;

		if (info->cutoff[addr])
			tmp = 0;

		if (info->mod_type[addr] == AVA4_TYPE_MM40)
			tmp = encode_voltage_adp3208d(tmp);
		if (info->mod_type[addr] == AVA4_TYPE_MM41)
			tmp = encode_voltage_ncp5392p(tmp);
		if (info->mod_type[addr] == AVA4_TYPE_MM50)
			tmp = encode_voltage_ncp5392p(tmp);
		/* TODO: fix it with the actual board */
		if (info->mod_type[addr] == AVA4_TYPE_MM60)
			tmp = encode_voltage_ncp5392p(tmp);

		tmp = htobe16(tmp);
		memcpy(send_pkg.data + 2 * ((4 + i / 5 * 5) - i + (i / 5 * 5)), &tmp, 2);
	}

	/* Package the data */
	avalon4_init_pkg(&send_pkg, AVA4_P_SET_VOLT, 1, 1);
	send_pkg.opt = opt;
	if (addr == AVA4_MODULE_BROADCAST)
		avalon4_send_bc_pkgs(avalon4, &send_pkg);
	else
		avalon4_iic_xfer_pkg(avalon4, addr, &send_pkg, NULL);
}

static uint32_t avalon4_get_cpm(unsigned int freq)
{
	unsigned int i;

	for (i = 0; i < sizeof(g_freq_array) / sizeof(g_freq_array[0]); i++)
		if (freq >= g_freq_array[i][0] && freq < g_freq_array[i+1][0])
			return g_freq_array[i][1];

	/* return the lowest freq if not found */
	return g_freq_array[0][1];
}

static void avalon4_set_freq(struct cgpu_info *avalon4, int addr, uint8_t miner_id, uint8_t chip_id, unsigned int freq[])
{
	struct avalon4_info *info = avalon4->device_data;
	struct avalon4_pkg send_pkg;
	uint32_t tmp;
	uint8_t set = 0;
	int i, j;

	/* Note: 0 (miner_id and chip_id) is reserved for all devices */
	if (!miner_id || !chip_id) {
		if (memcmp(freq, info->set_frequency[addr], sizeof(int) * 3) || !info->get_frequency[addr]) {
			memcpy(info->set_frequency[addr], freq, sizeof(int) * 3);
			for (i = 0; i < info->miner_count[addr]; i++) {
				for (j = 0; j < info->asic_count[addr]; j++)
					memcpy(info->set_frequency_i[addr][i][j], info->set_frequency[addr], sizeof(int) * 3);
			}
			set = 1;
		}
	} else {
		if (memcmp(freq, info->set_frequency_i[addr][miner_id - 1][chip_id - 1], sizeof(int) * 3)) {
			memcpy(info->set_frequency_i[addr][miner_id - 1][chip_id - 1], freq, sizeof(int) * 3);
			set = 1;
		}
	}

	if (avalon4_freezsafemode) {
		info->set_frequency[addr][0] = info->set_frequency[addr][1] = info->set_frequency[addr][2] = AVA4_FREEZESAFE_FREQUENCY;
		memcpy(freq, info->set_frequency[addr], sizeof(int) * 3);
		miner_id = 0;
		chip_id = 0;
		set = 1;
	}

	if (info->cutoff[addr]) {
		info->set_frequency[addr][0] = AVA4_DEFAULT_FREQUENCY_MIN;
		info->set_frequency[addr][1] = AVA4_DEFAULT_FREQUENCY_MIN;
		info->set_frequency[addr][2] = AVA4_DEFAULT_FREQUENCY_MIN;
		memcpy(freq, info->set_frequency[addr], sizeof(int) * 3);
		miner_id = 0;
		chip_id = 0;
		set = 1;
	}

	if (!set)
		return;

	memset(send_pkg.data, 0, AVA4_P_DATA_LEN);
	if (info->mod_type[addr] == AVA4_TYPE_MM60) {
		tmp = be32toh(freq[0]);
		memcpy(send_pkg.data, &tmp, 4);
		tmp = be32toh(freq[1]);
		memcpy(send_pkg.data + 4, &tmp, 4);
		tmp = be32toh(freq[2]);
		memcpy(send_pkg.data + 8, &tmp, 4);
	} else {
		tmp = avalon4_get_cpm(freq[0]);
		tmp = be32toh(tmp);
		memcpy(send_pkg.data, &tmp, 4);
		tmp = avalon4_get_cpm(freq[1]);
		tmp = be32toh(tmp);
		memcpy(send_pkg.data + 4, &tmp, 4);
		tmp = avalon4_get_cpm(freq[2]);
		tmp = be32toh(tmp);
		memcpy(send_pkg.data + 8, &tmp, 4);
	}
	applog(LOG_DEBUG, "%s-%d-%d: avalon4 set freq (%d-%d-%d)",
			avalon4->drv->name, avalon4->device_id, addr,
			freq[0],
			freq[1],
			freq[2]);
	send_pkg.data[12] = miner_id;

	/* Package the data */
	avalon4_init_pkg(&send_pkg, AVA4_P_SET_FREQ, 1, 1);
	send_pkg.opt = chip_id;

	if (addr == AVA4_MODULE_BROADCAST)
		avalon4_send_bc_pkgs(avalon4, &send_pkg);
	else
		avalon4_iic_xfer_pkg(avalon4, addr, &send_pkg, NULL);
}

static void avalon4_stratum_set(struct cgpu_info *avalon4, struct pool *pool, int addr)
{
	struct avalon4_info *info = avalon4->device_data;
	struct avalon4_pkg send_pkg;
	uint32_t tmp = 0, range, start, volt;

	/* Set the NTime, Voltage and Frequency */
	memset(send_pkg.data, 0, AVA4_P_DATA_LEN);

	if (info->ntime_offset[addr] != info->asic_count[addr]) {
		tmp = info->ntime_offset[addr] | 0x80000000;
		tmp = be32toh(tmp);
		memcpy(send_pkg.data, &tmp, 4);
	}

	volt = info->set_voltage[addr];
	if (avalon4_freezsafemode)
		volt = AVA4_FREEZESAFE_VOLTAGE;

	if (info->cutoff[addr])
		volt = 0;

	if (info->mod_type[addr] == AVA4_TYPE_MM40)
		tmp = encode_voltage_adp3208d(volt);
	if (info->mod_type[addr] == AVA4_TYPE_MM41)
		tmp = encode_voltage_ncp5392p(volt);
	if (info->mod_type[addr] == AVA4_TYPE_MM50)
		tmp = encode_voltage_ncp5392p(volt);
	/* TODO: fix it with the actual board */
	if (info->mod_type[addr] == AVA4_TYPE_MM60)
		tmp = encode_voltage_ncp5392p(volt);
	tmp = be32toh(tmp);
	memcpy(send_pkg.data + 4, &tmp, 4);

	tmp = info->set_frequency[addr][0] | (info->set_frequency[addr][1] << 10) | (info->set_frequency[addr][2] << 20);
	if (avalon4_freezsafemode)
		tmp = AVA4_FREEZESAFE_FREQUENCY | (AVA4_FREEZESAFE_FREQUENCY << 10) | (AVA4_FREEZESAFE_FREQUENCY << 20);

	if (info->cutoff[addr])
		tmp = AVA4_DEFAULT_FREQUENCY_MIN | (AVA4_DEFAULT_FREQUENCY_MIN << 10) | (AVA4_DEFAULT_FREQUENCY_MIN << 20);

	tmp = be32toh(tmp);
	memcpy(send_pkg.data + 8, &tmp, 4);

	/* Configure the nonce2 offset and range */
	if (pool->n2size == 3)
		range = 0xffffff / (total_devices ? total_devices : 1);
	else
		range = 0xffffffff / (total_devices ? total_devices : 1);
	start = range * avalon4->device_id;

	tmp = be32toh(start);
	memcpy(send_pkg.data + 12, &tmp, 4);

	tmp = be32toh(range);
	memcpy(send_pkg.data + 16, &tmp, 4);

	/* adjust flag [0-5]: reserved, 6: nonce check, 7: autof*/
	tmp = 1;
	if (!opt_avalon4_smart_speed)
		tmp = 0;
	if (opt_avalon4_noncecheck)
		tmp |= 2;
	send_pkg.data[20] = tmp & 0xff;
	send_pkg.data[21] = info->speed_bingo[addr];
	send_pkg.data[22] = info->speed_error[addr];

	/* Package the data */
	avalon4_init_pkg(&send_pkg, AVA4_P_SET, 1, 1);
	if (addr == AVA4_MODULE_BROADCAST)
		avalon4_send_bc_pkgs(avalon4, &send_pkg);
	else
		avalon4_iic_xfer_pkg(avalon4, addr, &send_pkg, NULL);
}

static void avalon4_stratum_finish(struct cgpu_info *avalon4)
{
	struct avalon4_pkg send_pkg;

	memset(send_pkg.data, 0, AVA4_P_DATA_LEN);
	avalon4_init_pkg(&send_pkg, AVA4_P_FINISH, 1, 1);
	avalon4_send_bc_pkgs(avalon4, &send_pkg);
}

static void avalon4_adjust_vf(struct cgpu_info *avalon4, int addr, uint8_t save)
{
	struct avalon4_info *info = avalon4->device_data;

	if (info->mod_type[addr] == AVA4_TYPE_MM50) {
		avalon4_set_voltage(avalon4, addr, ((save << 4) | opt_avalon4_miningmode));
		avalon4_set_freq(avalon4, addr, 0, 0, opt_avalon4_freq);
	}

	if ((info->mod_type[addr] == AVA4_TYPE_MM41) &&
			mm_cmp_1501(info, addr)) {
		avalon4_set_voltage(avalon4, addr, ((save << 4) | opt_avalon4_miningmode));
		avalon4_set_freq(avalon4, addr, 0, 0, opt_avalon4_freq);
	}

	if ((info->mod_type[addr] == AVA4_TYPE_MM40) &&
			mm_cmp_1501(info, addr)) {
		if (!mm_cmp_d17f4a(info, addr)) {
			avalon4_set_voltage(avalon4, addr, ((save << 4) | opt_avalon4_miningmode));
			avalon4_set_freq(avalon4, addr, 0, 0, opt_avalon4_freq);
		}
	}
}

static void avalon4_freq_inc(struct cgpu_info *avalon4, int addr, unsigned int freq[], unsigned int val)
{
	struct avalon4_info *info = avalon4->device_data;
	int i;

	if (info->mod_type[addr] == AVA4_TYPE_MM60) {
		for (i = 0; i < 3; i++) {
		if ((freq[i] + val) < AVA4_MM60_FREQUENCY_MAX)
			freq[i] += val;
		else
			freq[i] = AVA4_MM60_FREQUENCY_MAX;
		}
	}
}

static void avalon4_freq_dec(struct cgpu_info *avalon4, int addr, unsigned int freq[], unsigned int val)
{
	struct avalon4_info *info = avalon4->device_data;
	int i;

	if (info->mod_type[addr] == AVA4_TYPE_MM60) {
		for (i = 0; i < 3; i++) {
			if (freq[i] <= val) {
				freq[i] = AVA4_DEFAULT_FREQUENCY_MIN;
				continue;
			}

			if ((freq[i] - val) >= AVA4_DEFAULT_FREQUENCY_MIN)
				freq[i] -= val;
			else
				freq[i] = AVA4_DEFAULT_FREQUENCY_MIN;
		}
	}
}

static void avalon4_update(struct cgpu_info *avalon4)
{
	struct avalon4_info *info = avalon4->device_data;
	struct thr_info *thr = avalon4->thr[0];
	struct work *work;
	struct pool *pool;
	int coinbase_len_posthash, coinbase_len_prehash;
	int i;
	int max_temp;

	applog(LOG_DEBUG, "%s-%d: New stratum: restart: %d, update: %d",
	       avalon4->drv->name, avalon4->device_id,
	       thr->work_restart, thr->work_update);
	thr->work_update = false;
	thr->work_restart = false;

	/* Step 1: Make sure pool is ready */
	work = get_work(thr, thr->id);
	discard_work(work); /* Don't leak memory */

	/* Step 2: MM protocol check */
	pool = current_pool();
	if (!pool->has_stratum)
		quit(1, "%s-%d: MM has to use stratum pools", avalon4->drv->name, avalon4->device_id);

	coinbase_len_prehash = pool->nonce2_offset - (pool->nonce2_offset % SHA256_BLOCK_SIZE);
	coinbase_len_posthash = pool->coinbase_len - coinbase_len_prehash;

	if (coinbase_len_posthash + SHA256_BLOCK_SIZE > AVA4_P_COINBASE_SIZE) {
		applog(LOG_ERR, "%s-%d: MM pool modified coinbase length(%d) is more than %d",
		       avalon4->drv->name, avalon4->device_id,
		       coinbase_len_posthash + SHA256_BLOCK_SIZE, AVA4_P_COINBASE_SIZE);
		return;
	}
	if (pool->merkles > AVA4_P_MERKLES_COUNT) {
		applog(LOG_ERR, "%s-%d: MM merkles has to be less then %d", avalon4->drv->name, avalon4->device_id, AVA4_P_MERKLES_COUNT);
		return;
	}
	if (pool->n2size < 3) {
		applog(LOG_ERR, "%s-%d: MM nonce2 size has to be >= 3 (%d)", avalon4->drv->name, avalon4->device_id, pool->n2size);
		return;
	}

	cg_wlock(&info->update_lock);
	/* Step 3: Try to detect new modules */
	detect_modules(avalon4);

	/* Step 4: Send out stratum pkgs */
	cg_rlock(&pool->data_lock);
	cgtime(&info->last_stratum);
	info->pool_no = pool->pool_no;
	copy_pool_stratum(&info->pool2, &info->pool1);
	copy_pool_stratum(&info->pool1, &info->pool0);
	copy_pool_stratum(&info->pool0, pool);

	avalon4_stratum_pkgs(avalon4, pool);
	cg_runlock(&pool->data_lock);

	/* Step 5: Configure the parameter from outside */
	for (i = 1; i < AVA4_DEFAULT_MODULARS; i++) {
		if (!info->enable[i])
			continue;

		max_temp = get_temp_max(info, i);
		if (max_temp >= info->toverheat[i])
			info->cutoff[i] = 1;

		if (info->cutoff[i] && (max_temp <= (info->toverheat[i] - 10)))
			info->cutoff[i] = 0;

		if (info->cutoff[i])
			info->polling_first = 1;

		if (info->mod_type[i] == AVA4_TYPE_MM60) {
			switch (info->freq_mode[i]) {
				case AVA4_FREQ_INIT_MODE:
					memcpy(info->set_frequency[i], opt_avalon4_freq, sizeof(opt_avalon4_freq));
					memcpy(info->set_smart_frequency[i], info->set_frequency[i], sizeof(info->set_frequency[i]));
					if (info->cutoff[i]) {
						info->set_frequency[i][0] = AVA4_DEFAULT_FREQUENCY_MIN;
						info->set_frequency[i][1] = AVA4_DEFAULT_FREQUENCY_MIN;
						info->set_frequency[i][2] = AVA4_DEFAULT_FREQUENCY_MIN;
						info->freq_mode[i] = AVA4_FREQ_CUTOFF_MODE;
						break;
					}
					if (mm_cmp_1512(info, i) && (opt_avalon4_smart_speed != AVA4_DEFAULT_SMARTSPEED_OFF))
						info->freq_mode[i] = AVA4_FREQ_PLLADJ_MODE;
					break;
				case AVA4_FREQ_CUTOFF_MODE:
					info->set_frequency[i][0] = AVA4_DEFAULT_FREQUENCY_MIN;
					info->set_frequency[i][1] = AVA4_DEFAULT_FREQUENCY_MIN;
					info->set_frequency[i][2] = AVA4_DEFAULT_FREQUENCY_MIN;
					if (!info->cutoff[i]) {
						memcpy(info->set_frequency[i], opt_avalon4_freq, sizeof(opt_avalon4_freq));
						memcpy(info->set_smart_frequency[i], info->set_frequency[i], sizeof(info->set_frequency[i]));
						info->freq_mode[i] = AVA4_FREQ_INIT_MODE;
					}
					break;
				case AVA4_FREQ_TEMPADJ_MODE:
					if (info->cutoff[i]) {
						info->set_frequency[i][0] = AVA4_DEFAULT_FREQUENCY_MIN;
						info->set_frequency[i][1] = AVA4_DEFAULT_FREQUENCY_MIN;
						info->set_frequency[i][2] = AVA4_DEFAULT_FREQUENCY_MIN;
						info->freq_mode[i] = AVA4_FREQ_CUTOFF_MODE;
						break;
					}

					if (get_temp_max(info, i) <= (info->temp_target[i] - opt_avalon4_delta_temp)) {
						memcpy(info->set_frequency[i], opt_avalon4_freq, sizeof(opt_avalon4_freq));
						memcpy(info->set_smart_frequency[i], info->set_frequency[i], sizeof(info->set_frequency[i]));
						info->freq_mode[i] = AVA4_FREQ_INIT_MODE;
						break;
					}
					/* Adjust frequency when scanhash */
					break;
				case AVA4_FREQ_PLLADJ_MODE:
					if (info->cutoff[i]) {
						info->set_frequency[i][0] = AVA4_DEFAULT_FREQUENCY_MIN;
						info->set_frequency[i][1] = AVA4_DEFAULT_FREQUENCY_MIN;
						info->set_frequency[i][2] = AVA4_DEFAULT_FREQUENCY_MIN;
						info->freq_mode[i] = AVA4_FREQ_CUTOFF_MODE;
						break;
					}

					break;
				default:
					applog(LOG_ERR, "%s-%d-%d: Invalid frequency mode %d",
							avalon4->drv->name, avalon4->device_id, i, info->freq_mode[i]);
					break;
			}
			avalon4_stratum_set(avalon4, pool, i);
		} else {
			avalon4_stratum_set(avalon4, pool, i);
			avalon4_adjust_vf(avalon4, i, 0);
		}
	}

	/* Step 6: Send out finish pkg */
	avalon4_stratum_finish(avalon4);
	cg_wunlock(&info->update_lock);
}

static int64_t avalon4_scanhash(struct thr_info *thr)
{
	struct cgpu_info *avalon4 = thr->cgpu;
	struct avalon4_info *info = avalon4->device_data;
	struct timeval current;
	double device_tdiff, hwp;
	uint32_t a = 0, b = 0;
	int64_t h;
	int i, j, k, count = 0;
	uint32_t tmp;
	int max_temp;

	if ((info->connecter == AVA4_CONNECTER_AUC) &&
		(unlikely(avalon4->usbinfo.nodev))) {
		applog(LOG_ERR, "%s-%d: Device disappeared, shutting down thread",
				avalon4->drv->name, avalon4->device_id);
		return -1;
	}

	/* Step 1: Stop polling the device if there is no stratum in 3 minutes, network is down */
	cgtime(&current);
	avalon4_freezsafemode = 0;
	if (tdiff(&current, &(info->last_stratum)) > 180.0) {
		if(!opt_avalon4_freezesafe)
			return 0;

		if(opt_avalon4_freezesafe)
			avalon4_freezsafemode = 1;
	}

	/* Step 2: Polling  */
	cg_rlock(&info->update_lock);
	polling(avalon4);
	cg_runlock(&info->update_lock);

	/* Step 3: Adjust voltage */
	cgtime(&current);
	device_tdiff = tdiff(&current, &(info->last_5s));
	if (device_tdiff >= 5.0 || device_tdiff < 0) {
		copy_time(&info->last_5s, &current);
		if (++info->i_5s >= AVA4_DEFAULT_ADJ_TIMES)
			info->i_5s = 0;

		for (i = 1; i < AVA4_DEFAULT_MODULARS; i++) {
			if (!info->enable[i])
				continue;

			info->lw5[i][info->i_5s] = 0;
			info->hw5[i][info->i_5s] = 0;

			for(j = 0; j < info->miner_count[i]; j++) {
				info->lw5_i[i][j][info->i_5s] = 0;
				info->hw5_i[i][j][info->i_5s] = 0;
			}
		}
	}

	cgtime(&current);
	device_tdiff = tdiff(&current, &(info->last_30s));
	if (opt_avalon4_autov && (device_tdiff > 30.0 || device_tdiff < 0)) {
		copy_time(&info->last_30s, &current);

		for (i = 1; i < AVA4_DEFAULT_MODULARS; i++) {
			uint8_t individual = 0;

			if (!info->enable[i])
				continue;

			if (!info->autov[i])
				continue;

			if (info->mod_type[i] == AVA4_TYPE_MM60)
				continue;

			if (info->mod_type[i] == AVA4_TYPE_MM50)
				individual = 1;

			if ((info->mod_type[i] == AVA4_TYPE_MM41) && mm_cmp_1501(info, i))
				individual = 1;

			if ((info->mod_type[i] == AVA4_TYPE_MM40) && mm_cmp_1501(info, i)) {
				if (!mm_cmp_d17f4a(info, i))
					individual = 1;
			}

			if (info->cutoff[i]) {
				for (j = 0; j < info->miner_count[i]; j++)
					info->adjflag[i][j] = 0;
				continue;
			}

			if (!individual) {
				a = 0;
				b = 0;
				for (j = 0; j < AVA4_DEFAULT_ADJ_TIMES; j++) {
					a += info->lw5[i][j];
					b += info->hw5[i][j];
				}

				hwp = a ? (double)b / (double)a : 0;
				if (hwp > AVA4_DH_INC && (info->set_voltage[i] < info->set_voltage[0] + 125)) {
					info->set_voltage[i] += 125;
					for (j = 0; j < info->miner_count[i]; j++) {
						info->set_voltage_i[i][j] += 125;
					}
					info->adjflag[i][0] = 1;
					applog(LOG_NOTICE, "%s-%d: Automatic increase module[%d] voltage to %d",
							avalon4->drv->name, avalon4->device_id, i, info->set_voltage[i]);
				}
				if (!info->adjflag[i][0] && hwp < AVA4_DH_DEC && (info->set_voltage[i] > info->set_voltage[0] - (4 * 125))) {
					info->set_voltage[i] -= 125;
					for (j = 0; j < info->miner_count[i]; j++) {
						info->set_voltage_i[i][j] -= 125;
					}

					applog(LOG_NOTICE, "%s-%d: Automatic decrease module[%d] voltage to %d",
							avalon4->drv->name, avalon4->device_id, i, info->set_voltage[i]);
				}
			} else {
				for (j = 0; j < info->miner_count[i]; j++) {
					a = 0;
					b = 0;

					for (k = 0; k < AVA4_DEFAULT_ADJ_TIMES; k++) {
						a += info->lw5_i[i][j][k];
						b += info->hw5_i[i][j][k];
					}

					hwp = a ? (double)b / (double)a : 0;
					if (hwp > AVA4_DH_INC && (info->set_voltage_i[i][j] < info->set_voltage[0] + (2 * 125))) {
						//FIX ME: How to deal with set_voltage ?
						info->set_voltage_i[i][j] += 125;
						info->adjflag[i][j] = 1;
						applog(LOG_NOTICE, "%s-%d: Automatic increase module[%d-%d] voltage to %d",
							avalon4->drv->name, avalon4->device_id, i, j, info->set_voltage_i[i][j]);

					}
					if (!info->adjflag[i][j] && hwp < AVA4_DH_DEC && (info->set_voltage_i[i][j] > info->set_voltage[0] - (12 * 125))) {
						//FIX ME: How to deal with set_voltage ?
						info->set_voltage_i[i][j] -= 125;
						applog(LOG_NOTICE, "%s-%d: Automatic decrease module[%d-%d] voltage to %d",
								avalon4->drv->name, avalon4->device_id, i, j, info->set_voltage_i[i][j]);
					}
				}
			}

			/* Save config when run 10m */
			cgtime(&current);
			device_tdiff = tdiff(&current, &(info->elapsed[i]));
			if (device_tdiff >= 600.0) {
				if (!info->saved[i]) {
					applog(LOG_NOTICE, "%s-%d-%d: Avalon4 saved voltage !",
						avalon4->drv->name, avalon4->device_id, i);
					avalon4_adjust_vf(avalon4, i, 1);
					info->saved[i] = 1;
				} else
					avalon4_adjust_vf(avalon4, i, 0);

			} else
				avalon4_adjust_vf(avalon4, i, 0);

			if (((int)device_tdiff % 3600) >= 0 || ((int)device_tdiff % 3600) < 3) {
				for (j = 0; j < info->miner_count[i]; j++)
					info->adjflag[i][j] = 0;
			}
		}
	}

	/* Step 4: Adjust frequency */
	cgtime(&current);
	device_tdiff = tdiff(&current, &(info->last_tcheck));
	if (device_tdiff > 3.0 || device_tdiff < 0) {
		copy_time(&info->last_tcheck, &current);
		for (i = 1; i < AVA4_DEFAULT_MODULARS; i++) {
			if (!info->enable[i])
				continue;

			if (info->mod_type[i] == AVA4_TYPE_MM60) {
				max_temp = get_temp_max(info, i);
				if (info->freq_mode[i] != AVA4_FREQ_TEMPADJ_MODE) {
					if (max_temp >= opt_avalon4_freqadj_temp) {
						info->last_maxtemp[i] = max_temp;
						cg_wlock(&info->update_lock);
						avalon4_freq_dec(avalon4, i, info->set_smart_frequency[i], opt_avalon4_delta_freq + 50);
						avalon4_set_freq(avalon4, i, 0, 0, info->set_smart_frequency[i]);
						applog(LOG_DEBUG, "%s-%d-%d: set freq after temp check (%d-%d-%d)",
								avalon4->drv->name, avalon4->device_id, i,
								info->set_smart_frequency[i][0],
								info->set_smart_frequency[i][1],
								info->set_smart_frequency[i][2]);
						info->freq_mode[i] = AVA4_FREQ_TEMPADJ_MODE;
						/* Update time for frequency adjustment */
						copy_time(&info->last_fadj, &current);
						cg_wunlock(&info->update_lock);
					}
				}
			}
		}
	}

	device_tdiff = tdiff(&current, &(info->last_fadj));
	if (device_tdiff > opt_avalon4_freqadj_time || device_tdiff < 0) {
		copy_time(&info->last_fadj, &current);
		for (i = 1; i < AVA4_DEFAULT_MODULARS; i++) {
			if (!info->enable[i])
				continue;

			if (info->mod_type[i] == AVA4_TYPE_MM60) {
				switch (info->freq_mode[i]) {
					case AVA4_FREQ_TEMPADJ_MODE:
						if (info->cutoff[i])
							break;

						max_temp = get_temp_max(info, i);
						if (max_temp <= info->temp_target[i]) {
							applog(LOG_DEBUG, "AVA4_FREQ_TEMPADJ_MODE -> AVA4_FREQ_INIT_MODE");
							break;
						}
						/* if max_temp goes down ,then we don't need adjust frequency */
						if (info->last_maxtemp[i] > max_temp) {
							applog(LOG_DEBUG, "AVA4_FREQ_TEMPADJ_MODE temp goes down");
							info->last_maxtemp[i] = get_temp_max(info, i);
							break;
						}

						info->last_maxtemp[i] = get_temp_max(info, i);
						cg_wlock(&info->update_lock);
						avalon4_freq_dec(avalon4, i, info->set_smart_frequency[i], opt_avalon4_delta_freq);
						avalon4_set_freq(avalon4, i, 0, 0, info->set_smart_frequency[i]);
						applog(LOG_DEBUG, "%s-%d-%d: update freq (%d-%d-%d) AVA4_FREQ_PLLADJ_MODE",
								avalon4->drv->name, avalon4->device_id, i,
								info->set_smart_frequency[i][0],
								info->set_smart_frequency[i][1],
								info->set_smart_frequency[i][2]);
						cg_wunlock(&info->update_lock);
						break;
					case AVA4_FREQ_PLLADJ_MODE:
						/* AVA4_DEFAULT_SMARTSPEED_MODE1: auto speed by A3218 chips */
						cgtime(&current);
						if (opt_avalon4_smart_speed == AVA4_DEFAULT_SMARTSPEED_MODE2) {
							device_tdiff = tdiff(&current, &(info->last_fdec[i]));
							if ((device_tdiff >= AVA4_DEFAULT_FDEC_TIME) ||
									(device_tdiff < 0)) {
								copy_time(&info->last_fdec[i], &current);
								if ((opt_avalon4_least_pll_check && (info->pll_sel[i][0] >= opt_avalon4_least_pll_check)) ||
										(opt_avalon4_most_pll_check && (info->pll_sel[i][AVA4_DEFAULT_PLL_MAX - 1] <= opt_avalon4_most_pll_check)))
									avalon4_freq_dec(avalon4, i, info->set_smart_frequency[i], 25);
							}

							device_tdiff = tdiff(&current, &(info->last_finc[i]));
							if ((device_tdiff >= AVA4_DEFAULT_FINC_TIME) ||
									(device_tdiff < 0)) {
								copy_time(&info->last_finc[i], &current);
								if ((opt_avalon4_least_pll_check && (info->pll_sel[i][0] < opt_avalon4_least_pll_check)) ||
										(opt_avalon4_most_pll_check && (info->pll_sel[i][AVA4_DEFAULT_PLL_MAX - 1] > opt_avalon4_most_pll_check)))
									avalon4_freq_inc(avalon4, i, info->set_smart_frequency[i], 25);
							}
						}

						if (opt_avalon4_smart_speed == AVA4_DEFAULT_SMARTSPEED_MODE3) {
							device_tdiff = tdiff(&current, &(info->last_favg[i]));
							if ((device_tdiff >= AVA4_DEFAULT_FAVG_TIME) ||
									(device_tdiff < 0)) {
								copy_time(&info->last_favg[i], &current);
								tmp = (info->get_frequency[i] / 96);
								tmp = (uint32_t)ceil(tmp / 25.0) * 25 + 25;
								if (tmp < AVA4_DEFAULT_FREQUENCY_MIN)
									tmp = AVA4_DEFAULT_FREQUENCY_MIN;
								if (tmp > AVA4_MM60_FREQUENCY_MAX)
									tmp = AVA4_MM60_FREQUENCY_MAX;
								info->set_smart_frequency[i][0] = info->set_smart_frequency[i][1] = info->set_smart_frequency[i][2] = tmp;
							}
						}
						cg_wlock(&info->update_lock);
						avalon4_set_freq(avalon4, i, 0, 0, info->set_smart_frequency[i]);
						applog(LOG_DEBUG, "%s-%d-%d: update freq (%d-%d-%d) AVA4_FREQ_PLLADJ_MODE",
								avalon4->drv->name, avalon4->device_id, i,
								info->set_smart_frequency[i][0],
								info->set_smart_frequency[i][1],
								info->set_smart_frequency[i][2]);
						cg_wunlock(&info->update_lock);
						break;
					default:
						break;
				}
			}
		}
	}

	/* Step 5: Calculate mm count and hash */
	h = 0;
	for (i = 1; i < AVA4_DEFAULT_MODULARS; i++) {
		if (info->enable[i]) {
			count++;
			if (info->local_work[i] > info->hw_work[i]) {
				if (info->mod_type[i] == AVA4_TYPE_MM60) {
					h += avalon4->diff1 - info->newnonce;
					info->newnonce = avalon4->diff1;
				} else {
					h += (info->local_work[i] - info->hw_work[i]);
					info->local_work[i] = 0;
					info->hw_work[i] = 0;
				}
			}
		}
	}
	info->mm_count = count;

	if (h && !info->firsthash.tv_sec) {
		cgtime(&info->firsthash);
		copy_time(&(avalon4->dev_start_tv), &(info->firsthash));
	}

	return h * 0xffffffffull;
}

#define STATBUFLEN (6 * 1024)
static struct api_data *avalon4_api_stats(struct cgpu_info *cgpu)
{
	struct api_data *root = NULL;
	struct avalon4_info *info = cgpu->device_data;
	int i, j, k;
	uint32_t a, b, lw5_i[AVA4_DEFAULT_MINER_MAX], hw5_i[AVA4_DEFAULT_MINER_MAX];
	double hwp, diff;
	char buf[256];
	char statbuf[AVA4_DEFAULT_MODULARS][STATBUFLEN];
	struct timeval current;
	bool has_a6 = false;

	memset(statbuf, 0, AVA4_DEFAULT_MODULARS * STATBUFLEN);

	for (i = 1; i < AVA4_DEFAULT_MODULARS; i++) {
		if (info->mod_type[i] == AVA4_TYPE_NULL)
			continue;

		if (info->mod_type[i] == AVA4_TYPE_MM60)
			has_a6 = true;

		sprintf(buf, "Ver[%s]", info->mm_version[i]);
		strcat(statbuf[i], buf);
	}
	for (i = 1; i < AVA4_DEFAULT_MODULARS; i++) {
		if (info->mod_type[i] == AVA4_TYPE_NULL)
			continue;

		sprintf(buf, " DNA[%02x%02x%02x%02x%02x%02x%02x%02x]",
				info->mm_dna[i][0],
				info->mm_dna[i][1],
				info->mm_dna[i][2],
				info->mm_dna[i][3],
				info->mm_dna[i][4],
				info->mm_dna[i][5],
				info->mm_dna[i][6],
				info->mm_dna[i][7]);
		strcat(statbuf[i], buf);
	}
	for (i = 1; i < AVA4_DEFAULT_MODULARS; i++) {
		struct timeval now;
		if (info->mod_type[i] == AVA4_TYPE_NULL)
			continue;

		cgtime(&now);
		sprintf(buf, " Elapsed[%.0f]", tdiff(&now, &(info->elapsed[i])));
		strcat(statbuf[i], buf);
	}
	for (i = 1; i < AVA4_DEFAULT_MODULARS; i++) {
		uint8_t show = 0;

		if (info->mod_type[i] == AVA4_TYPE_NULL)
			continue;

		if (info->mod_type[i] == AVA4_TYPE_MM60)
			show = 1;

		if (info->mod_type[i] == AVA4_TYPE_MM50)
			show = 1;

		if ((info->mod_type[i] == AVA4_TYPE_MM41) && mm_cmp_1501(info, i))
			show = 1;

		if ((info->mod_type[i] == AVA4_TYPE_MM40) && mm_cmp_1501(info, i)) {
			if (!mm_cmp_d17f4a(info, i))
				show = 1;
		}

		strcat(statbuf[i], " MW[");
		for (j = 0; j < info->miner_count[i]; j++) {
			if (show)
				sprintf(buf, "%"PRIu64" ", info->local_works_i[i][j]);
			else
				sprintf(buf, "%d ", info->matching_work[i][j]);

			strcat(statbuf[i], buf);
		}
		statbuf[i][strlen(statbuf[i]) - 1] = ']';
	}
	for (i = 1; i < AVA4_DEFAULT_MODULARS; i++) {
		if (info->mod_type[i] == AVA4_TYPE_NULL)
			continue;

		sprintf(buf, " LW[%"PRIu64"]", info->local_works[i]);
		strcat(statbuf[i], buf);
	}
	for (i = 1; i < AVA4_DEFAULT_MODULARS; i++) {
		uint8_t show = 0;

		if (info->mod_type[i] == AVA4_TYPE_NULL)
			continue;

		if (info->mod_type[i] == AVA4_TYPE_MM60)
			show = 1;

		if (info->mod_type[i] == AVA4_TYPE_MM50)
			show = 1;

		if ((info->mod_type[i] == AVA4_TYPE_MM41) && mm_cmp_1501(info, i))
			show = 1;

		if ((info->mod_type[i] == AVA4_TYPE_MM40) && mm_cmp_1501(info, i)) {
			if (!mm_cmp_d17f4a(info, i))
				show = 1;
		}

		if (show) {
			strcat(statbuf[i], " MH[");
			for (j = 0; j < info->miner_count[i]; j++) {
				sprintf(buf, "%"PRIu64" ", info->hw_works_i[i][j]);
				strcat(statbuf[i], buf);
			}
			statbuf[i][strlen(statbuf[i]) - 1] = ']';
		}
	}
	for (i = 1; i < AVA4_DEFAULT_MODULARS; i++) {
		if (info->mod_type[i] == AVA4_TYPE_NULL)
			continue;

		sprintf(buf, " HW[%"PRIu64"]", info->hw_works[i]);
		strcat(statbuf[i], buf);
	}
	for (i = 1; i < AVA4_DEFAULT_MODULARS; i++) {
		if (info->mod_type[i] == AVA4_TYPE_NULL)
			continue;

		a = info->hw_works[i];
		b = info->local_works[i];
		hwp = b ? ((double)a / (double)b) * 100: 0;

		sprintf(buf, " DH[%.3f%%]", hwp);
		strcat(statbuf[i], buf);
	}
	for (i = 1; i < AVA4_DEFAULT_MODULARS; i++) {
		uint8_t show = 0;

		if (info->mod_type[i] == AVA4_TYPE_NULL)
			continue;

		a = 0;
		b = 0;
		memset(lw5_i, 0, info->miner_count[i] * sizeof(uint32_t));
		memset(hw5_i, 0, info->miner_count[i] * sizeof(uint32_t));

		for (j = 0; j < AVA4_DEFAULT_ADJ_TIMES; j++) {
			a += info->lw5[i][j];
			b += info->hw5[i][j];

			for (k = 0; k < info->miner_count[i]; k++) {
				lw5_i[k] += info->lw5_i[i][k][j];
				hw5_i[k] += info->hw5_i[i][k][j];
			}
		}

		cgtime(&current);
		diff = tdiff(&current, &(info->last_5s)) + 25.0;

		hwp = a ? (double)b / (double)a * 100 : 0;

		if (info->mod_type[i] == AVA4_TYPE_MM50)
			show = 1;

		if ((info->mod_type[i] == AVA4_TYPE_MM41) && mm_cmp_1501(info, i))
			show = 1;

		if ((info->mod_type[i] == AVA4_TYPE_MM40) && mm_cmp_1501(info, i)) {
			if (!mm_cmp_d17f4a(info, i))
				show = 1;
		}

		sprintf(buf, " GHS5m[%.2f] DH5m[%.3f%%]", ((double)a - (double)b) * 4.295 / diff, hwp);
		strcat(statbuf[i], buf);

		if (opt_debug && show) {
			strcat(statbuf[i], " MDH5m[");
			for (k = 0; k < info->miner_count[i]; k++) {
				hwp = lw5_i[k] ? (double)hw5_i[k] / (double)lw5_i[k] * 100 : 0;
				sprintf(buf, "%.3f%% ", hwp);
				strcat(statbuf[i], buf);
			}
			statbuf[i][strlen(statbuf[i]) - 1] = ']';
		}
	}
	for (i = 1; i < AVA4_DEFAULT_MODULARS; i++) {
		if (info->mod_type[i] == AVA4_TYPE_NULL)
			continue;

		if (info->mod_type[i] == AVA4_TYPE_MM60) {
			sprintf(buf, " Temp[%d] Temp0[%d] Temp1[%d]",
					info->temp[i],
					(int)convert_temp(info->adc[i][0]),
					(int)convert_temp(info->adc[i][1]));
		} else
			sprintf(buf, " Temp[%d]", info->temp[i]);

		strcat(statbuf[i], buf);
	}
	for (i = 1; i < AVA4_DEFAULT_MODULARS; i++) {
		if (info->mod_type[i] == AVA4_TYPE_NULL)
			continue;

		sprintf(buf, " Fan[%d]", info->fan[i]);
		strcat(statbuf[i], buf);
	}
	for (i = 1; i < AVA4_DEFAULT_MODULARS; i++) {
		if (info->mod_type[i] == AVA4_TYPE_NULL)
			continue;
		if (info->mod_type[i] == AVA4_TYPE_MM60)
			sprintf(buf, " Vol[%.1f]", convert_voltage(info->adc[i][4], 1 / 11.0));
		else
			sprintf(buf, " Vol[%.4f]", (float)info->get_voltage[i] / 10000);
		strcat(statbuf[i], buf);
	}
	for (i = 1; i < AVA4_DEFAULT_MODULARS; i++) {
		uint8_t show = 0;
		int32_t vref = 0;

		if (info->mod_type[i] == AVA4_TYPE_NULL)
			continue;

		if (info->mod_type[i] == AVA4_TYPE_MM50)
			show = 1;

		if ((info->mod_type[i] == AVA4_TYPE_MM41) && mm_cmp_1501(info, i))
			show = 1;

		if ((info->mod_type[i] == AVA4_TYPE_MM40) && mm_cmp_1501(info, i)) {
			if (!mm_cmp_d17f4a(info, i))
				show = 1;
		}

		if (opt_debug && show) {
			strcat(statbuf[i], " MVol[");
			for (j = 0; j < info->miner_count[i]; j++) {
				sprintf(buf, "%.4f ", (float)info->set_voltage_i[i][j] / 10000);
				vref += ((info->set_voltage_i[i][j] - opt_avalon4_voltage_min) / 125);
				strcat(statbuf[i], buf);
			}
			statbuf[i][strlen(statbuf[i]) - 1] = ']';

			strcat(statbuf[i], " VREF[");
			sprintf(buf, "%d ", vref);
			strcat(statbuf[i], buf);
			statbuf[i][strlen(statbuf[i]) - 1] = ']';
		}
	}
	for (i = 1; i < AVA4_DEFAULT_MODULARS; i++) {
		if (info->mod_type[i] == AVA4_TYPE_NULL)
			continue;

		if (info->mod_type[i] == AVA4_TYPE_MM60)
			sprintf(buf, " GHSmm[%.2f] Freq[%.2f]", (float)info->get_frequency[i] / 1000 * info->total_asics[i], (float)info->get_frequency[i] / 1000);
		else
			sprintf(buf, " Freq[%.2f]", (float)info->get_frequency[i] / 1000);
		strcat(statbuf[i], buf);
	}
	if (opt_debug) {
		for (i = 1; i < AVA4_DEFAULT_MODULARS; i++) {
			if (info->mod_type[i] == AVA4_TYPE_NULL)
				continue;

			if (info->mod_type[i] == AVA4_TYPE_MM50) {
				for (j = 0; j < info->miner_count[i]; j++) {
					sprintf(buf, " MFreq%d[", j);
					strcat(statbuf[i], buf);
					for (k = 0; k < info->asic_count[i]; k++) {
						sprintf(buf, "%d %d %d ",
								info->set_frequency_i[i][j][k][0],
								info->set_frequency_i[i][j][k][1],
								info->set_frequency_i[i][j][k][2]);
						strcat(statbuf[i], buf);
					}
					statbuf[i][strlen(statbuf[i]) - 1] = ']';
				}
			}
		}
	}
	for (i = 1; i < AVA4_DEFAULT_MODULARS; i++) {
		if (info->mod_type[i] == AVA4_TYPE_NULL)
			continue;

		sprintf(buf, " PG[%d]", info->power_good[i]);
		strcat(statbuf[i], buf);
	}
	for (i = 1; i < AVA4_DEFAULT_MODULARS; i++) {
		if (info->mod_type[i] == AVA4_TYPE_NULL)
			continue;

		sprintf(buf, " Led[%d]", info->led_red[i]);
		strcat(statbuf[i], buf);
	}
	for (i = 1; i < AVA4_DEFAULT_MODULARS; i++) {
		if (info->mod_type[i] == AVA4_TYPE_NULL)
			continue;

		if (info->mod_type[i] == AVA4_TYPE_MM50 || info->mod_type[i] == AVA4_TYPE_MM60) {
			for (j = 0; j < info->miner_count[i]; j++) {
				sprintf(buf, " MW%d[", j);
				strcat(statbuf[i], buf);
				for (k = 0; k < info->asic_count[i]; k++) {
					sprintf(buf, "%d ", info->chipmatching_work[i][j][k]);
					strcat(statbuf[i], buf);
				}

				statbuf[i][strlen(statbuf[i]) - 1] = ']';
			}
		}
	}
	for (i = 1; i < AVA4_DEFAULT_MODULARS; i++) {
		if (info->mod_type[i] == AVA4_TYPE_NULL)
			continue;

		if (info->mod_type[i] == AVA4_TYPE_MM50) {
			for (j = 0; j < info->miner_count[i]; j++) {
				sprintf(buf, " MA%d[", j);
				strcat(statbuf[i], buf);
				for (k = 0; k < info->asic_count[i]; k++) {
					sprintf(buf, "%d ", info->ma_sum[i][j][k]);
					strcat(statbuf[i], buf);
				}

				statbuf[i][strlen(statbuf[i]) - 1] = ']';
			}
		}
	}
	if (opt_debug) {
		for (i = 1; i < AVA4_DEFAULT_MODULARS; i++) {
			if (info->mod_type[i] == AVA4_TYPE_NULL)
				continue;

			if (info->mod_type[i] == AVA4_TYPE_MM60) {
				sprintf(buf, " PLL[");
				strcat(statbuf[i], buf);
				for (j = 0; j < AVA4_DEFAULT_PLL_MAX; j++) {
					sprintf(buf, "%d ", info->pll_sel[i][j]);
					strcat(statbuf[i], buf);
				}
				statbuf[i][strlen(statbuf[i]) - 1] = ']';
			}
		}
	}
	for (i = 1; i < AVA4_DEFAULT_MODULARS; i++) {
		if (info->mod_type[i] == AVA4_TYPE_NULL)
			continue;

		if (info->mod_type[i] == AVA4_TYPE_MM60) {
			sprintf(buf, " TA[%d]", info->total_asics[i]);
			strcat(statbuf[i], buf);
		}
	}
	for (i = 1; i < AVA4_DEFAULT_MODULARS; i++) {
		if (info->mod_type[i] == AVA4_TYPE_NULL)
			continue;

		if (info->mod_type[i] == AVA4_TYPE_MM60) {
			sprintf(buf, " EC[%d]", info->error_code[i]);
			strcat(statbuf[i], buf);
		}
	}
	if (opt_debug) {
		for (i = 1; i < AVA4_DEFAULT_MODULARS; i++) {
			if (info->mod_type[i] == AVA4_TYPE_NULL)
				continue;

			if (info->mod_type[i] == AVA4_TYPE_MM60) {
				sprintf(buf, " SF[%d %d %d]",
					info->set_smart_frequency[i][0],
					info->set_smart_frequency[i][1],
					info->set_smart_frequency[i][2]);
				strcat(statbuf[i], buf);
			}
		}

		for (i = 1; i < AVA4_DEFAULT_MODULARS; i++) {
			if (info->mod_type[i] == AVA4_TYPE_NULL)
				continue;

			if (info->mod_type[i] == AVA4_TYPE_MM60) {
				sprintf(buf, " FM[%d]", info->freq_mode[i]);
				strcat(statbuf[i], buf);
			}
		}
	}
	for (i = 1; i < AVA4_DEFAULT_MODULARS; i++) {
		if (info->mod_type[i] == AVA4_TYPE_NULL)
			continue;

		sprintf(buf, "MM ID%d", i);
		root = api_add_string(root, buf, statbuf[i], true);
	}

	root = api_add_int(root, "MM Count", &(info->mm_count), true);
	if (!has_a6)
		root = api_add_bool(root, "Automatic Voltage", &opt_avalon4_autov, true);
	root = api_add_int(root, "Smart Speed", &opt_avalon4_smart_speed, true);
	root = api_add_bool(root, "Nonce check", &opt_avalon4_noncecheck, true);
	if (info->connecter == AVA4_CONNECTER_IIC)
		root = api_add_string(root, "Connecter", "IIC", true);

	if (info->connecter == AVA4_CONNECTER_AUC) {
		root = api_add_string(root, "Connecter", "AUC", true);
		root = api_add_string(root, "AUC VER", info->auc_version, false);
		root = api_add_int(root, "AUC I2C Speed", &(info->auc_speed), true);
		root = api_add_int(root, "AUC I2C XDelay", &(info->auc_xdelay), true);
		root = api_add_int(root, "AUC ADC", &(info->auc_temp), true);
	}

	return root;
}

/* format: freq[-addr[-miner[-chip]]] add4[0, 63], miner[1, miner_count], chip[1, asic_count] */
char *set_avalon4_device_freq(struct cgpu_info *avalon4, char *arg)
{
	struct avalon4_info *info = avalon4->device_data;
	char *colon1, *colon2, *param = arg;
	unsigned int val[3], addr = 0, i;
	uint32_t miner_id = 0, chip_id = 0;

	if (!(*arg))
		return NULL;

	colon1 = strchr(arg, ':');
	if (colon1) {
		*(colon1++) = '\0';
		param = colon1;
	}

	if (*arg) {
		val[0] = atoi(arg);
		if (val[0] < AVA4_DEFAULT_FREQUENCY_MIN || val[0] > AVA4_DEFAULT_FREQUENCY_MAX)
			return "Invalid value1 passed to set_avalon4_device_freq";
	}

	if (colon1 && *colon1) {
		colon2 = strchr(colon1, ':');
		if (colon2) {
			*(colon2++) = '\0';
			param = colon2;
		}

		if (*colon1) {
			val[1] = atoi(colon1);
			if (val[1] < AVA4_DEFAULT_FREQUENCY_MIN || val[1] > AVA4_DEFAULT_FREQUENCY_MAX)
				return "Invalid value2 passed to set_avalon4_device_freq";
		}

		if (colon2 && *colon2) {
			val[2] = atoi(colon2);
			if (val[2] < AVA4_DEFAULT_FREQUENCY_MIN || val[2] > AVA4_DEFAULT_FREQUENCY_MAX)
				return "Invalid value3 passed to set_avalon4_device_freq";
		}
	}

	if (!val[0])
		val[2] = val[1] = val[0] = AVA4_DEFAULT_FREQUENCY;

	if (!val[1])
		val[2] = val[1] = val[0];

	if (!val[2])
		val[2] = val[1];

	colon1 = strchr(param, '-');
	if (colon1) {
		sscanf(colon1, "-%d-%d-%d", &addr, &miner_id, &chip_id);
		if (miner_id >= AVA4_DEFAULT_MODULARS) {
			applog(LOG_ERR, "invalid dev index: %d, valid range 0-%d", addr, (AVA4_DEFAULT_MODULARS - 1));
			return "Invalid dev index to set_avalon4_device_freq";
		}
		if (!info->enable[addr]) {
			applog(LOG_ERR, "Disabled dev:%d", addr);
			return "Disabled dev to set_avalon4_device_freq";
		}
		if (miner_id > info->miner_count[addr]) {
			applog(LOG_ERR, "invalid miner index: %d, valid range 0-%d", chip_id, info->miner_count[addr]);
			return "Invalid miner index to set_avalon4_device_freq";
		}
		if (chip_id > info->asic_count[addr]) {
			applog(LOG_ERR, "invalid asic index: %d, valid range 0-%d", chip_id, info->asic_count[addr]);
			return "Invalid asic index to set_avalon4_device_freq";
		}
	}

	if (!miner_id || !chip_id) {
		memcpy(opt_avalon4_freq, val, sizeof(int) * 3);
		if (!addr) {
			for (i = 1; i < AVA4_DEFAULT_MODULARS; i++) {
				if (!info->enable[i])
					continue;

				avalon4_set_freq(avalon4, i, 0, 0, val);
			}
		} else
			avalon4_set_freq(avalon4, addr, 0, 0, val);
	} else
		avalon4_set_freq(avalon4, addr, miner_id, chip_id, val);

	return NULL;
}

static char *avalon4_set_device(struct cgpu_info *avalon4, char *option, char *setting, char *replybuf)
{
	int val, i, j;
	struct avalon4_info *info = avalon4->device_data;

	if (strcasecmp(option, "help") == 0) {
		sprintf(replybuf, "led|fan|voltage|frequency|pdelay|freezesafe");
		return replybuf;
	}

	if (strcasecmp(option, "freezesafe") == 0) {
		if (!setting || !*setting) {
			sprintf(replybuf, "missing freezesafe mode setting");
			return replybuf;
		}

		val = atoi(setting);

		opt_avalon4_freezesafe = val ? 1 : 0;

		applog(LOG_NOTICE, "%s-%d: update freezesafe mode: %d",
			avalon4->drv->name, avalon4->device_id, opt_avalon4_freezesafe);

		return NULL;
	}

	if (strcasecmp(option, "pdelay") == 0) {
		if (!setting || !*setting) {
			sprintf(replybuf, "missing polling delay setting");
			return replybuf;
		}

		val = atoi(setting);
		if (val < 1 || val > 65535) {
			sprintf(replybuf, "invalid polling delay: %d, valid range 1-65535", val);
			return replybuf;
		}

		opt_avalon4_polling_delay = val;

		applog(LOG_NOTICE, "%s-%d: Update polling delay to: %d",
		       avalon4->drv->name, avalon4->device_id, val);

		return NULL;
	}

	if (strcasecmp(option, "fan") == 0) {
		if (!setting || !*setting) {
			sprintf(replybuf, "missing fan value");
			return replybuf;
		}

		if (set_avalon4_fan(setting)) {
			sprintf(replybuf, "invalid fan value, valid range 0-100");
			return replybuf;
		}

		applog(LOG_NOTICE, "%s-%d: Update fan to %d-%d",
		       avalon4->drv->name, avalon4->device_id,
		       opt_avalon4_fan_min, opt_avalon4_fan_max);

		return NULL;
	}

	if (strcasecmp(option, "frequency") == 0) {
		if (!setting || !*setting) {
			sprintf(replybuf, "missing frequency value");
			return replybuf;
		}

		if (set_avalon4_device_freq(avalon4, setting)) {
			sprintf(replybuf, "invalid frequency value, valid range %d-%d",
				AVA4_DEFAULT_FREQUENCY_MIN, AVA4_DEFAULT_FREQUENCY_MAX);
			return replybuf;
		}

		applog(LOG_NOTICE, "%s-%d: Update frequency to %d",
		       avalon4->drv->name, avalon4->device_id,
		       (opt_avalon4_freq[0] * 4 + opt_avalon4_freq[1] * 4 + opt_avalon4_freq[2]) / 9);

		return NULL;
	}

	if (strcasecmp(option, "led") == 0) {
		int val_led = -1;

		if (!setting || !*setting) {
			sprintf(replybuf, "missing module_id setting");
			return replybuf;
		}

		sscanf(setting, "%d-%d", &val, &val_led);
		if (val < 1 || val >= AVA4_DEFAULT_MODULARS) {
			sprintf(replybuf, "invalid module_id: %d, valid range 1-%d", val, AVA4_DEFAULT_MODULARS);
			return replybuf;
		}

		if (!info->enable[val]) {
			sprintf(replybuf, "the current module was disabled %d", val);
			return replybuf;
		}

		if (val_led == -1)
			info->led_red[val] = !info->led_red[val];
		else {
			if (val_led < 0 || val_led > 1) {
				sprintf(replybuf, "invalid LED status: %d, valid value 0|1", val_led);
				return replybuf;
			}

			if (val_led != info->led_red[val])
				info->led_red[val] = val_led;
		}

		applog(LOG_NOTICE, "%s-%d: Module:%d, LED: %s",
				avalon4->drv->name, avalon4->device_id,
				val, info->led_red[val] ? "on" : "off");

		return NULL;
	}

	if (strcasecmp(option, "voltage") == 0) {
		int val_mod, val_volt, val_ch = -1, val_offset = -1;

		if (!setting || !*setting) {
			sprintf(replybuf, "missing voltage value");
			return replybuf;
		}

		sscanf(setting, "%d-%d-%d-%d", &val_mod, &val_volt, &val_ch, &val_offset);
		if (val_mod < 0 || val_mod >= AVA4_DEFAULT_MODULARS ||
		    val_volt < AVA4_DEFAULT_VOLTAGE_MIN || val_volt > AVA4_DEFAULT_VOLTAGE_MAX) {
			sprintf(replybuf, "invalid module_id or voltage value, valid module_id range %d-%d, valid voltage range %d-%d",
				0, AVA4_DEFAULT_MODULARS,
				AVA4_DEFAULT_VOLTAGE_MIN, AVA4_DEFAULT_VOLTAGE_MAX);
			return replybuf;
		}

		if ((val_ch != -1) && (val_ch < -1 || val_ch >= AVA4_DEFAULT_MINER_MAX)) {
			sprintf(replybuf, "invalid miner_id,  valid miner_id range %d-%d",
				0, AVA4_DEFAULT_MINER_MAX - 1);
			return replybuf;
		}

		if ((val_offset != -1) && ((val_volt + val_offset) < AVA4_DEFAULT_VOLTAGE_MIN ||
					((val_volt + val_offset) > AVA4_DEFAULT_VOLTAGE_MAX))) {
			sprintf(replybuf, "invalid val_offset,  valid val_offset range %d-%d",
					AVA4_DEFAULT_VOLTAGE_MIN - val_volt,
					AVA4_DEFAULT_VOLTAGE_MAX - val_volt);
			return replybuf;
		}

		if (!info->enable[val_mod]) {
			sprintf(replybuf, "the current module was disabled %d", val_mod);
			return replybuf;
		}

		info->set_voltage[val_mod] = val_volt;
		if (val_ch == -1) {
			for (i = 0; i < info->miner_count[val_mod]; i++) {
				info->set_voltage_i[val_mod][i] = val_volt;
				info->set_voltage_offset[val_mod][i] = 0;
			}
		} else {
			info->set_voltage_i[val_mod][val_ch] = val_volt;
			if (val_offset == -1)
				info->set_voltage_offset[val_mod][val_ch] = 0;
			else
				info->set_voltage_offset[val_mod][val_ch] = val_offset;
		}

		if (val_mod == AVA4_MODULE_BROADCAST) {
			for (i = 1; i < AVA4_DEFAULT_MODULARS; i++) {
				info->set_voltage[i] = val_volt;
				if (val_ch == -1) {
					for (j = 0; j < info->miner_count[i]; j++) {
						info->set_voltage_i[i][j] = val_volt;
						info->set_voltage_offset[i][j] = 0;
					}
				} else {
					info->set_voltage_i[i][val_ch] = val_volt;
					if (val_offset == -1)
						info->set_voltage_offset[i][val_ch] = 0;
					else
						info->set_voltage_offset[i][val_ch] = val_offset;
				}
			}
		}

		applog(LOG_NOTICE, "%s-%d: Update module[%d] voltage to %d, val_ch:%d, val_offset:%d",
		       avalon4->drv->name, avalon4->device_id, val_mod, val_volt, val_ch, val_offset);

		return NULL;
	}

	sprintf(replybuf, "Unknown option: %s", option);
	return replybuf;
}

static void avalon4_statline_before(char *buf, size_t bufsiz, struct cgpu_info *avalon4)
{
	struct avalon4_info *info = avalon4->device_data;
	int temp = get_current_temp_max(info);
	int voltsmin = AVA4_DEFAULT_VOLTAGE_MAX, voltsmax = AVA4_DEFAULT_VOLTAGE_MIN;
	int fanmin = AVA4_DEFAULT_FAN_MAX, fanmax = AVA4_DEFAULT_FAN_MIN;
	int i, j, tempadcmin = AVA4_ADC_MAX, vcc12adcmin = AVA4_ADC_MAX;
	int has_a6 = 0;
	uint32_t frequency = 0;
	float ghs_sum = 0.0;

	for (i = 1; i < AVA4_DEFAULT_MODULARS; i++) {
		if (!info->enable[i])
			continue;

		if (info->mod_type[i] == AVA4_TYPE_MM60)
			has_a6 = 1;

		if (fanmax <= info->fan_pct[i])
			fanmax = info->fan_pct[i];
		if (fanmin >= info->fan_pct[i])
			fanmin = info->fan_pct[i];

		if (voltsmax <= info->get_voltage[i])
			voltsmax = info->get_voltage[i];
		if (voltsmin >= info->get_voltage[i])
			voltsmin = info->get_voltage[i];

		for (j = 0; j < AVA4_DEFAULT_ADC_MAX - 2; j++) {
			if (info->adc[i][j] < tempadcmin)
				tempadcmin = info->adc[i][j];
		}
		if (info->adc[i][4] < vcc12adcmin)
			vcc12adcmin = info->adc[i][4];
		frequency += info->get_frequency[i];
		ghs_sum += ((float)info->get_frequency[i] / 1000 * info->total_asics[i]);
	}

	if (has_a6) {
		if (info->mm_count)
			frequency /= info->mm_count;
		tailsprintf(buf, bufsiz, "%4dMhz %.2fGHS %2dC-%2dC %3d%% %.1fV", frequency / 96,
				ghs_sum, temp, (int)convert_temp(tempadcmin), fanmin,
				(vcc12adcmin == AVA4_ADC_MAX) ? 0 : convert_voltage(vcc12adcmin, 1 / 11.0));
	} else {
		frequency = (opt_avalon4_freq[0] * 4 + opt_avalon4_freq[1] * 4 + opt_avalon4_freq[2]) / 9;
		tailsprintf(buf, bufsiz, "%4dMhz %2dC %3d%% %.3fV", frequency,
				temp, fanmin, (float)voltsmax / 10000);
	}
}

struct device_drv avalon4_drv = {
	.drv_id = DRIVER_avalon4,
	.dname = "avalon4",
	.name = "AV4",
	.set_device = avalon4_set_device,
	.get_api_stats = avalon4_api_stats,
	.get_statline_before = avalon4_statline_before,
	.drv_detect = avalon4_detect,
	.thread_prepare = avalon4_prepare,
	.hash_work = hash_driver_work,
	.flush_work = avalon4_update,
	.update_work = avalon4_update,
	.scanwork = avalon4_scanhash,
	.max_diff = AVA4_DRV_DIFFMAX,
};
