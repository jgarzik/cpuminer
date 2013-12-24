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
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <poll.h>

#define MINION_SPI_BUS 0
#define MINION_SPI_CHIP 0

#define MINION_SPI_SPEED 96000
#define MINION_SPI_BUFSIZ 1024

#define MINION_CHIPS 32
#define MINION_CORES 64

#define MINION_FFL " - from %s %s() line %d"
#define MINION_FFL_HERE __FILE__, __func__, __LINE__
#define MINION_FFL_PASS file, func, line
#define MINION_FFL_ARGS __maybe_unused const char *file, \
			__maybe_unused const char *func, \
			__maybe_unused const int line

#define minion_txrx(_task, _ignore) _minion_txrx(minioncgpu, minioninfo, _task, _ignore, MINION_FFL_HERE)
#define do_ioctl(_obuf, _osiz, _rbuf, _rsiz) _do_ioctl(minioninfo, _obuf, _osiz, _rbuf, _rsiz, MINION_FFL_HERE)

#define MINION_SYS_REGS 0x00
#define MINION_CORE_REGS 0x10
#define MINION_RES_BUF 0x20
#define MINION_CMD_QUE 0x30
#define MINION_NONCE_RANGES 0x70

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

// All SYS data sizes are DATA_SIZ
#define MINION_SYS_SIZ DATA_SIZ

#define MINION_GPIO_RESULT_INT_PIN 24

#define MINION_GPIO_SYS "/sys/class/gpio"
#define MINION_GPIO_ENA "/export"
#define MINION_GPIO_ENA_VAL "%d"
#define MINION_GPIO_DIS "/unexport"
#define MINION_GPIO_PIN "/gpio%d"
#define MINION_GPIO_DIR "/direction"
#define MINION_GPIO_DIR_READ "in"
#define MINION_GPIO_DIR_WRITE "out"
#define MINION_GPIO_EDGE "/edge"
#define MINION_GPIO_EDGE_NONE "none"
#define MINION_GPIO_EDGE_RISING "rising"
#define MINION_GPIO_EDGE_FALLING "falling"
#define MINION_GPIO_EDGE_BOTH "both"
#define MINION_GPIO_ACT "/active_low"
#define MINION_GPIO_ACT_LO "1"
#define MINION_GPIO_ACT_HI "0"
#define MINION_GPIO_VALUE "/value"

#define MINION_RESULT_INT 0x01
#define MINION_RESULT_FULL_INT 0x02
#define MINION_CMD_INT 0x04
#define MINION_CMD_FULL_INT 0x08
#define MINION_TEMP_LOW_INT 0x10
#define MINION_TEMP_HI_INT 0x20
#define MINION_ALL_INT  MINION_RESULT_INT | \
			MINION_RESULT_FULL_INT | \
			MINION_CMD_INT | \
			MINION_CMD_FULL_INT | \
			MINION_TEMP_LOW_INT | \
			MINION_TEMP_HI_INT

// Number of results to make a GPIO5 interrupt
#define MINION_RESULT_INT_SIZE 1

#define RSTN_CTL_RESET_CORES 0x01
#define RSTN_CTL_FLUSH_RESULTS 0x02
#define RSTN_CTL_FLUSH_CMD_QUEUE 0x04
#define RSTN_CTL_SPI_SW_RSTN 0x08
#define RSTN_CTL_SHA_MGR_RESET 0x10

// Init
#define SYS_RSTN_CTL_INIT (RSTN_CTL_RESET_CORES | \
				RSTN_CTL_FLUSH_RESULTS | \
				RSTN_CTL_FLUSH_CMD_QUEUE | \
				RSTN_CTL_SPI_SW_RSTN | \
				RSTN_CTL_SHA_MGR_RESET)

// LP
#define SYS_RSTN_CTL_FLUSH (RSTN_CTL_RESET_CORES | \
				RSTN_CTL_SPI_SW_RSTN | \
				RSTN_CTL_FLUSH_CMD_QUEUE)

// enable 'no nonce' report
#define SYS_MISC_CTL_DEFAULT 0x04

// CORE data size is DATA_SIZ
#define MINION_CORE_ENA0_31 0x10
#define MINION_CORE_ENA32_63 0x11
#define MINION_CORE_ACT0_31 0x14
#define MINION_CORE_ACT32_63 0x15

// All CORE data sizes are DATA_SIZ
#define MINION_CORE_SIZ DATA_SIZ

// RES data size is minion_result
#define MINION_RES_DATA 0x20
#define MINION_RES_PEEK 0x21

// QUE data size is minion_que
#define MINION_QUE_0 0x30
#define MINION_QUE_R 0x31

// RANGE data sizes are DATA_SIZ
#define MINION_NONCE_STA 0x70
#define MINION_NONCE_RANGE 0x71

// This must be >= max txsiz + max rxsiz
#define MINION_BUFSIZ 1024

#define u8tou32(_c, _off) (((uint8_t *)(_c))[(_off)+0] + \
			   ((uint8_t *)(_c))[(_off)+1] * 0x100 + \
			   ((uint8_t *)(_c))[(_off)+2] * 0x10000 + \
			   ((uint8_t *)(_c))[(_off)+3] * 0x1000000 )

#define MINION_ADDR_WRITE 0x7f
#define MINION_ADDR_READ 0x80

#define READ_ADDR(_reg) ((_reg) | MINION_ADDR_READ)
#define WRITE_ADDR(_reg) ((_reg) & MINION_ADDR_WRITE)

#define IS_ADDR_READ(_reg) (((_reg) & MINION_ADDR_READ) == MINION_ADDR_READ)
#define IS_ADDR_WRITE(_reg) (((_reg) & MINION_ADDR_READ) == 0)

#define SET_HEAD_WRITE(_h, _reg) ((_h)->reg) = WRITE_ADDR(_reg)
#define SET_HEAD_READ(_h, _reg) ((_h)->reg) = READ_ADDR(_reg)
#define SET_HEAD_SIZ(_h, _siz) \
		do { \
			((_h)->siz)[0] = (uint8_t)((_siz) & 0xff); \
			((_h)->siz)[1] = (uint8_t)(((_siz) & 0xff00) >> 8); \
		} while (0)

struct minion_header {
	uint8_t chip;
	uint8_t reg;
	uint8_t siz[2];
	uint8_t data[4]; // placeholder
};

#define HSIZE() (sizeof(struct minion_header) - 4)

#define MINION_NOCHIP_SIG 0x00000000
#define MINION_CHIP_SIG 0x32020ffa

// TODO: Finding these means the chip is there - but how to fix it?
// Also, code can of course generate them from MINION_CHIP_SIG
#define MINION_CHIP_SIG_SHIFT1 0x0ffa0000
#define MINION_CHIP_SIG_SHIFT2 0x020ffa00
#define MINION_CHIP_SIG_SHIFT3 0x0032020f
#define MINION_CHIP_SIG_SHIFT4 0x00003202

/*
 * Number of times to try and get the SIG with each chip,
 * if the chip returns neither of the above values
 * TODO: maybe need some reset between tries, to handle an offset value?
 */
#define MINION_SIG_TRIES 3

#define STA_TEMP(_sta) ((uint16_t)((_sta)[3] & 0x1f))
#define STA_CORES(_sta) ((uint16_t)((_sta)[2]))
#define STA_FREQ(_sta) ((uint32_t)((_sta)[1]) * 0x100 + (uint32_t)((_sta)[0]))

// Randomly between 1s and 2s per chip
#define MINION_STATS_UPDATE_TIME_MS 1000
#define MINION_STATS_UPDATE_RAND_MS 1000

struct minion_status {
	uint16_t temp;
	uint16_t cores;
	uint32_t freq;
	struct timeval last;
};

// TODO: untested/unused
#define ENABLE_CORE(_core, _n) ((_core)->core[_n >> 4] |= (1 << (_n % 8)))
#define CORE_IDLE(_core, _n) ((_core)->core[_n >> 4] & (1 << (_n % 8)))

#define FIFO_RES(_fifo, _off) ((_fifo)[(_off) + 0])

#define RES_GOLD(_res) ((((_res)->status[3]) & 0x80) == 0)
#define RES_CHIP(_res) (((_res)->status[3]) & 0x1f)
#define RES_CORE(_res) ((_res)->status[2])
#define RES_TASK(_res) ((int)((_res)->status[1]) * 0x100 + (int)((_res)->status[0]))
#define RES_NONCE(_res) u8tou32((_res)->nonce, 0)

#define IS_RESULT(_res) ((_res)->status[1] || (_res)->status[0])

struct minion_result {
	uint8_t status[DATA_SIZ];
	uint8_t nonce[DATA_SIZ];
};

#define MINION_RES_DATA_SIZ sizeof(struct minion_result)

#define MIDSTATE_BYTES 32
#define MERKLE7_OFFSET 64
#define MERKLE_BYTES 12

