/*
 * Copyright 2013 BitMain <xlc1985@126.com>
 * Copyright 2012-2013 LingchaoXu <xlc1985@126.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 3 of the License, or (at your option)
 * any later version.  See COPYING for more details.
 */

#include "config.h"

#include <limits.h>
#include <pthread.h>
#include <stdio.h>
#include <sys/time.h>
#include <sys/types.h>
#include <dirent.h>
#include <unistd.h>
#ifndef WIN32
  #include <sys/select.h>
  #include <termios.h>
  #include <sys/stat.h>
  #include <fcntl.h>
  #ifndef O_CLOEXEC
    #define O_CLOEXEC 0
  #endif
#else
  #include "compat.h"
  #include <windows.h>
  #include <io.h>
#endif

#include "elist.h"
#include "miner.h"
#include "usbutils.h"
#include "driver-bitmain.h"
#include "hexdump.c"
#include "util.h"

char opt_bitmain_dev[256] = {0};
bool opt_bitmain_hwerror = false;
bool opt_bitmain_dev_usb = true;
int opt_bitmain_temp = BITMAIN_TEMP_TARGET;
int opt_bitmain_overheat = BITMAIN_TEMP_OVERHEAT;
int opt_bitmain_fan_min = BITMAIN_DEFAULT_FAN_MIN_PWM;
int opt_bitmain_fan_max = BITMAIN_DEFAULT_FAN_MAX_PWM;
int opt_bitmain_freq_min = BITMAIN_MIN_FREQUENCY;
int opt_bitmain_freq_max = BITMAIN_MAX_FREQUENCY;
bool opt_bitmain_auto;

static int option_offset = -1;
struct device_drv bitmain_drv;

// --------------------------------------------------------------
//      CRC16 check table
// --------------------------------------------------------------
const uint8_t chCRCHTalbe[] =                                 // CRC high byte table
{
 0x00, 0xC1, 0x81, 0x40, 0x01, 0xC0, 0x80, 0x41, 0x01, 0xC0, 0x80, 0x41,
 0x00, 0xC1, 0x81, 0x40, 0x01, 0xC0, 0x80, 0x41, 0x00, 0xC1, 0x81, 0x40,
 0x00, 0xC1, 0x81, 0x40, 0x01, 0xC0, 0x80, 0x41, 0x01, 0xC0, 0x80, 0x41,
 0x00, 0xC1, 0x81, 0x40, 0x00, 0xC1, 0x81, 0x40, 0x01, 0xC0, 0x80, 0x41,
 0x00, 0xC1, 0x81, 0x40, 0x01, 0xC0, 0x80, 0x41, 0x01, 0xC0, 0x80, 0x41,
 0x00, 0xC1, 0x81, 0x40, 0x01, 0xC0, 0x80, 0x41, 0x00, 0xC1, 0x81, 0x40,
 0x00, 0xC1, 0x81, 0x40, 0x01, 0xC0, 0x80, 0x41, 0x00, 0xC1, 0x81, 0x40,
 0x01, 0xC0, 0x80, 0x41, 0x01, 0xC0, 0x80, 0x41, 0x00, 0xC1, 0x81, 0x40,
 0x00, 0xC1, 0x81, 0x40, 0x01, 0xC0, 0x80, 0x41, 0x01, 0xC0, 0x80, 0x41,
 0x00, 0xC1, 0x81, 0x40, 0x01, 0xC0, 0x80, 0x41, 0x00, 0xC1, 0x81, 0x40,
 0x00, 0xC1, 0x81, 0x40, 0x01, 0xC0, 0x80, 0x41, 0x01, 0xC0, 0x80, 0x41,
 0x00, 0xC1, 0x81, 0x40, 0x00, 0xC1, 0x81, 0x40, 0x01, 0xC0, 0x80, 0x41,
 0x00, 0xC1, 0x81, 0x40, 0x01, 0xC0, 0x80, 0x41, 0x01, 0xC0, 0x80, 0x41,
 0x00, 0xC1, 0x81, 0x40, 0x00, 0xC1, 0x81, 0x40, 0x01, 0xC0, 0x80, 0x41,
 0x01, 0xC0, 0x80, 0x41, 0x00, 0xC1, 0x81, 0x40, 0x01, 0xC0, 0x80, 0x41,
 0x00, 0xC1, 0x81, 0x40, 0x00, 0xC1, 0x81, 0x40, 0x01, 0xC0, 0x80, 0x41,
 0x00, 0xC1, 0x81, 0x40, 0x01, 0xC0, 0x80, 0x41, 0x01, 0xC0, 0x80, 0x41,
 0x00, 0xC1, 0x81, 0x40, 0x01, 0xC0, 0x80, 0x41, 0x00, 0xC1, 0x81, 0x40,
 0x00, 0xC1, 0x81, 0x40, 0x01, 0xC0, 0x80, 0x41, 0x01, 0xC0, 0x80, 0x41,
 0x00, 0xC1, 0x81, 0x40, 0x00, 0xC1, 0x81, 0x40, 0x01, 0xC0, 0x80, 0x41,
 0x00, 0xC1, 0x81, 0x40, 0x01, 0xC0, 0x80, 0x41, 0x01, 0xC0, 0x80, 0x41,
 0x00, 0xC1, 0x81, 0x40
};

const uint8_t chCRCLTalbe[] =                                 // CRC low byte table
{
 0x00, 0xC0, 0xC1, 0x01, 0xC3, 0x03, 0x02, 0xC2, 0xC6, 0x06, 0x07, 0xC7,
 0x05, 0xC5, 0xC4, 0x04, 0xCC, 0x0C, 0x0D, 0xCD, 0x0F, 0xCF, 0xCE, 0x0E,
 0x0A, 0xCA, 0xCB, 0x0B, 0xC9, 0x09, 0x08, 0xC8, 0xD8, 0x18, 0x19, 0xD9,
 0x1B, 0xDB, 0xDA, 0x1A, 0x1E, 0xDE, 0xDF, 0x1F, 0xDD, 0x1D, 0x1C, 0xDC,
 0x14, 0xD4, 0xD5, 0x15, 0xD7, 0x17, 0x16, 0xD6, 0xD2, 0x12, 0x13, 0xD3,
 0x11, 0xD1, 0xD0, 0x10, 0xF0, 0x30, 0x31, 0xF1, 0x33, 0xF3, 0xF2, 0x32,
 0x36, 0xF6, 0xF7, 0x37, 0xF5, 0x35, 0x34, 0xF4, 0x3C, 0xFC, 0xFD, 0x3D,
 0xFF, 0x3F, 0x3E, 0xFE, 0xFA, 0x3A, 0x3B, 0xFB, 0x39, 0xF9, 0xF8, 0x38,
 0x28, 0xE8, 0xE9, 0x29, 0xEB, 0x2B, 0x2A, 0xEA, 0xEE, 0x2E, 0x2F, 0xEF,
 0x2D, 0xED, 0xEC, 0x2C, 0xE4, 0x24, 0x25, 0xE5, 0x27, 0xE7, 0xE6, 0x26,
 0x22, 0xE2, 0xE3, 0x23, 0xE1, 0x21, 0x20, 0xE0, 0xA0, 0x60, 0x61, 0xA1,
 0x63, 0xA3, 0xA2, 0x62, 0x66, 0xA6, 0xA7, 0x67, 0xA5, 0x65, 0x64, 0xA4,
 0x6C, 0xAC, 0xAD, 0x6D, 0xAF, 0x6F, 0x6E, 0xAE, 0xAA, 0x6A, 0x6B, 0xAB,
 0x69, 0xA9, 0xA8, 0x68, 0x78, 0xB8, 0xB9, 0x79, 0xBB, 0x7B, 0x7A, 0xBA,
 0xBE, 0x7E, 0x7F, 0xBF, 0x7D, 0xBD, 0xBC, 0x7C, 0xB4, 0x74, 0x75, 0xB5,
 0x77, 0xB7, 0xB6, 0x76, 0x72, 0xB2, 0xB3, 0x73, 0xB1, 0x71, 0x70, 0xB0,
 0x50, 0x90, 0x91, 0x51, 0x93, 0x53, 0x52, 0x92, 0x96, 0x56, 0x57, 0x97,
 0x55, 0x95, 0x94, 0x54, 0x9C, 0x5C, 0x5D, 0x9D, 0x5F, 0x9F, 0x9E, 0x5E,
 0x5A, 0x9A, 0x9B, 0x5B, 0x99, 0x59, 0x58, 0x98, 0x88, 0x48, 0x49, 0x89,
 0x4B, 0x8B, 0x8A, 0x4A, 0x4E, 0x8E, 0x8F, 0x4F, 0x8D, 0x4D, 0x4C, 0x8C,
 0x44, 0x84, 0x85, 0x45, 0x87, 0x47, 0x46, 0x86, 0x82, 0x42, 0x43, 0x83,
 0x41, 0x81, 0x80, 0x40
};

static uint16_t CRC16(const uint8_t* p_data, uint16_t w_len)
{
	uint8_t chCRCHi = 0xFF; // CRC high byte initialize
	uint8_t chCRCLo = 0xFF; // CRC low byte initialize
	uint16_t wIndex = 0;    // CRC cycling index

	while (w_len--) {
		wIndex = chCRCLo ^ *p_data++;
		chCRCLo = chCRCHi ^ chCRCHTalbe[wIndex];
		chCRCHi = chCRCLTalbe[wIndex];
	}
	return ((chCRCHi << 8) | chCRCLo);
}

