/*
 * Copyright 2013 Andrew Smith - BlackArrow Ltd
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 3 of the License, or (at your option)
 * any later version.  See COPYING for more details.
 */

#include "config.h"
#include "compat.h"
#include "miner.h"

#ifndef LINUX
static void minion_detect(__maybe_unused bool hotplug)
{
}
#else

#include <unistd.h>
#include <linux/spi/spidev.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <fcntl.h>

#define MINION_SPI_BUS 0
#define MINION_SPI_CHIP 0

#define MINION_SPI_SPEED 96000
#define MINION_SPI_BUFSIZ 1024

#define MINION_CHIPS 32
#define MINION_CORES 64

#define MINION_FFL " - from %s %s() line %d"
#define MINION_FFL_HERE __FILE__, __func__, __LINE__
#define MINION_FFL_PASS file, func, line

#define MINION_SYS_REGS 0x00
#define MINION_CORE_REGS 0x10
#define MINION_RES_BUF 0x20
#define MINION_CMD_QUE 0x30

#define DATA_SIZ (sizeof(uint32_t))

// All SYS data sizes are DATA_SIZ
#define MINION_SYS_CHIP_SIG 0x00
#define MINION_SYS_CHIP_STA 0x01
#define MINION_SYS_TEMP_CTL 0x03
#define MINION_SYS_FREQ_CTL 0x04
#define MINION_SYS_NONCE_LED 0x05
#define MINION_SYS_MISC_CTL 0x06
#define MINION_SYS_RSTN_CTL 0x07
#define MINION_SYS_INT_ENA 0x08
#define MINION_SYS_INT_CLR 0x09
#define MINION_SYS_INT_STA 0x0a
#define MINION_SYS_FIFO_STA 0x0b
#define MINION_SYS_QUE_TRIG 0x0c
#define MINION_SYS_BUF_TRIG 0x0d

// CORE data size is minion_core
#define MINION_CORE_ENA0_31 0x10
#define MINION_CORE_ENA32_63 0x11
#define MINION_CORE_ACT0_31 0x14
#define MINION_CORE_ACT32_63 0x15

// RES data size is minion_res
#define MINION_RES_DATA 0x20
#define MINION_RES_PEEK 0x21

// QUE data size is minion_que
#define MINION_QUE_0 0x30
#define MINION_QUE_R 0x31

struct minion_header {
	uint8_t chip;
	uint8_t reg;
	uint8_t siz[2];
}

#define MINION_CHIP_SIG 0x32020ffa

#define SET_CORE(_core, _n) ((_core)->core[_n << 4] &= (2 >> (_n % 8)))
#define CORE_IDLE(_core, _n) ((_core)->core[_n << 4] & (2 >> (_n % 8)))

struct minion_core {
	uint8_t core[DATA_SIZ];
}

#define RES_GOLD(_res) ((((_res)->status[0]) & 0x80) == 0)
#define RES_CHIP(_res) (((_res)->status[0]) & 0x1f)
#define RES_CORE(_res) ((_res)->status[1])
#define RES_TASK(_res) ((int)((_res)->status[2]) * 0x100 + (int)((_res)->status[2]))

struct minion_res {
	uint8_t status[DATA_SIZ];
	uint8_t nonce[DATA_SIZ];
};

#define MIDSTATE_BYTES 32
#define MERKLE_OFFSET 64
#define MERKLE_BYTES 12

#define MINION_MAX_TASK_ID 0xffff

struct minion_que {
	uint8_t reserved[2];
	uint8_t task_id[2];
	uint8_t midstate[MIDSTATE_BYTES];
	uint8_t merkle7[DATA_SIZ];
	uint8_t ntime[DATA_SIZ];
	uint8_t bits[DATA_SIZ];
};

#define ALLOC_WITEMS 4096

typedef struct witem {
	struct work *work;
	uint32_t task_id;
	int nonces;
	bool urgent;
} WITEM;

#define ALLOC_TITEMS 256

typedef struct titem {
	uint8_t siz[2];
	uint8_t wbuf[1024];
	uint8_t rbuf[1024];
	bool urgent;
} TITEM;

#define ALLOC_RITEMS 256

typedef struct ritem {
	int chip;
	int core;
	uint32_t task_id;
	uint32_t nonce;
	bool no_nonce;
} RITEM;

