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
#include "config.h"
#include "miner.h"
#include "driver-avalon4-mini.h"
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

uint32_t opt_avalonm_freq[3] = {AVAM_DEFAULT_FREQUENCY,
			   AVAM_DEFAULT_FREQUENCY,
			   AVAM_DEFAULT_FREQUENCY};
uint16_t opt_avalonm_ntime_offset = 0;
int opt_avalonm_voltage = AVAM_DEFAULT_VOLTAGE;
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
	{400, 0x1e078447},
	{413, 0x40300447},
	{425, 0x20080447},
	{438, 0x44310447},
	{450, 0x22088447},
	{463, 0x48320447},
	{475, 0x24090447},
	{488, 0x4c330447},
	{500, 0x26098447},
	{513, 0x50340447},
	{525, 0x280a0447},
	{538, 0x54350447},
	{550, 0x2a0a8447},
	{563, 0x58360447},
	{575, 0x2c0b0447},
	{588, 0x5c370447},
	{600, 0x2e0b8447},
	{613, 0x60380447},
	{625, 0x300c0447},
	{638, 0x64390447},
	{650, 0x320c8447},
	{663, 0x683a0447},
	{675, 0x340d0447},
	{688, 0x6c3b0447},
	{700, 0x360d8447},
	{713, 0x703c0447},
	{725, 0x380e0447},
	{738, 0x743d0447},
	{750, 0x3a0e8447},
	{763, 0x783e0447},
	{775, 0x3c0f0447},
	{788, 0x7c3f0447},
	{800, 0x3e0f8447},
	{813, 0x3e0f8447},
	{825, 0x40100447},
	{838, 0x40100447},
	{850, 0x42108447},
	{863, 0x42108447},
	{875, 0x44110447},
	{888, 0x44110447},
	{900, 0x46118447},
	{913, 0x46118447},
	{925, 0x48120447},
	{938, 0x48120447},
	{950, 0x4a128447},
	{963, 0x4a128447},
	{975, 0x4c130447},
	{988, 0x4c130447},
	{1000, 0x4e138447}
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

