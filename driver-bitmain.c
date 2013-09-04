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

uint16_t CRC16(const uint8_t* p_data, uint16_t w_len)
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

static int bitmain_set_txconfig(struct bitmain_txconfig_token *bm,
			    uint8_t reset, uint8_t fan_eft, uint8_t timeout_eft, uint8_t frequency_eft,
			    uint8_t voltage_eft, uint8_t chain_check_time_eft, uint8_t chip_config_eft,
			    uint8_t miner_num, uint8_t asic_num, uint8_t fan_pwm_data, uint8_t timeout_data,
			    uint16_t frequency, uint8_t voltage, uint8_t chain_check_time,
			    uint8_t chip_address, uint8_t reg_address, uint32_t reg_data)
{
	uint16_t crc = 0;
	int datalen = 0;
	if (unlikely(!bm)) {
		applog(LOG_WARNING, "bitmain_set_txconfig bitmain_txconfig_token is null");
		return -1;
	}

	if (unlikely(timeout_data <= 0 || asic_num <= 0 || miner_num <= 0)) {
		applog(LOG_WARNING, "bitmain_set_txconfig parameter invalid timeout_data(%d) asic_num(%d) miner_num(%d)",
				timeout_data, asic_num, miner_num);
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

	bm->miner_num = miner_num;
	bm->asic_num = asic_num;
	bm->fan_pwm_data = fan_pwm_data;
	bm->timeout_data = timeout_data;

	bm->frequency = htole16(frequency);
	bm->voltage = voltage;
	bm->chain_check_time = chain_check_time;

	bm->reg_data = htole32(reg_data);
	bm->chip_address = chip_address;
	bm->reg_address = reg_address;

	crc = CRC16((uint8_t *)bm, datalen-2);
	bm->crc = htole16(crc);

	applog(LOG_DEBUG, "BitMain TxConfig Token: reset(%d) fan_eft(%d) timeout_eft(%d) frequency_eft(%d) voltage_eft(%d) chain_check_time_eft(%d) chip_config_eft(%d) miner_num(%d) asic_num(%d) fan_pwm_data(%d) timeout_data(%d) frequency(%d) voltage(%d) chain_check_time(%d) reg_data(%08x) chip_address(%02x) reg_address(%02x) crc(%04x)",
					reset, fan_eft, timeout_eft, frequency_eft, voltage_eft,
					chain_check_time_eft, chip_config_eft, miner_num, asic_num,
					fan_pwm_data, timeout_data, frequency, voltage,
					chain_check_time, reg_data, chip_address, reg_address, crc);

	return datalen;
}

static int bitmain_set_txtask(struct bitmain_txtask_token *bm,
			    uint8_t new_block, struct work *work)
{
	uint16_t crc = 0;
	uint32_t work_id = 0;
	int datalen = 0;

	if (unlikely(!bm)) {
		applog(LOG_WARNING, "bitmain_set_txtask bitmain_txtask_token is null");
		return -1;
	}
	if (unlikely(!work)) {
		applog(LOG_WARNING, "bitmain_set_txtask work is null");
		return -1;
	}
	datalen = sizeof(struct bitmain_txtask_token);
	memset(bm, 0, datalen);

	work_id = work->id;

	bm->token_type = BITMAIN_TOKEN_TYPE_TXTASK;
	bm->length = datalen-2;

	bm->new_block = new_block;

	bm->work_id = htole32(work_id);

	memcpy(bm->midstate, work->midstate, 32);
	memcpy(bm->data2, work->data + 64, 12);

	crc = CRC16((uint8_t *)bm, datalen-2);
	bm->crc = htole16(crc);

	applog(LOG_DEBUG, "BitMain TxTask Token: new_block(%d) work_id(%d) crc(%04x)",
						new_block, work_id, crc);

	return datalen;
}

static int bitmain_set_rxstatus(struct bitmain_rxstatus_token *bm,
		uint8_t chip_status_eft, uint8_t detect_get, uint8_t chip_address, uint8_t reg_address)
{
	uint16_t crc = 0;
	int datalen = 0;

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
		applog(LOG_ERR, "bitmain_parse_rxstatus check crc(%d) != bm crc(%d)", crc, bm->crc);
		return -1;
	}
	if(bm->temp_num + bm->fan_num + 14 != datalen) {
		applog(LOG_ERR, "bitmain_parse_rxstatus temp_num(%d) fan_num(%d) not match datalen(%d)",
				bm->temp_num, bm->fan_num, datalen);
		return -1;
	}
	if(bm->temp_num > 0) {
		memcpy(bm->temp, data+12, bm->temp_num);
	}
	if(bm->fan_num > 0) {
		memcpy(bm->fan, data+12+bm->temp_num, bm->fan_num);
	}
	applog(LOG_DEBUG, "BitMain RxStatus Data: chip_value_eft(%d) reg_value(%d) miner_num(%d) asic_num(%d) temp_num(%d) fan_num(%d) crc(%04x)",
			bm->chip_value_eft, bm->reg_value, bm->miner_num, bm->asic_num, bm->temp_num, bm->fan_num, bm->crc);
	return 0;
}

