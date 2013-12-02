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

#define minion_txrx(_task, _ignore) _minion_txrx(minioncgpu, minioninfo, _task, _ignore, MINION_FFL_HERE)

#define MINION_SYS_REGS 0x00
#define MINION_CORE_REGS 0x10
#define MINION_RES_BUF 0x20
#define MINION_CMD_QUE 0x30
#define MINION_NONCE_RANGE 0x70

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

// RES data size is minion_result
#define MINION_RES_DATA 0x20
#define MINION_RES_PEEK 0x21

// QUE data size is minion_que
#define MINION_QUE_0 0x30
#define MINION_QUE_R 0x31

// RANGE data sizes are DATA_SIZ
#define MINION_NONCE_STA 0x70
#define MINION_NONCE_FIN 0x71

#define SET_HEAD_READ(_h, _reg) ((_h)->reg) = ((_reg) & 0x7f)
#define SET_HEAD_WRITE(_h, _reg) ((_h)->reg) = ((_reg) | 0x80)
#define SET_HEAD_SIZ(_h, _siz) \
		do { \
			((_h)->siz)[0] = (uint8_t)((_siz) & 0xff); \
			((_h)->siz)[1] = (uint8_t)(((_siz) & 0xff00) << 8); \
		} while (0)

struct minion_header {
	uint8_t chip;
	uint8_t reg;
	uint8_t siz[2];
	uint8_t data[4]; // placeholder
};

#define HSIZE() (sizeof(struct minion_header) - 4)

#define MINION_CHIP_SIG 0x32020ffa

#define SET_CORE(_core, _n) ((_core)->core[_n << 4] &= (2 >> (_n % 8)))
#define CORE_IDLE(_core, _n) ((_core)->core[_n << 4] & (2 >> (_n % 8)))

struct minion_core {
	uint8_t core[DATA_SIZ];
};

#define RES_GOLD(_res) ((((_res)->status[0]) & 0x80) == 0)
#define RES_CHIP(_res) (((_res)->status[0]) & 0x1f)
#define RES_CORE(_res) ((_res)->status[1])
#define RES_TASK(_res) ((int)((_res)->status[2]) * 0x100 + (int)((_res)->status[2]))
#define RES_NONCE(_res) (*(uint32_t *)(&((_res)->nonce[0])))

struct minion_result {
	uint8_t status[DATA_SIZ];
	uint8_t nonce[DATA_SIZ];
};

#define MIDSTATE_BYTES 32
#define MERKLE7_OFFSET 64
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
	struct timeval sent;
	int nonces;
	bool urgent;
} WITEM;

#define ALLOC_TITEMS 256

typedef struct titem {
	uint8_t chip;
	bool write;
	uint8_t address;
	uint32_t task_id;
	uint32_t siz;
	uint8_t wbuf[1024]; // TODO: tune the size of these 3
	uint8_t obuf[1024];
	uint8_t rbuf[1024];
	int reply;
	bool urgent;
	struct work *work;
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
	const char *name;
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
	struct thr_info spiw_thr;
	struct thr_info spir_thr;
	struct thr_info res_thr;

	pthread_mutex_t spi_lock;

	// TODO: can there be 2x fd's one for each spi thread?
	// or do I need to have one (as current code) and lock access each ioctl?
	int spifd;
	// TODO: need to track disabled chips
	// detect chip scan will need to check all chips
	int chips;
	bool chip[MINION_CHIPS];

	uint32_t next_task_id;

	// Stats
	uint64_t chip_nonces[MINION_CHIPS];
	uint64_t chip_good[MINION_CHIPS];
	uint64_t chip_bad[MINION_CHIPS];
	uint64_t core_good[MINION_CHIPS][MINION_CORES];
	uint64_t core_bad[MINION_CHIPS][MINION_CORES];

	pthread_mutex_t nonce_lock;
	uint64_t new_nonces;

	uint64_t ok_nonces;
	uint64_t untested_nonces;
	uint64_t tested_nonces;

	// Work items
	K_LIST *wfree_list;
	K_STORE *wwork_list;
	K_STORE *wchip_list[MINION_CHIPS];

	// Task list
	K_LIST *tfree_list;
	K_STORE *task_list;
	K_STORE *reply_list;

	// Nonce replies
	K_LIST *rfree_list;
	K_STORE *rnonce_list;

	struct timeval last_did;

	bool initialised;
};

