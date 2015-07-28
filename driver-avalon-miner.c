/*
 * Copyright 2013-2014 Con Kolivas <kernel@kolivas.org>
 * Copyright 2012-2014 Xiangfu <xiangfu@openmobilefree.com>
 * Copyright 2014-2015 Mikeqin <Fengling.Qin@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 3 of the License, or (at your option)
 * any later version.  See COPYING for more details.
 */
#include <math.h>
#include "config.h"
#include "miner.h"
#include "driver-avalon-miner.h"
#include "crc.h"
#include "sha2.h"
#include "hexdump.c"

#define UNPACK32(x, str)			\
{						\
	*((str) + 3) = (uint8_t) ((x)      );	\
	*((str) + 2) = (uint8_t) ((x) >>  8);	\
	*((str) + 1) = (uint8_t) ((x) >> 16);	\
	*((str) + 0) = (uint8_t) ((x) >> 24);	\
}

#define PACK32(str, x)                        \
{                                             \
    *(x) =   ((uint32_t) *((str) + 3)      )    \
           | ((uint32_t) *((str) + 2) <<  8)    \
           | ((uint32_t) *((str) + 1) << 16)    \
           | ((uint32_t) *((str) + 0) << 24);   \
}

#define V_REF	3.3
#define R_REF	10000
#define R0	10000
#define BCOEFFICIENT	3450
#define T0	25

static uint32_t opt_avalonm_freq[3] = {AVAM_DEFAULT_FREQUENCY, AVAM_DEFAULT_FREQUENCY, AVAM_DEFAULT_FREQUENCY};
uint8_t opt_avalonm_ntime_offset = 0;
int opt_avalonm_voltage = AVAM_DEFAULT_VOLTAGE;
uint32_t opt_avalonm_spispeed = AVAM_DEFAULT_SPISPEED;
bool opt_avalonm_autof;
static uint32_t g_freq_array[][2] = {
	{100, 0x1e678447},
	{113, 0x22688447},
	{125, 0x1c470447},
	{138, 0x2a6a8447},
	{150, 0x22488447},
	{163, 0x326c8447},
	{175, 0x1a268447},
	{188, 0x1c270447},
	{200, 0x1e278447},
	{213, 0x20280447},
	{225, 0x22288447},
	{238, 0x24290447},
	{250, 0x26298447},
	{263, 0x282a0447},
	{275, 0x2a2a8447},
	{288, 0x2c2b0447},
	{300, 0x2e2b8447},
	{313, 0x302c0447},
	{325, 0x322c8447},
	{338, 0x342d0447},
	{350, 0x1a068447},
	{363, 0x382e0447},
	{375, 0x1c070447},
	{388, 0x3c2f0447},
	{400, 0x1e078447}
};

static uint16_t encode_voltage(uint32_t v)
{
	if (v == 0)
		return 0xff;

	return (((0x59 - (v - 5000) / 125) & 0xff) << 1 | 1);
}

static uint32_t decode_voltage(uint8_t v)
{
	if (v == 0xff)
		return 0;

	return (0x59 - (v >> 1)) * 125 + 5000;
}

static uint32_t decode_cpm(uint32_t cpm)
{
	int i;

	for (i = 0; i < sizeof(g_freq_array) / sizeof(g_freq_array[0]); i++) {
		if (g_freq_array[i][1] == cpm)
			return g_freq_array[i][0];
	}

	return 0;
}

static int avalonm_init_pkg(struct avalonm_pkg *pkg, uint8_t type, uint8_t idx, uint8_t cnt)
{
	unsigned short crc;

	pkg->head[0] = AVAM_H1;
	pkg->head[1] = AVAM_H2;

	pkg->type = type;
	pkg->opt = 0;
	pkg->idx = idx;
	pkg->cnt = cnt;

	crc = crc16(pkg->data, AVAM_P_DATA_LEN);

	pkg->crc[0] = (crc & 0xff00) >> 8;
	pkg->crc[1] = crc & 0x00ff;
	return 0;
}

static float convert_temp(uint32_t adc)
{
	float ret, resistance;

	if (adc >= 1023)
		return -273.15;

	resistance = (1023.0 / adc) - 1;
	resistance = R_REF / resistance;
	ret = resistance / R0;
	ret = log(ret);
	ret /= BCOEFFICIENT;
	ret += 1.0 / (T0 + 273.15);
	ret = 1.0 / ret;
	ret -= 273.15;

	return ret;
}

static float convert_voltage(uint32_t adc, float percent)
{
	float voltage;

	voltage = adc * V_REF / 1023 / percent;
	return voltage;
}

static void process_nonce(struct cgpu_info *avalonm, uint8_t *report)
{
	struct avalonm_info *info = avalonm->device_data;
	struct work *work;
	uint8_t ntime, chip_id;
	uint32_t nonce, id;

	PACK32(report, &id);
	chip_id = report[6];
	if (chip_id >= info->asic_cnts) {
		applog(LOG_DEBUG, "%s-%d: chip_id >= info->asic_cnts(%d > %d)",
				avalonm->drv->name, avalonm->device_id,
				chip_id, info->asic_cnts);
		return;
	}

	ntime = report[7];
	PACK32(report + 8, &nonce);
	nonce -= 0x4000;
	info->usbfifo_cnt = report[13];
	info->workfifo_cnt = report[14];
	info->noncefifo_cnt = report[15];

	applog(LOG_DEBUG, "%s-%d: Found! - ID: %08x CID: %02x N:%08x NR:%d",
			avalonm->drv->name, avalonm->device_id,
			id, chip_id, nonce, ntime);

	work = clone_queued_work_byid(avalonm, id);
	if (!work)
		return;

	if(!submit_noffset_nonce(info->thr, work, nonce, ntime)) {
		info->hw_work[chip_id]++;
		info->hw_work_i[chip_id][info->time_i]++;
	}

	info->matching_work[chip_id]++;
	free_work(work);
	info->nonce_cnts++;
}

