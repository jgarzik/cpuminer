/*
 * Copyright 2013-2014 Con Kolivas <kernel@kolivas.org>
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

#ifdef USE_AVALON4

#define AVA4_DEFAULT_FAN_MIN	5 /* % */
#define AVA4_DEFAULT_FAN_MAX	85
/* Percentage required to make sure fan starts spinning, then we can go down */
#define AVA4_DEFAULT_FAN_START	15

#define AVA4_DEFAULT_TEMP_TARGET	42
#define AVA4_DEFAULT_TEMP_OVERHEAT	65

#define AVA4_DEFAULT_VOLTAGE_MIN	4000
#define AVA4_DEFAULT_VOLTAGE_MAX	9000

#define AVA4_DEFAULT_FREQUENCY_MIN	100
#define AVA4_DEFAULT_FREQUENCY_MAX	1000

#define AVA4_DEFAULT_MODULARS	64
#define AVA4_DEFAULT_MINERS	10
#define AVA4_DEFAULT_ASIC_COUNT	4

#define AVA4_DEFAULT_VOLTAGE	6875
#define AVA4_DEFAULT_FREQUENCY	200
#define AVA4_DEFAULT_POLLING_DELAY	20 /* ms */

#define AVA4_DH_INC	0.03
#define AVA4_DH_DEC	0.001

#define AVA4_PWM_MAX		0x3FF

#define AVA4_AUC_VER_LEN	12	/* Version length: 12 (AUC-YYYYMMDD) */
#define AVA4_AUC_SPEED		400000
#define AVA4_AUC_XDELAY  	9600	/* 4800 = 1ms in AUC (11U14)  */
#define AVA4_AUC_P_SIZE		64


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

/* Have to with I2C address */
#define AVA4_P_POLLING	0x30
#define AVA4_P_REQUIRE	0x31
#define AVA4_P_TEST	0x32

/* Back to host */
#define AVA4_P_ACKDETECT	0x40
#define AVA4_P_STATUS		0x41
#define AVA4_P_NONCE		0x42
#define AVA4_P_TEST_RET		0x43

#define AVA4_MODULE_BROADCAST	0
/* Endof Avalon4 protocol package type */

#define AVA4_MM40_PREFIXSTR	"40"
#define AVA4_MM41_PREFIXSTR	"41"
#define AVA4_MM_VERNULL		"NONE"

#define AVA4_TYPE_MM40		40
#define AVA4_TYPE_MM41		41
#define AVA4_TYPE_NULL		00

#define AVA4_IIC_RESET		0xa0
#define AVA4_IIC_INIT		0xa1
#define AVA4_IIC_DEINIT		0xa2
#define AVA4_IIC_XFER		0xa5
#define AVA4_IIC_INFO		0xa6

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

	int set_frequency[3];
	int set_voltage[AVA4_DEFAULT_MODULARS];
	int set_voltage_broadcat;

	int mod_type[AVA4_DEFAULT_MODULARS];
	bool enable[AVA4_DEFAULT_MODULARS];

	struct timeval elapsed[AVA4_DEFAULT_MODULARS];
	char mm_version[AVA4_DEFAULT_MODULARS][AVA4_MM_VER_LEN + 1];
	uint8_t mm_dna[AVA4_DEFAULT_MODULARS][AVA4_MM_DNA_LEN + 1];
	int get_voltage[AVA4_DEFAULT_MODULARS];
	int get_frequency[AVA4_DEFAULT_MODULARS];
	int power_good[AVA4_DEFAULT_MODULARS];
	int fan_pct[AVA4_DEFAULT_MODULARS];
	int fan[AVA4_DEFAULT_MODULARS];
	int temp[AVA4_DEFAULT_MODULARS];
	int led_red[AVA4_DEFAULT_MODULARS];

	uint64_t local_works[AVA4_DEFAULT_MODULARS];
	uint64_t hw_works[AVA4_DEFAULT_MODULARS];

	uint32_t local_work[AVA4_DEFAULT_MODULARS];
	uint32_t hw_work[AVA4_DEFAULT_MODULARS];

	uint32_t lw5[AVA4_DEFAULT_MODULARS][6];
	uint32_t hw5[AVA4_DEFAULT_MODULARS][6];
	int i_1m;
	struct timeval last_5m;
	struct timeval last_1m;

	int matching_work[AVA4_DEFAULT_MODULARS][AVA4_DEFAULT_MINERS];
	int chipmatching_work[AVA4_DEFAULT_MODULARS][AVA4_DEFAULT_MINERS][4];
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
extern int opt_avalon4_temp_target;
extern int opt_avalon4_overheat;
extern int opt_avalon4_polling_delay;
extern int opt_avalon4_aucspeed;
extern int opt_avalon4_aucxdelay;
extern int opt_avalon4_ntime_offset;
#endif /* USE_AVALON4 */
#endif	/* _AVALON4_H_ */