static void alloc_items(K_LIST *list)
{
	K_ITEM *item;
	int i;

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

static K_STORE *new_store(K_LIST *list, bool do_tail)
{
	K_STORE *store;

	store = calloc(1, sizeof(*store));
	if (!store)
		quithere(1, "Failed to calloc store for %s", list->name);

	store->is_store = true;
	store->lock = list->lock;
	store->name = list->name;
	list->do_tail = do_tail;

	return store;
}

static K_LIST *new_list(const char *name, size_t siz, int allocate, bool do_tail)
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

static K_ITEM *k_get_head(K_LIST *list)
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
	DATAW(item)->task_id = 0;
	memset(&(DATAW(item)->sent), 0, sizeof(DATAW(item)->sent));
	DATAW(item)->nonces = 0;
	DATAW(item)->urgent = false;

	k_add_head(minioninfo->wwork_list, item);

	K_WUNLOCK(minioninfo->wfree_list);
}

static bool oldest_nonce(struct cgpu_info *minioncgpu, int *chip, int *core, uint32_t *task_id, uint32_t *nonce, bool *no_nonce)
{
	struct minion_info *minioninfo = (struct minion_info *)(minioncgpu->device_data);
	K_ITEM *item = NULL;
	bool found = false;

	K_WLOCK(minioninfo->rnonce_list);

	item = minioninfo->rnonce_list->tail;
	if (item) {
		// unlink from res
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

#define MINION_UNKNOWN_TASK -999
#define MINION_OVERSIZE_TASK -998

static int do_ioctl(struct minion_info *minioninfo, uintptr_t wbuf, uint32_t wsiz, uintptr_t rbuf)
{
	struct spi_ioc_transfer tran;
	int ret;

	memset(&tran, 0, sizeof(tran));
	if (wsiz < MINION_SPI_BUFSIZ)
		tran.len = wsiz;
	else
		return MINION_OVERSIZE_TASK;
	tran.delay_usecs = 0;
	tran.speed_hz = MINION_SPI_SPEED;

	tran.tx_buf = wbuf;
	tran.rx_buf = rbuf;
	tran.speed_hz = MINION_SPI_SPEED;

	mutex_lock(&(minioninfo->spi_lock));
	ret = ioctl(minioninfo->spifd, SPI_IOC_MESSAGE(1), (void *)&tran);
	mutex_unlock(&(minioninfo->spi_lock));

	return ret;
}

static bool _minion_txrx(struct cgpu_info *minioncgpu, struct minion_info *minioninfo, TITEM *task, bool detect_ignore, const char *file, const char *func, const int line)
{
	struct minion_header *head;
	uintptr_t obuf, rbuf;
	uint32_t wsiz;

	head = (struct minion_header *)(task->obuf);
	head->chip = task->chip;
	if (task->write)
		SET_HEAD_WRITE(head, task->address);
	else
		SET_HEAD_READ(head, task->address);

	SET_HEAD_SIZ(head, task->siz); // TODO: divide by 4?

	memcpy(&(head->data[0]), task->wbuf, task->siz);

	obuf = (uintptr_t)(&(task->obuf));
	wsiz = HSIZE() + task->siz;
	rbuf = (uintptr_t)(&(task->rbuf));

	task->reply = do_ioctl(minioninfo, obuf, wsiz, rbuf);
//TODO:	if (task->reply < 0 && (!detect_ignore || errno != 110)) {
	if (task->reply < 0) {
		applog(LOG_ERR, "%s%d: ioctl failed err=%d" MINION_FFL,
				minioncgpu->drv->name, minioncgpu->device_id,
				errno, MINION_FFL_PASS);
	}
	return (task->reply >= 0);
}

// Simple detect - just check each chip for the signature
// TODO: retry on failure?
void minion_detect_chips(struct cgpu_info *minioncgpu, struct minion_info *minioninfo)
{
	struct minion_header head;
	uint8_t rbuf[32];
	uint32_t wsiz;
	int chip, reply;

	SET_HEAD_READ(&head, MINION_SYS_CHIP_SIG);
	SET_HEAD_SIZ(&head, 0);
	wsiz = HSIZE();
	for (chip = 0; chip < MINION_CHIPS; chip++) {
		head.chip = (uint8_t)chip;

		reply = do_ioctl(minioninfo, (uintptr_t)(&head), wsiz, (uintptr_t)&(rbuf[0]));
		if (reply == 4) {
			uint32_t sig = rbuf[0] + rbuf[1] * 0x100 + rbuf[2] * 0x10000 + rbuf[3] * 0x1000000;

			if (sig == MINION_CHIP_SIG) {
				minioninfo->chip[chip] = true;
				minioninfo->chips++;
			} else {
				applog(LOG_ERR, "%s: chip %d detect failed got 0x%08x wanted 0x%08x",
						minioncgpu->drv->dname, chip, sig, MINION_CHIP_SIG);
			}
		} else {
			applog(LOG_ERR, "%s: chip %d reply %d ignored",
					minioncgpu->drv->dname, chip, reply);
		}
	}
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
	int i, err, data;
	char buf[64];

	for (i = 0; minion_modules[i]; i++) {
		snprintf(buf, sizeof(buf), "modprobe %s", minion_modules[i]);
		err = system(buf);
		if (err) {
			applog(LOG_ERR, "%s: failed to modprobe %s (%d) - you need to be root?",
					minioncgpu->drv->dname,
					minion_modules[i], err);
			goto bad_out;
		}
	}

	snprintf(buf, sizeof(buf), "/dev/spidev%d.%d", bus, chip);
	minioninfo->spifd = open(buf, O_RDWR);
	if (minioninfo->spifd < 0) {
		applog(LOG_ERR, "%s: failed to open spidev (%d)",
				minioncgpu->drv->dname,
				errno);
		goto bad_out;
	}

	minioncgpu->device_path = strdup(buf);

	for (i = 0; minion_ioc[i].value != -1; i++) {
		data = minion_ioc[i].value;
		err = ioctl(minioninfo->spifd, minion_ioc[i].request, (void *)&data);
		if (err < 0) {
			applog(LOG_ERR, "%s: failed ioctl (%d) (%d)",
					minioncgpu->drv->dname,
					i, errno);
			goto close_out;
		}
	}

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

	mutex_init(&(minioninfo->spi_lock));

	applog(LOG_WARNING, "%s: checking for chips ...", minioncgpu->drv->dname);

	minion_detect_chips(minioncgpu, minioninfo);

	applog(LOG_WARNING, "%s: found %d chips", minioncgpu->drv->dname, minioninfo->chips);

	if (minioninfo->chips == 0)
		goto cleanup;

	if (!add_cgpu(minioncgpu))
		goto cleanup;

	mutex_init(&(minioninfo->nonce_lock));

	minioninfo->wfree_list = new_list("Work", sizeof(WITEM), ALLOC_WITEMS, true);
	minioninfo->wwork_list = new_store(minioninfo->wfree_list, true);
	// Initialise them all in case we later decide to enable chips
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
	mutex_destroy(&(minioninfo->spi_lock));
unalloc:
	free(minioninfo);
	free(minioncgpu);
}

static void minion_identify(__maybe_unused struct cgpu_info *minioncgpu)
{
}

#define MINION_POLL_uS 3000
#define MINION_TASK_uS 29000
#define MINION_REPLY_uS 9000
#define MINION_REPLY_MORE_uS 3000

/*
 * SPI/ioctl write thread
 * Poll task queue every POLL_uS to see if a new task is waiting to be sent
 *  TODO: use a timeout cgsem_wait instead
 * If the new task isn't urgent, then don't send it unless it's TASK_uS since last send
 * Non urgent work is to keep the queue full
 * Urgent work is when an LP occurs (or the queue is empty/low)
 */
static void *minion_spi_write(void *userdata)
{
	struct cgpu_info *minioncgpu = (struct cgpu_info *)userdata;
	struct minion_info *minioninfo = (struct minion_info *)(minioncgpu->device_data);
	struct timeval start, stop;
	K_ITEM *item;
	bool do_task;
	double wait;

	applog(LOG_DEBUG, "%s%i: SPI writing...",
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
		do_task = false;
		cgtime(&stop);
		wait = us_tdiff(&stop, &start);
		if (wait >= MINION_TASK_uS)
			do_task = true;

		K_WLOCK(minioninfo->task_list);

		item = minioninfo->task_list->tail;
		if (item)
			if (DATAT(item)->urgent)
				do_task = true;

		if (do_task)
			k_remove(minioninfo->task_list, item);
		else
			item = NULL;

		K_WUNLOCK(minioninfo->task_list);

		if (item) {
			bool dotxrx = true;

			switch (DATAT(item)->address) {
				// TODO: STA
				// TODO: case MINION_CORE_ENA0_31:
				// TODO: case MINION_CORE_ENA32_63:
				// TODO: case MINION_SYS_RSTN_CTL:
				// TODO: case MINION_SYS_TEMP_CTL:
				// TODO: case MINION_SYS_FREQ_CTL:
				case MINION_QUE_0:
					break;
				default:
					dotxrx = false;
					DATAT(item)->reply = MINION_UNKNOWN_TASK;
					applog(LOG_ERR, "%s%i: Unknown task address 0x%02x",
							minioncgpu->drv->name, minioncgpu->device_id,
							(unsigned int)(DATAT(item)->address));

					break;
			}

			if (dotxrx) {
				cgtime(&start);
				minion_txrx(DATAT(item), false);
			}

			K_WLOCK(minioninfo->reply_list);
			k_add_head(minioninfo->reply_list, item);
			K_WUNLOCK(minioninfo->reply_list);

			// always do the next task if there is one
			continue;
		}

// TODO: rather than polling - use a timeout cgsem_wait - work creation would notify the sem
//	but only urgent work

		cgsleep_us(MINION_POLL_uS);
	}

	return NULL;
}

/*
 * SPI/ioctl reply thread
 * ioctl done every REPLY_uS checking for results
 */
static void *minion_spi_reply(void *userdata)
{
	struct cgpu_info *minioncgpu = (struct cgpu_info *)userdata;
	struct minion_info *minioninfo = (struct minion_info *)(minioncgpu->device_data);
	struct minion_result *result;
	K_ITEM *item;
	TITEM task;

	applog(LOG_DEBUG, "%s%i: SPI replying...",
			  minioncgpu->drv->name, minioncgpu->device_id);

	// Wait until we're ready
	while (minioncgpu->shutdown == false) {
		if (minioninfo->initialised) {
			break;
		}
		cgsleep_ms(3);
	}

	task.chip = 0;
	task.write = false;
	task.address = MINION_RES_DATA;
	task.siz = 0;
	task.urgent = false;
	task.work = NULL;

	while (minioncgpu->shutdown == false) {
		task.reply = 0;
		minion_txrx(&task, false);
		if (task.reply > 0) {
			if (task.reply < (int)sizeof(struct minion_result)) {
				applog(LOG_ERR, "%s%i: Bad work reply size %d should be %d",
						minioncgpu->drv->name, minioncgpu->device_id,
						task.reply, (int)sizeof(struct minion_result));
			} else {
				if (task.reply > (int)sizeof(struct minion_result)) {
					applog(LOG_ERR, "%s%i: Unexpected work reply size %d expected %d",
							minioncgpu->drv->name, minioncgpu->device_id,
							task.reply, (int)sizeof(struct minion_result));
				}
				result = (struct minion_result *)&(task.rbuf[0]);

				K_WLOCK(minioninfo->rfree_list);
				item = k_get_head(minioninfo->rfree_list);
				K_WUNLOCK(minioninfo->rfree_list);

				DATAR(item)->chip = RES_CHIP(result);
				DATAR(item)->core = RES_CORE(result);
				DATAR(item)->task_id = RES_TASK(result);
				DATAR(item)->nonce = RES_NONCE(result);
				DATAR(item)->no_nonce = !RES_GOLD(result);

				K_WLOCK(minioninfo->rnonce_list);
				k_add_head(minioninfo->rnonce_list, item);
				K_WUNLOCK(minioninfo->rnonce_list);

				cgsleep_us(MINION_REPLY_MORE_uS);
				continue;
			}
		}
		cgsleep_us(MINION_REPLY_uS);
	}

	return NULL;
}

/*
 * Find the matching work item
 * Discard any older work items
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
	 * and the tail is OK to follow ->prev back to the head without a lock
	 */
	K_RLOCK(minioninfo->wchip_list[chip]);
	item = minioninfo->wchip_list[chip]->head;
	K_RUNLOCK(minioninfo->wchip_list[chip]);

	if (!item) {
		applog(LOG_ERR, "%s%i: no work (chip %d core %d task 0x%04x)",
				minioncgpu->drv->name, minioncgpu->device_id,
				chip, core, (int)task_id);
		minioninfo->untested_nonces++;
		return false;
	}

	while (item) {
		if (DATAW(item)->task_id == task_id)
			break;
		item = item->next;
	}


	if (!item) {
		applog(LOG_ERR, "%s%i: chip %d core %d unknown work task 0x%04x",
				minioncgpu->drv->name, minioncgpu->device_id,
				chip, core, (int)task_id);
		minioninfo->untested_nonces++;
		return false;
	}

	minioninfo->tested_nonces++;

	if (test_nonce(DATAW(item)->work, nonce)) {
		submit_tested_work(thr, DATAW(item)->work);

		minioninfo->chip_good[chip]++;
		minioninfo->core_good[chip][core]++;
		DATAW(item)->nonces++;

		mutex_lock(&(minioninfo->nonce_lock));
		minioninfo->new_nonces++;
		mutex_unlock(&(minioninfo->nonce_lock));
		minioninfo->ok_nonces++;

		// remove older work items
		if (item->next) {
			K_WLOCK(minioninfo->wchip_list[chip]);
			tail = minioninfo->wchip_list[chip]->tail;
			while (tail && tail != item) {
				k_remove(minioninfo->wchip_list[chip], tail);
				K_WUNLOCK(minioninfo->wchip_list[chip]);
				work_completed(minioncgpu, DATAW(tail)->work);
				K_WLOCK(minioninfo->wchip_list[chip]);
				k_add_head(minioninfo->wfree_list, tail);
				tail = minioninfo->wchip_list[chip]->tail;
			}
			K_WUNLOCK(minioninfo->wchip_list[chip]);
		}

		return true;
	}

	minioninfo->chip_bad[chip]++;
	minioninfo->core_bad[chip][core]++;
	inc_hw_errors(thr);

	return false;
}

// Results checking thread
static void *minion_results(void *userdata)
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
// TODO: rather than polling - use a cgsem_wait in minion_spi_reply() like in api.c
		if (!oldest_nonce(minioncgpu, &chip, &core, &task_id, &nonce, &no_nonce)) {
			cgsleep_ms(3);
			continue;
		}

		oknonce(thr, minioncgpu, chip, core, task_id, nonce);
	}

	return NULL;
}