#define MINION_MAX_TASK_ID 0xffff

struct minion_que {
	uint8_t task_id[2];
	uint8_t reserved[2];
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
	bool stale; // if stale, don't decrement count_up when discarded
} WITEM;

#define ALLOC_TITEMS 256

typedef struct titem {
	uint8_t chip;
	bool write;
	uint8_t address;
	uint32_t task_id;
	uint32_t wsiz;
	uint32_t osiz;
	uint32_t rsiz;
	uint8_t wbuf[MINION_BUFSIZ];
	uint8_t obuf[MINION_BUFSIZ];
	uint8_t rbuf[MINION_BUFSIZ];
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
	const char *name;
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
	int count_up;		// incremented every time one is added
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
	pthread_mutex_t sta_lock;

	int spifd;
	char gpiointvalue[64];
	int gpiointfd;

	// TODO: need to track disabled chips - done?
	int chips;
	bool chip[MINION_CHIPS];

	uint32_t next_task_id;

	// Stats
	uint64_t chip_nonces[MINION_CHIPS];
	uint64_t chip_good[MINION_CHIPS];
	uint64_t chip_bad[MINION_CHIPS];
	uint64_t core_good[MINION_CHIPS][MINION_CORES];
	uint64_t core_bad[MINION_CHIPS][MINION_CORES];

	struct minion_status chip_status[MINION_CHIPS];

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
	K_STORE *treply_list;

	// Nonce replies
	K_LIST *rfree_list;
	K_STORE *rnonce_list;

	struct timeval last_did;

	bool initialised;
};

static void alloc_items(K_LIST *list, MINION_FFL_ARGS)
{
	K_ITEM *item;
	int i;

	if (list->is_store) {
		quithere(1, "List %s store can't %s" MINION_FFL,
				list->name, __func__, MINION_FFL_PASS);
	}

	item = calloc(list->allocate, sizeof(*item));
	if (!item) {
		quithere(1, "List %s failed to calloc %d new items - total was %d",
				list->name, list->allocate, list->total);
	}

	list->total += list->allocate;
	list->count = list->allocate;
	list->count_up = list->allocate;

	item[0].name = list->name;
	item[0].prev = NULL;
	item[0].next = &(item[1]);
	for (i = 1; i < list->allocate-1; i++) {
		item[i].name = list->name;
		item[i].prev = &item[i-1];
		item[i].next = &item[i+1];
	}
	item[list->allocate-1].name = list->name;
	item[list->allocate-1].prev = &(item[list->allocate-2]);
	item[list->allocate-1].next = NULL;

	list->head = item;
	if (list->do_tail)
		list->tail = &(item[list->allocate-1]);

	item = list->head;
	while (item) {
		item->data = calloc(1, list->siz);
		if (!(item->data))
			quithere(1, "List %s failed to calloc item data", list->name);
		item = item->next;
	}
}

static K_STORE *new_store(K_LIST *list)
{
	K_STORE *store;

	store = calloc(1, sizeof(*store));
	if (!store)
		quithere(1, "Failed to calloc store for %s", list->name);

	store->is_store = true;
	store->lock = list->lock;
	store->name = list->name;
	store->do_tail = list->do_tail;

	return store;
}

static K_LIST *new_list(const char *name, size_t siz, int allocate, bool do_tail, MINION_FFL_ARGS)
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

	alloc_items(list, MINION_FFL_PASS);

	return list;
}

static K_ITEM *k_get_head(K_LIST *list, MINION_FFL_ARGS)
{
	K_ITEM *item;

	if (!(list->head))
		alloc_items(list, MINION_FFL_PASS);

	item = list->head;
	list->head = item->next;
	if (list->head)
		list->head->prev = NULL;
	else {
		if (list->do_tail)
			list->tail = NULL;
	}

	item->prev = item->next = NULL;

	list->count--;

	return item;
}

static void k_add_head(K_LIST *list, K_ITEM *item, MINION_FFL_ARGS)
{
	if (item->name != list->name) {
		quithere(1, "List %s can't %s a %s item" MINION_FFL,
				list->name, __func__, item->name, MINION_FFL_PASS);
	}

	item->prev = NULL;
	item->next = list->head;
	if (list->head)
		list->head->prev = item;

	list->head = item;

	if (list->do_tail) {
		if (!(list->tail))
			list->tail = item;
	}

	list->count++;
	list->count_up++;
}

// TODO: remove later - it slows it down (of course) - only for debugging
static void k_free_head(K_LIST *list, K_ITEM *item, MINION_FFL_ARGS)
{
	memset(item->data, 0xff, list->siz);
	k_add_head(list, item, MINION_FFL_PASS);
}

static void k_remove(K_LIST *list, K_ITEM *item)
{
	if (item->prev)
		item->prev->next = item->next;

	if (item->next)
		item->next->prev = item->prev;

	if (list->head == item)
		list->head = item->next;

	if (list->do_tail) {
		if (list->tail == item)
			list->tail = item->prev;
	}

	item->prev = item->next = NULL;

	list->count--;
}

static void ready_work(struct cgpu_info *minioncgpu, struct work *work)
{
	struct minion_info *minioninfo = (struct minion_info *)(minioncgpu->device_data);
	K_ITEM *item = NULL;

	K_WLOCK(minioninfo->wfree_list);

	item = k_get_head(minioninfo->wfree_list, MINION_FFL_HERE);

	DATAW(item)->work = work;
	DATAW(item)->task_id = 0;
	memset(&(DATAW(item)->sent), 0, sizeof(DATAW(item)->sent));
	DATAW(item)->nonces = 0;
	DATAW(item)->urgent = false;

	k_add_head(minioninfo->wwork_list, item, MINION_FFL_HERE);

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

		k_free_head(minioninfo->rfree_list, item, MINION_FFL_HERE);
	}

	K_WUNLOCK(minioninfo->rnonce_list);

	return found;
}

static const char *addr2txt(uint8_t addr)
{
	switch (addr) {
		case READ_ADDR(MINION_SYS_CHIP_SIG):
			return "ReadChipSig";
		case READ_ADDR(MINION_SYS_CHIP_STA):
			return "ReadChipStatus";
		case WRITE_ADDR(MINION_SYS_MISC_CTL):
			return "WriteMiscControl";
		case WRITE_ADDR(MINION_SYS_RSTN_CTL):
			return "WriteResetControl";
		case READ_ADDR(MINION_SYS_FIFO_STA):
			return "ReadFifoStatus";
		case READ_ADDR(MINION_CORE_ENA0_31):
			return "ReadCoreEnable0-31";
		case WRITE_ADDR(MINION_CORE_ENA0_31):
			return "WriteCoreEnable0-31";
		case READ_ADDR(MINION_CORE_ENA32_63):
			return "ReadCoreEnable32-63";
		case WRITE_ADDR(MINION_CORE_ENA32_63):
			return "WriteCoreEnable32-63";
		case READ_ADDR(MINION_RES_DATA):
			return "ReadResultData";
		case WRITE_ADDR(MINION_QUE_0):
			return "WriteQueWork";
		case READ_ADDR(MINION_NONCE_RANGE):
			return "ReadNonceRange";
		case WRITE_ADDR(MINION_NONCE_RANGE):
			return "WriteNonceRange";
	}

	// gcc warning if this is in default:
	if (IS_ADDR_READ(addr))
		return "ReadUnhandled";
	else
		return "WriteUnhandled";
}

// For display_ioctl()
#define IOCTRL_LOG LOG_DEBUG

// For all other debug so it can easily be switched always on
#define MINION_LOG LOG_DEBUG