static uint32_t num2bit(int num) {
	switch(num) {
	case 0:  return 0x80000000;
	case 1:  return 0x40000000;
	case 2:  return 0x20000000;
	case 3:  return 0x10000000;
	case 4:  return 0x08000000;
	case 5:  return 0x04000000;
	case 6:  return 0x02000000;
	case 7:  return 0x01000000;
	case 8:  return 0x00800000;
	case 9:  return 0x00400000;
	case 10: return 0x00200000;
	case 11: return 0x00100000;
	case 12: return 0x00080000;
	case 13: return 0x00040000;
	case 14: return 0x00020000;
	case 15: return 0x00010000;
	case 16: return 0x00008000;
	case 17: return 0x00004000;
	case 18: return 0x00002000;
	case 19: return 0x00001000;
	case 20: return 0x00000800;
	case 21: return 0x00000400;
	case 22: return 0x00000200;
	case 23: return 0x00000100;
	case 24: return 0x00000080;
	case 25: return 0x00000040;
	case 26: return 0x00000020;
	case 27: return 0x00000010;
	case 28: return 0x00000008;
	case 29: return 0x00000004;
	case 30: return 0x00000002;
	case 31: return 0x00000001;
	default: return 0x00000000;
	}
}

static bool get_options(int this_option_offset, int *baud, int *chain_num,
			int *asic_num, int *timeout, int *frequency, uint8_t * reg_data)
{
	char buf[BUFSIZ+1];
	char *ptr, *comma, *colon, *colon2, *colon3, *colon4, *colon5;
	size_t max;
	int i, tmp;

	if (opt_bitmain_options == NULL)
		buf[0] = '\0';
	else {
		ptr = opt_bitmain_options;
		for (i = 0; i < this_option_offset; i++) {
			comma = strchr(ptr, ',');
			if (comma == NULL)
				break;
			ptr = comma + 1;
		}

		comma = strchr(ptr, ',');
		if (comma == NULL)
			max = strlen(ptr);
		else
			max = comma - ptr;

		if (max > BUFSIZ)
			max = BUFSIZ;
		strncpy(buf, ptr, max);
		buf[max] = '\0';
	}

	if (!(*buf))
		return false;

	colon = strchr(buf, ':');
	if (colon)
		*(colon++) = '\0';

	tmp = atoi(buf);
	switch (tmp) {
	case 115200:
		*baud = 115200;
		break;
	case 57600:
		*baud = 57600;
		break;
	case 38400:
		*baud = 38400;
		break;
	case 19200:
		*baud = 19200;
		break;
	default:
		quit(1, "Invalid bitmain-options for baud (%s) "
			"must be 115200, 57600, 38400 or 19200", buf);
	}

	if (colon && *colon) {
		colon2 = strchr(colon, ':');
		if (colon2)
			*(colon2++) = '\0';

		if (*colon) {
			tmp = atoi(colon);
			if (tmp > 0) {
				*chain_num = tmp;
			} else {
				quit(1, "Invalid bitmain-options for "
					"chain_num (%s) must be 1 ~ %d",
					colon, BITMAIN_DEFAULT_CHAIN_NUM);
			}
		}

		if (colon2 && *colon2) {
			colon3 = strchr(colon2, ':');
			if (colon3)
				*(colon3++) = '\0';

			tmp = atoi(colon2);
			if (tmp > 0 && tmp <= BITMAIN_DEFAULT_ASIC_NUM)
				*asic_num = tmp;
			else {
				quit(1, "Invalid bitmain-options for "
					"asic_num (%s) must be 1 ~ %d",
					colon2, BITMAIN_DEFAULT_ASIC_NUM);
			}

			if (colon3 && *colon3) {
				colon4 = strchr(colon3, ':');
				if (colon4)
					*(colon4++) = '\0';

				tmp = atoi(colon3);
				if (tmp > 0 && tmp <= 0xff)
					*timeout = tmp;
				else {
					quit(1, "Invalid bitmain-options for "
						"timeout (%s) must be 1 ~ %d",
						colon3, 0xff);
				}
				if (colon4 && *colon4) {
					colon5 = strchr(colon4, ':');
					if(colon5)
						*(colon5++) = '\0';

					tmp = atoi(colon4);
					if (tmp < BITMAIN_MIN_FREQUENCY || tmp > BITMAIN_MAX_FREQUENCY) {
						quit(1, "Invalid bitmain-options for frequency, must be %d <= frequency <= %d",
						     BITMAIN_MIN_FREQUENCY, BITMAIN_MAX_FREQUENCY);
					} else {
						*frequency = tmp;
					}
					if (colon5 && *colon5) {
						if(strlen(colon5) > 8 || strlen(colon5)%2 != 0 || strlen(colon5)/2 == 0) {
							quit(1, "Invalid bitmain-options for reg data, must be hex now: %s",
									colon5);
						}
						memset(reg_data, 0, 4);
						if(!hex2bin(reg_data, colon5, strlen(colon5)/2)) {
							quit(1, "Invalid bitmain-options for reg data, hex2bin error now: %s",
									colon5);
						}
					}
				}
			}
		}
	}
	return true;
}

static int bitmain_set_txconfig(struct bitmain_txconfig_token *bm,
			    uint8_t reset, uint8_t fan_eft, uint8_t timeout_eft, uint8_t frequency_eft,
			    uint8_t voltage_eft, uint8_t chain_check_time_eft, uint8_t chip_config_eft, uint8_t hw_error_eft,
			    uint8_t chain_num, uint8_t asic_num, uint8_t fan_pwm_data, uint8_t timeout_data,
			    uint16_t frequency, uint8_t voltage, uint8_t chain_check_time,
			    uint8_t chip_address, uint8_t reg_address, uint8_t * reg_data)
{
	uint16_t crc = 0;
	int datalen = 0;
	uint8_t * sendbuf = (uint8_t *)bm;
	if (unlikely(!bm)) {
		applog(LOG_WARNING, "bitmain_set_txconfig bitmain_txconfig_token is null");
		return -1;
	}

	if (unlikely(timeout_data <= 0 || asic_num <= 0 || chain_num <= 0)) {
		applog(LOG_WARNING, "bitmain_set_txconfig parameter invalid timeout_data(%d) asic_num(%d) chain_num(%d)",
				timeout_data, asic_num, chain_num);
		return -1;
	}

	datalen = sizeof(struct bitmain_txconfig_token);
	memset(bm, 0, datalen);

	bm->token_type = BITMAIN_TOKEN_TYPE_TXCONFIG;
	bm->length = datalen-2;
	bm->reset = reset;
	bm->fan_eft = fan_eft;
	bm->timeout_eft = timeout_eft;
	bm->frequency_eft = frequency_eft;
	bm->voltage_eft = voltage_eft;
	bm->chain_check_time_eft = chain_check_time_eft;
	bm->chip_config_eft = chip_config_eft;
	bm->hw_error_eft = hw_error_eft;

	sendbuf[2] = htole8(sendbuf[2]);

	bm->chain_num = chain_num;
	bm->asic_num = asic_num;
	bm->fan_pwm_data = fan_pwm_data;
	bm->timeout_data = timeout_data;

	bm->frequency = htole16(frequency);
	bm->voltage = voltage;
	bm->chain_check_time = chain_check_time;

	memcpy(bm->reg_data, reg_data, 4);
	bm->chip_address = chip_address;
	bm->reg_address = reg_address;

	crc = CRC16((uint8_t *)bm, datalen-2);
	bm->crc = htole16(crc);

	applog(LOG_ERR, "BTM TxConfigToken:reset(%d) faneft(%d) touteft(%d) freqeft(%d) volteft(%d) chainceft(%d) chipceft(%d) hweft(%d) mnum(%d) anum(%d) fanpwmdata(%d) toutdata(%d) freq(%d) volt(%d) chainctime(%d) regdata(%02x%02x%02x%02x) chipaddr(%02x) regaddr(%02x) crc(%04x)",
					reset, fan_eft, timeout_eft, frequency_eft, voltage_eft,
					chain_check_time_eft, chip_config_eft, hw_error_eft, chain_num, asic_num,
					fan_pwm_data, timeout_data, frequency, voltage,
					chain_check_time, reg_data[0], reg_data[1], reg_data[2], reg_data[3], chip_address, reg_address, crc);

	return datalen;
}

#ifdef WIN32
#define BITMAIN_TEST
#endif

#ifdef BITMAIN_TEST
const char * btm_work_test_data = "00000002ddc1ce5579dbec17f17fbb8f31ae218a814b2a0c1900f0d90000000100000000b58aa6ca86546b07a5a46698f736c7ca9c0eedc756d8f28ac33c20cc24d792675276f879190afc85b6888022000000800000000000000000000000000000000000000000000000000000000000000000";
const char * btm_work_test_midstate = "2d8738e7f5bcf76dcb8316fec772e20e240cd58c88d47f2d3f5a6a9547ed0a35";
#endif