static void minion_flush_work(struct cgpu_info *minioncgpu)
{
	struct minion_info *minioninfo = (struct minion_info *)(minioncgpu->device_data);
	int i;

	applog(LOG_DEBUG, "%s%i: flushing work",
			  minioncgpu->drv->name, minioncgpu->device_id);

	K_WLOCK(minioninfo->wwork_list);
	// TODO: add task flush chip (or a flag to say to flush it) - a flag will avoid an extra lock
	for (i = 0; i < MINION_CHIPS; i++)
		if (minioninfo->chip[i])
			minioninfo->wchip_list[i]->stale_count = minioninfo->wchip_list[i]->count;
	// TODO: discard wwork_list
	K_WUNLOCK(minioninfo->wwork_list);

	// TODO: maybe send a signal to force sending new work - needs cgsem_wait in the sending thread
}

static void new_work_task(struct cgpu_info *minioncgpu, struct work *work, int chip, bool urgent)
{
	struct minion_info *minioninfo = (struct minion_info *)(minioncgpu->device_data);
	struct minion_que *que;
	K_ITEM *item;

	K_WLOCK(minioninfo->tfree_list);
	item = k_get_head(minioninfo->tfree_list);
	K_WUNLOCK(minioninfo->tfree_list);

	DATAT(item)->chip = chip;
	DATAT(item)->write = true;
	DATAT(item)->address = MINION_QUE_0;

	// if threaded access to new_work_task() is added, this will need locking
	DATAT(item)->task_id = minioninfo->next_task_id;
	minioninfo->next_task_id = (minioninfo->next_task_id + 1) & MINION_MAX_TASK_ID;

	DATAT(item)->urgent = urgent;
	DATAT(item)->work = work;

	que = (struct minion_que *)&(DATAT(item)->wbuf[0]);
	que->task_id[0] = DATAT(item)->task_id & 0xff;
	que->task_id[1] = (DATAT(item)->task_id & 0xff00) << 8;
	memcpy(&(que->midstate[0]), &(work->midstate[0]), MIDSTATE_BYTES);
	memcpy(&(que->merkle7[0]), &(work->data[MERKLE7_OFFSET]), MERKLE_BYTES);
	DATAT(item)->siz = (int)sizeof(*que);

	K_WLOCK(minioninfo->task_list);
	k_add_head(minioninfo->task_list, item);
	K_WUNLOCK(minioninfo->task_list);
}