typedef struct k_item {
	struct k_item *prev;
	struct k_item *next;
	void *data;
} K_ITEM;

#define DATAW(_item) ((WITEM *)(_item->data))
#define DATAT(_item) ((TITEM *)(_item->data))
#define DATAR(_item) ((RITEM *)(_item->data))

typedef struct k_list {
	bool is_store;
	cglock_t *lock;
	struct k_item *head;
	struct k_item *tail;
	size_t siz;		// item data size
	int total;		// total allocated
	int count;		// in this list
	int stale_count;	// for the work list
	int allocate;		// number to intially allocate and each time we run out
	bool do_tail;		// store tail
} K_LIST;

/*
 * K_STORE is for a list of items taken from a K_LIST
 * The restriction is, a K_STORE must not allocate new items,
 * only the K_LIST should do that
 * i.e. all K_STORE items came from a K_LIST
 */
#define K_STORE K_LIST

#define K_WLOCK(_list) cg_wlock(_list->lock)
#define K_WUNLOCK(_list) cg_wunlock(_list->lock)
#define K_RLOCK(_list) cg_rlock(_list->lock)
#define K_RUNLOCK(_list) cg_runlock(_list->lock)

struct minion_info {
	struct thr_info spi_thr;
	struct thr_info res_thr;

	int spifd;
	// TODO: need to track disabled chips
	// detect chip scan will need to check all chips
	int chips;

	uint32_t next_task_id;

	// Stats
	struct timeval chip_start[MINION_CHIPS];
	uint64_t chip_good[MINION_CHIPS];
	uint64_t chip_bad[MINION_CHIPS];
	uint64_t core_good[MINION_CHIPS][MINION_CORES];
	uint64_t core_bad[MINION_CHIPS][MINION_CORES];
	uint64_t chip_spie[MINION_CHIPS]; // spi errors
	uint64_t chip_miso[MINION_CHIPS]; // msio errors

	uint64_t ok_nonces;
	uint64_t new_nonces;
	uint64_t untested_nonces;
	uint64_t tested_nonces;

	// Work items
	K_LIST *wfree_list;
	K_STORE *wwork_list;
	K_STORE *wchip_list[MINION_CHIPS];

	// Task list
	K_LIST *tfree_list;
	K_STORE *task_list;

	// Nonce replies
	K_LIST *rfree_list;
	K_STORE *rnonce_list;

	// other replies?

	struct timeval last_did;

	bool initialised;
};

static void alloc_items(K_LIST *list)
{
	K_ITEM *item;

	if (list->is_store) {
		quithere(1, "List %s store can't %s",
				list->name, __func__);
	}

	item = calloc(list->allocate, sizeof(*item));
	if (!item) {
		quithere(1, "List %s failed to calloc %d new items - total was %d",
				list->name, list->allocate, list->total);
	}

	list->total += list->allocate;
	list->count = list->allocate;

	item[0].prev = NULL;
	item[0].next = &(item[1]);
	for (i = 1; i < list->allocate-1; i++) {
		item[i].prev = &item[i-1];
		item[i].next = &item[i+1];
	}
	item[list->allocate-1].prev = &(item[list->allocate-2]);
	item[list->allocate-1].next = NULL;

	list->head = item;
	if (list->do_tail)
		list->tail = &(item[list->allocate-1]);
}

static K_STORE new_store(K_LIST *list, bool do_tail)
{
	K_STORE *store;

	store = calloc(1, sizeof(*store));
	if (!store)
		quithere(1, "Failed to calloc store for %s", list->name);

	store->is_store = true;
	store->lock = list->lock;
	store->name = list->name;
	list->do_tail = do_tail;
}

static K_LIST new_list(const char *name, size_t siz, int allocate, bool do_tail)
{
	K_LIST *list;

	if (allocate < 1)
		quithere(1, "Invalid new list %s with allocate %d must be > 0", name, allocate);

	list = calloc(1, sizeof(*list));
	if (!list)
		quithere(1, "Failed to calloc list %s", name);

	list->is_store = false;

	list->lock = calloc(1, sizeof(*(list->lock)));
	if (!(list->lock))
		quithere(1, "Failed to calloc lock for list %s", name);

	cglock_init(list->lock);

	list->name = name;
	list->siz = siz;
	list->allocate = allocate;
	list->do_tail = do_tail;

	alloc_items(list);

	return list;
}