static int bitmain_set_txtask(uint8_t * sendbuf,
			    unsigned int * last_work_block, struct work **works, int work_array_size, int work_array, int sendworkcount, int * sendcount)
{
	uint16_t crc = 0;
	uint32_t work_id = 0;
	int datalen = 0;
	int i = 0;
	int index = work_array;
	uint8_t new_block= 0;
	char * ob_hex = NULL;
	struct bitmain_txtask_token *bm = (struct bitmain_txtask_token *)sendbuf;
	*sendcount = 0;
	int cursendcount = 0;
	if (unlikely(!bm)) {
		applog(LOG_WARNING, "bitmain_set_txtask bitmain_txtask_token is null");
		return -1;
	}
	if (unlikely(!works)) {
		applog(LOG_WARNING, "bitmain_set_txtask work is null");
		return -1;
	}
	memset(bm, 0, sizeof(struct bitmain_txtask_token));

	bm->token_type = BITMAIN_TOKEN_TYPE_TXTASK;

	datalen = 10;
	applog(LOG_DEBUG, "BTM send work count %d -----", sendworkcount);
	for(i = 0; i < sendworkcount; i++) {
		if(index > work_array_size) {
			index = 0;
		}
		if(works[index]) {
			if(works[index]->work_block > *last_work_block) {
				applog(LOG_ERR, "BTM send task new block %d old(%d)", works[index]->work_block, *last_work_block);
				new_block = 1;
				*last_work_block = works[index]->work_block;
			}
#ifdef BITMAIN_TEST
			if(!hex2bin(works[index]->data, btm_work_test_data, 128)) {
				applog(LOG_DEBUG, "BTM send task set test data error");
			}
			if(!hex2bin(works[index]->midstate, btm_work_test_midstate, 32)) {
				applog(LOG_DEBUG, "BTM send task set test midstate error");
			}
#endif
			work_id = works[index]->id;
			bm->works[cursendcount].work_id = htole32(work_id);
			applog(LOG_DEBUG, "BTM send task work id:%d %d", bm->works[cursendcount].work_id, work_id);
			memcpy(bm->works[cursendcount].midstate, works[index]->midstate, 32);
			memcpy(bm->works[cursendcount].data2, works[index]->data + 64, 12);

			/*ob_hex = bin2hex(works[index]->data, 76);
			applog(LOG_ERR, "work %d data: %s", works[index]->id, ob_hex);
			free(ob_hex);*/

			cursendcount++;
		}
		index++;
	}
	if(cursendcount <= 0) {
		applog(LOG_ERR, "BTM send work count %d", cursendcount);
		return 0;
	}
	datalen += 48*cursendcount;

	bm->length = datalen-4;
	bm->length = htole16(bm->length);
	//len = datalen-3;
	//len = htole16(len);
	//memcpy(sendbuf+1, &len, 2);
	bm->new_block = new_block;

	sendbuf[4] = htole8(sendbuf[4]);

	applog(LOG_DEBUG, "BitMain TxTask Token: %d %d %02x%02x%02x%02x%02x%02x",
				datalen, bm->length, sendbuf[0],sendbuf[1],sendbuf[2],sendbuf[3],sendbuf[4],sendbuf[5]);

	*sendcount = cursendcount;

	crc = CRC16(sendbuf, datalen-2);
	crc = htole16(crc);
	memcpy(sendbuf+datalen-2, &crc, 2);

	applog(LOG_DEBUG, "BitMain TxTask Token: new_block(%d) work_num(%d) crc(%04x)",
						new_block, cursendcount, crc);
	applog(LOG_DEBUG, "BitMain TxTask Token: %d %d %02x%02x%02x%02x%02x%02x",
			datalen, bm->length, sendbuf[0],sendbuf[1],sendbuf[2],sendbuf[3],sendbuf[4],sendbuf[5]);

	return datalen;
}

static int bitmain_set_rxstatus(struct bitmain_rxstatus_token *bm,
		uint8_t chip_status_eft, uint8_t detect_get, uint8_t chip_address, uint8_t reg_address)
{
	uint16_t crc = 0;
	int datalen = 0;
	uint8_t * sendbuf = (uint8_t *)bm;

	if (unlikely(!bm)) {
		applog(LOG_WARNING, "bitmain_set_rxstatus bitmain_rxstatus_token is null");
		return -1;
	}

	datalen = sizeof(struct bitmain_rxstatus_token);
	memset(bm, 0, datalen);

	bm->token_type = BITMAIN_TOKEN_TYPE_RXSTATUS;
	bm->length = datalen-2;

	bm->chip_status_eft = chip_status_eft;
	bm->detect_get = detect_get;

	sendbuf[2] = htole8(sendbuf[2]);

	bm->chip_address = chip_address;
	bm->reg_address = reg_address;

	crc = CRC16((uint8_t *)bm, datalen-2);
	bm->crc = htole16(crc);

	applog(LOG_DEBUG, "BitMain RxStatus Token: chip_status_eft(%d) detect_get(%d) chip_address(%02x) reg_address(%02x) crc(%04x)",
				chip_status_eft, detect_get, chip_address, reg_address, crc);

	return datalen;
}

static int bitmain_parse_rxstatus(const uint8_t * data, int datalen, struct bitmain_rxstatus_data *bm)
{
	uint16_t crc = 0;
	int i = 0;
	if (unlikely(!bm)) {
		applog(LOG_WARNING, "bitmain_parse_rxstatus bitmain_rxstatus_data is null");
		return -1;
	}
	if (unlikely(!data || datalen <= 0)) {
		applog(LOG_WARNING, "bitmain_parse_rxstatus parameter invalid data is null or datalen(%d) error", datalen);
		return -1;
	}
	memcpy(bm, data, sizeof(struct bitmain_rxstatus_data));
	if (bm->data_type != BITMAIN_DATA_TYPE_RXSTATUS) {
		applog(LOG_ERR, "bitmain_parse_rxstatus datatype(%02x) error", bm->data_type);
		return -1;
	}
	if (bm->length+2 != datalen) {
		applog(LOG_ERR, "bitmain_parse_rxstatus length(%d) error", bm->length);
		return -1;
	}
	crc = CRC16(data, datalen-2);
	memcpy(&(bm->crc), data+datalen-2, 2);
	bm->crc = htole16(bm->crc);
	if(crc != bm->crc) {
		applog(LOG_ERR, "bitmain_parse_rxstatus check crc(%d) != bm crc(%d) datalen(%d)", crc, bm->crc, datalen);
		return -1;
	}
	bm->fifo_space = htole32(bm->fifo_space);
	bm->nonce_error = htole32(bm->nonce_error);
	if(bm->chain_num*5 + bm->temp_num + bm->fan_num + 22 != datalen) {
		applog(LOG_ERR, "bitmain_parse_rxstatus chain_num(%d) temp_num(%d) fan_num(%d) not match datalen(%d)",
				bm->chain_num, bm->temp_num, bm->fan_num, datalen);
		return -1;
	}
	if(bm->chain_num > BITMAIN_MAX_CHAIN_NUM) {
		applog(LOG_ERR, "bitmain_parse_rxstatus chain_num=%d error", bm->chain_num);
		return -1;
	}
	if(bm->chain_num > 0) {
		memcpy(bm->chain_asic_status, data+20, bm->chain_num*4);
		memcpy(bm->chain_asic_num, data+20+bm->chain_num*4, bm->chain_num);
	}
	for(i = 0; i < bm->chain_num; i++) {
		//bm->chain_asic_status[i] = swab32(bm->chain_asic_status[i]);
		bm->chain_asic_status[i] = htole32(bm->chain_asic_status[i]);
	}
	if(bm->temp_num > 0) {
		memcpy(bm->temp, data+20+bm->chain_num*5, bm->temp_num);
	}
	if(bm->fan_num > 0) {
		memcpy(bm->fan, data+20+bm->chain_num*5+bm->temp_num, bm->fan_num);
	}
	applog(LOG_DEBUG, "BitMain RxStatusData: chipvalueeft(%d) version(%d) fifospace(%d) regvalue(%d) chainnum(%d) tempnum(%d) fannum(%d) crc(%04x)",
			bm->chip_value_eft, bm->version, bm->fifo_space, bm->reg_value, bm->chain_num, bm->temp_num, bm->fan_num, bm->crc);
	applog(LOG_DEBUG, "BitMain RxStatus Data chain info:");
	for(i = 0; i < bm->chain_num; i++) {
		applog(LOG_DEBUG, "BitMain RxStatus Data chain(%d) asic num=%d asic_status=%08x", i+1, bm->chain_asic_num[i], bm->chain_asic_status[i]);
	}
	applog(LOG_DEBUG, "BitMain RxStatus Data temp info:");
	for(i = 0; i < bm->temp_num; i++) {
		applog(LOG_DEBUG, "BitMain RxStatus Data temp(%d) temp=%d", i+1, bm->temp[i]);
	}
	applog(LOG_DEBUG, "BitMain RxStatus Data fan info:");
	for(i = 0; i < bm->fan_num; i++) {
		applog(LOG_DEBUG, "BitMain RxStatus Data fan(%d) fan=%d", i+1, bm->fan[i]);
	}
	return 0;
}

static int bitmain_parse_rxnonce(const uint8_t * data, int datalen, struct bitmain_rxnonce_data *bm, int * nonce_num)
{
	int i = 0;
	uint16_t crc = 0;
	int curnoncenum = 0;
	if (unlikely(!bm)) {
		applog(LOG_ERR, "bitmain_parse_rxnonce bitmain_rxstatus_data null");
		return -1;
	}
	if (unlikely(!data || datalen <= 0)) {
		applog(LOG_ERR, "bitmain_parse_rxnonce data null or datalen(%d) error", datalen);
		return -1;
	}
	memcpy(bm, data, sizeof(struct bitmain_rxnonce_data));

	if (bm->data_type != BITMAIN_DATA_TYPE_RXNONCE) {
		applog(LOG_ERR, "bitmain_parse_rxnonce datatype(%02x) error", bm->data_type);
		return -1;
	}
	if (bm->length+2 != datalen) {
		applog(LOG_ERR, "bitmain_parse_rxnonce length(%d) error", bm->length);
		return -1;
	}
	crc = CRC16(data, datalen-2);
	memcpy(&(bm->crc), data+datalen-2, 2);
	bm->crc = htole16(bm->crc);
	if(crc != bm->crc) {
		applog(LOG_ERR, "bitmain_parse_rxnonce check crc(%d) != bm crc(%d) datalen(%d)", crc, bm->crc, datalen);
		return -1;
	}
	curnoncenum = (datalen-4)/8;
	applog(LOG_DEBUG, "BitMain RxNonce Data: nonce_num(%d-%d) fifo_space(%d)", curnoncenum, bm->nonce_num, bm->fifo_space);
	for(i = 0; i < curnoncenum; i++) {
		bm->nonces[i].work_id = htole32(bm->nonces[i].work_id);
		bm->nonces[i].nonce = htole32(bm->nonces[i].nonce);

		applog(LOG_DEBUG, "BitMain RxNonce Data %d: work_id(%d) nonce(%08x)(%d)",
				i, bm->nonces[i].work_id, bm->nonces[i].nonce, bm->nonces[i].nonce);
	}
	*nonce_num = curnoncenum;
	return 0;
}