// TODO: stale work ...
static K_ITEM *next_work(struct minion_info *minioninfo)
{
	K_ITEM *item;

	K_WLOCK(minioninfo->wwork_list);
	item = minioninfo->wwork_list->tail;
	if (item)
		k_remove(minioninfo->wwork_list, item);
	K_WUNLOCK(minioninfo->wwork_list);

	return item;
}

#define MINION_QUE_HIGH 15
#define MINION_QUE_LOW 5

static void minion_do_work(struct cgpu_info *minioncgpu)
{
	struct minion_info *minioninfo = (struct minion_info *)(minioncgpu->device_data);
	int count;
	int state, i, j;
	K_ITEM *item;

	/*
	 * Fill the queues as follows:
	 *	1) put at least 1 in each queue
	 *	2) push each queue up to LOW
	 *	3) push each LOW queue up to HIGH
	 */
	for (state = 0; state < 2; state++) {
		for (i = 0; i < MINION_CHIPS; i++) {
			if (minioninfo->chip[i]) {
				K_RLOCK(minioninfo->wchip_list[i]);
				count = minioninfo->wchip_list[i]->count - minioninfo->wchip_list[i]->stale_count;
				K_RUNLOCK(minioninfo->wchip_list[i]);

				switch (state) {
					case 0:
						if (count == 0) {
							item = next_work(minioninfo);
							if (item)
								new_work_task(minioncgpu, DATAW(item)->work, i, true);
							else {
								applog(LOG_ERR, "%s%i: chip %d urgent empty work list",
										minioncgpu->drv->name,
										minioncgpu->device_id,
										i);
							}
						}
						break;
					case 1:
						if (count < MINION_QUE_LOW) {
							for (j = count; j < MINION_QUE_LOW; j++) {
								item = next_work(minioninfo);
								if (item)
									new_work_task(minioncgpu, DATAW(item)->work, i, false);
								else {
									applog(LOG_ERR, "%s%i: chip %d non-urgent lo empty work list (count=%d)",
											minioncgpu->drv->name,
											minioncgpu->device_id,
											i, j);
								}
							}
						}
						break;
					case 2:
						if (count <= MINION_QUE_LOW) {
							for (j = count; j < MINION_QUE_HIGH; j++) {
								item = next_work(minioninfo);
								if (item)
									new_work_task(minioncgpu, DATAW(item)->work, i, false);
								else {
									applog(LOG_ERR, "%s%i: chip %d non-urgent hi empty work list (count=%d)",
											minioncgpu->drv->name,
											minioncgpu->device_id,
											i, j);
								}
							}
						}
						break;
				}
			}
		}
	}
}

