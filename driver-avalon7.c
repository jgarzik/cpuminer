/*
 * Copyright 2016 Mikeqin <Fengling.Qin@gmail.com>
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
#include "driver-avalon7.h"
#include "crc.h"
#include "sha2.h"
#include "hexdump.c"

#define get_fan_pwm(v)	(AVA7_PWM_MAX - (v) * AVA7_PWM_MAX / 100)

int opt_avalon7_temp_target = AVA7_DEFAULT_TEMP_TARGET;

int opt_avalon7_fan_min = AVA7_DEFAULT_FAN_MIN;
int opt_avalon7_fan_max = AVA7_DEFAULT_FAN_MAX;

int opt_avalon7_voltage = AVA7_INVALID_VOLTAGE;
int opt_avalon7_voltage_offset = AVA7_DEFAULT_VOLTAGE_OFFSET;

int opt_avalon7_freq[AVA7_DEFAULT_PLL_CNT] = {AVA7_DEFAULT_FREQUENCY_0,
					      AVA7_DEFAULT_FREQUENCY_1,
					      AVA7_DEFAULT_FREQUENCY_2,
					      AVA7_DEFAULT_FREQUENCY_3,
					      AVA7_DEFAULT_FREQUENCY_4,
					      AVA7_DEFAULT_FREQUENCY_5};

int opt_avalon7_freq_sel = AVA7_DEFAULT_FREQUENCY_SEL;

int opt_avalon7_polling_delay = AVA7_DEFAULT_POLLING_DELAY;

int opt_avalon7_aucspeed = AVA7_AUC_SPEED;
int opt_avalon7_aucxdelay = AVA7_AUC_XDELAY;

int opt_avalon7_smart_speed = AVA7_DEFAULT_SMART_SPEED;
/*
 * smart speed have 2 modes
 * 1. auto speed by A3212 chips
 * 2. option 1 + adjust by average frequency
 */
bool opt_avalon7_iic_detect = AVA7_DEFAULT_IIC_DETECT;
int opt_avalon7_freqadj_time = AVA7_DEFAULT_FREQADJ_TIME;
int opt_avalon7_delta_temp = AVA7_DEFAULT_DELTA_T;
int opt_avalon7_delta_freq = AVA7_DEFAULT_DELTA_FREQ;
int opt_avalon7_freqadj_temp = AVA7_TEMP_FREQADJ;

uint32_t opt_avalon7_th_pass = AVA7_DEFAULT_TH_PASS;
uint32_t opt_avalon7_th_fail = AVA7_DEFAULT_TH_FAIL;
uint32_t opt_avalon7_th_init = AVA7_DEFAULT_TH_INIT;
uint32_t opt_avalon7_th_ms = AVA7_DEFAULT_TH_MS;
uint32_t opt_avalon7_th_timeout = AVA7_DEFAULT_TH_TIMEOUT;
uint32_t opt_avalon7_nonce_mask = AVA7_DEFAULT_NONCE_MASK;
bool opt_avalon7_asic_debug = true;

uint32_t cpm_table[] =
{
	0x0173f813,
	0x0175f813,
	0x0163f813,
	0x0164f813,
	0x0165f813,
	0x0166f813,
	0x0153f813,
	0x01547813,
	0x0154f813,
	0x01557813,
	0x0155f813,
	0x01567813,
	0x0156f813,
	0x01577813,
	0x0143f813,
	0x01443813,
	0x01447813,
	0x0144b813,
	0x0144f813,
	0x01453813,
	0x01457813,
	0x0145b813,
	0x0145f813,
	0x01463813,
	0x01467813,
	0x0146b813,
	0x0146f813,
	0x01473813,
	0x01477813,
	0x0147b813,
	0x0133f813,
	0x01341813,
	0x01343813,
	0x01345813,
	0x01347813,
	0x01349813,
	0x0134b813,
	0x0134d813,
	0x0134f813,
	0x01351813,
	0x01353813,
	0x01355813,
	0x01357813,
	0x01359813,
	0x0135b813,
	0x0135d813,
	0x0135f813,
	0x01361813,
	0x01363813,
	0x01365813,
	0x01367813,
	0x01369813,
	0x0136b813,
	0x0136d813,
	0x0136f813,
	0x01371813,
	0x01373813,
	0x01375813,
	0x01377813,
	0x01379813,
	0x0137b813,
	0x0123e813,
	0x0123f813,
	0x01240813,
	0x01241813,
	0x01242813,
	0x01243813,
	0x01244813,
	0x01245813,
	0x01246813,
	0x01247813,
	0x01248813,
	0x01249813,
	0x0124a813,
	0x0124b813,
	0x0124c813,
	0x0124d813,
	0x0124e813,
	0x0124f813,
	0x01250813,
	0x01251813,
	0x01252813,
	0x01253813,
	0x01254813,
	0x01255813,
	0x01256813,
	0x01257813,
	0x01258813,
	0x01259813,
	0x0125a813,
	0x0125b813,
	0x0125c813,
	0x0125d813,
	0x0125e813,
	0x0125f813,
	0x01260813,
	0x01261813,
	0x01262813,
	0x01263813,
	0x01264813,
	0x01265813,
	0x01266813,
	0x01267813,
	0x01268813,
	0x01269813,
	0x0126a813,
	0x0126b813,
	0x0126c813,
	0x0126d813,
	0x0126e813,
	0x0126f813,
	0x01270813,
	0x01271813,
	0x01272813,
	0x01273813,
	0x01274813,
};

struct avalon7_dev_description avalon7_dev_table[] = {
	{
		"711",
		711,
		4,
		18,
		AVA7_MM711_VOUT_ADC_RATIO,
		4981
	},
	{
		"721",
		721,
		4,
		18,
		AVA7_MM721_VOUT_ADC_RATIO,
		4981
	},
	{
		"741",
		741,
		4,
		22,
		AVA7_MM741_VOUT_ADC_RATIO,
		4825,
	}
};

static uint32_t api_get_cpm(uint32_t freq)
{
	return cpm_table[freq / 12 - 2];
}

static uint32_t encode_voltage(uint32_t volt)
{
	if (volt > AVA7_DEFAULT_VOLTAGE_MAX)
	      volt = AVA7_DEFAULT_VOLTAGE_MAX;

	if (volt < AVA7_DEFAULT_VOLTAGE_MIN)
	      volt = AVA7_DEFAULT_VOLTAGE_MIN;

	return 0x8000 | ((volt - AVA7_DEFAULT_VOLTAGE_MIN) / AVA7_DEFAULT_VOLTAGE_STEP);
}

static uint32_t convert_voltage_level(uint32_t level)
{
	if (level > AVA7_DEFAULT_VOLTAGE_LEVEL_MAX)
             level = AVA7_DEFAULT_VOLTAGE_LEVEL_MAX;

       return AVA7_DEFAULT_VOLTAGE_MIN + level * AVA7_DEFAULT_VOLTAGE_STEP;
}

static uint32_t decode_voltage(struct avalon7_info *info, int modular_id, uint32_t volt)
{
	return (volt * info->vout_adc_ratio[modular_id] / info->asic_count[modular_id] / 100);
}

static uint16_t decode_vin(uint16_t volt)
{
	return (volt * AVA7_VIN_ADC_RATIO);
}

