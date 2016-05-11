/*
 * Copyright 2013-2015 Con Kolivas <kernel@kolivas.org>
 * Copyright 2012-2014 Xiangfu <xiangfu@openmobilefree.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 3 of the License, or (at your option)
 * any later version.  See COPYING for more details.
 */

#ifndef _AVALON4_H_
#define _AVALON4_H_

#include "util.h"
#include "i2c-context.h"

#ifdef USE_AVALON4

#define AVA4_DEFAULT_FAN_MIN	10 /* % */
#define AVA4_DEFAULT_FAN_MAX	100
/* Percentage required to make sure fan starts spinning, then we can go down */
#define AVA4_DEFAULT_FAN_START	15
#define AVA4_FREEZESAFE_FAN	10

#define AVA4_DEFAULT_TEMP_TARGET	65
#define AVA4_DEFAULT_TEMP_OVERHEAT	85
#define AVA4_MM40_TEMP_TARGET	42
#define AVA4_MM40_TEMP_OVERHEAT	65
#define AVA4_MM60_TEMP_FREQADJ	70

#define AVA4_DEFAULT_VOLTAGE_MIN	4000
#define AVA4_DEFAULT_VOLTAGE_MAX	9000
#define AVA4_FREEZESAFE_VOLTAGE		4000

#define AVA4_DEFAULT_FREQUENCY_MIN	100
#define AVA4_DEFAULT_FREQUENCY_MAX	1000
#define AVA4_FREEZESAFE_FREQUENCY	100
#define AVA4_MM60_FREQUENCY_MAX	500

#define AVA4_DEFAULT_MODULARS	7	/* Only support 6 modules maximum with one AUC */
#define AVA4_DEFAULT_MINER_MAX	10
#define AVA4_DEFAULT_ASIC_MAX	40
#define AVA4_DEFAULT_ADC_MAX	6 /* RNTC1-4, VCC12, VCC3VC */
#define AVA4_DEFAULT_PLL_MAX	7

#define AVA4_DEFAULT_MINER_CNT	10
#define AVA4_DEFAULT_ASIC_CNT	4
#define AVA4_MM50_MINER_CNT	2
#define AVA4_MM50_ASIC_CNT	16
#define AVA4_MM60_MINER_CNT	2
#define AVA4_MM60_ASIC_CNT	40

#define AVA4_DEFAULT_VOLTAGE	6875
#define AVA4_DEFAULT_FREQUENCY	200
#define AVA4_DEFAULT_POLLING_DELAY	20 /* ms */

#define AVA4_DEFAULT_ADJ_TIMES	6
#define AVA4_DEFAULT_NTCB	3450
#define AVA4_DEFAULT_NCHECK	true
#define AVA4_DEFAULT_SPEED_BINGO	255
#define AVA4_DEFAULT_SPEED_ERROR	3

#define AVA4_DEFAULT_SMARTSPEED_OFF 0
#define AVA4_DEFAULT_SMARTSPEED_MODE1 1
#define AVA4_DEFAULT_SMARTSPEED_MODE2 2
#define AVA4_DEFAULT_SMARTSPEED_MODE3 3
#define AVA4_DEFAULT_SMART_SPEED	(AVA4_DEFAULT_SMARTSPEED_MODE3)

#define AVA4_DEFAULT_IIC_DETECT	false

#define AVA4_DH_INC	0.03
#define AVA4_DH_DEC	0.002

#define AVA4_PWM_MAX	0x3FF
#define AVA4_ADC_MAX	0x3FF
#define AVA4_DRV_DIFFMAX	1024

#define AVA4_AUC_VER_LEN	12	/* Version length: 12 (AUC-YYYYMMDD) */
#define AVA4_AUC_SPEED		400000
#define AVA4_AUC_XDELAY  	19200	/* 4800 = 1ms in AUC (11U14)  */
#define AVA4_AUC_P_SIZE		64