static int decode_pkg(struct thr_info *thr, struct avalonm_ret *ar)
{
	struct cgpu_info *avalonm = thr->cgpu;
	struct avalonm_info *info = avalonm->device_data;
	uint32_t ret, tmp, i, freq[3];
	unsigned int expected_crc;
	unsigned int actual_crc;

	if (ar->head[0] != AVAM_H1 && ar->head[1] != AVAM_H2) {
		applog(LOG_DEBUG, "%s-%d: H1 %02x, H2 %02x",
				avalonm->drv->name, avalonm->device_id,
				ar->head[0], ar->head[1]);
		info->crcerr_cnt++;
		return 0;
	}

	expected_crc = crc16(ar->data, AVAM_P_DATA_LEN);
	actual_crc = (ar->crc[1] & 0xff) | ((ar->crc[0] & 0xff) << 8);
	if (expected_crc != actual_crc) {
		applog(LOG_DEBUG, "%s-%d: %02x: expected crc(%04x), actual_crc(%04x)",
		       avalonm->drv->name, avalonm->device_id,
		       ar->type, expected_crc, actual_crc);
		return 0;
	}

	switch(ar->type) {
	case AVAM_P_NONCE_M:
		ret = ar->type;
		applog(LOG_DEBUG, "%s-%d: AVAM_P_NONCE", avalonm->drv->name, avalonm->device_id);
		hexdump(ar->data, 32);
		process_nonce(avalonm, ar->data);
		if (ar->data[22] != 0xff) {
			process_nonce(avalonm, ar->data + 16);
		}
		break;
	case AVAM_P_STATUS_M:
		ret = ar->type;
		applog(LOG_DEBUG, "%s-%d: AVAM_P_STATUS_M", avalonm->drv->name, avalonm->device_id);
		hexdump(ar->data, 32);
		memcpy(&tmp, ar->data, 4);
		if (!strncmp(info->ver, "3U", 2))
			info->get_frequency[0][0] = be32toh(tmp);
		else
			info->spi_speed = be32toh(tmp);
		memcpy(&tmp, ar->data + 4, 4);
		info->led_status = be32toh(tmp);
		memcpy(&tmp, ar->data + 8, 4);
		info->fan_pwm = be32toh(tmp);
		memcpy(&tmp, ar->data + 12, 4);
		if (!strncmp(info->ver, "3U", 2))
			info->get_voltage = convert_voltage(be32toh(tmp), 0.5);
		else
			info->get_voltage = decode_voltage((uint8_t)be32toh(tmp));
		memcpy(&tmp, ar->data + 16, 4);
		info->adc[0] = be32toh(tmp);
		memcpy(&tmp, ar->data + 20, 4);
		info->adc[1] = be32toh(tmp);
		memcpy(&tmp, ar->data + 24, 4);
		info->adc[2] = be32toh(tmp);
		memcpy(&tmp, ar->data + 28 , 4);
		info->power_good = be32toh(tmp);

		/* power off notice */
		if (!info->get_voltage) {
			usb_buffer_clear(avalonm);
			applog(LOG_NOTICE, "%s-%d: AVAM_P_STATUS_M Power off notice", avalonm->drv->name, avalonm->device_id);
			info->power_on = 1;
			memset(info->set_frequency, 0, sizeof(uint32_t) * info->asic_cnts * 3);
			for (i = 1; i <= info->asic_cnts; i++)
				FLAG_SET(info->freq_set, i);
		}
		break;
	case AVAM_P_STATUS_FREQ:
		applog(LOG_DEBUG, "%s-%d: AVAM_P_STATUS_FREQ", avalonm->drv->name, avalonm->device_id);
		memcpy(&tmp, ar->data, 4);
		tmp = be32toh(tmp);
		freq[0] = decode_cpm(tmp);
		memcpy(&tmp, ar->data + 4, 4);
		tmp = be32toh(tmp);
		freq[1] = decode_cpm(tmp);
		memcpy(&tmp, ar->data + 8, 4);
		tmp = be32toh(tmp);
		freq[2] = decode_cpm(tmp);

		if (!ar->opt) {
			for (i = 0; i < info->asic_cnts; i++) {
				info->get_frequency[i][0] = freq[0];
				info->get_frequency[i][1] = freq[1];
				info->get_frequency[i][2] = freq[2];
			}
		}

		if (ar->opt) {
			info->get_frequency[ar->opt - 1][0] = freq[0];
			info->get_frequency[ar->opt - 1][1] = freq[1];
			info->get_frequency[ar->opt - 1][2] = freq[2];
		}
		break;
	default:
		applog(LOG_DEBUG, "%s-%d: Unknown response (%x)", avalonm->drv->name, avalonm->device_id,
				ar->type);
		ret = 0;
		break;
	}

	return ret;
}

static int avalonm_send_pkg(struct cgpu_info *avalonm, const struct avalonm_pkg *pkg)
{
	int err = -1;
	int writecnt;

	if (unlikely(avalonm->usbinfo.nodev))
		return -1;

	err = usb_write(avalonm, (char *)pkg, AVAM_P_COUNT, &writecnt, C_AVAM_WRITE);
	if (err || writecnt != AVAM_P_COUNT) {
		applog(LOG_DEBUG, "%s-%d: avalonm_send_pkg %d, w(%d-%d)!", avalonm->drv->name, avalonm->device_id, err, AVAM_P_COUNT, writecnt);
		return -1;
	}

	return writecnt;
}

static int avalonm_receive_pkg(struct cgpu_info *avalonm, struct avalonm_ret *ret)
{
	int err = -1;
	int readcnt;

	if (unlikely(avalonm->usbinfo.nodev))
		return -1;

	err = usb_read(avalonm, (char*)ret, AVAM_P_COUNT, &readcnt, C_AVAM_READ);
	if (err || readcnt != AVAM_P_COUNT) {
		applog(LOG_DEBUG, "%s-%d: avalonm_receive_pkg %d, w(%d-%d)!", avalonm->drv->name, avalonm->device_id, err, AVAM_P_COUNT, readcnt);
		return -1;
	}

	return readcnt;
}

static int avalonm_xfer_pkg(struct cgpu_info *avalonm, const struct avalonm_pkg *pkg, struct avalonm_ret *ret)
{
	if (sizeof(struct avalonm_pkg) != avalonm_send_pkg(avalonm, pkg))
		return AVAM_SEND_ERROR;

	if (sizeof(struct avalonm_ret) != avalonm_receive_pkg(avalonm, ret))
		return AVAM_SEND_ERROR;

	return AVAM_SEND_OK;
}