static int bitmain_read(struct cgpu_info *bitmain, unsigned char *buf,
		       size_t bufsize, int timeout, int ep)
{
	int err = 0, readlen = 0;
	size_t total = 0;

	if(bitmain == NULL || buf == NULL || bufsize <= 0) {
		applog(LOG_WARNING, "bitmain_read parameter error bufsize(%d)", bufsize);
		return -1;
	}
	if(opt_bitmain_dev_usb) {
#ifdef WIN32
		char readbuf[BITMAIN_READBUF_SIZE];
		int ofs = 2, cp = 0;

		err = usb_read_once_timeout(bitmain, readbuf, bufsize, &readlen, timeout, ep);
		applog(LOG_DEBUG, "%s%i: Get bitmain read got readlen %d err %d",
			bitmain->drv->name, bitmain->device_id, readlen, err);

		if (readlen < 2)
			goto out;

		while (readlen > 2) {
			cp = readlen - 2;
			if (cp > 62)
				cp = 62;
			memcpy(&buf[total], &readbuf[ofs], cp);
			total += cp;
			readlen -= cp + 2;
			ofs += 64;
		}
#else
		err = usb_read_once_timeout(bitmain, buf, bufsize, &readlen, timeout, ep);
		applog(LOG_DEBUG, "%s%i: Get bitmain read got readlen %d err %d",
			bitmain->drv->name, bitmain->device_id, readlen, err);
		total = readlen;
#endif
	} else {
		err = btm_read(bitmain, buf, bufsize);
		total = err;
	}
out:
	return total;
}

static int bitmain_write(struct cgpu_info *bitmain, char *buf, ssize_t len, int ep)
{
	int err, amount;
	if(opt_bitmain_dev_usb) {
		err = usb_write(bitmain, buf, len, &amount, ep);
		applog(LOG_DEBUG, "%s%i: usb_write got err %d", bitmain->drv->name,
				bitmain->device_id, err);

		if (unlikely(err != 0)) {
			applog(LOG_ERR, "usb_write error on bitmain_write err=%d", err);
			return BTM_SEND_ERROR;
		}
		if (amount != len) {
			applog(LOG_ERR, "usb_write length mismatch on bitmain_write amount=%d len=%d", amount, len);
			return BTM_SEND_ERROR;
		}
	} else {
		int havelen = 0;
		while(havelen < len) {
			err = btm_write(bitmain, buf+havelen, len-havelen);
			if(err < 0) {
				applog(LOG_DEBUG, "%s%i: btm_write got err %d", bitmain->drv->name,
						bitmain->device_id, err);
				applog(LOG_WARNING, "usb_write error on bitmain_write");
				return BTM_SEND_ERROR;
			} else {
				havelen += err;
			}
		}
	}
	return BTM_SEND_OK;
}

static int bitmain_send_data(const uint8_t * data, int datalen, struct cgpu_info *bitmain)
{
	int delay, ret, ep = C_BITMAIN_SEND;
	struct bitmain_info *info = NULL;
	cgtimer_t ts_start;

	if(datalen <= 0) {
		return 0;
	}

	if(data[0] == BITMAIN_TOKEN_TYPE_TXCONFIG) {
		ep = C_BITMAIN_TOKEN_TXCONFIG;
	} else if(data[0] == BITMAIN_TOKEN_TYPE_TXTASK) {
		ep = C_BITMAIN_TOKEN_TXTASK;
	} else if(data[0] == BITMAIN_TOKEN_TYPE_RXSTATUS) {
		ep = C_BITMAIN_TOKEN_RXSTATUS;
	}

	info = bitmain->device_data;
	//delay = datalen * 10 * 1000000;
	//delay = delay / info->baud;
	//delay += 4000;

	if(opt_debug) {
		applog(LOG_DEBUG, "BitMain: Sent(%d):", datalen);
		hexdump(data, datalen);
	}

	//cgsleep_prepare_r(&ts_start);
	applog(LOG_DEBUG, "----bitmain_send_data  start");
	ret = bitmain_write(bitmain, (char *)data, datalen, ep);
	applog(LOG_DEBUG, "----bitmain_send_data  stop ret=%d datalen=%d", ret, datalen);
	//cgsleep_us_r(&ts_start, delay);

	//applog(LOG_DEBUG, "BitMain: Sent: Buffer delay: %dus", delay);

	return ret;
}

static bool bitmain_decode_nonce(struct thr_info *thr, struct cgpu_info *bitmain,
				struct bitmain_info *info, uint32_t nonce, struct work *work)
{
	info = bitmain->device_data;
	//info->matching_work[work->subid]++;
	applog(LOG_DEBUG, "BitMain: nonce = %08x", nonce);
	return submit_nonce(thr, work, nonce);
}

static void bitmain_inc_nvw(struct bitmain_info *info, struct thr_info *thr)
{
	applog(LOG_INFO, "%s%d: No matching work - HW error",
	       thr->cgpu->drv->name, thr->cgpu->device_id);

	inc_hw_errors(thr);
	info->no_matching_work++;
}

static inline void record_temp_fan(struct bitmain_info *info, struct bitmain_rxstatus_data *bm, float *temp_avg)
{
	int i = 0;
	*temp_avg = 0;

	info->fan_num = bm->fan_num;
	for(i = 0; i < bm->fan_num; i++) {
		info->fan[i] = bm->fan[i] * BITMAIN_FAN_FACTOR;
	}
	info->temp_num = bm->temp_num;
	for(i = 0; i < bm->temp_num; i++) {
		info->temp[i] = bm->temp[i];
		/*
		if(bm->temp[i] & 0x80) {
			bm->temp[i] &= 0x7f;
			info->temp[i] = 0 - ((~bm->temp[i] & 0x7f) + 1);
		}*/
		*temp_avg += info->temp[i];

		if(info->temp[i] > info->temp_max) {
			info->temp_max = info->temp[i];
		}
	}

	if(bm->temp_num > 0) {
		*temp_avg = *temp_avg / bm->temp_num;
		info->temp_avg = *temp_avg;
	}
}

static void bitmain_update_temps(struct cgpu_info *bitmain, struct bitmain_info *info,
				struct bitmain_rxstatus_data *bm)
{
	char tmp[64] = {0};
	char msg[10240] = {0};
	int i = 0;
	record_temp_fan(info, bm, &(bitmain->temp));

	strcpy(msg, "BitMain: ");
	for(i = 0; i < bm->fan_num; i++) {
		if(i != 0) {
			strcat(msg, ", ");
		}
		sprintf(tmp, "Fan%d: %d/m", i+1, info->fan[i]);
		strcat(msg, tmp);
	}
	strcat(msg, "\t");
	for(i = 0; i < bm->temp_num; i++) {
		if(i != 0) {
			strcat(msg, ", ");
		}
		sprintf(tmp, "Temp%d: %dC", i+1, info->temp[i]);
		strcat(msg, tmp);
	}
	sprintf(tmp, ", TempMAX: %dC", info->temp_max);
	strcat(msg, tmp);
	applog(LOG_INFO, msg);
	info->temp_history_index++;
	info->temp_sum += bitmain->temp;
	applog(LOG_DEBUG, "BitMain: temp_index: %d, temp_count: %d, temp_old: %d",
		info->temp_history_index, info->temp_history_count, info->temp_old);
	if (info->temp_history_index == info->temp_history_count) {
		info->temp_history_index = 0;
		info->temp_sum = 0;
	}
	if (unlikely(info->temp_old >= opt_bitmain_overheat)) {
		applog(LOG_WARNING, "BTM%d overheat! Idling", bitmain->device_id);
		info->overheat = true;
	} else if (info->overheat && info->temp_old <= opt_bitmain_temp) {
		applog(LOG_WARNING, "BTM%d cooled, restarting", bitmain->device_id);
		info->overheat = false;
	}
}

static void bitmain_parse_results(struct cgpu_info *bitmain, struct bitmain_info *info,
				 struct thr_info *thr, uint8_t *buf, int *offset)
{
	int i, j, n, m, errordiff, spare = BITMAIN_READ_SIZE;
	uint32_t checkbit = 0x00000000;
	bool found = false;
	struct work *work = NULL;
	char * ob_hex = NULL;

