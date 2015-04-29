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

#define AVAU_DEFAULT_MINERS	1
#define AVAU_DEFAULT_ASIC_COUNT	5

#define AVAU_VERSION_LEN        15
#define AVAU_H1 'C'
#define AVAU_H2 'N'

#define AVAU_P_COUNT    40
#define AVAU_P_DATA_LEN (AVAU_P_COUNT - 8)      //32Bytes

#define AVAU_P_DETECT   0x10
#define AVAU_P_SET_VOLT 0x11
#define AVAU_P_SET_FREQ 0x12
#define AVAU_P_WORK             0x13
#define AVAU_P_POLLING  0x14
#define AVAU_P_REQUIRE  0x15
#define AVAU_P_TEST     0x16

#define AVAU_P_ACKDETECT 0x20
#define AVAU_P_GET_VOLT  0x21
#define AVAU_P_GET_FREQ  0x22
#define AVAU_P_NONCE     0x23
#define AVAU_P_STATUS    0x24
#define AVAU_P_TEST_RET  0x25

struct avalonu_pkg {
	uint8_t head[2];
	uint8_t type;
	uint8_t opt;
	uint8_t idx;
	uint8_t cnt;
	uint8_t data[32];
	uint8_t crc[2];
};
#define avalonu_ret avalonu_pkg

struct avalonu_info {
	char avau_ver[AVAU_VERSION_LEN];
	struct thr_info *mainthr;
	pthread_t read_thr;
	uint8_t workinit;
	uint32_t nonce_cnts;
};

#define AVAU_WRITE_SIZE (sizeof(struct avalonu_pkg))
#define AVAU_READ_SIZE AVAU_WRITE_SIZE

#define AVAU_SEND_OK 0
#define AVAU_SEND_ERROR -1

#endif /* USE_AVALON4_MINI */
#endif	/* _AVALON4_MINI_H_ */