static int avalonm_get_frequency(struct cgpu_info *avalonm, uint8_t asic_index)
{
	struct avalonm_info *info = avalonm->device_data;
	struct thr_info *thr = info->thr;
	struct avalonm_pkg send_pkg;
	struct avalonm_ret ar;
	int ret = 0;

	memset(send_pkg.data, 0, AVAM_P_DATA_LEN);
	avalonm_init_pkg(&send_pkg, AVAM_P_GET_FREQ, 1, 1);
	send_pkg.opt = asic_index;
	ret = avalonm_xfer_pkg(avalonm, &send_pkg, &ar);
	if (ret == AVAM_SEND_OK) {
		ret = decode_pkg(thr, &ar);
	}

	return ret;
}

static void avalonm_set_spispeed(struct cgpu_info *avalonm, uint32_t speed)
{
	struct avalonm_pkg send_pkg;
	int tmp;

	memset(send_pkg.data, 0, AVAM_P_DATA_LEN);
	tmp = speed | 0x80000000;
	tmp = be32toh(tmp);
	memcpy(send_pkg.data, &tmp, 4);
	avalonm_init_pkg(&send_pkg, AVAM_P_SETM, 1, 1);
	avalonm_send_pkg(avalonm, &send_pkg);
}

static struct cgpu_info *avalonm_detect_one(struct libusb_device *dev, struct usb_find_devices *found)
{
	struct cgpu_info *avalonm = usb_alloc_cgpu(&avalonm_drv, 1);
	struct avalonm_info *info;
	struct avalonm_pkg send_pkg;
	struct avalonm_ret ar;
	int ret, i;

	if (!usb_init(avalonm, dev, found)) {
		applog(LOG_ERR, "Avalonm failed usb_init");
		avalonm = usb_free_cgpu(avalonm);
		return NULL;
	}

	usb_buffer_clear(avalonm);
	update_usb_stats(avalonm);

	/* Cleanup the usb fifo */
	while (avalonm_receive_pkg(avalonm, &ar) != -1);

	/* We have an Avalonm connected */
	avalonm->threads = 1;
	memset(send_pkg.data, 0, AVAM_P_DATA_LEN);
	avalonm_init_pkg(&send_pkg, AVAM_P_DETECT, 1, 1);
	ret = avalonm_xfer_pkg(avalonm, &send_pkg, &ar);
	if ((ret != AVAM_SEND_OK) || (ar.type != AVAM_P_ACKDETECT)) {
		applog(LOG_DEBUG, "%s-%d: Failed to detect Avalon miner", avalonm->drv->name, avalonm->device_id);
		return NULL;
	}

	add_cgpu(avalonm);

	applog(LOG_DEBUG, "%s-%d: Found at %s", avalonm->drv->name, avalonm->device_id,
	       avalonm->device_path);

	avalonm->device_data = cgcalloc(sizeof(struct avalonm_info), 1);
	info = avalonm->device_data;
	info->thr = NULL;
	memcpy(info->dna, ar.data, AVAM_MM_DNA_LEN);
	memcpy(info->ver, ar.data + AVAM_MM_DNA_LEN, AVAM_MM_VER_LEN);
	info->ver[AVAM_MM_VER_LEN] = '\0';
	memcpy(&info->asic_cnts, ar.data + AVAM_MM_DNA_LEN + AVAM_MM_VER_LEN, 4);
	info->asic_cnts = be32toh(info->asic_cnts);
	if (opt_avalonm_ntime_offset >= info->asic_cnts)
		quit(1, "%s-%d: invalid opt_avalonm_ntime_offset, should 0-%d", avalonm->drv->name, avalonm->device_id, info->asic_cnts - 1);

	memset(info->set_frequency, 0, sizeof(uint32_t) * info->asic_cnts * 3);
	memset(info->get_frequency, 0, sizeof(uint32_t) * info->asic_cnts * 3);
	for (i = 0; i < info->asic_cnts; i++) {
		info->opt_freq[i][0] = opt_avalonm_freq[0];
		info->opt_freq[i][1] = opt_avalonm_freq[1];
		info->opt_freq[i][2] = opt_avalonm_freq[2];
	}
	info->set_voltage = 0;
	info->opt_voltage = opt_avalonm_voltage;
	info->nonce_cnts = 0;
	info->usbfifo_cnt = 0;
	info->workfifo_cnt = 0;
	info->noncefifo_cnt = 0;
	info->crcerr_cnt = 0;
	info->power_good = 0;
	info->spi_speed = 0;
	info->led_status = 0;
	info->fan_pwm = 0;
	info->get_voltage = 0;
	info->freq_update = 0;
	info->freq_set = 0;
	FLAG_SET(info->freq_set, AVAM_ASIC_ALL);
	memset(info->hw_work, 0, sizeof(int) * info->asic_cnts);
	memset(info->matching_work, 0, sizeof(uint64_t) * info->asic_cnts);
	info->adc[0] = info->adc[1] = info->adc[2] = 0;

	avalonm_set_spispeed(avalonm, opt_avalonm_spispeed);
	cgtime(&info->elapsed);
	info->lastadj = info->lasttime = info->elapsed;
	info->time_i = 0;
	memset(info->hw_work_i, 0, sizeof(int) * info->asic_cnts * AVAM_DEFAULT_MOV_TIMES);
	return avalonm;
}

static uint32_t avalonm_get_cpm(uint32_t freq)
{
	int i;

	for (i = 0; i < (sizeof(g_freq_array) / sizeof(g_freq_array[0]) - 1); i++) {
		if (freq >= g_freq_array[i][0] && freq < g_freq_array[i+1][0])
			return g_freq_array[i][1];
	}

	/* check if the final freq match */
	if (freq == g_freq_array[i][0])
		return g_freq_array[i][1];

	/* return the lowest freq if not found */
	return g_freq_array[0][1];
}