#define AVA4_MOD_CUSTOM 0x0
#define AVA4_MOD_ECO    0x1
#define AVA4_MOD_NORMAL 0x2
#define AVA4_MOD_TURBO  0x3

#define AVA4_CONNECTER_AUC	1
#define AVA4_CONNECTER_IIC	2

/* Avalon4 protocol package type from MM protocol.h
 * https://github.com/Canaan-Creative/MM/blob/avalon4/firmware/protocol.h */
#define AVA4_MM_VER_LEN	15
#define AVA4_MM_DNA_LEN	8
#define AVA4_H1	'C'
#define AVA4_H2	'N'

#define AVA4_P_COINBASE_SIZE	(6 * 1024 + 64)
#define AVA4_P_MERKLES_COUNT	30

#define AVA4_P_COUNT	40
#define AVA4_P_DATA_LEN 32

/* Broadcase with block iic_write*/
#define AVA4_P_DETECT	0x10

/* Broadcase With non-block iic_write*/
#define AVA4_P_STATIC	0x11
#define AVA4_P_JOB_ID	0x12
#define AVA4_P_COINBASE	0x13
#define AVA4_P_MERKLES	0x14
#define AVA4_P_HEADER	0x15
#define AVA4_P_TARGET	0x16

/* Broadcase or Address */
#define AVA4_P_SET	0x20
#define AVA4_P_FINISH	0x21
#define AVA4_P_SET_VOLT 0x22
#define AVA4_P_SET_FREQ 0x23

/* Have to with I2C address */
#define AVA4_P_POLLING	0x30
#define AVA4_P_REQUIRE	0x31
#define AVA4_P_TEST	0x32
#define AVA4_P_RSTMMTX	0x33
#define AVA4_P_GET_VOLT 0x34

/* Back to host */
#define AVA4_P_ACKDETECT	0x40
#define AVA4_P_STATUS		0x41
#define AVA4_P_NONCE		0x42
#define AVA4_P_TEST_RET		0x43
#define AVA4_P_STATUS_LW        0x44
#define AVA4_P_STATUS_HW        0x45
#define AVA4_P_STATUS_VOLT	0x46
#define AVA4_P_STATUS_MA	0x47
#define AVA4_P_STATUS_M	0x48

#define AVA4_MODULE_BROADCAST	0
/* Endof Avalon4 protocol package type */

#define AVA4_MM40_PREFIXSTR	"40"
#define AVA4_MM41_PREFIXSTR	"41"
#define AVA4_MM50_PREFIXSTR	"50"
#define AVA4_MM60_PREFIXSTR	"60"
#define AVA4_MM_VERNULL		"NONE"

#define AVA4_TYPE_MM40		40
#define AVA4_TYPE_MM41		41
#define AVA4_TYPE_MM50		50
#define AVA4_TYPE_MM60		60
#define AVA4_TYPE_NULL		00

#define AVA4_IIC_RESET		0xa0
#define AVA4_IIC_INIT		0xa1
#define AVA4_IIC_DEINIT		0xa2
#define AVA4_IIC_XFER		0xa5
#define AVA4_IIC_INFO		0xa6

#define AVA4_FREQ_INIT_MODE	0x0
#define AVA4_FREQ_CUTOFF_MODE	0x1
#define AVA4_FREQ_TEMPADJ_MODE	0x2
#define AVA4_FREQ_PLLADJ_MODE	0x3

/* pll check range [0, 7680], 0 means turn off check */
#define AVA4_DEFAULT_LEAST_PLL	768
#define AVA4_DEFAULT_MOST_PLL	256

/* seconds */
#define AVA4_DEFAULT_FDEC_TIME	60.0
#define AVA4_DEFAULT_FINC_TIME	1200.0
#define AVA4_DEFAULT_FAVG_TIME	(15 * 60.0)
#define AVA4_DEFAULT_FREQADJ_TIME	60

#define AVA4_DEFAULT_DELTA_T	0
#define AVA4_DEFAULT_DELTA_FREQ	100

