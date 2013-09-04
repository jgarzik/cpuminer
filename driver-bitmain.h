/*
 * Copyright 2013 BitMain project
 * Copyright 2013 BitMain <xlc1985@126.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 3 of the License, or (at your option)
 * any later version.  See COPYING for more details.
 */

#ifndef BITMAIN_H
#define BITMAIN_H

#ifdef USE_BITMAIN

#include "util.h"

#define BITMAIN_RESET_FAULT_DECISECONDS 1
#define BITMAIN_MINER_THREADS 1

#define BITMAIN_IO_SPEED		115200
#define BITMAIN_HASH_TIME_FACTOR	((float)1.67/0x32)
#define BITMAIN_RESET_PITCH	(300*1000*1000)

#define BITMAIN_TOKEN_TYPE_TXCONFIG 0x51
#define BITMAIN_TOKEN_TYPE_TXTASK   0x52
#define BITMAIN_TOKEN_TYPE_RXSTATUS 0x53

#define BITMAIN_DATA_TYPE_RXSTATUS  0xa1
#define BITMAIN_DATA_TYPE_RXNONCE   0xa2

#define BITMAIN_FAN_FACTOR 120
#define BITMAIN_PWM_MAX 0xA0
#define BITMAIN_DEFAULT_FAN_MIN 20
#define BITMAIN_DEFAULT_FAN_MAX 100
#define BITMAIN_DEFAULT_FAN_MAX_PWM 0xA0 /* 100% */
#define BITMAIN_DEFAULT_FAN_MIN_PWM 0x20 /*  20% */

#define BITMAIN_TEMP_TARGET 50
#define BITMAIN_TEMP_HYSTERESIS 3
#define BITMAIN_TEMP_OVERHEAT 60

#define BITMAIN_DEFAULT_TIMEOUT 0x2D
#define BITMAIN_MIN_FREQUENCY 256
#define BITMAIN_MAX_FREQUENCY 450
#define BITMAIN_TIMEOUT_FACTOR 12690
#define BITMAIN_DEFAULT_FREQUENCY 282
#define BITMAIN_DEFAULT_VOLTAGE 5
#define BITMAIN_DEFAULT_MINER_NUM 0x20
#define BITMAIN_DEFAULT_ASIC_NUM 0xA

#define BITMAIN_AUTO_CYCLE 1024

#define BITMAIN_FTDI_READSIZE 510
#define BITMAIN_USB_PACKETSIZE 512
#define BITMAIN_SENDBUF_SIZE 64
#define BITMAIN_READBUF_SIZE 8192
#define BITMAIN_RESET_TIMEOUT 100
#define BITMAIN_READ_TIMEOUT 18 /* Enough to only half fill the buffer */
#define BITMAIN_LATENCY 1

struct bitmain_txconfig_token {
	uint8_t token_type;
	uint8_t length;
	uint8_t reset                :1;
	uint8_t fan_eft              :1;
	uint8_t timeout_eft          :1;
	uint8_t frequency_eft        :1;
	uint8_t voltage_eft          :1;
	uint8_t chain_check_time_eft :1;
	uint8_t chip_config_eft      :1;
	uint8_t reserved1            :1;
	uint8_t reserved2;

	uint8_t miner_num;
	uint8_t asic_num;
	uint8_t fan_pwm_data;
	uint8_t timeout_data;

	uint16_t frequency;
	uint8_t voltage;
	uint8_t chain_check_time;

	uint32_t reg_data;
	uint8_t chip_address;
	uint8_t reg_address;
	uint16_t crc;
} __attribute__((packed, aligned(4)));

struct bitmain_txtask_token {
	uint8_t token_type;
	uint8_t length;
	uint8_t new_block            :1;
	uint8_t reserved1            :7;
	uint8_t reserved2;

	uint32_t work_id;
	uint8_t midstate[32];
	uint8_t data2[12];
	uint8_t reserved3[2];
	uint16_t crc;
} __attribute__((packed, aligned(4)));

struct bitmain_rxstatus_token {
	uint8_t token_type;
	uint8_t length;
	uint8_t chip_status_eft      :1;
	uint8_t detect_get           :1;
	uint8_t reserved1            :6;
	uint8_t reserved2;

	uint8_t chip_address;
	uint8_t reg_address;
	uint16_t crc;
} __attribute__((packed, aligned(4)));

struct bitmain_rxstatus_data {
	uint8_t data_type;
	uint8_t length;
	uint8_t chip_value_eft       :1;
	uint8_t reserved1            :7;
	uint8_t reserved2;

	uint32_t reg_value;
	uint8_t miner_num;
	uint8_t asic_num;
	uint8_t temp_num;
	uint8_t fan_num;
	uint8_t temp[1024];
	uint8_t fan[1024];
	uint16_t crc;
} __attribute__((packed, aligned(4)));

struct bitmain_rxnonce_data {
	uint8_t data_type;
	uint8_t length;
	uint8_t reserved1[2];
	uint32_t work_id;
	uint32_t nonce;
} __attribute__((packed, aligned(4)));

struct bitmain_info {
	int baud;
	int miner_num;
	int asic_num;
	int timeout;

	int fan_num;
	int fan[1024];
	int temp_num;
	int temp[1024];

	int temp_max;
	int temp_avg;
	int temp_history_count;
	int temp_history_index;
	int temp_sum;
	int temp_old;
	int fan_pwm;

	int no_matching_work;
	int matching_work[BITMAIN_DEFAULT_MINER_NUM];

	int frequency;

	struct thr_info *thr;
	pthread_t read_thr;
	pthread_t write_thr;
	pthread_mutex_t lock;
	pthread_mutex_t qlock;
	pthread_cond_t qcond;
	cgsem_t write_sem;
	int nonces;

	int auto_queued;
	int auto_nonces;
	int auto_hw;

	int idle;
	bool reset;
	bool overheat;
	bool optimal;
};

#define BITMAIN_READ_SIZE 12
#define BITMAIN_ARRAY_SIZE 512

#define BTM_GETS_ERROR -1
#define BTM_GETS_OK 0

#define BTM_SEND_ERROR -1
#define BTM_SEND_OK 0

#define bitmain_buffer_full(bitmain) !usb_ftdi_cts(bitmain)

#define BITMAIN_READ_TIME(baud) ((double)BITMAIN_READ_SIZE * (double)8.0 / (double)(baud))
#define ASSERT1(condition) __maybe_unused static char sizeof_uint32_t_must_be_4[(condition)?1:-1]
ASSERT1(sizeof(uint32_t) == 4);

extern struct bitmain_info **bitmain_info;
extern int opt_bitmain_temp;
extern int opt_bitmain_overheat;
extern int opt_bitmain_fan_min;
extern int opt_bitmain_fan_max;
extern int opt_bitmain_freq_min;
extern int opt_bitmain_freq_max;
extern bool opt_bitmain_auto;
extern char *set_bitmain_fan(char *arg);
extern char *set_bitmain_freq(char *arg);

#endif /* USE_BITMAIN */
#endif	/* BITMAIN_H */