static void avalonm_set_freq(struct cgpu_info *avalonm, uint8_t asic_index, uint32_t freq[])
{
	struct avalonm_info *info = avalonm->device_data;
	struct avalonm_pkg send_pkg;
	uint32_t tmp, i;
	uint8_t index, change = 0;
	uint32_t max_freq = 0;

	if (asic_index == AVAM_ASIC_ALL) {
		index = 0;
		for (i = 0; i < info->asic_cnts; i++) {
			if ((info->set_frequency[i][0] == freq[0]) &&
				(info->set_frequency[i][1] == freq[1]) &&
				(info->set_frequency[i][2] == freq[2]))
				continue;

			change = 1;
			info->set_frequency[i][0] = freq[0];
			info->set_frequency[i][1] = freq[1];
			info->set_frequency[i][2] = freq[2];
			FLAG_SET(info->freq_update, AVAM_ASIC_ALL);
		}
	}

	if (asic_index != AVAM_ASIC_ALL) {
		index = asic_index - 1;
		if (!((info->set_frequency[index][0] == freq[0]) &&
				(info->set_frequency[index][1] == freq[1]) &&
				(info->set_frequency[index][2] == freq[2]))) {
			change = 1;
			info->set_frequency[index][0] = freq[0];
			info->set_frequency[index][1] = freq[1];
			info->set_frequency[index][2] = freq[2];
			FLAG_SET(info->freq_update, asic_index);
		}
	}

	if (!change)
		return;

	for (i = 0; i < info->asic_cnts; i++) {
		if (max_freq < info->set_frequency[i][0])
			max_freq = info->set_frequency[i][0];

		if (max_freq < info->set_frequency[i][1])
			max_freq = info->set_frequency[i][1];

		if (max_freq < info->set_frequency[i][2])
			max_freq = info->set_frequency[i][2];
	}

	info->delay_ms = CAL_DELAY(max_freq);

	memset(send_pkg.data, 0, AVAM_P_DATA_LEN);
	tmp = avalonm_get_cpm(freq[0]);
	tmp = be32toh(tmp);
	memcpy(send_pkg.data, &tmp, 4);
	tmp = avalonm_get_cpm(freq[1]);
	tmp = be32toh(tmp);
	memcpy(send_pkg.data + 4, &tmp, 4);
	tmp = avalonm_get_cpm(freq[2]);
	tmp = be32toh(tmp);
	memcpy(send_pkg.data + 8, &tmp, 4);

	avalonm_init_pkg(&send_pkg, AVAM_P_SET_FREQ, 1, 1);
	send_pkg.opt = asic_index;
	avalonm_send_pkg(avalonm, &send_pkg);
	applog(LOG_NOTICE, "%s-%d: Avalonm set asic_index %d freq %d,%d,%d",
			avalonm->drv->name, avalonm->device_id,
			asic_index,
			freq[0],
			freq[1],
			freq[2]);
}

static void avalonm_set_voltage(struct cgpu_info *avalonm)
{
	struct avalonm_info *info = avalonm->device_data;
	struct avalonm_pkg send_pkg;
	uint16_t tmp;

	if (!info->power_on && (info->set_voltage == info->opt_voltage))
		return;

	info->set_voltage = info->opt_voltage;
	memset(send_pkg.data, 0, AVAM_P_DATA_LEN);
	/* Use shifter to set voltage */
	tmp = info->set_voltage;
	tmp = encode_voltage(tmp);
	tmp = htobe16(tmp);
	memcpy(send_pkg.data, &tmp, 2);

	/* Package the data */
	avalonm_init_pkg(&send_pkg, AVAM_P_SET_VOLT, 1, 1);
	avalonm_send_pkg(avalonm, &send_pkg);
	applog(LOG_NOTICE, "%s-%d: Avalonm set volt %d",
	       avalonm->drv->name, avalonm->device_id,
	       info->set_voltage);

	if (info->power_on)
		cgsleep_ms(1000);

	info->power_on = 0;
}

static inline void avalonm_detect(bool __maybe_unused hotplug)
{
	usb_detect(&avalonm_drv, avalonm_detect_one);
}

static int avalonm_get_reports(void *userdata)
{
	struct cgpu_info *avalonm = (struct cgpu_info *)userdata;
	struct avalonm_info *info = avalonm->device_data;
	struct thr_info *thr = info->thr;
	struct avalonm_pkg send_pkg;
	struct avalonm_ret ar;
	int ret = 0;

	memset(send_pkg.data, 0, AVAM_P_DATA_LEN);
	avalonm_init_pkg(&send_pkg, AVAM_P_POLLING, 1, 1);
	ret = avalonm_xfer_pkg(avalonm, &send_pkg, &ar);
	if (ret == AVAM_SEND_OK) {
		ret = decode_pkg(thr, &ar);
	}

	return ret;
}

static void rev(unsigned char *s, size_t l)
{
	size_t i, j;
	unsigned char t;

	for (i = 0, j = l - 1; i < j; i++, j--) {
		t = s[i];
		s[i] = s[j];
		s[j] = t;
	}
}