static K_ITEM k_get_head(K_LIST *list)
{
	K_ITEM *item;

	item = list->head;
	if (item) {
		list->count--;
		if (item->next) {
			list->head = item->next;
			list->head->prev = NULL;
		} else {
			list->head = NULL;
			if (list->do_tail)
				list->tail = NULL;
		}
	}
	item->prev = NULL;
	item->next = NULL;

	return item;
}

static void k_add_head(K_LIST *list, K_ITEM *item)
{
	item->prev = NULL;
	item->next = list->head;
	if (list->head)
		list->head->prev = item;

	if (list->do_tail) {
		if (list->do_tail && !(list->tail))
			list->tail = item;
	}
	list->head = item;
}

static void k_remove(K_LIST *list, K_ITEM *item)
{
	if (item->prev) {
		item->prev->next = item->next;
	} else {
		list->head = item->next;
		if (list->head)
			list->head->prev = NULL;
	} 

	if (item->next) {
		item->next->prev = item->prev;
	} else {
		if (list->do_tail) {
			list->tail = item->prev;
			if (list->tail)
				list->tail->next = NULL;
		}
	}

	item->prev = item->next = NULL;

	list->count--;
}

static void ready_work(struct cgpu_info *minioncgpu, struct work *work)
{
	struct minion_info *minioninfo = (struct minion_info *)(minioncgpu->device_data);
	K_ITEM *item = NULL;

	K_WLOCK(minioninfo->wfree_list);

	item = k_get_head(minioninfo->wfree_list);

	DATAW(item)->work = work;
	DATAW(item)->nonces = 0;

	k_add_head(minioninfo->wwork_list, item);

	K_WUNLOCK(minioninfo->wfree_list);
}

static bool oldest_nonce(struct cgpu_info *minioncgpu, int *chip, int *core, uint32_t *task_id, uint32_t *nonce, bool *no_nonce)
{
	struct minion_info *minioninfo = (struct minion_info *)(minioncgpu->device_data);
	K_ITEM *item = NULL;
	bool found = false;

	K_WLOCK(minioninfo->rnonce_list);

	if (minioninfo->rnonce_list->tail) {
		// unlink from res
		item = minioninfo->rnonce_list->tail;
		k_remove(minioninfo->rnonce_list, item);

		found = true;
		*chip = DATAR(item)->chip;
		*core = DATAR(item)->core;
		*task_id = DATAR(item)->task_id;
		*nonce = DATAR(item)->nonce;
		*no_nonce = DATAR(item)->no_nonce;

		k_add_head(minioninfo->rfree_list, item);
	}

	K_WUNLOCK(minioninfo->rnonce_list);

	return found;
}

static REPLY _minion_txrx(struct cgpu_info *minioncgpu, struct minion_info *minioninfo, uintptr_t wbuf, uint32_t wsiz, uintptr_t rbuf, uint32_t rsiz, bool detect_ignore, const char *file, const char *func, const int line)
{
	int bank, i;
	uint32_t pos;
	struct spi_ioc_transfer tran;

	memset(&tran, 0, sizeof(tran));
	tran.delay_usecs = 0;
	tran.speed_hz = MINION_SPI_SPEED;

// TODO:

	pos = 0;
	while (wsiz > 0) {
		tran.tx_buf = wbuf;
		tran.rx_buf = rbuf;
		tran.speed_hz = MINION_SPI_SPEED;
		if (wsiz < MINION_SPI_BUFSIZ)
			tran.len = wsiz;
		else
			tran.len = MINION_SPI_BUFSIZ;

		if (ioctl(minioninfo->spifd, SPI_IOC_MESSAGE(1), (void *)&tran) < 0) {
			if (!detect_ignore || errno != 110) {
				applog(LOG_ERR, "%s%d: ioctl failed err=%d" MINION_FFL,
						minioncgpu->drv->name, minioncgpu->device_id,
						errno, MINION_FFL_PASS);
			}
			return false;
		}

		wsiz -= tran.len;
		wbuf += tran.len;
		rbuf += tran.len;
		pos += tran.len;
	}
	return true;
}

static bool do_ioctl(struct cgpu_info *minioncgpu, struct minion_info *minioninfo, ... )
{
	if (ioctl(babinfo->spifd, SPI_IOC_MESSAGE(1), (void *)&tran) < 0) {
		if (!detect_ignore || errno != 110) {
			applog(LOG_ERR, "%s%d: ioctl failed err=%d" BAB_FFL,
			babcgpu->drv->name, babcgpu->device_id,
			errno, BAB_FFL_PASS);
		}
		return false;
	}
zzzzz

}