static void process_nonce(struct cgpu_info *avalonm, uint8_t *report)
{
	struct avalonm_info *info = avalonm->device_data;
	struct work *work;
	uint8_t ntime, chip_id;
	uint32_t nonce, id;

	PACK32(report, &id);
	chip_id = report[6];
	if (chip_id >= info->asic_cnts) {
		applog(LOG_ERR, "%s-%d: chip_id >= info->asic_cnts(%d > %d)",
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

	submit_noffset_nonce(info->thr, work, nonce, ntime);
	free_work(work);
	info->nonce_cnts++;
}

static int decode_pkg(struct thr_info *thr, struct avalonm_ret *ar)
{
	struct cgpu_info *avalonm = thr->cgpu;
	struct avalonm_info *info = avalonm->device_data;
	uint32_t ret, tmp;
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
		info->spi_speed = be32toh(tmp);
		memcpy(&tmp, ar->data + 4, 4);
		info->led_status = be32toh(tmp);
		memcpy(&tmp, ar->data + 8, 4);
		info->fan_pwm = be32toh(tmp);
		memcpy(&tmp, ar->data + 12, 4);
		info->get_voltage = decode_voltage((uint8_t)be32toh(tmp));
		memcpy(&tmp, ar->data + 28 , 4);
		info->power_good = be32toh(tmp);

		/* power off notice */
		if (!info->get_voltage) {
			applog(LOG_ERR, "%s-%d: AVAM_P_STATUS_M Power off notice", avalonm->drv->name, avalonm->device_id);
			info->power_on = 1;
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

static struct cgpu_info *avalonm_detect_one(struct libusb_device *dev, struct usb_find_devices *found)
{
	struct cgpu_info *avalonm = usb_alloc_cgpu(&avalonm_drv, 1);
	struct avalonm_info *info;
	struct avalonm_pkg send_pkg;
	struct avalonm_ret ar;
	int ret;

	if (!usb_init(avalonm, dev, found)) {
		applog(LOG_ERR, "Avalonm failed usb_init");
		avalonm = usb_free_cgpu(avalonm);
		return NULL;
	}

	/* Avalonm prefers not to use zero length packets */
	avalonm->nozlp = true;

	/* We have an Avalonm connected */
	avalonm->threads = 1;
	memset(send_pkg.data, 0, AVAM_P_DATA_LEN);
	avalonm_init_pkg(&send_pkg, AVAM_P_DETECT, 1, 1);
	ret = avalonm_xfer_pkg(avalonm, &send_pkg, &ar);
	if ((ret != AVAM_SEND_OK) && (ar.type != AVAM_P_ACKDETECT)) {
		applog(LOG_DEBUG, "%s-%d: Failed to detect Avalon4 mini!", avalonm->drv->name, avalonm->device_id);
		return NULL;
	}

	add_cgpu(avalonm);

	usb_buffer_clear(avalonm);
	update_usb_stats(avalonm);
	applog(LOG_ERR, "%s-%d: Found at %s", avalonm->drv->name, avalonm->device_id,
	       avalonm->device_path);

	avalonm->device_data = cgcalloc(sizeof(struct avalonm_info), 1);
	info = avalonm->device_data;
	info->thr = NULL;
	info->set_frequency[0] = info->set_frequency[1] = info->set_frequency[2] = AVAM_DEFAULT_FREQUENCY;
	info->nonce_cnts = 0;
	memcpy(info->dna, ar.data, AVAM_MM_DNA_LEN);
	memcpy(info->ver, ar.data + AVAM_MM_DNA_LEN, AVAM_MM_VER_LEN);
	memcpy(&info->asic_cnts, ar.data + AVAM_MM_DNA_LEN + AVAM_MM_VER_LEN, 4);
	info->asic_cnts = be32toh(info->asic_cnts);
	info->usbfifo_cnt = 0;
	info->workfifo_cnt = 0;
	info->noncefifo_cnt = 0;
	info->crcerr_cnt = 0;
	info->power_good = 0;
	info->spi_speed = 0;
	info->led_status = 0;
	info->fan_pwm = 0;
	info->get_voltage = 0;

	return avalonm;
}

static uint32_t avalonm_get_cpm(int freq)
{
	int i;

	for (i = 0; i < sizeof(g_freq_array) / sizeof(g_freq_array[0]); i++)
		if (freq >= g_freq_array[i][0] && freq < g_freq_array[i+1][0])
			return g_freq_array[i][1];

	/* return the lowest freq if not found */
	return g_freq_array[0][1];
}

static void avalonm_set_freq(struct cgpu_info *avalonm)
{
	struct avalonm_info *info = avalonm->device_data;
	struct avalonm_pkg send_pkg;
	uint32_t tmp;
	uint32_t max_freq, i;

	info->set_frequency[0] = opt_avalonm_freq[0];
	info->set_frequency[1] = opt_avalonm_freq[1];
	info->set_frequency[2] = opt_avalonm_freq[2];

	max_freq = opt_avalonm_freq[0];
	for (i = 1; i < 3; i++) {
		if (max_freq < opt_avalonm_freq[i])
			max_freq = opt_avalonm_freq[i];
	}

	info->delay_ms = CAL_DELAY(max_freq);

	memset(send_pkg.data, 0, AVAM_P_DATA_LEN);
	tmp = avalonm_get_cpm(info->set_frequency[0]);
	tmp = be32toh(tmp);
	memcpy(send_pkg.data, &tmp, 4);
	tmp = avalonm_get_cpm(info->set_frequency[1]);
	tmp = be32toh(tmp);
	memcpy(send_pkg.data + 4, &tmp, 4);
	tmp = avalonm_get_cpm(info->set_frequency[2]);
	tmp = be32toh(tmp);
	memcpy(send_pkg.data + 8, &tmp, 4);

	avalonm_init_pkg(&send_pkg, AVAM_P_SET_FREQ, 1, 1);
	avalonm_send_pkg(avalonm, &send_pkg);
	applog(LOG_DEBUG, "%s-%d: Avalonm set freq %d,%d,%d",
			avalonm->drv->name, avalonm->device_id,
			info->set_frequency[0],
			info->set_frequency[1],
			info->set_frequency[2]);
}

static void avalonm_set_voltage(struct cgpu_info *avalonm)
{
	struct avalonm_info *info = avalonm->device_data;
	struct avalonm_pkg send_pkg;
	uint16_t tmp;

	if (!info->power_on)
		return;

	info->set_voltage = opt_avalonm_voltage;
	memset(send_pkg.data, 0, AVAM_P_DATA_LEN);
	/* Use shifter to set voltage */
	tmp = info->set_voltage;
	tmp = encode_voltage(tmp);
	tmp = htobe16(tmp);
	memcpy(send_pkg.data, &tmp, 2);

	/* Package the data */
	avalonm_init_pkg(&send_pkg, AVAM_P_SET_VOLT, 1, 1);
	avalonm_send_pkg(avalonm, &send_pkg);
	applog(LOG_ERR, "%s-%d: Avalonm set volt %d",
	       avalonm->drv->name, avalonm->device_id,
	       info->set_voltage);

	info->power_on = 0;
	cgsleep_ms(500);
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

	/* Half nonce range */
	ms_timeout = 0x80000000ll / 1000;

	/* Wait until avalon_send_tasks signals us that it has completed
	 * sending its work or a full nonce range timeout has occurred. We use
	 * cgsems to never miss a wakeup. */
	cgsem_mswait(&info->qsem, ms_timeout);

	hash_count = info->nonce_cnts++;
	info->nonce_cnts = 0;

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

	int start_count, end_count, i, j, ret;
	int avalon_get_work_count = AVAM_DEFAULT_ASIC_COUNT;

	snprintf(threadname, sizeof(threadname), "%d/AvmProc", avalonm->device_id);
	RenameThread(threadname);

	while (likely(!avalonm->shutdown)) {
		if (unlikely(avalonm->usbinfo.nodev)) {
			applog(LOG_ERR, "%s-%d: Device disappeared, shutting down thread",
			       avalonm->drv->name, avalonm->device_id);
			goto out;
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

			if (likely(j < avalonm->queued && avalonm->works[i])) {
				/* Configuration */
				avalonm_set_voltage(avalonm);
				avalonm_set_freq(avalonm);

				/* P_WORK part 1: midstate */
				memcpy(send_pkg.data, work->midstate, AVAM_P_DATA_LEN);
				rev((void *)(send_pkg.data), AVAM_P_DATA_LEN);
				avalonm_init_pkg(&send_pkg, AVAM_P_WORK, 1, 2);
				hexdump(send_pkg.data, 32);
				avalonm_send_pkg(avalonm, &send_pkg);

				/* P_WORK part 2:
				 * nonce2(4)+id(4)+ntime(2)+reserved(12)+data(12) */
				memset(send_pkg.data, 0, AVAM_P_DATA_LEN);

				UNPACK32(work->id, send_pkg.data);

				send_pkg.data[9] = opt_avalonm_ntime_offset;

				/* TODO led */
				UNPACK32(0, send_pkg.data + 12);

				memcpy(send_pkg.data + 20, work->data + 64, 12);
				rev((void *)(send_pkg.data + 20), 12);
				avalonm_init_pkg(&send_pkg, AVAM_P_WORK, 2, 2);
				hexdump(send_pkg.data, 32);
				avalonm_send_pkg(avalonm, &send_pkg);

				cgsleep_ms(1);
			} else {
			}
		}

		mutex_unlock(&info->qlock);
		avalonm_rotate_array(avalonm, info);

		cgsem_post(&info->qsem);

		/* Get result */
		do {
			ret = avalonm_get_reports(avalonm);
		} while (ret != AVAM_P_STATUS_M);

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
	avalonm->works = calloc(AVAM_DEFAULT_ASIC_COUNT * sizeof(struct work *),
			       AVAM_DEFAULT_ARRAY_SIZE);
	if (!avalonm->works)
		quit(1, "Failed to calloc avalon4 mini works in avalonm_prepare");

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
			return "Invalid value1 passed to avalonm-freq";
	}

	if (colon1 && *colon1) {
		colon2 = strchr(colon1, ':');
		if (colon2)
			*(colon2++) = '\0';

		if (*colon1) {
			val2 = atoi(colon1);
			if (val2 < AVAM_DEFAULT_FREQUENCY_MIN || val2 > AVAM_DEFAULT_FREQUENCY_MAX)
				return "Invalid value2 passed to avalonm-freq";
		}

		if (colon2 && *colon2) {
			val3 = atoi(colon2);
			if (val3 < AVAM_DEFAULT_FREQUENCY_MIN || val3 > AVAM_DEFAULT_FREQUENCY_MAX)
				return "Invalid value3 passed to avalonm-freq";
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

		if (set_avalonm_freq(setting)) {
			sprintf(replybuf, "invalid frequency value, valid range %d-%d",
				AVAM_DEFAULT_FREQUENCY_MIN, AVAM_DEFAULT_FREQUENCY_MAX);
			return replybuf;
		}

		applog(LOG_NOTICE, "%s-%d: Update frequency to %d",
		       avalonm->drv->name, avalonm->device_id,
		       (opt_avalonm_freq[0] * 4 + opt_avalonm_freq[1] * 4 + opt_avalonm_freq[2]) / 9);

		return NULL;
	}

	if (strcasecmp(option, "voltage") == 0) {
		if (!setting || !*setting) {
			sprintf(replybuf, "missing voltage value");
			return replybuf;
		}

		if (set_avalonm_voltage(setting)) {
			sprintf(replybuf, "invalid voltage value, valid range %d-%d",
					AVAM_DEFAULT_VOLTAGE_MIN, AVAM_DEFAULT_VOLTAGE_MAX);
			return replybuf;
		}

		applog(LOG_NOTICE, "%s-%d: Update voltage to %d",
		       avalonm->drv->name, avalonm->device_id, opt_avalonm_voltage);

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

	sprintf(buf, " Chips[%d]", info->asic_cnts);
	strcat(statbuf, buf);

	sprintf(buf, " Crc[%d]", info->crcerr_cnt);
	strcat(statbuf, buf);

	sprintf(buf, " Speed[%d]", info->spi_speed);
	strcat(statbuf, buf);

	sprintf(buf, " Vol[%.4f]", (float)info->get_voltage / 10000);
	strcat(statbuf, buf);

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
	return root;
}

static void avalonm_statline_before(char *buf, size_t bufsiz, struct cgpu_info *avalonm)
{
	struct avalonm_info *info = avalonm->device_data;
	int frequency;

	frequency = (info->set_frequency[0] * 4 + info->set_frequency[1] * 4 + info->set_frequency[2]) / 9;
	tailsprintf(buf, bufsiz, "%4dMhz %.4fV", frequency, (float)info->get_voltage / 10000);
}

/* We use a replacement algorithm to only remove references to work done from
 * the buffer when we need the extra space for new work. */
static bool avalonm_fill(struct cgpu_info *avalonm)
{
	struct avalonm_info *info = avalonm->device_data;
	int subid, slot, ac;
	struct work *work;
	bool ret = true;

	ac = AVAM_DEFAULT_ASIC_COUNT;
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