static int64_t avalonm_scanhash(struct thr_info *thr)
{
	struct cgpu_info *avalonm = thr->cgpu;
	struct avalonm_info *info = avalonm->device_data;
	int64_t hash_count, ms_timeout;
	struct timeval current;
	double device_tdiff;
	uint32_t i, j, tmp;

	/* Half nonce range */
	ms_timeout = 0x80000000ll / 1000;

	/* Wait until avalon_send_tasks signals us that it has completed
	 * sending its work or a full nonce range timeout has occurred. We use
	 * cgsems to never miss a wakeup. */
	cgsem_mswait(&info->qsem, ms_timeout);

	hash_count = info->nonce_cnts++;
	info->nonce_cnts = 0;

	cgtime(&current);
	device_tdiff = tdiff(&current, &(info->lastadj));
	if (opt_avalonm_autof && (device_tdiff > AVAM_DEFAULT_ADJ_INTERVAL || device_tdiff < 0)) {
		copy_time(&info->lastadj, &current);

		for (i = 0; i < AVAM_DEFAULT_ASIC_COUNT; i++) {
			tmp = 0;
			for (j = 0; j < AVAM_DEFAULT_MOV_TIMES; j++)
				tmp += info->hw_work_i[i][j];

			if (tmp > AVAM_HW_HIGH && (info->opt_freq[i][0] > opt_avalonm_freq[0] - (uint32_t)(12 * 12.5))) {
				if ((info->opt_freq[i][0] == AVAM_DEFAULT_FREQUENCY_MIN) &&
						(info->opt_freq[i][1] == AVAM_DEFAULT_FREQUENCY_MIN) &&
						(info->opt_freq[i][2] == AVAM_DEFAULT_FREQUENCY_MIN))
					continue;

				if (info->opt_freq[i][0] * 10 % 125)
					info->opt_freq[i][0] -= 13;
				else
					info->opt_freq[i][0] -= 12;

				if (info->opt_freq[i][1] * 10 % 125)
					info->opt_freq[i][1] -= 13;
				else
					info->opt_freq[i][1] -= 12;

				if (info->opt_freq[i][2] * 10 % 125)
					info->opt_freq[i][2] -= 13;
				else
					info->opt_freq[i][2] -= 12;

				if (info->opt_freq[i][0] < AVAM_DEFAULT_FREQUENCY_MIN)
					info->opt_freq[i][0] = AVAM_DEFAULT_FREQUENCY_MIN;

				if (info->opt_freq[i][1] < AVAM_DEFAULT_FREQUENCY_MIN)
					info->opt_freq[i][1] = AVAM_DEFAULT_FREQUENCY_MIN;

				if (info->opt_freq[i][2] < AVAM_DEFAULT_FREQUENCY_MIN)
					info->opt_freq[i][2] = AVAM_DEFAULT_FREQUENCY_MIN;

				FLAG_SET(info->freq_set, i + 1);
				applog(LOG_NOTICE, "%s-%d: Automatic decrease [%d] freq to %d,%d,%d",
						avalonm->drv->name, avalonm->device_id, i,
						info->opt_freq[i][0],
						info->opt_freq[i][1],
						info->opt_freq[i][2]);
			}

			if (tmp < AVAM_HW_LOW && (info->opt_freq[i][0] < opt_avalonm_freq[0] + (uint32_t)(8 * 12.5))) {
				if ((info->opt_freq[i][0] == AVAM_DEFAULT_FREQUENCY_MAX) &&
						(info->opt_freq[i][1] == AVAM_DEFAULT_FREQUENCY_MAX) &&
						(info->opt_freq[i][2] == AVAM_DEFAULT_FREQUENCY_MAX))
					continue;

				if (info->opt_freq[i][0] * 10 % 125)
					info->opt_freq[i][0] += 12;
				else
					info->opt_freq[i][0] += 13;

				if (info->opt_freq[i][1] * 10 % 125)
					info->opt_freq[i][1] += 12;
				else
					info->opt_freq[i][1] += 13;

				if (info->opt_freq[i][2] * 10 % 125)
					info->opt_freq[i][2] += 12;
				else
					info->opt_freq[i][2] += 13;

				if (info->opt_freq[i][0] > AVAM_DEFAULT_FREQUENCY_MAX)
					info->opt_freq[i][0] = AVAM_DEFAULT_FREQUENCY_MAX;

				if (info->opt_freq[i][1] > AVAM_DEFAULT_FREQUENCY_MAX)
					info->opt_freq[i][1] = AVAM_DEFAULT_FREQUENCY_MAX;

				if (info->opt_freq[i][2] > AVAM_DEFAULT_FREQUENCY_MAX)
					info->opt_freq[i][2] = AVAM_DEFAULT_FREQUENCY_MAX;

				FLAG_SET(info->freq_set, i + 1);
				applog(LOG_NOTICE, "%s-%d: Automatic increase [%d] freq to %d,%d,%d",
						avalonm->drv->name, avalonm->device_id, i,
						info->opt_freq[i][0],
						info->opt_freq[i][1],
						info->opt_freq[i][2]);
			}
		}
	}

	return hash_count * 0xffffffffull;
}

static void avalonm_rotate_array(struct cgpu_info *avalonm, struct avalonm_info *info)
{
	mutex_lock(&info->qlock);
	avalonm->queued = 0;
	if (++avalonm->work_array >= AVAM_DEFAULT_ARRAY_SIZE)
		avalonm->work_array = 0;
	mutex_unlock(&info->qlock);
}