void minion_detect_chips(struct cgpu_info *minioncgpu)
{
	// TODO:
}

static const char *minion_modules[] = {
	"i2c-dev",
	"i2c-bcm2708",
	"spidev",
	"spi-bcm2708",
	NULL
};

static struct {
	int request;
	int value;
} minion_ioc[] = {
	{ SPI_IOC_RD_MODE, 0 }, // ?
	{ SPI_IOC_WR_MODE, 0 }, // ?
	{ SPI_IOC_RD_BITS_PER_WORD, 8 }, // 32? 8?
	{ SPI_IOC_WR_BITS_PER_WORD, 8 }, // 32? 8?
	{ SPI_IOC_RD_MAX_SPEED_HZ, 1000000 }, // 3000000 ?
	{ SPI_IOC_WR_MAX_SPEED_HZ, 1000000 }, // 3000000 ?
	{ -1, -1 }
};

static bool minion_init_spi(struct cgpu_info *minioncgpu, struct minion_info *minioninfo, int bus, int chip)
{
	int i, err, memfd, data;
	char buf[64];

	for (i = 0; minion_modules[i]; i++) {
		snprintf(buf, sizeof(buf), "modprobe %s", minion_modules[i]);
		err = system(buf);
		if (err) {
			applog(LOG_ERR, "%s failed to modprobe %s (%d) - you need to be root?",
					minioncgpu->drv->dname,
					minion_modules[i], err);
			goto bad_out;
		}
	}

	snprintf(buf, sizeof(buf), "/dev/spidev%d.%d", bus, chip);
	minioninfo->spifd = open(buf, O_RDWR);
	if (minioninfo->spifd < 0) {
		applog(LOG_ERR, "%s failed to open spidev (%d)",
				minioncgpu->drv->dname,
				errno);
		goto bad_out;
	}

	minioncgpu->device_path = strdup(buf);

	// TODO: initialise speed info

	return true;

close_out:
	close(minioninfo->spifd);
	minioninfo->spifd = 0;
	free(minioncgpu->device_path);
	minioncgpu->device_path = NULL;
bad_out:
	return false;
}

static void minion_detect(bool hotplug)
{
	struct cgpu_info *minioncgpu = NULL;
	struct minion_info *minioninfo = NULL;
	int i;

	if (hotplug)
		return;

	minioncgpu = calloc(1, sizeof(*minioncgpu));
	if (unlikely(!minioncgpu))
		quithere(1, "Failed to calloc minioncgpu");

	minioncgpu->drv = &minion_drv;
	minioncgpu->deven = DEV_ENABLED;
	minioncgpu->threads = 1;

	minioninfo = calloc(1, sizeof(*minioninfo));
	if (unlikely(!minioninfo))
		quithere(1, "Failed to calloc minioninfo");
	minioncgpu->device_data = (void *)minioninfo;

	if (!minion_init_spi(minioncgpu, minioninfo, MINION_SPI_BUS, MINION_SPI_CHIP))
		goto unalloc;

	applog(LOG_WARNING, "%s checking for chips ...", minioncgpu->drv->dname);

	minion_detect_chips(minioncgpu, minioninfo);

	applog(LOG_WARNING, "%s found %d chips", minioncgpu->drv->dname, minioninfo->chips);

	if (minioninfo->chips == 0)
		goto cleanup;

	if (!add_cgpu(minioncgpu))
		goto cleanup;

	minioninfo->wfree_list = new_list("Work", sizeof(WITEM), ALLOC_WITEMS, true);
	minioninfo->wwork_list = new_store(minioninfo->wfree_list, true);
	for (i = 0; i < minioninfo->chips; i++)
		minioninfo->wchip_list[i] = new_store(minioninfo->wfree_list, true);

	minioninfo->tfree_list = new_list("Task", sizeof(TITEM), ALLOC_TITEMS, true);
	minioninfo->task_list = new_store(minioninfo->tfree_list, true);

	minioninfo->rfree_list = new_list("Reply", sizeof(RITEM), ALLOC_RITEMS, true);
	minioninfo->rnonce_list = new_store(minioninfo->rfree_list, true);

	minioninfo->initialised = true;

	return;

cleanup:
	close(minioninfo->spifd);
unalloc:
	free(minioninfo);
	free(minioncgpu);
}