	for (i = 0; i <= spare; i++) {
		if(buf[i] == 0xa1) {
			struct bitmain_rxstatus_data rxstatusdata;
			applog(LOG_DEBUG, "bitmain_parse_results RxStatus Data");
			if(*offset < 2) {
				return;
			}
			if(buf[i+1] > 124) {
				applog(LOG_ERR, "bitmain_parse_results bitmain_parse_rxstatus datalen=%d error", buf[i+1]+2);
				continue;
			}
			if(*offset < buf[i+1] + 2) {
				return;
			}
			if(bitmain_parse_rxstatus(buf+i, buf[i+1]+2, &rxstatusdata) != 0) {
				applog(LOG_ERR, "bitmain_parse_results bitmain_parse_rxstatus error len=%d", buf[i+1]+2);
			} else {
				mutex_lock(&info->qlock);
				info->chain_num = rxstatusdata.chain_num;
				info->fifo_space = rxstatusdata.fifo_space;
				info->nonce_error = rxstatusdata.nonce_error;
				errordiff = info->nonce_error-info->last_nonce_error;
				applog(LOG_ERR, "bitmain_parse_results bitmain_parse_rxstatus version=%d chainnum=%d fifospace=%d nonceerror=%d-%d freq=%d chain info:",
						rxstatusdata.version, info->chain_num, info->fifo_space, info->last_nonce_error, info->nonce_error, info->frequency);
				for(n = 0; n < rxstatusdata.chain_num; n++) {
					info->chain_asic_num[n] = rxstatusdata.chain_asic_num[n];
					info->chain_asic_status[n] = rxstatusdata.chain_asic_status[n];
					memset(info->chain_asic_status_t[n], 0, 40);
					j = 0;
					for(m = 0; m < 32; m++) {
						if(m%8 == 0 && m != 0) {
							info->chain_asic_status_t[n][j] = ' ';
							j++;
						}
						checkbit = num2bit(m);
						if(rxstatusdata.chain_asic_status[n] & checkbit) {
							info->chain_asic_status_t[n][j] = 'o';
						} else {
							info->chain_asic_status_t[n][j] = 'x';
						}
						j++;
					}
					applog(LOG_ERR, "bitmain_parse_rxstatus chain(%d) asic_num=%d asic_status=%08x-%s", n, info->chain_asic_num[n], info->chain_asic_status[n], info->chain_asic_status_t[n]);
				}
				mutex_unlock(&info->qlock);

				if(errordiff > 0) {
					for(j = 0; j < errordiff; j++) {
						bitmain_inc_nvw(info, thr);
					}
					mutex_lock(&info->qlock);
					info->last_nonce_error += errordiff;
					mutex_unlock(&info->qlock);
				}
				bitmain_update_temps(bitmain, info, &rxstatusdata);
			}

			found = true;
			spare = buf[i+1] + 2 + i;
			if(spare > *offset) {
				applog(LOG_ERR, "bitmain_parse_rxresults space(%d) > offset(%d)", spare, *offset);
				spare = *offset;
			}
			break;
		} else if(buf[i] == 0xa2) {
			struct bitmain_rxnonce_data rxnoncedata;
			int nonce_num = 0;
			applog(LOG_DEBUG, "bitmain_parse_results RxNonce Data");
			if(*offset < 2) {
				return;
			}
			if(buf[i+1] > 70) {
				applog(LOG_ERR, "bitmain_parse_results bitmain_parse_rxnonce datalen=%d error", buf[i+1]+2);
				continue;
			}
			if(*offset < buf[i+1] + 2) {
				return;
			}
			if(bitmain_parse_rxnonce(buf+i, buf[i+1]+2, &rxnoncedata, &nonce_num) != 0) {
				applog(LOG_ERR, "bitmain_parse_results bitmain_parse_rxnonce error len=%d", buf[i+1]+2);
			} else {
				for(j = 0; j < nonce_num; j++) {
					work = clone_queued_work_byid(bitmain, rxnoncedata.nonces[j].work_id);
					if(work) {
						applog(LOG_DEBUG, "bitmain_parse_results nonce find work(%d-%d)(%08x)", work->id, rxnoncedata.nonces[j].work_id, rxnoncedata.nonces[j].nonce);

						/*ob_hex = bin2hex(work->midstate, 32);
						applog(LOG_ERR, "work %d midstate: %s", work->id, ob_hex);
						free(ob_hex);

						ob_hex = bin2hex(work->data+64, 12);
						applog(LOG_ERR, "work %d data2: %s", work->id, ob_hex);
						free(ob_hex);*/

						if (bitmain_decode_nonce(thr, bitmain, info, rxnoncedata.nonces[j].nonce, work)) {
					 		applog(LOG_DEBUG, "bitmain_decode_nonce info->qlock start");
					 		mutex_lock(&info->qlock);
					 		info->nonces++;
							info->auto_nonces++;
							mutex_unlock(&info->qlock);
							applog(LOG_DEBUG, "bitmain_decode_nonce info->qlock stop");
					 	} else {
					 		//bitmain_inc_nvw(info, thr);
					 		applog(LOG_ERR, "BitMain: bitmain_decode_nonce error work(%d)", rxnoncedata.nonces[j].work_id);
					 	}
					 	free_work(work);
					} else {
						//bitmain_inc_nvw(info, thr);
						applog(LOG_ERR, "BitMain: Nonce not find work(%d)", rxnoncedata.nonces[j].work_id);
					}
				}
				mutex_lock(&info->qlock);
				info->fifo_space = rxnoncedata.fifo_space;
				applog(LOG_DEBUG, "bitmain_parse_rxnonce fifo space=%d", info->fifo_space);
				mutex_unlock(&info->qlock);
			}

 			found = true;
 			spare = buf[i+1] + 2 + i;
 			if(spare > *offset) {
 				applog(LOG_ERR, "bitmain_parse_rxnonce space(%d) > offset(%d)", spare, *offset);
 				spare = *offset;
 			}
 			break;
		} else {
			applog(LOG_ERR, "bitmain_parse_results data type error=%02x", buf[i]);
		}
	}
	if (!found) {
		spare = *offset - BITMAIN_READ_SIZE;
		/* We are buffering and haven't accumulated one more corrupt
		 * work result. */
		if (spare < (int)BITMAIN_READ_SIZE)
			return;
		bitmain_inc_nvw(info, thr);
	}

	*offset -= spare;
	memmove(buf, buf + spare, *offset);
}

static void bitmain_running_reset(struct cgpu_info *bitmain,
				   struct bitmain_info *info)
{
	bitmain->results = 0;
	info->reset = false;
}

static void *bitmain_get_results(void *userdata)
{
	struct cgpu_info *bitmain = (struct cgpu_info *)userdata;
	struct bitmain_info *info = bitmain->device_data;
	int offset = 0, read_delay = 0, ret = 0;
	const int rsize = BITMAIN_FTDI_READSIZE;
	char readbuf[BITMAIN_READBUF_SIZE];
	struct thr_info *thr = info->thr;
	cgtimer_t ts_start;
	char threadname[24];
	int errorcount = 0;

	snprintf(threadname, 24, "btm_recv/%d", bitmain->device_id);
	RenameThread(threadname);
	cgsleep_prepare_r(&ts_start);

	while (likely(!bitmain->shutdown)) {
		unsigned char buf[rsize];

		applog(LOG_DEBUG, "+++++++bitmain_get_results offset=%d", offset);

		if (offset >= (int)BITMAIN_READ_SIZE) {
			applog(LOG_DEBUG, "======start bitmain_get_results ");
			bitmain_parse_results(bitmain, info, thr, readbuf, &offset);
			applog(LOG_DEBUG, "======stop bitmain_get_results ");
		}

		if (unlikely(offset + rsize >= BITMAIN_READBUF_SIZE)) {
			/* This should never happen */
			applog(LOG_DEBUG, "BitMain readbuf overflow, resetting buffer");
			offset = 0;
		}

		if (unlikely(info->reset)) {
			bitmain_running_reset(bitmain, info);
			/* Discard anything in the buffer */
			offset = 0;
		}

		/* As the usb read returns after just 1ms, sleep long enough
		 * to leave the interface idle for writes to occur, but do not
		 * sleep if we have been receiving data as more may be coming. */
		//if (offset == 0) {
		//	cgsleep_ms_r(&ts_start, BITMAIN_READ_TIMEOUT);
		//}

		//cgsleep_prepare_r(&ts_start);
		applog(LOG_DEBUG, "======start bitmain_get_results bitmain_read");
		ret = bitmain_read(bitmain, buf, rsize, BITMAIN_READ_TIMEOUT, C_BITMAIN_READ);
		applog(LOG_DEBUG, "======stop bitmain_get_results bitmain_read=%d", ret);

		if (ret < 1) {
			errorcount++;
			if(errorcount > 100) {
				applog(LOG_ERR, "bitmain_read errorcount ret=%d", ret);
				cgsleep_prepare_r(&ts_start);
				cgsleep_ms_r(&ts_start, 20);
				errorcount = 0;
			}
			//cgsleep_prepare_r(&ts_start);
			//cgsleep_ms_r(&ts_start, 200);
			continue;
		}

		if (opt_debug) {
			applog(LOG_DEBUG, "BitMain: get:");
			hexdump((uint8_t *)buf, ret);
		}

		memcpy(readbuf+offset, buf, ret);
		offset += ret;
	}
	return NULL;
}

static void bitmain_set_timeout(struct bitmain_info *info)
{
	info->timeout = BITMAIN_TIMEOUT_FACTOR / info->frequency;
}

static void *bitmain_send_tasks(void *userdata)
{
	return NULL;
}

static void bitmain_init(struct cgpu_info *bitmain)
{
	applog(LOG_INFO, "BitMain: Opened on %s", bitmain->device_path);
}

static bool bitmain_prepare(struct thr_info *thr)
{
	struct cgpu_info *bitmain = thr->cgpu;
	struct bitmain_info *info = bitmain->device_data;

	free(bitmain->works);
	bitmain->works = calloc(BITMAIN_MAX_WORK_NUM * sizeof(struct work *),
			       BITMAIN_ARRAY_SIZE);
	if (!bitmain->works)
		quit(1, "Failed to calloc bitmain works in bitmain_prepare");

	info->thr = thr;
	mutex_init(&info->lock);
	mutex_init(&info->qlock);
	if (unlikely(pthread_cond_init(&info->qcond, NULL)))
		quit(1, "Failed to pthread_cond_init bitmain qcond");
	cgsem_init(&info->write_sem);

	if (pthread_create(&info->read_thr, NULL, bitmain_get_results, (void *)bitmain))
		quit(1, "Failed to create bitmain read_thr");

	//if (pthread_create(&info->write_thr, NULL, bitmain_send_tasks, (void *)bitmain))
	//	quit(1, "Failed to create bitmain write_thr");

	bitmain_init(bitmain);

	return true;
}