static void *avalonm_process_tasks(void *userdata)
{
	char threadname[16];

	struct cgpu_info *avalonm = userdata;
	struct avalonm_info *info = avalonm->device_data;
	struct work *work;
	struct avalonm_pkg send_pkg;

	int start_count, end_count, i, j, k, ret;
	int avalon_get_work_count = info->asic_cnts;
	struct timeval current;
	double device_tdiff;

	snprintf(threadname, sizeof(threadname), "%d/AvmProc", avalonm->device_id);
	RenameThread(threadname);

	while (likely(!avalonm->shutdown)) {
		if (unlikely(avalonm->usbinfo.nodev)) {
			applog(LOG_ERR, "%s-%d: Device disappeared, shutting down thread",
			       avalonm->drv->name, avalonm->device_id);
			goto out;
		}

		cgtime(&current);
		device_tdiff = tdiff(&current, &(info->lasttime));
		if (device_tdiff >= AVAM_DEFAULT_MOV_TIMES || device_tdiff < 0) {
			copy_time(&info->lasttime, &current);
			if (info->time_i++ >= AVAM_DEFAULT_MOV_TIMES)
				info->time_i = 0;

			for(i = 0; i < AVAM_DEFAULT_ASIC_COUNT; i++)
				info->hw_work_i[i][info->time_i] = 0;
		}

		/* Give other threads a chance to acquire qlock. */
		i = 0;
		do {
			cgsleep_ms(40);
		} while (!avalonm->shutdown && avalonm->queued < avalon_get_work_count);

		mutex_lock(&info->qlock);

		start_count = avalonm->work_array * avalon_get_work_count;
		end_count = start_count + avalon_get_work_count;

		for (i = start_count, j = 0; i < end_count; i++, j++) {
			work = avalonm->works[i];
			if (likely(j < avalonm->queued && avalonm->works[i] && j < (info->asic_cnts - opt_avalonm_ntime_offset))) {
				/* Configuration */
				avalonm_set_voltage(avalonm);

				if (FLAG_GET(info->freq_set, 0)) {
					avalonm_set_freq(avalonm, AVAM_ASIC_ALL, info->opt_freq[0]);
					FLAG_CLEAR(info->freq_set, 0);
					cgsleep_ms(20);
				}

				for (k = 1; k <= info->asic_cnts; k++) {
					if (FLAG_GET(info->freq_set, k)) {
						avalonm_set_freq(avalonm, k, info->opt_freq[k - 1]);
						FLAG_CLEAR(info->freq_set, k);
						cgsleep_ms(20);
					}
				}

				/* P_WORK part 1: midstate */
				memcpy(send_pkg.data, work->midstate, AVAM_P_DATA_LEN);
				rev((void *)(send_pkg.data), AVAM_P_DATA_LEN);
				avalonm_init_pkg(&send_pkg, AVAM_P_WORK, 1, 2);
				hexdump(send_pkg.data, 32);
				avalonm_send_pkg(avalonm, &send_pkg);
				if (info->freq_update)
					cgsleep_ms(300);

				/* P_WORK part 2:
				 * id(6)+reserved(2)+ntime(1)+fan(3)+led(4)+reserved(4)+data(12) */
				memset(send_pkg.data, 0, AVAM_P_DATA_LEN);

				UNPACK32(work->id, send_pkg.data);

				/* always roll work 0 */
				if (j == 0)
					send_pkg.data[8] = opt_avalonm_ntime_offset;
				else
					send_pkg.data[8] = 0;

				/* TODO led */
				UNPACK32(0, send_pkg.data + 12);

				memcpy(send_pkg.data + 20, work->data + 64, 12);
				rev((void *)(send_pkg.data + 20), 12);
				avalonm_init_pkg(&send_pkg, AVAM_P_WORK, 2, 2);
				hexdump(send_pkg.data, 32);
				avalonm_send_pkg(avalonm, &send_pkg);
				if (info->freq_update)
					cgsleep_ms(300);
			}
		}

		mutex_unlock(&info->qlock);
		avalonm_rotate_array(avalonm, info);

		cgsem_post(&info->qsem);

		/* little delay, let asics process more job */
		if (!strncmp(info->ver, "3U", 2))
			cgsleep_ms(400);

		/* Get result */
		do {
			ret = avalonm_get_reports(avalonm);
			cgsleep_ms(5);
		} while (ret != AVAM_P_STATUS_M);

		if (info->freq_update) {
			applog(LOG_NOTICE, "%s-%d: avalonm_process_tasks freq change flag %02x",
					avalonm->drv->name, avalonm->device_id,
					info->freq_update);
			if (FLAG_GET(info->freq_update, 0)) {
				avalonm_get_frequency(avalonm, 0);
				FLAG_CLEAR(info->freq_update, 0);
			}

			for (i = 1; i <= info->asic_cnts; i++) {
				if (FLAG_GET(info->freq_update, i)) {
					avalonm_get_frequency(avalonm, i);
					FLAG_CLEAR(info->freq_update, i);
				}
			}
		}

		cgsleep_ms(info->delay_ms);
	}

out:
	return NULL;
}

static bool avalonm_prepare(struct thr_info *thr)
{
	struct cgpu_info *avalonm = thr->cgpu;
	struct avalonm_info *info = avalonm->device_data;

	free(avalonm->works);
	avalonm->works = calloc(info->asic_cnts * sizeof(struct work *),
			       AVAM_DEFAULT_ARRAY_SIZE);
	if (!avalonm->works)
		quit(1, "Failed to calloc avalon miner works in avalonm_prepare");

	info->thr = thr;
	info->delay_ms = CAL_DELAY(AVAM_DEFAULT_FREQUENCY);
	info->power_on = 1;

	mutex_init(&info->lock);
	mutex_init(&info->qlock);
	cgsem_init(&info->qsem);

	if (pthread_create(&info->process_thr, NULL, avalonm_process_tasks, (void *)avalonm))
		quit(1, "Failed to create avalonm process_thr");

	return true;
}

static void avalonm_shutdown(struct thr_info *thr)
{
	struct cgpu_info *avalonm = thr->cgpu;
	struct avalonm_info *info = avalonm->device_data;

	pthread_join(info->process_thr, NULL);

	cgsem_destroy(&info->qsem);
	mutex_destroy(&info->qlock);
	mutex_destroy(&info->lock);
	free(avalonm->works);
	avalonm->works = NULL;
}

char *set_avalonm_freq(char *arg)
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
		if (val1 < AVAM_DEFAULT_FREQUENCY_MIN || val1 > AVAM_DEFAULT_FREQUENCY_MAX)
			return "Invalid value1 passed to set_avalonm_freq";
	}

	if (colon1 && *colon1) {
		colon2 = strchr(colon1, ':');
		if (colon2)
			*(colon2++) = '\0';

		if (*colon1) {
			val2 = atoi(colon1);
			if (val2 < AVAM_DEFAULT_FREQUENCY_MIN || val2 > AVAM_DEFAULT_FREQUENCY_MAX)
				return "Invalid value2 passed to set_avalonm_freq";
		}

		if (colon2 && *colon2) {
			val3 = atoi(colon2);
			if (val3 < AVAM_DEFAULT_FREQUENCY_MIN || val3 > AVAM_DEFAULT_FREQUENCY_MAX)
				return "Invalid value3 passed to set_avalonm_freq";
		}
	}

	if (!val1)
		val3 = val2 = val1 = AVAM_DEFAULT_FREQUENCY;

	if (!val2)
		val3 = val2 = val1;

	if (!val3)
		val3 = val2;

	opt_avalonm_freq[0] = val1;
	opt_avalonm_freq[1] = val2;
	opt_avalonm_freq[2] = val3;
	applog(LOG_NOTICE, "Update all asic frequency to %d",
			(opt_avalonm_freq[0] * 4 + opt_avalonm_freq[1] * 4 + opt_avalonm_freq[2]) / 9);

	return NULL;
}