static void display_ioctl(int reply, uint32_t osiz, uint8_t *obuf, uint32_t rsiz, uint8_t *rbuf)
{
	struct minion_result *res;
	const char *name, *dir, *ex;
	char buf[1024];
	int i, rescount;

	name = addr2txt(obuf[1]);

	if (IS_ADDR_READ(obuf[1]))
		dir = "from";
	else
		dir = "to";

	buf[0] = '\0';
	ex = "";

	switch (obuf[1]) {
		case READ_ADDR(MINION_SYS_CHIP_SIG):
		case READ_ADDR(MINION_SYS_CHIP_STA):
			break;
		case WRITE_ADDR(MINION_SYS_MISC_CTL):
		case WRITE_ADDR(MINION_SYS_RSTN_CTL):
			if (osiz > HSIZE()) {
				ex = " wrote ";
				__bin2hex(buf, obuf + HSIZE(), osiz - HSIZE());
			} else
				ex = " wrote nothing";
			break;
		default:
			if (IS_ADDR_WRITE(obuf[1])) {
				if (osiz > HSIZE()) {
					ex = " wrote ";
					__bin2hex(buf, obuf + HSIZE(), osiz - HSIZE());
				} else
					ex = " wrote nothing";
			}
			break;
	}

	if (reply < 0) {
		applog(IOCTRL_LOG, "%s %s chip %d osiz %d%s%s",
				   name, dir, (int)obuf[0], (int)osiz, ex, buf);
		applog(IOCTRL_LOG, "  reply was error %d", reply);
	} else {
		if (IS_ADDR_WRITE(obuf[1])) {
			applog(IOCTRL_LOG, "%s %s chip %d osiz %d%s%s",
					   name, dir, (int)obuf[0], (int)osiz, ex, buf);
			applog(IOCTRL_LOG, "  write ret was %d", reply);
		} else {
			switch (obuf[1]) {
				case READ_ADDR(MINION_RES_DATA):
					rescount = (int)((float)rsiz / (float)MINION_RES_DATA_SIZ);
					applog(IOCTRL_LOG, "%s %s chip %d osiz %d%s%s",
							   name, dir, (int)obuf[0], (int)osiz, ex, buf);
					for (i = 0; i < rescount; i++) {
						res = (struct minion_result *)(rbuf + osiz - rsiz + (i * MINION_RES_DATA_SIZ));
						if (!IS_RESULT(res)) {
							applog(IOCTRL_LOG, "  %s reply %d of %d - none", name, i+1, rescount);
						} else {
							__bin2hex(buf, res->nonce, DATA_SIZ);
							applog(IOCTRL_LOG, "  %s reply %d of %d %d(%d) was task 0x%04x"
										   " chip %d core %d gold %s nonce 0x%s",
									   name, i+1, rescount, reply, rsiz,
									   RES_TASK(res),
									   (int)RES_CHIP(res),
									   (int)RES_CORE(res),
									   (int)RES_GOLD(res) ? "Y" : "N",
									   buf);
						}
					}
					break;
				case READ_ADDR(MINION_SYS_CHIP_SIG):
				case READ_ADDR(MINION_SYS_CHIP_STA):
				default:
					applog(IOCTRL_LOG, "%s %s chip %d osiz %d%s%s",
							   name, dir, (int)obuf[0], (int)osiz, ex, buf);
					__bin2hex(buf, rbuf + osiz - rsiz, rsiz);
					applog(IOCTRL_LOG, "  %s reply %d(%d) was %s", name, reply, rsiz, buf);
					break;
			}
		}
	}
}

#define MINION_UNEXPECTED_TASK -999
#define MINION_OVERSIZE_TASK -998

// Set to 1 for debug
#define MINION_SHOW_IO 0

static int _do_ioctl(struct minion_info *minioninfo, uint8_t *zobuf, uint32_t osiz, uint8_t *zrbuf, uint32_t rsiz, MINION_FFL_ARGS)
{
	// TODO: remove these 2 later and rename the z[or]buf back to [or]buf
	//  this simply ensures the IO buffers displayed are not affected by a bug elsewhere - during dev/testing
	uint8_t obuf[MINION_BUFSIZ], rbuf[MINION_BUFSIZ];

	struct spi_ioc_transfer tran;
	int ret;

	if ((int)osiz > MINION_BUFSIZ)
		quitfrom(1, file, func, line, "%s() invalid osiz %u > %d", __func__, osiz, MINION_BUFSIZ);

	if (rsiz >= osiz)
		quitfrom(1, file, func, line, "%s() invalid rsiz %u >= osiz %u", __func__, rsiz, osiz);

	memcpy(obuf, zobuf, osiz);
	memset(&obuf[0] + osiz - rsiz, 0xff, rsiz);

#if MINION_SHOW_IO
	char *buf = bin2hex((char *)obuf, osiz);
	applog(LOG_WARNING, "*** %s() sending %s", __func__, buf);
	free(buf);
#endif

	memset((char *)rbuf, 0x00, osiz);

	cgsleep_ms(5); // TODO: a delay ... based on the last command? But subtract elapsed
			// i.e. do any commands need a delay after the I/O has completed before the next I/O?

	memset(&tran, 0, sizeof(tran));
	if (osiz < MINION_SPI_BUFSIZ)
		tran.len = osiz;
	else
		return MINION_OVERSIZE_TASK;

	tran.delay_usecs = 0;
	tran.speed_hz = MINION_SPI_SPEED;

	tran.tx_buf = (uintptr_t)obuf;
	tran.rx_buf = (uintptr_t)rbuf;
	tran.speed_hz = MINION_SPI_SPEED;

	mutex_lock(&(minioninfo->spi_lock));
	ret = ioctl(minioninfo->spifd, SPI_IOC_MESSAGE(1), (void *)&tran);
	mutex_unlock(&(minioninfo->spi_lock));

#if MINION_SHOW_IO
	if (ret > 0) {
		buf = bin2hex((char *)rbuf, ret);
		applog(LOG_WARNING, "*** %s() reply %d = %s", __func__, ret, buf);
		free(buf);
	} else
		applog(LOG_WARNING, "*** %s() reply = %d", __func__, ret);
#endif

	display_ioctl(ret, osiz, obuf, rsiz, rbuf);

	memcpy(zrbuf, &rbuf[0], osiz);
	return ret;
}

static bool _minion_txrx(struct cgpu_info *minioncgpu, struct minion_info *minioninfo, TITEM *task, bool detect_ignore, MINION_FFL_ARGS)
{
	struct minion_header *head;

	head = (struct minion_header *)(task->obuf);
	head->chip = task->chip;
	if (task->write)
		SET_HEAD_WRITE(head, task->address);
	else
		SET_HEAD_READ(head, task->address);

	SET_HEAD_SIZ(head, task->wsiz + task->rsiz);

	if (task->wsiz)
		memcpy(&(head->data[0]), task->wbuf, task->wsiz);
	task->osiz = HSIZE() + task->wsiz + task->rsiz;

	task->reply = do_ioctl(task->obuf, task->osiz, task->rbuf, task->rsiz);
//TODO:	if (task->reply < 0 && (!detect_ignore || errno != 110)) {
	if (task->reply < 0) {
		applog(LOG_ERR, "%s%d: ioctl failed reply=%d err=%d" MINION_FFL,
				minioncgpu->drv->name, minioncgpu->device_id,
				task->reply, errno, MINION_FFL_PASS);
	} else if (task->reply < (int)(task->osiz)) {
		applog(LOG_ERR, "%s%d: ioctl failed to write %d only wrote %d (err=%d)" MINION_FFL,
				minioncgpu->drv->name, minioncgpu->device_id,
				(int)(task->osiz), task->reply, errno, MINION_FFL_PASS);
	}

	return (task->reply >= (int)(task->osiz));
}

// TODO: hard coded for now
void init_chip(struct cgpu_info *minioncgpu, struct minion_info *minioninfo, int chip)
{
	struct minion_header *head;
	uint8_t rbuf[MINION_BUFSIZ];
	uint8_t wbuf[MINION_BUFSIZ];
	uint32_t wsiz;
	int reply;

	head = (struct minion_header *)wbuf;
	head->chip = chip;
	SET_HEAD_WRITE(head, MINION_SYS_RSTN_CTL);
	SET_HEAD_SIZ(head, MINION_SYS_SIZ);
	head->data[0] = 0x00;
	head->data[1] = 0x00;
	head->data[2] = 0xa5;
	head->data[3] = 0xf5;

	wsiz = HSIZE() + MINION_SYS_SIZ;
	reply = do_ioctl(wbuf, wsiz, rbuf, 0);

	if (reply != (int)wsiz) {
		applog(LOG_ERR, "%s: chip %d reset full returned %d (should be %d)",
				minioncgpu->drv->dname, chip, reply, (int)wsiz);
	}

	head = (struct minion_header *)wbuf;
	head->chip = chip;
	SET_HEAD_WRITE(head, MINION_SYS_RSTN_CTL);
	SET_HEAD_SIZ(head, MINION_SYS_SIZ);
	head->data[0] = SYS_RSTN_CTL_INIT;
	head->data[1] = 0x00;
	head->data[2] = 0x00;
	head->data[3] = 0x00;

	wsiz = HSIZE() + MINION_SYS_SIZ;
	reply = do_ioctl(wbuf, wsiz, rbuf, 0);

	if (reply != (int)wsiz) {
		applog(LOG_ERR, "%s: chip %d reset init returned %d (should be %d)",
				minioncgpu->drv->dname, chip, reply, (int)wsiz);
	}

	head = (struct minion_header *)wbuf;
	head->chip = chip;
	SET_HEAD_WRITE(head, MINION_SYS_MISC_CTL);
	SET_HEAD_SIZ(head, MINION_SYS_SIZ);
	head->data[0] = SYS_MISC_CTL_DEFAULT;
	head->data[1] = 0x00;
	head->data[2] = 0x00;
	head->data[3] = 0x00;

	wsiz = HSIZE() + MINION_SYS_SIZ;
	reply = do_ioctl(wbuf, wsiz, rbuf, 0);

	if (reply != (int)wsiz) {
		applog(LOG_ERR, "%s: chip %d control returned %d (should be %d)",
				minioncgpu->drv->dname, chip, reply, (int)wsiz);
	}
}

