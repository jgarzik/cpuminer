/*
 * Copyright 2017 xuzhenxing <xuzhenxing@canaan-creative.com>
 * Copyright 2016-2017 Mikeqin <Fengling.Qin@gmail.com>
 * Copyright 2016 Con Kolivas <kernel@kolivas.org>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 3 of the License, or (at your option)
 * any later version.  See COPYING for more details.
 */
#include <math.h>
#include "config.h"

#include "miner.h"
#include "driver-avalon8.h"
#include "crc.h"
#include "sha2.h"
#include "hexdump.c"

#define get_fan_pwm(v)	(AVA8_PWM_MAX - (v) * AVA8_PWM_MAX / 100)

int opt_avalon8_temp_target = AVA8_DEFAULT_TEMP_TARGET;

int opt_avalon8_fan_min = AVA8_DEFAULT_FAN_MIN;
int opt_avalon8_fan_max = AVA8_DEFAULT_FAN_MAX;

int opt_avalon8_voltage_level = AVA8_INVALID_VOLTAGE_LEVEL;
int opt_avalon8_voltage_level_offset = AVA8_DEFAULT_VOLTAGE_LEVEL_OFFSET;

int opt_avalon8_asic_otp = AVA8_INVALID_ASIC_OTP;
static uint8_t opt_avalon8_cycle_hit_flag;

int opt_avalon8_freq[AVA8_DEFAULT_PLL_CNT] =
{
	AVA8_DEFAULT_FREQUENCY,
	AVA8_DEFAULT_FREQUENCY,
	AVA8_DEFAULT_FREQUENCY,
	AVA8_DEFAULT_FREQUENCY
};

int opt_avalon8_freq_sel = AVA8_DEFAULT_FREQUENCY_SEL;

int opt_avalon8_polling_delay = AVA8_DEFAULT_POLLING_DELAY;

int opt_avalon8_aucspeed = AVA8_AUC_SPEED;
int opt_avalon8_aucxdelay = AVA8_AUC_XDELAY;

int opt_avalon8_smart_speed = AVA8_DEFAULT_SMART_SPEED;
/*
 * smart speed have 2 modes
 * 1. auto speed by A3210 chips
 * 2. option 1 + adjust by average frequency
 */
bool opt_avalon8_iic_detect = AVA8_DEFAULT_IIC_DETECT;

uint32_t opt_avalon8_th_pass = AVA8_INVALID_TH_PASS;
uint32_t opt_avalon8_th_fail = AVA8_INVALID_TH_FAIL;
uint32_t opt_avalon8_th_init = AVA8_DEFAULT_TH_INIT;
uint32_t opt_avalon8_th_ms = AVA8_DEFAULT_TH_MS;
uint32_t opt_avalon8_th_timeout = AVA8_INVALID_TH_TIMEOUT;
uint32_t opt_avalon8_th_add = AVA8_DEFAULT_TH_ADD;
uint32_t opt_avalon8_nonce_mask = AVA8_INVALID_NONCE_MASK;
uint32_t opt_avalon8_nonce_check = AVA8_DEFAULT_NONCE_CHECK;
uint32_t opt_avalon8_mux_l2h = AVA8_DEFAULT_MUX_L2H;
uint32_t opt_avalon8_mux_h2l = AVA8_DEFAULT_MUX_H2L;
uint32_t opt_avalon8_h2ltime0_spd = AVA8_DEFAULT_H2LTIME0_SPD;
uint32_t opt_avalon8_roll_enable = AVA8_DEFAULT_ROLL_ENABLE;
uint32_t opt_avalon8_spdlow = AVA8_INVALID_SPDLOW;
uint32_t opt_avalon8_spdhigh = AVA8_DEFAULT_SPDHIGH;

uint32_t opt_avalon8_pid_p = AVA8_DEFAULT_PID_P;
uint32_t opt_avalon8_pid_i = AVA8_DEFAULT_PID_I;
uint32_t opt_avalon8_pid_d = AVA8_DEFAULT_PID_D;

uint32_t cpm_table[] =
{
	0x04400000,
	0x04000000,
	0x008ffbe1,
	0x0097fde1,
	0x009fffe1,
	0x009ddf61,
	0x009dcf61,
	0x009f47c1,
	0x009fbfe1,
	0x009f37c1,
	0x009daf61,
	0x009b26c1,
	0x009da761,
	0x00999e61,
	0x009b9ee1,
	0x009d9f61,
	0x009f9fe1,
	0x00991641,
	0x009a96a1,
	0x009c1701,
	0x009d9761,
	0x009f17c1,
	0x00958d61,
	0x00968da1,
	0x00978de1,
	0x00988e21,
	0x00998e61,
	0x009a8ea1,
	0x009b8ee1,
	0x009c8f21,
	0x009d8f61,
	0x009e8fa1,
	0x009f8fe1,
	0x00900401,
	0x00908421,
	0x00910441,
	0x00918461,
	0x00920481,
	0x009284a1,
	0x009304c1,
	0x009384e1,
	0x00940501,
	0x00948521,
	0x00950541,
	0x00958561,
	0x00960581,
	0x009685a1,
	0x009705c1,
	0x009785e1
};

struct avalon8_dev_description avalon8_dev_table[] = {
	{
		"821",
		821,
		4,
		26,
		AVA8_MM821_VIN_ADC_RATIO,
		AVA8_MM821_VOUT_ADC_RATIO,
		5,
		{
			AVA8_DEFAULT_FREQUENCY_0M,
			AVA8_DEFAULT_FREQUENCY_0M,
			AVA8_DEFAULT_FREQUENCY_0M,
			AVA8_DEFAULT_FREQUENCY_650M
		}
	},
	{
		"831",
		831,
		4,
		26,
		AVA8_MM831_VIN_ADC_RATIO,
		AVA8_MM831_VOUT_ADC_RATIO,
		5,
		{
			AVA8_DEFAULT_FREQUENCY_0M,
			AVA8_DEFAULT_FREQUENCY_0M,
			AVA8_DEFAULT_FREQUENCY_0M,
			AVA8_DEFAULT_FREQUENCY_725M
		}
	},
	{
		"841",
		841,
		4,
		26,
		AVA8_MM841_VIN_ADC_RATIO,
		AVA8_MM841_VOUT_ADC_RATIO,
		5,
		{
			AVA8_DEFAULT_FREQUENCY_0M,
			AVA8_DEFAULT_FREQUENCY_0M,
			AVA8_DEFAULT_FREQUENCY_0M,
			AVA8_DEFAULT_FREQUENCY_775M
		}
	},
	{
		"851",
		851,
		4,
		26,
		AVA8_MM851_VIN_ADC_RATIO,
		AVA8_MM851_VOUT_ADC_RATIO,
		5,
		{
			AVA8_DEFAULT_FREQUENCY_0M,
			AVA8_DEFAULT_FREQUENCY_0M,
			AVA8_DEFAULT_FREQUENCY_0M,
			AVA8_DEFAULT_FREQUENCY_850M
		}
	}
};

static uint32_t api_get_cpm(uint32_t freq)
{
	return cpm_table[freq / 25];
}

static uint32_t encode_voltage(int volt_level)
{
	if (volt_level > AVA8_DEFAULT_VOLTAGE_LEVEL_MAX)
	      volt_level = AVA8_DEFAULT_VOLTAGE_LEVEL_MAX;
	else if (volt_level < AVA8_DEFAULT_VOLTAGE_LEVEL_MIN)
	      volt_level = AVA8_DEFAULT_VOLTAGE_LEVEL_MIN;

	if (volt_level < 0)
		return 0x8080 | (-volt_level);

	return 0x8000 | volt_level;
}

static uint32_t decode_voltage(struct avalon8_info *info, int modular_id, uint32_t volt)
{
	return (volt * info->vout_adc_ratio[modular_id] / info->asic_count[modular_id] / 100);
}

static uint16_t decode_vin(struct avalon8_info *info, int modular_id, uint16_t volt)
{
	return (volt * info->vin_adc_ratio[modular_id] / 1000);
}

static double decode_pvt_temp(uint16_t pvt_code)
{
	double g = 60.0;
	double h = 200.0;
	double cal5 = 4094.0;
	double j = -0.1;
	double fclkm = 6.25;

	/* Mode2 temperature equation */
	return g + h * (pvt_code / cal5 - 0.5) + j * fclkm;
}

static uint32_t decode_pvt_volt(uint16_t volt)
{
	double vref = 1.20;
	double r = 16384.0; /* 2 ** 14 */
	double c;

	c = vref / 5.0 * (6 * (volt - 0.5) / r - 1.0);

	if (c < 0)
		c = 0;

	return c * 1000;
}

#define SERIESRESISTOR          10000
#define THERMISTORNOMINAL       10000
#define BCOEFFICIENT            3500
#define TEMPERATURENOMINAL      25
float decode_auc_temp(int value)
{
	float ret, resistance;

	if (!((value > 0) && (value < 33000)))
		return -273;

	resistance = (3.3 * 10000 / value) - 1;
	resistance = SERIESRESISTOR / resistance;
	ret = resistance / THERMISTORNOMINAL;
	ret = logf(ret);
	ret /= BCOEFFICIENT;
	ret += 1.0 / (TEMPERATURENOMINAL + 273.15);
	ret = 1.0 / ret;
	ret -= 273.15;

	return ret;
}

#define UNPACK32(x, str)			\
{						\
	*((str) + 3) = (uint8_t) ((x)      );	\
	*((str) + 2) = (uint8_t) ((x) >>  8);	\
	*((str) + 1) = (uint8_t) ((x) >> 16);	\
	*((str) + 0) = (uint8_t) ((x) >> 24);	\
}

static inline void sha256_prehash(const unsigned char *message, unsigned int len, unsigned char *digest)
{
	int i;
	sha256_ctx ctx;

	sha256_init(&ctx);
	sha256_update(&ctx, message, len);

	for (i = 0; i < 8; i++)
		UNPACK32(ctx.h[i], &digest[i << 2]);
}

char *set_avalon8_fan(char *arg)
{
	int val1, val2, ret;

	ret = sscanf(arg, "%d-%d", &val1, &val2);
	if (ret < 1)
		return "No value passed to avalon8-fan";
	if (ret == 1)
		val2 = val1;

	if (val1 < 0 || val1 > 100 || val2 < 0 || val2 > 100 || val2 < val1)
		return "Invalid value passed to avalon8-fan";

	opt_avalon8_fan_min = val1;
	opt_avalon8_fan_max = val2;

	return NULL;
}

char *set_avalon8_freq(char *arg)
{
	int val[AVA8_DEFAULT_PLL_CNT];
	char *colon, *data;
	int i;

	if (!(*arg))
		return NULL;

	data = arg;
	memset(val, 0, sizeof(val));

	for (i = 0; i < AVA8_DEFAULT_PLL_CNT; i++) {
		colon = strchr(data, ':');
		if (colon)
			*(colon++) = '\0';
		else {
			/* last value */
			if (*data) {
				val[i] = atoi(data);
				if (val[i] > AVA8_DEFAULT_FREQUENCY_MAX)
					return "Invalid value passed to avalon8-freq";
			}
			break;
		}

		if (*data) {
			val[i] = atoi(data);
			if (val[i] > AVA8_DEFAULT_FREQUENCY_MAX)
				return "Invalid value passed to avalon8-freq";
		}
		data = colon;
	}

	for (i = 0; i < AVA8_DEFAULT_PLL_CNT; i++)
		opt_avalon8_freq[i] = val[i];

	return NULL;
}

char *set_avalon8_voltage_level(char *arg)
{
       int val, ret;

       ret = sscanf(arg, "%d", &val);
       if (ret < 1)
               return "No value passed to avalon8-voltage-level";

       if (val < AVA8_DEFAULT_VOLTAGE_LEVEL_MIN || val > AVA8_DEFAULT_VOLTAGE_LEVEL_MAX)
               return "Invalid value passed to avalon8-voltage-level";

       opt_avalon8_voltage_level = val;

       return NULL;
}

char *set_avalon8_voltage_level_offset(char *arg)
{
       int val, ret;

       ret = sscanf(arg, "%d", &val);
       if (ret < 1)
               return "No value passed to avalon8-voltage-level-offset";

       if (val < AVA8_DEFAULT_VOLTAGE_LEVEL_OFFSET_MIN || val > AVA8_DEFAULT_VOLTAGE_LEVEL_OFFSET_MAX)
               return "Invalid value passed to avalon8-voltage-level-offset";

       opt_avalon8_voltage_level_offset = val;

       return NULL;
}

char *set_avalon8_asic_otp(char *arg)
{
	int val, ret;

	ret = sscanf(arg, "%d", &val);
	if (ret < 1)
		return "No value passed to avalon8-cinfo-asic";

	if (val < 0 || val > (AVA8_DEFAULT_ASIC_MAX - 1))
		return "Invalid value passed to avalon8-cinfo-asic";

	opt_avalon8_asic_otp = val;

	opt_avalon8_cycle_hit_flag = 0;

	return NULL;
}

static int avalon8_init_pkg(struct avalon8_pkg *pkg, uint8_t type, uint8_t idx, uint8_t cnt)
{
	unsigned short crc;

	pkg->head[0] = AVA8_H1;
	pkg->head[1] = AVA8_H2;

	pkg->type = type;
	pkg->opt = 0;
	pkg->idx = idx;
	pkg->cnt = cnt;

	crc = crc16(pkg->data, AVA8_P_DATA_LEN);

	pkg->crc[0] = (crc & 0xff00) >> 8;
	pkg->crc[1] = crc & 0xff;

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

	applog(LOG_DEBUG, "avalon8: job_id doesn't match! [%04x:%04x (%s)]",
	       crc, crc_expect, pool_job_id);

	return 1;
}

static inline int get_temp_max(struct avalon8_info *info, int addr)
{
	int i, j;
	int max = -273;

	for (i = 0; i < info->miner_count[addr]; i++) {
		for (j = 0; j < info->asic_count[addr]; j++) {
			if (info->temp[addr][i][j] > max)
				max = info->temp[addr][i][j];
		}
	}

	if (max < info->temp_mm[addr])
		max = info->temp_mm[addr];

	return max;
}

/*
 * Incremental PID controller
 *
 * controller input: u, output: t
 *
 * delta_u = P * [e(k) - e(k-1)] + I * e(k) + D * [e(k) - 2*e(k-1) + e(k-2)];
 * e(k) = t(k) - t[target];
 * u(k) = u(k-1) + delta_u;
 *
 */