static int bitmain_initialize(struct cgpu_info *bitmain)
{
	uint8_t data[BITMAIN_READBUF_SIZE];
	struct bitmain_info *info = NULL;
	int ret = 0, spare = 0;
	uint8_t sendbuf[BITMAIN_SENDBUF_SIZE];
	int readlen = 0;
	int sendlen = 0;
	int trycount = 3;
	struct timespec p;
	struct bitmain_rxstatus_data rxstatusdata;
	int i = 0, j = 0, m = 0, statusok = 0;
	uint32_t checkbit = 0x00000000;
	int eft = 0;

	/* Send reset, then check for result */
	if(!bitmain) {
		applog(LOG_WARNING, "bitmain_initialize cgpu_info is null");
		return -1;
	}
	info = bitmain->device_data;

	/* clear read buf */
	ret = bitmain_read(bitmain, data, BITMAIN_READBUF_SIZE,
				  BITMAIN_RESET_TIMEOUT, C_BITMAIN_READ);
	if(ret > 0) {
		if (opt_debug) {
			applog(LOG_DEBUG, "BTM%d Clear Read(%d):", bitmain->device_id, ret);
			hexdump(data, ret);
		}
	}

	sendlen = bitmain_set_rxstatus((struct bitmain_rxstatus_token *)sendbuf, 0, 1, 0, 0);
	if(sendlen <= 0) {
		applog(LOG_ERR, "bitmain_initialize bitmain_set_rxstatus error(%d)", sendlen);
		return -1;
	}

	ret = bitmain_send_data(sendbuf, sendlen, bitmain);
	if (unlikely(ret == BTM_SEND_ERROR)) {
		applog(LOG_ERR, "bitmain_initialize bitmain_send_data error");
		return -1;
	}
	while(trycount >= 0) {
		ret = bitmain_read(bitmain, data+readlen, BITMAIN_READBUF_SIZE, BITMAIN_RESET_TIMEOUT, C_BITMAIN_DATA_RXSTATUS);
		if(ret > 0) {
			readlen += ret;
			if(readlen > BITMAIN_READ_SIZE) {
				for(i = 0; i < readlen; i++) {
					if(data[i] == 0xa1) {
						if (opt_debug) {
							applog(LOG_DEBUG, "%s%d initset: get:", bitmain->drv->name, bitmain->device_id);
							hexdump(data, readlen);
						}
						if(data[i+1] > 124) {
							applog(LOG_ERR, "bitmain_initialize rxstatus datalen=%d error", data[i+1]+2);
							continue;
						}
						if(readlen-i < data[i+1]+2) {
							applog(LOG_ERR, "bitmain_initialize rxstatus datalen=%d low", data[i+1]+2);
							continue;
						}
						if (bitmain_parse_rxstatus(data+i, data[i+1]+2, &rxstatusdata) != 0) {
							applog(LOG_ERR, "bitmain_initialize bitmain_parse_rxstatus error");
							continue;
						}
						info->chain_num = rxstatusdata.chain_num;
						info->fifo_space = rxstatusdata.fifo_space;
						info->nonce_error = 0;
						info->last_nonce_error = 0;
						applog(LOG_ERR, "bitmain_initialize bitmain_parse_rxstatus version(%d) chain_num(%d) fifo_space(%d) nonce_error(%d) freq=%d",
								rxstatusdata.version, info->chain_num, info->fifo_space, rxstatusdata.nonce_error, info->frequency);
						for(i = 0; i < rxstatusdata.chain_num; i++) {
							info->chain_asic_num[i] = rxstatusdata.chain_asic_num[i];
							info->chain_asic_status[i] = rxstatusdata.chain_asic_status[i];
							memset(info->chain_asic_status_t[i], 0, 40);
							j = 0;
							for(m = 0; m < 32; m++) {
								if(m%8 == 0 && m != 0) {
									info->chain_asic_status_t[i][j] = ' ';
									j++;
								}
								checkbit = num2bit(m);
								if(rxstatusdata.chain_asic_status[i] & checkbit) {
									info->chain_asic_status_t[i][j] = 'o';
								} else {
									info->chain_asic_status_t[i][j] = 'x';
								}
								j++;
							}
							applog(LOG_ERR, "bitmain_initialize bitmain_parse_rxstatus chain(%d) asic_num=%d asic_status=%08x-%s",
									i, info->chain_asic_num[i], info->chain_asic_status[i], info->chain_asic_status_t[i]);
						}
						bitmain_update_temps(bitmain, info, &rxstatusdata);
						statusok = 1;
						break;
					}
				}
				if(statusok) {
					break;
				}
			}
		}
		trycount--;
		p.tv_sec = 0;
		p.tv_nsec = BITMAIN_RESET_PITCH;
		nanosleep(&p, NULL);
	}

	p.tv_sec = 0;
	p.tv_nsec = BITMAIN_RESET_PITCH;
	nanosleep(&p, NULL);

	cgtime(&info->last_status_time);

	if(statusok) {
		applog(LOG_ERR, "bitmain_initialize start send txconfig");
		if(opt_bitmain_hwerror)
			eft = 1;
		else
			eft = 0;
		sendlen = bitmain_set_txconfig((struct bitmain_txconfig_token *)sendbuf, 1, 1, 1, 1, 1, 0, 1, eft,
				info->chain_num, info->asic_num, BITMAIN_DEFAULT_FAN_MAX_PWM, info->timeout,
				info->frequency, BITMAIN_DEFAULT_VOLTAGE, 0, 0, 0x04, info->reg_data);
		if(sendlen <= 0) {
			applog(LOG_ERR, "bitmain_initialize bitmain_set_txconfig error(%d)", sendlen);
			return -1;
		}

		ret = bitmain_send_data(sendbuf, sendlen, bitmain);
		if (unlikely(ret == BTM_SEND_ERROR)) {
			applog(LOG_ERR, "bitmain_initialize bitmain_send_data error");
			return -1;
		}
		applog(LOG_WARNING, "BMM%d: InitSet succeeded", bitmain->device_id);
	} else {
		applog(LOG_WARNING, "BMS%d: InitSet error", bitmain->device_id);
		return -1;
	}
	return 0;
}

static void bitmain_usb_init(struct cgpu_info *bitmain)
{
	int err, interface;

	if (bitmain->usbinfo.nodev)
		return;

	interface = bitmain->usbdev->found->interface;
	// Reset
	err = usb_transfer(bitmain, FTDI_TYPE_OUT, FTDI_REQUEST_RESET,
		FTDI_VALUE_RESET, interface, C_RESET);

	applog(LOG_DEBUG, "%s%i: reset got err %d",
		bitmain->drv->name, bitmain->device_id, err);

	if (bitmain->usbinfo.nodev)
		return;

	// Set latency
	err = usb_transfer(bitmain, FTDI_TYPE_OUT, FTDI_REQUEST_LATENCY,
		BITMAIN_LATENCY, interface, C_LATENCY);

	applog(LOG_DEBUG, "%s%i: latency got err %d",
		bitmain->drv->name, bitmain->device_id, err);

	if (bitmain->usbinfo.nodev)
		return;

	// Set data
	err = usb_transfer(bitmain, FTDI_TYPE_OUT, FTDI_REQUEST_DATA,
				FTDI_VALUE_DATA_BTM, interface, C_SETDATA);

	applog(LOG_DEBUG, "%s%i: data got err %d",
		bitmain->drv->name, bitmain->device_id, err);

	if (bitmain->usbinfo.nodev)
		return;

	// Set the baud
	err = usb_transfer(bitmain, FTDI_TYPE_OUT, FTDI_REQUEST_BAUD, FTDI_VALUE_BAUD_BTM,
				(FTDI_INDEX_BAUD_BTM & 0xff00) | interface,
				C_SETBAUD);

	applog(LOG_DEBUG, "%s%i: setbaud got err %d",
		bitmain->drv->name, bitmain->device_id, err);

	if (bitmain->usbinfo.nodev)
		return;

	// Set Modem Control
	err = usb_transfer(bitmain, FTDI_TYPE_OUT, FTDI_REQUEST_MODEM,
		FTDI_VALUE_MODEM, interface, C_SETMODEM);

	applog(LOG_DEBUG, "%s%i: setmodemctrl got err %d",
		bitmain->drv->name, bitmain->device_id, err);

	if (bitmain->usbinfo.nodev)
		return;

	// Set Flow Control
	err = usb_transfer(bitmain, FTDI_TYPE_OUT, FTDI_REQUEST_FLOW,
		FTDI_VALUE_FLOW, interface, C_SETFLOW);

	applog(LOG_DEBUG, "%s%i: setflowctrl got err %d",
		bitmain->drv->name, bitmain->device_id, err);

	if (bitmain->usbinfo.nodev)
		return;

	/* BitMain repeats the following */
	// Set Modem Control
	err = usb_transfer(bitmain, FTDI_TYPE_OUT, FTDI_REQUEST_MODEM,
		FTDI_VALUE_MODEM, interface, C_SETMODEM);

	applog(LOG_DEBUG, "%s%i: setmodemctrl 2 got err %d",
		bitmain->drv->name, bitmain->device_id, err);

	if (bitmain->usbinfo.nodev)
		return;

	// Set Flow Control
	err = usb_transfer(bitmain, FTDI_TYPE_OUT, FTDI_REQUEST_FLOW,
		FTDI_VALUE_FLOW, interface, C_SETFLOW);

	applog(LOG_DEBUG, "%s%i: setflowctrl 2 got err %d",
		bitmain->drv->name, bitmain->device_id, err);
}

static bool bitmain_usb_detect_one(libusb_device *dev, struct usb_find_devices *found)
{
	int baud, chain_num, asic_num, timeout, frequency = 0;
	uint8_t reg_data[4] = {0};
	int this_option_offset = ++option_offset;
	struct bitmain_info *info;
	struct cgpu_info *bitmain;
	bool configured;
	int ret;

	if (opt_bitmain_options == NULL)
		return false;

	bitmain = usb_alloc_cgpu(&bitmain_drv, BITMAIN_MINER_THREADS);

	configured = get_options(this_option_offset, &baud, &chain_num,
				 &asic_num, &timeout, &frequency, reg_data);

	if (!usb_init(bitmain, dev, found))
		goto shin;

	/* Even though this is an FTDI type chip, we want to do the parsing
	 * all ourselves so set it to std usb type */
	bitmain->usbdev->usb_type = USB_TYPE_STD;
	bitmain->usbdev->PrefPacketSize = BITMAIN_USB_PACKETSIZE;

	/* We have a real BitMain! */
	bitmain_usb_init(bitmain);

	bitmain->device_data = calloc(sizeof(struct bitmain_info), 1);
	if (unlikely(!(bitmain->device_data)))
		quit(1, "Failed to calloc bitmain_info data");
	info = bitmain->device_data;

	if (configured) {
		info->baud = baud;
		info->chain_num = chain_num;
		info->asic_num = asic_num;
		info->timeout = timeout;
		info->frequency = frequency;
		memcpy(info->reg_data, reg_data, 4);
	} else {
		info->baud = BITMAIN_IO_SPEED;
		info->chain_num = BITMAIN_DEFAULT_CHAIN_NUM;
		info->asic_num = BITMAIN_DEFAULT_ASIC_NUM;
		info->timeout = BITMAIN_DEFAULT_TIMEOUT;
		info->frequency = BITMAIN_DEFAULT_FREQUENCY;
		memset(info->reg_data, 0, 4);
	}
	info->voltage = BITMAIN_DEFAULT_VOLTAGE;

	info->fan_pwm = BITMAIN_DEFAULT_FAN_MIN_PWM;
	info->temp_max = 0;
	/* This is for check the temp/fan every 3~4s */
	info->temp_history_count = (4 / (float)((float)info->timeout * ((float)1.67/0x32))) + 1;
	if (info->temp_history_count <= 0)
		info->temp_history_count = 1;

	info->temp_history_index = 0;
	info->temp_sum = 0;
	info->temp_old = 0;

	if (!add_cgpu(bitmain))
		goto unshin;

	applog(LOG_ERR, "------bitmain usb detect one------");
	ret = bitmain_initialize(bitmain);
	if (ret && !configured)
		goto unshin;

	update_usb_stats(bitmain);

	info->errorcount = 0;

	applog(LOG_DEBUG, "BitMain Detected: %s "
	       "(chain_num=%d asic_num=%d timeout=%d frequency=%d)",
	       bitmain->device_path, info->chain_num, info->asic_num, info->timeout,
	       info->frequency);

	return true;

unshin:

	usb_uninit(bitmain);

shin:

	free(bitmain->device_data);
	bitmain->device_data = NULL;

	bitmain = usb_free_cgpu(bitmain);

	return false;
}