char *set_avalonm_device_freq(struct cgpu_info *avalonm, char *arg)
{
	struct avalonm_info *info = avalonm->device_data;
	char *colon1, *colon2;
	int val1 = 0, val2 = 0, val3 = 0;
	int asic_index = AVAM_ASIC_ALL;
	uint8_t i;

	if (!(*arg))
		return NULL;

	colon1 = strchr(arg, '-');
	if (colon1) {
		sscanf(arg, "%d-", &asic_index);
		arg = colon1 + 1;
		if (asic_index < 0 || asic_index > info->asic_cnts) {
			applog(LOG_ERR, "invalid asic index: %d, valid range 0-%d", asic_index, info->asic_cnts);
			return "Invalid asic index to set_avalonm_freq";
		}
	}

	colon1 = strchr(arg, ':');
	if (colon1)
		*(colon1++) = '\0';

	if (*arg) {
		val1 = atoi(arg);
		if (val1 < AVAM_DEFAULT_FREQUENCY_MIN || val1 > AVAM_DEFAULT_FREQUENCY_MAX)
			return "Invalid value1 passed to set_avalonm_freq";
	}

	if (colon1 && *colon1) {
		colon2 = strchr(colon1, ':');
		if (colon2)
			*(colon2++) = '\0';

		if (*colon1) {
			val2 = atoi(colon1);
			if (val2 < AVAM_DEFAULT_FREQUENCY_MIN || val2 > AVAM_DEFAULT_FREQUENCY_MAX)
				return "Invalid value2 passed to set_avalonm_freq";
		}

		if (colon2 && *colon2) {
			val3 = atoi(colon2);
			if (val3 < AVAM_DEFAULT_FREQUENCY_MIN || val3 > AVAM_DEFAULT_FREQUENCY_MAX)
				return "Invalid value3 passed to set_avalonm_freq";
		}
	}

	if (!val1)
		val3 = val2 = val1 = AVAM_DEFAULT_FREQUENCY;

	if (!val2)
		val3 = val2 = val1;

	if (!val3)
		val3 = val2;

	if (!asic_index) {
		for (i = 0; i < info->asic_cnts; i++) {
			info->opt_freq[i][0] = val1;
			info->opt_freq[i][1] = val2;
			info->opt_freq[i][2] = val3;
		}
		FLAG_SET(info->freq_set, AVAM_ASIC_ALL);
		applog(LOG_NOTICE, "Update all asic frequency to %d",
				(val1 * 4 + val2 * 4 + val3) / 9);
	}

	if (asic_index) {
		info->opt_freq[asic_index - 1][0] = val1;
		info->opt_freq[asic_index - 1][1] = val2;
		info->opt_freq[asic_index - 1][2] = val3;

		FLAG_SET(info->freq_set, asic_index);
		applog(LOG_NOTICE, "Update asic %d frequency to %d",
				asic_index - 1,
				(val1 * 4 + val2 * 4 + val3) / 9);
	}

	return NULL;
}

char *set_avalonm_voltage(char *arg)
{
	int val, ret;

	ret = sscanf(arg, "%d", &val);
	if (ret < 1)
		return "No values passed to avalonm-voltage";

	if (val < AVAM_DEFAULT_VOLTAGE_MIN || val > AVAM_DEFAULT_VOLTAGE_MAX)
		return "Invalid value passed to avalonm-voltage";

	opt_avalonm_voltage = val;

	return NULL;
}

char *set_avalonm_device_voltage(struct cgpu_info *avalonm, char *arg)
{
	struct avalonm_info *info = avalonm->device_data;
	int val, ret;

	ret = sscanf(arg, "%d", &val);
	if (ret < 1)
		return "No values passed to avalonm-voltage";

	if (val < AVAM_DEFAULT_VOLTAGE_MIN || val > AVAM_DEFAULT_VOLTAGE_MAX)
		return "Invalid value passed to avalonm-voltage";

	info->opt_voltage = val;
	applog(LOG_NOTICE, "%s-%d: Update voltage to %d",
			avalonm->drv->name, avalonm->device_id, info->opt_voltage);

	return NULL;
}

static char *avalonm_set_device(struct cgpu_info *avalonm, char *option, char *setting, char *replybuf)
{
	if (strcasecmp(option, "help") == 0) {
		sprintf(replybuf, "frequency|voltage");
		return replybuf;
	}

	if (strcasecmp(option, "frequency") == 0) {
		if (!setting || !*setting) {
			sprintf(replybuf, "missing frequency value");
			return replybuf;
		}

		if (set_avalonm_device_freq(avalonm, setting)) {
			sprintf(replybuf, "invalid frequency value, valid range %d-%d",
				AVAM_DEFAULT_FREQUENCY_MIN, AVAM_DEFAULT_FREQUENCY_MAX);
			return replybuf;
		}

		return NULL;
	}

	if (strcasecmp(option, "voltage") == 0) {
		if (!setting || !*setting) {
			sprintf(replybuf, "missing voltage value");
			return replybuf;
		}

		if (set_avalonm_device_voltage(avalonm, setting)) {
			sprintf(replybuf, "invalid voltage value, valid range %d-%d",
					AVAM_DEFAULT_VOLTAGE_MIN, AVAM_DEFAULT_VOLTAGE_MAX);
			return replybuf;
		}

		return NULL;
	}

	sprintf(replybuf, "Unknown option: %s", option);
	return replybuf;
}

#define STATBUFLEN 512
static struct api_data *avalonm_api_stats(struct cgpu_info *cgpu)
{
	struct api_data *root = NULL;
	struct avalonm_info *info = cgpu->device_data;
	char buf[256];
	char statbuf[STATBUFLEN];
	uint32_t i, j, tmp;
	struct timeval now;

	memset(statbuf, 0, STATBUFLEN);

	sprintf(buf, "VER[%s]", info->ver);
	strcat(statbuf, buf);

	sprintf(buf, " DNA[%02x%02x%02x%02x%02x%02x%02x%02x]",
				info->dna[0],
				info->dna[1],
				info->dna[2],
				info->dna[3],
				info->dna[4],
				info->dna[5],
				info->dna[6],
				info->dna[7]);
	strcat(statbuf, buf);

	cgtime(&now);
	sprintf(buf, " Elapsed[%.0f]", tdiff(&now, &(info->elapsed)));
	strcat(statbuf, buf);

	sprintf(buf, " Chips[%d]", info->asic_cnts);
	strcat(statbuf, buf);

	sprintf(buf, " Crc[%d]", info->crcerr_cnt);
	strcat(statbuf, buf);

