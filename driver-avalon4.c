/*
 * Copyright 2014 Mikeqin <Fengling.Qin@gmail.com>
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
int opt_avalon4_voltage_min = AVA4_DEFAULT_VOLTAGE;
int opt_avalon4_voltage_max = AVA4_DEFAULT_VOLTAGE;
int opt_avalon4_freq[3] = {AVA4_DEFAULT_FREQUENCY,
			   AVA4_DEFAULT_FREQUENCY,
			   AVA4_DEFAULT_FREQUENCY};

int opt_avalon4_polling_delay = AVA4_DEFAULT_POLLING_DELAY;

int opt_avalon4_aucspeed = AVA4_AUC_SPEED;
int opt_avalon4_aucxdelay = AVA4_AUC_XDELAY;

int opt_avalon4_ntime_offset = AVA4_DEFAULT_ASIC_COUNT;

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
		if (info->temp[i] > t)
			t = info->temp[i];
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
	int t = info->temp[id];

	if (t < opt_avalon4_temp_target - 10)
		info->fan_pct[id] = opt_avalon4_fan_min;
	else if (t > opt_avalon4_temp_target + 10 || t > opt_avalon4_overheat - 3)
		info->fan_pct[id] = opt_avalon4_fan_max;
	else if (t > opt_avalon4_temp_target + 1)
		info->fan_pct[id] += 2;
	else if (t < opt_avalon4_temp_target - 1)
		info->fan_pct[id] -= 2;

	if (info->fan_pct[id] < opt_avalon4_fan_min)
		info->fan_pct[id] = opt_avalon4_fan_min;
	if (info->fan_pct[id] > opt_avalon4_fan_max)
		info->fan_pct[id] = opt_avalon4_fan_max;

	pwm = get_fan_pwm(info->fan_pct[id]);
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
	int pool_no;

	if (ar->head[0] != AVA4_H1 && ar->head[1] != AVA4_H2) {
		applog(LOG_DEBUG, "Avalon4: H1 %02x, H2 %02x", ar->head[0], ar->head[1]);
		hexdump(ar->data, 32);
		return 1;
	}

	expected_crc = crc16(ar->data, AVA4_P_DATA_LEN);
	actual_crc = (ar->crc[0] & 0xff) | ((ar->crc[1] & 0xff) << 8);
	if (expected_crc != actual_crc) {
		applog(LOG_DEBUG, "Avalon4: %02x: expected crc(%04x), actual_crc(%04x)",
		       ar->type, expected_crc, actual_crc);
		return 1;
	}

	switch(ar->type) {
	case AVA4_P_NONCE:
		applog(LOG_DEBUG, "Avalon4: AVA4_P_NONCE");
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
		if (miner >= AVA4_DEFAULT_MINERS ||
		    pool_no >= total_pools || pool_no < 0) {
			applog(LOG_DEBUG, "Avalon4: Wrong miner/pool_no %d/%d", miner, pool_no);
			break;
		} else {
			info->matching_work[modular_id][miner]++;
			info->chipmatching_work[modular_id][miner][chip_id]++;
		}
		nonce2 = be32toh(nonce2);
		nonce = be32toh(nonce);
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
				applog(LOG_DEBUG, "Avalon4: Match to previous stratum0! (%s)", pool_stratum0->swork.job_id);
				pool = pool_stratum0;
			} else if (!job_idcmp(job_id, pool_stratum1->swork.job_id)) {
				applog(LOG_DEBUG, "Avalon4: Match to previous stratum1! (%s)", pool_stratum1->swork.job_id);
				pool = pool_stratum1;
			} else if (!job_idcmp(job_id, pool_stratum2->swork.job_id)) {
				applog(LOG_DEBUG, "Avalon4: Match to previous stratum2! (%s)", pool_stratum2->swork.job_id);
				pool = pool_stratum2;
			} else {
				applog(LOG_ERR, "Avalon4: Cannot match to any stratum! (%s)", pool->swork.job_id);
				inc_hw_errors(thr);
				break;
			}
		}

		submit_nonce2_nonce(thr, pool, real_pool, nonce2, nonce, ntime);
		break;
	case AVA4_P_STATUS:
		applog(LOG_DEBUG, "Avalon4: AVA4_P_STATUS");
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

		info->get_frequency[modular_id] = be32toh(info->get_frequency[modular_id]) * 3968 / 65;
		info->get_voltage[modular_id] = be32toh(info->get_voltage[modular_id]);
		info->local_work[modular_id] = be32toh(info->local_work[modular_id]);
		info->hw_work[modular_id] = be32toh(info->hw_work[modular_id]);
		info->power_good[modular_id] = be32toh(info->power_good[modular_id]);

		volt = info->get_voltage[modular_id];
		if (info->mod_type[modular_id] == AVA4_TYPE_MM40)
			tmp = decode_voltage_adp3208d(volt);
		if (info->mod_type[modular_id] == AVA4_TYPE_MM41)
			tmp = decode_voltage_ncp5392p(volt);
		info->get_voltage[modular_id] = tmp;

		info->local_works[modular_id] += info->local_work[modular_id];
		info->hw_works[modular_id] += info->hw_work[modular_id];

		info->lw5[modular_id][info->i_1m] += info->local_work[modular_id];
		info->hw5[modular_id][info->i_1m] += info->hw_work[modular_id];

		avalon4->temp = get_current_temp_max(info);
		break;
	case AVA4_P_ACKDETECT:
		applog(LOG_DEBUG, "Avalon4: AVA4_P_ACKDETECT");
		break;
	default:
		applog(LOG_DEBUG, "Avalon4: Unknown response");
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
static int avalon4_iic_init_pkg(uint8_t *iic_pkg, struct avalon4_iic_info *iic_info, uint8_t *buf, int wlen, int rlen)
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

static int avalon4_iic_xfer(struct cgpu_info *avalon4,
			    uint8_t *wbuf, int wlen, int *write,
			    uint8_t *rbuf, int rlen, int *read)
{
	int err = -1;

	if (unlikely(avalon4->usbinfo.nodev))
		goto out;

	err = usb_write(avalon4, (char *)wbuf, wlen, write, C_AVA4_WRITE);
	if (err || *write != wlen) {
		applog(LOG_DEBUG, "Avalon4: AUC xfer %d, w(%d-%d)!", err, wlen, *write);
		usb_nodev(avalon4);
		goto out;
	}

	cgsleep_ms(opt_avalon4_aucxdelay / 4800 + 1);

	rlen += 4;		/* Add 4 bytes IIC header */
	err = usb_read(avalon4, (char *)rbuf, rlen, read, C_AVA4_READ);
	if (err || *read != rlen) {
		applog(LOG_DEBUG, "Avalon4: AUC xfer %d, r(%d-%d)!", err, rlen - 4, *read);
		hexdump(rbuf, rlen);
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
	err = usb_read(avalon4, (char *)rbuf, AVA4_AUC_P_SIZE, &rlen, C_AVA4_READ);
	applog(LOG_DEBUG, "Avalon4: AUC usb_read %d, %d!", err, rlen);
	hexdump(rbuf, AVA4_AUC_P_SIZE);

	/* Reset */
	iic_info.iic_op = AVA4_IIC_RESET;
	rlen = 0;
	avalon4_iic_init_pkg(wbuf, &iic_info, NULL, 0, rlen);

	memset(rbuf, 0, AVA4_AUC_P_SIZE);
	err = avalon4_iic_xfer(avalon4, wbuf, AVA4_AUC_P_SIZE, &wlen, rbuf, rlen, &rlen);
	if (err) {
		applog(LOG_ERR, "Avalon4: Failed to reset Avalon USB2IIC Converter");
		return 1;
	}

	/* Deinit */
	iic_info.iic_op = AVA4_IIC_DEINIT;
	rlen = 0;
	avalon4_iic_init_pkg(wbuf, &iic_info, NULL, 0, rlen);

	memset(rbuf, 0, AVA4_AUC_P_SIZE);
	err = avalon4_iic_xfer(avalon4, wbuf, AVA4_AUC_P_SIZE, &wlen, rbuf, rlen, &rlen);
	if (err) {
		applog(LOG_ERR, "Avalon4: Failed to deinit Avalon USB2IIC Converter");
		return 1;
	}

	/* Init */
	iic_info.iic_op = AVA4_IIC_INIT;
	iic_info.iic_param.aucParam[0] = opt_avalon4_aucspeed;
	iic_info.iic_param.aucParam[1] = opt_avalon4_aucxdelay;
	rlen = AVA4_AUC_VER_LEN;
	avalon4_iic_init_pkg(wbuf, &iic_info, NULL, 0, rlen);

	memset(rbuf, 0, AVA4_AUC_P_SIZE);
	err = avalon4_iic_xfer(avalon4, wbuf, AVA4_AUC_P_SIZE, &wlen, rbuf, rlen, &rlen);
	if (err) {
		applog(LOG_ERR, "Avalon4: Failed to init Avalon USB2IIC Converter");
		return 1;
	}

	hexdump(rbuf, AVA4_AUC_P_SIZE);

	memcpy(ver, rbuf + 4, AVA4_AUC_VER_LEN);
	ver[AVA4_AUC_VER_LEN] = '\0';

	applog(LOG_DEBUG, "Avalon4: USB2IIC Converter version: %s!", ver);
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
	avalon4_iic_init_pkg(wbuf, &iic_info, NULL, 0, rlen);

	memset(rbuf, 0, AVA4_AUC_P_SIZE);
	err = avalon4_iic_xfer(avalon4, wbuf, AVA4_AUC_P_SIZE, &wlen, rbuf, rlen, &rlen);
	if (err) {
		applog(LOG_ERR, "Avalon4: AUC Failed to get info ");
		return 1;
	}

	applog(LOG_DEBUG, "Avalon4: AUC tempADC(%03d), reqcnt(%d), respcnt(%d), txflag(%d), state(%d)",
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

	iic_info.iic_op = AVA4_IIC_XFER;
	iic_info.iic_param.slave_addr = slave_addr;
	if (ret)
		rlen = AVA4_READ_SIZE;

	avalon4_iic_init_pkg(wbuf, &iic_info, (uint8_t *)pkg, AVA4_WRITE_SIZE, rlen);
	err = avalon4_iic_xfer(avalon4, wbuf, wbuf[0], &wcnt, rbuf, rlen, &rcnt);
	if ((pkg->type != AVA4_P_DETECT) && err == -7 && !rcnt && rlen) {
		avalon4_iic_init_pkg(wbuf, &iic_info, NULL, 0, rlen);
		err = avalon4_iic_xfer(avalon4, wbuf, wbuf[0], &wcnt, rbuf, rlen, &rcnt);
		applog(LOG_DEBUG, "Avalon4: IIC read again!(err:%d)", err);
	}
	if (err || rcnt != rlen) {
		if (info->xfer_err_cnt++ == 100) {
			applog(LOG_DEBUG, "Avalon4: AUC xfer_err_cnt reach err = %d, rcnt = %d, rlen = %d", err, rcnt, rlen);

			cgsleep_ms(5 * 1000); /* Wait MM reset */
			avalon4_auc_init(avalon4, info->auc_version);
		}
		return AVA4_SEND_ERROR;
	}

	if (ret)
		memcpy((char *)ret, rbuf + 4, AVA4_READ_SIZE);

	info->xfer_err_cnt = 0;
	return AVA4_SEND_OK;
}

static int avalon4_send_bc_pkgs(struct cgpu_info *avalon4, const struct avalon4_pkg *pkg)
{
	int ret;

	do {
		if (unlikely(avalon4->usbinfo.nodev))
			return -1;
		ret = avalon4_iic_xfer_pkg(avalon4, AVA4_MODULE_BROADCAST, pkg, NULL);
	} while (ret != AVA4_SEND_OK);

	return 0;
}

static void avalon4_stratum_pkgs(struct cgpu_info *avalon4, struct pool *pool)
{
	const int merkle_offset = 36;
	struct avalon4_pkg pkg;
	int i, a, b, tmp;
	unsigned char target[32];
	int job_id_len, n2size;
	unsigned short crc;

	int coinbase_len_posthash, coinbase_len_prehash;
	uint8_t coinbase_prehash[32];

	/* Send out the first stratum message STATIC */
	applog(LOG_DEBUG, "Avalon4: Pool stratum message STATIC: %d, %d, %d, %d, %d",
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

	set_target(target, pool->sdiff);
	memcpy(pkg.data, target, 32);
	if (opt_debug) {
		char *target_str;
		target_str = bin2hex(target, 32);
		applog(LOG_DEBUG, "Avalon4: Pool stratum target: %s", target_str);
		free(target_str);
	}
	avalon4_init_pkg(&pkg, AVA4_P_TARGET, 1, 1);
	if (avalon4_send_bc_pkgs(avalon4, &pkg))
		return;

	memset(pkg.data, 0, AVA4_P_DATA_LEN);

	job_id_len = strlen(pool->swork.job_id);
	crc = crc16((unsigned char *)pool->swork.job_id, job_id_len);
	applog(LOG_DEBUG, "Avalon4: Pool stratum message JOBS_ID[%04x]: %s",
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
	applog(LOG_DEBUG, "Avalon4: Pool stratum message modified COINBASE: %d %d", a, b);
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
	applog(LOG_DEBUG, "Avalon4: Pool stratum message MERKLES: %d", b);
	for (i = 0; i < b; i++) {
		memset(pkg.data, 0, AVA4_P_DATA_LEN);
		memcpy(pkg.data, pool->swork.merkle_bin[i], 32);
		avalon4_init_pkg(&pkg, AVA4_P_MERKLES, i + 1, b);
		if (avalon4_send_bc_pkgs(avalon4, &pkg))
			return;
	}

	applog(LOG_DEBUG, "Avalon4: Pool stratum message HEADER: 4");
	for (i = 0; i < 4; i++) {
		memset(pkg.data, 0, AVA4_P_DATA_LEN);
		memcpy(pkg.data, pool->header_bin + i * 32, 32);
		avalon4_init_pkg(&pkg, AVA4_P_HEADER, i + 1, 4);
		if (avalon4_send_bc_pkgs(avalon4, &pkg))
			return;
	}

	avalon4_auc_getinfo(avalon4);
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

	avalon4->device_data = calloc(sizeof(struct avalon4_info), 1);
	if (unlikely(!(avalon4->device_data)))
		quit(1, "Failed to calloc avalon4_info");

	info = avalon4->device_data;
	memcpy(info->auc_version, auc_ver, AVA4_AUC_VER_LEN);
	info->auc_version[AVA4_AUC_VER_LEN] = '\0';
	info->auc_speed = opt_avalon4_aucspeed;
	info->auc_xdelay = opt_avalon4_aucxdelay;

	info->polling_first = 1;

	info->set_voltage_broadcat = 1;

	for (i = 0; i < AVA4_DEFAULT_MODULARS; i++) {
		info->enable[i] = 0;
		info->mod_type[i] = AVA4_TYPE_NULL;
		info->fan_pct[i] = AVA4_DEFAULT_FAN_START;
		info->set_voltage[i] = opt_avalon4_voltage_min;
	}

	info->enable[0] = 1;
	info->mod_type[0] = AVA4_TYPE_MM40;

	info->set_frequency[0] = opt_avalon4_freq[0];
	info->set_frequency[1] = opt_avalon4_freq[1];
	info->set_frequency[2] = opt_avalon4_freq[2];

	return avalon4;
}

static inline void avalon4_detect(bool __maybe_unused hotplug)
{
	usb_detect(&avalon4_drv, avalon4_auc_detect);
}

static bool avalon4_prepare(struct thr_info *thr)
{
	int i;
	struct cgpu_info *avalon4 = thr->cgpu;
	struct avalon4_info *info = avalon4->device_data;

	info->polling_first = 1;

	cgtime(&(info->last_fan));
	cgtime(&(info->last_5m));
	cgtime(&(info->last_1m));

	cglock_init(&info->update_lock);
	cglock_init(&info->pool0.data_lock);
	cglock_init(&info->pool1.data_lock);
	cglock_init(&info->pool2.data_lock);

	info->set_voltage_broadcat = 1;

	for (i = 0; i < AVA4_DEFAULT_MODULARS; i++)
		info->fan_pct[i] = AVA4_DEFAULT_FAN_START;


	return true;
}

static void detect_modules(struct cgpu_info *avalon4)
{
	struct avalon4_info *info = avalon4->device_data;
	struct thr_info *thr = avalon4->thr[0];

	struct avalon4_pkg detect_pkg;
	struct avalon4_ret ret_pkg;
	uint32_t tmp;
	int i, err;

	/* Detect new modules here */
	for (i = 1; i < AVA4_DEFAULT_MODULARS; i++) {
		if (info->enable[i])
			continue;

		/* Send out detect pkg */
		applog(LOG_DEBUG, "%s %d: AVA4_P_DETECT ID[%d]",
		       avalon4->drv->name, avalon4->device_id, i);
		memset(detect_pkg.data, 0, AVA4_P_DATA_LEN);
		tmp = be32toh(i); /* ID */
		memcpy(detect_pkg.data + 28, &tmp, 4);
		avalon4_init_pkg(&detect_pkg, AVA4_P_DETECT, 1, 1);
		err = avalon4_iic_xfer_pkg(avalon4, AVA4_MODULE_BROADCAST, &detect_pkg, &ret_pkg);
		if (err == AVA4_SEND_OK) {
			if (decode_pkg(thr, &ret_pkg, AVA4_MODULE_BROADCAST)) {
				applog(LOG_DEBUG, "%s %d: Should be AVA4_P_ACKDETECT(%d), but %d",
				       avalon4->drv->name, avalon4->device_id, AVA4_P_ACKDETECT, ret_pkg.type);
				continue;
			}
		}

		if (err != AVA4_SEND_OK) {
			applog(LOG_DEBUG, "%s %d: AVA4_P_DETECT: Failed AUC xfer data with err %d",
					avalon4->drv->name, avalon4->device_id, err);
			break;
		}

		applog(LOG_DEBUG, "%s %d: Module detect ID[%d]: %d",
		       avalon4->drv->name, avalon4->device_id, i, ret_pkg.type);
		if (ret_pkg.type != AVA4_P_ACKDETECT)
			break;

		cgtime(&info->elapsed[i]);
		info->enable[i] = 1;
		memcpy(info->mm_dna[i], ret_pkg.data, AVA4_MM_DNA_LEN);
		info->mm_dna[i][AVA4_MM_DNA_LEN] = '\0';
		memcpy(info->mm_version[i], ret_pkg.data + AVA4_MM_DNA_LEN, AVA4_MM_VER_LEN);
		info->mm_version[i][AVA4_MM_VER_LEN] = '\0';
		if (!strncmp((char *)&(info->mm_version[i]), AVA4_MM40_PREFIXSTR, 2))
			info->mod_type[i] = AVA4_TYPE_MM40;
		if (!strncmp((char *)&(info->mm_version[i]), AVA4_MM41_PREFIXSTR, 2))
			info->mod_type[i] = AVA4_TYPE_MM41;

		info->fan_pct[i] = AVA4_DEFAULT_FAN_START;
		info->set_voltage[i] = opt_avalon4_voltage_min;
		info->led_red[i] = 0;
		applog(LOG_NOTICE, "%s %d: New module detect! ID[%d]",
		       avalon4->drv->name, avalon4->device_id, i);
	}
}

static int polling(struct thr_info *thr, struct cgpu_info *avalon4, struct avalon4_info *info)
{
	struct avalon4_pkg send_pkg;
	struct avalon4_ret ar;
	int i, j, tmp, ret, decode_err = 0, do_polling = 0;
	struct timeval current_fan;
	int do_adjust_fan = 0;
	uint32_t fan_pwm;
	double device_tdiff;

	if (info->polling_first) {
		cgsleep_ms(300);
		info->polling_first = 0;
	}

	cgtime(&current_fan);
	device_tdiff = tdiff(&current_fan, &(info->last_fan));
	if (device_tdiff > 5.0 || device_tdiff < 0) {
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
			decode_err =  decode_pkg(thr, &ar, i);

		if (ret != AVA4_SEND_OK || decode_err) {
			info->polling_err_cnt[i]++;
			if (info->polling_err_cnt[i] >= 4) {
				info->polling_err_cnt[i] = 0;
				info->mod_type[i] = AVA4_TYPE_NULL;
				info->enable[i] = 0;
				info->get_voltage[i] = 0;
				info->get_frequency[i] = 0;
				info->power_good[i] = 0;
				info->local_work[i] = 0;
				info->local_works[i] = 0;
				info->hw_work[i] = 0;
				info->hw_works[i] = 0;
				for (j = 0; j < 6; j++) {
					info->lw5[i][j] = 0;
					info->hw5[i][j] = 0;
				}

				for (j = 0; j < AVA4_DEFAULT_MINERS; j++) {
					info->matching_work[i][j] = 0;
					info->chipmatching_work[i][j][0] = 0;
					info->chipmatching_work[i][j][1] = 0;
					info->chipmatching_work[i][j][2] = 0;
					info->chipmatching_work[i][j][3] = 0;
				}
				applog(LOG_NOTICE, "%s %d: Module detached! ID[%d]",
				       avalon4->drv->name, avalon4->device_id, i);
			}
		}

		if (ret == AVA4_SEND_OK && !decode_err)
			info->polling_err_cnt[i] = 0;
	}

	if (!do_polling)
		detect_modules(avalon4);

	return 0;
}

static void copy_pool_stratum(struct pool *pool_stratum, struct pool *pool)
{
	int i;
	int merkles = pool->merkles;
	size_t coinbase_len = pool->coinbase_len;

	if (!pool->swork.job_id)
		return;

	if (!job_idcmp((unsigned char *)pool->swork.job_id, pool_stratum->swork.job_id))
		return;

	cg_wlock(&pool_stratum->data_lock);
	free(pool_stratum->swork.job_id);
	free(pool_stratum->nonce1);
	free(pool_stratum->coinbase);

	align_len(&coinbase_len);
	pool_stratum->coinbase = calloc(coinbase_len, 1);
	if (unlikely(!pool_stratum->coinbase))
		quit(1, "Failed to calloc pool_stratum coinbase in avalon4");
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

static void avalon4_stratum_set(struct cgpu_info *avalon4, struct pool *pool, int addr, int cutoff)
{
	struct avalon4_info *info = avalon4->device_data;
	struct avalon4_pkg send_pkg;
	uint32_t tmp = 0, range, start, volt;

	info->set_frequency[0] = opt_avalon4_freq[0];
	info->set_frequency[1] = opt_avalon4_freq[1];
	info->set_frequency[2] = opt_avalon4_freq[2];

	/* Set the NTime, Voltage and Frequency */
	memset(send_pkg.data, 0, AVA4_P_DATA_LEN);

	if (opt_avalon4_ntime_offset != AVA4_DEFAULT_ASIC_COUNT) {
		tmp = opt_avalon4_ntime_offset | 0x80000000;
		tmp = be32toh(tmp);
		memcpy(send_pkg.data, &tmp, 4);
	}

	volt = info->set_voltage[addr];
	if (cutoff)
		volt = 0;
	if (info->mod_type[addr] == AVA4_TYPE_MM40)
		tmp = encode_voltage_adp3208d(volt);
	if (info->mod_type[addr] == AVA4_TYPE_MM41)
		tmp = encode_voltage_ncp5392p(volt);
	tmp = be32toh(tmp);
	memcpy(send_pkg.data + 4, &tmp, 4);

	tmp = info->set_frequency[0] | (info->set_frequency[1] << 10) | (info->set_frequency[2] << 20);
	tmp = be32toh(tmp);
	memcpy(send_pkg.data + 8, &tmp, 4);

	/* Configure the nonce2 offset and range */
	if (pool->n2size == 3)
		range = 0xffffff / (total_devices + 1);
	else
		range = 0xffffffff / (total_devices + 1);
	start = range * (avalon4->device_id + 1);

	tmp = be32toh(start);
	memcpy(send_pkg.data + 12, &tmp, 4);

	tmp = be32toh(range);
	memcpy(send_pkg.data + 16, &tmp, 4);

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

static void avalon4_update(struct cgpu_info *avalon4)
{
	struct avalon4_info *info = avalon4->device_data;
	struct thr_info *thr = avalon4->thr[0];
	struct work *work;
	struct pool *pool;
	int coinbase_len_posthash, coinbase_len_prehash;
	int i, count = 0;

	applog(LOG_DEBUG, "Avalon4: New stratum: restart: %d, update: %d",
	       thr->work_restart, thr->work_update);
	thr->work_update = false;
	thr->work_restart = false;

	/* Step 1: Make sure pool is ready */
	work = get_work(thr, thr->id);
	discard_work(work); /* Don't leak memory */

	/* Step 2: MM protocol check */
	pool = current_pool();
	if (!pool->has_stratum)
		quit(1, "Avalon4: MM has to use stratum pools");

	coinbase_len_prehash = pool->nonce2_offset - (pool->nonce2_offset % SHA256_BLOCK_SIZE);
	coinbase_len_posthash = pool->coinbase_len - coinbase_len_prehash;

	if (coinbase_len_posthash + SHA256_BLOCK_SIZE > AVA4_P_COINBASE_SIZE) {
		applog(LOG_ERR, "Avalon4: MM pool modified coinbase length(%d) is more than %d",
		       coinbase_len_posthash + SHA256_BLOCK_SIZE, AVA4_P_COINBASE_SIZE);
		return;
	}
	if (pool->merkles > AVA4_P_MERKLES_COUNT) {
		applog(LOG_ERR, "Avalon4: MM merkles has to be less then %d", AVA4_P_MERKLES_COUNT);
		return;
	}
	if (pool->n2size < 3) {
		applog(LOG_ERR, "Avalon4: MM nonce2 size has to be >= 3 (%d)", pool->n2size);
		return;
	}

	/* Step 3: Send out stratum pkgs */
	cg_wlock(&info->update_lock);
	cg_rlock(&pool->data_lock);

	cgtime(&info->last_stratum);
	info->pool_no = pool->pool_no;
	copy_pool_stratum(&info->pool2, &info->pool1);
	copy_pool_stratum(&info->pool1, &info->pool0);
	copy_pool_stratum(&info->pool0, pool);
	avalon4_stratum_pkgs(avalon4, pool);

	cg_runlock(&pool->data_lock);
	cg_wunlock(&info->update_lock);

	/* Step 4: Try to detect new modules */
	detect_modules(avalon4);

	/* Step 5: Configure the parameter from outside */
	avalon4_stratum_set(avalon4, pool, AVA4_MODULE_BROADCAST, 0);

	if (!info->set_voltage_broadcat) {
		for (i = 1; i < AVA4_DEFAULT_MODULARS; i++) {
			if (!info->enable[i])
				continue;
			if (info->set_voltage[i] == info->set_voltage[0])
				continue;

			avalon4_stratum_set(avalon4, pool, i, 0);
		}
	} else {
		for (i = 1; i < AVA4_DEFAULT_MODULARS; i++) {
			if (!info->enable[i])
				continue;
			if (info->mod_type[i] == AVA4_TYPE_MM40)
				continue;

			avalon4_stratum_set(avalon4, pool, i, 0);
		}
	}

	for (i = 1; i < AVA4_DEFAULT_MODULARS; i++) {
		if (!info->enable[i])
			continue;

		count++;
		if (info->temp[i] < opt_avalon4_overheat)
			continue;

		avalon4_stratum_set(avalon4, pool, i, 1);
	}
	info->mm_count = count;

	/* Step 6: Send out finish pkg */
	avalon4_stratum_finish(avalon4);
}

static int64_t avalon4_scanhash(struct thr_info *thr)
{
	struct cgpu_info *avalon4 = thr->cgpu;
	struct avalon4_info *info = avalon4->device_data;
	struct timeval current;
	double device_tdiff, hwp;
	uint32_t a = 0, b = 0;
	uint64_t h;
	int i, j;

	if (unlikely(avalon4->usbinfo.nodev)) {
		applog(LOG_ERR, "%s-%d: Device disappeared, shutting down thread",
		       avalon4->drv->name, avalon4->device_id);
		return -1;
	}

	/* Stop polling the device if there is no stratum in 3 minutes, network is down */
	cgtime(&current);
	if (tdiff(&current, &(info->last_stratum)) > 180.0)
		return 0;

	cg_rlock(&info->update_lock);
	polling(thr, avalon4, info);
	cg_runlock(&info->update_lock);

	cgtime(&current);
	device_tdiff = tdiff(&current, &(info->last_1m));
	if (device_tdiff >= 60.0 || device_tdiff < 0) {
		copy_time(&info->last_1m, &current);
		if (info->i_1m++ >= 6)
			info->i_1m = 0;

		for (i = 1; i < AVA4_DEFAULT_MODULARS; i++) {
			if (!info->enable[i])
				continue;

			info->lw5[i][info->i_1m] = 0;
			info->hw5[i][info->i_1m] = 0;
		}
	}

	cgtime(&current);
	device_tdiff = tdiff(&current, &(info->last_5m));
	if (opt_avalon4_autov && (device_tdiff > 480.0 || device_tdiff < 0)) {
		copy_time(&info->last_5m, &current);

		for (i = 1; i < AVA4_DEFAULT_MODULARS; i++) {
			if (!info->enable[i])
				continue;

			a = 0;
			b = 0;
			for (j = 0; j < 6; j++) {
				a += info->lw5[i][j];
				b += info->hw5[i][j];
			}

			hwp = a ? (double)b / (double)a : 0;
			if (hwp > AVA4_DH_INC && (info->set_voltage[i] < info->set_voltage[0] + 125)) {
				info->set_voltage[i] += 125;
				applog(LOG_NOTICE, "%s %d: Automatic increase module[%d] voltage to %d",
				       avalon4->drv->name, avalon4->device_id, i, info->set_voltage[i]);
			}
			if (hwp < AVA4_DH_DEC && (info->set_voltage[i] > info->set_voltage[0] - (4 * 125))) {
				info->set_voltage[i] -= 125;
				applog(LOG_NOTICE, "%s %d: Automatic decrease module[%d] voltage to %d",
				       avalon4->drv->name, avalon4->device_id, i, info->set_voltage[i]);
			}

		}
	}

	for (i = 1; i < AVA4_DEFAULT_MODULARS; i++) {
		if (info->set_voltage[i] != info->set_voltage[0])
			break;
	}

	if (i < AVA4_DEFAULT_MODULARS)
		info->set_voltage_broadcat = 0;
	else
		info->set_voltage_broadcat = 1;

	h = 0;
	for (i = 1; i < AVA4_DEFAULT_MODULARS; i++) {
		h += info->enable[i] ? (info->local_work[i] - info->hw_work[i]) : 0;
		info->local_work[i] = 0;
		info->hw_work[i] = 0;
	}
	return h * 0xffffffffull;
}

#define STATBUFLEN 512
static struct api_data *avalon4_api_stats(struct cgpu_info *cgpu)
{
	struct api_data *root = NULL;
	struct avalon4_info *info = cgpu->device_data;
	int i, j;
	uint32_t a,b ;
	double hwp, diff;
	char buf[256];
	char statbuf[AVA4_DEFAULT_MODULARS][STATBUFLEN];
	struct timeval current;

	memset(statbuf, 0, AVA4_DEFAULT_MODULARS * STATBUFLEN);

	for (i = 1; i < AVA4_DEFAULT_MODULARS; i++) {
		if(info->mod_type[i] == AVA4_TYPE_NULL)
			continue;
		sprintf(buf, "Ver[%s]", info->mm_version[i]);
		strcat(statbuf[i], buf);
	}

	for (i = 1; i < AVA4_DEFAULT_MODULARS; i++) {
		if(info->mod_type[i] == AVA4_TYPE_NULL)
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

#if 0
	for (i = 1; i < AVA4_DEFAULT_MODULARS; i++) {
		if (info->mod_type[i] == AVA4_TYPE_NULL)
			continue;

		strcat(statbuf[i], " MW[");
		for (j = 0; j < AVA4_DEFAULT_MINERS; j++) {
			sprintf(buf, "%d ", info->matching_work[i][j]);
			strcat(statbuf[i], buf);
		}
		statbuf[i][strlen(statbuf[i]) - 1] = ']';
	}
#endif

	for (i = 1; i < AVA4_DEFAULT_MODULARS; i++) {
		if(info->mod_type[i] == AVA4_TYPE_NULL)
			continue;
		sprintf(buf, " LW[%"PRIu64"]", info->local_works[i]);
		strcat(statbuf[i], buf);
	}
	for (i = 1; i < AVA4_DEFAULT_MODULARS; i++) {
		if(info->mod_type[i] == AVA4_TYPE_NULL)
			continue;
		sprintf(buf, " HW[%"PRIu64"]", info->hw_works[i]);
		strcat(statbuf[i], buf);
	}
	for (i = 1; i < AVA4_DEFAULT_MODULARS; i++) {
		if(info->mod_type[i] == AVA4_TYPE_NULL)
			continue;
		a = info->hw_works[i];
		b = info->local_works[i];
		hwp = b ? ((double)a / (double)b) * 100: 0;

		sprintf(buf, " DH[%.3f%%]", hwp);
		strcat(statbuf[i], buf);
	}
	for (i = 1; i < AVA4_DEFAULT_MODULARS; i++) {
		if(info->mod_type[i] == AVA4_TYPE_NULL)
			continue;

		a = 0;
		b = 0;
		for (j = 0; j < 6; j++) {
			a += info->lw5[i][j];
			b += info->hw5[i][j];
		}

		cgtime(&current);
		diff = tdiff(&current, &(info->last_1m)) + 300.0;

		hwp = a ? (double)b / (double)a * 100 : 0;

		sprintf(buf, " GHS5m[%.2f] DH5m[%.3f%%]", ((double)a - (double)b) * 4.295 / diff, hwp);
		strcat(statbuf[i], buf);
	}
	for (i = 1; i < AVA4_DEFAULT_MODULARS; i++) {
		if(info->mod_type[i] == AVA4_TYPE_NULL)
			continue;
		sprintf(buf, " Temp[%d]", info->temp[i]);
		strcat(statbuf[i], buf);
	}
	for (i = 1; i < AVA4_DEFAULT_MODULARS; i++) {
		if(info->mod_type[i] == AVA4_TYPE_NULL)
			continue;
		sprintf(buf, " Fan[%d]", info->fan[i]);
		strcat(statbuf[i], buf);
	}
	for (i = 1; i < AVA4_DEFAULT_MODULARS; i++) {
		if(info->mod_type[i] == AVA4_TYPE_NULL)
			continue;
		sprintf(buf, " Vol[%.4f]", (float)info->get_voltage[i] / 10000);
		strcat(statbuf[i], buf);
	}
	for (i = 1; i < AVA4_DEFAULT_MODULARS; i++) {
		if(info->mod_type[i] == AVA4_TYPE_NULL)
			continue;
		sprintf(buf, " Freq[%.2f]", (float)info->get_frequency[i] / 1000);
		strcat(statbuf[i], buf);
	}
	for (i = 1; i < AVA4_DEFAULT_MODULARS; i++) {
		if(info->mod_type[i] == AVA4_TYPE_NULL)
			continue;
		sprintf(buf, " PG[%d]", info->power_good[i]);
		strcat(statbuf[i], buf);
	}
	for (i = 1; i < AVA4_DEFAULT_MODULARS; i++) {
		if(info->mod_type[i] == AVA4_TYPE_NULL)
			continue;
		sprintf(buf, " Led[%d]", info->led_red[i]);
		strcat(statbuf[i], buf);
	}

	for (i = 1; i < AVA4_DEFAULT_MODULARS; i++) {
		if(info->mod_type[i] == AVA4_TYPE_NULL)
			continue;
		sprintf(buf, "MM ID%d", i);
		root = api_add_string(root, buf, statbuf[i], true);
	}

	root = api_add_int(root, "MM Count", &(info->mm_count), true);
	root = api_add_bool(root, "Automatic Voltage", &opt_avalon4_autov, true);
	root = api_add_string(root, "AUC VER", info->auc_version, false);
	root = api_add_int(root, "AUC I2C Speed", &(info->auc_speed), true);
	root = api_add_int(root, "AUC I2C XDelay", &(info->auc_xdelay), true);
	root = api_add_int(root, "AUC ADC", &(info->auc_temp), true);

	return root;
}

static char *avalon4_set_device(struct cgpu_info *avalon4, char *option, char *setting, char *replybuf)
{
	int val, i;
	struct avalon4_info *info = avalon4->device_data;

	if (strcasecmp(option, "help") == 0) {
		sprintf(replybuf, "led|fan|voltage|frequency|pdelay");
		return replybuf;
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

		applog(LOG_NOTICE, "%s %d: Update polling delay to: %d",
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

		applog(LOG_NOTICE, "%s %d: Update fan to %d-%d",
		       avalon4->drv->name, avalon4->device_id,
		       opt_avalon4_fan_min, opt_avalon4_fan_max);

		return NULL;
	}

	if (strcasecmp(option, "frequency") == 0) {
		if (!setting || !*setting) {
			sprintf(replybuf, "missing frequency value");
			return replybuf;
		}

		if (set_avalon4_freq(setting)) {
			sprintf(replybuf, "invalid frequency value, valid range %d-%d",
				AVA4_DEFAULT_FREQUENCY_MIN, AVA4_DEFAULT_FREQUENCY_MAX);
			return replybuf;
		}

		applog(LOG_NOTICE, "%s %d: Update frequency to %d",
		       avalon4->drv->name, avalon4->device_id,
		       (opt_avalon4_freq[0] * 4 + opt_avalon4_freq[1] * 4 + opt_avalon4_freq[2]) / 9);

		return NULL;
	}

	if (strcasecmp(option, "led") == 0) {
		if (!setting || !*setting) {
			sprintf(replybuf, "missing module_id setting");
			return replybuf;
		}

		val = atoi(setting);
		if (val < 1 || val >= AVA4_DEFAULT_MODULARS) {
			sprintf(replybuf, "invalid module_id: %d, valid range 1-%d", val, AVA4_DEFAULT_MODULARS);
			return replybuf;
		}

		if (!info->enable[val]) {
			sprintf(replybuf, "the current module was disabled %d", val);
			return replybuf;
		}

		info->led_red[val] = !info->led_red[val];

		applog(LOG_NOTICE, "%s %d: Module:%d, LED: %s",
		       avalon4->drv->name, avalon4->device_id,
		       val, info->led_red[val] ? "on" : "off");

		return NULL;
	}

	if (strcasecmp(option, "voltage") == 0) {
		int val_mod, val_volt, ret;

		if (!setting || !*setting) {
			sprintf(replybuf, "missing voltage value");
			return replybuf;
		}

		ret = sscanf(setting, "%d-%d", &val_mod, &val_volt);
		if (ret != 2) {
			sprintf(replybuf, "invalid voltage parameter, format: moduleid-voltage");
			return replybuf;
		}

		if (val_mod < 0 || val_mod >= AVA4_DEFAULT_MODULARS ||
		    val_volt < AVA4_DEFAULT_VOLTAGE_MIN || val_volt > AVA4_DEFAULT_VOLTAGE_MAX) {
			sprintf(replybuf, "invalid module_id or voltage value, valid module_id range %d-%d, valid voltage range %d-%d",
				0, AVA4_DEFAULT_MODULARS,
				AVA4_DEFAULT_VOLTAGE_MIN, AVA4_DEFAULT_VOLTAGE_MAX);
			return replybuf;
		}

		if (!info->enable[val_mod]) {
			sprintf(replybuf, "the current module was disabled %d", val_mod);
			return replybuf;
		}

		info->set_voltage[val_mod] = val_volt;

		if (val_mod == AVA4_MODULE_BROADCAST) {
			for (i = 1; i < AVA4_DEFAULT_MODULARS; i++)
				info->set_voltage[i] = val_volt;
			info->set_voltage_broadcat = 1;
		} else
			info->set_voltage_broadcat = 0;

		applog(LOG_NOTICE, "%s %d: Update module[%d] voltage to %d",
		       avalon4->drv->name, avalon4->device_id, val_mod, val_volt);

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
	int i, frequency;

	for (i = 1; i < AVA4_DEFAULT_MODULARS; i++) {
		if (!info->enable[i])
			continue;

		if (fanmax <= info->fan_pct[i])
			fanmax = info->fan_pct[i];
		if (fanmin >= info->fan_pct[i])
			fanmin = info->fan_pct[i];

		if (voltsmax <= info->get_voltage[i])
			voltsmax = info->get_voltage[i];
		if (voltsmin >= info->get_voltage[i])
			voltsmin = info->get_voltage[i];
	}
#if 0
	tailsprintf(buf, bufsiz, "%2dMMs %.4fV-%.4fV %4dMhz %2dC %3d%%-%3d%%",
		    info->mm_count, (float)voltsmin / 10000, (float)voltsmax / 10000,
		    (info->set_frequency[0] * 4 + info->set_frequency[1] * 4 + info->set_frequency[2]) / 9,
		    temp, fanmin, fanmax);
#endif
	frequency = (info->set_frequency[0] * 4 + info->set_frequency[1] * 4 + info->set_frequency[2]) / 9;
	tailsprintf(buf, bufsiz, "%4dMhz %2dC %3d%% %.3fV", frequency,
		    temp, fanmin, (float)voltsmax / 10000);
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
};