struct avalon4_pkg {
	uint8_t head[2];
	uint8_t type;
	uint8_t opt;
	uint8_t idx;
	uint8_t cnt;
	uint8_t data[32];
	uint8_t crc[2];
};
#define avalon4_ret avalon4_pkg

struct avalon4_info {
	cglock_t update_lock;

	int polling_first;
	int polling_err_cnt[AVA4_DEFAULT_MODULARS];
	int xfer_err_cnt;

	int pool_no;
	struct pool pool0;
	struct pool pool1;
	struct pool pool2;

	struct timeval last_fan;
	struct timeval last_stratum;

	char auc_version[AVA4_AUC_VER_LEN + 1];
	int auc_speed;
	int auc_xdelay;
	int auc_temp;

	int mm_count;
	uint8_t connecter;

	unsigned int set_frequency[AVA4_DEFAULT_MODULARS][3];
	unsigned int set_smart_frequency[AVA4_DEFAULT_MODULARS][3];
	int set_frequency_i[AVA4_DEFAULT_MODULARS][AVA4_DEFAULT_MINER_MAX][AVA4_DEFAULT_ASIC_MAX][3];
	int set_voltage[AVA4_DEFAULT_MODULARS];
	uint16_t set_voltage_i[AVA4_DEFAULT_MODULARS][AVA4_DEFAULT_MINER_MAX];
	int8_t set_voltage_offset[AVA4_DEFAULT_MODULARS][AVA4_DEFAULT_MINER_MAX];

	int mod_type[AVA4_DEFAULT_MODULARS];
	bool enable[AVA4_DEFAULT_MODULARS];

	struct timeval elapsed[AVA4_DEFAULT_MODULARS];
	struct timeval firsthash;
	char mm_version[AVA4_DEFAULT_MODULARS][AVA4_MM_VER_LEN + 1];
	uint8_t mm_dna[AVA4_DEFAULT_MODULARS][AVA4_MM_DNA_LEN + 1];
	int get_voltage[AVA4_DEFAULT_MODULARS];
	int get_voltage_i[AVA4_DEFAULT_MODULARS][AVA4_DEFAULT_MINER_MAX];
	int get_frequency[AVA4_DEFAULT_MODULARS];
	int power_good[AVA4_DEFAULT_MODULARS];
	int error_code[AVA4_DEFAULT_MODULARS];
	int fan_pct[AVA4_DEFAULT_MODULARS];
	int fan[AVA4_DEFAULT_MODULARS];
	int temp[AVA4_DEFAULT_MODULARS];
	int led_red[AVA4_DEFAULT_MODULARS];
	uint16_t adc[AVA4_DEFAULT_MODULARS][AVA4_DEFAULT_ADC_MAX];
	uint16_t pll_sel[AVA4_DEFAULT_MODULARS][AVA4_DEFAULT_PLL_MAX];

	uint64_t local_works[AVA4_DEFAULT_MODULARS];
	uint64_t local_works_i[AVA4_DEFAULT_MODULARS][AVA4_DEFAULT_MINER_MAX];
	uint64_t hw_works[AVA4_DEFAULT_MODULARS];
	uint64_t hw_works_i[AVA4_DEFAULT_MODULARS][AVA4_DEFAULT_MINER_MAX];

	uint32_t local_work[AVA4_DEFAULT_MODULARS];
	uint32_t hw_work[AVA4_DEFAULT_MODULARS];