static inline uint32_t adjust_fan(struct avalon8_info *info, int id)
{
	int t;
	double delta_u;
	double delta_p, delta_i, delta_d;
	uint32_t pwm;

	t = get_temp_max(info, id);

	/* update target error */
	info->pid_e[id][2] = info->pid_e[id][1];
	info->pid_e[id][1] = info->pid_e[id][0];
	info->pid_e[id][0] = t - info->temp_target[id];

	if (t > AVA8_DEFAULT_PID_TEMP_MAX) {
		info->pid_u[id] = opt_avalon8_fan_max;
	} else if (t < AVA8_DEFAULT_PID_TEMP_MIN) {
		info->pid_u[id] = opt_avalon8_fan_min;
	} else if (!info->pid_0[id]) {
			/* first, init u as t */
			info->pid_0[id] = 1;
			info->pid_u[id] = t;
	} else {
		delta_p = info->pid_p[id] * (info->pid_e[id][0] - info->pid_e[id][1]);
		delta_i = info->pid_i[id] * info->pid_e[id][0];
		delta_d = info->pid_d[id] * (info->pid_e[id][0] - 2 * info->pid_e[id][1] + info->pid_e[id][2]);

		/*Parameter I is int type(1, 2, 3...), but should be used as a smaller value (such as 0.1, 0.01...)*/
		delta_u = delta_p + delta_i / 100 + delta_d;

		info->pid_u[id] += delta_u;
	}

	if(info->pid_u[id] > opt_avalon8_fan_max)
		info->pid_u[id] = opt_avalon8_fan_max;

	if (info->pid_u[id] < opt_avalon8_fan_min)
		info->pid_u[id] = opt_avalon8_fan_min;

	/* Round from float to int */
	info->fan_pct[id] = (int)(info->pid_u[id] + 0.5);
	pwm = get_fan_pwm(info->fan_pct[id]);

	return pwm;
}

