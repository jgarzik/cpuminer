/*
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

static int avalonu_init_pkg(struct avalonu_pkg *pkg, uint8_t type, uint8_t idx, uint8_t cnt)
{
	unsigned short crc;

	pkg->head[0] = AVAU_H1;
	pkg->head[1] = AVAU_H2;

	pkg->type = type;
	pkg->opt = 0;
	pkg->idx = idx;
	pkg->cnt = cnt;

	crc = crc16(pkg->data, AVAU_P_DATA_LEN);

	pkg->crc[0] = (crc & 0xff00) >> 8;
	pkg->crc[1] = crc & 0x00ff;
	return 0;
}

static int decode_pkg(struct thr_info *thr, struct avalonu_ret *ar)
{
	struct cgpu_info *avalonu = thr->cgpu;
	struct avalonu_info *info = avalonu->device_data;
	unsigned int expected_crc;
	unsigned int actual_crc;
	uint32_t nonce, nonce2, ntime, tmp;
	uint32_t job_id;
	int pool_no, i;
	struct pool *pool, *real_pool;

	if (ar->head[0] != AVAU_H1 && ar->head[1] != AVAU_H2) {
		applog(LOG_DEBUG, "%s-%d: H1 %02x, H2 %02x",
				avalonu->drv->name, avalonu->device_id,
				ar->head[0], ar->head[1]);
		hexdump(ar->data, 32);
		return 1;
	}

	expected_crc = crc16(ar->data, AVAU_P_DATA_LEN);
	actual_crc = (ar->crc[1] & 0xff) | ((ar->crc[0] & 0xff) << 8);
	if (expected_crc != actual_crc) {
		applog(LOG_DEBUG, "%s-%d: %02x: expected crc(%04x), actual_crc(%04x)",
		       avalonu->drv->name, avalonu->device_id,
		       ar->type, expected_crc, actual_crc);
		return 1;
	}

	switch(ar->type) {
	case AVAU_P_NONCE:
		applog(LOG_DEBUG, "%s-%d: AVAU_P_NONCE", avalonu->drv->name, avalonu->device_id);
		job_id = ar->data[0];
		ntime = ar->data[1];
		pool_no = (ar->data[2] << 8) | ar->data[3];
		nonce2 = (ar->data[4] << 24) | (ar->data[5] << 16) | (ar->data[6] << 8) | ar->data[7];
		nonce = (ar->data[11] << 24) | (ar->data[10] << 16) | (ar->data[9] << 8) | ar->data[8];

		if (pool_no >= total_pools || pool_no < 0) {
			applog(LOG_DEBUG, "%s-%d: Wrong pool_no %d",
					avalonu->drv->name, avalonu->device_id,
					pool_no);
			break;
		}
		nonce -= 0x4000;

		applog(LOG_DEBUG, "%s-%d: Found! P:%d - N2:%08x N:%08x NR:%d",
		       avalonu->drv->name, avalonu->device_id,
		       pool_no, nonce2, nonce, ntime);

		real_pool = pool = pools[pool_no];
		submit_nonce2_nonce(thr, pool, real_pool, nonce2, nonce, ntime);
		info->nonce_cnts++;
		break;
	case AVAU_P_STATUS:
		applog(LOG_DEBUG, "%s-%d: AVAU_P_STATUS", avalonu->drv->name, avalonu->device_id);
		hexdump(ar->data, 32);
		break;
	default:
		applog(LOG_DEBUG, "%s-%d: Unknown response (%x)", avalonu->drv->name, avalonu->device_id,
				ar->type);
		break;
	}
	return 0;
}

static struct cgpu_info *avalonu_detect_one(struct libusb_device *dev, struct usb_find_devices *found)
{
	struct cgpu_info *avalonu = usb_alloc_cgpu(&avalonu_drv, 1);
	struct avalonu_info *info;

	if (!usb_init(avalonu, dev, found)) {
		applog(LOG_ERR, "Avalonu failed usb_init");
		avalonu = usb_free_cgpu(avalonu);
		return NULL;
	}

	/* Avalonu prefers not to use zero length packets */
	avalonu->nozlp = true;

	/* We have an Avalonu connected */
	avalonu->threads = 1;
	add_cgpu(avalonu);

	usb_buffer_clear(avalonu);
	update_usb_stats(avalonu);
	applog(LOG_INFO, "%s-%d: Found at %s", avalonu->drv->name, avalonu->device_id,
	       avalonu->device_path);

	avalonu->device_data = cgcalloc(sizeof(struct avalonu_info), 1);
	info = avalonu->device_data;
	info->mainthr = NULL;
	info->workinit = 0;
	info->nonce_cnts = 0;
	return avalonu;
}

static inline void avalonu_detect(bool __maybe_unused hotplug)
{
	usb_detect(&avalonu_drv, avalonu_detect_one);
}

static int avalonu_send_pkg(struct cgpu_info *avalonu, const struct avalonu_pkg *pkg)
{
	int err = -1;
	int writecnt;

	if (unlikely(avalonu->usbinfo.nodev))
		return -1;

	err = usb_write(avalonu, (char *)pkg, AVAU_P_COUNT, &writecnt, C_AVAU_WRITE);
	if (err || writecnt != AVAU_P_COUNT) {
		applog(LOG_DEBUG, "%s-%d: avalonu_send_pkg %d, w(%d-%d)!", avalonu->drv->name, avalonu->device_id, err, AVAU_P_COUNT, writecnt);
		return -1;
	}

	return writecnt;
}

