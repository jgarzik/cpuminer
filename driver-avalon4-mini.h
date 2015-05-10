/*
 * Copyright 2014-2015 Mikeqin <Fengling.Qin@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 3 of the License, or (at your option)
 * any later version.  See COPYING for more details.
 */

#ifndef _AVALON4_MINI_H_
#define _AVALON4_MINI_H_

#include "util.h"

#ifdef USE_AVALON4_MINI

#define AVAM_DEFAULT_ASIC_COUNT		5
#define AVAM_DEFAULT_ARRAY_SIZE		(3 + 1) /* This is from the A3222 datasheet. 3 quequed work + 1 new work */

#define AVAM_DEFAULT_FREQUENCY_MIN	100
#define AVAM_DEFAULT_FREQUENCY_MAX	1000
#define AVAM_DEFAULT_FREQUENCY		100

#define AVAM_DEFAULT_VOLTAGE_MIN	4000
#define AVAM_DEFAULT_VOLTAGE_MAX	9000
#define AVAM_DEFAULT_VOLTAGE	6875

#define CAL_DELAY(freq)	(100 * AVAM_ASIC_TIMEOUT_100M / (freq) / 4)

/* 2 ^ 32 * 1000 / (10 ^ 8 * 3968 / 65.0) ~= 703 ms */
#define AVAM_ASIC_TIMEOUT_100M	703
/* Avalon4 protocol package type from MM protocol.h
 * https://github.com/Canaan-Creative/MM/blob/avalon4/firmware/protocol.h */
#define AVAM_MM_VER_LEN	15

#define AVAM_H1 'C'
#define AVAM_H2 'N'

#define AVAM_P_COUNT    40
#define AVAM_P_DATA_LEN 32

#define AVAM_P_DETECT   0x10

#define AVAM_P_SET_VOLT 0x22
#define AVAM_P_SET_FREQ 0x23
#define AVAM_P_WORK     0x24

#define AVAM_P_POLLING	0x30
#define AVAM_P_REQUIRE	0x31
#define AVAM_P_TEST	0x32

#define AVAM_P_ACKDETECT	0x40
#define AVAM_P_STATUS		0x41
#define AVAM_P_NONCE		0x42
#define AVAM_P_TEST_RET		0x43

struct avalonm_pkg {
	uint8_t head[2];
	uint8_t type;
	uint8_t opt;
	uint8_t idx;
	uint8_t cnt;
	uint8_t data[32];
	uint8_t crc[2];
};
#define avalonm_ret avalonm_pkg

struct avalonm_info {
	struct thr_info *thr;

	pthread_t process_thr;
	pthread_mutex_t lock;
	pthread_mutex_t qlock;
	cgsem_t qsem;

	char avam_ver[AVAM_MM_VER_LEN];
	int set_frequency[3];
	int set_voltage;
	uint32_t nonce_cnts;
};

#define AVAM_WRITE_SIZE (sizeof(struct avalonm_pkg))
#define AVAM_READ_SIZE AVAM_WRITE_SIZE

#define AVAM_SEND_OK 0
#define AVAM_SEND_ERROR -1

extern char *set_avalonm_freq(char *arg);
extern uint16_t opt_avalonm_ntime_offset;
extern char *set_avalonm_voltage(char *arg);
#endif /* USE_AVALON4_MINI */
#endif	/* _AVALON4_MINI_H_ */