static void minion_identify(__maybe_unused struct cgpu_info *minioncgpu)
{
}

#define MINION_POLL_uS 3000
#define MINION_TASK_uS 29000
#define MINION_REPLY_uS 29000

/*
 * SPI/ioctl thread
 * Concept:
 * Poll task queue every POLL_uS to see if a new task is waiting to be sent
 *  TODO: use a timeout cgsem_wait instead
 * If the new task isn't urgent, then don't send it unless it's TASK_uS since last send
 * Non urgent work is to keep a queue full
 * Urgent work is when an LP occurs (or a queue is empty due to running out)
 * Also, an ioctl must be done every REPLY_uS checking for results
 */
static void *minion_spi(void *userdata)
{
	struct cgpu_info *minioncgpu = (struct cgpu_info *)userdata;
	struct minion_info *minioninfo = (struct minion_info *)(minioncgpu->device_data);
	struct timeval start, stop;
	bool do_task, do_reply;
	double wait;

	applog(LOG_DEBUG, "%s%i: SPIing...",
			  minioncgpu->drv->name, minioncgpu->device_id);

	// Wait until we're ready
	while (minioncgpu->shutdown == false) {
		if (minioninfo->initialised) {
			break;
		}
		cgsleep_ms(3);
	}

	cgtime(&start);
	while (minioncgpu->shutdown == false) {
		do_task = do_reply = false;
		cgtime(&stop);
		wait = us_tdiff(&stop, &start);
		if (wait >= MINION_TASK_uS)
			do_task = true;
		if (wait >= MINION_REPLY_uS)
			do_reply = true;

		mutex_lock(&(minioninfo->task_lock));
// should I check each task ...
// no - since LP should remove all old tasks thus the tail will be urgent
		task = minioninfo->task_tail;
		if (task->urgent)
			do_task = true;
		if (!do_task)
			task = null;
		else {
			unlink task/tail
		}
		mutex_unlock(&(minioninfo->task_lock));

		if (do_task || do_reply) {
			if (do_task)
				setup task;
			n.b. we always check for replies
			minion_txrx(buf, minioninfo->buf_used[buf], false);
			cgtime(&start);

			process reply
			do_ioctl(minioncgpu, minioninfo, .......);
		}

// TODO: rather than polling - use a timeout cgsem_wait - work creation would notify the sem
//	but only urgent work
// Timeout would be min(TASK_uS, REPLY_uS)

		cgsleep_us(MINION_POLL_uS);
	}

	return NULL;
}

static void minion_flush_work(struct cgpu_info *minioncgpu)
{
	struct minion_info *minioninfo = (struct minion_info *)(minioncgpu->device_data);

	applog(LOG_DEBUG, "%s%i: flushing work",
			  minioncgpu->drv->name, minioncgpu->device_id);

	K_WLOCK(minioninfo->wwork_list);
	add task flush chip (or a flag to say to flush it) - a flag will avoid an extra lock
	for (i = 0; i < minioninfo->chips; i++)
		minioninfo->wchip_list[i]->stale_count = minioninfo->wchip_list[i]->count;
	discard wwork_list
	K_WUNLOCK(minioninfo->wwork_list);

	maybe send a signal to force sending new work - needs cgsem_wait in the sending thread
}

/*
 * Find the matching work item
 * Discard any work items older than a match
 */