static int avalonu_receive_pkg(struct cgpu_info *avalonu, struct avalonu_ret *ret)
{
	int err = -1;
	int readcnt;

	if (unlikely(avalonu->usbinfo.nodev))
		return -1;

	err = usb_read(avalonu, (char*)ret, AVAU_P_COUNT, &readcnt, C_AVAU_READ);
	if (err || readcnt != AVAU_P_COUNT) {
		applog(LOG_DEBUG, "%s-%d: avalonu_receive_pkg %d, w(%d-%d)!", avalonu->drv->name, avalonu->device_id, err, AVAU_P_COUNT, readcnt);
		return -1;
	}

	return readcnt;
}

static int avalonu_xfer_pkg(struct cgpu_info *avalonu, const struct avalonu_pkg *pkg, struct avalonu_ret *ret)
{
	if (sizeof(struct avalonu_pkg) != avalonu_send_pkg(avalonu, pkg))
		return AVAU_SEND_ERROR;

	cgsleep_ms(50);
	if (sizeof(struct avalonu_ret) != avalonu_receive_pkg(avalonu, ret))
		return AVAU_SEND_ERROR;

	return AVAU_SEND_OK;
}

static void *avalonu_get_reports(void *userdata)
{
	struct cgpu_info *avalonu = (struct cgpu_info *)userdata;
	struct avalonu_info *info = avalonu->device_data;
	struct thr_info *thr = info->mainthr;
	char threadname[16];
	struct avalonu_pkg send_pkg;
	struct avalonu_ret ar;
	int ret;

	/* wait miner thread start */
	while (!info->workinit)
		cgsleep_ms(200);

	snprintf(threadname, sizeof(threadname), "%d/AvauRecv", avalonu->device_id);
	RenameThread(threadname);

	while (likely(!avalonu->shutdown)) {
		memset(send_pkg.data, 0, AVAU_P_DATA_LEN);
		avalonu_init_pkg(&send_pkg, AVAU_P_POLLING, 1, 1);
		ret = avalonu_xfer_pkg(avalonu, &send_pkg, &ar);
		if (ret == AVAU_SEND_OK)
			decode_pkg(thr, &ar);

		cgsleep_ms(20);
	}
}

static bool avalonu_prepare(struct thr_info *thr)
{
	struct cgpu_info *avalonu = thr->cgpu;
	struct avalonu_info *info = avalonu->device_data;

	info->mainthr = thr;
	if (pthread_create(&info->read_thr, NULL, avalonu_get_reports, (void*)avalonu))
		quit(1, "Failed to create avalonu read_thr");

	return true;
}

static int64_t avalonu_scanhash(struct thr_info *thr)
{
	static uint8_t job_id;
	struct cgpu_info *avalonu = thr->cgpu;
	struct avalonu_info *info = avalonu->device_data;
	int64_t h;
	int i;
	struct work *work;
	struct avalonu_pkg send_pkg;
	struct pool *pool;

	if (unlikely(avalonu->usbinfo.nodev)) {
		applog(LOG_ERR, "%s-%d: Device disappeared, shutting down thread",
		       avalonu->drv->name, avalonu->device_id);
		return -1;
	}

	info->workinit = 1;
	work = get_work(thr, thr->id);
	memcpy(send_pkg.data, work->midstate, AVAU_P_DATA_LEN);
	avalonu_init_pkg(&send_pkg, AVAU_P_WORK, 1, 2);
	avalonu_send_pkg(avalonu, &send_pkg);

	memset(send_pkg.data, 0, AVAU_P_DATA_LEN);
	send_pkg.data[0] = job_id++; /* job_id */
	send_pkg.data[1] = 0; /* rolling ntime */
	pool = current_pool();
	send_pkg.data[2] = pool->pool_no >> 8; /* pool no */
	send_pkg.data[3] = pool->pool_no & 0xff;
	send_pkg.data[4] = (work->nonce2 >> 24) & 0xff;
	send_pkg.data[5] = (work->nonce2 >> 16) & 0xff;
	send_pkg.data[6] = (work->nonce2 >> 8) & 0xff;
	send_pkg.data[7] = (work->nonce2) & 0xff;
	memcpy(send_pkg.data + 20, work->data + 64, 12);
	avalonu_init_pkg(&send_pkg, AVAU_P_WORK, 2, 2);
	avalonu_send_pkg(avalonu, &send_pkg);

	h = info->nonce_cnts;
	info->nonce_cnts = 0;
	return  h * 0xffffffffull;
}

static void avalonu_shutdown(struct thr_info *thr)
{
	struct cgpu_info *avalonu = thr->cgpu;
	struct avalonu_info *info = avalonu->device_data;

	pthread_join(info->read_thr, NULL);
}

struct device_drv avalonu_drv = {
	.drv_id = DRIVER_avalonu,
	.dname = "avalonu",
	.name = "AVU",
	.drv_detect = avalonu_detect,
	.thread_prepare = avalonu_prepare,
	.hash_work = hash_driver_work,
	.scanwork = avalonu_scanhash,
	.thread_shutdown = avalonu_shutdown,
};