static int bitmain_parse_rxnonce(const uint8_t * data, int datalen, struct bitmain_rxnonce_data *bm)
{
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
	bm->work_id = htole32(bm->work_id);
	bm->nonce = htole32(bm->nonce);

	applog(LOG_DEBUG, "BitMain RxNonce Data: work_id(%d) nonce(%08x)",
				bm->work_id, bm->nonce);
	return 0;
}

static int bitmain_write(struct cgpu_info *bitmain, char *buf, ssize_t len, int ep)
{
	int err, amount;

	err = usb_write(bitmain, buf, len, &amount, ep);
	applog(LOG_DEBUG, "%s%i: usb_write got err %d", bitmain->drv->name,
	       bitmain->device_id, err);

	if (unlikely(err != 0)) {
		applog(LOG_WARNING, "usb_write error on bitmain_write");
		return BTM_SEND_ERROR;
	}
	if (amount != len) {
		applog(LOG_WARNING, "usb_write length mismatch on bitmain_write");
		return BTM_SEND_ERROR;
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
	delay = datalen * 10 * 1000000;
	delay = delay / info->baud;
	delay += 4000;

	if(opt_debug) {
		applog(LOG_DEBUG, "BitMain: Sent(%d):", datalen);
		hexdump(data, datalen);
	}

	cgsleep_prepare_r(&ts_start);
	ret = bitmain_write(bitmain, (char *)data, datalen, ep);
	cgsleep_us_r(&ts_start, delay);

	applog(LOG_DEBUG, "BitMain: Sent: Buffer delay: %dus", delay);

	return ret;
}

static bool bitmain_decode_nonce(struct thr_info *thr, struct cgpu_info *bitmain,
				struct bitmain_info *info, struct bitmain_rxnonce_data *rxnoncedata, struct work *work)
{
	info = bitmain->device_data;
	info->matching_work[work->subid]++;
	applog(LOG_DEBUG, "BitMain: nonce = %0x08x", rxnoncedata->nonce);
	return submit_nonce(thr, work, rxnoncedata->nonce);
}

/* Wait until the ftdi chip returns a CTS saying we can send more data. */
static void wait_bitmain_ready(struct cgpu_info *bitmain)
{
	while (bitmain_buffer_full(bitmain)) {
		cgsleep_ms(40);
	}
}

#define BITMAIN_CTS    (1 << 4)

static inline bool bitmain_cts(char c)
{
	return (c & BITMAIN_CTS);
}

static int bitmain_read(struct cgpu_info *bitmain, unsigned char *buf,
		       size_t bufsize, int timeout, int ep)
{
	struct bitmain_info *info = bitmain->device_data;
	size_t total = 0;
	char readbuf[BITMAIN_READBUF_SIZE];
	int err = 0, readlen = 0;

	if(bitmain == NULL || buf == NULL || bufsize <= 0) {
		applog(LOG_WARNING, "bitmain_read parameter error bufsize(%d)", bufsize);
		return -1;
	}

	err = usb_read_once_timeout(bitmain, readbuf, bufsize, &readlen, timeout, ep);
	applog(LOG_DEBUG, "%s%i: Get bitmain read got err %d",
			bitmain->drv->name, bitmain->device_id, err);

	if (readlen < 2)
		goto out;
	total = readlen-2;

	/* The first 2 of every 64 bytes are status on FTDIRL */
	memcpy(buf, readbuf+2, readlen-2);
out:
	return total;
}

static int bitmain_initset(struct cgpu_info *bitmain)
{
	uint8_t data[BITMAIN_READBUF_SIZE];
	struct bitmain_info *info = NULL;
	int ret = 0, spare = 0;
	uint8_t sendbuf[BITMAIN_SENDBUF_SIZE];
	int sendlen = 0;
	int trycount = 3;
	struct timespec p;
	struct bitmain_rxstatus_data rxstatusdata;

	/* Send reset, then check for result */
	if(!bitmain) {
		applog(LOG_WARNING, "bitmain_initset cgpu_info is null");
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
		applog(LOG_ERR, "bitmain_initset bitmain_set_rxstatus error(%d)", sendlen);
		return -1;
	}

	wait_bitmain_ready(bitmain);
	ret = bitmain_send_data(sendbuf, sendlen, bitmain);
	if (unlikely(ret == BTM_SEND_ERROR)) {
		applog(LOG_ERR, "bitmain_initset bitmain_send_data error");
		return -1;
	}
	while(trycount >= 0) {
		ret = bitmain_read(bitmain, data, BITMAIN_READBUF_SIZE,
			  BITMAIN_RESET_TIMEOUT, C_BITMAIN_DATA_RXSTATUS);
		if(ret > 0) {
			break;
		}
		trycount--;
		p.tv_sec = 0;
		p.tv_nsec = BITMAIN_RESET_PITCH;
		nanosleep(&p, NULL);
	}

	p.tv_sec = 0;
	p.tv_nsec = BITMAIN_RESET_PITCH;
	nanosleep(&p, NULL);

	if(ret > 0) {
		if (opt_debug) {
			applog(LOG_DEBUG, "%s%d initset: get:", bitmain->drv->name, bitmain->device_id);
			hexdump(data, ret);
		}
		if (bitmain_parse_rxstatus(data, ret, &rxstatusdata) != 0) {
			applog(LOG_ERR, "bitmain_initset bitmain_parse_rxstatus error");
			return -1;
		}
		info->miner_num = rxstatusdata.miner_num;
		info->asic_num = rxstatusdata.asic_num;

		sendlen = bitmain_set_txconfig((struct bitmain_txconfig_token *)sendbuf, 1, 1, 1, 1, 1, 0, 0,
				info->miner_num, info->asic_num, BITMAIN_DEFAULT_FAN_MAX_PWM, info->timeout,
				BITMAIN_DEFAULT_FREQUENCY, BITMAIN_DEFAULT_VOLTAGE, 0, 0, 0, 0);
		if(sendlen <= 0) {
			applog(LOG_ERR, "bitmain_initset bitmain_set_txconfig error(%d)", sendlen);
			return -1;
		}

		wait_bitmain_ready(bitmain);
		ret = bitmain_send_data(sendbuf, sendlen, bitmain);
		if (unlikely(ret == BTM_SEND_ERROR)) {
			applog(LOG_ERR, "bitmain_initset bitmain_send_data error");
			return -1;
		}
		applog(LOG_WARNING, "BMM%d: InitSet succeeded", bitmain->device_id);
	} else {
		applog(LOG_WARNING, "BMS%d: InitSet succeeded", bitmain->device_id);
	}
	return 0;
}

static bool get_options(int this_option_offset, int *baud, int *miner_num,
			int *asic_num, int *timeout, int *frequency)
{
	char buf[BUFSIZ+1];
	char *ptr, *comma, *colon, *colon2, *colon3, *colon4;
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
			if (tmp > 0 && tmp <= BITMAIN_DEFAULT_MINER_NUM) {
				*miner_num = tmp;
			} else {
				quit(1, "Invalid bitmain-options for "
					"miner_num (%s) must be 1 ~ %d",
					colon, BITMAIN_DEFAULT_MINER_NUM);
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
					tmp = atoi(colon4);
					if (tmp < BITMAIN_MIN_FREQUENCY || tmp > BITMAIN_MAX_FREQUENCY) {
						quit(1, "Invalid bitmain-options for frequency, must be %d <= frequency <= %d",
						     BITMAIN_MIN_FREQUENCY, BITMAIN_MAX_FREQUENCY);
					}
					*frequency = tmp;
				}
			}
		}
	}
	return true;
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