// TODO: hard coded for now
void enable_chip_cores(struct cgpu_info *minioncgpu, struct minion_info *minioninfo, int chip)
{
	struct minion_header *head;
	uint8_t rbuf[MINION_BUFSIZ];
	uint8_t wbuf[MINION_BUFSIZ];
	uint32_t wsiz;
	int reply;

	// First see what it reports as
	head = (struct minion_header *)wbuf;
	head->chip = chip;
	SET_HEAD_READ(head, MINION_CORE_ENA0_31);
	SET_HEAD_SIZ(head, MINION_CORE_SIZ);

	wsiz = HSIZE() + MINION_CORE_SIZ;
	reply = do_ioctl(wbuf, wsiz, rbuf, MINION_CORE_SIZ);

	if (reply != (int)wsiz) {
		applog(LOG_ERR, "%s: chip %d core0-31 read returned %d (should be %d)",
				minioncgpu->drv->dname, chip, reply, (int)wsiz);
	}

	SET_HEAD_READ(head, MINION_CORE_ENA32_63);
	reply = do_ioctl(wbuf, wsiz, rbuf, MINION_CORE_SIZ);

	if (reply != (int)wsiz) {
		applog(LOG_ERR, "%s: chip %d core0-31 read returned %d (should be %d)",
				minioncgpu->drv->dname, chip, reply, (int)wsiz);
	}

	head = (struct minion_header *)wbuf;
	head->chip = chip;
	SET_HEAD_WRITE(head, MINION_CORE_ENA0_31);
	SET_HEAD_SIZ(head, MINION_CORE_SIZ);
/*
	head->data[0] = 0xff;
	head->data[1] = 0xff;
	head->data[2] = 0xff;
	head->data[3] = 0xff;
*/
	/*
	 * there really is no reason to do this except in testing
	 * since when mining with real data it will still mine at
	 * full speed, however if we are testing for specific
	 * results, not mining speed, then it's necessary to force
	 * the nonce ranges to be done fully on incomplete hardware
	 */
	head->data[0] = 0x0e; // only core 1,2,3
	head->data[1] = 0x00;
	head->data[2] = 0x00;
	head->data[3] = 0x00;

	wsiz = HSIZE() + MINION_CORE_SIZ;
	reply = do_ioctl(wbuf, wsiz, rbuf, 0);

	if (reply != (int)wsiz) {
		applog(LOG_ERR, "%s: chip %d core0-31 enable returned %d (should be %d)",
				minioncgpu->drv->dname, chip, reply, (int)wsiz);
	}

	head->data[0] = 0x00;
	head->data[1] = 0x00;
	head->data[2] = 0x00;
	head->data[3] = 0x00;

	SET_HEAD_WRITE(head, MINION_CORE_ENA32_63);
	reply = do_ioctl(wbuf, wsiz, rbuf, 0);

	if (reply != (int)wsiz) {
		applog(LOG_ERR, "%s: chip %d core32-63 enable returned %d (should be %d)",
				minioncgpu->drv->dname, chip, reply, (int)wsiz);
	}

	SET_HEAD_WRITE(head, MINION_NONCE_RANGE);
	head->data[0] = 0x55;
	head->data[1] = 0x55;
	head->data[2] = 0x55;
	head->data[3] = 0x55;

	// quicker replies
//	head->data[0] = 0x5;
//	head->data[1] = 0x5;
//	head->data[2] = 0x5;
//	head->data[3] = 0x5;

	wsiz = HSIZE() + MINION_CORE_SIZ;
	reply = do_ioctl(wbuf, wsiz, rbuf, 0);

	if (reply != (int)wsiz) {
		applog(LOG_ERR, "%s: chip %d nonce range returned %d (should be %d)",
				minioncgpu->drv->dname, chip, reply, (int)wsiz);
	}
}

// TODO: hard coded for now
void enable_interrupt(struct cgpu_info *minioncgpu, struct minion_info *minioninfo, int chip)
{
	struct minion_header *head;
	uint8_t rbuf[MINION_BUFSIZ];
	uint8_t wbuf[MINION_BUFSIZ];
	uint32_t wsiz;
	int reply;

	head = (struct minion_header *)wbuf;
	head->chip = chip;
	SET_HEAD_WRITE(head, MINION_SYS_BUF_TRIG);
	SET_HEAD_SIZ(head, MINION_SYS_SIZ);

	head->data[0] = MINION_RESULT_INT_SIZE;
	head->data[1] = 0x00;
	head->data[2] = 0x00;
	head->data[3] = 0x00;

	wsiz = HSIZE() + MINION_SYS_SIZ;
	reply = do_ioctl(wbuf, wsiz, rbuf, 0);

	if (reply != (int)wsiz) {
		applog(LOG_ERR, "%s: chip %d result tigger set %d returned %d (should be %d)",
				minioncgpu->drv->dname, chip, MINION_RESULT_INT_SIZE,
				reply, (int)wsiz);
	}

	head = (struct minion_header *)wbuf;
	head->chip = chip;
	SET_HEAD_WRITE(head, MINION_SYS_INT_ENA);
	SET_HEAD_SIZ(head, MINION_SYS_SIZ);

	head->data[0] = MINION_RESULT_INT;
	head->data[1] = 0x00;
	head->data[2] = 0x00;
	head->data[3] = 0x00;

	wsiz = HSIZE() + MINION_CORE_SIZ;
	reply = do_ioctl(wbuf, wsiz, rbuf, 0);

	if (reply != (int)wsiz) {
		applog(LOG_ERR, "%s: chip %d result trigger enable returned %d (should be %d)",
				minioncgpu->drv->dname, chip, reply, (int)wsiz);
	}
}