	uint32_t lw5[AVA4_DEFAULT_MODULARS][AVA4_DEFAULT_ADJ_TIMES];
	uint32_t lw5_i[AVA4_DEFAULT_MODULARS][AVA4_DEFAULT_MINER_MAX][AVA4_DEFAULT_ADJ_TIMES];
	uint32_t hw5[AVA4_DEFAULT_MODULARS][AVA4_DEFAULT_ADJ_TIMES];
	uint32_t hw5_i[AVA4_DEFAULT_MODULARS][AVA4_DEFAULT_MINER_MAX][AVA4_DEFAULT_ADJ_TIMES];
	int i_5s;
	struct timeval last_30s;
	struct timeval last_5s;
	struct timeval last_finc[AVA4_DEFAULT_MODULARS];
	struct timeval last_fdec[AVA4_DEFAULT_MODULARS];
	struct timeval last_favg[AVA4_DEFAULT_MODULARS];
	struct timeval last_fadj;
	struct timeval last_tcheck;

	int matching_work[AVA4_DEFAULT_MODULARS][AVA4_DEFAULT_MINER_MAX];
	int chipmatching_work[AVA4_DEFAULT_MODULARS][AVA4_DEFAULT_MINER_MAX][AVA4_DEFAULT_ASIC_MAX];
	uint8_t saved[AVA4_DEFAULT_MODULARS];
	uint8_t adjflag[AVA4_DEFAULT_MODULARS][AVA4_DEFAULT_MINER_MAX];
	uint8_t cutoff[AVA4_DEFAULT_MODULARS];
	uint8_t miner_count[AVA4_DEFAULT_MODULARS];
	uint8_t asic_count[AVA4_DEFAULT_MODULARS];
	int ntime_offset[AVA4_DEFAULT_MODULARS];
	bool autov[AVA4_DEFAULT_MODULARS];
	uint8_t ma_sum[AVA4_DEFAULT_MODULARS][AVA4_DEFAULT_MINER_MAX][AVA4_DEFAULT_ASIC_MAX];
	uint32_t newnonce;
	uint32_t total_asics[AVA4_DEFAULT_MODULARS];
	int toverheat[AVA4_DEFAULT_MODULARS];
	int temp_target[AVA4_DEFAULT_MODULARS];
	uint8_t speed_bingo[AVA4_DEFAULT_MODULARS];
	uint8_t speed_error[AVA4_DEFAULT_MODULARS];
	uint32_t freq_mode[AVA4_DEFAULT_MODULARS];
	struct i2c_ctx *i2c_slaves[AVA4_DEFAULT_MODULARS];
	int last_maxtemp[AVA4_DEFAULT_MODULARS];
};

struct avalon4_iic_info {
	uint8_t iic_op;
	union {
		uint32_t aucParam[2];
		uint8_t slave_addr;
	} iic_param;
};

#define AVA4_WRITE_SIZE (sizeof(struct avalon4_pkg))
#define AVA4_READ_SIZE AVA4_WRITE_SIZE

#define AVA4_SEND_OK 0
#define AVA4_SEND_ERROR -1

extern char *set_avalon4_fan(char *arg);
extern char *set_avalon4_temp(char *arg);
extern char *set_avalon4_freq(char *arg);
extern char *set_avalon4_voltage(char *arg);
extern bool opt_avalon4_autov;
extern bool opt_avalon4_freezesafe;
extern int opt_avalon4_temp_target;
extern int opt_avalon4_overheat;
extern int opt_avalon4_polling_delay;
extern int opt_avalon4_aucspeed;
extern int opt_avalon4_aucxdelay;
extern int opt_avalon4_ntime_offset;
extern int opt_avalon4_miningmode;
extern int opt_avalon4_ntcb;
extern int opt_avalon4_freq_min;
extern int opt_avalon4_freq_max;
extern bool opt_avalon4_noncecheck;
extern int opt_avalon4_smart_speed;
extern int opt_avalon4_speed_bingo;
extern int opt_avalon4_speed_error;
extern int opt_avalon4_least_pll_check;
extern int opt_avalon4_most_pll_check;
extern bool opt_avalon4_iic_detect;
extern int opt_avalon4_freqadj_time;
extern int opt_avalon4_delta_temp;
extern int opt_avalon4_delta_freq;
extern int opt_avalon4_freqadj_temp;
#endif /* USE_AVALON4 */
#endif	/* _AVALON4_H_ */