static void bitmain_initialise(struct cgpu_info *bitmain)
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

static bool bitmain_detect_one(libusb_device *dev, struct usb_find_devices *found)
{
	int baud, miner_num, asic_num, timeout, frequency = 0;
	int this_option_offset = ++option_offset;
	struct bitmain_info *info;
	struct cgpu_info *bitmain;
	bool configured;
	int ret;

	bitmain = usb_alloc_cgpu(&bitmain_drv, BITMAIN_MINER_THREADS);

	configured = get_options(this_option_offset, &baud, &miner_num,
				 &asic_num, &timeout, &frequency);

	if (!usb_init(bitmain, dev, found))
		goto shin;

	/* Even though this is an FTDI type chip, we want to do the parsing
	 * all ourselves so set it to std usb type */
	bitmain->usbdev->usb_type = USB_TYPE_STD;
	bitmain->usbdev->PrefPacketSize = BITMAIN_USB_PACKETSIZE;

	/* We have a real BitMain! */
	bitmain_initialise(bitmain);

	bitmain->device_data = calloc(sizeof(struct bitmain_info), 1);
	if (unlikely(!(bitmain->device_data)))
		quit(1, "Failed to calloc bitmain_info data");
	info = bitmain->device_data;

	if (configured) {
		info->baud = baud;
		info->miner_num = miner_num;
		info->asic_num = asic_num;
		info->timeout = timeout;
		info->frequency = frequency;
	} else {
		info->baud = BITMAIN_IO_SPEED;
		info->miner_num = BITMAIN_DEFAULT_MINER_NUM;
		info->asic_num = BITMAIN_DEFAULT_ASIC_NUM;
		info->timeout = BITMAIN_DEFAULT_TIMEOUT;
		info->frequency = BITMAIN_DEFAULT_FREQUENCY;
	}

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

	ret = bitmain_initset(bitmain);
	if (ret && !configured)
		goto unshin;

	update_usb_stats(bitmain);

	applog(LOG_DEBUG, "BitMain Detected: %s "
	       "(miner_num=%d asic_num=%d timeout=%d frequency=%d)",
	       bitmain->device_path, info->miner_num, info->asic_num, info->timeout,
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

static void bitmain_detect(void)
{
	usb_detect(&bitmain_drv, bitmain_detect_one);
}

static void bitmain_init(struct cgpu_info *bitmain)
{
	applog(LOG_INFO, "BitMain: Opened on %s", bitmain->device_path);
}

static void bitmain_update_temps(struct cgpu_info *bitmain, struct bitmain_info *info,
		struct bitmain_rxstatus_data *bm);

static void bitmain_inc_nvw(struct bitmain_info *info, struct thr_info *thr)
{
	applog(LOG_INFO, "%s%d: No matching work - HW error",
	       thr->cgpu->drv->name, thr->cgpu->device_id);

	inc_hw_errors(thr);
	info->no_matching_work++;
}

static void bitmain_parse_results(struct cgpu_info *bitmain, struct bitmain_info *info,
				 struct thr_info *thr, uint8_t *buf, int *offset)
{
	int i, spare = BITMAIN_READ_SIZE;
	bool found = false;

	for (i = 0; i <= spare; i++) {
		struct work *work;
		if(buf[i] == 0xa1) {
			struct bitmain_rxstatus_data rxstatusdata;
			applog(LOG_DEBUG, "bitmain_parse_results RxStatus Data");
			if(*offset < 2) {
				return;
			}
			if(*offset < buf[1] + 2) {
				return;
			}
			if(bitmain_parse_rxstatus(buf+i, buf[i+1]+2, &rxstatusdata) != 0) {
				applog(LOG_ERR, "bitmain_parse_results bitmain_parse_rxstatus error");
			} else {
				bitmain_update_temps(bitmain, info, &rxstatusdata);
			}

			found = true;
			spare = buf[i+1] + 2 + i;
			break;
		} else if(buf[i] == 0xa2) {
			struct bitmain_rxnonce_data rxnoncedata;
			applog(LOG_DEBUG, "bitmain_parse_results RxNonce Data");
			if(*offset < 12) {
				return;
			}
			if(bitmain_parse_rxnonce(buf+i, buf[i+1]+2, &rxnoncedata) != 0) {
				applog(LOG_ERR, "bitmain_parse_results bitmain_parse_rxnonce error");
			} else {
				work = find_queued_work_byid(bitmain, rxnoncedata.work_id);
				if(work) {
				 	applog(LOG_DEBUG, "bitmain_parse_results nonce find work(%d)", rxnoncedata.work_id);
				 	if (bitmain_decode_nonce(thr, bitmain, info, &rxnoncedata, work)) {
				 		mutex_lock(&info->lock);
				 		info->auto_nonces++;
				 		mutex_unlock(&info->lock);
				 	} else if (opt_bitmain_auto) {
				 		mutex_lock(&info->lock);
				 		info->auto_hw++;
				 		mutex_unlock(&info->lock);
				 	}
				 	if (i) {
				 		if (i >= (int)BITMAIN_READ_SIZE)
				 			bitmain_inc_nvw(info, thr);
				 		else
				 			applog(LOG_WARNING, "BitMain: Discarding %d bytes from buffer", i);
				 	}
				 } else {
				 	applog(LOG_ERR, "bitmain_parse_results nonce not find work(%d)", rxnoncedata.work_id);
				 }
			}

 			found = true;
 			spare = 12 + i;
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

	snprintf(threadname, 24, "btm_recv/%d", bitmain->device_id);
	RenameThread(threadname);
	cgsleep_prepare_r(&ts_start);

	while (likely(!bitmain->shutdown)) {
		unsigned char buf[rsize];

		if (offset >= (int)BITMAIN_READ_SIZE) {
			bitmain_parse_results(bitmain, info, thr, readbuf, &offset);
		}

		if (unlikely(offset + rsize >= BITMAIN_READBUF_SIZE)) {
			/* This should never happen */
			applog(LOG_ERR, "BitMain readbuf overflow, resetting buffer");
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
		if (ret < 1 || offset == 0) {
			cgsleep_ms_r(&ts_start, BITMAIN_READ_TIMEOUT);
		}

		cgsleep_prepare_r(&ts_start);
		ret = bitmain_read(bitmain, buf, rsize, BITMAIN_READ_TIMEOUT,
				  C_BITMAIN_READ);

		if (ret < 1)
			continue;

		if (opt_debug) {
			applog(LOG_DEBUG, "BitMain: get:");
			hexdump((uint8_t *)buf, ret);
		}

		memcpy(readbuf+offset, buf, ret);
		offset += ret;
	}
	return NULL;
}

static void bitmain_rotate_array(struct cgpu_info *bitmain)
{
	bitmain->queued = 0;
	if (++bitmain->work_array >= BITMAIN_ARRAY_SIZE)
		bitmain->work_array = 0;
}

static void bitmain_set_timeout(struct bitmain_info *info)
{
	info->timeout = BITMAIN_TIMEOUT_FACTOR / info->frequency;
}

static void bitmain_inc_freq(struct bitmain_info *info)
{
	info->frequency += 2;
	if (info->frequency > opt_bitmain_freq_max)
		info->frequency = opt_bitmain_freq_max;
	bitmain_set_timeout(info);
	applog(LOG_NOTICE, "BitMain increasing frequency to %d, timeout %d",
	       info->frequency, info->timeout);
}

static void bitmain_dec_freq(struct bitmain_info *info)
{
	info->frequency -= 1;
	if (info->frequency < opt_bitmain_freq_min)
		info->frequency = opt_bitmain_freq_min;
	bitmain_set_timeout(info);
	applog(LOG_NOTICE, "BitMain decreasing frequency to %d, timeout %d",
	       info->frequency, info->timeout);
}

static void bitmain_reset_auto(struct bitmain_info *info)
{
	info->auto_queued =
	info->auto_nonces =
	info->auto_hw = 0;
}

static void *bitmain_send_tasks(void *userdata)
{
	struct cgpu_info *bitmain = (struct cgpu_info *)userdata;
	struct bitmain_info *info = bitmain->device_data;
	const int bitmain_get_work_count = info->miner_num;
	char threadname[24];
	uint8_t sendbuf[BITMAIN_SENDBUF_SIZE];
	int sendlen = 0;
	char lastjobid[1024] = {0};
	uint8_t new_block = 1;
	int laststatustime = 0;

	snprintf(threadname, 24, "btm_send/%d", bitmain->device_id);
	RenameThread(threadname);

	while (likely(!bitmain->shutdown)) {
		int start_count, end_count, i, j, ret;
		bool idled = false;

		while (bitmain_buffer_full(bitmain))
			cgsem_wait(&info->write_sem);

		if (opt_bitmain_auto && info->auto_queued >= BITMAIN_AUTO_CYCLE) {
			mutex_lock(&info->lock);
			if (!info->optimal) {
				if (info->fan_pwm >= opt_bitmain_fan_max) {
					applog(LOG_WARNING,
					       "BTM%i: Above optimal temperature, throttling",
					       bitmain->device_id);
					bitmain_dec_freq(info);
				}
			} else if (info->auto_nonces >= (BITMAIN_AUTO_CYCLE * 19 / 20) &&
				   info->auto_nonces <= (BITMAIN_AUTO_CYCLE * 21 / 20)) {
					int total = info->auto_nonces + info->auto_hw;

					/* Try to keep hw errors < 2% */
					if (info->auto_hw * 100 < total)
						bitmain_inc_freq(info);
					else if (info->auto_hw * 66 > total)
						bitmain_dec_freq(info);
			}
			bitmain_reset_auto(info);
			mutex_unlock(&info->lock);
		}

		mutex_lock(&info->qlock);
		start_count = bitmain->work_array * bitmain_get_work_count;
		end_count = start_count + bitmain_get_work_count;
		for (i = start_count, j = 0; i < end_count; i++, j++) {
			if (bitmain_buffer_full(bitmain)) {
				applog(LOG_INFO,
				       "BTM%i: Buffer full after only %d of %d work queued",
					bitmain->device_id, j, bitmain_get_work_count);
				break;
			}

			if (likely(j < bitmain->queued && !info->overheat && bitmain->works[i])) {
				if(strcmp(lastjobid, bitmain->works[i]->job_id) == 0) {
					new_block = 0;
				} else {
					applog(LOG_DEBUG, "BTM send task new block jobid %s old(%s)", bitmain->works[i]->job_id, lastjobid);
					new_block = 1;
					strcpy(lastjobid, bitmain->works[i]->job_id);
				}
				sendlen = bitmain_set_txtask((struct bitmain_txtask_token *)sendbuf, new_block, bitmain->works[i]);
				info->auto_queued++;
			} else {
				int newtime = time(NULL);
				int idle_freq = info->frequency;

				if (!info->idle++)
					idled = true;
				if (unlikely(info->overheat && opt_bitmain_auto))
					idle_freq = BITMAIN_MIN_FREQUENCY;

				if(newtime > laststatustime+10) {
					applog(LOG_DEBUG, "BTM send rxstatus lasttime(%d) newtime(%d)", laststatustime, newtime);
					sendlen = bitmain_set_rxstatus((struct bitmain_rxstatus_token *)sendbuf, 0, 1, 0, 0);
					laststatustime = newtime;
				} else {
					sendlen = 0;
				}
			}

			if(sendlen > 0) {
				ret = bitmain_send_data(sendbuf, sendlen, bitmain);
				if (unlikely(ret == BTM_SEND_ERROR)) {
					applog(LOG_ERR, "BTM%i: Comms error(buffer)",
							bitmain->device_id);
					dev_error(bitmain, REASON_DEV_COMMS_ERROR);
					info->reset = true;
					break;
				}
			}
		}

		bitmain_rotate_array(bitmain);
		pthread_cond_signal(&info->qcond);
		mutex_unlock(&info->qlock);

		if (unlikely(idled)) {
			applog(LOG_WARNING, "BTM%i: Idled %d miners",
			       bitmain->device_id, idled);
		}
	}
	return NULL;
}

static bool bitmain_prepare(struct thr_info *thr)
{
	struct cgpu_info *bitmain = thr->cgpu;
	struct bitmain_info *info = bitmain->device_data;

	free(bitmain->works);
	bitmain->works = calloc(info->miner_num * sizeof(struct work *),
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

	if (pthread_create(&info->write_thr, NULL, bitmain_send_tasks, (void *)bitmain))
		quit(1, "Failed to create bitmain write_thr");

	bitmain_init(bitmain);

	return true;
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

		if(bm->temp[i] & 0x80) {
			bm->temp[i] &= 0x7f;
			info->temp[i] = 0 - ((~bm->temp[i] & 0x7f) + 1);
		}

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

static void temp_rise(struct bitmain_info *info, int temp)
{
	if (temp >= opt_bitmain_temp + BITMAIN_TEMP_HYSTERESIS * 3) {
		info->fan_pwm = BITMAIN_PWM_MAX;
		return;
	}
	if (temp >= opt_bitmain_temp + BITMAIN_TEMP_HYSTERESIS * 2)
		info->fan_pwm += 10;
	else if (temp > opt_bitmain_temp)
		info->fan_pwm += 5;
	else if (temp >= opt_bitmain_temp - BITMAIN_TEMP_HYSTERESIS)
		info->fan_pwm += 1;
	else
		return;

	if (info->fan_pwm > opt_bitmain_fan_max)
		info->fan_pwm = opt_bitmain_fan_max;
}

static void temp_drop(struct bitmain_info *info, int temp)
{
	if (temp <= opt_bitmain_temp - BITMAIN_TEMP_HYSTERESIS * 3) {
		info->fan_pwm = opt_bitmain_fan_min;
		return;
	}
	if (temp <= opt_bitmain_temp - BITMAIN_TEMP_HYSTERESIS * 2)
		info->fan_pwm -= 10;
	else if (temp <= opt_bitmain_temp - BITMAIN_TEMP_HYSTERESIS)
		info->fan_pwm -= 5;
	else if (temp < opt_bitmain_temp)
		info->fan_pwm -= 1;

	if (info->fan_pwm < opt_bitmain_fan_min)
		info->fan_pwm = opt_bitmain_fan_min;
}

static inline void adjust_fan(struct bitmain_info *info)
{
	int temp_new;

	temp_new = info->temp_sum / info->temp_history_count;

	if (temp_new > info->temp_old)
		temp_rise(info, temp_new);
	else if (temp_new < info->temp_old)
		temp_drop(info, temp_new);
	else {
		/* temp_new == info->temp_old */
		if (temp_new > opt_bitmain_temp)
			temp_rise(info, temp_new);
		else if (temp_new < opt_bitmain_temp - BITMAIN_TEMP_HYSTERESIS)
			temp_drop(info, temp_new);
	}
	info->temp_old = temp_new;
	if (info->temp_old <= opt_bitmain_temp)
		info->optimal = true;
	else
		info->optimal = false;
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
		adjust_fan(info);
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
	int subid, slot, mc;
	struct work *work;
	bool ret = true;

	mc = info->miner_num;
	mutex_lock(&info->qlock);
	if (bitmain->queued >= mc)
		goto out_unlock;
	work = get_queued(bitmain);
	if (unlikely(!work)) {
		ret = false;
		goto out_unlock;
	}
	subid = bitmain->queued++;
	work->subid = subid;
	slot = bitmain->work_array * mc + subid;
	if (likely(bitmain->works[slot]))
		work_completed(bitmain, bitmain->works[slot]);
	bitmain->works[slot] = work;
	if (bitmain->queued < mc)
		ret = false;
out_unlock:
	mutex_unlock(&info->qlock);

	return ret;
}

static int64_t bitmain_scanhash(struct thr_info *thr)
{
	struct cgpu_info *bitmain = thr->cgpu;
	struct bitmain_info *info = bitmain->device_data;
	const int miner_num = info->miner_num;
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

	/* Wait until bitmain_send_tasks signals us that it has completed
	 * sending its work or a full nonce range timeout has occurred */
	mutex_lock(&info->qlock);
	ret = pthread_cond_timedwait(&info->qcond, &info->qlock, &abstime);
	mutex_unlock(&info->qlock);

	/* If we timed out, bitmain_send_tasks may be stuck waiting on the
	 * write_sem, so force it to check for bitmain_buffer_full itself. */
	if (ret)
		cgsem_post(&info->write_sem);

	mutex_lock(&info->lock);
	hash_count = 0xffffffffull * (uint64_t)info->nonces;
	bitmain->results += info->nonces + info->idle;
	if (bitmain->results > miner_num)
		bitmain->results = miner_num;
	if (!info->reset)
		bitmain->results--;
	info->nonces = info->idle = 0;
	mutex_unlock(&info->lock);

	/* Check for nothing but consecutive bad results or consistently less
	 * results than we should be getting and reset the FPGA if necessary */
	if (bitmain->results < -miner_num && !info->reset) {
		applog(LOG_ERR, "BTM%d: Result return rate low, resetting!",
			bitmain->device_id);
		info->reset = true;
	}

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

	mutex_lock(&info->qlock);
	/* Will overwrite any work queued */
	bitmain->queued = 0;
	pthread_cond_signal(&info->qcond);
	mutex_unlock(&info->qlock);
}

static struct api_data *bitmain_api_stats(struct cgpu_info *cgpu)
{
	struct api_data *root = NULL;
	struct bitmain_info *info = cgpu->device_data;
	int i = 0;

	root = api_add_int(root, "baud", &(info->baud), false);
	root = api_add_int(root, "miner_num", &(info->miner_num),false);
	root = api_add_int(root, "asic_num", &(info->asic_num), false);
	root = api_add_int(root, "timeout", &(info->timeout), false);
	root = api_add_int(root, "frequency", &(info->frequency), false);
	root = api_add_int(root, "voltage", &(info->frequency), false);

	root = api_add_int(root, "fan_num", &(info->fan_num), false);
	root = api_add_int(root, "fan1", &(info->fan[0]), false);
	root = api_add_int(root, "fan2", &(info->fan[1]), false);
	root = api_add_int(root, "fan3", &(info->fan[2]), false);
	root = api_add_int(root, "fan4", &(info->fan[3]), false);
	root = api_add_int(root, "fan5", &(info->fan[4]), false);

	root = api_add_int(root, "temp_num", &(info->temp_num), false);
	root = api_add_int(root, "temp1", &(info->temp[0]), false);
	root = api_add_int(root, "temp2", &(info->temp[1]), false);
	root = api_add_int(root, "temp3", &(info->temp[2]), false);
	root = api_add_int(root, "temp4", &(info->temp[3]), false);
	root = api_add_int(root, "temp5", &(info->temp[4]), false);
	root = api_add_int(root, "temp_avg", &(info->temp_avg), false);
	root = api_add_int(root, "temp_max", &(info->temp_max), false);

	root = api_add_int(root, "no_matching_work", &(info->no_matching_work), false);
	for (i = 0; i < info->miner_num; i++) {
		char mcw[24];

		sprintf(mcw, "match_work_count%d", i + 1);
		root = api_add_int(root, mcw, &(info->matching_work[i]), false);
	}

	return root;
}

static void bitmain_shutdown(struct thr_info *thr)
{
	do_bitmain_close(thr);
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