// Simple detect - just check each chip for the signature
void minion_detect_chips(struct cgpu_info *minioncgpu, struct minion_info *minioninfo)
{
	struct minion_header *head;
	uint8_t wbuf[MINION_BUFSIZ];
	uint8_t rbuf[MINION_BUFSIZ];
	uint32_t wsiz, rsiz;
	int chip, reply, tries;
	bool ok;

	head = (struct minion_header *)wbuf;
	rsiz = MINION_SYS_SIZ;
	SET_HEAD_READ(head, MINION_SYS_CHIP_SIG);
	SET_HEAD_SIZ(head, rsiz);
	wsiz = HSIZE() + rsiz;
	for (chip = 0; chip < MINION_CHIPS; chip++) {
		head->chip = (uint8_t)chip;
		tries = 0;
		ok = false;
		do {
			reply = do_ioctl(wbuf, wsiz, rbuf, rsiz);

			if (reply == (int)(wsiz)) {
				uint32_t sig = u8tou32(rbuf, wsiz - rsiz);

				if (sig == MINION_CHIP_SIG) {
					minioninfo->chip[chip] = true;
					minioninfo->chips++;
					ok = true;
				} else {
					if (sig == MINION_NOCHIP_SIG) // Assume no chip
						ok = true;
					else {
						applog(LOG_ERR, "%s: chip %d detect failed got"
								" 0x%08x wanted 0x%08x",
								minioncgpu->drv->dname, chip, sig,
								MINION_CHIP_SIG);
					}
				}
			} else {
				applog(LOG_ERR, "%s: chip %d reply %d ignored should be %d",
						minioncgpu->drv->dname, chip, reply, (int)(wsiz));
			}
		} while (!ok && ++tries <= MINION_SIG_TRIES);

		if (!ok) {
			applog(LOG_ERR, "%s: chip %d - detect failure status",
					minioncgpu->drv->dname, chip);
		}
	}

	if (minioninfo->chips) {
		for (chip = 0; chip < MINION_CHIPS; chip++) {
			if (minioninfo->chip[chip]) {
				init_chip(minioncgpu, minioninfo, chip);
				enable_chip_cores(minioncgpu, minioninfo, chip);
			}
		}

		// After everything is ready
		for (chip = 0; chip < MINION_CHIPS; chip++)
			if (minioninfo->chip[chip])
				enable_interrupt(minioncgpu, minioninfo, chip);
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
	{ SPI_IOC_RD_MODE, 0 },
	{ SPI_IOC_WR_MODE, 0 },
	{ SPI_IOC_RD_BITS_PER_WORD, 8 },
	{ SPI_IOC_WR_BITS_PER_WORD, 8 },
	{ SPI_IOC_RD_MAX_SPEED_HZ, 1000000 },
	{ SPI_IOC_WR_MAX_SPEED_HZ, 1000000 },
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
			applog(LOG_ERR, "%s: failed ioctl configuration (%d) (%d)",
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

static bool minion_init_gpio_interrupt(struct cgpu_info *minioncgpu, struct minion_info *minioninfo)
{
	char pindir[64], ena[64], pin[8], dir[64], edge[64], act[64];
	struct stat st;
	int file, err;
	ssize_t ret;

	snprintf(pindir, sizeof(pindir), MINION_GPIO_SYS MINION_GPIO_PIN,
					 MINION_GPIO_RESULT_INT_PIN);
	memset(&st, 0, sizeof(st));

	if (stat(pindir, &st) == 0) { // already exists
		if (!S_ISDIR(st.st_mode)) {
			applog(LOG_ERR, "%s: failed1 to enable GPIO pin %d interrupt"
					" - not a directory",
					minioncgpu->drv->dname,
					MINION_GPIO_RESULT_INT_PIN);
			return false;
		}
	} else {
		snprintf(ena, sizeof(ena), MINION_GPIO_SYS MINION_GPIO_ENA);
		file = open(ena, O_WRONLY | O_SYNC);
		if (file == -1) {
			applog(LOG_ERR, "%s: failed2 to enable GPIO pin %d interrupt (%d)"
					" - you need to be root?",
					minioncgpu->drv->dname,
					MINION_GPIO_RESULT_INT_PIN,
					errno);
			return false;
		}
		snprintf(pin, sizeof(pin), MINION_GPIO_ENA_VAL, MINION_GPIO_RESULT_INT_PIN);
		ret = write(file, pin, (size_t)strlen(pin));
		if (ret != (ssize_t)strlen(pin)) {
			if (ret < 0)
				err = errno;
			else
				err = (int)ret;
			close(file);
			applog(LOG_ERR, "%s: failed3 to enable GPIO pin %d interrupt (%d:%d)",
					minioncgpu->drv->dname,
					MINION_GPIO_RESULT_INT_PIN,
					err, strlen(pin));
			return false;
		}
		close(file);

		// Check again if it exists
		memset(&st, 0, sizeof(st));
		if (stat(pindir, &st) != 0) {
			applog(LOG_ERR, "%s: failed4 to enable GPIO pin %d interrupt (%d)",
					minioncgpu->drv->dname,
					MINION_GPIO_RESULT_INT_PIN,
					errno);
			return false;
		}
	}

	// Set the pin attributes
	// Direction
	snprintf(dir, sizeof(dir), MINION_GPIO_SYS MINION_GPIO_PIN MINION_GPIO_DIR,
				   MINION_GPIO_RESULT_INT_PIN);
	file = open(dir, O_WRONLY | O_SYNC);
	if (file == -1) {
		applog(LOG_ERR, "%s: failed5 to enable GPIO pin %d interrupt (%d)"
				" - you need to be root?",
				minioncgpu->drv->dname,
				MINION_GPIO_RESULT_INT_PIN,
				errno);
		return false;
	}
	ret = write(file, MINION_GPIO_DIR_READ, (size_t)strlen(MINION_GPIO_DIR_READ));
	if (ret != (ssize_t)strlen(MINION_GPIO_DIR_READ)) {
		if (ret < 0)
			err = errno;
		else
			err = (int)ret;
		close(file);
		applog(LOG_ERR, "%s: failed6 to enable GPIO pin %d interrupt (%d:%d)",
				minioncgpu->drv->dname,
				MINION_GPIO_RESULT_INT_PIN,
				err, strlen(MINION_GPIO_DIR_READ));
		return false;
	}
	close(file);

	// Edge
	snprintf(edge, sizeof(edge), MINION_GPIO_SYS MINION_GPIO_PIN MINION_GPIO_EDGE,
				   MINION_GPIO_RESULT_INT_PIN);
	file = open(edge, O_WRONLY | O_SYNC);
	if (file == -1) {
		applog(LOG_ERR, "%s: failed7 to enable GPIO pin %d interrupt (%d)",
				minioncgpu->drv->dname,
				MINION_GPIO_RESULT_INT_PIN,
				errno);
		return false;
	}
	ret = write(file, MINION_GPIO_EDGE_RISING, (size_t)strlen(MINION_GPIO_EDGE_RISING));
	if (ret != (ssize_t)strlen(MINION_GPIO_EDGE_RISING)) {
		if (ret < 0)
			err = errno;
		else
			err = (int)ret;
		close(file);
		applog(LOG_ERR, "%s: failed8 to enable GPIO pin %d interrupt (%d:%d)",
				minioncgpu->drv->dname,
				MINION_GPIO_RESULT_INT_PIN,
				err, strlen(MINION_GPIO_EDGE_RISING));
		return false;
	}
	close(file);

	// Active
	snprintf(act, sizeof(act), MINION_GPIO_SYS MINION_GPIO_PIN MINION_GPIO_ACT,
				   MINION_GPIO_RESULT_INT_PIN);
	file = open(act, O_WRONLY | O_SYNC);
	if (file == -1) {
		applog(LOG_ERR, "%s: failed9 to enable GPIO pin %d interrupt (%d)",
				minioncgpu->drv->dname,
				MINION_GPIO_RESULT_INT_PIN,
				errno);
		return false;
	}
	ret = write(file, MINION_GPIO_ACT_HI, (size_t)strlen(MINION_GPIO_ACT_HI));
	if (ret != (ssize_t)strlen(MINION_GPIO_ACT_HI)) {
		if (ret < 0)
			err = errno;
		else
			err = (int)ret;
		close(file);
		applog(LOG_ERR, "%s: failed10 to enable GPIO pin %d interrupt (%d:%d)",
				minioncgpu->drv->dname,
				MINION_GPIO_RESULT_INT_PIN,
				err, strlen(MINION_GPIO_ACT_HI));
		return false;
	}
	close(file);

	// Value
	snprintf(minioninfo->gpiointvalue, sizeof(minioninfo->gpiointvalue),
				   MINION_GPIO_SYS MINION_GPIO_PIN MINION_GPIO_VALUE,
				   MINION_GPIO_RESULT_INT_PIN);
	minioninfo->gpiointfd = open(minioninfo->gpiointvalue, O_RDONLY);
	if (minioninfo->gpiointfd == -1) {
		applog(LOG_ERR, "%s: failed11 to enable GPIO pin %d interrupt (%d)",
				minioncgpu->drv->dname,
				MINION_GPIO_RESULT_INT_PIN,
				errno);
		return false;
	}

	return true;
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

	if (!minion_init_gpio_interrupt(minioncgpu, minioninfo))
		goto unalloc;

	mutex_init(&(minioninfo->spi_lock));
	mutex_init(&(minioninfo->sta_lock));

	applog(LOG_WARNING, "%s: checking for chips ...", minioncgpu->drv->dname);

	minion_detect_chips(minioncgpu, minioninfo);

	applog(LOG_WARNING, "%s: found %d chip%s",
				minioncgpu->drv->dname, minioninfo->chips,
				(minioninfo->chips == 1) ? "" : "s");

	if (minioninfo->chips == 0)
		goto cleanup;

	if (!add_cgpu(minioncgpu))
		goto cleanup;

	mutex_init(&(minioninfo->nonce_lock));

	minioninfo->wfree_list = new_list("Work", sizeof(WITEM), ALLOC_WITEMS, true, MINION_FFL_HERE);
	minioninfo->wwork_list = new_store(minioninfo->wfree_list);
	// Initialise them all in case we later decide to enable chips
	for (i = 0; i < minioninfo->chips; i++)
		minioninfo->wchip_list[i] = new_store(minioninfo->wfree_list);

	minioninfo->tfree_list = new_list("Task", sizeof(TITEM), ALLOC_TITEMS, true, MINION_FFL_HERE);
	minioninfo->task_list = new_store(minioninfo->tfree_list);
	minioninfo->treply_list = new_store(minioninfo->tfree_list);

	minioninfo->rfree_list = new_list("Reply", sizeof(RITEM), ALLOC_RITEMS, true, MINION_FFL_HERE);
	minioninfo->rnonce_list = new_store(minioninfo->rfree_list);

	minioninfo->initialised = true;

	return;

cleanup:
	close(minioninfo->gpiointfd);
	close(minioninfo->spifd);
	mutex_destroy(&(minioninfo->sta_lock));
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
#define MINION_REPLY_mS 888

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
	K_ITEM *item, *tail;
	TITEM *titem;
	bool do_task;
	double wait;

	applog(MINION_LOG, "%s%i: SPI writing...",
				minioncgpu->drv->name, minioncgpu->device_id);

	// Wait until we're ready
	while (minioncgpu->shutdown == false) {
		if (minioninfo->initialised) {
			break;
		}
		cgsleep_ms(3); // asap to start mining
	}

	cgtime(&start);
	while (minioncgpu->shutdown == false) {
		do_task = false;
		cgtime(&stop);
		wait = us_tdiff(&stop, &start);
		if (wait >= MINION_TASK_uS)
			do_task = true;

		item = NULL;
		K_WLOCK(minioninfo->task_list);
		tail = minioninfo->task_list->tail;
		if (tail) {
			// Find first urgent item
			item = tail;
			while (item && !(DATAT(item)->urgent))
				item = item->prev;

			// No urgent items, just do the tail
			if (!item)
				item = tail;

			if (DATAT(item)->urgent)
				do_task = true;

			if (do_task)
				k_remove(minioninfo->task_list, item);
			else
				item = NULL;
		}
		K_WUNLOCK(minioninfo->task_list);

		if (item) {
			bool do_txrx = true;
			bool store_reply = true;

			titem = DATAT(item);

			switch (titem->address) {
				// TODO: case MINION_CORE_ENA0_31:
				// TODO: case MINION_CORE_ENA32_63:
				// TODO: case MINION_SYS_TEMP_CTL:
				// TODO: case MINION_SYS_FREQ_CTL:
				case READ_ADDR(MINION_SYS_CHIP_STA):
					store_reply = false;
					break;
				case WRITE_ADDR(MINION_QUE_0):
					store_reply = false;
					break;
				case WRITE_ADDR(MINION_SYS_RSTN_CTL):
					store_reply = false;
					break;
				default:
					do_txrx = false;
					titem->reply = MINION_UNEXPECTED_TASK;
					applog(LOG_ERR, "%s%i: Unexpected task address 0x%02x",
							minioncgpu->drv->name, minioncgpu->device_id,
							(unsigned int)(titem->address));

					break;
			}

			if (do_txrx) {
				cgtime(&start);
				minion_txrx(titem, false);

				switch (titem->address) {
					case READ_ADDR(MINION_SYS_CHIP_STA):
						if (titem->reply >= (int)(titem->osiz)) {
							uint8_t *rep = &(titem->rbuf[titem->osiz - titem->rsiz]);
							int chip = titem->chip;

							mutex_lock(&(minioninfo->sta_lock));
							minioninfo->chip_status[chip].temp = STA_TEMP(rep);
							minioninfo->chip_status[chip].cores = STA_CORES(rep);
							minioninfo->chip_status[chip].freq = STA_FREQ(rep);
							mutex_unlock(&(minioninfo->sta_lock));
						}
						break;
					case WRITE_ADDR(MINION_QUE_0):
					case WRITE_ADDR(MINION_SYS_RSTN_CTL):
					default:
						break;
				}
			}

			K_WLOCK(minioninfo->treply_list);
			if (store_reply)
				k_add_head(minioninfo->treply_list, item, MINION_FFL_HERE);
			else
				k_free_head(minioninfo->tfree_list, item, MINION_FFL_HERE);
			K_WUNLOCK(minioninfo->treply_list);

			// always do the next task immediately if we just did one
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
 * ioctl done every interrupt or MINION_REPLY_mS checking for results
 */
static void *minion_spi_reply(void *userdata)
{
	struct cgpu_info *minioncgpu = (struct cgpu_info *)userdata;
	struct minion_info *minioninfo = (struct minion_info *)(minioncgpu->device_data);
	struct minion_result *result;
	K_ITEM *item;
	TITEM fifo_task, res_task;
	int chip, resoff, ret;
	struct pollfd pfd;

	struct minion_header *head;
	uint8_t rbuf[MINION_BUFSIZ];
	uint8_t wbuf[MINION_BUFSIZ];
	uint32_t wsiz;
	int reply;

	applog(MINION_LOG, "%s%i: SPI replying...",
				minioncgpu->drv->name, minioncgpu->device_id);

	// Wait until we're ready
	while (minioncgpu->shutdown == false) {
		if (minioninfo->initialised) {
			break;
		}
		cgsleep_ms(3);
	}

	fifo_task.chip = 0;
	fifo_task.write = false;
	fifo_task.address = MINION_SYS_FIFO_STA;
	fifo_task.wsiz = 0;
	fifo_task.rsiz = MINION_SYS_SIZ;
	fifo_task.urgent = false;
	fifo_task.work = NULL;

	res_task.chip = 0;
	res_task.write = false;
	res_task.address = MINION_RES_DATA;
	res_task.wsiz = 0;
	res_task.rsiz = MINION_RES_DATA_SIZ;
	res_task.urgent = false;
	res_task.work = NULL;

	memset(&pfd, 0, sizeof(pfd));
	pfd.fd = minioninfo->gpiointfd;
	pfd.events = POLLPRI;

	head = (struct minion_header *)wbuf;
	SET_HEAD_SIZ(head, MINION_SYS_SIZ);
	wsiz = HSIZE() + MINION_SYS_SIZ;

	while (minioncgpu->shutdown == false) {
		for (chip = 0; chip < MINION_CHIPS; chip++) {
			if (minioninfo->chip[chip]) {
				uint8_t res = 0;
				fifo_task.chip = chip;
				fifo_task.reply = 0;
				minion_txrx(&fifo_task, false);
				if (fifo_task.reply > 0) {
					if (fifo_task.reply < (int)(fifo_task.osiz)) {
						char *buf = bin2hex((unsigned char *)(&(fifo_task.rbuf[fifo_task.osiz - fifo_task.rsiz])), (int)(fifo_task.rsiz));
						applog(LOG_ERR, "%s%i: Bad fifo reply (%s) size %d, should be %d",
								minioncgpu->drv->name, minioncgpu->device_id, buf,
								fifo_task.reply, (int)(fifo_task.osiz));
						free(buf);
					} else {
						if (fifo_task.reply > (int)(fifo_task.osiz)) {
							applog(LOG_ERR, "%s%i: Unexpected fifo reply size %d, expected only %d",
									minioncgpu->drv->name, minioncgpu->device_id,
									fifo_task.reply, (int)(fifo_task.osiz));
						}
						res = FIFO_RES(fifo_task.rbuf, fifo_task.osiz - fifo_task.rsiz);
					}
				}
				/*
				 * Chip has results?
				 * You can't request results unless it says it has some.
				 * We don't ever directly flush the output queue while processing
				 * (except at startup) so the answer is always valid
				 */
				if (res > 0) {
					res_task.chip = chip;
					res_task.reply = 0;
					res_task.rsiz = res * MINION_RES_DATA_SIZ;
					minion_txrx(&res_task, false);
					if (res_task.reply > 0) {
						if (res_task.reply < (int)MINION_RES_DATA_SIZ) {
							char *buf = bin2hex((unsigned char *)(&(res_task.rbuf[res_task.osiz - res_task.rsiz])), (int)(res_task.rsiz));
							applog(LOG_ERR, "%s%i: Bad work reply (%s) size %d, should be at least %d",
									minioncgpu->drv->name, minioncgpu->device_id, buf,
									res_task.reply, MINION_RES_DATA_SIZ);
							free(buf);
						} else {
							if (res_task.reply != (int)(res_task.osiz)) {
								applog(LOG_ERR, "%s%i: Unexpected work reply size %d, expected %d",
										minioncgpu->drv->name, minioncgpu->device_id,
										res_task.reply, (int)(res_task.osiz));
							}
							for (resoff = res_task.osiz - res_task.rsiz; resoff < (int)res_task.osiz; resoff += MINION_RES_DATA_SIZ) {
								result = (struct minion_result *)&(res_task.rbuf[resoff]);

								if (IS_RESULT(result)) {
									K_WLOCK(minioninfo->rfree_list);
									item = k_get_head(minioninfo->rfree_list, MINION_FFL_HERE);
									K_WUNLOCK(minioninfo->rfree_list);

									DATAR(item)->chip = RES_CHIP(result);
									DATAR(item)->core = RES_CORE(result);
									DATAR(item)->task_id = RES_TASK(result);
									DATAR(item)->nonce = RES_NONCE(result);
									DATAR(item)->no_nonce = !RES_GOLD(result);

applog(LOG_ERR, "%s%i: found a result chip %d core %d task 0x%04x nonce 0x%08x", minioncgpu->drv->name, minioncgpu->device_id, DATAR(item)->chip, DATAR(item)->core, DATAR(item)->task_id, DATAR(item)->nonce);

									K_WLOCK(minioninfo->rnonce_list);
									k_add_head(minioninfo->rnonce_list, item, MINION_FFL_HERE);
									K_WUNLOCK(minioninfo->rnonce_list);
								}
							}
						}
					}
				}
			}
		}

		// TODO: this is going to require a bit of tuning with 2TH/s mining:
		// The interrupt size MINION_RESULT_INT_SIZE should be high enough to expect
		// most chips to have some results but low enough to cause negligible latency
		// If all chips don't have some results when an interrupt occurs, then it is a waste
		// since we have to check all chips for results anyway since we don't know which one
		// caused the interrupt
		// MINION_REPLY_mS needs to be low enough in the case of bad luck where no chip
		// finds MINION_RESULT_INT_SIZE results in a short amount of time, so we go check
		// them all anyway - to avoid high latency when there are only a few results due to low luck
		ret = poll(&pfd, 1, MINION_REPLY_mS);
		if (ret > 0) {
			int c;

			read(minioninfo->gpiointfd, &c, 1);

			applog(LOG_ERR, "%s%i: result interrupt",
					   minioncgpu->drv->name, minioncgpu->device_id);

			// TODO: which chip do I check for interrupts? Do I need to check every one of them?
			SET_HEAD_READ(head, MINION_SYS_INT_STA);
			head->chip = 0;
			// TODO: will we lose an interrupt if it happens before it gets back to poll
			// but after 'get count of results' is done?
			reply = do_ioctl(wbuf, wsiz, rbuf, MINION_SYS_SIZ);
			if (reply != (int)wsiz) {
				applog(LOG_ERR, "%s: chip %d int status returned %d (should be %d)",
						minioncgpu->drv->dname, chip, reply, (int)wsiz);
			}
/*
			if (rbuf[wsiz] & MINION_RESULT_INT) {
				applog(LOG_ERR, "%s%i: correct interrupt",
						   minioncgpu->drv->name, minioncgpu->device_id);
			}
				applog(LOG_ERR, "%s%i: interrupt: 0x%02x 0x%02x 0x%02x 0x%02x",
						   minioncgpu->drv->name, minioncgpu->device_id,
						   rbuf[wsiz], rbuf[wsiz+1], rbuf[wsiz+2], rbuf[wsiz+3]);
*/

			// Clear the interrupt bit
			SET_HEAD_WRITE(head, MINION_SYS_INT_CLR);
			head->data[0] = MINION_RESULT_INT;
			head->data[1] = 0x00;
			head->data[2] = 0x00;
			head->data[3] = 0x00;
			reply = do_ioctl(wbuf, wsiz, rbuf, 0);
			if (reply != (int)wsiz) {
				applog(LOG_ERR, "%s: chip %d int clear returned %d (should be %d)",
						minioncgpu->drv->dname, chip, reply, (int)wsiz);
			}
		}
	}

	return NULL;
}

/*
 * Find the matching work item for this chip
 * Discard any older work items for this chip
 */

enum nonce_state {
	NONCE_GOOD_NONCE,
	NONCE_NO_NONCE,
	NONCE_BAD_NONCE,
	NONCE_BAD_WORK,
	NONCE_NO_WORK
};

static void cleanup_older(struct cgpu_info *minioncgpu, int chip, K_ITEM *item)
{
	struct minion_info *minioninfo = (struct minion_info *)(minioncgpu->device_data);
	K_ITEM *tail;

	// remove older work items
	if (item->next) {
		K_WLOCK(minioninfo->wchip_list[chip]);
		tail = minioninfo->wchip_list[chip]->tail;
		while (tail && tail != item) {
			k_remove(minioninfo->wchip_list[chip], tail);
			if (!(DATAW(item)->stale))
				minioninfo->wchip_list[chip]->count_up--;
			K_WUNLOCK(minioninfo->wchip_list[chip]);
			applog(MINION_LOG, "%s%i: marking complete - old task 0x%04x chip %d",
					   minioncgpu->drv->name, minioncgpu->device_id,
					   DATAW(tail)->task_id, chip);
			work_completed(minioncgpu, DATAW(tail)->work);
			K_WLOCK(minioninfo->wchip_list[chip]);
			k_free_head(minioninfo->wfree_list, tail, MINION_FFL_HERE);
			tail = minioninfo->wchip_list[chip]->tail;
		}
		K_WUNLOCK(minioninfo->wchip_list[chip]);
	}
}

static enum nonce_state oknonce(struct thr_info *thr, struct cgpu_info *minioncgpu, int chip, int core, uint32_t task_id, uint32_t nonce, bool no_nonce)
{
	struct minion_info *minioninfo = (struct minion_info *)(minioncgpu->device_data);
	K_ITEM *item;

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
		return NONCE_NO_WORK;
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
		return NONCE_BAD_WORK;
	}

	if (no_nonce) {
		cleanup_older(minioncgpu, chip, item);
		return NONCE_NO_NONCE;
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

		cleanup_older(minioncgpu, chip, item);
		return NONCE_GOOD_NONCE;
	}

	minioninfo->chip_bad[chip]++;
	minioninfo->core_bad[chip][core]++;
	inc_hw_errors(thr);

	return NONCE_BAD_NONCE;
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

	applog(MINION_LOG, "%s%i: Results...",
				minioncgpu->drv->name, minioncgpu->device_id);

	// Wait until we're ready
	while (minioncgpu->shutdown == false) {
		if (minioninfo->initialised) {
			break;
		}
		cgsleep_ms(30);
	}

	while (minioncgpu->shutdown == false) {
// TODO: rather than polling - use a cgsem_wait in minion_spi_reply() like in api.c
		if (!oldest_nonce(minioncgpu, &chip, &core, &task_id, &nonce, &no_nonce)) {
			cgsleep_ms(3);
			continue;
		}

		oknonce(thr, minioncgpu, chip, core, task_id, nonce, no_nonce);
	}

	return NULL;
}

static void minion_flush_work(struct cgpu_info *minioncgpu)
{
	struct minion_info *minioninfo = (struct minion_info *)(minioncgpu->device_data);
	K_ITEM *stale_unused_work, *prev_unused, *task, *prev_task, *work;
	int i;

	applog(MINION_LOG, "%s%i: flushing work",
				minioncgpu->drv->name, minioncgpu->device_id);

	// set stale all wchip_list contents
	// TODO: N.B. scanwork also gets work locks - which master thread calls flush?
	K_WLOCK(minioninfo->wwork_list);

	for (i = 0; i < MINION_CHIPS; i++)
		if (minioninfo->chip[i]) {
			work = minioninfo->wchip_list[i]->head;
			while (work) {
				DATAW(work)->stale = true;
				work = work->next;
			}
			minioninfo->wchip_list[i]->count_up = 0;
		}

	// Simply remove the whole unused wwork_list
	stale_unused_work = minioninfo->wwork_list->tail;
	if (stale_unused_work) {
		minioninfo->wwork_list->head = NULL;
		minioninfo->wwork_list->tail = NULL;
		minioninfo->wwork_list->count = 0;
	}

	// TODO: flush/work tasks should have a block sequence number so this task removal code
	// might be better implemented in minion_spi_write where each work task would
	// update the block sequence number and any work tasks with an old block sequence
	// number would be discarded rather than sent - minion_spi_write will also need to
	// prioritise flush urgent tasks above work urgent tasks - have 3 urgent states?
	// They should however be 2 seperate variables in minioninfo to reduce locking
	// - flush will increment one and put it in the flush task, (and work will use that)
	// minion_spi_write will check/update the other and thus not need a lock

	// No deadlock since this is the only code to get 2 locks
	K_WLOCK(minioninfo->tfree_list);
	task = minioninfo->task_list->tail;
	while (task) {
		prev_task = task->prev;
		if (DATAT(task)->address == WRITE_ADDR(MINION_QUE_0)) {
			k_remove(minioninfo->task_list, task);
			/*
			 * Discard it - the work is already in the wchip_list and
			 * will be cleaned up by the next task on the chip
			 */
			k_free_head(minioninfo->tfree_list, task, MINION_FFL_HERE);
		}
		task = prev_task;
	}
	for (i = 0; i < MINION_CHIPS; i++) {
		if (minioninfo->chip[i]) {
			task = k_get_head(minioninfo->tfree_list, MINION_FFL_HERE);
			DATAT(task)->chip = i;
			DATAT(task)->write = true;
			DATAT(task)->address = MINION_SYS_RSTN_CTL;
			DATAT(task)->task_id = 0; // ignored
			DATAT(task)->wsiz = MINION_SYS_SIZ;
			DATAT(task)->rsiz = 0;
			DATAT(task)->wbuf[0] = SYS_RSTN_CTL_FLUSH;
			DATAT(task)->wbuf[1] = 0;
			DATAT(task)->wbuf[2] = 0;
			DATAT(task)->wbuf[3] = 0;
			DATAT(task)->urgent = true;
			k_add_head(minioninfo->task_list, task, MINION_FFL_HERE);
		}
	}
	K_WUNLOCK(minioninfo->tfree_list);

	K_WUNLOCK(minioninfo->wwork_list);

	// TODO: send a signal to force getting and sending new work - needs cgsem_wait in the sending thread

	// TODO: should we use this thread to do the following work?
	if (stale_unused_work) {
		// mark complete all stale unused work (oldest first)
		prev_unused = stale_unused_work;
		while (prev_unused) {
			work_completed(minioncgpu, DATAW(prev_unused)->work);
			prev_unused = prev_unused->prev;
		}

		// put the items back in the wfree_list (oldest first)
		K_WLOCK(minioninfo->wfree_list);
		while (stale_unused_work) {
			prev_unused = stale_unused_work->prev;
			k_free_head(minioninfo->wfree_list, stale_unused_work, MINION_FFL_HERE);
			stale_unused_work = prev_unused;
		}
		K_WUNLOCK(minioninfo->wfree_list);
	}
}

static void new_work_task(struct cgpu_info *minioncgpu, K_ITEM *witem, int chip, bool urgent)
{
	struct minion_info *minioninfo = (struct minion_info *)(minioncgpu->device_data);
	struct minion_que *que;
	K_ITEM *item;

	K_WLOCK(minioninfo->tfree_list);
	item = k_get_head(minioninfo->tfree_list, MINION_FFL_HERE);
	K_WUNLOCK(minioninfo->tfree_list);

	DATAT(item)->chip = chip;
	DATAT(item)->write = true;
	DATAT(item)->address = MINION_QUE_0;

	// if threaded access to new_work_task() is added, this will need locking
	// Don't use task_id 0 so that we can ignore all '0' work replies
	if (minioninfo->next_task_id == 0)
		minioninfo->next_task_id = 7;
	DATAT(item)->task_id = minioninfo->next_task_id;
	DATAW(witem)->task_id = minioninfo->next_task_id;
	minioninfo->next_task_id = (minioninfo->next_task_id + 1) & MINION_MAX_TASK_ID;

	DATAT(item)->urgent = urgent;
	DATAT(item)->work = DATAW(witem)->work;

	que = (struct minion_que *)&(DATAT(item)->wbuf[0]);
	que->task_id[0] = DATAT(item)->task_id & 0xff;
	que->task_id[1] = (DATAT(item)->task_id & 0xff00) >> 8;

	memcpy(&(que->midstate[0]), &(DATAW(witem)->work->midstate[0]), MIDSTATE_BYTES);
	memcpy(&(que->merkle7[0]), &(DATAW(witem)->work->data[MERKLE7_OFFSET]), MERKLE_BYTES);

	DATAT(item)->wsiz = (int)sizeof(*que);
	DATAT(item)->rsiz = 0;

	K_WLOCK(minioninfo->task_list);
	k_add_head(minioninfo->task_list, item, MINION_FFL_HERE);
	K_WUNLOCK(minioninfo->task_list);

	// N.B. this will only update often enough if a chip is > ~2GH/s
	if (!urgent) {
		struct timeval now;
		int limit;

		cgtime(&now);
		// No lock required since 'last' is only accessed here
		if (minioninfo->chip_status[chip].last.tv_sec == 0) {
			memcpy(&(minioninfo->chip_status[chip].last), &now, sizeof(now));
		} else {
			limit = MINION_STATS_UPDATE_TIME_MS +
				(int)(random() % MINION_STATS_UPDATE_RAND_MS);
			if (ms_tdiff(&now, &(minioninfo->chip_status[chip].last)) > limit) {
				memcpy(&(minioninfo->chip_status[chip].last), &now, sizeof(now));

				K_WLOCK(minioninfo->tfree_list);
				item = k_get_head(minioninfo->tfree_list, MINION_FFL_HERE);
				K_WUNLOCK(minioninfo->tfree_list);

				DATAT(item)->chip = chip;
				DATAT(item)->write = false;
				DATAT(item)->address = READ_ADDR(MINION_SYS_CHIP_STA);
				DATAT(item)->task_id = 0;
				DATAT(item)->wsiz = 0;
				DATAT(item)->rsiz = MINION_SYS_SIZ;
				DATAT(item)->urgent = false;

				K_WLOCK(minioninfo->task_list);
				k_add_head(minioninfo->task_list, item, MINION_FFL_HERE);
				K_WUNLOCK(minioninfo->task_list);

				cgtime(&(minioninfo->chip_status[chip].last));
			}
		}
	}
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

#define MINION_QUE_HIGH 4
#define MINION_QUE_LOW 2

static void minion_do_work(struct cgpu_info *minioncgpu)
{
	struct minion_info *minioninfo = (struct minion_info *)(minioncgpu->device_data);
	int count;
	int state, chip, j;
	K_ITEM *item;

	/*
	 * Fill the queues as follows:
	 *	1) put at least 1 in each queue
	 *	2) push each queue up to LOW
	 *	3) push each LOW queue up to HIGH
	 */
	for (state = 0; state < 3; state++) {
		for (chip = 0; chip < MINION_CHIPS; chip++) {
			if (minioninfo->chip[chip]) {
				K_RLOCK(minioninfo->wchip_list[chip]);
				count = minioninfo->wchip_list[chip]->count_up;
				K_RUNLOCK(minioninfo->wchip_list[chip]);

				switch (state) {
					case 0:
						if (count == 0) {
							item = next_work(minioninfo);
							if (item) {
								new_work_task(minioncgpu, item, chip, true);
								K_WLOCK(minioninfo->wchip_list[chip]);
								k_add_head(minioninfo->wchip_list[chip], item, MINION_FFL_HERE);
								K_WUNLOCK(minioninfo->wchip_list[chip]);
								applog(MINION_LOG, "%s%i: 0 task 0x%04x in chip %d list",
										   minioncgpu->drv->name,
										   minioncgpu->device_id,
										   DATAW(item)->task_id, chip);
							} else {
								applog(LOG_ERR, "%s%i: chip %d urgent empty work list",
										minioncgpu->drv->name,
										minioncgpu->device_id,
										chip);
							}
						}
						break;
					case 1:
						if (count < MINION_QUE_LOW) {
							for (j = count; j < MINION_QUE_LOW; j++) {
								item = next_work(minioninfo);
								if (item) {
									new_work_task(minioncgpu, item, chip, false);
									K_WLOCK(minioninfo->wchip_list[chip]);
									k_add_head(minioninfo->wchip_list[chip], item, MINION_FFL_HERE);
									K_WUNLOCK(minioninfo->wchip_list[chip]);
									applog(MINION_LOG, "%s%i: 1 task 0x%04x in chip %d list",
											   minioncgpu->drv->name,
											   minioncgpu->device_id,
											   DATAW(item)->task_id, chip);
								} else {
									applog(LOG_ERR, "%s%i: chip %d non-urgent lo "
											   "empty work list (count=%d)",
											   minioncgpu->drv->name,
											   minioncgpu->device_id,
											   chip, j);
								}
							}
						}
						break;
					case 2:
						if (count <= MINION_QUE_LOW) {
							for (j = count; j < MINION_QUE_HIGH; j++) {
								item = next_work(minioninfo);
								if (item) {
									new_work_task(minioncgpu, item, chip, false);
									K_WLOCK(minioninfo->wchip_list[chip]);
									k_add_head(minioninfo->wchip_list[chip], item, MINION_FFL_HERE);
									K_WUNLOCK(minioninfo->wchip_list[chip]);
									applog(MINION_LOG, "%s%i: 2 task 0x%04x in chip %d list",
											   minioncgpu->drv->name,
											   minioncgpu->device_id,
											   DATAW(item)->task_id, chip);
								} else {
									applog(LOG_ERR, "%s%i: chip %d non-urgent hi "
											   "empty work list (count=%d)",
											   minioncgpu->drv->name,
											   minioncgpu->device_id,
											   chip, j);
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

	applog(MINION_LOG, "%s%i: shutting down",
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

static void minion_get_statline_before(char *buf, size_t bufsiz, struct cgpu_info *minioncgpu)
{
	struct minion_info *minioninfo = (struct minion_info *)(minioncgpu->device_data);
	uint16_t max_temp, cores;
	int chip, sp;

	max_temp = 0;
	cores = 0;
	mutex_lock(&(minioninfo->sta_lock));
		for (chip = 0; chip < MINION_CHIPS; chip++) {
			if (minioninfo->chip[chip]) {
				cores += minioninfo->chip_status[chip].cores;
				if (max_temp < minioninfo->chip_status[chip].temp)
					max_temp = minioninfo->chip_status[chip].temp;
			}
		}
	mutex_unlock(&(minioninfo->sta_lock));

	sp = 0;
	if (cores < 100) {
		sp++;
		if (cores < 10)
			sp++;
	}

	if (max_temp > 99)
		max_temp = 99;

	tailsprintf(buf, bufsiz, "max%2dC Ch:%2d.%d%*s| ", (int)max_temp,
				 minioninfo->chips, (int)cores, sp, "");
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
	root = api_add_int(root, "Reply Count", &(minioninfo->treply_list->count), true);

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
	.get_statline_before = minion_get_statline_before,
	.identify_device = minion_identify,
	.thread_prepare = minion_thread_prepare,
	.hash_work = hash_queued_work,
	.scanwork = minion_scanwork,
	.queue_full = minion_queue_full,
	.flush_work = minion_flush_work,
	.thread_shutdown = minion_shutdown
#endif
};