	sprintf(buf, " Speed[%d]", info->spi_speed);
	strcat(statbuf, buf);

	if (!strncmp(info->ver, "3U", 2))
		sprintf(buf, " Vol[%.2f]", (float)info->get_voltage);
	else
		sprintf(buf, " Vol[%.4f]", (float)info->get_voltage / 10000);
	strcat(statbuf, buf);

	if (!strncmp(info->ver, "3U", 2))
		sprintf(buf, " V_CORE[%.2f]", convert_voltage(info->adc[0], 1));
	else
		sprintf(buf, " V12[%.2f]", convert_voltage(info->adc[0], (10 / 110)));
	strcat(statbuf, buf);

	if (!strncmp(info->ver, "3U", 2))
		sprintf(buf, " T[%d]", info->adc[1]);
	else
		sprintf(buf, " TC[%.2f]", convert_temp(info->adc[1]));
	strcat(statbuf, buf);

	if (!strncmp(info->ver, "3U", 2)) {
		sprintf(buf, " V0_9[%.2f]", convert_voltage(info->adc[2] & 0xffff, 1));
		strcat(statbuf, buf);
		sprintf(buf, " V1_8[%.2f]", convert_voltage((info->adc[2] >> 16) & 0xffff, 1));
	} else
		sprintf(buf, " TF[%.2f]", convert_temp(info->adc[2]));
	strcat(statbuf, buf);

	if (!strncmp(info->ver, "3U", 2)) {
		sprintf(buf, " Freq[%d]", info->get_frequency[0][0]);
		strcat(statbuf, buf);
	} else {
		strcat(statbuf, " Freq[");
		for (i = 0; i < info->asic_cnts; i++) {
			sprintf(buf, "%d %d %d ",
					info->get_frequency[i][0],
					info->get_frequency[i][1],
					info->get_frequency[i][2]);
			strcat(statbuf, buf);
		}
		statbuf[strlen(statbuf) - 1] = ']';
	}

	strcat(statbuf, " HW[");
	for (i = 0; i < info->asic_cnts; i++) {
		sprintf(buf, "%d ", info->hw_work[i]);
		strcat(statbuf, buf);
	}
	statbuf[strlen(statbuf) - 1] = ']';

	/* simple moving the sum of hardware error */
	strcat(statbuf, " SHW[");
	for (i = 0; i < info->asic_cnts; i++) {
		tmp = 0;
		for (j = 0; j < AVAM_DEFAULT_MOV_TIMES; j++)
			tmp += info->hw_work_i[i][j];

		sprintf(buf, "%d ", tmp);
		strcat(statbuf, buf);
	}
	statbuf[strlen(statbuf) - 1] = ']';

	strcat(statbuf, " MW[");
	for (i = 0; i < info->asic_cnts; i++) {
		sprintf(buf, "%ld ", info->matching_work[i]);
		strcat(statbuf, buf);
	}
	statbuf[strlen(statbuf) - 1] = ']';

	sprintf(buf, " Led[%d]", info->led_status);
	strcat(statbuf, buf);

	sprintf(buf, " PG[%d]", info->power_good);
	strcat(statbuf, buf);

	root = api_add_string(root, "AVAM Dev", statbuf, true);

	sprintf(buf, "%d %d %d",
			info->usbfifo_cnt,
			info->workfifo_cnt,
			info->noncefifo_cnt);

	root = api_add_string(root, "AVAM Fifo", buf, true);
	root = api_add_uint8(root, "AVAM ntime", &opt_avalonm_ntime_offset, true);
	root = api_add_bool(root, "Automatic Frequency", &opt_avalonm_autof, true);
	return root;
}

static void avalonm_statline_before(char *buf, size_t bufsiz, struct cgpu_info *avalonm)
{
	struct avalonm_info *info = avalonm->device_data;
	int frequency;

	if (!strncmp(info->ver, "3U", 2))
		tailsprintf(buf, bufsiz, "%4dMhz %.2fV", info->get_frequency[0][0], (float)info->get_voltage);
	else {
		frequency = (info->set_frequency[0][0] * 4 + info->set_frequency[0][1] * 4 + info->set_frequency[0][2]) / 9;
		tailsprintf(buf, bufsiz, "%4dMhz %.4fV", frequency, (float)info->get_voltage / 10000);
	}
}

/* We use a replacement algorithm to only remove references to work done from
 * the buffer when we need the extra space for new work. */
static bool avalonm_fill(struct cgpu_info *avalonm)
{
	struct avalonm_info *info = avalonm->device_data;
	int subid, slot, ac;
	struct work *work;
	bool ret = true;

	ac = info->asic_cnts;
	mutex_lock(&info->qlock);
	if (avalonm->queued >= ac)
		goto out_unlock;
	work = get_queued(avalonm);
	if (unlikely(!work)) {
		ret = false;
		goto out_unlock;
	}
	subid = avalonm->queued++;
	work->subid = subid;
	slot = avalonm->work_array * ac + subid;
	if (likely(avalonm->works[slot]))
		work_completed(avalonm, avalonm->works[slot]);
	avalonm->works[slot] = work;
	if (avalonm->queued < ac)
		ret = false;
out_unlock:
	mutex_unlock(&info->qlock);

	return ret;
}

static void avalonm_flush_work(struct cgpu_info *avalonm)
{

	struct avalonm_info *info = avalonm->device_data;

	/* Will overwrite any work queued. Do this unlocked since it's just
	 * changing a single non-critical value and prevents deadlocks */
	avalonm->queued = 0;

	/* Signal main loop we need more work */
	cgsem_post(&info->qsem);
}

struct device_drv avalonm_drv = {
	.drv_id = DRIVER_avalonm,
	.dname = "avalonm",
	.name = "AVM",
	.set_device = avalonm_set_device,
	.get_api_stats = avalonm_api_stats,
	.get_statline_before = avalonm_statline_before,
	.drv_detect = avalonm_detect,
	.thread_prepare = avalonm_prepare,

	.hash_work = hash_queued_work,
	.scanwork = avalonm_scanhash,
	.queue_full = avalonm_fill,
	.flush_work = avalonm_flush_work,

	.thread_shutdown = avalonm_shutdown,
};