static int decode_pkg(struct cgpu_info *avalon8, struct avalon8_ret *ar, int modular_id)
{
	struct avalon8_info *info = avalon8->device_data;
	struct pool *pool, *real_pool;
	struct pool *pool_stratum0 = &info->pool0;
	struct pool *pool_stratum1 = &info->pool1;
	struct pool *pool_stratum2 = &info->pool2;
	struct thr_info *thr = NULL;

	unsigned short expected_crc;
	unsigned short actual_crc;
	uint32_t nonce, nonce2, ntime, miner, chip_id, tmp;
	uint8_t job_id[2];
	int pool_no;
	uint32_t i;
	int64_t last_diff1;
	uint16_t vin;

	uint32_t asic_id,miner_id;

	if (likely(avalon8->thr))
		thr = avalon8->thr[0];
	if (ar->head[0] != AVA8_H1 && ar->head[1] != AVA8_H2) {
		applog(LOG_DEBUG, "%s-%d-%d: H1 %02x, H2 %02x",
				avalon8->drv->name, avalon8->device_id, modular_id,
				ar->head[0], ar->head[1]);
		hexdump(ar->data, 32);
		return 1;
	}

	expected_crc = crc16(ar->data, AVA8_P_DATA_LEN);
	actual_crc = ((ar->crc[0] & 0xff) << 8) | (ar->crc[1] & 0xff);
	if (expected_crc != actual_crc) {
		applog(LOG_DEBUG, "%s-%d-%d: %02x: expected crc(%04x), actual_crc(%04x)",
		       avalon8->drv->name, avalon8->device_id, modular_id,
		       ar->type, expected_crc, actual_crc);
		return 1;
	}

	switch(ar->type) {
	case AVA8_P_NONCE:
		applog(LOG_DEBUG, "%s-%d-%d: AVA8_P_NONCE", avalon8->drv->name, avalon8->device_id, modular_id);
		memcpy(&miner, ar->data + 0, 4);
		memcpy(&nonce2, ar->data + 4, 4);
		memcpy(&ntime, ar->data + 8, 4);
		memcpy(&nonce, ar->data + 12, 4);
		job_id[0] = ar->data[16];
		job_id[1] = ar->data[17];
		pool_no = (ar->data[18] | (ar->data[19] << 8));

		miner = be32toh(miner);
		chip_id = (miner >> 16) & 0xffff;
		miner &= 0xffff;
		ntime = be32toh(ntime);
		if (miner >= info->miner_count[modular_id] ||
		    pool_no >= total_pools || pool_no < 0) {
			applog(LOG_DEBUG, "%s-%d-%d: Wrong miner/pool_no %d/%d",
					avalon8->drv->name, avalon8->device_id, modular_id,
					miner, pool_no);
			break;
		}
		nonce2 = be32toh(nonce2);
		nonce = be32toh(nonce);

		if (ntime > info->max_ntime)
			info->max_ntime = ntime;

		applog(LOG_NOTICE, "%s-%d-%d: Found! P:%d - N2:%08x N:%08x NR:%d/%d [M:%d, A:%d, C:%d - MW: (%"PRIu64",%"PRIu64",%"PRIu64",%"PRIu64")]",
		       avalon8->drv->name, avalon8->device_id, modular_id,
		       pool_no, nonce2, nonce, ntime, info->max_ntime,
		       miner, chip_id, nonce & 0x7f,
		       info->chip_matching_work[modular_id][miner][0],
		       info->chip_matching_work[modular_id][miner][1],
		       info->chip_matching_work[modular_id][miner][2],
		       info->chip_matching_work[modular_id][miner][3]);

		real_pool = pool = pools[pool_no];
		if (job_idcmp(job_id, pool->swork.job_id)) {
			if (!job_idcmp(job_id, pool_stratum0->swork.job_id)) {
				applog(LOG_DEBUG, "%s-%d-%d: Match to previous stratum0! (%s)",
						avalon8->drv->name, avalon8->device_id, modular_id,
						pool_stratum0->swork.job_id);
				pool = pool_stratum0;
			} else if (!job_idcmp(job_id, pool_stratum1->swork.job_id)) {
				applog(LOG_DEBUG, "%s-%d-%d: Match to previous stratum1! (%s)",
						avalon8->drv->name, avalon8->device_id, modular_id,
						pool_stratum1->swork.job_id);
				pool = pool_stratum1;
			} else if (!job_idcmp(job_id, pool_stratum2->swork.job_id)) {
				applog(LOG_DEBUG, "%s-%d-%d: Match to previous stratum2! (%s)",
						avalon8->drv->name, avalon8->device_id, modular_id,
						pool_stratum2->swork.job_id);
				pool = pool_stratum2;
			} else {
				applog(LOG_ERR, "%s-%d-%d: Cannot match to any stratum! (%s)",
						avalon8->drv->name, avalon8->device_id, modular_id,
						pool->swork.job_id);
				if (likely(thr))
					inc_hw_errors(thr);
				info->hw_works_i[modular_id][miner]++;
				break;
			}
		}

		/* Can happen during init sequence before add_cgpu */
		if (unlikely(!thr))
			break;

		last_diff1 = avalon8->diff1;
		if (!submit_nonce2_nonce(thr, pool, real_pool, nonce2, nonce, ntime))
			info->hw_works_i[modular_id][miner]++;
		else {
			info->diff1[modular_id] += (avalon8->diff1 - last_diff1);
			info->chip_matching_work[modular_id][miner][chip_id]++;
		}
		break;
	case AVA8_P_STATUS:
		applog(LOG_DEBUG, "%s-%d-%d: AVA8_P_STATUS", avalon8->drv->name, avalon8->device_id, modular_id);
		hexdump(ar->data, 32);
		memcpy(&tmp, ar->data, 4);
		tmp = be32toh(tmp);
		info->temp_mm[modular_id] = tmp;
		avalon8->temp = decode_auc_temp(info->auc_sensor);

		memcpy(&tmp, ar->data + 4, 4);
		tmp = be32toh(tmp);
		info->fan_cpm[modular_id] = tmp;

		memcpy(&tmp, ar->data + 8, 4);
		info->local_works_i[modular_id][ar->idx] += be32toh(tmp);

		memcpy(&tmp, ar->data + 12, 4);
		info->hw_works_i[modular_id][ar->idx] += be32toh(tmp);

		memcpy(&tmp, ar->data + 16, 4);
		info->error_code[modular_id][ar->idx] = be32toh(tmp);

		memcpy(&tmp, ar->data + 20, 4);
		info->error_code[modular_id][ar->cnt] = be32toh(tmp);

		memcpy(&tmp, ar->data + 24, 4);
		info->error_crc[modular_id][ar->idx] += be32toh(tmp);
		break;
	case AVA8_P_STATUS_PMU:
		/* TODO: decode ntc led from PMU */
		applog(LOG_DEBUG, "%s-%d-%d: AVA8_P_STATUS_PMU", avalon8->drv->name, avalon8->device_id, modular_id);
		info->power_good[modular_id] = ar->data[16];
		for (i = 0; i < AVA8_DEFAULT_PMU_CNT; i++) {
			memcpy(&info->pmu_version[modular_id][i], ar->data + 24 + (i * 4), 4);
			info->pmu_version[modular_id][i][4] = '\0';
		}

		for (i = 0; i < info->miner_count[modular_id]; i++) {
			memcpy(&vin, ar->data + 8 + i * 2, 2);
			info->get_vin[modular_id][i] = decode_vin(info, modular_id, be16toh(vin));
		}
		break;
	case AVA8_P_STATUS_OTP:
		if (opt_avalon8_cycle_hit_flag)
			break;

		applog(LOG_DEBUG, "%s-%d-%d: AVA8_P_STATUS_OTP", avalon8->drv->name, avalon8->device_id, modular_id);

		/* ASIC reading cycle limit hit */
		if (ar->data[AVA8_OTP_INDEX_CYCLE_HIT]) {
			applog(LOG_DEBUG, "%s-%d-%d: AVA8_P_STATUS_OTP, OTP read cycle hit!", avalon8->drv->name, avalon8->device_id, modular_id);
			opt_avalon8_cycle_hit_flag = 1;
			break;
		}

		miner_id = ar->idx;
		if (miner_id > AVA8_DEFAULT_MINER_CNT)
			break;

		/* the reading step on MM side, 0:byte 3-0, 1:byte 7-4, 2:byte 11-8, 3:byte 15-12 */
		switch (ar->data[AVA8_OTP_INDEX_READ_STEP]) {
		case 0:
			memcpy(info->otp_info[modular_id][miner_id] + AVA8_OTP_INFO_LOTIDCRC_OFFSET, ar->data + AVA8_OTP_INFO_LOTIDCRC_OFFSET, 4);
			break;
		case 1:
			memcpy(info->otp_info[modular_id][miner_id] + AVA8_OTP_INFO_LOTIDCRC_OFFSET + 4, ar->data + AVA8_OTP_INFO_LOTIDCRC_OFFSET + 4, 2);
			break;
		case 2:
			memcpy(info->otp_info[modular_id][miner_id] + AVA8_OTP_INFO_LOTID_OFFSET, ar->data + AVA8_OTP_INFO_LOTID_OFFSET, 4);
			break;
		case 3:
			memcpy(info->otp_info[modular_id][miner_id] + AVA8_OTP_INFO_LOTID_OFFSET + 4, ar->data + AVA8_OTP_INFO_LOTID_OFFSET + 4, 4);
			break;
		case 4:
			memcpy(info->otp_info[modular_id][miner_id] + AVA8_OTP_INFO_LOTID_OFFSET + 8, ar->data + AVA8_OTP_INFO_LOTID_OFFSET + 8, 4);
			break;
		case 5:
			memcpy(info->otp_info[modular_id][miner_id] + AVA8_OTP_INFO_LOTID_OFFSET + 12, ar->data + AVA8_OTP_INFO_LOTID_OFFSET + 12, 4);
			break;
		case 6:
			memcpy(info->otp_info[modular_id][miner_id] + AVA8_OTP_INFO_LOTID_OFFSET + 16, ar->data + AVA8_OTP_INFO_LOTID_OFFSET + 16, 4);
			break;
		default:
			break;
		}

		/* get the data behind AVA8_OTP_INDEX_READ_STEP for later displaying use */
		memcpy(info->otp_info[modular_id][miner_id] + AVA8_OTP_INDEX_READ_STEP, ar->data + AVA8_OTP_INDEX_READ_STEP, 4);

		break;
	case AVA8_P_STATUS_VOLT:
		applog(LOG_DEBUG, "%s-%d-%d: AVA8_P_STATUS_VOLT", avalon8->drv->name, avalon8->device_id, modular_id);
		for (i = 0; i < info->miner_count[modular_id]; i++) {
			memcpy(&tmp, ar->data + i * 4, 4);
			info->get_voltage[modular_id][i] = decode_voltage(info, modular_id, be32toh(tmp));
		}
		break;
	case AVA8_P_STATUS_PLL:
		applog(LOG_DEBUG, "%s-%d-%d: AVA8_P_STATUS_PLL", avalon8->drv->name, avalon8->device_id, modular_id);
		for (i = 0; i < AVA8_DEFAULT_PLL_CNT; i++) {
			memcpy(&tmp, ar->data + i * 4, 4);
			info->get_pll[modular_id][ar->idx][i] = be32toh(tmp);

			memcpy(&tmp, ar->data + AVA8_DEFAULT_PLL_CNT * 4 + i * 4, 4);
			tmp = be32toh(tmp);
			if (tmp)
				info->set_frequency[modular_id][ar->idx][i] = tmp;
		}
		break;
	case AVA8_P_STATUS_PVT:
		applog(LOG_DEBUG, "%s-%d-%d: AVA8_P_STATUS_PVT", avalon8->drv->name, avalon8->device_id, modular_id);
		if (!strncmp((char *)&(info->mm_version[modular_id]), "851", 3)) {
			if (!info->asic_count[modular_id])
				break;

			if (ar->idx < info->asic_count[modular_id]) {
				for (i = 0; i < info->miner_count[modular_id]; i++) {
					memcpy(&tmp, ar->data + i * 4, 2);
					tmp = be16toh(tmp);
					info->temp[modular_id][i][ar->idx] = decode_pvt_temp(tmp);

					memcpy(&tmp, ar->data + i * 4 + 2, 2);
					tmp = be16toh(tmp);
					info->core_volt[modular_id][i][ar->idx][0] = decode_pvt_volt(tmp);
				}
			}
		} else {
			uint16_t pvt_tmp;

			if (!info->asic_count[modular_id])
				break;

			miner = ar->idx / info->asic_count[modular_id];
			chip_id = ar->idx % info->asic_count[modular_id];

			memcpy(&pvt_tmp, ar->data, 2);
			pvt_tmp = be16toh(pvt_tmp);
			info->temp[modular_id][miner][chip_id] = decode_pvt_temp(pvt_tmp);

			for (i = 0; i < AVA8_DEFAULT_CORE_VOLT_CNT; i++) {
				memcpy(&pvt_tmp, ar->data + 2 + 2 * i, 2);
				pvt_tmp = be16toh(pvt_tmp);
				info->core_volt[modular_id][miner][chip_id][i] = decode_pvt_volt(pvt_tmp);
			}
		}
		break;
	case AVA8_P_STATUS_ASIC:
		{
			int miner_id;
			int asic_id;
			uint16_t freq;

			if (!info->asic_count[modular_id])
				break;

			miner_id = ar->idx / info->asic_count[modular_id];
			asic_id = ar->idx % info->asic_count[modular_id];

			applog(LOG_DEBUG, "%s-%d-%d: AVA8_P_STATUS_ASIC %d-%d",
						avalon8->drv->name, avalon8->device_id, modular_id,
						miner_id, asic_id);

			memcpy(&tmp, ar->data + 0, 4);
			if (tmp)
				info->get_asic[modular_id][miner_id][asic_id][0] = be32toh(tmp);

			memcpy(&tmp, ar->data + 4, 4);
			if (tmp)
				info->get_asic[modular_id][miner_id][asic_id][1] = be32toh(tmp);

			for (i = 0; i < AVA8_DEFAULT_PLL_CNT; i++)
				info->get_asic[modular_id][miner_id][asic_id][2 + i] = ar->data[8 + i];

			if (!strncmp((char *)&(info->mm_version[modular_id]), "851", 3)) {
				for (i = 0; i < AVA8_DEFAULT_PLL_CNT; i++) {
					memcpy(&freq, ar->data + 8 + AVA8_DEFAULT_PLL_CNT + i * 2, 2);
					info->get_frequency[modular_id][miner_id][asic_id][i] = be16toh(freq);
				}
			}
		}
		break;
	case AVA8_P_STATUS_FAC:
		applog(LOG_DEBUG, "%s-%d-%d: AVA8_P_STATUS_FAC", avalon8->drv->name, avalon8->device_id, modular_id);
		info->factory_info[0] = ar->data[0];
		break;
	case AVA8_P_STATUS_OC:
		applog(LOG_DEBUG, "%s-%d-%d: AVA8_P_STATUS_OC", avalon8->drv->name, avalon8->device_id, modular_id);
		info->overclocking_info[0] = ar->data[0];
		break;
	default:
		applog(LOG_DEBUG, "%s-%d-%d: Unknown response %x", avalon8->drv->name, avalon8->device_id, modular_id, ar->type);
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
static int avalon8_auc_init_pkg(uint8_t *iic_pkg, struct avalon8_iic_info *iic_info, uint8_t *buf, int wlen, int rlen)
{
	memset(iic_pkg, 0, AVA8_AUC_P_SIZE);

	switch (iic_info->iic_op) {
	case AVA8_IIC_INIT:
		iic_pkg[0] = 12;	/* 4 bytes IIC header + 4 bytes speed + 4 bytes xfer delay */
		iic_pkg[3] = AVA8_IIC_INIT;
		iic_pkg[4] = iic_info->iic_param.aucParam[0] & 0xff;
		iic_pkg[5] = (iic_info->iic_param.aucParam[0] >> 8) & 0xff;
		iic_pkg[6] = (iic_info->iic_param.aucParam[0] >> 16) & 0xff;
		iic_pkg[7] = iic_info->iic_param.aucParam[0] >> 24;
		iic_pkg[8] = iic_info->iic_param.aucParam[1] & 0xff;
		iic_pkg[9] = (iic_info->iic_param.aucParam[1] >> 8) & 0xff;
		iic_pkg[10] = (iic_info->iic_param.aucParam[1] >> 16) & 0xff;
		iic_pkg[11] = iic_info->iic_param.aucParam[1] >> 24;
		break;
	case AVA8_IIC_XFER:
		iic_pkg[0] = 8 + wlen;
		iic_pkg[3] = AVA8_IIC_XFER;
		iic_pkg[4] = wlen;
		iic_pkg[5] = rlen;
		iic_pkg[7] = iic_info->iic_param.slave_addr;
		if (buf && wlen)
			memcpy(iic_pkg + 8, buf, wlen);
		break;
	case AVA8_IIC_RESET:
	case AVA8_IIC_DEINIT:
	case AVA8_IIC_INFO:
		iic_pkg[0] = 4;
		iic_pkg[3] = iic_info->iic_op;
		break;

	default:
		break;
	}

	return 0;
}

static int avalon8_iic_xfer(struct cgpu_info *avalon8, uint8_t slave_addr,
			    uint8_t *wbuf, int wlen,
			    uint8_t *rbuf, int rlen)
{
	struct avalon8_info *info = avalon8->device_data;
	struct i2c_ctx *pctx = NULL;
	int err = 1;
	bool ret = false;

	pctx = info->i2c_slaves[slave_addr];
	if (!pctx) {
		applog(LOG_ERR, "%s-%d: IIC xfer i2c slaves null!", avalon8->drv->name, avalon8->device_id);
		goto out;
	}

	if (wbuf) {
		ret = pctx->write_raw(pctx, wbuf, wlen);
		if (!ret) {
			applog(LOG_DEBUG, "%s-%d: IIC xfer write raw failed!", avalon8->drv->name, avalon8->device_id);
			goto out;
		}
	}

	cgsleep_ms(5);

	if (rbuf) {
		ret = pctx->read_raw(pctx, rbuf, rlen);
		if (!ret) {
			applog(LOG_DEBUG, "%s-%d: IIC xfer read raw failed!", avalon8->drv->name, avalon8->device_id);
			hexdump(rbuf, rlen);
			goto out;
		}
	}

	return 0;
out:
	return err;
}

static int avalon8_auc_xfer(struct cgpu_info *avalon8,
			    uint8_t *wbuf, int wlen, int *write,
			    uint8_t *rbuf, int rlen, int *read)
{
	int err = -1;

	if (unlikely(avalon8->usbinfo.nodev))
		goto out;

	usb_buffer_clear(avalon8);
	err = usb_write(avalon8, (char *)wbuf, wlen, write, C_AVA8_WRITE);
	if (err || *write != wlen) {
		applog(LOG_DEBUG, "%s-%d: AUC xfer %d, w(%d-%d)!", avalon8->drv->name, avalon8->device_id, err, wlen, *write);
		usb_nodev(avalon8);
		goto out;
	}

	cgsleep_ms(opt_avalon8_aucxdelay / 4800 + 1);

	rlen += 4;		/* Add 4 bytes IIC header */
	err = usb_read(avalon8, (char *)rbuf, rlen, read, C_AVA8_READ);
	if (err || *read != rlen || *read != rbuf[0]) {
		applog(LOG_DEBUG, "%s-%d: AUC xfer %d, r(%d-%d-%d)!", avalon8->drv->name, avalon8->device_id, err, rlen - 4, *read, rbuf[0]);
		hexdump(rbuf, rlen);
		return -1;
	}
	*read = rbuf[0] - 4;	/* Remove 4 bytes IIC header */
out:
	return err;
}

static int avalon8_auc_init(struct cgpu_info *avalon8, char *ver)
{
	struct avalon8_iic_info iic_info;
	int err, wlen, rlen;
	uint8_t wbuf[AVA8_AUC_P_SIZE];
	uint8_t rbuf[AVA8_AUC_P_SIZE];

	if (unlikely(avalon8->usbinfo.nodev))
		return 1;

	/* Try to clean the AUC buffer */
	usb_buffer_clear(avalon8);
	err = usb_read(avalon8, (char *)rbuf, AVA8_AUC_P_SIZE, &rlen, C_AVA8_READ);
	applog(LOG_DEBUG, "%s-%d: AUC usb_read %d, %d!", avalon8->drv->name, avalon8->device_id, err, rlen);
	hexdump(rbuf, AVA8_AUC_P_SIZE);

	/* Reset */
	iic_info.iic_op = AVA8_IIC_RESET;
	rlen = 0;
	avalon8_auc_init_pkg(wbuf, &iic_info, NULL, 0, rlen);

	memset(rbuf, 0, AVA8_AUC_P_SIZE);
	err = avalon8_auc_xfer(avalon8, wbuf, AVA8_AUC_P_SIZE, &wlen, rbuf, rlen, &rlen);
	if (err) {
		applog(LOG_ERR, "%s-%d: Failed to reset Avalon USB2IIC Converter", avalon8->drv->name, avalon8->device_id);
		return 1;
	}

	/* Deinit */
	iic_info.iic_op = AVA8_IIC_DEINIT;
	rlen = 0;
	avalon8_auc_init_pkg(wbuf, &iic_info, NULL, 0, rlen);

	memset(rbuf, 0, AVA8_AUC_P_SIZE);
	err = avalon8_auc_xfer(avalon8, wbuf, AVA8_AUC_P_SIZE, &wlen, rbuf, rlen, &rlen);
	if (err) {
		applog(LOG_ERR, "%s-%d: Failed to deinit Avalon USB2IIC Converter", avalon8->drv->name, avalon8->device_id);
		return 1;
	}

	/* Init */
	iic_info.iic_op = AVA8_IIC_INIT;
	iic_info.iic_param.aucParam[0] = opt_avalon8_aucspeed;
	iic_info.iic_param.aucParam[1] = opt_avalon8_aucxdelay;
	rlen = AVA8_AUC_VER_LEN;
	avalon8_auc_init_pkg(wbuf, &iic_info, NULL, 0, rlen);

	memset(rbuf, 0, AVA8_AUC_P_SIZE);
	err = avalon8_auc_xfer(avalon8, wbuf, AVA8_AUC_P_SIZE, &wlen, rbuf, rlen, &rlen);
	if (err) {
		applog(LOG_ERR, "%s-%d: Failed to init Avalon USB2IIC Converter", avalon8->drv->name, avalon8->device_id);
		return 1;
	}

	hexdump(rbuf, AVA8_AUC_P_SIZE);

	memcpy(ver, rbuf + 4, AVA8_AUC_VER_LEN);
	ver[AVA8_AUC_VER_LEN] = '\0';

	applog(LOG_DEBUG, "%s-%d: USB2IIC Converter version: %s!", avalon8->drv->name, avalon8->device_id, ver);

	return 0;
}

static int avalon8_auc_getinfo(struct cgpu_info *avalon8)
{
	struct avalon8_iic_info iic_info;
	int err, wlen, rlen;
	uint8_t wbuf[AVA8_AUC_P_SIZE];
	uint8_t rbuf[AVA8_AUC_P_SIZE];
	uint8_t *pdata = rbuf + 4;
	uint16_t adc_val;
	struct avalon8_info *info = avalon8->device_data;

	iic_info.iic_op = AVA8_IIC_INFO;
	/*
	 * Device info: (9 bytes)
	 * tempadc(2), reqRdIndex, reqWrIndex,
	 * respRdIndex, respWrIndex, tx_flags, state
	 */
	rlen = 7;
	avalon8_auc_init_pkg(wbuf, &iic_info, NULL, 0, rlen);

	memset(rbuf, 0, AVA8_AUC_P_SIZE);
	err = avalon8_auc_xfer(avalon8, wbuf, AVA8_AUC_P_SIZE, &wlen, rbuf, rlen, &rlen);
	if (err) {
		applog(LOG_ERR, "%s-%d: AUC Failed to get info ", avalon8->drv->name, avalon8->device_id);
		return 1;
	}

	applog(LOG_DEBUG, "%s-%d: AUC tempADC(%03d), reqcnt(%d), respcnt(%d), txflag(%d), state(%d)",
			avalon8->drv->name, avalon8->device_id,
			pdata[1] << 8 | pdata[0],
			pdata[2],
			pdata[3],
			pdata[5] << 8 | pdata[4],
			pdata[6]);

	adc_val = pdata[1] << 8 | pdata[0];

	info->auc_sensor = 3.3 * adc_val * 10000 / 1023;

	return 0;
}

static int avalon8_iic_xfer_pkg(struct cgpu_info *avalon8, uint8_t slave_addr,
				const struct avalon8_pkg *pkg, struct avalon8_ret *ret)
{
	struct avalon8_iic_info iic_info;
	int err, wcnt, rcnt, rlen = 0;
	uint8_t wbuf[AVA8_AUC_P_SIZE];
	uint8_t rbuf[AVA8_AUC_P_SIZE];

	struct avalon8_info *info = avalon8->device_data;

	if (ret)
		rlen = AVA8_READ_SIZE;

	if (info->connecter == AVA8_CONNECTER_AUC) {
		if (unlikely(avalon8->usbinfo.nodev))
			return AVA8_SEND_ERROR;

		iic_info.iic_op = AVA8_IIC_XFER;
		iic_info.iic_param.slave_addr = slave_addr;

		avalon8_auc_init_pkg(wbuf, &iic_info, (uint8_t *)pkg, AVA8_WRITE_SIZE, rlen);
		err = avalon8_auc_xfer(avalon8, wbuf, wbuf[0], &wcnt, rbuf, rlen, &rcnt);
		if ((pkg->type != AVA8_P_DETECT) && err == -7 && !rcnt && rlen) {
			avalon8_auc_init_pkg(wbuf, &iic_info, NULL, 0, rlen);
			err = avalon8_auc_xfer(avalon8, wbuf, wbuf[0], &wcnt, rbuf, rlen, &rcnt);
			applog(LOG_DEBUG, "%s-%d-%d: AUC read again!(type:0x%x, err:%d)", avalon8->drv->name, avalon8->device_id, slave_addr, pkg->type, err);
		}
		if (err || rcnt != rlen) {
			if (info->xfer_err_cnt++ == 100) {
				applog(LOG_DEBUG, "%s-%d-%d: AUC xfer_err_cnt reach err = %d, rcnt = %d, rlen = %d",
						avalon8->drv->name, avalon8->device_id, slave_addr,
						err, rcnt, rlen);

				cgsleep_ms(5 * 1000); /* Wait MM reset */
				if (avalon8_auc_init(avalon8, info->auc_version)) {
					applog(LOG_WARNING, "%s-%d: Failed to re-init auc, unplugging for new hotplug",
					       avalon8->drv->name, avalon8->device_id);
					usb_nodev(avalon8);
				}
			}
			return AVA8_SEND_ERROR;
		}

		if (ret)
			memcpy((char *)ret, rbuf + 4, AVA8_READ_SIZE);

		info->xfer_err_cnt = 0;
	}

	if (info->connecter == AVA8_CONNECTER_IIC) {
		err = avalon8_iic_xfer(avalon8, slave_addr, (uint8_t *)pkg, AVA8_WRITE_SIZE, (uint8_t *)ret, AVA8_READ_SIZE);
		if ((pkg->type != AVA8_P_DETECT) && err) {
			err = avalon8_iic_xfer(avalon8, slave_addr, (uint8_t *)pkg, AVA8_WRITE_SIZE, (uint8_t *)ret, AVA8_READ_SIZE);
			applog(LOG_DEBUG, "%s-%d-%d: IIC read again!(type:0x%x, err:%d)", avalon8->drv->name, avalon8->device_id, slave_addr, pkg->type, err);
		}
		if (err) {
			/* FIXME: Don't care broadcast message with no reply, or it will block other thread when called by avalon8_send_bc_pkgs */
			if ((pkg->type != AVA8_P_DETECT) && (slave_addr == AVA8_MODULE_BROADCAST))
				return AVA8_SEND_OK;

			if (info->xfer_err_cnt++ == 100) {
				info->xfer_err_cnt = 0;
				applog(LOG_DEBUG, "%s-%d-%d: IIC xfer_err_cnt reach err = %d, rcnt = %d, rlen = %d",
						avalon8->drv->name, avalon8->device_id, slave_addr,
						err, rcnt, rlen);

				cgsleep_ms(5 * 1000); /* Wait MM reset */
			}
			return AVA8_SEND_ERROR;
		}

		info->xfer_err_cnt = 0;
	}

	return AVA8_SEND_OK;
}

static int avalon8_send_bc_pkgs(struct cgpu_info *avalon8, const struct avalon8_pkg *pkg)
{
	int ret;

	do {
		ret = avalon8_iic_xfer_pkg(avalon8, AVA8_MODULE_BROADCAST, pkg, NULL);
	} while (ret != AVA8_SEND_OK);

	return 0;
}

static void avalon8_stratum_pkgs(struct cgpu_info *avalon8, struct pool *pool)
{
	struct avalon8_info *info = avalon8->device_data;
	const int merkle_offset = 36;
	struct avalon8_pkg pkg;
	int i, a, b;
	uint32_t tmp;
	unsigned char target[32];
	int job_id_len, n2size;
	unsigned short crc;
	int coinbase_len_posthash, coinbase_len_prehash;
	uint8_t coinbase_prehash[32];
	uint32_t range, start;

	/* Send out the first stratum message STATIC */
	applog(LOG_DEBUG, "%s-%d: Pool stratum message STATIC: %d, %d, %d, %d, %d",
	       avalon8->drv->name, avalon8->device_id,
	       pool->coinbase_len,
	       pool->nonce2_offset,
	       pool->n2size,
	       merkle_offset,
	       pool->merkles);
	memset(pkg.data, 0, AVA8_P_DATA_LEN);
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

	if (pool->n2size == 3)
		range = 0xffffff / (total_devices ? total_devices : 1);
	else
		range = 0xffffffff / (total_devices ? total_devices : 1);
	start = range * avalon8->device_id;

	tmp = be32toh(start);
	memcpy(pkg.data + 20, &tmp, 4);

	tmp = be32toh(range);
	memcpy(pkg.data + 24, &tmp, 4);

	if (info->work_restart) {
		info->work_restart = false;
		tmp = be32toh(0x1);
		memcpy(pkg.data + 28, &tmp, 4);
	}

	avalon8_init_pkg(&pkg, AVA8_P_STATIC, 1, 1);
	if (avalon8_send_bc_pkgs(avalon8, &pkg))
		return;

	if (pool->sdiff <= AVA8_DRV_DIFFMAX)
		set_target(target, pool->sdiff);
	else
		set_target(target, AVA8_DRV_DIFFMAX);

	memcpy(pkg.data, target, 32);
	if (opt_debug) {
		char *target_str;
		target_str = bin2hex(target, 32);
		applog(LOG_DEBUG, "%s-%d: Pool stratum target: %s", avalon8->drv->name, avalon8->device_id, target_str);
		free(target_str);
	}
	avalon8_init_pkg(&pkg, AVA8_P_TARGET, 1, 1);
	if (avalon8_send_bc_pkgs(avalon8, &pkg))
		return;

	memset(pkg.data, 0, AVA8_P_DATA_LEN);

	job_id_len = strlen(pool->swork.job_id);
	crc = crc16((unsigned char *)pool->swork.job_id, job_id_len);
	applog(LOG_DEBUG, "%s-%d: Pool stratum message JOBS_ID[%04x]: %s",
	       avalon8->drv->name, avalon8->device_id,
	       crc, pool->swork.job_id);
	tmp = ((crc << 16) | pool->pool_no);
	if (info->last_jobid != tmp) {
		info->last_jobid = tmp;
		pkg.data[0] = (crc & 0xff00) >> 8;
		pkg.data[1] = crc & 0xff;
		pkg.data[2] = pool->pool_no & 0xff;
		pkg.data[3] = (pool->pool_no & 0xff00) >> 8;
		avalon8_init_pkg(&pkg, AVA8_P_JOB_ID, 1, 1);
		if (avalon8_send_bc_pkgs(avalon8, &pkg))
			return;
	}

	coinbase_len_prehash = pool->nonce2_offset - (pool->nonce2_offset % SHA256_BLOCK_SIZE);
	coinbase_len_posthash = pool->coinbase_len - coinbase_len_prehash;
	sha256_prehash(pool->coinbase, coinbase_len_prehash, coinbase_prehash);

	a = (coinbase_len_posthash / AVA8_P_DATA_LEN) + 1;
	b = coinbase_len_posthash % AVA8_P_DATA_LEN;
	memcpy(pkg.data, coinbase_prehash, 32);
	avalon8_init_pkg(&pkg, AVA8_P_COINBASE, 1, a + (b ? 1 : 0));
	if (avalon8_send_bc_pkgs(avalon8, &pkg))
		return;

	applog(LOG_DEBUG, "%s-%d: Pool stratum message modified COINBASE: %d %d",
			avalon8->drv->name, avalon8->device_id,
			a, b);
	for (i = 1; i < a; i++) {
		memcpy(pkg.data, pool->coinbase + coinbase_len_prehash + i * 32 - 32, 32);
		avalon8_init_pkg(&pkg, AVA8_P_COINBASE, i + 1, a + (b ? 1 : 0));
		if (avalon8_send_bc_pkgs(avalon8, &pkg))
			return;
	}
	if (b) {
		memset(pkg.data, 0, AVA8_P_DATA_LEN);
		memcpy(pkg.data, pool->coinbase + coinbase_len_prehash + i * 32 - 32, b);
		avalon8_init_pkg(&pkg, AVA8_P_COINBASE, i + 1, i + 1);
		if (avalon8_send_bc_pkgs(avalon8, &pkg))
			return;
	}

	b = pool->merkles;
	applog(LOG_DEBUG, "%s-%d: Pool stratum message MERKLES: %d", avalon8->drv->name, avalon8->device_id, b);
	for (i = 0; i < b; i++) {
		memset(pkg.data, 0, AVA8_P_DATA_LEN);
		memcpy(pkg.data, pool->swork.merkle_bin[i], 32);
		avalon8_init_pkg(&pkg, AVA8_P_MERKLES, i + 1, b);
		if (avalon8_send_bc_pkgs(avalon8, &pkg))
			return;
	}

	applog(LOG_DEBUG, "%s-%d: Pool stratum message HEADER: 4", avalon8->drv->name, avalon8->device_id);
	for (i = 0; i < 4; i++) {
		memset(pkg.data, 0, AVA8_P_DATA_LEN);
		memcpy(pkg.data, pool->header_bin + i * 32, 32);
		avalon8_init_pkg(&pkg, AVA8_P_HEADER, i + 1, 4);
		if (avalon8_send_bc_pkgs(avalon8, &pkg))
			return;
	}

	if (info->connecter == AVA8_CONNECTER_AUC)
		avalon8_auc_getinfo(avalon8);
}

static struct cgpu_info *avalon8_iic_detect(void)
{
	int i;
	struct avalon8_info *info;
	struct cgpu_info *avalon8 = NULL;
	struct i2c_ctx *i2c_slave = NULL;

	i2c_slave = i2c_slave_open(I2C_BUS, 0);
	if (!i2c_slave) {
		applog(LOG_ERR, "avalon8 init iic failed\n");
		return NULL;
	}

	i2c_slave->exit(i2c_slave);
	i2c_slave = NULL;

	avalon8 = cgcalloc(1, sizeof(*avalon8));
	avalon8->drv = &avalon8_drv;
	avalon8->deven = DEV_ENABLED;
	avalon8->threads = 1;
	add_cgpu(avalon8);

	applog(LOG_INFO, "%s-%d: Found at %s", avalon8->drv->name, avalon8->device_id,
	       I2C_BUS);

	avalon8->device_data = cgcalloc(sizeof(struct avalon8_info), 1);
	memset(avalon8->device_data, 0, sizeof(struct avalon8_info));
	info = avalon8->device_data;

	for (i = 0; i < AVA8_DEFAULT_MODULARS; i++) {
		info->enable[i] = false;
		info->reboot[i] = false;
		info->i2c_slaves[i] = i2c_slave_open(I2C_BUS, i);
		if (!info->i2c_slaves[i]) {
			applog(LOG_ERR, "avalon8 init i2c slaves failed\n");
			free(avalon8->device_data);
			avalon8->device_data = NULL;
			free(avalon8);
			avalon8 = NULL;
			return NULL;
		}
	}

	info->connecter = AVA8_CONNECTER_IIC;

	return avalon8;
}

static void detect_modules(struct cgpu_info *avalon8);

static struct cgpu_info *avalon8_auc_detect(struct libusb_device *dev, struct usb_find_devices *found)
{
	int i, modules = 0;
	struct avalon8_info *info;
	struct cgpu_info *avalon8 = usb_alloc_cgpu(&avalon8_drv, 1);
	char auc_ver[AVA8_AUC_VER_LEN];

	if (!usb_init(avalon8, dev, found)) {
		applog(LOG_ERR, "avalon8 failed usb_init");
		avalon8 = usb_free_cgpu(avalon8);
		return NULL;
	}

	/* avalon8 prefers not to use zero length packets */
	avalon8->nozlp = true;

	/* We try twice on AUC init */
	if (avalon8_auc_init(avalon8, auc_ver) && avalon8_auc_init(avalon8, auc_ver))
		return NULL;

	applog(LOG_INFO, "%s-%d: Found at %s", avalon8->drv->name, avalon8->device_id,
	       avalon8->device_path);

	avalon8->device_data = cgcalloc(sizeof(struct avalon8_info), 1);
	memset(avalon8->device_data, 0, sizeof(struct avalon8_info));
	info = avalon8->device_data;
	memcpy(info->auc_version, auc_ver, AVA8_AUC_VER_LEN);
	info->auc_version[AVA8_AUC_VER_LEN] = '\0';
	info->auc_speed = opt_avalon8_aucspeed;
	info->auc_xdelay = opt_avalon8_aucxdelay;

	for (i = 0; i < AVA8_DEFAULT_MODULARS; i++)
		info->enable[i] = 0;

	info->connecter = AVA8_CONNECTER_AUC;

	detect_modules(avalon8);
	for (i = 0; i < AVA8_DEFAULT_MODULARS; i++)
		modules += info->enable[i];

	if (!modules) {
		applog(LOG_INFO, "avalon8 found but no modules initialised");
		free(info);
		avalon8 = usb_free_cgpu(avalon8);
		return NULL;
	}

	/* We have an avalon8 AUC connected */
	avalon8->threads = 1;
	add_cgpu(avalon8);

	update_usb_stats(avalon8);

	return avalon8;
}

static inline void avalon8_detect(bool __maybe_unused hotplug)
{
	usb_detect(&avalon8_drv, avalon8_auc_detect);
	if (!hotplug && opt_avalon8_iic_detect)
		avalon8_iic_detect();
}

static bool avalon8_prepare(struct thr_info *thr)
{
	struct cgpu_info *avalon8 = thr->cgpu;
	struct avalon8_info *info = avalon8->device_data;

	info->last_diff1 = 0;
	info->pending_diff1 = 0;
	info->last_rej = 0;
	info->mm_count = 0;
	info->xfer_err_cnt = 0;
	info->pool_no = 0;

	memset(&(info->firsthash), 0, sizeof(info->firsthash));
	cgtime(&(info->last_fan_adj));
	cgtime(&info->last_stratum);
	cgtime(&info->last_detect);

	cglock_init(&info->update_lock);
	cglock_init(&info->pool0.data_lock);
	cglock_init(&info->pool1.data_lock);
	cglock_init(&info->pool2.data_lock);

	return true;
}

static int check_module_exist(struct cgpu_info *avalon8, uint8_t mm_dna[AVA8_MM_DNA_LEN])
{
	struct avalon8_info *info = avalon8->device_data;
	int i;

	for (i = 0; i < AVA8_DEFAULT_MODULARS; i++) {
		/* last byte is \0 */
		if (info->enable[i] && !memcmp(info->mm_dna[i], mm_dna, AVA8_MM_DNA_LEN))
			return 1;
	}

	return 0;
}

static void detect_modules(struct cgpu_info *avalon8)
{
	struct avalon8_info *info = avalon8->device_data;
	struct avalon8_pkg send_pkg;
	struct avalon8_ret ret_pkg;
	uint32_t tmp;
	int i, j, k, err, rlen;
	uint8_t dev_index;
	uint8_t rbuf[AVA8_AUC_P_SIZE];

	/* Detect new modules here */
	for (i = 1; i < AVA8_DEFAULT_MODULARS + 1; i++) {
		if (info->enable[i])
			continue;

		/* Send out detect pkg */
		applog(LOG_DEBUG, "%s-%d: AVA8_P_DETECT ID[%d]",
		       avalon8->drv->name, avalon8->device_id, i);
		memset(send_pkg.data, 0, AVA8_P_DATA_LEN);
		tmp = be32toh(i); /* ID */
		memcpy(send_pkg.data + 28, &tmp, 4);
		avalon8_init_pkg(&send_pkg, AVA8_P_DETECT, 1, 1);
		err = avalon8_iic_xfer_pkg(avalon8, AVA8_MODULE_BROADCAST, &send_pkg, &ret_pkg);
		if (err == AVA8_SEND_OK) {
			if (decode_pkg(avalon8, &ret_pkg, AVA8_MODULE_BROADCAST)) {
				applog(LOG_DEBUG, "%s-%d: Should be AVA8_P_ACKDETECT(%d), but %d",
				       avalon8->drv->name, avalon8->device_id, AVA8_P_ACKDETECT, ret_pkg.type);
				continue;
			}
		}

		if (err != AVA8_SEND_OK) {
			applog(LOG_DEBUG, "%s-%d: AVA8_P_DETECT: Failed AUC xfer data with err %d",
					avalon8->drv->name, avalon8->device_id, err);
			break;
		}

		applog(LOG_DEBUG, "%s-%d: Module detect ID[%d]: %d",
		       avalon8->drv->name, avalon8->device_id, i, ret_pkg.type);
		if (ret_pkg.type != AVA8_P_ACKDETECT)
			break;

		if (check_module_exist(avalon8, ret_pkg.data))
			continue;

		/* Check count of modulars */
		if (i == AVA8_DEFAULT_MODULARS) {
			applog(LOG_NOTICE, "You have connected more than %d machines. This is discouraged.", (AVA8_DEFAULT_MODULARS - 1));
			info->conn_overloaded = true;
			break;
		} else
			info->conn_overloaded = false;

		memcpy(info->mm_version[i], ret_pkg.data + AVA8_MM_DNA_LEN, AVA8_MM_VER_LEN);
		info->mm_version[i][AVA8_MM_VER_LEN] = '\0';
		for (dev_index = 0; dev_index < (sizeof(avalon8_dev_table) / sizeof(avalon8_dev_table[0])); dev_index++) {
			if (!strncmp((char *)&(info->mm_version[i]), (char *)(avalon8_dev_table[dev_index].dev_id_str), 3)) {
				info->mod_type[i] = avalon8_dev_table[dev_index].mod_type;
				info->miner_count[i] = avalon8_dev_table[dev_index].miner_count;
				info->asic_count[i] = avalon8_dev_table[dev_index].asic_count;
				info->vin_adc_ratio[i] = avalon8_dev_table[dev_index].vin_adc_ratio;
				info->vout_adc_ratio[i] = avalon8_dev_table[dev_index].vout_adc_ratio;
				break;
			}
		}
		if (dev_index == (sizeof(avalon8_dev_table) / sizeof(avalon8_dev_table[0]))) {
			applog(LOG_NOTICE, "%s-%d: The modular version %s cann't be support",
				       avalon8->drv->name, avalon8->device_id, info->mm_version[i]);
			break;
		}

		info->enable[i] = 1;
		cgtime(&info->elapsed[i]);
		memcpy(info->mm_dna[i], ret_pkg.data, AVA8_MM_DNA_LEN);
		memcpy(&tmp, ret_pkg.data + AVA8_MM_DNA_LEN + AVA8_MM_VER_LEN, 4);
		tmp = be32toh(tmp);
		info->total_asics[i] = tmp;
		info->temp_overheat[i] = AVA8_DEFAULT_TEMP_OVERHEAT;
		info->temp_target[i] = opt_avalon8_temp_target;
		info->fan_pct[i] = opt_avalon8_fan_min;
		for (j = 0; j < info->miner_count[i]; j++) {
			if (opt_avalon8_voltage_level == AVA8_INVALID_VOLTAGE_LEVEL)
				info->set_voltage_level[i][j] = avalon8_dev_table[dev_index].set_voltage_level;
			else
				info->set_voltage_level[i][j] = opt_avalon8_voltage_level;
			info->get_voltage[i][j] = 0;
			info->get_vin[i][j] = 0;

			for (k = 0; k < info->asic_count[i]; k++)
				info->temp[i][j][k] = -273;

			for (k = 0; k < AVA8_DEFAULT_PLL_CNT; k++)
				info->set_frequency[i][j][k] = avalon8_dev_table[dev_index].set_freq[k];

			if (AVA8_INVALID_ASIC_OTP == opt_avalon8_asic_otp)
				info->set_asic_otp[i][j] = 0; /* default asic: 0 */
			else
				info->set_asic_otp[i][j] = opt_avalon8_asic_otp;
		}

		info->freq_mode[i] = AVA8_FREQ_INIT_MODE;
		memset(info->get_pll[i], 0, sizeof(uint32_t) * info->miner_count[i] * AVA8_DEFAULT_PLL_CNT);

		info->led_indicator[i] = 0;
		info->cutoff[i] = 0;
		info->fan_cpm[i] = 0;
		info->temp_mm[i] = -273;
		info->local_works[i] = 0;
		info->hw_works[i] = 0;

		/*PID controller*/
		info->pid_u[i] = opt_avalon8_fan_min;
		info->pid_p[i] = opt_avalon8_pid_p;
		info->pid_i[i] = opt_avalon8_pid_i;
		info->pid_d[i] = opt_avalon8_pid_d;
		info->pid_e[i][0] = 0;
		info->pid_e[i][1] = 0;
		info->pid_e[i][2] = 0;
		info->pid_0[i] = 0;

		for (j = 0; j < info->miner_count[i]; j++) {
			memset(info->chip_matching_work[i][j], 0, sizeof(uint64_t) * info->asic_count[i]);
			info->local_works_i[i][j] = 0;
			info->hw_works_i[i][j] = 0;
			info->error_code[i][j] = 0;
			info->error_crc[i][j] = 0;
		}
		info->error_code[i][j] = 0;
		info->error_polling_cnt[i] = 0;
		info->power_good[i] = 0;
		memset(info->pmu_version[i], 0, sizeof(char) * 5 * AVA8_DEFAULT_PMU_CNT);
		info->diff1[i] = 0;

		applog(LOG_NOTICE, "%s-%d: New module detected! ID[%d-%x]",
		       avalon8->drv->name, avalon8->device_id, i, info->mm_dna[i][AVA8_MM_DNA_LEN - 1]);

		/* Tell MM, it has been detected */
		memset(send_pkg.data, 0, AVA8_P_DATA_LEN);
		memcpy(send_pkg.data, info->mm_dna[i],  AVA8_MM_DNA_LEN);
		avalon8_init_pkg(&send_pkg, AVA8_P_SYNC, 1, 1);
		avalon8_iic_xfer_pkg(avalon8, i, &send_pkg, &ret_pkg);
		/* Keep the usb buffer is empty */
		usb_buffer_clear(avalon8);
		usb_read(avalon8, (char *)rbuf, AVA8_AUC_P_SIZE, &rlen, C_AVA8_READ);
	}
}

static void detach_module(struct cgpu_info *avalon8, int addr)
{
	struct avalon8_info *info = avalon8->device_data;

	info->enable[addr] = 0;
	applog(LOG_NOTICE, "%s-%d: Module detached! ID[%d]",
		avalon8->drv->name, avalon8->device_id, addr);
}

static int polling(struct cgpu_info *avalon8)
{
	struct avalon8_info *info = avalon8->device_data;
	struct avalon8_pkg send_pkg;
	struct avalon8_ret ar;
	int i, tmp, ret, decode_err = 0;
	struct timeval current_fan;
	int do_adjust_fan = 0;
	uint32_t fan_pwm;
	double device_tdiff;

	cgtime(&current_fan);
	device_tdiff = tdiff(&current_fan, &(info->last_fan_adj));
	if (device_tdiff > 2.0 || device_tdiff < 0) {
		cgtime(&info->last_fan_adj);
		do_adjust_fan = 1;
	}

	for (i = 1; i < AVA8_DEFAULT_MODULARS; i++) {
		if (!info->enable[i])
			continue;

		cgsleep_ms(opt_avalon8_polling_delay);

		memset(send_pkg.data, 0, AVA8_P_DATA_LEN);
		/* Red LED */
		tmp = be32toh(info->led_indicator[i]);
		memcpy(send_pkg.data, &tmp, 4);

		/* Adjust fan every 2 seconds*/
		if (do_adjust_fan) {
			fan_pwm = adjust_fan(info, i);
			fan_pwm |= 0x80000000;
			tmp = be32toh(fan_pwm);
			memcpy(send_pkg.data + 4, &tmp, 4);
		}

		if (info->reboot[i]) {
			info->reboot[i] = false;
			send_pkg.data[8] = 0x1;
		}

		avalon8_init_pkg(&send_pkg, AVA8_P_POLLING, 1, 1);
		ret = avalon8_iic_xfer_pkg(avalon8, i, &send_pkg, &ar);
		if (ret == AVA8_SEND_OK)
			decode_err = decode_pkg(avalon8, &ar, i);

		if (ret != AVA8_SEND_OK || decode_err) {
			info->error_polling_cnt[i]++;
			memset(send_pkg.data, 0, AVA8_P_DATA_LEN);
			avalon8_init_pkg(&send_pkg, AVA8_P_RSTMMTX, 1, 1);
			avalon8_iic_xfer_pkg(avalon8, i, &send_pkg, NULL);
			if (info->error_polling_cnt[i] >= 10)
				detach_module(avalon8, i);
		}

		if (ret == AVA8_SEND_OK && !decode_err) {
			info->error_polling_cnt[i] = 0;

			if ((ar.opt == AVA8_P_STATUS) &&
				(info->mm_dna[i][AVA8_MM_DNA_LEN - 1] != ar.opt)) {
				applog(LOG_ERR, "%s-%d-%d: Dup address found %d-%d",
						avalon8->drv->name, avalon8->device_id, i,
						info->mm_dna[i][AVA8_MM_DNA_LEN - 1], ar.opt);
				hexdump((uint8_t *)&ar, sizeof(ar));
				detach_module(avalon8, i);
			}
		}
	}

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

static void avalon8_init_setting(struct cgpu_info *avalon8, int addr)
{
	struct avalon8_pkg send_pkg;
	uint32_t tmp;

	memset(send_pkg.data, 0, AVA8_P_DATA_LEN);

	tmp = be32toh(opt_avalon8_freq_sel);
	memcpy(send_pkg.data + 4, &tmp, 4);

	/*
	 * set flags:
	 * 0: ss switch
	 * 1: nonce check
	 * 2: roll enable
	 */
	tmp = 1;
	if (!opt_avalon8_smart_speed)
	      tmp = 0;
	tmp |= (opt_avalon8_nonce_check << 1);
	tmp |= (opt_avalon8_roll_enable << 2);
	send_pkg.data[8] = tmp & 0xff;
	send_pkg.data[9] = opt_avalon8_nonce_mask & 0xff;

	tmp = be32toh(opt_avalon8_mux_l2h);
	memcpy(send_pkg.data + 10, &tmp, 4);
	applog(LOG_DEBUG, "%s-%d-%d: avalon8 set mux l2h %u",
			avalon8->drv->name, avalon8->device_id, addr,
			opt_avalon8_mux_l2h);

	tmp = be32toh(opt_avalon8_mux_h2l);
	memcpy(send_pkg.data + 14, &tmp, 4);
	applog(LOG_DEBUG, "%s-%d-%d: avalon8 set mux h2l %u",
			avalon8->drv->name, avalon8->device_id, addr,
			opt_avalon8_mux_h2l);

	tmp = be32toh(opt_avalon8_h2ltime0_spd);
	memcpy(send_pkg.data + 18, &tmp, 4);
	applog(LOG_DEBUG, "%s-%d-%d: avalon8 set h2ltime0 spd %u",
			avalon8->drv->name, avalon8->device_id, addr,
			opt_avalon8_h2ltime0_spd);

	tmp = be32toh(opt_avalon8_spdlow);
	memcpy(send_pkg.data + 22, &tmp, 4);
	applog(LOG_DEBUG, "%s-%d-%d: avalon8 set spdlow %u",
			avalon8->drv->name, avalon8->device_id, addr,
			opt_avalon8_spdlow);

	tmp = be32toh(opt_avalon8_spdhigh);
	memcpy(send_pkg.data + 26, &tmp, 4);
	applog(LOG_DEBUG, "%s-%d-%d: avalon8 set spdhigh %u",
			avalon8->drv->name, avalon8->device_id, addr,
			opt_avalon8_spdhigh);

	/* Package the data */
	avalon8_init_pkg(&send_pkg, AVA8_P_SET, 1, 1);
	if (addr == AVA8_MODULE_BROADCAST)
		avalon8_send_bc_pkgs(avalon8, &send_pkg);
	else
		avalon8_iic_xfer_pkg(avalon8, addr, &send_pkg, NULL);
}

static void avalon8_set_voltage_level(struct cgpu_info *avalon8, int addr, unsigned int voltage[])
{
	struct avalon8_info *info = avalon8->device_data;
	struct avalon8_pkg send_pkg;
	uint32_t tmp;
	uint8_t i;

	memset(send_pkg.data, 0, AVA8_P_DATA_LEN);

	/* NOTE: miner_count should <= 8 */
	for (i = 0; i < info->miner_count[addr]; i++) {
		tmp = be32toh(encode_voltage(voltage[i] +
				opt_avalon8_voltage_level_offset));
		memcpy(send_pkg.data + i * 4, &tmp, 4);
	}
	applog(LOG_DEBUG, "%s-%d-%d: avalon8 set voltage miner %d, (%d-%d)",
			avalon8->drv->name, avalon8->device_id, addr,
			i, voltage[0], voltage[info->miner_count[addr] - 1]);

	/* Package the data */
	avalon8_init_pkg(&send_pkg, AVA8_P_SET_VOLT, 1, 1);
	if (addr == AVA8_MODULE_BROADCAST)
		avalon8_send_bc_pkgs(avalon8, &send_pkg);
	else
		avalon8_iic_xfer_pkg(avalon8, addr, &send_pkg, NULL);
}

static void avalon8_set_asic_otp(struct cgpu_info *avalon8, int addr, unsigned int asic[])
{
	struct avalon8_info *info = avalon8->device_data;
	struct avalon8_pkg send_pkg;
	uint32_t tmp, core_sel;
	uint8_t i;

	memset(send_pkg.data, 0, AVA8_P_DATA_LEN);

	/* NOTE: miner_count should <= 8 */
	for (i = 0; i < info->miner_count[addr]; i++) {
		if (asic[i] < 0)
		      asic[i] = 0;
		else if (asic[i] > (AVA8_DEFAULT_ASIC_MAX -1))
			asic[i] = AVA8_DEFAULT_ASIC_MAX - 1;
		tmp = be32toh(asic[i]);
		memcpy(send_pkg.data + i * 4, &tmp, 4);
	}
	applog(LOG_DEBUG, "%s-%d-%d: avalon8 set asic for otp reading %d, (%d-%d)",
			avalon8->drv->name, avalon8->device_id, addr,
			i, asic[0], asic[info->miner_count[addr] - 1]);

	/* Package the data */
	avalon8_init_pkg(&send_pkg, AVA8_P_SET_ASIC_OTP, 1, 1);
	if (addr == AVA8_MODULE_BROADCAST)
		avalon8_send_bc_pkgs(avalon8, &send_pkg);
	else
		avalon8_iic_xfer_pkg(avalon8, addr, &send_pkg, NULL);
}

static void avalon8_set_freq(struct cgpu_info *avalon8, int addr, int miner_id, unsigned int freq[])
{
	struct avalon8_info *info = avalon8->device_data;
	struct avalon8_pkg send_pkg;
	uint32_t tmp, f;
	uint8_t i;

	memset(send_pkg.data, 0, AVA8_P_DATA_LEN);
	for (i = 0; i < AVA8_DEFAULT_PLL_CNT; i++) {
		tmp = be32toh(api_get_cpm(freq[i]));
		memcpy(send_pkg.data + i * 4, &tmp, 4);
	}

	f = freq[0];
	for (i = 1; i < AVA8_DEFAULT_PLL_CNT; i++)
		f = f > freq[i] ? f : freq[i];

	f = f ? f : 1;

	/* TODO: adjust it according to frequency */
	tmp = 100;
	tmp = be32toh(tmp);
	memcpy(send_pkg.data + AVA8_DEFAULT_PLL_CNT * 4, &tmp, 4);

	tmp = AVA8_ASIC_TIMEOUT_CONST / f * 83 / 100;
	tmp = be32toh(tmp);
	memcpy(send_pkg.data + AVA8_DEFAULT_PLL_CNT * 4 + 4, &tmp, 4);
	applog(LOG_DEBUG, "%s-%d-%d: avalon8 set freq miner %x-%x",
			avalon8->drv->name, avalon8->device_id, addr,
			miner_id, be32toh(tmp));

	/* Package the data */
	avalon8_init_pkg(&send_pkg, AVA8_P_SET_PLL, miner_id + 1, info->miner_count[addr]);

	if (addr == AVA8_MODULE_BROADCAST)
		avalon8_send_bc_pkgs(avalon8, &send_pkg);
	else
		avalon8_iic_xfer_pkg(avalon8, addr, &send_pkg, NULL);
}

static void avalon8_set_factory_info(struct cgpu_info *avalon8, int addr, uint8_t value[])
{
	struct avalon8_pkg send_pkg;
	uint8_t i;

	memset(send_pkg.data, 0, AVA8_P_DATA_LEN);

	for (i = 0; i < AVA8_DEFAULT_FACTORY_INFO_CNT; i++)
	      send_pkg.data[i] = value[i];

	/* Package the data */
	avalon8_init_pkg(&send_pkg, AVA8_P_SET_FAC, 1, 1);
	if (addr == AVA8_MODULE_BROADCAST)
		avalon8_send_bc_pkgs(avalon8, &send_pkg);
	else
		avalon8_iic_xfer_pkg(avalon8, addr, &send_pkg, NULL);
}

static void avalon8_set_overclocking_info(struct cgpu_info *avalon8, int addr, uint8_t value[])
{
	struct avalon8_pkg send_pkg;
	uint8_t i;

	memset(send_pkg.data, 0, AVA8_P_DATA_LEN);

	for (i = 0; i < AVA8_DEFAULT_OVERCLOCKING_CNT; i++)
		send_pkg.data[i] = value[i];

	/* Package the data */
	avalon8_init_pkg(&send_pkg, AVA8_P_SET_OC, 1, 1);
	if (addr == AVA8_MODULE_BROADCAST)
		avalon8_send_bc_pkgs(avalon8, &send_pkg);
	else
		avalon8_iic_xfer_pkg(avalon8, addr, &send_pkg, NULL);
}

static void avalon8_set_ss_param(struct cgpu_info *avalon8, int addr)
{
	struct avalon8_pkg send_pkg;
	uint32_t tmp;

	if (!opt_avalon8_smart_speed)
		return;

	memset(send_pkg.data, 0, AVA8_P_DATA_LEN);

	tmp = be32toh(opt_avalon8_th_pass);
	memcpy(send_pkg.data, &tmp, 4);
	applog(LOG_DEBUG, "%s-%d-%d: avalon8 set th pass %u",
			avalon8->drv->name, avalon8->device_id, addr,
			opt_avalon8_th_pass);

	tmp = be32toh(opt_avalon8_th_fail);
	memcpy(send_pkg.data + 4, &tmp, 4);
	applog(LOG_DEBUG, "%s-%d-%d: avalon8 set th fail %u",
			avalon8->drv->name, avalon8->device_id, addr,
			opt_avalon8_th_fail);

	tmp = be32toh(opt_avalon8_th_init);
	memcpy(send_pkg.data + 8, &tmp, 4);
	applog(LOG_DEBUG, "%s-%d-%d: avalon8 set th init %u",
			avalon8->drv->name, avalon8->device_id, addr,
			opt_avalon8_th_init);

	tmp = be32toh(opt_avalon8_th_ms);
	memcpy(send_pkg.data + 12, &tmp, 4);
	applog(LOG_DEBUG, "%s-%d-%d: avalon8 set th ms %u",
			avalon8->drv->name, avalon8->device_id, addr,
			opt_avalon8_th_ms);

	tmp = be32toh(opt_avalon8_th_timeout);
	memcpy(send_pkg.data + 16, &tmp, 4);
	applog(LOG_DEBUG, "%s-%d-%d: avalon8 set th timeout %u",
			avalon8->drv->name, avalon8->device_id, addr,
			opt_avalon8_th_timeout);

	tmp = be32toh(opt_avalon8_th_add);
	memcpy(send_pkg.data + 20, &tmp, 4);
	applog(LOG_DEBUG, "%s-%d-%d: avalon8 set th add %u",
			avalon8->drv->name, avalon8->device_id, addr,
			opt_avalon8_th_add);

	/* Package the data */
	avalon8_init_pkg(&send_pkg, AVA8_P_SET_SS, 1, 1);

	if (addr == AVA8_MODULE_BROADCAST)
		avalon8_send_bc_pkgs(avalon8, &send_pkg);
	else
		avalon8_iic_xfer_pkg(avalon8, addr, &send_pkg, NULL);
}

static void avalon8_stratum_finish(struct cgpu_info *avalon8)
{
	struct avalon8_pkg send_pkg;

	memset(send_pkg.data, 0, AVA8_P_DATA_LEN);
	avalon8_init_pkg(&send_pkg, AVA8_P_JOB_FIN, 1, 1);
	avalon8_send_bc_pkgs(avalon8, &send_pkg);
}

static void avalon8_set_finish(struct cgpu_info *avalon8, int addr)
{
	struct avalon8_pkg send_pkg;

	memset(send_pkg.data, 0, AVA8_P_DATA_LEN);
	avalon8_init_pkg(&send_pkg, AVA8_P_SET_FIN, 1, 1);
	avalon8_iic_xfer_pkg(avalon8, addr, &send_pkg, NULL);
}

static void avalon8_sswork_update(struct cgpu_info *avalon8)
{
	struct avalon8_info *info = avalon8->device_data;
	struct thr_info *thr = avalon8->thr[0];
	struct pool *pool;
	int coinbase_len_posthash, coinbase_len_prehash;

	cgtime(&info->last_stratum);
	/*
	 * NOTE: We need mark work_restart to private information,
	 * So that it cann't reset by hash_driver_work
	 */
	if (thr->work_restart)
		info->work_restart = thr->work_restart;
	applog(LOG_NOTICE, "%s-%d: New stratum: restart: %d, update: %d",
	       avalon8->drv->name, avalon8->device_id,
	       thr->work_restart, thr->work_update);

	/* Step 1: MM protocol check */
	pool = current_pool();
	if (!pool->has_stratum)
		quit(1, "%s-%d: MM has to use stratum pools", avalon8->drv->name, avalon8->device_id);

	coinbase_len_prehash = pool->nonce2_offset - (pool->nonce2_offset % SHA256_BLOCK_SIZE);
	coinbase_len_posthash = pool->coinbase_len - coinbase_len_prehash;

	if (coinbase_len_posthash + SHA256_BLOCK_SIZE > AVA8_P_COINBASE_SIZE) {
		applog(LOG_ERR, "%s-%d: MM pool modified coinbase length(%d) is more than %d",
		       avalon8->drv->name, avalon8->device_id,
		       coinbase_len_posthash + SHA256_BLOCK_SIZE, AVA8_P_COINBASE_SIZE);
		return;
	}
	if (pool->merkles > AVA8_P_MERKLES_COUNT) {
		applog(LOG_ERR, "%s-%d: MM merkles has to be less then %d", avalon8->drv->name, avalon8->device_id, AVA8_P_MERKLES_COUNT);
		return;
	}
	if (pool->n2size < 3) {
		applog(LOG_ERR, "%s-%d: MM nonce2 size has to be >= 3 (%d)", avalon8->drv->name, avalon8->device_id, pool->n2size);
		return;
	}
	cg_wlock(&info->update_lock);

	/* Step 2: Send out stratum pkgs */
	cg_rlock(&pool->data_lock);
	info->pool_no = pool->pool_no;
	copy_pool_stratum(&info->pool2, &info->pool1);
	copy_pool_stratum(&info->pool1, &info->pool0);
	copy_pool_stratum(&info->pool0, pool);

	avalon8_stratum_pkgs(avalon8, pool);
	cg_runlock(&pool->data_lock);

	/* Step 3: Send out finish pkg */
	avalon8_stratum_finish(avalon8);
	cg_wunlock(&info->update_lock);
}

static int64_t avalon8_scanhash(struct thr_info *thr)
{
	struct cgpu_info *avalon8 = thr->cgpu;
	struct avalon8_info *info = avalon8->device_data;
	struct timeval current;
	int i, j, k, count = 0;
	int temp_max;
	int64_t ret;
	bool update_settings = false;

	if ((info->connecter == AVA8_CONNECTER_AUC) &&
		(unlikely(avalon8->usbinfo.nodev))) {
		applog(LOG_ERR, "%s-%d: Device disappeared, shutting down thread",
				avalon8->drv->name, avalon8->device_id);
		return -1;
	}

	/* Step 1: Stop polling and detach the device if there is no stratum in 3 minutes, network is down */
	cgtime(&current);
	if (tdiff(&current, &(info->last_stratum)) > 180.0) {
		for (i = 1; i < AVA8_DEFAULT_MODULARS; i++) {
			if (!info->enable[i])
				continue;
			detach_module(avalon8, i);
		}
		info->mm_count = 0;
		return 0;
	}

	/* Step 2: Try to detect new modules */
	if ((tdiff(&current, &(info->last_detect)) > AVA8_MODULE_DETECT_INTERVAL) ||
		!info->mm_count) {
		cgtime(&info->last_detect);
		detect_modules(avalon8);
	}

	/* Step 3: ASIC configrations (voltage and frequency) */
	for (i = 1; i < AVA8_DEFAULT_MODULARS; i++) {
		if (!info->enable[i])
			continue;

		update_settings = false;

		/* Check temperautre */
		temp_max = get_temp_max(info, i);

		/* Enter too hot */
		if (temp_max >= info->temp_overheat[i])
			info->cutoff[i] = 1;

		/* Exit too hot */
		if (info->cutoff[i] && (temp_max <= (info->temp_overheat[i] - 10)))
			info->cutoff[i] = 0;

		switch (info->freq_mode[i]) {
			case AVA8_FREQ_INIT_MODE:
				update_settings = true;
				/* Make sure to send configuration first */
				thr->work_update = false;
				for (j = 0; j < info->miner_count[i]; j++) {
					for (k = 0; k < AVA8_DEFAULT_PLL_CNT; k++) {
						if (opt_avalon8_freq[k] != AVA8_DEFAULT_FREQUENCY)
							info->set_frequency[i][j][k] = opt_avalon8_freq[k];
					}
				}

				if (!strncmp((char *)&(info->mm_version[i]), "851", 3)) {
					if (opt_avalon8_spdlow == AVA8_INVALID_SPDLOW)
						opt_avalon8_spdlow = AVA851_DEFAULT_SPDLOW;
					if (opt_avalon8_nonce_mask == AVA8_INVALID_NONCE_MASK)
						opt_avalon8_nonce_mask = AVA851_DEFAULT_NONCE_MASK;
				} else if (!strncmp((char *)&(info->mm_version[i]), "831", 3)) {
					if (opt_avalon8_spdlow == AVA8_INVALID_SPDLOW)
						opt_avalon8_spdlow = AVA831_DEFAULT_SPDLOW;
					if (opt_avalon8_nonce_mask == AVA8_INVALID_NONCE_MASK)
						opt_avalon8_nonce_mask = AVA831_DEFAULT_NONCE_MASK;
				} else {
					if (opt_avalon8_spdlow == AVA8_INVALID_SPDLOW)
						opt_avalon8_spdlow = AVA8_DEFAULT_SPDLOW;
					if (opt_avalon8_nonce_mask == AVA8_INVALID_NONCE_MASK)
						opt_avalon8_nonce_mask = AVA8_DEFAULT_NONCE_MASK;
				}
				avalon8_init_setting(avalon8, i);

				info->freq_mode[i] = AVA8_FREQ_PLLADJ_MODE;
				break;
			case AVA8_FREQ_PLLADJ_MODE:
				if (opt_avalon8_smart_speed == AVA8_DEFAULT_SMARTSPEED_OFF)
					break;

				/* AVA8_DEFAULT_SMARTSPEED_MODE1: auto speed by A3210 chips */
				break;
			default:
				applog(LOG_ERR, "%s-%d-%d: Invalid frequency mode %d",
						avalon8->drv->name, avalon8->device_id, i, info->freq_mode[i]);
				break;
		}
		if (update_settings) {
			cg_wlock(&info->update_lock);
			avalon8_set_voltage_level(avalon8, i, info->set_voltage_level[i]);
			avalon8_set_asic_otp(avalon8, i, info->set_asic_otp[i]);
			for (j = 0; j < info->miner_count[i]; j++)
				avalon8_set_freq(avalon8, i, j, info->set_frequency[i][j]);
			if (opt_avalon8_smart_speed) {
				if (!strncmp((char *)&(info->mm_version[i]), "851", 3)) {
					if (opt_avalon8_th_pass == AVA8_INVALID_TH_PASS)
						opt_avalon8_th_pass = AVA851_DEFAULT_TH_PASS;
					if (opt_avalon8_th_fail == AVA8_INVALID_TH_FAIL)
						opt_avalon8_th_fail = AVA851_DEFAULT_TH_FAIL;
					if (opt_avalon8_th_timeout == AVA8_INVALID_TH_TIMEOUT)
						opt_avalon8_th_timeout = AVA851_DEFAULT_TH_TIMEOUT;
				} else if (!strncmp((char *)&(info->mm_version[i]), "831", 3)) {
					if (opt_avalon8_th_pass == AVA8_INVALID_TH_PASS)
						opt_avalon8_th_pass = AVA831_DEFAULT_TH_PASS;
					if (opt_avalon8_th_fail == AVA8_INVALID_TH_FAIL)
						opt_avalon8_th_fail = AVA831_DEFAULT_TH_FAIL;
					if (opt_avalon8_th_timeout == AVA8_INVALID_TH_TIMEOUT)
						opt_avalon8_th_timeout = AVA831_DEFAULT_TH_TIMEOUT;
				} else {
					if (opt_avalon8_th_pass == AVA8_INVALID_TH_PASS)
						opt_avalon8_th_pass = AVA8_DEFAULT_TH_PASS;
					if (opt_avalon8_th_fail == AVA8_INVALID_TH_FAIL)
						opt_avalon8_th_fail = AVA8_DEFAULT_TH_FAIL;
					if (opt_avalon8_th_timeout == AVA8_INVALID_TH_TIMEOUT)
						opt_avalon8_th_timeout = AVA8_DEFAULT_TH_TIMEOUT;
				}
				avalon8_set_ss_param(avalon8, i);
			}
			avalon8_set_finish(avalon8, i);
			cg_wunlock(&info->update_lock);
		}
	}

	/* Step 4: Polling  */
	cg_rlock(&info->update_lock);
	polling(avalon8);
	cg_runlock(&info->update_lock);

	/* Step 5: Calculate mm count */
	for (i = 1; i < AVA8_DEFAULT_MODULARS; i++) {
		if (info->enable[i])
			count++;
	}
	info->mm_count = count;

	/* Step 6: Calculate hashes. Use the diff1 value which is scaled by
	 * device diff and is usually lower than pool diff which will give a
	 * more stable result, but remove diff rejected shares to more closely
	 * approximate diff accepted values. */
	info->pending_diff1 += avalon8->diff1 - info->last_diff1;
	info->last_diff1 = avalon8->diff1;
	info->pending_diff1 -= avalon8->diff_rejected - info->last_rej;
	info->last_rej = avalon8->diff_rejected;
	if (info->pending_diff1 && !info->firsthash.tv_sec) {
		cgtime(&info->firsthash);
		copy_time(&(avalon8->dev_start_tv), &(info->firsthash));
	}

	if (info->pending_diff1 <= 0)
		ret = 0;
	else {
		ret = info->pending_diff1;
		info->pending_diff1 = 0;
	}
	return ret * 0xffffffffull;
}

static float avalon8_hash_cal(struct cgpu_info *avalon8, int modular_id)
{
	struct avalon8_info *info = avalon8->device_data;
	uint32_t tmp_freq[AVA8_DEFAULT_PLL_CNT];
	unsigned int i, j, k;
	float mhsmm;

	mhsmm = 0;
	if (!strncmp((char *)&(info->mm_version[modular_id]), "851", 3)) {
		for (i = 0; i < info->miner_count[modular_id]; i++) {
			for (j = 0; j < info->asic_count[modular_id]; j++) {
				for (k = 0; k < AVA8_DEFAULT_PLL_CNT; k++)
					mhsmm += (info->get_asic[modular_id][i][j][2 + k] * info->get_frequency[modular_id][i][j][k]);
			}
		}
	} else {
		for (i = 0; i < info->miner_count[modular_id]; i++) {
			for (j = 0; j < AVA8_DEFAULT_PLL_CNT; j++)
				tmp_freq[j] = info->set_frequency[modular_id][i][j];

			for (j = 0; j < AVA8_DEFAULT_PLL_CNT; j++)
				mhsmm += (info->get_pll[modular_id][i][j] * tmp_freq[j]);
		}
	}

	return mhsmm;
}

#define STATBUFLEN_WITHOUT_DBG (6 * 1024)
#define STATBUFLEN_WITH_DBG (6 * 7 * 1024)
static struct api_data *avalon8_api_stats(struct cgpu_info *avalon8)
{
	struct api_data *root = NULL;
	struct avalon8_info *info = avalon8->device_data;
	int i, j, k, m;
	char buf[256];
	char *statbuf = NULL;
	struct timeval current;
	float mhsmm, auc_temp = 0.0;

	cgtime(&current);
	if (opt_debug)
		statbuf = cgcalloc(STATBUFLEN_WITH_DBG, 1);
	else
		statbuf = cgcalloc(STATBUFLEN_WITHOUT_DBG, 1);

	for (i = 1; i < AVA8_DEFAULT_MODULARS; i++) {
		if (!info->enable[i])
			continue;

		sprintf(buf, "Ver[%s]", info->mm_version[i]);
		strcpy(statbuf, buf);

		sprintf(buf, " DNA[%02x%02x%02x%02x%02x%02x%02x%02x]",
				info->mm_dna[i][0],
				info->mm_dna[i][1],
				info->mm_dna[i][2],
				info->mm_dna[i][3],
				info->mm_dna[i][4],
				info->mm_dna[i][5],
				info->mm_dna[i][6],
				info->mm_dna[i][7]);
		strcat(statbuf, buf);

		sprintf(buf, " Elapsed[%.0f]", tdiff(&current, &(info->elapsed[i])));
		strcat(statbuf, buf);

		strcat(statbuf, " MW[");
		info->local_works[i] = 0;
		for (j = 0; j < info->miner_count[i]; j++) {
			info->local_works[i] += info->local_works_i[i][j];
			sprintf(buf, "%"PRIu64" ", info->local_works_i[i][j]);
			strcat(statbuf, buf);
		}
		statbuf[strlen(statbuf) - 1] = ']';

		sprintf(buf, " LW[%"PRIu64"]", info->local_works[i]);
		strcat(statbuf, buf);

		strcat(statbuf, " MH[");
		info->hw_works[i]  = 0;
		for (j = 0; j < info->miner_count[i]; j++) {
			info->hw_works[i] += info->hw_works_i[i][j];
			sprintf(buf, "%"PRIu64" ", info->hw_works_i[i][j]);
			strcat(statbuf, buf);
		}
		statbuf[strlen(statbuf) - 1] = ']';

		sprintf(buf, " HW[%"PRIu64"]", info->hw_works[i]);
		strcat(statbuf, buf);

		if (!strncmp((char *)&(info->mm_version[i]), "851", 3))	{
			double a, b, dh;

			a = 0;
			b = 0;
			for (j = 0; j < info->miner_count[i]; j++) {
				for (k = 0; k < info->asic_count[i]; k++) {
					a += info->get_asic[i][j][k][0];
					b += info->get_asic[i][j][k][1];
				}
			}
			dh = b ? (b / (a + b)) * 100 : 0;
			sprintf(buf, " DH[%.3f%%]", dh);
			strcat(statbuf, buf);
		}

		sprintf(buf, " Temp[%d]", info->temp_mm[i]);
		strcat(statbuf, buf);

		sprintf(buf, " TMax[%d]", get_temp_max(info, i));
		strcat(statbuf, buf);

		sprintf(buf, " Fan[%d]", info->fan_cpm[i]);
		strcat(statbuf, buf);

		sprintf(buf, " FanR[%d%%]", info->fan_pct[i]);
		strcat(statbuf, buf);

		sprintf(buf, " Vi[");
		strcat(statbuf, buf);
		for (j = 0; j < info->miner_count[i]; j++) {
			sprintf(buf, "%d ", info->get_vin[i][j]);
			strcat(statbuf, buf);
		}
		statbuf[strlen(statbuf) - 1] = ']';

		sprintf(buf, " Vo[");
		strcat(statbuf, buf);
		for (j = 0; j < info->miner_count[i]; j++) {
			sprintf(buf, "%d ", info->get_voltage[i][j]);
			strcat(statbuf, buf);
		}
		statbuf[strlen(statbuf) - 1] = ']';

		if (opt_debug) {
			for (j = 0; j < info->miner_count[i]; j++) {
				sprintf(buf, " PLL%d[", j);
				strcat(statbuf, buf);
				for (k = 0; k < AVA8_DEFAULT_PLL_CNT; k++) {
					sprintf(buf, "%d ", info->get_pll[i][j][k]);
					strcat(statbuf, buf);
				}
				statbuf[strlen(statbuf) - 1] = ']';
			}
		}

		mhsmm = avalon8_hash_cal(avalon8, i);
		sprintf(buf, " GHSmm[%.2f] WU[%.2f] Freq[%.2f]", (float)mhsmm / 1000,
					info->diff1[i] / tdiff(&current, &(info->elapsed[i])) * 60.0,
					(float)mhsmm / (info->asic_count[i] * info->miner_count[i] * 172));
		strcat(statbuf, buf);

		sprintf(buf, " PG[%d]", info->power_good[i]);
		strcat(statbuf, buf);

		sprintf(buf, " Led[%d]", info->led_indicator[i]);
		strcat(statbuf, buf);

		for (j = 0; j < info->miner_count[i]; j++) {
			sprintf(buf, " MW%d[", j);
			strcat(statbuf, buf);
			for (k = 0; k < info->asic_count[i]; k++) {
				sprintf(buf, "%"PRIu64" ", info->chip_matching_work[i][j][k]);
				strcat(statbuf, buf);
			}

			statbuf[strlen(statbuf) - 1] = ']';
		}

		sprintf(buf, " TA[%d]", info->total_asics[i]);
		strcat(statbuf, buf);

		strcat(statbuf, " ECHU[");
		for (j = 0; j < info->miner_count[i]; j++) {
			sprintf(buf, "%d ", info->error_code[i][j]);
			strcat(statbuf, buf);
		}
		statbuf[strlen(statbuf) - 1] = ']';

		sprintf(buf, " ECMM[%d]", info->error_code[i][j]);
		strcat(statbuf, buf);

		if (opt_debug) {
			sprintf(buf, " FAC0[%d]", info->factory_info[0]);
			strcat(statbuf, buf);

			sprintf(buf, " OC[%d]", info->overclocking_info[0]);
			strcat(statbuf, buf);

			for (j = 0; j < info->miner_count[i]; j++) {
				sprintf(buf, " SF%d[", j);
				strcat(statbuf, buf);
				for (k = 0; k < AVA8_DEFAULT_PLL_CNT; k++) {
					sprintf(buf, "%d ", info->set_frequency[i][j][k]);
					strcat(statbuf, buf);
				}

				statbuf[strlen(statbuf) - 1] = ']';
			}

			strcat(statbuf, " PMUV[");
			for (j = 0; j < AVA8_DEFAULT_PMU_CNT; j++) {
				sprintf(buf, "%s ", info->pmu_version[i][j]);
				strcat(statbuf, buf);
			}
			statbuf[strlen(statbuf) - 1] = ']';

			if (!strncmp((char *)&(info->mm_version[i]), "851", 3)) {
				for (j = 0; j < info->miner_count[i]; j++) {
					sprintf(buf, " PVT_T%d[", j);
					strcat(statbuf, buf);
					for (k = 0; k < info->asic_count[i]; k++) {
						sprintf(buf, "%3d ", info->temp[i][j][k]);
						strcat(statbuf, buf);
					}

					statbuf[strlen(statbuf) - 1] = ']';
					statbuf[strlen(statbuf)] = '\0';
				}

				for (j = 0; j < info->miner_count[i]; j++) {
					sprintf(buf, " PVT_V%d[", j);
					strcat(statbuf, buf);
					for (k = 0; k < info->asic_count[i]; k++) {
						sprintf(buf, "%d ", info->core_volt[i][j][k][0]);
						strcat(statbuf, buf);
					}

					statbuf[strlen(statbuf) - 1] = ']';
					statbuf[strlen(statbuf)] = '\0';
				}

				for (j = 0; j < info->miner_count[i]; j++) {
					sprintf(buf, " ERATIO%d[", j);
					strcat(statbuf, buf);
					for (k = 0; k < info->asic_count[i]; k++) {
						if (info->get_asic[i][j][k][0])
							sprintf(buf, "%6.2f%% ", (double)(info->get_asic[i][j][k][1] * 100.0 / (info->get_asic[i][j][k][0] + info->get_asic[i][j][k][1])));
						else
							sprintf(buf, "%6.2f%% ", 0.0);
						strcat(statbuf, buf);
					}

					statbuf[strlen(statbuf) - 1] = ']';
				}

				int l;
				/* i: modular, j: miner, k:asic, l:value */
				for (l = 0; l < 2; l++) {
					for (j = 0; j < info->miner_count[i]; j++) {
						sprintf(buf, " C_%02d_%02d[", j, l);
						strcat(statbuf, buf);
						for (k = 0; k < info->asic_count[i]; k++) {
							sprintf(buf, "%7d ", info->get_asic[i][j][k][l]);
							strcat(statbuf, buf);
						}

						statbuf[strlen(statbuf) - 1] = ']';
					}
				}

				for (j = 0; j < info->miner_count[i]; j++) {
					sprintf(buf, " GHSmm%02d[", j);
					strcat(statbuf, buf);
					for (k = 0; k < info->asic_count[i]; k++) {
						mhsmm = 0;
						for (l = 2; l < 6; l++) {
							if (!strncmp((char *)&(info->mm_version[i]), "851", 3))
								mhsmm += (info->get_asic[i][j][k][l] * info->get_frequency[i][j][k][l - 2]);
							else
								mhsmm += (info->get_asic[i][j][k][l] * info->set_frequency[i][j][l - 2]);
						}
						sprintf(buf, "%7.2f ", mhsmm / 1000);
						strcat(statbuf, buf);
					}
					statbuf[strlen(statbuf) - 1] = ']';
				}

				for (k = 0; k < info->miner_count[i]; k++) {
					sprintf(buf, " CINFO%02d[", k);
					strcat(statbuf, buf);

					for (m = 0; m < 23; m++) {
						sprintf(buf, "%02x", info->otp_info[i][k][m]);
						strcat(statbuf, buf);
					}

					sprintf(buf, "]");
					strcat(statbuf, buf);
				}
			} else {
				for (j = 0; j < info->miner_count[i]; j++) {
					sprintf(buf, " PVT_T%d[", j);
					strcat(statbuf, buf);
					for (k = 0; k < info->asic_count[i]; k++) {
						sprintf(buf, "%d ", info->temp[i][j][k]);
						strcat(statbuf, buf);
					}

					statbuf[strlen(statbuf) - 1] = ']';
					statbuf[strlen(statbuf)] = '\0';
				}

				for (j = 0; j < info->miner_count[i]; j++) {
					for (k = 0; k < info->asic_count[i]; k++) {
						sprintf(buf, " PVT_V%d_%d[", j, k);
						strcat(statbuf, buf);
						for (m = 0; m < AVA8_DEFAULT_CORE_VOLT_CNT; m++) {
							sprintf(buf, "%d ", info->core_volt[i][j][k][m]);
							strcat(statbuf, buf);
						}

						statbuf[strlen(statbuf) - 1] = ']';
						statbuf[strlen(statbuf)] = '\0';
					}
				}
			}
		}

		sprintf(buf, " FM[%d]", info->freq_mode[i]);
		strcat(statbuf, buf);

		strcat(statbuf, " CRC[");
		for (j = 0; j < info->miner_count[i]; j++) {
			sprintf(buf, "%d ", info->error_crc[i][j]);
			strcat(statbuf, buf);
		}
		statbuf[strlen(statbuf) - 1] = ']';

		sprintf(buf, "MM ID%d", i);
		root = api_add_string(root, buf, statbuf, true);
	}
	free(statbuf);

	root = api_add_int(root, "MM Count", &(info->mm_count), true);
	root = api_add_int(root, "Smart Speed", &opt_avalon8_smart_speed, true);
	if (info->connecter == AVA8_CONNECTER_IIC)
		root = api_add_string(root, "Connecter", "IIC", true);

	if (info->connecter == AVA8_CONNECTER_AUC) {
		root = api_add_string(root, "Connecter", "AUC", true);
		root = api_add_string(root, "AUC VER", info->auc_version, false);
		root = api_add_int(root, "AUC I2C Speed", &(info->auc_speed), true);
		root = api_add_int(root, "AUC I2C XDelay", &(info->auc_xdelay), true);
		root = api_add_int(root, "AUC Sensor", &(info->auc_sensor), true);
		auc_temp = decode_auc_temp(info->auc_sensor);
		root = api_add_temp(root, "AUC Temperature", &auc_temp, true);
	}

	root = api_add_bool(root, "Connection Overloaded", &info->conn_overloaded, true);
	root = api_add_int(root, "Voltage Level Offset", &opt_avalon8_voltage_level_offset, true);
	root = api_add_uint32(root, "Nonce Mask", &opt_avalon8_nonce_mask, true);

	return root;
}

/* format: voltage[-addr[-miner]]
 * addr[0, AVA8_DEFAULT_MODULARS - 1], 0 means all modulars
 * miner[0, miner_count], 0 means all miners
 */
char *set_avalon8_device_voltage_level(struct cgpu_info *avalon8, char *arg)
{
	struct avalon8_info *info = avalon8->device_data;
	int val;
	unsigned int addr = 0, i, j;
	uint32_t miner_id = 0;

	if (!(*arg))
		return NULL;

	sscanf(arg, "%d-%d-%d", &val, &addr, &miner_id);

	if (val < AVA8_DEFAULT_VOLTAGE_LEVEL_MIN || val > AVA8_DEFAULT_VOLTAGE_LEVEL_MAX)
		return "Invalid value passed to set_avalon8_device_voltage_level";

	if (addr >= AVA8_DEFAULT_MODULARS) {
		applog(LOG_ERR, "invalid modular index: %d, valid range 0-%d", addr, (AVA8_DEFAULT_MODULARS - 1));
		return "Invalid modular index to set_avalon8_device_voltage_level";
	}

	if (!addr) {
		for (i = 1; i < AVA8_DEFAULT_MODULARS; i++) {
			if (!info->enable[i])
				continue;

			if (miner_id > info->miner_count[i]) {
				applog(LOG_ERR, "invalid miner index: %d, valid range 0-%d", miner_id, info->miner_count[i]);
				return "Invalid miner index to set_avalon8_device_voltage_level";
			}

			if (miner_id)
				info->set_voltage_level[i][miner_id - 1] = val;
			else {
				for (j = 0; j < info->miner_count[i]; j++)
					info->set_voltage_level[i][j] = val;
			}
			avalon8_set_voltage_level(avalon8, i, info->set_voltage_level[i]);
		}
	} else {
		if (!info->enable[addr]) {
			applog(LOG_ERR, "Disabled modular:%d", addr);
			return "Disabled modular to set_avalon8_device_voltage_level";
		}

		if (miner_id > info->miner_count[addr]) {
			applog(LOG_ERR, "invalid miner index: %d, valid range 0-%d", miner_id, info->miner_count[addr]);
			return "Invalid miner index to set_avalon8_device_voltage_level";
		}

		if (miner_id)
			info->set_voltage_level[addr][miner_id - 1] = val;
		else {
			for (j = 0; j < info->miner_count[addr]; j++)
				info->set_voltage_level[addr][j] = val;
		}
		avalon8_set_voltage_level(avalon8, addr, info->set_voltage_level[addr]);
	}

	applog(LOG_NOTICE, "%s-%d: Update voltage-level to %d", avalon8->drv->name, avalon8->device_id, val);

	return NULL;
}

/*
 * format: freq[-addr[-miner]]
 * addr[0, AVA8_DEFAULT_MODULARS - 1], 0 means all modulars
 * miner[0, miner_count], 0 means all miners
 */
char *set_avalon8_device_freq(struct cgpu_info *avalon8, char *arg)
{
	struct avalon8_info *info = avalon8->device_data;
	unsigned int val, addr = 0, i, j, k;
	uint32_t miner_id = 0;

	if (!(*arg))
		return NULL;

	sscanf(arg, "%d-%d-%d", &val, &addr, &miner_id);

	if (val > AVA8_DEFAULT_FREQUENCY_MAX)
		return "Invalid value passed to set_avalon8_device_freq";

	if (addr >= AVA8_DEFAULT_MODULARS) {
		applog(LOG_ERR, "invalid modular index: %d, valid range 0-%d", addr, (AVA8_DEFAULT_MODULARS - 1));
		return "Invalid modular index to set_avalon8_device_freq";
	}

	if (!addr) {
		for (i = 1; i < AVA8_DEFAULT_MODULARS; i++) {
			if (!info->enable[i])
				continue;

			if (miner_id > info->miner_count[i]) {
				applog(LOG_ERR, "invalid miner index: %d, valid range 0-%d", miner_id, info->miner_count[i]);
				return "Invalid miner index to set_avalon8_device_freq";
			}

			if (miner_id) {
				for (k = 0; k < AVA8_DEFAULT_PLL_CNT; k++)
					info->set_frequency[i][miner_id - 1][k] = val;

				avalon8_set_freq(avalon8, i, miner_id - 1, info->set_frequency[i][miner_id - 1]);
			} else {
				for (j = 0; j < info->miner_count[i]; j++) {
					for (k = 0; k < AVA8_DEFAULT_PLL_CNT; k++)
						info->set_frequency[i][j][k] = val;

					avalon8_set_freq(avalon8, i, j, info->set_frequency[i][j]);
				}
			}
		}
	} else {
		if (!info->enable[addr]) {
			applog(LOG_ERR, "Disabled modular:%d", addr);
			return "Disabled modular to set_avalon8_device_freq";
		}

		if (miner_id > info->miner_count[addr]) {
			applog(LOG_ERR, "invalid miner index: %d, valid range 0-%d", miner_id, info->miner_count[addr]);
			return "Invalid miner index to set_avalon8_device_freq";
		}

		if (miner_id) {
			for (k = 0; k < AVA8_DEFAULT_PLL_CNT; k++)
				info->set_frequency[addr][miner_id - 1][k] = val;

			avalon8_set_freq(avalon8, addr, miner_id - 1, info->set_frequency[addr][miner_id - 1]);

		} else {
			for (j = 0; j < info->miner_count[addr]; j++) {
				for (k = 0; k < AVA8_DEFAULT_PLL_CNT; k++)
					info->set_frequency[addr][j][k] = val;

				avalon8_set_freq(avalon8, addr, j, info->set_frequency[addr][j]);
			}
		}
	}

	applog(LOG_NOTICE, "%s-%d: Update frequency to %d",
		avalon8->drv->name, avalon8->device_id, val);

	return NULL;
}

char *set_avalon8_factory_info(struct cgpu_info *avalon8, char *arg)
{
	struct avalon8_info *info = avalon8->device_data;
	char type[AVA8_DEFAULT_FACTORY_INFO_1_CNT];
	int val;

	if (!(*arg))
		return NULL;

	memset(type, 0, AVA8_DEFAULT_FACTORY_INFO_1_CNT);

	sscanf(arg, "%d-%s", &val, type);

	if ((val != AVA8_DEFAULT_FACTORY_INFO_0_IGNORE) &&
				(val < AVA8_DEFAULT_FACTORY_INFO_0_MIN || val > AVA8_DEFAULT_FACTORY_INFO_0_MAX))
		return "Invalid value passed to set_avalon8_factory_info";

	info->factory_info[0] = val;

	memcpy(&info->factory_info[1], type, AVA8_DEFAULT_FACTORY_INFO_1_CNT);

	avalon8_set_factory_info(avalon8, 0, (uint8_t *)info->factory_info);

	applog(LOG_NOTICE, "%s-%d: Update factory info %d",
		avalon8->drv->name, avalon8->device_id, val);

	return NULL;
}

char *set_avalon8_overclocking_info(struct cgpu_info *avalon8, char *arg)
{
	struct avalon8_info *info = avalon8->device_data;
	int val;

	if (!(*arg))
		return NULL;

	sscanf(arg, "%d", &val);

	if (val != AVA8_DEFAULT_OVERCLOCKING_OFF && val != AVA8_DEFAULT_OVERCLOCKING_ON)
		return "Invalid value passed to set_avalon8_overclocking_info";

	info->overclocking_info[0] = val;
	avalon8_set_overclocking_info(avalon8, 0, (uint8_t *)info->overclocking_info);

	applog(LOG_NOTICE, "%s-%d: Update Overclocking info %d",
		avalon8->drv->name, avalon8->device_id, val);

	return NULL;
}

static char *avalon8_set_device(struct cgpu_info *avalon8, char *option, char *setting, char *replybuf)
{
	unsigned int val;
	struct avalon8_info *info = avalon8->device_data;

	if (strcasecmp(option, "help") == 0) {
		sprintf(replybuf, "pdelay|fan|frequency|led|voltage");
		return replybuf;
	}

	if (strcasecmp(option, "pdelay") == 0) {
		if (!setting || !*setting) {
			sprintf(replybuf, "missing polling delay setting");
			return replybuf;
		}

		val = (unsigned int)atoi(setting);
		if (val < 1 || val > 65535) {
			sprintf(replybuf, "invalid polling delay: %d, valid range 1-65535", val);
			return replybuf;
		}

		opt_avalon8_polling_delay = val;

		applog(LOG_NOTICE, "%s-%d: Update polling delay to: %d",
		       avalon8->drv->name, avalon8->device_id, val);

		return NULL;
	}

	if (strcasecmp(option, "fan") == 0) {
		if (!setting || !*setting) {
			sprintf(replybuf, "missing fan value");
			return replybuf;
		}

		if (set_avalon8_fan(setting)) {
			sprintf(replybuf, "invalid fan value, valid range 0-100");
			return replybuf;
		}

		applog(LOG_NOTICE, "%s-%d: Update fan to %d-%d",
		       avalon8->drv->name, avalon8->device_id,
		       opt_avalon8_fan_min, opt_avalon8_fan_max);

		return NULL;
	}

	if (strcasecmp(option, "frequency") == 0) {
		if (!setting || !*setting) {
			sprintf(replybuf, "missing frequency value");
			return replybuf;
		}

		return set_avalon8_device_freq(avalon8, setting);
	}

	if (strcasecmp(option, "led") == 0) {
		int val_led = -1;

		if (!setting || !*setting) {
			sprintf(replybuf, "missing module_id setting");
			return replybuf;
		}

		sscanf(setting, "%d-%d", &val, &val_led);
		if (val < 1 || val >= AVA8_DEFAULT_MODULARS) {
			sprintf(replybuf, "invalid module_id: %d, valid range 1-%d", val, AVA8_DEFAULT_MODULARS);
			return replybuf;
		}

		if (!info->enable[val]) {
			sprintf(replybuf, "the current module was disabled %d", val);
			return replybuf;
		}

		if (val_led == -1)
			info->led_indicator[val] = !info->led_indicator[val];
		else {
			if (val_led < 0 || val_led > 1) {
				sprintf(replybuf, "invalid LED status: %d, valid value 0|1", val_led);
				return replybuf;
			}

			if (val_led != info->led_indicator[val])
				info->led_indicator[val] = val_led;
		}

		applog(LOG_NOTICE, "%s-%d: Module:%d, LED: %s",
				avalon8->drv->name, avalon8->device_id,
				val, info->led_indicator[val] ? "on" : "off");

		return NULL;
	}

	if (strcasecmp(option, "voltage-level") == 0) {
		if (!setting || !*setting) {
			sprintf(replybuf, "missing voltage-level value");
			return replybuf;
		}

		return set_avalon8_device_voltage_level(avalon8, setting);
	}

	if (strcasecmp(option, "factory") == 0) {
		if (!setting || !*setting) {
			sprintf(replybuf, "missing factory info");
			return replybuf;
		}

		return set_avalon8_factory_info(avalon8, setting);
	}

	if (strcasecmp(option, "reboot") == 0) {
		if (!setting || !*setting) {
			sprintf(replybuf, "missing reboot value");
			return replybuf;
		}

		sscanf(setting, "%d", &val);
		if (val < 1 || val >= AVA8_DEFAULT_MODULARS) {
			sprintf(replybuf, "invalid module_id: %d, valid range 1-%d", val, AVA8_DEFAULT_MODULARS);
			return replybuf;
		}

		info->reboot[val] = true;

		return NULL;
	}

	if (strcasecmp(option, "overclocking") == 0) {
		if (!setting || !*setting) {
			sprintf(replybuf, "missing overclocking info");
			return replybuf;
		}

		return set_avalon8_overclocking_info(avalon8, setting);
	}

	sprintf(replybuf, "Unknown option: %s", option);
	return replybuf;
}

static void avalon8_statline_before(char *buf, size_t bufsiz, struct cgpu_info *avalon8)
{
	struct avalon8_info *info = avalon8->device_data;
	int temp = -273;
	int fanmin = AVA8_DEFAULT_FAN_MAX;
	int i, j, k;
	uint32_t frequency = 0;
	float ghs_sum = 0, mhsmm = 0;
	double pass_num = 0.0, fail_num = 0.0;
	uint8_t flag = 0;

	for (i = 1; i < AVA8_DEFAULT_MODULARS; i++) {
		if (!info->enable[i])
			continue;

		if (fanmin >= info->fan_pct[i])
			fanmin = info->fan_pct[i];

		if (temp < get_temp_max(info, i))
			temp = get_temp_max(info, i);

		mhsmm = avalon8_hash_cal(avalon8, i);
		frequency += (mhsmm / (info->asic_count[i] * info->miner_count[i] * 172));
		ghs_sum += (mhsmm / 1000);

		if (!strncmp((char *)&(info->mm_version[i]), "851", 3)) {
			for (j = 0; j < info->miner_count[i]; j++) {
				for (k = 0; k < info->asic_count[i]; k++) {
					pass_num += info->get_asic[i][j][k][0];
					fail_num += info->get_asic[i][j][k][1];
				}
			}
			flag = 1;
		}
	}

	if (info->mm_count)
		frequency /= info->mm_count;

	if (flag)
		tailsprintf(buf, bufsiz, "%4dMhz %.2fGHS %2dC %.2f%% %3d%%", frequency, ghs_sum, temp,
					(fail_num + pass_num) ? fail_num * 100.0 / (fail_num + pass_num) : 0, fanmin);
	else
		tailsprintf(buf, bufsiz, "%4dMhz %.2fGHS %2dC %3d%%", frequency, ghs_sum, temp, fanmin);
}

struct device_drv avalon8_drv = {
	.drv_id = DRIVER_avalon8,
	.dname = "avalon8",
	.name = "AV8",
	.set_device = avalon8_set_device,
	.get_api_stats = avalon8_api_stats,
	.get_statline_before = avalon8_statline_before,
	.drv_detect = avalon8_detect,
	.thread_prepare = avalon8_prepare,
	.hash_work = hash_driver_work,
	.flush_work = avalon8_sswork_update,
	.update_work = avalon8_sswork_update,
	.scanwork = avalon8_scanhash,
	.max_diff = AVA8_DRV_DIFFMAX,
	.genwork = true,
};