static bool minion_thread_prepare(struct thr_info *thr)
{
	struct cgpu_info *minioncgpu = thr->cgpu;
	struct minion_info *minioninfo = (struct minion_info *)(minioncgpu->device_data);

	/*
	 * SPI/ioctl write thread
	 */
	if (thr_info_create(&(minioninfo->spiw_thr), NULL, minion_spi_write, (void *)minioncgpu)) {
		applog(LOG_ERR, "%s%i: SPI write thread create failed",
				minioncgpu->drv->name, minioncgpu->device_id);
		return false;
	}
	pthread_detach(minioninfo->spiw_thr.pth);

	/*
	 * SPI/ioctl results thread
	 */
	if (thr_info_create(&(minioninfo->spir_thr), NULL, minion_spi_reply, (void *)minioncgpu)) {
		applog(LOG_ERR, "%s%i: SPI reply thread create failed",
				minioncgpu->drv->name, minioncgpu->device_id);
		return false;
	}
	pthread_detach(minioninfo->spir_thr.pth);

	/*
	 * Seperate results checking thread so ioctl timing can ignore the results checking
	 */
	if (thr_info_create(&(minioninfo->res_thr), NULL, minion_results, (void *)minioncgpu)) {
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
		if (minioninfo->chip[i])
// TODO:		minion_shutdown(minioncgpu, minioninfo, i);
			i = i;

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

	if (count >= (MINION_QUE_HIGH * minioninfo->chips))
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

	mutex_lock(&(minioninfo->nonce_lock));
	if (minioninfo->new_nonces) {
		hashcount += 0xffffffffull * minioninfo->new_nonces;
		minioninfo->new_nonces = 0;
	}
	mutex_unlock(&(minioninfo->nonce_lock));

	return hashcount;
}

#define CHIPS_PER_STAT 8

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
					"%s%d",
					j == i ? "" : " ",
					minioninfo->chip[j] ? 1 : 0);
			strcat(data, buf);
		}
		snprintf(buf, sizeof(buf), "Detected %02d - %02d", i, to);
		root = api_add_string(root, buf, data, true);

		data[0] = '\0';
		for (j = i; j <= to; j++) {
			snprintf(buf, sizeof(buf),
					"%s%8"PRIu64,
					j == i ? "" : " ",
					minioninfo->chip_nonces[j]);
			strcat(data, buf);
		}
		snprintf(buf, sizeof(buf), "Nonces %02d - %02d", i, to);
		root = api_add_string(root, buf, data, true);

		data[0] = '\0';
		for (j = i; j <= to; j++) {
			snprintf(buf, sizeof(buf),
					"%s%8"PRIu64,
					j == i ? "" : " ",
					minioninfo->chip_good[j]);
			strcat(data, buf);
		}
		snprintf(buf, sizeof(buf), "Good %02d - %02d", i, to);
		root = api_add_string(root, buf, data, true);

		data[0] = '\0';
		for (j = i; j <= to; j++) {
			snprintf(buf, sizeof(buf),
					"%s%8"PRIu64,
					j == i ? "" : " ",
					minioninfo->chip_bad[j]);
			strcat(data, buf);
		}
		snprintf(buf, sizeof(buf), "Bad %02d - %02d", i, to);
		root = api_add_string(root, buf, data, true);
	}

	root = api_add_int(root, "WFree Total", &(minioninfo->wfree_list->total), true);
	root = api_add_int(root, "WFree Count", &(minioninfo->wfree_list->count), true);
	root = api_add_int(root, "WWork Count", &(minioninfo->wwork_list->count), true);

	root = api_add_int(root, "TFree Total", &(minioninfo->tfree_list->total), true);
	root = api_add_int(root, "TFree Count", &(minioninfo->tfree_list->count), true);
	root = api_add_int(root, "Task Count", &(minioninfo->task_list->count), true);
	root = api_add_int(root, "Reply Count", &(minioninfo->reply_list->count), true);

	root = api_add_int(root, "RFree Total", &(minioninfo->rfree_list->total), true);
	root = api_add_int(root, "RFree Count", &(minioninfo->rfree_list->count), true);
	root = api_add_int(root, "RNonce Count", &(minioninfo->rnonce_list->count), true);

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