static bool oknonce(struct thr_info *thr, struct cgpu_info *minioncgpu, int chip, int core, uint32_t task_id, uint32_t nonce)
{
	struct minion_info *minioninfo = (struct minion_info *)(minioncgpu->device_data);
	K_ITEM *item, *tail;

	minioninfo->chip_nonces[chip]++;

	/*
	 * TODO: change this to start at the tail
	 * and find the work while still in the lock
	 * N.B. the head is OK to follow ->next without a lock
	 */
	K_RLOCK(minioninfo->wfree_list->lock);
	item = minioninfo->wchip_list[chip];
	K_RUNLOCK(minioninfo->wfree_list->lock);

	if (!item) {
		applog(LOG_ERR, "%s%i: chip %d no work (for task 0x%04x)",
				minioncgpu->drv->name, minioncgpu->device_id,
				chip, (int)task_id);
		minioninfo->untested_nonces++;
		return false;
	}

	while (item) {
		if (item->task_id == task_id)
			break;
		item = item->next;
	}


	if (!item) {
		applog(LOG_ERR, "%s%i: chip %d unknown work task 0x%04x",
				minioncgpu->drv->name, minioncgpu->device_id,
				chip, (int)task_id);
		minioninfo->untested_nonces++;
		return false;
	}

	minioninfo->tested_nonces++;

	if (test_nonce(item->work, nonce)) {
		submit_tested_work(thr, item->work);

		minioninfo->chip_good[chip]++;
		minioninfo->core_good[chip][core]++;
		item->nonces++;
		minioninfo->new_nonces++;
		minioninfo->ok_nonces++;

		if (item->next) {
			K_WLOCK(minioninfo->wfree_list->lock);

			while (minioninfo->wchip_list[chip].tail != item) {
				tail = k_remove(minioninfo->wchip_list[chip], minioninfo->wchip_list[chip].tail);
				K_WUNLOCK(minioninfo->wfree_list->lock);
				work_completed(minioncgpu, WDATA(tail)->work);
				DATAW(tail)->work = NULL;
				DATAW(tail)->task_id = 0;
				DATAW(tail)->nonces = 0;
				DATAW(tail)->urgent = false;
				K_WLOCK(minioninfo->wfree_list->lock);
				k_add_head(minioninfo->wfree_list, tail);
			}

			K_WUNLOCK(minioninfo->witem_lock);
		}

		return true;
	}

	minioninfo->chip_bad[chip]++;
	minioninfo->core_bad[chip][core]++;
	inc_hw_errors(thr);

	return false;
}

// Results checking thread
static void *minion_res(void *userdata)
{
	struct cgpu_info *minioncgpu = (struct cgpu_info *)userdata;
	struct minion_info *minioninfo = (struct minion_info *)(minioncgpu->device_data);
	struct thr_info *thr = minioncgpu->thr[0];
	int chip, core;
	uint32_t task_id;
	uint32_t nonce;
	bool no_nonce;

	applog(LOG_DEBUG, "%s%i: Results...",
			  minioncgpu->drv->name, minioncgpu->device_id);

	// Wait until we're ready
	while (minioncgpu->shutdown == false) {
		if (minioninfo->initialised) {
			break;
		}
		cgsleep_ms(3);
	}

	while (minioncgpu->shutdown == false) {
// TODO: rather than polling - use a cgsem_wait like in api.c
		if (!oldest_nonce(minioncgpu, &chip, &core, &task_id, &nonce, &no_nonce)) {
			cgsleep_ms(3);
			continue;
		}

		oknonce(thr, minioncgpu, chip, core, task_id, nonce);
	}

	return NULL;
}

static void new_task(struct cgpu_info *minioncgpu, work *work, int chip, bool urgent)
{
	struct minion_info *minioninfo = (struct minion_info *)(minioncgpu->device_data);
	K_ITEM *item;

	K_WLOCK(minioninfo->tfree_list);

	item = k_get_head(minioninfo->tfree_list);

	DATAT(item)->work = work;
	DATAT(item)->task_id = minioninfo->next_task_id;
	minioninfo->next_task_id = (minioninfo->next_task_id + 1) & MINION_MAX_TASK_ID;
	DATAT(item)->chip = chip;
	DATAT(item)->urgent = urgent;

	k_add_head(minioninfo->task_list, item);

	K_WUNLOCK(minioninfo->tfree_list);
}

#define MINION_CHIP_HIGH 15
#define MINION_CHIP_LOW 5

