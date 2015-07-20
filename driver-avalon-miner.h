/*
 * Copyright 2014-2015 Mikeqin <Fengling.Qin@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 3 of the License, or (at your option)
 * any later version.  See COPYING for more details.
 */

#ifndef _AVALON_MINER_H_
#define _AVALON_MINER_H_

#include "util.h"

#ifdef USE_AVALON_MINER

#define AVAM_DEFAULT_ASIC_COUNT		5
#define AVAM_DEFAULT_ARRAY_SIZE		(3 + 2) /* This is from the A3222 datasheet. 3 quequed work + 1 new work + 1 auxiliary, because the device may buffer more work */

#define AVAM_DEFAULT_FREQUENCY_MIN	100
#define AVAM_DEFAULT_FREQUENCY_MAX	400
#define AVAM_DEFAULT_FREQUENCY		200

#define AVAM_DEFAULT_VOLTAGE_MIN	5000
#define AVAM_DEFAULT_VOLTAGE_MAX	9000
#define AVAM_DEFAULT_VOLTAGE		6500

#define AVAM_DEFAULT_SPISPEED		1000000

#define AVAM_ASIC_ALL	0

#define CAL_DELAY(freq)	(100 * AVAM_ASIC_TIMEOUT_100M / (freq) / 4)

/* 2 ^ 32 * 1000 / (10 ^ 8 * 3968 / 65.0) ~= 703 ms */
#define AVAM_ASIC_TIMEOUT_100M	703

#define AVAM_DEFAULT_MOV_TIMES	6
#define AVAM_DEFAULT_ADJ_INTERVAL	40

#define AVAM_HW_HIGH	20
#define AVAM_HW_LOW	6

/* Avalon4 protocol package type from MM protocol.h
 * https://github.com/Canaan-Creative/MM/blob/avalon4/firmware/protocol.h */
#define AVAM_MM_VER_LEN	15
#define AVAM_MM_DNA_LEN	8

#define AVAM_H1 'C'
#define AVAM_H2 'N'

#define AVAM_P_COUNT    40
#define AVAM_P_DATA_LEN 32

#define AVAM_P_DETECT   0x10

#define AVAM_P_SET_VOLT 0x22
#define AVAM_P_SET_FREQ 0x23
#define AVAM_P_WORK     0x24
#define AVAM_P_SETM     0x25

#define AVAM_P_POLLING	0x30
#define AVAM_P_REQUIRE	0x31
#define AVAM_P_TEST	0x32
#define AVAM_P_GET_FREQ	0x33

#define AVAM_P_ACKDETECT	0x40
#define AVAM_P_STATUS_M		0x41
#define AVAM_P_NONCE_M		0x42
#define AVAM_P_TEST_RET		0x43
#define AVAM_P_STATUS_FREQ	0x44

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

	uint32_t delay_ms;
	int power_on;

	unsigned char dna[AVAM_MM_DNA_LEN];
	unsigned char ver[AVAM_MM_VER_LEN + 1];
	uint32_t asic_cnts;
	uint32_t set_frequency[AVAM_DEFAULT_ASIC_COUNT][3];
	uint32_t opt_freq[AVAM_DEFAULT_ASIC_COUNT][3];
	uint32_t get_frequency[AVAM_DEFAULT_ASIC_COUNT][3];
	int set_voltage;
	int opt_voltage;
	uint32_t nonce_cnts;
	uint8_t usbfifo_cnt;
	uint8_t workfifo_cnt;
	uint8_t noncefifo_cnt;
	uint32_t crcerr_cnt;
	uint32_t power_good;
	uint32_t spi_speed;
	uint32_t led_status;
	uint32_t fan_pwm;
	uint32_t get_voltage;
	uint8_t freq_update;
	uint8_t freq_set;
	int hw_work[AVAM_DEFAULT_ASIC_COUNT];
	uint64_t matching_work[AVAM_DEFAULT_ASIC_COUNT];
	uint32_t adc[3];
	struct timeval elapsed;
	struct timeval lasttime;
	struct timeval lastadj;
	uint8_t time_i;
	int hw_work_i[AVAM_DEFAULT_ASIC_COUNT][AVAM_DEFAULT_MOV_TIMES];
};

#define AVAM_WRITE_SIZE (sizeof(struct avalonm_pkg))
#define AVAM_READ_SIZE AVAM_WRITE_SIZE

#define AVAM_SEND_OK 0
#define AVAM_SEND_ERROR -1

#define FLAG_SET(val, bit)	((val) |= (1 << (bit)))
#define FLAG_CLEAR(val, bit)	((val) &= ~(1 << (bit)))
#define FLAG_GET(val, bit)	(((val) >> (bit)) & 1)

extern char *set_avalonm_freq(char *arg);
extern uint8_t opt_avalonm_ntime_offset;
extern char *set_avalonm_voltage(char *arg);
extern uint32_t opt_avalonm_spispeed;
extern bool opt_avalonm_autof;
#endif /* USE_AVALON_MINER */
#endif	/* _AVALON_MINER_H_ */