static bool bitmain_detect_one(const char * devpath)
{
	int baud, chain_num, asic_num, timeout, frequency = 0;
	uint8_t reg_data[4] = {0};
	int this_option_offset = ++option_offset;
	struct bitmain_info *info;
	struct cgpu_info *bitmain;
	bool configured;
	int ret;

	if (opt_bitmain_options == NULL)
		return false;

	bitmain = btm_alloc_cgpu(&bitmain_drv, BITMAIN_MINER_THREADS);

	configured = get_options(this_option_offset, &baud, &chain_num,
				 &asic_num, &timeout, &frequency, reg_data);

	if (!btm_init(bitmain, opt_bitmain_dev))
		goto shin;
	applog(LOG_ERR, "bitmain_detect_one btm init ok");

	bitmain->device_data = calloc(sizeof(struct bitmain_info), 1);
	if (unlikely(!(bitmain->device_data)))
		quit(1, "Failed to calloc bitmain_info data");
	info = bitmain->device_data;

	if (configured) {
		info->baud = baud;
		info->chain_num = chain_num;
		info->asic_num = asic_num;
		info->timeout = timeout;
		info->frequency = frequency;
		memcpy(info->reg_data, reg_data, 4);
	} else {
		info->baud = BITMAIN_IO_SPEED;
		info->chain_num = BITMAIN_DEFAULT_CHAIN_NUM;
		info->asic_num = BITMAIN_DEFAULT_ASIC_NUM;
		info->timeout = BITMAIN_DEFAULT_TIMEOUT;
		info->frequency = BITMAIN_DEFAULT_FREQUENCY;
		memset(info->reg_data, 0, 4);
	}
	info->voltage = BITMAIN_DEFAULT_VOLTAGE;

	info->fan_pwm = BITMAIN_DEFAULT_FAN_MIN_PWM;
	info->temp_max = 0;
	/* This is for check the temp/fan every 3~4s */
	info->temp_history_count = (4 / (float)((float)info->timeout * ((float)1.67/0x32))) + 1;
	if (info->temp_history_count <= 0)
		info->temp_history_count = 1;

	info->temp_history_index = 0;
	info->temp_sum = 0;
	info->temp_old = 0;

	if (!add_cgpu(bitmain))
		goto unshin;

	ret = bitmain_initialize(bitmain);
	applog(LOG_ERR, "bitmain_detect_one stop bitmain_initialize %d", ret);
	if (ret && !configured)
		goto unshin;

	info->errorcount = 0;

	applog(LOG_DEBUG, "BitMain Detected: %s "
	       "(chain_num=%d asic_num=%d timeout=%d frequency=%d)",
	       bitmain->device_path, info->chain_num, info->asic_num, info->timeout,
	       info->frequency);

	return true;

unshin:
	btm_uninit(bitmain);

shin:
	free(bitmain->device_data);
	bitmain->device_data = NULL;

	bitmain = usb_free_cgpu(bitmain);

	return false;
}

static void bitmain_detect(void)
{
	applog(LOG_DEBUG, "BTM detect dev: %s", opt_bitmain_dev);
	if(strlen(opt_bitmain_dev) <= 0) {
		opt_bitmain_dev_usb = true;
	} else {
		opt_bitmain_dev_usb = false;
	}
	if(opt_bitmain_dev_usb) {
		usb_detect(&bitmain_drv, bitmain_usb_detect_one);
	} else {
		btm_detect(&bitmain_drv, bitmain_detect_one);
	}
}

static void do_bitmain_close(struct thr_info *thr)
{
	struct cgpu_info *bitmain = thr->cgpu;
	struct bitmain_info *info = bitmain->device_data;

	pthread_join(info->read_thr, NULL);
	pthread_join(info->write_thr, NULL);
	bitmain_running_reset(bitmain, info);

	info->no_matching_work = 0;

	cgsem_destroy(&info->write_sem);
}

static void get_bitmain_statline_before(char *buf, size_t bufsiz, struct cgpu_info *bitmain)
{
	struct bitmain_info *info = bitmain->device_data;
	int lowfan = 10000;
	int i = 0;

	/* Find the lowest fan speed of the ASIC cooling fans. */
	for(i = 0; i < info->fan_num; i++) {
		if (info->fan[i] >= 0 && info->fan[i] < lowfan)
			lowfan = info->fan[i];
	}

	tailsprintf(buf, bufsiz, "%2d/%3dC %04dR | ", info->temp_avg, info->temp_max, lowfan);
}

/* We use a replacement algorithm to only remove references to work done from
 * the buffer when we need the extra space for new work. */
static bool bitmain_fill(struct cgpu_info *bitmain)
{
	struct bitmain_info *info = bitmain->device_data;
	int subid, slot;
	struct work *work;
	bool ret = true;
	int sendret = 0, sendcount = 0, neednum = 0, queuednum = 0, sendnum = 0, sendlen = 0;
	uint8_t sendbuf[BITMAIN_SENDBUF_SIZE];
	cgtimer_t ts_start;
	int senderror = 0;
	struct timeval now;
	int timediff = 0;

	applog(LOG_DEBUG, "BTM bitmain_fill start--------");
	mutex_lock(&info->qlock);
	if(info->fifo_space <= 0) {
		applog(LOG_DEBUG, "BTM bitmain_fill fifo space empty--------");
		ret = true;
		goto out_unlock;
	}
	if (bitmain->queued >= BITMAIN_MAX_WORK_QUEUE_NUM) {
		ret = true;
	} else {
		ret = false;
	}
	while(info->fifo_space > 0) {
		neednum = info->fifo_space<8?info->fifo_space:8;
		queuednum = bitmain->queued;
		applog(LOG_DEBUG, "BTM: Work task queued(%d) fifo space(%d) needsend(%d)", queuednum, info->fifo_space, neednum);
		if(queuednum < neednum) {
			while(true) {
				work = get_queued(bitmain);
				if (unlikely(!work)) {
					break;
				} else {
					applog(LOG_DEBUG, "BTM get work queued number:%d neednum:%d", queuednum, neednum);
					subid = bitmain->queued++;
					work->subid = subid;
					slot = bitmain->work_array + subid;
					if (slot > BITMAIN_ARRAY_SIZE) {
						applog(LOG_DEBUG, "bitmain_fill array cyc %d", BITMAIN_ARRAY_SIZE);
						slot = 0;
					}
					if (likely(bitmain->works[slot])) {
						applog(LOG_DEBUG, "bitmain_fill work_completed %d", slot);
						work_completed(bitmain, bitmain->works[slot]);
					}
					bitmain->works[slot] = work;
					queuednum++;
					if(queuednum >= neednum) {
						break;
					}
				}
			}
		}
		if(queuednum < BITMAIN_MAX_DEAL_QUEUE_NUM) {
			if(queuednum < neednum) {
				applog(LOG_DEBUG, "BTM: No enough work to send, queue num=%d", queuednum);
				break;
			}
		}
		sendnum = queuednum < neednum ? queuednum : neednum;
		sendlen = bitmain_set_txtask(sendbuf, &(info->last_work_block), bitmain->works, BITMAIN_ARRAY_SIZE, bitmain->work_array, sendnum, &sendcount);
		bitmain->queued -= sendnum;
		info->send_full_space += sendnum;
		if (bitmain->queued < 0)
			bitmain->queued = 0;
		if (bitmain->work_array + sendnum > BITMAIN_ARRAY_SIZE) {
			bitmain->work_array = bitmain->work_array + sendnum-BITMAIN_ARRAY_SIZE;
		} else {
			bitmain->work_array += sendnum;
		}
		applog(LOG_DEBUG, "BTM: Send work array %d", bitmain->work_array);
		if (sendlen > 0) {
			info->fifo_space -= sendcount;
			if (info->fifo_space < 0)
				info->fifo_space = 0;
			sendret = bitmain_send_data(sendbuf, sendlen, bitmain);
			if (unlikely(sendret == BTM_SEND_ERROR)) {
				applog(LOG_ERR, "BTM%i: Comms error(buffer)", bitmain->device_id);
				//dev_error(bitmain, REASON_DEV_COMMS_ERROR);
				info->reset = true;
				info->errorcount++;
				senderror = 1;
				if (info->errorcount > 1000) {
					info->errorcount = 0;
					applog(LOG_ERR, "%s%d: Device disappeared, shutting down thread", bitmain->drv->name, bitmain->device_id);
					bitmain->shutdown = true;
				}
				break;
			} else {
				applog(LOG_DEBUG, "bitmain_send_data send ret=%d", sendret);
				info->errorcount = 0;
			}
		} else {
			applog(LOG_DEBUG, "BTM: Send work bitmain_set_txtask error: %d", sendlen);
			break;
		}
	}

out_unlock:
	cgtime(&now);
	timediff = now.tv_sec - info->last_status_time.tv_sec;
	if(timediff < 0) timediff = -timediff;

	if (now.tv_sec - info->last_status_time.tv_sec > BITMAIN_SEND_STATUS_TIME) {
		applog(LOG_DEBUG, "BTM: Send RX Status Token fifo_space(%d) timediff(%d)",
			info->fifo_space, timediff);
		copy_time(&(info->last_status_time), &now);

		sendlen = bitmain_set_rxstatus((struct bitmain_rxstatus_token *) sendbuf, 0, 0, 0, 0);
		if (sendlen > 0) {
			sendret = bitmain_send_data(sendbuf, sendlen, bitmain);
			if (unlikely(sendret == BTM_SEND_ERROR)) {
				applog(LOG_ERR, "BTM%i: Comms error(buffer)", bitmain->device_id);
				//dev_error(bitmain, REASON_DEV_COMMS_ERROR);
				info->reset = true;
				info->errorcount++;
				senderror = 1;
				if (info->errorcount > 1000) {
					info->errorcount = 0;
					applog(LOG_ERR, "%s%d: Device disappeared, shutting down thread", bitmain->drv->name, bitmain->device_id);
					bitmain->shutdown = true;
				}
			} else {
				info->errorcount = 0;
				if (info->fifo_space <= 0) {
					senderror = 1;
				}
			}
		}
	}
	if(info->send_full_space > BITMAIN_SEND_FULL_SPACE) {
		info->send_full_space = 0;
		ret = true;
	}
	mutex_unlock(&info->qlock);
	if(senderror) {
		ret = true;
		cgsleep_prepare_r(&ts_start);
		cgsleep_ms_r(&ts_start, 50);
	}
	return ret;
}