static void minion_do_work(struct cgpu_info *minioncgpu)
{
	struct minion_info *minioninfo = (struct minion_info *)(minioncgpu->device_data);
	int busy, newbusy, match, work_items = 0;
	int count;
	int spi, mis, miso;
	int state, i, j;
	BLIST *bitem;
	bool res, got_a_nonce;

	/*
	 * Fill the queues as follows:
	 *	1) put at least 1 in each queue
	 *	2) push each queue up to LOW
	 *	3) push each LOW queue up to HIGH
	 */
	for (state = 0; state < 3; state++) {
		for (i = 0; i < minioninfo->chips; i++) {
			K_RLOCK(minioninfo->wchip_list[i]->lock);
			count = minioninfo->wchip_list[i]->count - minioninfo->wchip_list[i]->stale_count;
			K_RUNLOCK(minioninfo->wchip_list[i]->lock);

			switch (state) {
				case 0:
					if (count == 0) {
						item = next_work(minioncgpu, i);
						if (item)
							new_task(minioncgpu, WDATA(item)->work, i, true);
						else {
							applog(LOG_ERR, "%s%i: urgent empty work list (%i)",
									minioncgpu->drv->name,
									minioncgpu->device_id, i);
						}
					}
					break;
				case 1:
					if (count < MINION_CHIP_LOW) {
						for (j = count; j < MINION_CHIP_LOW; j++) {
							item = next_work(minioncgpu, i);
							if (item)
								new_task(minioncgpu, WDATA(item)->work, i, false);
							else {
								applog(LOG_ERR, "%s%i: non-urgent lo empty work list (%i)",
										minioncgpu->drv->name,
										minioncgpu->device_id, i);
							}
						}
					}
					break;
				case 2:
					if (count <= MINION_CHIP_LOW) {
						for (j = count; j < MINION_CHIP_HIGH; j++) {
							item = next_work(minioncgpu, i);
							if (item)
								new_task(minioncgpu, WDATA(item)->work, i, false);
							else {
								applog(LOG_ERR, "%s%i: non-urgent hi empty work list (%i)",
										minioncgpu->drv->name,
										minioncgpu->device_id, i);
							}
						}
					}
					break;
			}
		}
	}
}

static bool minion_thread_prepare(struct thr_info *thr)
{
	struct cgpu_info *minioncgpu = thr->cgpu;
	struct minion_info *minioninfo = (struct minion_info *)(minioncgpu->device_data);

	/*
	 * SPI/ioctl thread 
	 */
	if (thr_info_create(&(minioninfo->spi_thr), NULL, minion_spi, (void *)minioncgpu)) {
		applog(LOG_ERR, "%s%i: SPI thread create failed",
				minioncgpu->drv->name, minioncgpu->device_id);
		return false;
	}
	pthread_detach(minioninfo->spi_thr.pth);

	/*
	 * Seperate results checking thread so ioctl timing can ignore the results checking
	 */
	if (thr_info_create(&(minioninfo->res_thr), NULL, minion_res, (void *)minioncgpu)) {
		applog(LOG_ERR, "%s%i: Results thread create failed",
				minioncgpu->drv->name, minioncgpu->device_id);
		return false;
	}
	pthread_detach(minioninfo->res_thr.pth);

	return true;
}

static void minion_shutdown(struct thr_info *thr)
{
	struct cgpu_info *minioncgpu = thr->cgpu;
	struct minion_info *minioninfo = (struct minion_info *)(minioncgpu->device_data);
	int i;

	applog(LOG_DEBUG, "%s%i: shutting down",
			  minioncgpu->drv->name, minioncgpu->device_id);

	for (i = 0; i < minioninfo->chips; i++)
// TODO:	minion_shutdown(minioncgpu, minioninfo, i);
		;

	minioncgpu->shutdown = true;
}

static bool minion_queue_full(struct cgpu_info *minioncgpu)
{
	struct minion_info *minioninfo = (struct minion_info *)(minioncgpu->device_data);
	struct work *work;
	int count;
	bool ret;

	K_RLOCK(minioninfo->wwork_list);
	count = minioninfo->wwork_list->count;
	K_RUNLOCK(minioninfo->wwork_list);

	if (count >= (MINION_CHIP_HIGH * minioncgpu->chips))
		ret = true;
	else {
		work = get_queued(minioncgpu);
		if (work)
			ready_work(minioncgpu, work);
		else
			// Avoid a hard loop when we can't get work fast enough
			cgsleep_ms(3);

		ret = false;
	}

	return ret;
}

static int64_t minion_scanwork(__maybe_unused struct thr_info *thr)
{
	struct cgpu_info *minioncgpu = thr->cgpu;
	struct minion_info *minioninfo = (struct minion_info *)(minioncgpu->device_data);
	int64_t hashcount = 0;

	minion_do_work(minioncgpu);

	cgsleep_ms(3); // May need to be longer

	if (minioninfo->new_nonces) {
		hashcount += 0xffffffffull * minioninfo->new_nonces;
		minioninfo->new_nonces = 0;
	}

	return hashcount;
}

#define CHIPS_PER_STAT 16