static double decode_pvt_temp(uint16_t pvt_code)
{
	double a4 = -1.1876E-11;
	double a3 =  6.6675E-08;
	double a2 = -1.7724E-04;
	double a1 =  3.3691E-01;
	double a0 = -6.0605E+01;

	return a4 * pow(pvt_code, 4) + a3 * pow(pvt_code, 3) + a2 * pow(pvt_code, 2) + a1 * pow(pvt_code, 1) + a0;
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

char *set_avalon7_fan(char *arg)
{
	int val1, val2, ret;

	ret = sscanf(arg, "%d-%d", &val1, &val2);
	if (ret < 1)
		return "No value passed to avalon7-fan";
	if (ret == 1)
		val2 = val1;

	if (val1 < 0 || val1 > 100 || val2 < 0 || val2 > 100 || val2 < val1)
		return "Invalid value passed to avalon7-fan";

	opt_avalon7_fan_min = val1;
	opt_avalon7_fan_max = val2;

	return NULL;
}

char *set_avalon7_freq(char *arg)
{
	int val[AVA7_DEFAULT_PLL_CNT];
	char *colon, *data;
	int i;

	if (!(*arg))
		return NULL;

	data = arg;
	memset(val, 0, sizeof(val));

	for (i = 0; i < AVA7_DEFAULT_PLL_CNT; i++) {
		colon = strchr(data, ':');
		if (colon)
			*(colon++) = '\0';
		else {
			/* last value */
			if (*data) {
				val[i] = atoi(data);
				if (val[i] < AVA7_DEFAULT_FREQUENCY_MIN || val[i] > AVA7_DEFAULT_FREQUENCY_MAX)
					return "Invalid value passed to avalon7-freq";
			}
			break;
		}

		if (*data) {
			val[i] = atoi(data);
			if (val[i] < AVA7_DEFAULT_FREQUENCY_MIN || val[i] > AVA7_DEFAULT_FREQUENCY_MAX)
				return "Invalid value passed to avalon7-freq";
		}
		data = colon;
	}

	for (i = 0; i < AVA7_DEFAULT_PLL_CNT; i++) {
		if (!val[i] && i)
			val[i] = val[i - 1];
		opt_avalon7_freq[i] = val[i];
	}

	return NULL;
}

char *set_avalon7_voltage(char *arg)
{
	int val, ret;

	ret = sscanf(arg, "%d", &val);
	if (ret < 1)
		return "No value passed to avalon7-voltage";

	if (val < AVA7_DEFAULT_VOLTAGE_MIN || val > AVA7_DEFAULT_VOLTAGE_MAX)
		return "Invalid value passed to avalon7-voltage";

	opt_avalon7_voltage = val;

	return NULL;
}

char *set_avalon7_voltage_level(char *arg)
{
       int val, ret;

       ret = sscanf(arg, "%d", &val);
       if (ret < 1)
               return "No value passed to avalon7-voltage-level";

       if (val < AVA7_DEFAULT_VOLTAGE_LEVEL_MIN || val > AVA7_DEFAULT_VOLTAGE_LEVEL_MAX)
               return "Invalid value passed to avalon7-voltage-level";

       opt_avalon7_voltage = convert_voltage_level(val);

       return NULL;
}

char *set_avalon7_voltage_offset(char *arg)
{
       int val, ret;

       ret = sscanf(arg, "%d", &val);
       if (ret < 1)
               return "No value passed to avalon7-voltage-offset";

       if (val < AVA7_DEFAULT_VOLTAGE_OFFSET_MIN || val > AVA7_DEFAULT_VOLTAGE_OFFSET_MAX)
               return "Invalid value passed to avalon7-voltage-offset";

       opt_avalon7_voltage_offset = val;

       return NULL;
}

static int avalon7_init_pkg(struct avalon7_pkg *pkg, uint8_t type, uint8_t idx, uint8_t cnt)
{
	unsigned short crc;

	pkg->head[0] = AVA7_H1;
	pkg->head[1] = AVA7_H2;

	pkg->type = type;
	pkg->opt = 0;
	pkg->idx = idx;
	pkg->cnt = cnt;

	crc = crc16(pkg->data, AVA7_P_DATA_LEN);

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

	applog(LOG_DEBUG, "avalon7: job_id doesn't match! [%04x:%04x (%s)]",
	       crc, crc_expect, pool_job_id);

	return 1;
}

static inline int get_temp_max(struct avalon7_info *info, int addr)
{
	int i;
	int max = -273;

	for (i = 0; i < info->miner_count[addr]; i++) {
		if (info->temp[addr][i][3] > max)
			max = info->temp[addr][i][3];
	}

	if (max < info->temp_mm[addr])
		max = info->temp_mm[addr];

	return max;
}

/* Use a PID-like feedback mechanism for optimal temperature and fan speed */
static inline uint32_t adjust_fan(struct avalon7_info *info, int id)
{
	int t, tdiff, delta;
	uint32_t pwm;
	time_t now_t;

	now_t = time(NULL);
	t = get_temp_max(info, id);
	tdiff = t - info->temp_last_max[id];
	if (!tdiff && now_t < info->last_temp_time[id] + AVA7_DEFAULT_FAN_INTERVAL)
		goto out;
	info->last_temp_time[id] = now_t;
	delta = t - info->temp_target[id];

	/* Check for init value and ignore it */
	if (unlikely(info->temp_last_max[id] == -273))
		tdiff = 0;
	info->temp_last_max[id] = t;

	if (t >= info->temp_overheat[id]) {
		/* Hit the overheat temperature limit */
		if (info->fan_pct[id] < opt_avalon7_fan_max) {
			applog(LOG_WARNING, "Overheat detected on AV7-%d, increasing fan to max", id);
			info->fan_pct[id] = opt_avalon7_fan_max;
		}
	} else if (delta > 0) {
		/* Over target temperature. */

		/* Is the temp already coming down */
		if (tdiff < 0)
			goto out;
		/* Adjust fanspeed by temperature over and any further rise */
		info->fan_pct[id] += delta + tdiff;
	} else {
		/* Below target temperature */
		int diff = tdiff;

		if (tdiff > 0) {
			int divisor = -delta / AVA7_DEFAULT_TEMP_HYSTERESIS + 1;

			/* Adjust fanspeed by temperature change proportional to
			 * diff from optimal. */
			diff /= divisor;
		} else {
			/* Is the temp below optimal and unchanging, gently lower speed */
			if (t < info->temp_target[id] - AVA7_DEFAULT_TEMP_HYSTERESIS && !tdiff)
				diff -= 1;
		}
		info->fan_pct[id] += diff;
	}

	if (info->fan_pct[id] > opt_avalon7_fan_max)
		info->fan_pct[id] = opt_avalon7_fan_max;
	else if (info->fan_pct[id] < opt_avalon7_fan_min)
		info->fan_pct[id] = opt_avalon7_fan_min;
out:
	pwm = get_fan_pwm(info->fan_pct[id]);
	if (info->freq_mode[id] == AVA7_FREQ_TEMPADJ_MODE)
		pwm = get_fan_pwm(opt_avalon7_fan_max);

	if (info->cutoff[id])
		pwm = get_fan_pwm(opt_avalon7_fan_max);

	applog(LOG_DEBUG, "[%d], Adjust_fan: %dC-%d%%(%03x)", id, t, info->fan_pct[id], pwm);

	return pwm;
}

static int decode_pkg(struct cgpu_info *avalon7, struct avalon7_ret *ar, int modular_id)
{
	struct avalon7_info *info = avalon7->device_data;
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

	if (likely(avalon7->thr))
		thr = avalon7->thr[0];
	if (ar->head[0] != AVA7_H1 && ar->head[1] != AVA7_H2) {
		applog(LOG_DEBUG, "%s-%d-%d: H1 %02x, H2 %02x",
				avalon7->drv->name, avalon7->device_id, modular_id,
				ar->head[0], ar->head[1]);
		hexdump(ar->data, 32);
		return 1;
	}

	expected_crc = crc16(ar->data, AVA7_P_DATA_LEN);
	actual_crc = ((ar->crc[0] & 0xff) << 8) | (ar->crc[1] & 0xff);
	if (expected_crc != actual_crc) {
		applog(LOG_DEBUG, "%s-%d-%d: %02x: expected crc(%04x), actual_crc(%04x)",
		       avalon7->drv->name, avalon7->device_id, modular_id,
		       ar->type, expected_crc, actual_crc);
		return 1;
	}

	switch(ar->type) {
	case AVA7_P_NONCE:
		applog(LOG_DEBUG, "%s-%d-%d: AVA7_P_NONCE", avalon7->drv->name, avalon7->device_id, modular_id);
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
					avalon7->drv->name, avalon7->device_id, modular_id,
					miner, pool_no);
			break;
		}
		nonce2 = be32toh(nonce2);
		nonce = be32toh(nonce);

		if (ntime > info->max_ntime)
			info->max_ntime = ntime;

		applog(LOG_DEBUG, "%s-%d-%d: Found! P:%d - N2:%08x N:%08x NR:%d/%d [M:%d - MW: (%"PRIu64",%"PRIu64",%"PRIu64",%"PRIu64")]",
		       avalon7->drv->name, avalon7->device_id, modular_id,
		       pool_no, nonce2, nonce, ntime, info->max_ntime,
		       miner,
		       info->chip_matching_work[modular_id][miner][0],
		       info->chip_matching_work[modular_id][miner][1],
		       info->chip_matching_work[modular_id][miner][2],
		       info->chip_matching_work[modular_id][miner][3]);

		real_pool = pool = pools[pool_no];
		if (job_idcmp(job_id, pool->swork.job_id)) {
			if (!job_idcmp(job_id, pool_stratum0->swork.job_id)) {
				applog(LOG_DEBUG, "%s-%d-%d: Match to previous stratum0! (%s)",
						avalon7->drv->name, avalon7->device_id, modular_id,
						pool_stratum0->swork.job_id);
				pool = pool_stratum0;
			} else if (!job_idcmp(job_id, pool_stratum1->swork.job_id)) {
				applog(LOG_DEBUG, "%s-%d-%d: Match to previous stratum1! (%s)",
						avalon7->drv->name, avalon7->device_id, modular_id,
						pool_stratum1->swork.job_id);
				pool = pool_stratum1;
			} else if (!job_idcmp(job_id, pool_stratum2->swork.job_id)) {
				applog(LOG_DEBUG, "%s-%d-%d: Match to previous stratum2! (%s)",
						avalon7->drv->name, avalon7->device_id, modular_id,
						pool_stratum2->swork.job_id);
				pool = pool_stratum2;
			} else {
				applog(LOG_ERR, "%s-%d-%d: Cannot match to any stratum! (%s)",
						avalon7->drv->name, avalon7->device_id, modular_id,
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

		last_diff1 = avalon7->diff1;
		if (!submit_nonce2_nonce(thr, pool, real_pool, nonce2, nonce, ntime))
			info->hw_works_i[modular_id][miner]++;
		else {
			info->diff1[modular_id] += (avalon7->diff1 - last_diff1);
			info->chip_matching_work[modular_id][miner][chip_id]++;
		}
		break;
	case AVA7_P_STATUS:
		applog(LOG_DEBUG, "%s-%d-%d: AVA7_P_STATUS", avalon7->drv->name, avalon7->device_id, modular_id);
		hexdump(ar->data, 32);
		memcpy(&tmp, ar->data, 4);
		tmp = be32toh(tmp);
		info->temp_mm[modular_id] = tmp;
		avalon7->temp = decode_auc_temp(info->auc_sensor);

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
	case AVA7_P_STATUS_PMU:
		/* TODO: decode ntc led from PMU */
		applog(LOG_DEBUG, "%s-%d-%d: AVA7_P_STATUS_PMU", avalon7->drv->name, avalon7->device_id, modular_id);
		info->power_good[modular_id] = ar->data[16];
		for (i = 0; i < AVA7_DEFAULT_PMU_CNT; i++) {
			memcpy(&info->pmu_version[modular_id][i], ar->data + 24 + (i * 4), 4);
			info->pmu_version[modular_id][i][4] = '\0';
		}

		for (i = 0; i < info->miner_count[modular_id]; i++) {
			memcpy(&vin, ar->data + 8 + i * 2, 2);
			info->get_vin[modular_id][i] = decode_vin(be16toh(vin));
		}
		break;
	case AVA7_P_STATUS_VOLT:
		applog(LOG_DEBUG, "%s-%d-%d: AVA7_P_STATUS_VOLT", avalon7->drv->name, avalon7->device_id, modular_id);
		for (i = 0; i < info->miner_count[modular_id]; i++) {
			memcpy(&tmp, ar->data + i * 4, 4);
			info->get_voltage[modular_id][i] = decode_voltage(info, modular_id, be32toh(tmp));
		}
		break;
	case AVA7_P_STATUS_PLL:
		applog(LOG_DEBUG, "%s-%d-%d: AVA7_P_STATUS_PLL", avalon7->drv->name, avalon7->device_id, modular_id);
		for (i = 0; i < AVA7_DEFAULT_PLL_CNT; i++) {
			memcpy(&tmp, ar->data + i * 4, 4);
			info->get_pll[modular_id][ar->idx][i] = be32toh(tmp);
		}
		break;
	case AVA7_P_STATUS_PVT:
		applog(LOG_DEBUG, "%s-%d-%d: AVA7_P_STATUS_PVT", avalon7->drv->name, avalon7->device_id, modular_id);
		for (i = 0; i < info->miner_count[modular_id]; i++) {
			memcpy(&tmp, ar->data + i * 8, 4);
			tmp = be32toh(tmp);
			info->temp[modular_id][i][0] = (tmp >> 24) & 0xff;
			info->temp[modular_id][i][1] = (tmp >> 16) & 0xff;
			info->temp[modular_id][i][2] = tmp & 0xffff;

			memcpy(&tmp, ar->data + (i + 1) * 8 - 4, 4);
			tmp = be32toh(tmp);
			info->temp[modular_id][i][3] = (tmp >> 16) & 0xffff;
			info->temp[modular_id][i][4] = tmp & 0xffff;

			/* Update the pvt code to real temperature */
			info->temp[modular_id][i][2] = (int)decode_pvt_temp((uint16_t)info->temp[modular_id][i][2]);
			info->temp[modular_id][i][3] = (int)decode_pvt_temp((uint16_t)info->temp[modular_id][i][3]);
			info->temp[modular_id][i][4] = (int)decode_pvt_temp((uint16_t)info->temp[modular_id][i][4]);
		}
		break;
	case AVA7_P_STATUS_ASIC:
		{
			int x_miner_id;
			int x_asic_id;

			if (!info->asic_count[modular_id])
				break;
			x_miner_id = ar->idx / info->asic_count[modular_id];
			x_asic_id = ar->idx % info->asic_count[modular_id];

			applog(LOG_DEBUG, "%s-%d-%d: AVA7_P_STATUS_ASIC %d-%d",
					avalon7->drv->name, avalon7->device_id, modular_id,
					x_miner_id, x_asic_id);
			memcpy(&tmp, ar->data + 0, 4);
			if (tmp) {
				info->get_asic[modular_id][x_miner_id][x_asic_id][0] = be32toh(tmp);
				memcpy(&tmp, ar->data + 4, 4);
				info->get_asic[modular_id][x_miner_id][x_asic_id][1] = be32toh(tmp);
				memcpy(&tmp, ar->data + 8, 4);
				info->get_asic[modular_id][x_miner_id][x_asic_id][2] = be32toh(tmp);
				memcpy(&tmp, ar->data + 12, 4);
				info->get_asic[modular_id][x_miner_id][x_asic_id][3] = be32toh(tmp);
				memcpy(&tmp, ar->data + 16, 4);
				info->get_asic[modular_id][x_miner_id][x_asic_id][4] = be32toh(tmp);
			}
			tmp = *(ar->data + 20);
			info->get_asic[modular_id][x_miner_id][x_asic_id][5] = tmp;
			tmp = *(ar->data + 21);
			info->get_asic[modular_id][x_miner_id][x_asic_id][6] = tmp;
			tmp = *(ar->data + 22);
			info->get_asic[modular_id][x_miner_id][x_asic_id][7] = tmp;
			tmp = *(ar->data + 23);
			info->get_asic[modular_id][x_miner_id][x_asic_id][8] = tmp;
			tmp = *(ar->data + 24);
			info->get_asic[modular_id][x_miner_id][x_asic_id][9] = tmp;
			tmp = *(ar->data + 25);
			info->get_asic[modular_id][x_miner_id][x_asic_id][10] = tmp;
		}
		break;
	default:
		applog(LOG_DEBUG, "%s-%d-%d: Unknown response %x", avalon7->drv->name, avalon7->device_id, modular_id, ar->type);
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
static int avalon7_auc_init_pkg(uint8_t *iic_pkg, struct avalon7_iic_info *iic_info, uint8_t *buf, int wlen, int rlen)
{
	memset(iic_pkg, 0, AVA7_AUC_P_SIZE);

	switch (iic_info->iic_op) {
	case AVA7_IIC_INIT:
		iic_pkg[0] = 12;	/* 4 bytes IIC header + 4 bytes speed + 4 bytes xfer delay */
		iic_pkg[3] = AVA7_IIC_INIT;
		iic_pkg[4] = iic_info->iic_param.aucParam[0] & 0xff;
		iic_pkg[5] = (iic_info->iic_param.aucParam[0] >> 8) & 0xff;
		iic_pkg[6] = (iic_info->iic_param.aucParam[0] >> 16) & 0xff;
		iic_pkg[7] = iic_info->iic_param.aucParam[0] >> 24;
		iic_pkg[8] = iic_info->iic_param.aucParam[1] & 0xff;
		iic_pkg[9] = (iic_info->iic_param.aucParam[1] >> 8) & 0xff;
		iic_pkg[10] = (iic_info->iic_param.aucParam[1] >> 16) & 0xff;
		iic_pkg[11] = iic_info->iic_param.aucParam[1] >> 24;
		break;
	case AVA7_IIC_XFER:
		iic_pkg[0] = 8 + wlen;
		iic_pkg[3] = AVA7_IIC_XFER;
		iic_pkg[4] = wlen;
		iic_pkg[5] = rlen;
		iic_pkg[7] = iic_info->iic_param.slave_addr;
		if (buf && wlen)
			memcpy(iic_pkg + 8, buf, wlen);
		break;
	case AVA7_IIC_RESET:
	case AVA7_IIC_DEINIT:
	case AVA7_IIC_INFO:
		iic_pkg[0] = 4;
		iic_pkg[3] = iic_info->iic_op;
		break;

	default:
		break;
	}

	return 0;
}

static int avalon7_iic_xfer(struct cgpu_info *avalon7, uint8_t slave_addr,
			    uint8_t *wbuf, int wlen,
			    uint8_t *rbuf, int rlen)
{
	struct avalon7_info *info = avalon7->device_data;
	struct i2c_ctx *pctx = NULL;
	int err = 1;
	bool ret = false;

	pctx = info->i2c_slaves[slave_addr];
	if (!pctx) {
		applog(LOG_ERR, "%s-%d: IIC xfer i2c slaves null!", avalon7->drv->name, avalon7->device_id);
		goto out;
	}

	if (wbuf) {
		ret = pctx->write_raw(pctx, wbuf, wlen);
		if (!ret) {
			applog(LOG_DEBUG, "%s-%d: IIC xfer write raw failed!", avalon7->drv->name, avalon7->device_id);
			goto out;
		}
	}

	cgsleep_ms(5);

	if (rbuf) {
		ret = pctx->read_raw(pctx, rbuf, rlen);
		if (!ret) {
			applog(LOG_DEBUG, "%s-%d: IIC xfer read raw failed!", avalon7->drv->name, avalon7->device_id);
			hexdump(rbuf, rlen);
			goto out;
		}
	}

	return 0;
out:
	return err;
}

static int avalon7_auc_xfer(struct cgpu_info *avalon7,
			    uint8_t *wbuf, int wlen, int *write,
			    uint8_t *rbuf, int rlen, int *read)
{
	int err = -1;

	if (unlikely(avalon7->usbinfo.nodev))
		goto out;

	usb_buffer_clear(avalon7);
	err = usb_write(avalon7, (char *)wbuf, wlen, write, C_AVA7_WRITE);
	if (err || *write != wlen) {
		applog(LOG_DEBUG, "%s-%d: AUC xfer %d, w(%d-%d)!", avalon7->drv->name, avalon7->device_id, err, wlen, *write);
		usb_nodev(avalon7);
		goto out;
	}

	cgsleep_ms(opt_avalon7_aucxdelay / 4800 + 1);

	rlen += 4;		/* Add 4 bytes IIC header */
	err = usb_read(avalon7, (char *)rbuf, rlen, read, C_AVA7_READ);
	if (err || *read != rlen || *read != rbuf[0]) {
		applog(LOG_DEBUG, "%s-%d: AUC xfer %d, r(%d-%d-%d)!", avalon7->drv->name, avalon7->device_id, err, rlen - 4, *read, rbuf[0]);
		hexdump(rbuf, rlen);
		return -1;
	}
	*read = rbuf[0] - 4;	/* Remove 4 bytes IIC header */
out:
	return err;
}

static int avalon7_auc_init(struct cgpu_info *avalon7, char *ver)
{
	struct avalon7_iic_info iic_info;
	int err, wlen, rlen;
	uint8_t wbuf[AVA7_AUC_P_SIZE];
	uint8_t rbuf[AVA7_AUC_P_SIZE];

	if (unlikely(avalon7->usbinfo.nodev))
		return 1;

	/* Try to clean the AUC buffer */
	usb_buffer_clear(avalon7);
	err = usb_read(avalon7, (char *)rbuf, AVA7_AUC_P_SIZE, &rlen, C_AVA7_READ);
	applog(LOG_DEBUG, "%s-%d: AUC usb_read %d, %d!", avalon7->drv->name, avalon7->device_id, err, rlen);
	hexdump(rbuf, AVA7_AUC_P_SIZE);

	/* Reset */
	iic_info.iic_op = AVA7_IIC_RESET;
	rlen = 0;
	avalon7_auc_init_pkg(wbuf, &iic_info, NULL, 0, rlen);

	memset(rbuf, 0, AVA7_AUC_P_SIZE);
	err = avalon7_auc_xfer(avalon7, wbuf, AVA7_AUC_P_SIZE, &wlen, rbuf, rlen, &rlen);
	if (err) {
		applog(LOG_ERR, "%s-%d: Failed to reset Avalon USB2IIC Converter", avalon7->drv->name, avalon7->device_id);
		return 1;
	}

	/* Deinit */
	iic_info.iic_op = AVA7_IIC_DEINIT;
	rlen = 0;
	avalon7_auc_init_pkg(wbuf, &iic_info, NULL, 0, rlen);

	memset(rbuf, 0, AVA7_AUC_P_SIZE);
	err = avalon7_auc_xfer(avalon7, wbuf, AVA7_AUC_P_SIZE, &wlen, rbuf, rlen, &rlen);
	if (err) {
		applog(LOG_ERR, "%s-%d: Failed to deinit Avalon USB2IIC Converter", avalon7->drv->name, avalon7->device_id);
		return 1;
	}

	/* Init */
	iic_info.iic_op = AVA7_IIC_INIT;
	iic_info.iic_param.aucParam[0] = opt_avalon7_aucspeed;
	iic_info.iic_param.aucParam[1] = opt_avalon7_aucxdelay;
	rlen = AVA7_AUC_VER_LEN;
	avalon7_auc_init_pkg(wbuf, &iic_info, NULL, 0, rlen);

	memset(rbuf, 0, AVA7_AUC_P_SIZE);
	err = avalon7_auc_xfer(avalon7, wbuf, AVA7_AUC_P_SIZE, &wlen, rbuf, rlen, &rlen);
	if (err) {
		applog(LOG_ERR, "%s-%d: Failed to init Avalon USB2IIC Converter", avalon7->drv->name, avalon7->device_id);
		return 1;
	}

	hexdump(rbuf, AVA7_AUC_P_SIZE);

	memcpy(ver, rbuf + 4, AVA7_AUC_VER_LEN);
	ver[AVA7_AUC_VER_LEN] = '\0';

	applog(LOG_DEBUG, "%s-%d: USB2IIC Converter version: %s!", avalon7->drv->name, avalon7->device_id, ver);

	return 0;
}

static int avalon7_auc_getinfo(struct cgpu_info *avalon7)
{
	struct avalon7_iic_info iic_info;
	int err, wlen, rlen;
	uint8_t wbuf[AVA7_AUC_P_SIZE];
	uint8_t rbuf[AVA7_AUC_P_SIZE];
	uint8_t *pdata = rbuf + 4;
	uint16_t adc_val;
	struct avalon7_info *info = avalon7->device_data;

	iic_info.iic_op = AVA7_IIC_INFO;
	/* Device info: (9 bytes)
	 * tempadc(2), reqRdIndex, reqWrIndex,
	 * respRdIndex, respWrIndex, tx_flags, state
	 * */
	rlen = 7;
	avalon7_auc_init_pkg(wbuf, &iic_info, NULL, 0, rlen);

	memset(rbuf, 0, AVA7_AUC_P_SIZE);
	err = avalon7_auc_xfer(avalon7, wbuf, AVA7_AUC_P_SIZE, &wlen, rbuf, rlen, &rlen);
	if (err) {
		applog(LOG_ERR, "%s-%d: AUC Failed to get info ", avalon7->drv->name, avalon7->device_id);
		return 1;
	}

	applog(LOG_DEBUG, "%s-%d: AUC tempADC(%03d), reqcnt(%d), respcnt(%d), txflag(%d), state(%d)",
			avalon7->drv->name, avalon7->device_id,
			pdata[1] << 8 | pdata[0],
			pdata[2],
			pdata[3],
			pdata[5] << 8 | pdata[4],
			pdata[6]);

	adc_val = pdata[1] << 8 | pdata[0];

	info->auc_sensor = 3.3 * adc_val * 10000 / 1023;

	return 0;
}

static int avalon7_iic_xfer_pkg(struct cgpu_info *avalon7, uint8_t slave_addr,
				const struct avalon7_pkg *pkg, struct avalon7_ret *ret)
{
	struct avalon7_iic_info iic_info;
	int err, wcnt, rcnt, rlen = 0;
	uint8_t wbuf[AVA7_AUC_P_SIZE];
	uint8_t rbuf[AVA7_AUC_P_SIZE];

	struct avalon7_info *info = avalon7->device_data;

	if (ret)
		rlen = AVA7_READ_SIZE;

	if (info->connecter == AVA7_CONNECTER_AUC) {
		if (unlikely(avalon7->usbinfo.nodev))
			return AVA7_SEND_ERROR;

		iic_info.iic_op = AVA7_IIC_XFER;
		iic_info.iic_param.slave_addr = slave_addr;

		avalon7_auc_init_pkg(wbuf, &iic_info, (uint8_t *)pkg, AVA7_WRITE_SIZE, rlen);
		err = avalon7_auc_xfer(avalon7, wbuf, wbuf[0], &wcnt, rbuf, rlen, &rcnt);
		if ((pkg->type != AVA7_P_DETECT) && err == -7 && !rcnt && rlen) {
			avalon7_auc_init_pkg(wbuf, &iic_info, NULL, 0, rlen);
			err = avalon7_auc_xfer(avalon7, wbuf, wbuf[0], &wcnt, rbuf, rlen, &rcnt);
			applog(LOG_DEBUG, "%s-%d-%d: AUC read again!(type:0x%x, err:%d)", avalon7->drv->name, avalon7->device_id, slave_addr, pkg->type, err);
		}
		if (err || rcnt != rlen) {
			if (info->xfer_err_cnt++ == 100) {
				applog(LOG_DEBUG, "%s-%d-%d: AUC xfer_err_cnt reach err = %d, rcnt = %d, rlen = %d",
						avalon7->drv->name, avalon7->device_id, slave_addr,
						err, rcnt, rlen);

				cgsleep_ms(5 * 1000); /* Wait MM reset */
				if (avalon7_auc_init(avalon7, info->auc_version)) {
					applog(LOG_WARNING, "%s-%d: Failed to re-init auc, unplugging for new hotplug",
					       avalon7->drv->name, avalon7->device_id);
					usb_nodev(avalon7);
				}
			}
			return AVA7_SEND_ERROR;
		}

		if (ret)
			memcpy((char *)ret, rbuf + 4, AVA7_READ_SIZE);

		info->xfer_err_cnt = 0;
	}

	if (info->connecter == AVA7_CONNECTER_IIC) {
		err = avalon7_iic_xfer(avalon7, slave_addr, (uint8_t *)pkg, AVA7_WRITE_SIZE, (uint8_t *)ret, AVA7_READ_SIZE);
		if ((pkg->type != AVA7_P_DETECT) && err) {
			err = avalon7_iic_xfer(avalon7, slave_addr, (uint8_t *)pkg, AVA7_WRITE_SIZE, (uint8_t *)ret, AVA7_READ_SIZE);
			applog(LOG_DEBUG, "%s-%d-%d: IIC read again!(type:0x%x, err:%d)", avalon7->drv->name, avalon7->device_id, slave_addr, pkg->type, err);
		}
		if (err) {
			/* FIXME: Don't care broadcast message with no reply, or it will block other thread when called by avalon7_send_bc_pkgs */
			if ((pkg->type != AVA7_P_DETECT) && (slave_addr == AVA7_MODULE_BROADCAST))
				return AVA7_SEND_OK;

			if (info->xfer_err_cnt++ == 100) {
				info->xfer_err_cnt = 0;
				applog(LOG_DEBUG, "%s-%d-%d: IIC xfer_err_cnt reach err = %d, rcnt = %d, rlen = %d",
						avalon7->drv->name, avalon7->device_id, slave_addr,
						err, rcnt, rlen);

				cgsleep_ms(5 * 1000); /* Wait MM reset */
			}
			return AVA7_SEND_ERROR;
		}

		info->xfer_err_cnt = 0;
	}

	return AVA7_SEND_OK;
}

static int avalon7_send_bc_pkgs(struct cgpu_info *avalon7, const struct avalon7_pkg *pkg)
{
	int ret;

	do {
		ret = avalon7_iic_xfer_pkg(avalon7, AVA7_MODULE_BROADCAST, pkg, NULL);
	} while (ret != AVA7_SEND_OK);

	return 0;
}

static void avalon7_stratum_pkgs(struct cgpu_info *avalon7, struct pool *pool)
{
	struct avalon7_info *info = avalon7->device_data;
	const int merkle_offset = 36;
	struct avalon7_pkg pkg;
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
	       avalon7->drv->name, avalon7->device_id,
	       pool->coinbase_len,
	       pool->nonce2_offset,
	       pool->n2size,
	       merkle_offset,
	       pool->merkles);
	memset(pkg.data, 0, AVA7_P_DATA_LEN);
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
	start = range * avalon7->device_id;

	tmp = be32toh(start);
	memcpy(pkg.data + 20, &tmp, 4);

	tmp = be32toh(range);
	memcpy(pkg.data + 24, &tmp, 4);

	if (info->work_restart) {
		info->work_restart = false;
		tmp = be32toh(0x1);
		memcpy(pkg.data + 28, &tmp, 4);
	}

	avalon7_init_pkg(&pkg, AVA7_P_STATIC, 1, 1);
	if (avalon7_send_bc_pkgs(avalon7, &pkg))
		return;

	if (pool->sdiff <= AVA7_DRV_DIFFMAX)
		set_target(target, pool->sdiff);
	else
		set_target(target, AVA7_DRV_DIFFMAX);

	memcpy(pkg.data, target, 32);
	if (opt_debug) {
		char *target_str;
		target_str = bin2hex(target, 32);
		applog(LOG_DEBUG, "%s-%d: Pool stratum target: %s", avalon7->drv->name, avalon7->device_id, target_str);
		free(target_str);
	}
	avalon7_init_pkg(&pkg, AVA7_P_TARGET, 1, 1);
	if (avalon7_send_bc_pkgs(avalon7, &pkg))
		return;

	memset(pkg.data, 0, AVA7_P_DATA_LEN);

	job_id_len = strlen(pool->swork.job_id);
	crc = crc16((unsigned char *)pool->swork.job_id, job_id_len);
	applog(LOG_DEBUG, "%s-%d: Pool stratum message JOBS_ID[%04x]: %s",
	       avalon7->drv->name, avalon7->device_id,
	       crc, pool->swork.job_id);
	tmp = ((crc << 16) | pool->pool_no);
	if (info->last_jobid != tmp) {
		info->last_jobid = tmp;
		pkg.data[0] = (crc & 0xff00) >> 8;
		pkg.data[1] = crc & 0xff;
		pkg.data[2] = pool->pool_no & 0xff;
		pkg.data[3] = (pool->pool_no & 0xff00) >> 8;
		avalon7_init_pkg(&pkg, AVA7_P_JOB_ID, 1, 1);
		if (avalon7_send_bc_pkgs(avalon7, &pkg))
			return;
	}

	coinbase_len_prehash = pool->nonce2_offset - (pool->nonce2_offset % SHA256_BLOCK_SIZE);
	coinbase_len_posthash = pool->coinbase_len - coinbase_len_prehash;
	sha256_prehash(pool->coinbase, coinbase_len_prehash, coinbase_prehash);

	a = (coinbase_len_posthash / AVA7_P_DATA_LEN) + 1;
	b = coinbase_len_posthash % AVA7_P_DATA_LEN;
	memcpy(pkg.data, coinbase_prehash, 32);
	avalon7_init_pkg(&pkg, AVA7_P_COINBASE, 1, a + (b ? 1 : 0));
	if (avalon7_send_bc_pkgs(avalon7, &pkg))
		return;

	applog(LOG_DEBUG, "%s-%d: Pool stratum message modified COINBASE: %d %d",
			avalon7->drv->name, avalon7->device_id,
			a, b);
	for (i = 1; i < a; i++) {
		memcpy(pkg.data, pool->coinbase + coinbase_len_prehash + i * 32 - 32, 32);
		avalon7_init_pkg(&pkg, AVA7_P_COINBASE, i + 1, a + (b ? 1 : 0));
		if (avalon7_send_bc_pkgs(avalon7, &pkg))
			return;
	}
	if (b) {
		memset(pkg.data, 0, AVA7_P_DATA_LEN);
		memcpy(pkg.data, pool->coinbase + coinbase_len_prehash + i * 32 - 32, b);
		avalon7_init_pkg(&pkg, AVA7_P_COINBASE, i + 1, i + 1);
		if (avalon7_send_bc_pkgs(avalon7, &pkg))
			return;
	}

	b = pool->merkles;
	applog(LOG_DEBUG, "%s-%d: Pool stratum message MERKLES: %d", avalon7->drv->name, avalon7->device_id, b);
	for (i = 0; i < b; i++) {
		memset(pkg.data, 0, AVA7_P_DATA_LEN);
		memcpy(pkg.data, pool->swork.merkle_bin[i], 32);
		avalon7_init_pkg(&pkg, AVA7_P_MERKLES, i + 1, b);
		if (avalon7_send_bc_pkgs(avalon7, &pkg))
			return;
	}

	applog(LOG_DEBUG, "%s-%d: Pool stratum message HEADER: 4", avalon7->drv->name, avalon7->device_id);
	for (i = 0; i < 4; i++) {
		memset(pkg.data, 0, AVA7_P_DATA_LEN);
		memcpy(pkg.data, pool->header_bin + i * 32, 32);
		avalon7_init_pkg(&pkg, AVA7_P_HEADER, i + 1, 4);
		if (avalon7_send_bc_pkgs(avalon7, &pkg))
			return;
	}

	if (info->connecter == AVA7_CONNECTER_AUC)
		avalon7_auc_getinfo(avalon7);
}

static struct cgpu_info *avalon7_iic_detect(void)
{
	int i;
	struct avalon7_info *info;
	struct cgpu_info *avalon7 = NULL;
	struct i2c_ctx *i2c_slave = NULL;

	i2c_slave = i2c_slave_open(I2C_BUS, 0);
	if (!i2c_slave) {
		applog(LOG_ERR, "avalon7 init iic failed\n");
		return NULL;
	}

	i2c_slave->exit(i2c_slave);
	i2c_slave = NULL;

	avalon7 = cgcalloc(1, sizeof(*avalon7));
	avalon7->drv = &avalon7_drv;
	avalon7->deven = DEV_ENABLED;
	avalon7->threads = 1;
	add_cgpu(avalon7);

	applog(LOG_INFO, "%s-%d: Found at %s", avalon7->drv->name, avalon7->device_id,
	       I2C_BUS);

	avalon7->device_data = cgcalloc(sizeof(struct avalon7_info), 1);
	memset(avalon7->device_data, 0, sizeof(struct avalon7_info));
	info = avalon7->device_data;

	for (i = 0; i < AVA7_DEFAULT_MODULARS; i++) {
		info->enable[i] = false;
		info->reboot[i] = false;
		info->i2c_slaves[i] = i2c_slave_open(I2C_BUS, i);
		if (!info->i2c_slaves[i]) {
			applog(LOG_ERR, "avalon7 init i2c slaves failed\n");
			free(avalon7->device_data);
			avalon7->device_data = NULL;
			free(avalon7);
			avalon7 = NULL;
			return NULL;
		}
	}

	info->connecter = AVA7_CONNECTER_IIC;

	return avalon7;
}

static void detect_modules(struct cgpu_info *avalon7);

static struct cgpu_info *avalon7_auc_detect(struct libusb_device *dev, struct usb_find_devices *found)
{
	int i, modules = 0;
	struct avalon7_info *info;
	struct cgpu_info *avalon7 = usb_alloc_cgpu(&avalon7_drv, 1);
	char auc_ver[AVA7_AUC_VER_LEN];

	if (!usb_init(avalon7, dev, found)) {
		applog(LOG_ERR, "avalon7 failed usb_init");
		avalon7 = usb_free_cgpu(avalon7);
		return NULL;
	}

	/* avalon7 prefers not to use zero length packets */
	avalon7->nozlp = true;

	/* We try twice on AUC init */
	if (avalon7_auc_init(avalon7, auc_ver) && avalon7_auc_init(avalon7, auc_ver))
		return NULL;

	applog(LOG_INFO, "%s-%d: Found at %s", avalon7->drv->name, avalon7->device_id,
	       avalon7->device_path);

	avalon7->device_data = cgcalloc(sizeof(struct avalon7_info), 1);
	memset(avalon7->device_data, 0, sizeof(struct avalon7_info));
	info = avalon7->device_data;
	memcpy(info->auc_version, auc_ver, AVA7_AUC_VER_LEN);
	info->auc_version[AVA7_AUC_VER_LEN] = '\0';
	info->auc_speed = opt_avalon7_aucspeed;
	info->auc_xdelay = opt_avalon7_aucxdelay;

	for (i = 0; i < AVA7_DEFAULT_MODULARS; i++)
		info->enable[i] = 0;

	info->connecter = AVA7_CONNECTER_AUC;

	detect_modules(avalon7);
	for (i = 0; i < AVA7_DEFAULT_MODULARS; i++)
		modules += info->enable[i];

	if (!modules) {
		applog(LOG_INFO, "avalon7 found but no modules initialised");
		free(info);
		avalon7 = usb_free_cgpu(avalon7);
		return NULL;
	}

	/* We have an avalon7 AUC connected */
	avalon7->threads = 1;
	add_cgpu(avalon7);

	update_usb_stats(avalon7);

	return avalon7;
}

static inline void avalon7_detect(bool __maybe_unused hotplug)
{
	usb_detect(&avalon7_drv, avalon7_auc_detect);
	if (!hotplug && opt_avalon7_iic_detect)
		avalon7_iic_detect();
}

static bool avalon7_prepare(struct thr_info *thr)
{
	struct cgpu_info *avalon7 = thr->cgpu;
	struct avalon7_info *info = avalon7->device_data;

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
	cgtime(&info->last_fadj);
	cgtime(&info->last_tcheck);

	cglock_init(&info->update_lock);
	cglock_init(&info->pool0.data_lock);
	cglock_init(&info->pool1.data_lock);
	cglock_init(&info->pool2.data_lock);

	return true;
}

static int check_module_exist(struct cgpu_info *avalon7, uint8_t mm_dna[AVA7_MM_DNA_LEN])
{
	struct avalon7_info *info = avalon7->device_data;
	int i;

	for (i = 0; i < AVA7_DEFAULT_MODULARS; i++) {
		/* last byte is \0 */
		if (info->enable[i] && !memcmp(info->mm_dna[i], mm_dna, AVA7_MM_DNA_LEN))
			return 1;
	}

	return 0;
}

static void detect_modules(struct cgpu_info *avalon7)
{
	struct avalon7_info *info = avalon7->device_data;
	struct avalon7_pkg send_pkg;
	struct avalon7_ret ret_pkg;
	uint32_t tmp;
	int i, j, err, rlen;
	uint8_t dev_index;
	uint8_t rbuf[AVA7_AUC_P_SIZE];

	/* Detect new modules here */
	for (i = 1; i < AVA7_DEFAULT_MODULARS + 1; i++) {
		if (info->enable[i])
			continue;

		/* Send out detect pkg */
		applog(LOG_DEBUG, "%s-%d: AVA7_P_DETECT ID[%d]",
		       avalon7->drv->name, avalon7->device_id, i);
		memset(send_pkg.data, 0, AVA7_P_DATA_LEN);
		tmp = be32toh(i); /* ID */
		memcpy(send_pkg.data + 28, &tmp, 4);
		avalon7_init_pkg(&send_pkg, AVA7_P_DETECT, 1, 1);
		err = avalon7_iic_xfer_pkg(avalon7, AVA7_MODULE_BROADCAST, &send_pkg, &ret_pkg);
		if (err == AVA7_SEND_OK) {
			if (decode_pkg(avalon7, &ret_pkg, AVA7_MODULE_BROADCAST)) {
				applog(LOG_DEBUG, "%s-%d: Should be AVA7_P_ACKDETECT(%d), but %d",
				       avalon7->drv->name, avalon7->device_id, AVA7_P_ACKDETECT, ret_pkg.type);
				continue;
			}
		}

		if (err != AVA7_SEND_OK) {
			applog(LOG_DEBUG, "%s-%d: AVA7_P_DETECT: Failed AUC xfer data with err %d",
					avalon7->drv->name, avalon7->device_id, err);
			break;
		}

		applog(LOG_DEBUG, "%s-%d: Module detect ID[%d]: %d",
		       avalon7->drv->name, avalon7->device_id, i, ret_pkg.type);
		if (ret_pkg.type != AVA7_P_ACKDETECT)
			break;

		if (check_module_exist(avalon7, ret_pkg.data))
			continue;

		/* Check count of modulars */
		if (i == AVA7_DEFAULT_MODULARS) {
			applog(LOG_NOTICE, "You have connected more than %d machines. This is discouraged.", (AVA7_DEFAULT_MODULARS - 1));
			info->conn_overloaded = true;
			break;
		} else
			info->conn_overloaded = false;

		memcpy(info->mm_version[i], ret_pkg.data + AVA7_MM_DNA_LEN, AVA7_MM_VER_LEN);
		info->mm_version[i][AVA7_MM_VER_LEN] = '\0';
		for (dev_index = 0; dev_index < (sizeof(avalon7_dev_table) / sizeof(avalon7_dev_table[0])); dev_index++) {
			if (!strncmp((char *)&(info->mm_version[i]), (char *)(avalon7_dev_table[dev_index].dev_id_str), 3)) {
				info->mod_type[i] = avalon7_dev_table[dev_index].mod_type;
				info->miner_count[i] = avalon7_dev_table[dev_index].miner_count;
				info->asic_count[i] = avalon7_dev_table[dev_index].asic_count;
				info->vout_adc_ratio[i] = avalon7_dev_table[dev_index].vout_adc_ratio;
				break;
			}
		}
		if (dev_index == (sizeof(avalon7_dev_table) / sizeof(avalon7_dev_table[0]))) {
			applog(LOG_NOTICE, "%s-%d: The modular version %s cann't be support",
				       avalon7->drv->name, avalon7->device_id, info->mm_version[i]);
			break;
		}

		info->enable[i] = 1;
		cgtime(&info->elapsed[i]);
		memcpy(info->mm_dna[i], ret_pkg.data, AVA7_MM_DNA_LEN);
		memcpy(&tmp, ret_pkg.data + AVA7_MM_DNA_LEN + AVA7_MM_VER_LEN, 4);
		tmp = be32toh(tmp);
		info->total_asics[i] = tmp;
		info->temp_overheat[i] = AVA7_DEFAULT_TEMP_OVERHEAT;
		info->temp_target[i] = opt_avalon7_temp_target;
		info->fan_pct[i] = opt_avalon7_fan_min + (opt_avalon7_fan_min + opt_avalon7_fan_max) / 3;
		for (j = 0; j < info->miner_count[i]; j++) {
			if (opt_avalon7_voltage == AVA7_INVALID_VOLTAGE)
				info->set_voltage[i][j] = avalon7_dev_table[dev_index].set_voltage;
			else
				info->set_voltage[i][j] = opt_avalon7_voltage;
			info->get_voltage[i][j] = 0;
			info->get_vin[i][j] = 0;
		}

		info->freq_mode[i] = AVA7_FREQ_INIT_MODE;
		memset(info->set_frequency[i], 0, sizeof(unsigned int) * info->miner_count[i] * AVA7_DEFAULT_PLL_CNT);
		memset(info->get_pll[i], 0, sizeof(uint32_t) * info->miner_count[i] * AVA7_DEFAULT_PLL_CNT);
		memset(info->get_asic[i], 0, sizeof(uint32_t) * 11 * info->miner_count[i] * AVA7_DEFAULT_PLL_CNT);

		info->led_indicator[i] = 0;
		info->cutoff[i] = 0;
		info->fan_cpm[i] = 0;
		info->temp_mm[i] = -273;
		info->temp_last_max[i] = -273;
		info->local_works[i] = 0;
		info->hw_works[i] = 0;
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
		memset(info->pmu_version[i], 0, sizeof(char) * 5 * AVA7_DEFAULT_PMU_CNT);
		info->diff1[i] = 0;

		applog(LOG_NOTICE, "%s-%d: New module detected! ID[%d-%x]",
		       avalon7->drv->name, avalon7->device_id, i, info->mm_dna[i][AVA7_MM_DNA_LEN - 1]);

		/* Tell MM, it has been detected */
		memset(send_pkg.data, 0, AVA7_P_DATA_LEN);
		memcpy(send_pkg.data, info->mm_dna[i],  AVA7_MM_DNA_LEN);
		avalon7_init_pkg(&send_pkg, AVA7_P_SYNC, 1, 1);
		avalon7_iic_xfer_pkg(avalon7, i, &send_pkg, &ret_pkg);
		/* Keep the usb buffer is empty */
		usb_buffer_clear(avalon7);
		usb_read(avalon7, (char *)rbuf, AVA7_AUC_P_SIZE, &rlen, C_AVA7_READ);
	}
}

static void detach_module(struct cgpu_info *avalon7, int addr)
{
	struct avalon7_info *info = avalon7->device_data;

	info->enable[addr] = 0;
	applog(LOG_NOTICE, "%s-%d: Module detached! ID[%d]",
		avalon7->drv->name, avalon7->device_id, addr);
}

static int polling(struct cgpu_info *avalon7)
{
	struct avalon7_info *info = avalon7->device_data;
	struct avalon7_pkg send_pkg;
	struct avalon7_ret ar;
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

	for (i = 1; i < AVA7_DEFAULT_MODULARS; i++) {
		if (!info->enable[i])
			continue;

		cgsleep_ms(opt_avalon7_polling_delay);

		memset(send_pkg.data, 0, AVA7_P_DATA_LEN);
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

		avalon7_init_pkg(&send_pkg, AVA7_P_POLLING, 1, 1);
		ret = avalon7_iic_xfer_pkg(avalon7, i, &send_pkg, &ar);
		if (ret == AVA7_SEND_OK)
			decode_err = decode_pkg(avalon7, &ar, i);

		if (ret != AVA7_SEND_OK || decode_err) {
			info->error_polling_cnt[i]++;
			memset(send_pkg.data, 0, AVA7_P_DATA_LEN);
			avalon7_init_pkg(&send_pkg, AVA7_P_RSTMMTX, 1, 1);
			avalon7_iic_xfer_pkg(avalon7, i, &send_pkg, NULL);
			if (info->error_polling_cnt[i] >= 10)
				detach_module(avalon7, i);
		}

		if (ret == AVA7_SEND_OK && !decode_err) {
			info->error_polling_cnt[i] = 0;

			if ((ar.opt == AVA7_P_STATUS) &&
				(info->mm_dna[i][AVA7_MM_DNA_LEN - 1] != ar.opt)) {
				applog(LOG_ERR, "%s-%d-%d: Dup address found %d-%d",
						avalon7->drv->name, avalon7->device_id, i,
						info->mm_dna[i][AVA7_MM_DNA_LEN - 1], ar.opt);
				hexdump((uint8_t *)&ar, sizeof(ar));
				detach_module(avalon7, i);
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

static void avalon7_init_setting(struct cgpu_info *avalon7, int addr)
{
	struct avalon7_pkg send_pkg;
	uint32_t tmp;

	memset(send_pkg.data, 0, AVA7_P_DATA_LEN);

	/* TODO:ss/ssp mode */

	tmp = be32toh(opt_avalon7_freq_sel);
	memcpy(send_pkg.data + 4, &tmp, 4);

	/* adjust flag [0-5]: reserved, 6: nonce check, 7: autof*/
	tmp = 1;
	if (!opt_avalon7_smart_speed)
	      tmp = 0;
	tmp |= (1 << 1); /* Enable nonce check */
	tmp |= (opt_avalon7_asic_debug << 2);
	send_pkg.data[8] = tmp & 0xff;
	send_pkg.data[9] = opt_avalon7_nonce_mask & 0xff;

	/* Package the data */
	avalon7_init_pkg(&send_pkg, AVA7_P_SET, 1, 1);
	if (addr == AVA7_MODULE_BROADCAST)
		avalon7_send_bc_pkgs(avalon7, &send_pkg);
	else
		avalon7_iic_xfer_pkg(avalon7, addr, &send_pkg, NULL);
}

static void avalon7_set_voltage(struct cgpu_info *avalon7, int addr, unsigned int voltage[])
{
	struct avalon7_info *info = avalon7->device_data;
	struct avalon7_pkg send_pkg;
	uint32_t tmp;
	uint8_t i;

	memset(send_pkg.data, 0, AVA7_P_DATA_LEN);

	/* FIXME: miner_count should <= 8 */
	for (i = 0; i < info->miner_count[addr]; i++) {
		tmp = be32toh(encode_voltage(voltage[i] +
				opt_avalon7_voltage_offset * AVA7_DEFAULT_VOLTAGE_STEP));
		memcpy(send_pkg.data + i * 4, &tmp, 4);
	}
	applog(LOG_DEBUG, "%s-%d-%d: avalon7 set voltage miner %d, (%d-%d)",
			avalon7->drv->name, avalon7->device_id, addr,
			i, voltage[0], voltage[info->miner_count[addr] - 1]);

	/* Package the data */
	avalon7_init_pkg(&send_pkg, AVA7_P_SET_VOLT, 1, 1);
	if (addr == AVA7_MODULE_BROADCAST)
		avalon7_send_bc_pkgs(avalon7, &send_pkg);
	else
		avalon7_iic_xfer_pkg(avalon7, addr, &send_pkg, NULL);
}

static void avalon7_set_freq(struct cgpu_info *avalon7, int addr, int miner_id, unsigned int freq[])
{
	struct avalon7_info *info = avalon7->device_data;
	struct avalon7_pkg send_pkg;
	uint32_t tmp, f;
	uint8_t i;

	send_pkg.idx = 0; 	/* TODO: This is only for broadcast to all miners
				 * This should be support 4 miners */
	send_pkg.cnt = info->miner_count[addr];

	memset(send_pkg.data, 0, AVA7_P_DATA_LEN);
	for (i = 0; i < AVA7_DEFAULT_PLL_CNT; i++) {
		tmp = be32toh(api_get_cpm(freq[i]));
		memcpy(send_pkg.data + i * 4, &tmp, 4);
	}

	f = freq[0];
	for (i = 1; i < AVA7_DEFAULT_PLL_CNT; i++)
		f = f > freq[i] ? f : freq[i];

	tmp = ((AVA7_ASIC_TIMEOUT_CONST / f) * 40 / 4);
	tmp = be32toh(tmp);
	memcpy(send_pkg.data + AVA7_DEFAULT_PLL_CNT * 4, &tmp, 4);

	tmp = AVA7_ASIC_TIMEOUT_CONST / f * 98 / 100;
	tmp = be32toh(tmp);
	memcpy(send_pkg.data + AVA7_DEFAULT_PLL_CNT * 4 + 4, &tmp, 4);
	applog(LOG_DEBUG, "%s-%d-%d: avalon7 set freq miner %x-%x",
			avalon7->drv->name, avalon7->device_id, addr,
			miner_id, be32toh(tmp));

	/* Package the data */
	avalon7_init_pkg(&send_pkg, AVA7_P_SET_PLL, miner_id + 1, info->miner_count[addr]);

	if (addr == AVA7_MODULE_BROADCAST)
		avalon7_send_bc_pkgs(avalon7, &send_pkg);
	else
		avalon7_iic_xfer_pkg(avalon7, addr, &send_pkg, NULL);
}

static void avalon7_set_ss_param(struct cgpu_info *avalon7, int addr)
{
	struct avalon7_pkg send_pkg;
	uint32_t tmp;

	if (!opt_avalon7_smart_speed)
		return;

	memset(send_pkg.data, 0, AVA7_P_DATA_LEN);

	tmp = be32toh(opt_avalon7_th_pass);
	memcpy(send_pkg.data, &tmp, 4);
	applog(LOG_DEBUG, "%s-%d-%d: avalon7 set th pass %u",
			avalon7->drv->name, avalon7->device_id, addr,
			opt_avalon7_th_pass);

	tmp = be32toh(opt_avalon7_th_fail);
	memcpy(send_pkg.data + 4, &tmp, 4);
	applog(LOG_DEBUG, "%s-%d-%d: avalon7 set th fail %u",
			avalon7->drv->name, avalon7->device_id, addr,
			opt_avalon7_th_fail);

	tmp = be32toh(opt_avalon7_th_init);
	memcpy(send_pkg.data + 8, &tmp, 4);
	applog(LOG_DEBUG, "%s-%d-%d: avalon7 set th init %u",
			avalon7->drv->name, avalon7->device_id, addr,
			opt_avalon7_th_init);

	tmp = be32toh(opt_avalon7_th_ms);
	memcpy(send_pkg.data + 12, &tmp, 4);
	applog(LOG_DEBUG, "%s-%d-%d: avalon7 set th ms %u",
			avalon7->drv->name, avalon7->device_id, addr,
			opt_avalon7_th_ms);

	tmp = be32toh(opt_avalon7_th_timeout);
	memcpy(send_pkg.data + 16, &tmp, 4);
	applog(LOG_DEBUG, "%s-%d-%d: avalon7 set th timeout %u",
			avalon7->drv->name, avalon7->device_id, addr,
			opt_avalon7_th_timeout);

	/* Package the data */
	avalon7_init_pkg(&send_pkg, AVA7_P_SET_SS, 1, 1);

	if (addr == AVA7_MODULE_BROADCAST)
		avalon7_send_bc_pkgs(avalon7, &send_pkg);
	else
		avalon7_iic_xfer_pkg(avalon7, addr, &send_pkg, NULL);
}

static void avalon7_stratum_finish(struct cgpu_info *avalon7)
{
	struct avalon7_pkg send_pkg;

	memset(send_pkg.data, 0, AVA7_P_DATA_LEN);
	avalon7_init_pkg(&send_pkg, AVA7_P_JOB_FIN, 1, 1);
	avalon7_send_bc_pkgs(avalon7, &send_pkg);
}

static void avalon7_set_finish(struct cgpu_info *avalon7, int addr)
{
	struct avalon7_pkg send_pkg;

	memset(send_pkg.data, 0, AVA7_P_DATA_LEN);
	avalon7_init_pkg(&send_pkg, AVA7_P_SET_FIN, 1, 1);
	avalon7_iic_xfer_pkg(avalon7, addr, &send_pkg, NULL);
}

/* miner [0, miner_count], 0 means all miners */
static void avalon7_freq_dec(struct cgpu_info *avalon7, int addr, unsigned int miner_id, unsigned int freq[][AVA7_DEFAULT_PLL_CNT], unsigned int val)
{
	struct avalon7_info *info = avalon7->device_data;
	int i, j;

	if (!miner_id) {
		for (i = 0; i < info->miner_count[addr]; i++) {
			for (j = 0; j < AVA7_DEFAULT_PLL_CNT; j++) {
				if (freq[i][j] <= val) {
					freq[i][j] = AVA7_DEFAULT_FREQUENCY_MIN;
					continue;
				}

				if ((freq[i][j] - val) >= AVA7_DEFAULT_FREQUENCY_MIN)
					freq[i][j] -= val;
				else
					freq[i][j] = AVA7_DEFAULT_FREQUENCY_MIN;
			}
		}
	} else {
		for (i = 0; i < AVA7_DEFAULT_PLL_CNT; i++) {
			if (freq[miner_id][i] <= val)
				freq[miner_id][i] = AVA7_DEFAULT_FREQUENCY_MIN;

			if (freq[miner_id][i] >= AVA7_DEFAULT_FREQUENCY_MIN)
				freq[miner_id][i] -= val;
			else
				freq[miner_id][i] -= AVA7_DEFAULT_FREQUENCY_MIN;
		}
	}
}

static void avalon7_sswork_update(struct cgpu_info *avalon7)
{
	struct avalon7_info *info = avalon7->device_data;
	struct thr_info *thr = avalon7->thr[0];
	struct pool *pool;
	int coinbase_len_posthash, coinbase_len_prehash;

	/*
	 * NOTE: We need mark work_restart to private information,
	 * So that it cann't reset by hash_driver_work
	 */
	if (thr->work_restart)
		info->work_restart = thr->work_restart;
	applog(LOG_DEBUG, "%s-%d: New stratum: restart: %d, update: %d",
	       avalon7->drv->name, avalon7->device_id,
	       thr->work_restart, thr->work_update);

	/* Step 1: MM protocol check */
	pool = current_pool();
	if (!pool->has_stratum)
		quit(1, "%s-%d: MM has to use stratum pools", avalon7->drv->name, avalon7->device_id);

	coinbase_len_prehash = pool->nonce2_offset - (pool->nonce2_offset % SHA256_BLOCK_SIZE);
	coinbase_len_posthash = pool->coinbase_len - coinbase_len_prehash;

	if (coinbase_len_posthash + SHA256_BLOCK_SIZE > AVA7_P_COINBASE_SIZE) {
		applog(LOG_ERR, "%s-%d: MM pool modified coinbase length(%d) is more than %d",
		       avalon7->drv->name, avalon7->device_id,
		       coinbase_len_posthash + SHA256_BLOCK_SIZE, AVA7_P_COINBASE_SIZE);
		return;
	}
	if (pool->merkles > AVA7_P_MERKLES_COUNT) {
		applog(LOG_ERR, "%s-%d: MM merkles has to be less then %d", avalon7->drv->name, avalon7->device_id, AVA7_P_MERKLES_COUNT);
		return;
	}
	if (pool->n2size < 3) {
		applog(LOG_ERR, "%s-%d: MM nonce2 size has to be >= 3 (%d)", avalon7->drv->name, avalon7->device_id, pool->n2size);
		return;
	}
	cg_wlock(&info->update_lock);

	/* Step 2: Send out stratum pkgs */
	cg_rlock(&pool->data_lock);
	cgtime(&info->last_stratum);
	info->pool_no = pool->pool_no;
	copy_pool_stratum(&info->pool2, &info->pool1);
	copy_pool_stratum(&info->pool1, &info->pool0);
	copy_pool_stratum(&info->pool0, pool);

	avalon7_stratum_pkgs(avalon7, pool);
	cg_runlock(&pool->data_lock);

	/* Step 3: Send out finish pkg */
	avalon7_stratum_finish(avalon7);
	cg_wunlock(&info->update_lock);
}

static int64_t avalon7_scanhash(struct thr_info *thr)
{
	struct cgpu_info *avalon7 = thr->cgpu;
	struct avalon7_info *info = avalon7->device_data;
	struct timeval current;
	int i, j, k, count = 0;
	double device_tdiff;
	int temp_max;
	int64_t ret;
	bool update_settings = false;
	bool freq_dec_check = false;
	bool freq_adj_check = false;

	if ((info->connecter == AVA7_CONNECTER_AUC) &&
		(unlikely(avalon7->usbinfo.nodev))) {
		applog(LOG_ERR, "%s-%d: Device disappeared, shutting down thread",
				avalon7->drv->name, avalon7->device_id);
		return -1;
	}

	/* Step 1: Stop polling and detach the device if there is no stratum in 3 minutes, network is down */
	cgtime(&current);
	if (tdiff(&current, &(info->last_stratum)) > 180.0) {
		for (i = 1; i < AVA7_DEFAULT_MODULARS; i++) {
			if (!info->enable[i])
				continue;
			detach_module(avalon7, i);
		}
		info->mm_count = 0;
		return 0;
	}

	/* Step 2: Try to detect new modules */
	if ((tdiff(&current, &(info->last_detect)) > AVA7_MODULE_DETECT_INTERVAL) ||
		!info->mm_count) {
		cgtime(&info->last_detect);
		detect_modules(avalon7);
	}

	/* Step 3: ASIC configrations (voltage and frequency) */
	device_tdiff = tdiff(&current, &(info->last_tcheck));
	if (device_tdiff > 3.0 || device_tdiff < 0) {
		freq_dec_check = true;
		copy_time(&info->last_tcheck, &current);
	}

	device_tdiff = tdiff(&current, &(info->last_fadj));
	if (device_tdiff > opt_avalon7_freqadj_time || device_tdiff < 0) {
		freq_adj_check = true;
		copy_time(&info->last_fadj, &current);
	}

	for (i = 1; i < AVA7_DEFAULT_MODULARS; i++) {
		if (!info->enable[i])
			continue;

		update_settings = false;

		/* Check temperautre */
		temp_max = get_temp_max(info, i);

		/* Check if frequency need decrease */
		if (freq_dec_check && (info->freq_mode[i] != AVA7_FREQ_TEMPADJ_MODE)) {
			if (temp_max >= opt_avalon7_freqadj_temp) {
				update_settings = true;
				info->temp_last_max[i] = temp_max;
				avalon7_freq_dec(avalon7, i, 0, info->set_frequency[i], opt_avalon7_delta_freq + 50);
				applog(LOG_DEBUG, "%s-%d-%d: set freq after temp check %d-%d",
					avalon7->drv->name, avalon7->device_id, i,
					info->set_frequency[i][0][0],
					info->set_frequency[i][info->miner_count[i] - 1][0]);
				info->freq_mode[i] = AVA7_FREQ_TEMPADJ_MODE;
			}
		}

		/* Enter too hot */
		if (temp_max >= info->temp_overheat[i]) {
			info->cutoff[i] = 1;
			info->freq_mode[i] = AVA7_FREQ_CUTOFF_MODE;
		}

		/* Exit too hot */
		if (info->cutoff[i] && (temp_max <= (info->temp_overheat[i] - 10)))
			info->cutoff[i] = 0;

		/* State machine for settings
		 * https://en.bitcoin.it/wiki/Avalon6#Frequency_Statechart
		 * */
		switch (info->freq_mode[i]) {
			case AVA7_FREQ_INIT_MODE:
				update_settings = true;
				for (j = 0; j < info->miner_count[i]; j++) {
					for (k = 0; k < AVA7_DEFAULT_PLL_CNT; k++)
						info->set_frequency[i][j][k] = opt_avalon7_freq[k];
				}

				avalon7_init_setting(avalon7, i);

				info->freq_mode[i] = AVA7_FREQ_PLLADJ_MODE;
				break;
			case AVA7_FREQ_CUTOFF_MODE:
				if (!info->cutoff[i])
					info->freq_mode[i] = AVA7_FREQ_INIT_MODE;
				break;
			case AVA7_FREQ_TEMPADJ_MODE:
				if (freq_adj_check) {
					/* if temp_max goes down ,then we don't need adjust frequency */
					if (info->temp_last_max[i] > temp_max) {
						applog(LOG_DEBUG, "AVA7_FREQ_TEMPADJ_MODE temp goes down");
						info->temp_last_max[i] = get_temp_max(info, i);
						break;
					}

					update_settings = true;
					info->temp_last_max[i] = get_temp_max(info, i);
					avalon7_freq_dec(avalon7, i, 0, info->set_frequency[i], opt_avalon7_delta_freq);
					applog(LOG_DEBUG, "%s-%d-%d: update freq (%d-%d) AVA7_FREQ_PLLADJ_MODE",
							avalon7->drv->name, avalon7->device_id, i,
							info->set_frequency[i][0][0],
							info->set_frequency[i][info->miner_count[i] - 1][0]);
				}

				if (get_temp_max(info, i) <= (info->temp_target[i] - opt_avalon7_delta_temp)) {
					update_settings = true;
					for (j = 0; j < info->miner_count[i]; j++) {
						for (k = 0; k < AVA7_DEFAULT_PLL_CNT; k++)
							info->set_frequency[i][j][k] = opt_avalon7_freq[k];
					}

					info->freq_mode[i] = AVA7_FREQ_INIT_MODE;
					break;
				}
				break;
			case AVA7_FREQ_PLLADJ_MODE:
				if (opt_avalon7_smart_speed == AVA7_DEFAULT_SMARTSPEED_OFF)
					break;

				/* AVA7_DEFAULT_SMARTSPEED_MODE1: auto speed by A3212 chips */
				break;
			default:
				applog(LOG_ERR, "%s-%d-%d: Invalid frequency mode %d",
						avalon7->drv->name, avalon7->device_id, i, info->freq_mode[i]);
				break;
		}
		if (update_settings) {
			cg_wlock(&info->update_lock);
			avalon7_set_voltage(avalon7, i, info->set_voltage[i]);
			for (j = 0; j < info->miner_count[i]; j++)
				avalon7_set_freq(avalon7, i, j, info->set_frequency[i][j]);
			if (opt_avalon7_smart_speed)
				avalon7_set_ss_param(avalon7, i);

			avalon7_set_finish(avalon7, i);
			cg_wunlock(&info->update_lock);
		}
	}

	/* Step 4: Polling  */
	cg_rlock(&info->update_lock);
	polling(avalon7);
	cg_runlock(&info->update_lock);

	/* Step 5: Calculate mm count */
	for (i = 1; i < AVA7_DEFAULT_MODULARS; i++) {
		if (info->enable[i])
			count++;
	}
	info->mm_count = count;

	/* Step 6: Calculate hashes. Use the diff1 value which is scaled by
	 * device diff and is usually lower than pool diff which will give a
	 * more stable result, but remove diff rejected shares to more closely
	 * approximate diff accepted values. */
	info->pending_diff1 += avalon7->diff1 - info->last_diff1;
	info->last_diff1 = avalon7->diff1;
	info->pending_diff1 -= avalon7->diff_rejected - info->last_rej;
	info->last_rej = avalon7->diff_rejected;
	if (info->pending_diff1 && !info->firsthash.tv_sec) {
		cgtime(&info->firsthash);
		copy_time(&(avalon7->dev_start_tv), &(info->firsthash));
	}

	if (info->pending_diff1 <= 0)
		ret = 0;
	else {
		ret = info->pending_diff1;
		info->pending_diff1 = 0;
	}
	return ret * 0xffffffffull;
}

static float avalon7_hash_cal(struct cgpu_info *avalon7, int modular_id)
{
	struct avalon7_info *info = avalon7->device_data;
	uint32_t tmp_freq[AVA7_DEFAULT_PLL_CNT];
	unsigned int i, j;
	float mhsmm;

	mhsmm = 0;
	for (i = 0; i < info->miner_count[modular_id]; i++) {
		for (j = 0; j < AVA7_DEFAULT_PLL_CNT; j++)
			tmp_freq[j] = info->set_frequency[modular_id][i][j];

		for (j = 0; j < AVA7_DEFAULT_PLL_CNT; j++)
			mhsmm += (info->get_pll[modular_id][i][j] * tmp_freq[j]);
	}

	return mhsmm;
}

#define STATBUFLEN_WITHOUT_DBG (6 * 1024)
#define STATBUFLEN_WITH_DBG (6 * 7 * 1024)
static struct api_data *avalon7_api_stats(struct cgpu_info *avalon7)
{
	struct api_data *root = NULL;
	struct avalon7_info *info = avalon7->device_data;
	int i, j, k;
	double a, b, dh;
	char buf[256];
	char *statbuf = NULL;
	struct timeval current;
	float mhsmm, auc_temp = 0.0;

	cgtime(&current);
	if (opt_debug)
		statbuf = cgcalloc(STATBUFLEN_WITH_DBG, 1);
	else
		statbuf = cgcalloc(STATBUFLEN_WITHOUT_DBG, 1);

	for (i = 1; i < AVA7_DEFAULT_MODULARS; i++) {
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

		a = 0;
		b = 0;
		for (j = 0; j < info->miner_count[i]; j++) {
			for (k = 0; k < info->asic_count[i]; k++) {
				a += info->get_asic[i][j][k][0];
				b += info->get_asic[i][j][k][1];
			}
		}
		dh = b ? (b / (a + b)) * 100: 0;
		sprintf(buf, " DH[%.3f%%]", dh);
		strcat(statbuf, buf);

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
				for (k = 0; k < AVA7_DEFAULT_PLL_CNT; k++) {
					sprintf(buf, "%d ", info->get_pll[i][j][k]);
					strcat(statbuf, buf);
				}
				statbuf[strlen(statbuf) - 1] = ']';
			}
		}

		mhsmm = avalon7_hash_cal(avalon7, i);
		sprintf(buf, " GHSmm[%.2f] WU[%.2f] Freq[%.2f]", (float)mhsmm / 1000,
					info->diff1[i] / tdiff(&current, &(info->elapsed[i])) * 60.0,
					(float)mhsmm / (info->asic_count[i] * info->miner_count[i] * 128));
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
			for (j = 0; j < info->miner_count[i]; j++) {
				sprintf(buf, " SF%d[", j);
				strcat(statbuf, buf);
				for (k = 0; k < AVA7_DEFAULT_PLL_CNT; k++) {
					sprintf(buf, "%d ", info->set_frequency[i][j][k]);
					strcat(statbuf, buf);
				}

				statbuf[strlen(statbuf) - 1] = ']';
			}

			strcat(statbuf, " PMUV[");
			for (j = 0; j < AVA7_DEFAULT_PMU_CNT; j++) {
				sprintf(buf, "%s ", info->pmu_version[i][j]);
				strcat(statbuf, buf);
			}
			statbuf[strlen(statbuf) - 1] = ']';

			for (j = 0; j < info->miner_count[i]; j++) {
				sprintf(buf, " ERATIO%d[", j);
				strcat(statbuf, buf);
				for (k = 0; k < info->asic_count[i]; k++) {
					if (info->get_asic[i][j][k][0])
						sprintf(buf, "%.2f%% ", (double)(info->get_asic[i][j][k][1] * 100.0 / (info->get_asic[i][j][k][0] + info->get_asic[i][j][k][1])));
					else
						sprintf(buf, "%.2f%% ", 0.0);
					strcat(statbuf, buf);
				}

				statbuf[strlen(statbuf) - 1] = ']';
			}
			int l;
			/* i: modular, j: miner, k:asic, l:value */
			for (l = 0; l < 5; l++) {
				for (j = 0; j < info->miner_count[i]; j++) {
					sprintf(buf, " C_%d_%02d[", j, l);
					strcat(statbuf, buf);
					for (k = 0; k < info->asic_count[i]; k++) {
						sprintf(buf, "%5d ", info->get_asic[i][j][k][l]);
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
					for (l = 5; l < 11; l++)
						mhsmm += (info->get_asic[i][j][k][l] * info->set_frequency[i][j][l - 5]);
					sprintf(buf, "%.2f ", mhsmm / 1000);
					strcat(statbuf, buf);
				}
				statbuf[strlen(statbuf) - 1] = ']';
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

		strcat(statbuf, " PVT_T[");
		for (j = 0; j < info->miner_count[i]; j++) {
			sprintf(buf, "%d-%d/%d-%d/%d ",
				info->temp[i][j][0],
				info->temp[i][j][2],
				info->temp[i][j][1],
				info->temp[i][j][3],
				info->temp[i][j][4]);
			strcat(statbuf, buf);
		}
		statbuf[strlen(statbuf) - 1] = ']';
		statbuf[strlen(statbuf)] = '\0';

		sprintf(buf, "MM ID%d", i);
		root = api_add_string(root, buf, statbuf, true);
	}
	free(statbuf);

	root = api_add_int(root, "MM Count", &(info->mm_count), true);
	root = api_add_int(root, "Smart Speed", &opt_avalon7_smart_speed, true);
	if (info->connecter == AVA7_CONNECTER_IIC)
		root = api_add_string(root, "Connecter", "IIC", true);

	if (info->connecter == AVA7_CONNECTER_AUC) {
		root = api_add_string(root, "Connecter", "AUC", true);
		root = api_add_string(root, "AUC VER", info->auc_version, false);
		root = api_add_int(root, "AUC I2C Speed", &(info->auc_speed), true);
		root = api_add_int(root, "AUC I2C XDelay", &(info->auc_xdelay), true);
		root = api_add_int(root, "AUC Sensor", &(info->auc_sensor), true);
		auc_temp = decode_auc_temp(info->auc_sensor);
		root = api_add_temp(root, "AUC Temperature", &auc_temp, true);
	}

	root = api_add_bool(root, "Connection Overloaded", &info->conn_overloaded, true);
	root = api_add_int(root, "Voltage Offset", &opt_avalon7_voltage_offset, true);
	root = api_add_uint32(root, "Nonce Mask", &opt_avalon7_nonce_mask, true);

	return root;
}

/* format: voltage[-addr[-miner]]
 * add4[0, AVA7_DEFAULT_MODULARS - 1], 0 means all modulars
 * miner[0, miner_count], 0 means all miners
 */
char *set_avalon7_device_voltage(struct cgpu_info *avalon7, char *arg)
{
	struct avalon7_info *info = avalon7->device_data;
	unsigned int val, addr = 0, i, j;
	uint32_t miner_id = 0;

	if (!(*arg))
		return NULL;

	sscanf(arg, "%d-%d-%d", &val, &addr, &miner_id);
	if (!val)
		val = AVA7_DEFAULT_VOLTAGE_MIN;

	if (val < AVA7_DEFAULT_VOLTAGE_MIN || val > AVA7_DEFAULT_VOLTAGE_MAX)
		return "Invalid value passed to set_avalon7_device_voltage";

	if (addr >= AVA7_DEFAULT_MODULARS) {
		applog(LOG_ERR, "invalid modular index: %d, valid range 0-%d", addr, (AVA7_DEFAULT_MODULARS - 1));
		return "Invalid modular index to set_avalon7_device_voltage";
	}

	if (!info->enable[addr]) {
		applog(LOG_ERR, "Disabled modular:%d", addr);
		return "Disabled modular to set_avalon7_device_voltage";
	}
	if (miner_id > info->miner_count[addr]) {
		applog(LOG_ERR, "invalid miner index: %d, valid range 0-%d", miner_id, info->miner_count[addr]);
		return "Invalid miner index to set_avalon7_device_voltage";
	}

	if (!addr) {
		for (i = 1; i < AVA7_DEFAULT_MODULARS; i++) {
			if (!info->enable[i])
				continue;

			if (miner_id)
				info->set_voltage[i][miner_id - 1] = val;
			else {
				for (j = 0; j < info->miner_count[i]; j++)
					info->set_voltage[i][j] = val;
			}
			avalon7_set_voltage(avalon7, i, info->set_voltage[i]);
		}
	} else {
		if (miner_id)
			info->set_voltage[addr][miner_id - 1] = val;
		else {
			for (j = 0; j < info->miner_count[addr]; j++)
				info->set_voltage[addr][j] = val;
		}
		avalon7_set_voltage(avalon7, addr, info->set_voltage[addr]);
	}

	applog(LOG_NOTICE, "%s-%d: Update voltage to %d",
		avalon7->drv->name, avalon7->device_id, val);

	return NULL;
}

/* format: freq[-addr[-miner]]
 * add4[0, AVA7_DEFAULT_MODULARS - 1], 0 means all modulars
 * miner[0, miner_count], 0 means all miners
 */
char *set_avalon7_device_freq(struct cgpu_info *avalon7, char *arg)
{
	struct avalon7_info *info = avalon7->device_data;
	unsigned int val, addr = 0, i, j, k;
	uint32_t miner_id = 0;

	if (!(*arg))
		return NULL;

	sscanf(arg, "%d-%d-%d", &val, &addr, &miner_id);
	if (!val)
		val = AVA7_DEFAULT_FREQUENCY_MIN;

	if (val < AVA7_DEFAULT_FREQUENCY_MIN || val > AVA7_DEFAULT_FREQUENCY_MAX)
		return "Invalid value passed to set_avalon7_device_freq";

	if (addr >= AVA7_DEFAULT_MODULARS) {
		applog(LOG_ERR, "invalid modular index: %d, valid range 0-%d", addr, (AVA7_DEFAULT_MODULARS - 1));
		return "Invalid modular index to set_avalon7_device_freq";
	}
	if (!info->enable[addr]) {
		applog(LOG_ERR, "Disabled modular:%d", addr);
		return "Disabled modular to set_avalon7_device_freq";
	}
	if (miner_id > info->miner_count[addr]) {
		applog(LOG_ERR, "invalid miner index: %d, valid range 0-%d", miner_id, info->miner_count[addr]);
		return "Invalid miner index to set_avalon7_device_freq";
	}

	if (!addr) {
		for (i = 1; i < AVA7_DEFAULT_MODULARS; i++) {
			if (!info->enable[i])
				continue;

			if (miner_id) {
				for (k = 0; k < AVA7_DEFAULT_PLL_CNT; k++)
					info->set_frequency[i][miner_id - 1][k] = val;

				avalon7_set_freq(avalon7, i, miner_id, info->set_frequency[i][miner_id]);
			} else {
				for (j = 0; j < info->miner_count[i]; j++) {
					for (k = 0; k < AVA7_DEFAULT_PLL_CNT; k++)
						info->set_frequency[i][j][k] = val;

					avalon7_set_freq(avalon7, i, j, info->set_frequency[i][j]);
				}
			}
		}
	} else {
		if (miner_id) {
			for (k = 0; k < AVA7_DEFAULT_PLL_CNT; k++)
				info->set_frequency[addr][miner_id - 1][k] = val;

			avalon7_set_freq(avalon7, addr, miner_id, info->set_frequency[addr][miner_id]);

		} else {
			for (j = 0; j < info->miner_count[addr]; j++) {
				for (k = 0; k < AVA7_DEFAULT_PLL_CNT; k++)
					info->set_frequency[addr][j][k] = val;

				avalon7_set_freq(avalon7, addr, j, info->set_frequency[addr][j]);
			}
		}
	}

	applog(LOG_NOTICE, "%s-%d: Update frequency to %d",
		avalon7->drv->name, avalon7->device_id, val);

	return NULL;
}

static char *avalon7_set_device(struct cgpu_info *avalon7, char *option, char *setting, char *replybuf)
{
	unsigned int val;
	struct avalon7_info *info = avalon7->device_data;

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

		opt_avalon7_polling_delay = val;

		applog(LOG_NOTICE, "%s-%d: Update polling delay to: %d",
		       avalon7->drv->name, avalon7->device_id, val);

		return NULL;
	}

	if (strcasecmp(option, "fan") == 0) {
		if (!setting || !*setting) {
			sprintf(replybuf, "missing fan value");
			return replybuf;
		}

		if (set_avalon7_fan(setting)) {
			sprintf(replybuf, "invalid fan value, valid range 0-100");
			return replybuf;
		}

		applog(LOG_NOTICE, "%s-%d: Update fan to %d-%d",
		       avalon7->drv->name, avalon7->device_id,
		       opt_avalon7_fan_min, opt_avalon7_fan_max);

		return NULL;
	}

	if (strcasecmp(option, "frequency") == 0) {
		if (!setting || !*setting) {
			sprintf(replybuf, "missing frequency value");
			return replybuf;
		}

		return set_avalon7_device_freq(avalon7, setting);
	}

	if (strcasecmp(option, "led") == 0) {
		int val_led = -1;

		if (!setting || !*setting) {
			sprintf(replybuf, "missing module_id setting");
			return replybuf;
		}

		sscanf(setting, "%d-%d", &val, &val_led);
		if (val < 1 || val >= AVA7_DEFAULT_MODULARS) {
			sprintf(replybuf, "invalid module_id: %d, valid range 1-%d", val, AVA7_DEFAULT_MODULARS);
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
				avalon7->drv->name, avalon7->device_id,
				val, info->led_indicator[val] ? "on" : "off");

		return NULL;
	}

	if (strcasecmp(option, "voltage") == 0) {
		if (!setting || !*setting) {
			sprintf(replybuf, "missing voltage value");
			return replybuf;
		}

		return set_avalon7_device_voltage(avalon7, setting);
	}

	if (strcasecmp(option, "reboot") == 0) {
		if (!setting || !*setting) {
			sprintf(replybuf, "missing reboot value");
			return replybuf;
		}

		sscanf(setting, "%d", &val);
		if (val < 1 || val >= AVA7_DEFAULT_MODULARS) {
			sprintf(replybuf, "invalid module_id: %d, valid range 1-%d", val, AVA7_DEFAULT_MODULARS);
			return replybuf;
		}

		info->reboot[val] = true;

		return NULL;
	}

	sprintf(replybuf, "Unknown option: %s", option);
	return replybuf;
}

static void avalon7_statline_before(char *buf, size_t bufsiz, struct cgpu_info *avalon7)
{
	struct avalon7_info *info = avalon7->device_data;
	int temp = -273;
	int fanmin = AVA7_DEFAULT_FAN_MAX;
	int i, j, k;
	uint32_t frequency = 0;
	float ghs_sum = 0, mhsmm = 0;
	double pass_num = 0.0, fail_num = 0.0;

	for (i = 1; i < AVA7_DEFAULT_MODULARS; i++) {
		if (!info->enable[i])
			continue;

		if (fanmin >= info->fan_pct[i])
			fanmin = info->fan_pct[i];

		if (temp < get_temp_max(info, i))
			temp = get_temp_max(info, i);

		mhsmm = avalon7_hash_cal(avalon7, i);
		frequency += (mhsmm / (info->asic_count[i] * info->miner_count[i] * 128));
		ghs_sum += (mhsmm / 1000);

		for (j = 0; j < info->miner_count[i]; j++) {
			for (k = 0; k < info->asic_count[i]; k++) {
				pass_num += info->get_asic[i][j][k][0];
				fail_num += info->get_asic[i][j][k][1];
			}
		}
	}

	if (info->mm_count)
		frequency /= info->mm_count;

	tailsprintf(buf, bufsiz, "%4dMhz %.2fGHS %2dC %.2f%% %3d%%", frequency,
			ghs_sum, temp, (fail_num + pass_num) ? fail_num * 100.0 / (fail_num + pass_num) : 0, fanmin);
}

struct device_drv avalon7_drv = {
	.drv_id = DRIVER_avalon7,
	.dname = "avalon7",
	.name = "AV7",
	.set_device = avalon7_set_device,
	.get_api_stats = avalon7_api_stats,
	.get_statline_before = avalon7_statline_before,
	.drv_detect = avalon7_detect,
	.thread_prepare = avalon7_prepare,
	.hash_work = hash_driver_work,
	.flush_work = avalon7_sswork_update,
	.update_work = avalon7_sswork_update,
	.scanwork = avalon7_scanhash,
	.max_diff = AVA7_DRV_DIFFMAX,
	.genwork = true,
};
