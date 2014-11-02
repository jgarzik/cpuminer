/*
 * Copyright 2013-2014 Con Kolivas <kernel@kolivas.org>
 * Copyright 2012-2014 Xiangfu <xiangfu@openmobilefree.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 3 of the License, or (at your option)
 * any later version.  See COPYING for more details.
 */

#ifndef _HASHRATIO_H_
#define _HASHRATIO_H_

#include "miner.h"
#include "util.h"

#ifdef USE_HASHRATIO
char *opt_hashratio_freq;

#define HRTO_MINER_THREADS	1

#define HRTO_RESET_FAULT_DECISECONDS  10
#define HRTO_IO_SPEED                 115200

#define HRTO_DEFAULT_MODULARS          5
#define HRTO_DEFAULT_MINERS_PER_MODULAR 16
/* total chips number */
#define HRTO_DEFAULT_MINERS  (HRTO_DEFAULT_MODULARS * 16)

#define HRTO_PWM_MAX          0x3FF
#define HRTO_DEFAULT_FAN      20  /* N% */
#define HRTO_DEFAULT_FAN_MIN  50  /* N% */
#define HRTO_DEFAULT_FAN_MAX  100 /* N% */

#define HRTO_DEFAULT_FREQUENCY      280 /* MHz */
#define HRTO_DEFAULT_FREQUENCY_MIN  100
#define HRTO_DEFAULT_FREQUENCY_MAX  750

#define HRTO_FAN_COUNT  2
//#define HRTO_TEMP_COUNT 1

/* Hashratio protocol package type */
#define HRTO_H1  'H'
#define HRTO_H2  'R'

#define HRTO_P_COINBASE_SIZE  (6 * 1024)
#define HRTO_P_MERKLES_COUNT  20

#define HRTO_P_COUNT	39
#define HRTO_P_DATA_LEN		(HRTO_P_COUNT - 7)

#define HRTO_P_DETECT    10  // 0x0a
#define HRTO_P_STATIC    11  // 0x0b
#define HRTO_P_JOB_ID    12  // 0x0c
#define HRTO_P_COINBASE  13  // 0x0d
#define HRTO_P_MERKLES   14  // 0x0e
#define HRTO_P_HEADER    15  // 0x0f
#define HRTO_P_POLLING   16  // 0x10
#define HRTO_P_TARGET    17  // 0x11
#define HRTO_P_REQUIRE   18  // 0x12
#define HRTO_P_SET       19  // 0x13
#define HRTO_P_TEST      20  // 0x14

#define HRTO_P_ACK        51 // 0x33
#define HRTO_P_NAK        52 // 0x34
#define HRTO_P_NONCE      53 // 0x35
#define HRTO_P_STATUS     54 // 0x36
#define HRTO_P_ACKDETECT  55 // 0x37
#define HRTO_P_TEST_RET   56 // 0x38
/* Hashratio protocol package type */

struct hashratio_pkg {
	uint8_t head[2];
	uint8_t type;
	uint8_t idx;
	uint8_t cnt;
	uint8_t data[32];
	uint8_t crc[2];
};
#define hashratio_ret hashratio_pkg

struct hashratio_info {
	int default_freq;

	int fan_pwm;

	int     temp;
	int     fan[HRTO_FAN_COUNT];
//	uint8_t freq[HRTO_DEFAULT_MINERS];
	uint8_t target_freq[HRTO_DEFAULT_MINERS];

	int temp_max;
	int temp_history_count;
	int temp_history_index;
	int temp_sum;
	int temp_old;

	struct timeval last_stratum;
	struct pool pool;
	int pool_no;

	int local_works;
	int hw_works;
	int matching_work[HRTO_DEFAULT_MINERS];
	int local_work;
	int hw_work;

//	uint32_t get_result_counter;
	
	char mm_version[16];
};

#define HRTO_WRITE_SIZE (sizeof(struct hashratio_pkg))
#define HRTO_READ_SIZE HRTO_WRITE_SIZE

#define HRTO_GETS_OK 0
#define HRTO_GETS_TIMEOUT -1
#define HRTO_GETS_RESTART -2
#define HRTO_GETS_ERROR -3

#define HRTO_SEND_OK 0
#define HRTO_SEND_ERROR -1

#define hashratio_open(devpath, baud, purge)  serial_open(devpath, baud, HRTO_RESET_FAULT_DECISECONDS, purge)
#define hashratio_close(fd) close(fd)

extern char *set_hashratio_fan(char *arg);
extern char *set_hashratio_freq(char *arg);

#endif /* USE_HASHRATIO */
#endif	/* _HASHRATIO_H_ */