static struct api_data *minion_api_stats(struct cgpu_info *minioncgpu)
{
	struct minion_info *minioninfo = (struct minion_info *)(minioncgpu->device_data);
	struct api_data *root = NULL;
	char data[2048];
	char buf[32];
	int i, to, j;

	if (minioninfo->initialised == false)
		return NULL;

	root = api_add_uint64(root, "OK Nonces", &(minioninfo->ok_nonces), true);
	root = api_add_uint64(root, "New Nonces", &(minioninfo->new_nonces), true);
	root = api_add_uint64(root, "Tested Nonces", &(minioninfo->tested_nonces), true);
	root = api_add_uint64(root, "Untested Nonces", &(minioninfo->untested_nonces), true);

	root = api_add_int(root, "Chips", &(minioninfo->chips), true);

	for (i = 0; i < minioninfo->chips; i += CHIPS_PER_STAT) {
		to = i + CHIPS_PER_STAT - 1;
		if (to >= minioninfo->chips)
			to = minioninfo->chips - 1;

		data[0] = '\0';
		for (j = i; j <= to; j++) {
			snprintf(buf, sizeof(buf),
					"%s%"PRIu64,
					j == i ? "" : " ",
					minioninfo->chip_nonces[j]);
			strcat(data, buf);
		}
		snprintf(buf, sizeof(buf), "Nonces %d - %d", i, to);
		root = api_add_string(root, buf, data, true);

		data[0] = '\0';
		for (j = i; j <= to; j++) {
			snprintf(buf, sizeof(buf),
					"%s%"PRIu64,
					j == i ? "" : " ",
					minioninfo->chip_good[j]);
			strcat(data, buf);
		}
		snprintf(buf, sizeof(buf), "Good %d - %d", i, to);
		root = api_add_string(root, buf, data, true);

		data[0] = '\0';
		for (j = i; j <= to; j++) {
			snprintf(buf, sizeof(buf),
					"%s%"PRIu64,
					j == i ? "" : " ",
					minioninfo->chip_bad[j]);
			strcat(data, buf);
		}
		snprintf(buf, sizeof(buf), "Bad %d - %d", i, to);
		root = api_add_string(root, buf, data, true);

		data[0] = '\0';
		for (j = i; j <= to; j++) {
			snprintf(buf, sizeof(buf),
					"%s0x%02x",
					j == i ? "" : " ",
					(int)(minioninfo->chip_conf[j]));
			strcat(data, buf);
		}
		snprintf(buf, sizeof(buf), "Conf %d - %d", i, to);
		root = api_add_string(root, buf, data, true);

		data[0] = '\0';
		for (j = i; j <= to; j++) {
			snprintf(buf, sizeof(buf),
					"%s0x%02x",
					j == i ? "" : " ",
					(int)(minioninfo->chip_fast[j]));
			strcat(data, buf);
		}
		snprintf(buf, sizeof(buf), "Fast %d - %d", i, to);
		root = api_add_string(root, buf, data, true);
	}

	root = api_add_int(root, "WFree Total", &(minioninfo->wfree_list->total), true);
	root = api_add_int(root, "WFree Count", &(minioninfo->wfree_list->count), true);
	root = api_add_int(root, "WWork Count", &(minioninfo->wwork_list->count), true);

	root = api_add_int(root, "TFree Total", &(minioninfo->tfree_list->total), true);
	root = api_add_int(root, "Task Count", &(minioninfo->task_list->count), true);

	root = api_add_int(root, "RFree Total", &(minioninfo->rfree_list->total), true);
	root = api_add_int(root, "RFree Count", &(minioninfo->rfree_list->count), true);
	root = api_add_int(root, "Result Count", &(minioninfo->rnonce_list->count), true);

	return root;
}
#endif

struct device_drv minion_drv = {
	.drv_id = DRIVER_minion,
	.dname = "Minion BlackArrow",
	.name = "MBA",
	.drv_detect = minion_detect,
#ifdef LINUX
	.get_api_stats = minion_api_stats,
//TODO:	.get_statline_before = get_minion_statline_before,
	.identify_device = minion_identify,
	.thread_prepare = minion_thread_prepare,
	.hash_work = hash_queued_work,
	.scanwork = minion_scanwork,
	.queue_full = minion_queue_full,
	.flush_work = minion_flush_work,
	.thread_shutdown = minion_shutdown
#endif
};