static int64_t bitmain_scanhash(struct thr_info *thr)
{
	struct cgpu_info *bitmain = thr->cgpu;
	struct bitmain_info *info = bitmain->device_data;
	const int chain_num = info->chain_num;
	struct timeval now, then, tdiff;
	int64_t hash_count, us_timeout;
	struct timespec abstime;
	int ret;

	/* Half nonce range */
	us_timeout = 0x80000000ll / info->asic_num / info->frequency;
	tdiff.tv_sec = us_timeout / 1000000;
	tdiff.tv_usec = us_timeout - (tdiff.tv_sec * 1000000);
	cgtime(&now);
	timeradd(&now, &tdiff, &then);
	abstime.tv_sec = then.tv_sec;
	abstime.tv_nsec = then.tv_usec * 1000;

	applog(LOG_DEBUG, "bitmain_scanhash info->qlock start");
	mutex_lock(&info->qlock);
	hash_count = 0xffffffffull * (uint64_t)info->nonces;
	bitmain->results += info->nonces + info->idle;
	if (bitmain->results > chain_num)
		bitmain->results = chain_num;
	if (!info->reset)
		bitmain->results--;
	info->nonces = info->idle = 0;
	mutex_unlock(&info->qlock);
	applog(LOG_DEBUG, "bitmain_scanhash info->qlock stop");

	/* Check for nothing but consecutive bad results or consistently less
	 * results than we should be getting and reset the FPGA if necessary */
	//if (bitmain->results < -chain_num && !info->reset) {
	//	applog(LOG_ERR, "BTM%d: Result return rate low, resetting!",
	//		bitmain->device_id);
	//	info->reset = true;
	//}

	if (unlikely(bitmain->usbinfo.nodev)) {
		applog(LOG_ERR, "BTM%d: Device disappeared, shutting down thread",
		       bitmain->device_id);
		bitmain->shutdown = true;
	}

	/* This hashmeter is just a utility counter based on returned shares */
	return hash_count;
}

static void bitmain_flush_work(struct cgpu_info *bitmain)
{
	struct bitmain_info *info = bitmain->device_data;
	int i = 0;

	mutex_lock(&info->qlock);
	/* Will overwrite any work queued */
	applog(LOG_ERR, "bitmain_flush_work queued=%d array=%d", bitmain->queued, bitmain->work_array);
	if(bitmain->queued > 0) {
		if (bitmain->work_array + bitmain->queued > BITMAIN_ARRAY_SIZE) {
			bitmain->work_array = bitmain->work_array + bitmain->queued-BITMAIN_ARRAY_SIZE;
		} else {
			bitmain->work_array += bitmain->queued;
		}
	}
	bitmain->queued = 0;
	//bitmain->work_array = 0;
	//for(i = 0; i < BITMAIN_ARRAY_SIZE; i++) {
	//	bitmain->works[i] = NULL;
	//}
	//pthread_cond_signal(&info->qcond);
	mutex_unlock(&info->qlock);
}

static struct api_data *bitmain_api_stats(struct cgpu_info *cgpu)
{
	struct api_data *root = NULL;
	struct bitmain_info *info = cgpu->device_data;
	char buf[64];
	int i = 0;
	double hwp = (cgpu->hw_errors + cgpu->diff1) ?
			(double)(cgpu->hw_errors) / (double)(cgpu->hw_errors + cgpu->diff1) : 0;

	root = api_add_int(root, "baud", &(info->baud), false);
	root = api_add_int(root, "miner_count", &(info->chain_num), false);
	root = api_add_int(root, "asic_count", &(info->asic_num), false);
	root = api_add_int(root, "timeout", &(info->timeout), false);
	root = api_add_int(root, "frequency", &(info->frequency), false);
	root = api_add_int(root, "voltage", &(info->voltage), false);

	root = api_add_int(root, "fan_num", &(info->fan_num), false);
	root = api_add_int(root, "fan1", &(info->fan[0]), false);
	root = api_add_int(root, "fan2", &(info->fan[1]), false);
	root = api_add_int(root, "fan3", &(info->fan[2]), false);
	root = api_add_int(root, "fan4", &(info->fan[3]), false);

	root = api_add_int(root, "temp_num", &(info->temp_num), false);
	root = api_add_int(root, "temp1", &(info->temp[0]), false);
	root = api_add_int(root, "temp2", &(info->temp[1]), false);
	root = api_add_int(root, "temp3", &(info->temp[2]), false);
	root = api_add_int(root, "temp4", &(info->temp[3]), false);
	root = api_add_int(root, "temp_avg", &(info->temp_avg), false);
	root = api_add_int(root, "temp_max", &(info->temp_max), false);
	root = api_add_percent(root, "Device Hardware%", &hwp, true);
	root = api_add_int(root, "no_matching_work", &(info->no_matching_work), false);
	/*
	for (i = 0; i < info->chain_num; i++) {
		char mcw[24];

		sprintf(mcw, "match_work_count%d", i + 1);
		root = api_add_int(root, mcw, &(info->matching_work[i]), false);
	}*/

	root = api_add_int(root, "chain_acn1", &(info->chain_asic_num[0]), false);
	root = api_add_int(root, "chain_acn2", &(info->chain_asic_num[1]), false);
	root = api_add_int(root, "chain_acn3", &(info->chain_asic_num[2]), false);
	root = api_add_int(root, "chain_acn4", &(info->chain_asic_num[3]), false);

	//applog(LOG_ERR, "chain asic status:%s", info->chain_asic_status_t[0]);
	root = api_add_string(root, "chain_acs1", info->chain_asic_status_t[0], false);
	root = api_add_string(root, "chain_acs2", info->chain_asic_status_t[1], false);
	root = api_add_string(root, "chain_acs3", info->chain_asic_status_t[2], false);
	root = api_add_string(root, "chain_acs4", info->chain_asic_status_t[3], false);

	//root = api_add_int(root, "chain_acs1", &(info->chain_asic_status[0]), false);
	//root = api_add_int(root, "chain_acs2", &(info->chain_asic_status[1]), false);
	//root = api_add_int(root, "chain_acs3", &(info->chain_asic_status[2]), false);
	//root = api_add_int(root, "chain_acs4", &(info->chain_asic_status[3]), false);

	return root;
}

static void bitmain_shutdown(struct thr_info *thr)
{
	do_bitmain_close(thr);
}

char *set_bitmain_dev(char *arg)
{
	if(arg == NULL || strlen(arg) <= 0) {
		memcpy(opt_bitmain_dev, 0, 256);
	} else {
		strncpy(opt_bitmain_dev, arg, 256);
	}
	applog(LOG_DEBUG, "BTM set device: %s", opt_bitmain_dev);
	return NULL;
}

char *set_bitmain_fan(char *arg)
{
	int val1, val2, ret;

	ret = sscanf(arg, "%d-%d", &val1, &val2);
	if (ret < 1)
		return "No values passed to bitmain-fan";
	if (ret == 1)
		val2 = val1;

	if (val1 < 0 || val1 > 100 || val2 < 0 || val2 > 100 || val2 < val1)
		return "Invalid value passed to bitmain-fan";

	opt_bitmain_fan_min = val1 * BITMAIN_PWM_MAX / 100;
	opt_bitmain_fan_max = val2 * BITMAIN_PWM_MAX / 100;

	return NULL;
}

char *set_bitmain_freq(char *arg)
{
	int val1, val2, ret;

	ret = sscanf(arg, "%d-%d", &val1, &val2);
	if (ret < 1)
		return "No values passed to bitmain-freq";
	if (ret == 1)
		val2 = val1;

	if (val1 < BITMAIN_MIN_FREQUENCY || val1 > BITMAIN_MAX_FREQUENCY ||
	    val2 < BITMAIN_MIN_FREQUENCY || val2 > BITMAIN_MAX_FREQUENCY ||
	    val2 < val1)
		return "Invalid value passed to bitmain-freq";

	opt_bitmain_freq_min = val1;
	opt_bitmain_freq_max = val2;

	return NULL;
}

struct device_drv bitmain_drv = {
	.drv_id = DRIVER_BITMAIN,
	.dname = "bitmain",
	.name = "BTM",
	.drv_detect = bitmain_detect,
	.thread_prepare = bitmain_prepare,
	.hash_work = hash_queued_work,
	.queue_full = bitmain_fill,
	.scanwork = bitmain_scanhash,
	.flush_work = bitmain_flush_work,
	.get_api_stats = bitmain_api_stats,
	.get_statline_before = get_bitmain_statline_before,
	.reinit_device = bitmain_init,
	.thread_shutdown = bitmain_shutdown,
};
