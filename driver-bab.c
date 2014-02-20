/*
 * Copyright 2013-2014 Andrew Smith
 * Copyright 2013 bitfury
 *
 * BitFury GPIO code originally based on chainminer code:
 *	https://github.com/bfsb/chainminer
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 3 of the License, or (at your option)
 * any later version.  See COPYING for more details.
 */

#include "config.h"
#include "compat.h"
#include "miner.h"
#include "sha2.h"
#include "klist.h"
#include <ctype.h>

/*
 * Tested on RPi running both Raspbian and Arch
 *  with BlackArrow BitFury V1 & V2 GPIO Controller
 *  with 16 chip BlackArrow BitFury boards
 */

#ifndef LINUX
static void bab_detect(__maybe_unused bool hotplug)
{
}
#else

#include <unistd.h>
#include <linux/spi/spidev.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <fcntl.h>

#define BAB_SPI_BUS 0
#define BAB_SPI_CHIP 0

#define BAB_SPI_SPEED 96000
#define BAB_SPI_BUFSIZ 1024

#define BAB_DELAY_USECS 0
#define BAB_TRF_DELAY 0

#define BAB_ADDR(_n) (*((babinfo->gpio) + (_n)))

#define BAB_INP_GPIO(_n) BAB_ADDR((_n) / 10) &= (~(7 << (((_n) % 10) * 3)))
#define BAB_OUT_GPIO(_n) BAB_ADDR((_n) / 10) |= (1 << (((_n) % 10) * 3))
#define BAB_OUT_GPIO_V(_n, _v) BAB_ADDR((_n) / 10) |= (((_v) <= 3 ? (_v) + 4 : \
					((_v) == 4 ? 3 : 2)) << (((_n) % 10) * 3))

#define BAB_GPIO_SET BAB_ADDR(7)
#define BAB_GPIO_CLR BAB_ADDR(10)
#define BAB_GPIO_LEVEL BAB_ADDR(13)

// If the V1 test of this many chips finds no chips it will try V2
#define BAB_V1_CHIP_TEST 32

//maximum number of chips per board
#define BAB_BOARDCHIPS 16
#define BAB_MAXBUF (BAB_MAXCHIPS * 512)
#define BAB_V1_BANK 0
//maximum number of alternative banks
#define BAB_MAXBANKS 4
//maximum number of boards in a bank
#define BAB_BANKBOARDS 6
//maximum number of chips on alternative bank
#define BAB_BANKCHIPS (BAB_BOARDCHIPS * BAB_BANKBOARDS)
//maximum number of chips
#define BAB_MAXCHIPS (BAB_MAXBANKS * BAB_BANKCHIPS)
#define BAB_CORES 16
#define BAB_X_COORD 21
#define BAB_Y_COORD 36

#define BAB_NOOP 0
#define BAB_BREAK ((uint8_t *)"\04")
#define BAB_ASYNC ((uint8_t *)"\05")
#define BAB_SYNC ((uint8_t *)"\06")

#define BAB_FFL " - from %s %s() line %d"
#define BAB_FFL_HERE __FILE__, __func__, __LINE__
#define BAB_FFL_PASS file, func, line

#define bab_reset(_bank, _times) _bab_reset(babcgpu, babinfo, _bank, _times)
#define bab_txrx(_item, _det) _bab_txrx(babcgpu, babinfo, _item, _det, BAB_FFL_HERE)
#define bab_add_buf(_item, _data) _bab_add_buf(_item, _data, sizeof(_data)-1, BAB_FFL_HERE)
#define BAB_ADD_BREAK(_item) _bab_add_buf(_item, BAB_BREAK, 1, BAB_FFL_HERE)
#define BAB_ADD_ASYNC(_item) _bab_add_buf(_item, BAB_ASYNC, 1, BAB_FFL_HERE)
#define bab_config_reg(_item, _reg, _ena) _bab_config_reg(_item, _reg, _ena, BAB_FFL_HERE)
#define bab_add_data(_item, _addr, _data, _siz) _bab_add_data(_item, _addr, (const uint8_t *)(_data), _siz, BAB_FFL_HERE)

#define BAB_ADD_NOOPs(_item, _count) _bab_add_noops(_item, _count, BAB_FFL_HERE)

#define BAB_ADD_MIN 4
#define BAB_ADD_MAX 128

#define BAB_BASEA 4
#define BAB_BASEB 61
#define BAB_COUNTERS 16
static const uint8_t bab_counters[BAB_COUNTERS] = {
	64,			64,
	BAB_BASEA,		BAB_BASEA+4,
	BAB_BASEA+2,		BAB_BASEA+2+16,
	BAB_BASEA,		BAB_BASEA+1,
	(BAB_BASEB)%65,		(BAB_BASEB+1)%65,
	(BAB_BASEB+3)%65,	(BAB_BASEB+3+16)%65,
	(BAB_BASEB+4)%65,	(BAB_BASEB+4+4)%65,
	(BAB_BASEB+3+3)%65,	(BAB_BASEB+3+1+3)%65
};

#define BAB_W1 16
static const uint32_t bab_w1[BAB_W1] = {
	0,		0,	0,	0xffffffff,
	0x80000000,	0,	0,	0,
	0,		0,	0,	0,
	0,		0,	0,	0x00000280
};

#define BAB_W2 8
static const uint32_t bab_w2[BAB_W2] = {
	0x80000000,	0,	0,	0,
	0,		0,	0,	0x00000100
};

#define BAB_TEST_DATA 19
static const uint32_t bab_test_data[BAB_TEST_DATA] = {
	0xb0e72d8e,	0x1dc5b862,	0xe9e7c4a6,	0x3050f1f5,
	0x8a1a6b7e,	0x7ec384e8,	0x42c1c3fc,	0x8ed158a1,
	0x8a1a6b7e,	0x6f484872,	0x4ff0bb9b,	0x12c97f07,
	0xb0e72d8e,	0x55d979bc,	0x39403296,	0x40f09e84,
	0x8a0bb7b7,	0x33af304f,	0x0b290c1a //,	0xf0c4e61f
};

/*
 * maximum chip speed available for auto tuner
 * speed/nrate/hrate/watt
 *    53/   97/  100/  84
 *    54/   98/  107/  88
 *    55/   99/  115/  93
 *    56/  101/  125/  99
 */
#define BAB_MAXSPEED 57
#define BAB_DEFMAXSPEED 55
#define BAB_DEFSPEED 53
#define BAB_MINSPEED 52
#define BAB_ABSMINSPEED 32

/*
 * % of errors to tune the speed up or down
 * 1.0 to 10.0 should average around 5.5% errors
 */
#define BAB_TUNEUP 1.0
#define BAB_TUNEDOWN 10.0

#define MIDSTATE_BYTES 32
#define MERKLE_OFFSET 64
#define MERKLE_BYTES 12
#define BLOCK_HEADER_BYTES 80

#define MIDSTATE_UINTS (MIDSTATE_BYTES / sizeof(uint32_t))
#define DATA_UINTS ((BLOCK_HEADER_BYTES / sizeof(uint32_t)) - 1)

// Auto adjust
#define BAB_AUTO_REG 0
#define BAB_AUTO_VAL 0x01
// iclk
#define BAB_ICLK_REG 1
#define BAB_ICLK_VAL 0x02
// No fast clock
#define BAB_FAST_REG 2
#define BAB_FAST_VAL 0x04
// Divide by 2
#define BAB_DIV2_REG 3
#define BAB_DIV2_VAL 0x08
// Slow Clock
#define BAB_SLOW_REG 4
#define BAB_SLOW_VAL 0x10
// No oclk
#define BAB_OCLK_REG 6
#define BAB_OCLK_VAL 0x20
// Has configured
#define BAB_CFGD_VAL 0x40

#define BAB_DEFCONF (BAB_AUTO_VAL | \
		     BAB_ICLK_VAL | \
		     BAB_DIV2_VAL | \
		     BAB_SLOW_VAL)

#define BAB_REG_CLR_FROM 7
#define BAB_REG_CLR_TO 11

#define BAB_AUTO_SET(_c) ((_c) & BAB_AUTO_VAL)
#define BAB_ICLK_SET(_c) ((_c) & BAB_ICLK_VAL)
#define BAB_FAST_SET(_c) ((_c) & BAB_FAST_VAL)
#define BAB_DIV2_SET(_c) ((_c) & BAB_DIV2_VAL)
#define BAB_SLOW_SET(_c) ((_c) & BAB_SLOW_VAL)
#define BAB_OCLK_SET(_c) ((_c) & BAB_OCLK_VAL)
#define BAB_CFGD_SET(_c) ((_c) & BAB_CFGD_VAL)

#define BAB_AUTO_BIT(_c) (BAB_AUTO_SET(_c) ? true : false)
#define BAB_ICLK_BIT(_c) (BAB_ICLK_SET(_c) ? false : true)
#define BAB_FAST_BIT(_c) (BAB_FAST_SET(_c) ? true : false)
#define BAB_DIV2_BIT(_c) (BAB_DIV2_SET(_c) ? false : true)
#define BAB_SLOW_BIT(_c) (BAB_SLOW_SET(_c) ? true : false)
#define BAB_OCLK_BIT(_c) (BAB_OCLK_SET(_c) ? true : false)

#define BAB_COUNT_ADDR 0x0100
#define BAB_W1A_ADDR 0x1000
#define BAB_W1B_ADDR 0x1400
#define BAB_W2_ADDR 0x1900
#define BAB_INP_ADDR 0x3000
#define BAB_OSC_ADDR 0x6000
#define BAB_REG_ADDR 0x7000

/*
 * valid: 0x01 0x03 0x07 0x0F 0x1F 0x3F 0x7F 0xFF
 * max { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x7F, 0x00 }
 * max { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x3F, 0x00 }
 * avg { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x01, 0x00, 0x00 }
 * slo { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x3F, 0x00 }
 * min { 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }
 * good: 0x1F (97) 0x3F (104) 0x7F (109) 0xFF (104)
 */

#define BAB_OSC 8
static const uint8_t bab_osc_bits[BAB_OSC] =
	{ 0x01, 0x03, 0x07, 0x0F, 0x1F, 0x3F, 0x7F, 0xFF };

static const uint8_t bab_reg_ena[4] = { 0xc1, 0x6a, 0x59, 0xe3 };
static const uint8_t bab_reg_dis[4] = { 0x00, 0x00, 0x00, 0x00 };

#define BAB_NONCE_OFFSETS 3
#define BAB_OFF_0x1C_STA 2
#define BAB_OFF_0x1C_FIN 2
#define BAB_OFF_OTHER_STA 0
#define BAB_OFF_OTHER_FIN 1

#define BAB_EVIL_NONCE 0xe0
#define BAB_EVIL_MASK 0xff

static const uint32_t bab_nonce_offsets[] = {-0x800000, 0, -0x400000};

struct bab_work_send {
	uint32_t midstate[MIDSTATE_UINTS];
	uint32_t ms3steps[MIDSTATE_UINTS];
	uint32_t merkle7;
	uint32_t ntime;
	uint32_t bits;
};

#define BAB_REPLY_NONCES 16
struct bab_work_reply {
	uint32_t nonce[BAB_REPLY_NONCES];
	uint32_t jobsel;
	uint32_t spichk;
};

#define BAB_CHIP_MIN (sizeof(struct bab_work_reply)+16)

#define ALLOC_WITEMS 1024
#define LIMIT_WITEMS 0

// Work
typedef struct witem {
	struct work *work;
	struct bab_work_send chip_input;
	bool ci_setup;
	bool rolled;
	int nonces;
	struct timeval work_start;
} WITEM;

#define ALLOC_SITEMS 8
#define LIMIT_SITEMS 0

// SPI I/O
typedef struct sitem {
	uint32_t siz;
	uint8_t wbuf[BAB_MAXBUF];
	uint8_t rbuf[BAB_MAXBUF];
	uint32_t chip_off[BAB_MAXCHIPS+1];
	uint32_t bank_off[BAB_MAXBANKS+2];
	// WITEMs used to build the work
	K_ITEM *witems[BAB_MAXCHIPS];
	struct timeval work_start;
} SITEM;

#define ALLOC_RITEMS 256
#define LIMIT_RITEMS 0

// Results
typedef struct ritem {
	int chip;
	int nonces;
	uint32_t nonce[BAB_REPLY_NONCES];
	bool not_first_reply;
	struct timeval when;
} RITEM;

#define ALLOC_NITEMS 102400
#define LIMIT_NITEMS 0

// Nonce History
typedef struct nitem {
	struct timeval found;
} NITEM;

#define DATAW(_item) ((WITEM *)(_item->data))
#define DATAS(_item) ((SITEM *)(_item->data))
#define DATAR(_item) ((RITEM *)(_item->data))
#define DATAN(_item) ((NITEM *)(_item->data))

// Record the number of each band between work sends
#define BAB_DELAY_BANDS 10
#define BAB_DELAY_BASE 0.5
#define BAB_DELAY_STEP 0.2

#define BAB_CHIP_SPEEDS 6
// less than or equal GH/s
static double chip_speed_ranges[BAB_CHIP_SPEEDS - 1] =
	{ 0.0, 0.8, 1.6, 2.2, 2.8 };
// Greater than the last one above means it's the last speed
static char *chip_speed_names[BAB_CHIP_SPEEDS] =
	{ "Bad", "V.Slow", "Slow", "OK", "Good", "Fast" };

/*
 * This is required to do chip tuning
 * If disabled, it will simply run the chips at default speed
 * unless they never return valid results
 */
#define UPDATE_HISTORY 1

struct bab_info {
	struct thr_info spi_thr;
	struct thr_info res_thr;

	pthread_mutex_t did_lock;
	pthread_mutex_t nonce_lock;

	// All GPIO goes through this
	volatile unsigned *gpio;

	int version;
	int spifd;
	int chips;
	int chips_per_bank[BAB_MAXBANKS+1];
	int missing_chips_per_bank[BAB_MAXBANKS+1];
	int bank_first_chip[BAB_MAXBANKS+1];
	int bank_last_chip[BAB_MAXBANKS+1];
	int boards;
	int banks;
	uint32_t chip_spis[BAB_MAXCHIPS+1];

	int reply_wait;
	uint64_t reply_waits;

	cgsem_t scan_work;
	cgsem_t spi_work;
	cgsem_t spi_reply;
	cgsem_t process_reply;

	bool disabled[BAB_MAXCHIPS];
	int total_disabled;

	struct bab_work_reply chip_results[BAB_MAXCHIPS];
	struct bab_work_reply chip_prev[BAB_MAXCHIPS];

	uint8_t chip_fast[BAB_MAXCHIPS];
	uint8_t chip_conf[BAB_MAXCHIPS];
	uint8_t old_fast[BAB_MAXCHIPS];
	uint8_t old_conf[BAB_MAXCHIPS];
	uint8_t chip_bank[BAB_MAXCHIPS+1];

	uint8_t osc[BAB_OSC];

	/*
	 * Ignore errors in the first work reply since
	 * they may be from a previous run or random junk
	 * There can be >100 with just one 16 chip board
	 */
	uint32_t initial_ignored;
	bool not_first_reply[BAB_MAXCHIPS];

	// Stats
	uint64_t core_good[BAB_MAXCHIPS][BAB_CORES];
	uint64_t core_bad[BAB_MAXCHIPS][BAB_CORES];
	uint64_t chip_spie[BAB_MAXCHIPS]; // spi errors
	uint64_t chip_miso[BAB_MAXCHIPS]; // msio errors
	uint64_t chip_nonces[BAB_MAXCHIPS];
	uint64_t chip_good[BAB_MAXCHIPS];
	uint64_t chip_bad[BAB_MAXCHIPS];
	uint64_t chip_ncore[BAB_MAXCHIPS][BAB_X_COORD][BAB_Y_COORD];

	uint64_t chip_cont_bad[BAB_MAXCHIPS];
	uint64_t chip_max_bad[BAB_MAXCHIPS];

	uint64_t discarded_e0s;

	uint64_t untested_nonces;
	uint64_t tested_nonces;

	uint64_t new_nonces;
	uint64_t ok_nonces;

	uint64_t nonce_offset_count[BAB_NONCE_OFFSETS];
	uint64_t total_tests;
	uint64_t max_tests_per_nonce;
	uint64_t total_links;
	uint64_t total_proc_links;
	uint64_t max_links;
	uint64_t max_proc_links;
	uint64_t total_work_links;

	uint64_t fail;
	uint64_t fail_total_tests;
	uint64_t fail_total_links;
	uint64_t fail_total_work_links;

	uint64_t ign_total_tests;
	uint64_t ign_total_links;
	uint64_t ign_total_work_links;

	struct timeval last_sent_work;
	uint64_t delay_count;
	double delay_min;
	double delay_max;
	/*
	 * 0 is below band ranges
	 * BAB_DELAY_BANDS+1 is above band ranges
	 */
	uint64_t delay_bands[BAB_DELAY_BANDS+2];

	uint64_t send_count;
	double send_total;
	double send_min;
	double send_max;

	// Work
	K_LIST *wfree_list;
	K_STORE *available_work;
	K_STORE *chip_work[BAB_MAXCHIPS];

	// SPI I/O
	K_LIST *sfree_list;
	// Waiting to send
	K_STORE *spi_list;
	// Sent
	K_STORE *spi_sent;

	// Results
	K_LIST *rfree_list;
	K_STORE *res_list;

	// Nonce History
	K_LIST *nfree_list;
	K_STORE *good_nonces[BAB_MAXCHIPS];
	K_STORE *bad_nonces[BAB_MAXCHIPS];

	struct timeval first_work[BAB_MAXCHIPS];
#if UPDATE_HISTORY
	uint32_t work_count[BAB_MAXCHIPS];
	struct timeval last_tune[BAB_MAXCHIPS];
	uint8_t bad_fast[BAB_MAXCHIPS];
	bool bad_msg[BAB_MAXCHIPS];
#endif
	uint64_t work_unrolled;
	uint64_t work_rolled;

	// bab-options (in order)
	uint8_t max_speed;
	uint8_t def_speed;
	uint8_t min_speed;
	double tune_up;
	double tune_down;
	uint32_t speed_hz;
	uint16_t delay_usecs;
	uint64_t trf_delay;

	struct timeval last_did;

	bool initialised;
};

/*
 * Amount of time for history
 * Older items in nonce_history are discarded
 * 300s / 5 minutes
 */
#define HISTORY_TIME_S 300

/*
 * If the SPI I/O thread waits longer than this long for work
 * it will report an error saying how long it's waiting
 * and again every BAB_STD_WAIT_mS after that
 */
#define BAB_LONG_uS 1200000

/*
 * If work wasn't available early enough,
 * report every BAB_LONG_WAIT_mS until it is
 */
#define BAB_LONG_WAIT_mS 888

/*
 * Some amount of time to wait for work
 * before checking how long we've waited
 */
#define BAB_STD_WAIT_mS 888

/*
 * How long to wait for the ioctl() to complete (per BANK)
 * This is a failsafe in case the ioctl() fails
 * since bab_txrx() will already post a wakeup when it completes
 * V1 is set to this x 2
 * V2 is set to this x active banks
 */
#define BAB_REPLY_WAIT_mS 160

/*
 * Work items older than this should not expect results
 * It has to allow for the result buffer returned with the next result
 * 0.75GH/s takes 5.727s to do a full nonce range
 * If HW is too high, consider increasing this to see if work is being
 * expired too early (due to slow chips)
 */
#define BAB_WORK_EXPIRE_mS 7800

// Don't send work more often than this
#define BAB_EXPECTED_WORK_DELAY_mS 899

/*
 * If a chip only has bad results after this time limit in seconds,
 * then switch it down to min_speed
 */
#define BAB_BAD_TO_MIN (HISTORY_TIME_S + 10)

/*
 * Also, just to be sure it's actually mining, it must have got this
 * many bad results before considering disabling it
 */
#define BAB_BAD_COUNT 100

/*
 * If a chip only has bad results after this time limit in seconds,
 * then disable it
 * A chip only returning bad results will use a lot more CPU than
 * an ok chip since all results will be tested against all unexpired
 * work that's been sent to the chip
 */
#define BAB_BAD_DEAD (BAB_BAD_TO_MIN * 2)

/*
 * Maximum bab_queue_full() will roll work if it is allowed to
 * Since work can somtimes (rarely) queue up with many chips,
 * limit it to avoid it getting too much range in the pending work
 */
#define BAB_MAX_ROLLTIME 42

static void bab_ms3steps(uint32_t *p)
{
	uint32_t a, b, c, d, e, f, g, h, new_e, new_a;
	int i;

	a = p[0];
	b = p[1];
	c = p[2];
	d = p[3];
	e = p[4];
	f = p[5];
	g = p[6];
	h = p[7];
	for (i = 0; i < 3; i++) {
		new_e = p[i+16] + sha256_k[i] + h + CH(e,f,g) + SHA256_F2(e) + d;
		new_a = p[i+16] + sha256_k[i] + h + CH(e,f,g) + SHA256_F2(e) +
			SHA256_F1(a) + MAJ(a,b,c);
		d = c;
		c = b;
		b = a;
		a = new_a;
		h = g;
		g = f;
		f = e;
		e = new_e;
	}
	p[15] = a;
	p[14] = b;
	p[13] = c;
	p[12] = d;
	p[11] = e;
	p[10] = f;
	p[9] = g;
	p[8] = h;
}

static uint32_t bab_decnonce(uint32_t in)
{
	uint32_t out;

	/* First part load */
	out = (in & 0xFF) << 24;
	in >>= 8;

	/* Byte reversal */
	in = (((in & 0xaaaaaaaa) >> 1) | ((in & 0x55555555) << 1));
	in = (((in & 0xcccccccc) >> 2) | ((in & 0x33333333) << 2));
	in = (((in & 0xf0f0f0f0) >> 4) | ((in & 0x0f0f0f0f) << 4));

	out |= (in >> 2) & 0x3FFFFF;

	/* Extraction */
	if (in & 1)
		out |= (1 << 23);
	if (in & 2)
		out |= (1 << 22);

	out -= 0x800004;
	return out;
}

static void cleanup_older(struct cgpu_info *babcgpu, int chip, K_ITEM *witem)
{
	struct bab_info *babinfo = (struct bab_info *)(babcgpu->device_data);
	struct timeval now;
	bool expired_item;
	K_ITEM *tail;

	cgtime(&now);

	K_WLOCK(babinfo->chip_work[chip]);
	tail = babinfo->chip_work[chip]->tail;
	expired_item = false;
	// Discard expired work
	while (tail) {
		if (ms_tdiff(&now, &(DATAW(tail)->work_start)) < BAB_WORK_EXPIRE_mS)
			break;

		if (tail == witem)
			expired_item = true;

		k_unlink_item(babinfo->chip_work[chip], tail);
		K_WUNLOCK(babinfo->chip_work[chip]);
		if (DATAW(tail)->rolled)
			free_work(DATAW(tail)->work);
		else
			work_completed(babcgpu, DATAW(tail)->work);
		K_WLOCK(babinfo->chip_work[chip]);
		k_add_head(babinfo->wfree_list, tail);
		tail = babinfo->chip_work[chip]->tail;
	}
	// If we didn't expire witem, then remove all older than it
	if (!expired_item && witem && witem->next) {
		tail = babinfo->chip_work[chip]->tail;
		while (tail && tail != witem) {
			k_unlink_item(babinfo->chip_work[chip], tail);
			K_WUNLOCK(babinfo->chip_work[chip]);
			if (DATAW(tail)->rolled)
				free_work(DATAW(tail)->work);
			else
				work_completed(babcgpu, DATAW(tail)->work);
			K_WLOCK(babinfo->chip_work[chip]);
			k_add_head(babinfo->wfree_list, tail);
			tail = babinfo->chip_work[chip]->tail;
		}
	}
	K_WUNLOCK(babinfo->chip_work[chip]);
}

static void _bab_reset(__maybe_unused struct cgpu_info *babcgpu, struct bab_info *babinfo, int bank, int times)
{
	const int banks[BAB_MAXBANKS] = { 18, 23, 24, 25 };
	int i;

	BAB_INP_GPIO(10);
	BAB_OUT_GPIO(10);
	BAB_INP_GPIO(11);
	BAB_OUT_GPIO(11);

	if (bank) {
		for (i = 0; i < BAB_MAXBANKS; i++) {
			BAB_INP_GPIO(banks[i]);
			BAB_OUT_GPIO(banks[i]);
			if (bank == i+1)
				BAB_GPIO_SET = 1 << banks[i];
			else
				BAB_GPIO_CLR = 1 << banks[i];
		}
		cgsleep_us(4096);
	} else {
		for (i = 0; i < BAB_MAXBANKS; i++)
			BAB_INP_GPIO(banks[i]);
	}

	BAB_GPIO_SET = 1 << 11;
	for (i = 0; i < times; i++) { // 1us = 1MHz
		BAB_GPIO_SET = 1 << 10;
		cgsleep_us(1);
		BAB_GPIO_CLR = 1 << 10;
		cgsleep_us(1);
	}
	BAB_GPIO_CLR = 1 << 11;
	BAB_INP_GPIO(11);
	BAB_INP_GPIO(10);
	BAB_INP_GPIO(9);
	BAB_OUT_GPIO_V(11, 0);
	BAB_OUT_GPIO_V(10, 0);
	BAB_OUT_GPIO_V(9, 0);
}

// TODO: handle a false return where this is called?
static bool _bab_txrx(struct cgpu_info *babcgpu, struct bab_info *babinfo, K_ITEM *item, bool detect_ignore, const char *file, const char *func, const int line)
{
	int bank, i, count, chip1, chip2;
	uint32_t siz, pos;
	struct spi_ioc_transfer tran;
	uintptr_t rbuf, wbuf;

	wbuf = (uintptr_t)(DATAS(item)->wbuf);
	rbuf = (uintptr_t)(DATAS(item)->rbuf);
	siz = (uint32_t)(DATAS(item)->siz);

	memset(&tran, 0, sizeof(tran));
	tran.speed_hz = babinfo->speed_hz;
	tran.delay_usecs = babinfo->delay_usecs;

	i = 0;
	pos = 0;
	for (bank = 0; bank <= BAB_MAXBANKS; bank++) {
		if (DATAS(item)->bank_off[bank]) {
			bab_reset(bank, 64);
			break;
		}
	}

	if (unlikely(bank > BAB_MAXBANKS)) {
		applog(LOG_ERR, "%s%d: %s() failed to find a bank" BAB_FFL,
				babcgpu->drv->name, babcgpu->device_id,
				__func__, BAB_FFL_PASS);
		return false;
	}

	count = 0;
	while (siz > 0) {
		tran.tx_buf = wbuf;
		tran.rx_buf = rbuf;
		tran.speed_hz = BAB_SPI_SPEED;
		if (pos == DATAS(item)->bank_off[bank]) {
			for (; ++bank <= BAB_MAXBANKS; ) {
				if (DATAS(item)->bank_off[bank] > pos) {
					bab_reset(bank, 64);
					break;
				}
			}
		}
		if (siz < BAB_SPI_BUFSIZ)
			tran.len = siz;
		else
			tran.len = BAB_SPI_BUFSIZ;

		if (pos < DATAS(item)->bank_off[bank] &&
		    DATAS(item)->bank_off[bank] < (pos + tran.len))
			tran.len = DATAS(item)->bank_off[bank] - pos;

		for (; i < babinfo->chips; i++) {
			if (!DATAS(item)->chip_off[i])
				continue;
			if (DATAS(item)->chip_off[i] >= pos + tran.len) {
				tran.speed_hz = babinfo->chip_spis[i];
				break;
			}
		}

		if (unlikely(i > babinfo->chips)) {
			applog(LOG_ERR, "%s%d: %s() failed to find chip" BAB_FFL,
					babcgpu->drv->name, babcgpu->device_id,
					__func__, BAB_FFL_PASS);
			return false;
		}

		if (unlikely(babinfo->chip_spis[i] == BAB_SPI_SPEED)) {
			applog(LOG_DEBUG, "%s%d: %s() chip[%d] speed %d shouldn't be %d" BAB_FFL,
						babcgpu->drv->name, babcgpu->device_id,
						__func__, i, (int)babinfo->chip_spis[i],
						BAB_SPI_SPEED, BAB_FFL_PASS);
		}

		if (unlikely(tran.speed_hz == BAB_SPI_SPEED)) {
			applog(LOG_DEBUG, "%s%d: %s() transfer speed %d shouldn't be %d" BAB_FFL,
						babcgpu->drv->name, babcgpu->device_id,
						__func__, (int)tran.speed_hz,
						BAB_SPI_SPEED, BAB_FFL_PASS);
		}

		count++;
		if (ioctl(babinfo->spifd, SPI_IOC_MESSAGE(1), (void *)&tran) < 0) {
			if (!detect_ignore || errno != 110) {
				for (bank = BAB_MAXBANKS; bank >= 0; bank--) {
					if (DATAS(item)->bank_off[bank] &&
					    pos >= DATAS(item)->bank_off[bank]) {
						break;
					}
				}
				for (chip1 = babinfo->chips-1; chip1 >= 0; chip1--) {
					if (DATAS(item)->chip_off[chip1] &&
					    pos >= DATAS(item)->chip_off[chip1]) {
						break;
					}
				}
				for (chip2 = babinfo->chips-1; chip2 >= 0; chip2--) {
					if (DATAS(item)->chip_off[chip2] &&
					    (pos + tran.len) >= DATAS(item)->chip_off[chip2]) {
						break;
					}
				}
				applog(LOG_ERR, "%s%d: ioctl (%d) siz=%d bank=%d chip=%d-%d"
						" failed err=%d" BAB_FFL,
						babcgpu->drv->name,
						babcgpu->device_id,
						count, (int)(tran.len),
						bank, chip1, chip2,
						errno, BAB_FFL_PASS);
			}
			return false;
		}

		siz -= tran.len;
		wbuf += tran.len;
		rbuf += tran.len;
		pos += tran.len;

		if (siz > 0 && babinfo->trf_delay > 0)
			cgsleep_us(babinfo->trf_delay);
	}
	cgtime(&(DATAS(item)->work_start));
	mutex_lock(&(babinfo->did_lock));
	cgtime(&(babinfo->last_did));
	mutex_unlock(&(babinfo->did_lock));
	return true;
}

static void _bab_add_buf_rev(K_ITEM *item, const uint8_t *data, uint32_t siz, const char *file, const char *func, const int line)
{
	uint32_t now_used, i;
	uint8_t tmp;

	now_used = DATAS(item)->siz;
	if (now_used + siz >= BAB_MAXBUF) {
		quitfrom(1, file, func, line,
			"%s() buffer limit of %d exceeded=%d siz=%d",
			__func__, BAB_MAXBUF, (int)(now_used + siz), (int)siz);
	}

	for (i = 0; i < siz; i++) {
		tmp = data[i];
		tmp = ((tmp & 0xaa)>>1) | ((tmp & 0x55) << 1);
		tmp = ((tmp & 0xcc)>>2) | ((tmp & 0x33) << 2);
		tmp = ((tmp & 0xf0)>>4) | ((tmp & 0x0f) << 4);
		DATAS(item)->wbuf[now_used + i] = tmp;
	}

	DATAS(item)->siz += siz;
}

static void _bab_add_buf(K_ITEM *item, const uint8_t *data, size_t siz, const char *file, const char *func, const int line)
{
	uint32_t now_used;

	now_used = DATAS(item)->siz;
	if (now_used + siz >= BAB_MAXBUF) {
		quitfrom(1, file, func, line,
			"%s() DATAS buffer limit of %d exceeded=%d siz=%d",
			__func__, BAB_MAXBUF, (int)(now_used + siz), (int)siz);
	}

	memcpy(&(DATAS(item)->wbuf[now_used]), data, siz);
	DATAS(item)->siz += siz;
}

static void _bab_add_noops(K_ITEM *item, size_t siz, const char *file, const char *func, const int line)
{
	uint32_t now_used;

	now_used = DATAS(item)->siz;
	if (now_used + siz >= BAB_MAXBUF) {
		quitfrom(1, file, func, line,
			"%s() DATAS buffer limit of %d exceeded=%d siz=%d",
			__func__, BAB_MAXBUF, (int)(now_used + siz), (int)siz);
	}

	memset(&(DATAS(item)->wbuf[now_used]), BAB_NOOP, siz);
	DATAS(item)->siz += siz;
}

static void _bab_add_data(K_ITEM *item, uint32_t addr, const uint8_t *data, size_t siz, const char *file, const char *func, const int line)
{
	uint8_t tmp[3];
	int trf_siz;

	if (siz < BAB_ADD_MIN || siz > BAB_ADD_MAX) {
		quitfrom(1, file, func, line,
			"%s() called with invalid siz=%d (min=%d max=%d)",
			__func__, (int)siz, BAB_ADD_MIN, BAB_ADD_MAX);
	}
	trf_siz = siz / 4;
	tmp[0] = (trf_siz - 1) | 0xE0;
	tmp[1] = (addr >> 8) & 0xff;
	tmp[2] = addr & 0xff;
	_bab_add_buf(item, tmp, sizeof(tmp), BAB_FFL_PASS);
	_bab_add_buf_rev(item, data, siz, BAB_FFL_PASS);
}

static void _bab_config_reg(K_ITEM *item, uint32_t reg, bool enable, const char *file, const char *func, const int line)
{
	if (enable) {
		_bab_add_data(item, BAB_REG_ADDR + reg*32,
				bab_reg_ena, sizeof(bab_reg_ena), BAB_FFL_PASS);
	} else {
		_bab_add_data(item, BAB_REG_ADDR + reg*32,
				bab_reg_dis, sizeof(bab_reg_dis), BAB_FFL_PASS);
	}

}

static void bab_set_osc(struct bab_info *babinfo, int chip)
{
	int fast, i;

	fast = babinfo->chip_fast[chip];

	for (i = 0; i < BAB_OSC && fast > BAB_OSC; i++, fast -= BAB_OSC) {
		babinfo->osc[i] = 0xff;
	}
	if (i < BAB_OSC && fast > 0 && fast <= BAB_OSC)
		babinfo->osc[i++] = bab_osc_bits[fast - 1];
	for (; i < BAB_OSC; i++)
		babinfo->osc[i] = 0x00;

	applog(LOG_DEBUG, "@osc(chip=%d) fast=%d 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x", chip, fast, babinfo->osc[0], babinfo->osc[1], babinfo->osc[2], babinfo->osc[3], babinfo->osc[4], babinfo->osc[5], babinfo->osc[6], babinfo->osc[7]);
}

static void bab_put(struct bab_info *babinfo, K_ITEM *sitem)
{
	struct bab_work_send *chip_input;
	int i, reg, bank = 0;
	size_t chip_siz;

	BAB_ADD_BREAK(sitem);
	for (i = 0; i < babinfo->chips; i++) {
		if (babinfo->chip_bank[i] != bank) {
			DATAS(sitem)->bank_off[bank] = DATAS(sitem)->siz;
			bank = babinfo->chip_bank[i];
			BAB_ADD_BREAK(sitem);
		}
		if (!(babinfo->disabled[i])) {
			if (BAB_CFGD_SET(babinfo->chip_conf[i]) || !babinfo->chip_conf[i]) {
				bab_set_osc(babinfo, i);
				bab_add_data(sitem, BAB_OSC_ADDR, babinfo->osc, sizeof(babinfo->osc));
				bab_config_reg(sitem, BAB_ICLK_REG, BAB_ICLK_BIT(babinfo->chip_conf[i]));
				bab_config_reg(sitem, BAB_FAST_REG, BAB_FAST_BIT(babinfo->chip_conf[i]));
				bab_config_reg(sitem, BAB_DIV2_REG, BAB_DIV2_BIT(babinfo->chip_conf[i]));
				bab_config_reg(sitem, BAB_SLOW_REG, BAB_SLOW_BIT(babinfo->chip_conf[i]));
				bab_config_reg(sitem, BAB_OCLK_REG, BAB_OCLK_BIT(babinfo->chip_conf[i]));
				for (reg = BAB_REG_CLR_FROM; reg <= BAB_REG_CLR_TO; reg++)
					bab_config_reg(sitem, reg, false);
				if (babinfo->chip_conf[i]) {
					bab_add_data(sitem, BAB_COUNT_ADDR, bab_counters, sizeof(bab_counters));
					bab_add_data(sitem, BAB_W1A_ADDR, bab_w1, sizeof(bab_w1));
					bab_add_data(sitem, BAB_W1B_ADDR, bab_w1, sizeof(bab_w1)/2);
					bab_add_data(sitem, BAB_W2_ADDR, bab_w2, sizeof(bab_w2));
					babinfo->chip_conf[i] ^= BAB_CFGD_VAL;
				}
				babinfo->old_fast[i] = babinfo->chip_fast[i];
				babinfo->old_conf[i] = babinfo->chip_conf[i];
			} else {
				if (babinfo->old_fast[i] != babinfo->chip_fast[i]) {
					bab_set_osc(babinfo, i);
					bab_add_data(sitem, BAB_OSC_ADDR, babinfo->osc, sizeof(babinfo->osc));
					babinfo->old_fast[i] = babinfo->chip_fast[i];
				}
				if (babinfo->old_conf[i] != babinfo->chip_conf[i]) {
					if (BAB_ICLK_SET(babinfo->old_conf[i]) !=
							 BAB_ICLK_SET(babinfo->chip_conf[i]))
						bab_config_reg(sitem, BAB_ICLK_REG,
								BAB_ICLK_BIT(babinfo->chip_conf[i]));
					if (BAB_FAST_SET(babinfo->old_conf[i]) !=
							 BAB_FAST_SET(babinfo->chip_conf[i]))
						bab_config_reg(sitem, BAB_FAST_REG,
								BAB_FAST_BIT(babinfo->chip_conf[i]));
					if (BAB_DIV2_SET(babinfo->old_conf[i]) !=
							 BAB_DIV2_SET(babinfo->chip_conf[i]))
						bab_config_reg(sitem, BAB_DIV2_REG,
								BAB_DIV2_BIT(babinfo->chip_conf[i]));
					if (BAB_SLOW_SET(babinfo->old_conf[i]) !=
							 BAB_SLOW_SET(babinfo->chip_conf[i]))
						bab_config_reg(sitem, BAB_SLOW_REG,
								BAB_SLOW_BIT(babinfo->chip_conf[i]));
					if (BAB_OCLK_SET(babinfo->old_conf[i]) !=
							 BAB_OCLK_SET(babinfo->chip_conf[i]))
						bab_config_reg(sitem, BAB_OCLK_REG,
								BAB_OCLK_BIT(babinfo->chip_conf[i]));
					babinfo->old_conf[i] = babinfo->chip_conf[i];
				}
			}
			DATAS(sitem)->chip_off[i] = DATAS(sitem)->siz + 3;
			chip_input = &(DATAW(DATAS(sitem)->witems[i])->chip_input);

			if (babinfo->chip_conf[i])
				bab_add_data(sitem, BAB_INP_ADDR, (uint8_t *)chip_input, sizeof(*chip_input));

			chip_siz = DATAS(sitem)->siz - babinfo->chip_conf[i];
			if (chip_siz < BAB_CHIP_MIN)
				BAB_ADD_NOOPs(sitem, BAB_CHIP_MIN - chip_siz);
		}
		BAB_ADD_ASYNC(sitem);
	}
	DATAS(sitem)->chip_off[i] = DATAS(sitem)->siz;
	DATAS(sitem)->bank_off[bank] = DATAS(sitem)->siz;

	K_WLOCK(babinfo->spi_list);
	k_add_head(babinfo->spi_list, sitem);
	K_WUNLOCK(babinfo->spi_list);

	cgsem_post(&(babinfo->spi_work));
}

static bool bab_get(__maybe_unused struct cgpu_info *babcgpu, struct bab_info *babinfo, struct timeval *when)
{
	K_ITEM *item;
	bool delayed;
	int i;

	item = NULL;
	delayed = false;
	while (item == NULL) {
		cgsem_mswait(&(babinfo->spi_reply), babinfo->reply_wait);

		K_WLOCK(babinfo->spi_sent);
		item = k_unlink_tail(babinfo->spi_sent);
		K_WUNLOCK(babinfo->spi_sent);

		if (!item) {
			if (!delayed) {
				applog(LOG_WARNING, "%s%d: Delay getting work reply ...",
							babcgpu->drv->name,
							babcgpu->device_id);
				delayed = true;
				babinfo->reply_waits++;
			}
		}
	}

	for (i = 0; i < babinfo->chips; i++) {
		if (babinfo->chip_conf[i] & 0x7f) {
			memcpy((void *)&(babinfo->chip_results[i]),
				(void *)(DATAS(item)->rbuf + DATAS(item)->chip_off[i]),
				sizeof(babinfo->chip_results[0]));
		}
	}

	// work_start is also the time the results were read
	memcpy(when, &(DATAS(item)->work_start), sizeof(*when));

	K_WLOCK(babinfo->sfree_list);
	k_add_head(babinfo->sfree_list, item);
	K_WUNLOCK(babinfo->sfree_list);

	return true;
}

void bab_detect_chips(struct cgpu_info *babcgpu, struct bab_info *babinfo, int bank, int first, int last)
{
	int i, reg, j;
	K_ITEM *item;

	if (sizeof(struct bab_work_send) != sizeof(bab_test_data)) {
		quithere(1, "struct bab_work_send (%d) and bab_test_data (%d)"
			    " must be the same size",
			    (int)sizeof(struct bab_work_send),
			    (int)sizeof(bab_test_data));
	}

	K_WLOCK(babinfo->sfree_list);
	item = k_unlink_head_zero(babinfo->sfree_list);
	K_WUNLOCK(babinfo->sfree_list);
	BAB_ADD_BREAK(item);
	for (i = first; i < last && i < BAB_MAXCHIPS; i++) {
		bab_set_osc(babinfo, i);
		bab_add_data(item, BAB_OSC_ADDR, babinfo->osc, sizeof(babinfo->osc));
		bab_config_reg(item, BAB_ICLK_REG, BAB_ICLK_BIT(babinfo->chip_conf[i]));
		bab_config_reg(item, BAB_FAST_REG, BAB_FAST_BIT(babinfo->chip_conf[i]));
		bab_config_reg(item, BAB_DIV2_REG, BAB_DIV2_BIT(babinfo->chip_conf[i]));
		bab_config_reg(item, BAB_SLOW_REG, BAB_SLOW_BIT(babinfo->chip_conf[i]));
		bab_config_reg(item, BAB_OCLK_REG, BAB_OCLK_BIT(babinfo->chip_conf[i]));
		for (reg = BAB_REG_CLR_FROM; reg <= BAB_REG_CLR_TO; reg++)
			bab_config_reg(item, reg, false);
		bab_add_data(item, BAB_COUNT_ADDR, bab_counters, sizeof(bab_counters));
		bab_add_data(item, BAB_W1A_ADDR, bab_w1, sizeof(bab_w1));
		bab_add_data(item, BAB_W1B_ADDR, bab_w1, sizeof(bab_w1)/2);
		bab_add_data(item, BAB_W2_ADDR, bab_w2, sizeof(bab_w2));
		DATAS(item)->chip_off[i] = DATAS(item)->siz + 3;
		bab_add_data(item, BAB_INP_ADDR, bab_test_data, sizeof(bab_test_data));
		DATAS(item)->chip_off[i+1] = DATAS(item)->siz;
		DATAS(item)->bank_off[bank] = DATAS(item)->siz;
		babinfo->chips = i + 1;
		bab_txrx(item, false);
		DATAS(item)->siz = 0;
		BAB_ADD_BREAK(item);
		for (j = first; j <= i; j++) {
			DATAS(item)->chip_off[j] = DATAS(item)->siz + 3;
			BAB_ADD_ASYNC(item);
		}
	}

	memset(item->data, 0, babinfo->sfree_list->siz);
	BAB_ADD_BREAK(item);
	for (i = first; i < last && i < BAB_MAXCHIPS; i++) {
		DATAS(item)->chip_off[i] = DATAS(item)->siz + 3;
		bab_add_data(item, BAB_INP_ADDR, bab_test_data, sizeof(bab_test_data));
		BAB_ADD_ASYNC(item);
	}
	DATAS(item)->chip_off[i] = DATAS(item)->siz;
	DATAS(item)->bank_off[bank] = DATAS(item)->siz;
	babinfo->chips = i;
	bab_txrx(item, true);
	DATAS(item)->siz = 0;
	babinfo->chips = first;
	for (i = first; i < last && i < BAB_MAXCHIPS; i++) {
		uint32_t tmp[DATA_UINTS-1];
		memcpy(tmp, DATAS(item)->rbuf + DATAS(item)->chip_off[i], sizeof(tmp));
		DATAS(item)->chip_off[i] = 0;
		for (j = 0; j < BAB_REPLY_NONCES; j++) {
			if (tmp[j] != 0xffffffff && tmp[j] != 0x00000000) {
				babinfo->chip_bank[i] = bank;
				babinfo->chips = i + 1;
				break;
			}
		}
	}
	for (i = first ; i < babinfo->chips; i++)
		babinfo->chip_bank[i] = bank;

	K_WLOCK(babinfo->sfree_list);
	k_add_head(babinfo->sfree_list, item);
	K_WUNLOCK(babinfo->sfree_list);
}

static const char *bab_modules[] = {
	"i2c-dev",
	"i2c-bcm2708",
	"spidev",
	"spi-bcm2708",
	NULL
};

static const char *bab_memory = "/dev/mem";

static int bab_memory_addr = 0x20200000;

// TODO: add --bab-options for SPEED_HZ, tran.delay_usecs and an inter transfer delay (default 0)
static struct {
	int request;
	int value;
} bab_ioc[] = {
	{ SPI_IOC_RD_MODE, 0 },
	{ SPI_IOC_WR_MODE, 0 },
	{ SPI_IOC_RD_BITS_PER_WORD, 8 },
	{ SPI_IOC_WR_BITS_PER_WORD, 8 },
	{ SPI_IOC_RD_MAX_SPEED_HZ, 1000000 },
	{ SPI_IOC_WR_MAX_SPEED_HZ, 1000000 },
	{ -1, -1 }
};

static bool bab_init_gpio(struct cgpu_info *babcgpu, struct bab_info *babinfo, int bus, int chip)
{
	int i, err, memfd, data;
	char buf[64];

	bab_ioc[4].value = (int)(babinfo->speed_hz);
	bab_ioc[5].value = (int)(babinfo->speed_hz);

	for (i = 0; bab_modules[i]; i++) {
		snprintf(buf, sizeof(buf), "modprobe %s", bab_modules[i]);
		err = system(buf);
		if (err) {
			applog(LOG_ERR, "%s failed to modprobe %s (%d) - you need to be root?",
					babcgpu->drv->dname,
					bab_modules[i], err);
			goto bad_out;
		}
	}

	memfd = open(bab_memory, O_RDWR | O_SYNC);
	if (memfd < 0) {
		applog(LOG_ERR, "%s failed open %s (%d)",
				babcgpu->drv->dname,
				bab_memory, errno);
		goto bad_out;
	}

	babinfo->gpio = (volatile unsigned *)mmap(NULL, BAB_SPI_BUFSIZ,
						  PROT_READ | PROT_WRITE,
						  MAP_SHARED, memfd,
						  bab_memory_addr);
	if (babinfo->gpio == MAP_FAILED) {
		close(memfd);
		applog(LOG_ERR, "%s failed mmap gpio (%d)",
				babcgpu->drv->dname,
				errno);
		goto bad_out;
	}

	close(memfd);

	snprintf(buf, sizeof(buf), "/dev/spidev%d.%d", bus, chip);
	babinfo->spifd = open(buf, O_RDWR);
	if (babinfo->spifd < 0) {
		applog(LOG_ERR, "%s failed to open spidev (%d)",
				babcgpu->drv->dname,
				errno);
		goto map_out;
	}

	babcgpu->device_path = strdup(buf);

	for (i = 0; bab_ioc[i].value != -1; i++) {
		data = bab_ioc[i].value;
		err = ioctl(babinfo->spifd, bab_ioc[i].request, (void *)&data);
		if (err < 0) {
			applog(LOG_ERR, "%s failed ioctl (%d) (%d)",
					babcgpu->drv->dname,
					i, errno);
			goto close_out;
		}
	}

	for (i = 0; i < BAB_MAXCHIPS; i++)
		babinfo->chip_spis[i] = (int)((1000000.0 / (100.0 + 31.0 * (i + 1))) * 1000);

	return true;

close_out:
	close(babinfo->spifd);
	babinfo->spifd = 0;
	free(babcgpu->device_path);
	babcgpu->device_path = NULL;
map_out:
	munmap((void *)(babinfo->gpio), BAB_SPI_BUFSIZ);
	babinfo->gpio = NULL;
bad_out:
	return false;
}

static void bab_init_chips(struct cgpu_info *babcgpu, struct bab_info *babinfo)
{
	int chip, chipoff, bank, chips, new_chips, boards, mis;

	applog(LOG_WARNING, "%s V1 first test for %d chips ...",
			    babcgpu->drv->dname, BAB_V1_CHIP_TEST);

	bab_detect_chips(babcgpu, babinfo, 0, 0, BAB_V1_CHIP_TEST);
	if (babinfo->chips > 0) {
		babinfo->version = 1;
		babinfo->banks = 0;
		if (babinfo->chips == BAB_V1_CHIP_TEST) {
			applog(LOG_WARNING, "%s V1 test for %d more chips ...",
					    babcgpu->drv->dname, BAB_MAXCHIPS - BAB_V1_CHIP_TEST);

			bab_detect_chips(babcgpu, babinfo, 0, BAB_V1_CHIP_TEST, BAB_MAXCHIPS);
		}
		babinfo->chips_per_bank[BAB_V1_BANK] = babinfo->chips;
		babinfo->bank_first_chip[BAB_V1_BANK] = 0;
		babinfo->bank_last_chip[BAB_V1_BANK] = babinfo->chips - 1;
		babinfo->boards = (int)((float)(babinfo->chips - 1) / BAB_BOARDCHIPS) + 1;
		babinfo->reply_wait = BAB_REPLY_WAIT_mS * 2;

		if ((chip = (babinfo->chips_per_bank[BAB_V1_BANK] % BAB_BOARDCHIPS))) {
			mis = BAB_BOARDCHIPS - chip;
			babinfo->missing_chips_per_bank[BAB_V1_BANK] = mis;
			applog(LOG_WARNING, "%s V1: missing %d chip%s",
					    babcgpu->drv->dname, mis,
					    (mis == 1) ? "" : "s");
		}
	} else {
		applog(LOG_WARNING, "%s no chips found with V1", babcgpu->drv->dname);
		applog(LOG_WARNING, "%s V2 test %d banks %d chips ...",
				    babcgpu->drv->dname, BAB_MAXBANKS, BAB_MAXCHIPS);

		chips = 0;
		babinfo->version = 2;
		babinfo->banks = 0;
		for (bank = 1; bank <= BAB_MAXBANKS; bank++) {
			for (chipoff = 0; chipoff < BAB_BANKCHIPS; chipoff++) {
				chip = babinfo->chips + chipoff;
				babinfo->chip_spis[chip] = 625000;
			}
			bab_reset(bank, 64);
			bab_detect_chips(babcgpu, babinfo, bank, babinfo->chips, babinfo->chips + BAB_BANKCHIPS);
			new_chips = babinfo->chips - chips;
			babinfo->chips_per_bank[bank] = new_chips;
			if (new_chips > 0) {
				babinfo->bank_first_chip[bank] = babinfo->chips - new_chips;
				babinfo->bank_last_chip[bank] = babinfo->chips - 1;
			}
			chips = babinfo->chips;
			if (new_chips == 0)
				boards = 0;
			else {
				boards = (int)((float)(new_chips - 1) / BAB_BOARDCHIPS) + 1;
				babinfo->banks++;
			}
			applog(LOG_WARNING, "%s V2 bank %d: %d chips %d board%s",
					    babcgpu->drv->dname, bank, new_chips,
					    boards, (boards == 1) ? "" : "s");
			babinfo->boards += boards;

			if ((chip = (babinfo->chips_per_bank[bank] % BAB_BOARDCHIPS))) {
				mis = BAB_BOARDCHIPS - chip;
				babinfo->missing_chips_per_bank[bank] = mis;
				applog(LOG_WARNING, "%s V2: bank %d missing %d chip%s",
						    babcgpu->drv->dname, bank,
						    mis, (mis == 1) ? "" : "s");
			}
		}
		babinfo->reply_wait = BAB_REPLY_WAIT_mS * babinfo->banks;
		bab_reset(0, 8);
	}

	memcpy(babinfo->old_conf, babinfo->chip_conf, sizeof(babinfo->old_conf));
	memcpy(babinfo->old_fast, babinfo->chip_fast, sizeof(babinfo->old_fast));
}

static char *bab_options[] = {
	"MaxSpeed",
	"DefaultSpeed",
	"MinSpeed",
	"TuneUp",
	"TuneDown",
	"SPISpeed",
	"SPIDelayuS",
	"TransferDelayuS"
};

#define INVOP " Invalid Option "

static void bab_get_options(struct cgpu_info *babcgpu, struct bab_info *babinfo)
{
	char *ptr, *colon;
	int which, val;
	double fval;
	long lval;

	if (opt_bab_options == NULL)
		return;

	which = 0;
	ptr = opt_bab_options;
	while (ptr && *ptr) {
		colon = strchr(ptr, ':');
		if (colon)
			*(colon++) = '\0';

		switch (which) {
			case 0:
				if (*ptr && tolower(*ptr) != 'd') {
					val = atoi(ptr);
					if (!isdigit(*ptr) || val < BAB_ABSMINSPEED || val > BAB_MAXSPEED) {
						quit(1, "%s"INVOP"%s '%s' must be %d <= %s <= %d",
							babcgpu->drv->dname,
							bab_options[which],
							ptr, BAB_ABSMINSPEED,
							bab_options[which],
							BAB_MAXSPEED);
					}
					babinfo->max_speed = (uint8_t)val;
					// Adjust def,min down if they are above max specified
					if (babinfo->def_speed > babinfo->max_speed)
						babinfo->def_speed = babinfo->max_speed;
					if (babinfo->min_speed > babinfo->max_speed)
						babinfo->min_speed = babinfo->max_speed;
				}
				break;
			case 1:
				if (*ptr && tolower(*ptr) != 'd') {
					val = atoi(ptr);
					if (!isdigit(*ptr) || val < BAB_ABSMINSPEED || val > babinfo->max_speed) {
						quit(1, "%s"INVOP"%s '%s' must be %d <= %s <= %d",
							babcgpu->drv->dname,
							bab_options[which],
							ptr, BAB_ABSMINSPEED,
							bab_options[which],
							babinfo->max_speed);
					}
					babinfo->def_speed = (uint8_t)val;
					// Adjust min down if is is above def specified
					if (babinfo->min_speed > babinfo->def_speed)
						babinfo->min_speed = babinfo->def_speed;
				}
				break;
			case 2:
				if (*ptr && tolower(*ptr) != 'd') {
					val = atoi(ptr);
					if (!isdigit(*ptr) || val < BAB_ABSMINSPEED || val > babinfo->def_speed) {
						quit(1, "%s"INVOP"%s '%s' must be %d <= %s <= %d",
							babcgpu->drv->dname,
							bab_options[which],
							ptr, BAB_ABSMINSPEED,
							bab_options[which],
							babinfo->def_speed);
					}
					babinfo->min_speed = (uint8_t)val;
				}
				break;
			case 3:
				if (*ptr && tolower(*ptr) != 'd') {
					fval = atof(ptr);
					if (!isdigit(*ptr) || fval < 0.0 || fval > 100.0) {
						quit(1, "%s"INVOP"%s '%s' must be 0.0 <= %s <= 100.0",
							babcgpu->drv->dname,
							bab_options[which], ptr,
							bab_options[which]);
					}
					babinfo->tune_up = fval;
				}
				break;
			case 4:
				if (*ptr && tolower(*ptr) != 'd') {
					fval = atof(ptr);
					if (!isdigit(*ptr) || fval < 0.0 || fval > 100.0) {
						quit(1, "%s"INVOP"%s '%s' must be %f <= %s <= 100.0",
							babcgpu->drv->dname,
							bab_options[which],
							ptr, babinfo->tune_up,
							bab_options[which]);
					}
					babinfo->tune_down = fval;
				}
				break;
			case 5:
				if (*ptr && tolower(*ptr) != 'd') {
					val = atoi(ptr);
					if (!isdigit(*ptr) || val < 10000 || val > 10000000) {
						quit(1, "%s"INVOP"%s '%s' must be 10,000 <= %s <= 10,000,000",
							babcgpu->drv->dname,
							bab_options[which], ptr,
							bab_options[which]);
					}
					babinfo->speed_hz = (uint32_t)val;
				}
				break;
			case 6:
				if (*ptr && tolower(*ptr) != 'd') {
					val = atoi(ptr);
					if (!isdigit(*ptr) || val < 0 || val > 65535) {
						quit(1, "%s"INVOP"%s '%s' must be 0 <= %s <= 65535",
							babcgpu->drv->dname,
							bab_options[which], ptr,
							bab_options[which]);
					}
					babinfo->delay_usecs = (uint16_t)val;
				}
				break;
			case 7:
				if (*ptr && tolower(*ptr) != 'd') {
					lval = atol(ptr);
					if (!isdigit(*ptr) || lval < 0) {
						quit(1, "%s"INVOP"%s '%s' must be %s >= 0",
							babcgpu->drv->dname,
							bab_options[which], ptr,
							bab_options[which]);
					}
					babinfo->trf_delay = (uint64_t)lval;
				}
				break;
			default:
				break;
		}
		ptr = colon;
		which++;
	}
}

static void bab_detect(bool hotplug)
{
	struct cgpu_info *babcgpu = NULL;
	struct bab_info *babinfo = NULL;
	int i;

	if (hotplug)
		return;

	babcgpu = calloc(1, sizeof(*babcgpu));
	if (unlikely(!babcgpu))
		quithere(1, "Failed to calloc babcgpu");

	babcgpu->drv = &bab_drv;
	babcgpu->deven = DEV_ENABLED;
	babcgpu->threads = 1;

	babinfo = calloc(1, sizeof(*babinfo));
	if (unlikely(!babinfo))
		quithere(1, "Failed to calloc babinfo");
	babcgpu->device_data = (void *)babinfo;

	babinfo->max_speed = BAB_DEFMAXSPEED;
	babinfo->def_speed = BAB_DEFSPEED;
	babinfo->min_speed = BAB_ABSMINSPEED;

	babinfo->tune_up = BAB_TUNEUP;
	babinfo->tune_down = BAB_TUNEDOWN;

	babinfo->speed_hz = BAB_SPI_SPEED;
	babinfo->delay_usecs = BAB_DELAY_USECS;
	babinfo->trf_delay = BAB_TRF_DELAY;

	bab_get_options(babcgpu, babinfo);

	for (i = 0; i < BAB_MAXCHIPS; i++) {
		babinfo->chip_conf[i] = BAB_DEFCONF;
		babinfo->chip_fast[i] = babinfo->def_speed;
#if UPDATE_HISTORY
		babinfo->bad_fast[i] = babinfo->max_speed + 1;
#endif
	}

	if (!bab_init_gpio(babcgpu, babinfo, BAB_SPI_BUS, BAB_SPI_CHIP))
		goto unalloc;

	babinfo->sfree_list = k_new_list("SPI I/O", sizeof(SITEM),
					 ALLOC_SITEMS, LIMIT_SITEMS, true);
	babinfo->spi_list = k_new_store(babinfo->sfree_list);
	babinfo->spi_sent = k_new_store(babinfo->sfree_list);

	for (i = 0; i <= BAB_MAXBANKS; i++) {
		babinfo->bank_first_chip[i] = -1;
		babinfo->bank_last_chip[i] = -1;
	}

	bab_init_chips(babcgpu, babinfo);

	if (babinfo->boards) {
		applog(LOG_WARNING, "%s found %d chips %d board%s",
				    babcgpu->drv->dname, babinfo->chips,
				    babinfo->boards,
				    (babinfo->boards == 1) ? "" : "s");
	} else {
		applog(LOG_WARNING, "%s found %d chips",
				    babcgpu->drv->dname, babinfo->chips);
	}

	if (babinfo->chips == 0)
		goto cleanup;

	if (!add_cgpu(babcgpu))
		goto cleanup;

	cgsem_init(&(babinfo->scan_work));
	cgsem_init(&(babinfo->spi_work));
	cgsem_init(&(babinfo->spi_reply));
	cgsem_init(&(babinfo->process_reply));

	mutex_init(&babinfo->did_lock);
	mutex_init(&babinfo->nonce_lock);

	babinfo->rfree_list = k_new_list("Results", sizeof(RITEM),
					 ALLOC_RITEMS, LIMIT_RITEMS, true);
	babinfo->res_list = k_new_store(babinfo->rfree_list);

	babinfo->wfree_list = k_new_list("Work", sizeof(WITEM),
					 ALLOC_WITEMS, LIMIT_WITEMS, true);
	babinfo->available_work = k_new_store(babinfo->wfree_list);
	for (i = 0; i < BAB_MAXCHIPS; i++)
		babinfo->chip_work[i] = k_new_store(babinfo->wfree_list);

	babinfo->nfree_list = k_new_list("Nonce History", sizeof(WITEM),
					 ALLOC_NITEMS, LIMIT_NITEMS, true);
	for (i = 0; i < BAB_MAXCHIPS; i++) {
		babinfo->good_nonces[i] = k_new_store(babinfo->nfree_list);
		babinfo->bad_nonces[i] = k_new_store(babinfo->nfree_list);
	}

	// Exclude detection
	cgtime(&(babcgpu->dev_start_tv));
	// Ignore detection tests
	babinfo->last_did.tv_sec = 0;

	babinfo->initialised = true;

	return;

cleanup:
	close(babinfo->spifd);
	munmap((void *)(babinfo->gpio), BAB_SPI_BUFSIZ);
unalloc:
	free(babinfo);
	free(babcgpu);
}

static void bab_identify(__maybe_unused struct cgpu_info *babcgpu)
{
}

// thread to do spi txrx
static void *bab_spi(void *userdata)
{
	struct cgpu_info *babcgpu = (struct cgpu_info *)userdata;
	struct bab_info *babinfo = (struct bab_info *)(babcgpu->device_data);
	struct timeval start, stop, send, now;
	K_ITEM *sitem, *witem;
	double wait, delay;
	int chip, band;

	applog(LOG_DEBUG, "%s%i: SPIing...",
			  babcgpu->drv->name, babcgpu->device_id);

	// Wait until we're ready
	while (babcgpu->shutdown == false) {
		if (babinfo->initialised) {
			break;
		}
		cgsleep_ms(3);
	}

	cgtime(&start);
	while (babcgpu->shutdown == false) {
		K_WLOCK(babinfo->spi_list);
		sitem = k_unlink_tail(babinfo->spi_list);
		K_WUNLOCK(babinfo->spi_list);

		if (!sitem) {
			cgtime(&stop);
			wait = us_tdiff(&stop, &start);
			if (wait > BAB_LONG_uS) {
				applog(LOG_WARNING, "%s%i: SPI waiting %fs ...",
							babcgpu->drv->name,
							babcgpu->device_id,
							(float)wait / 1000000.0);
				cgsem_mswait(&(babinfo->spi_work), BAB_LONG_WAIT_mS);
			} else
				cgsem_mswait(&(babinfo->spi_work), (int)((BAB_LONG_uS - wait) / 1000));
			continue;
		}

		// TODO: need an LP/urgent flag to skip this possible cgsem_mswait()
		// maybe zero last_sent_work.tv_sec ?
		while (babinfo->last_sent_work.tv_sec) {
			cgtime(&now);
			delay = tdiff(&now, &(babinfo->last_sent_work)) * 1000.0;
			if (delay < BAB_EXPECTED_WORK_DELAY_mS)
				cgsem_mswait(&(babinfo->spi_work), BAB_EXPECTED_WORK_DELAY_mS - delay);
			else
				break;
		}

		/*
		 * TODO: handle if an LP happened after bab_do_work() started
		 * i.e. we don't want to send the work
		 * Have an LP counter that at this point would show the work
		 * is stale - so don't send it
		 */
		cgtime(&send);
		bab_txrx(sitem, false);
		cgtime(&start);

		// The work isn't added to the chip until it has been sent
		K_WLOCK(babinfo->wfree_list);
		for (chip = 0; chip < babinfo->chips; chip++) {
			witem = DATAS(sitem)->witems[chip];
			if (witem) {
				memcpy(&(DATAW(witem)->work_start), &(DATAS(sitem)->work_start),
					sizeof(DATAW(witem)->work_start));
				k_add_head(babinfo->chip_work[chip], witem);
#if UPDATE_HISTORY
				babinfo->work_count[chip]++;
#endif
				if (babinfo->first_work[chip].tv_sec == 0)
					memcpy(&(babinfo->first_work[chip]), &send, sizeof(send));
			}
		}
		K_WUNLOCK(babinfo->wfree_list);

		K_WLOCK(babinfo->spi_sent);
		k_add_head(babinfo->spi_sent, sitem);
		K_WUNLOCK(babinfo->spi_sent);

		cgsem_post(&(babinfo->spi_reply));

		// Store stats
		if (babinfo->last_sent_work.tv_sec) {
			delay = tdiff(&send, &(babinfo->last_sent_work));
			babinfo->delay_count++;
			if (babinfo->delay_min == 0 || babinfo->delay_min > delay)
				babinfo->delay_min = delay;
			if (babinfo->delay_max < delay)
				babinfo->delay_max = delay;
			if (delay < BAB_DELAY_BASE)
				band = 0;
			else if (delay >= (BAB_DELAY_BASE+BAB_DELAY_STEP*(BAB_DELAY_BANDS+1)))
				band = BAB_DELAY_BANDS+1;
			else
				band = (int)(((double)delay - BAB_DELAY_BASE) / BAB_DELAY_STEP) + 1;
			babinfo->delay_bands[band]++;
		}
		memcpy(&(babinfo->last_sent_work), &send, sizeof(start));

		delay = tdiff(&start, &send);
		babinfo->send_count++;
		babinfo->send_total += delay;
		if (babinfo->send_min == 0 || babinfo->send_min > delay)
			babinfo->send_min = delay;
		if (babinfo->send_max < delay)
			babinfo->send_max = delay;

		cgsem_mswait(&(babinfo->spi_work), BAB_STD_WAIT_mS);
	}

	return NULL;
}

static void bab_flush_work(struct cgpu_info *babcgpu)
{
	struct bab_info *babinfo = (struct bab_info *)(babcgpu->device_data);

	applog(LOG_DEBUG, "%s%i: flushing work",
			  babcgpu->drv->name, babcgpu->device_id);

	mutex_lock(&(babinfo->did_lock));
	babinfo->last_did.tv_sec = 0;
	mutex_unlock(&(babinfo->did_lock));

	cgsem_post(&(babinfo->scan_work));
}

#define DATA_MERKLE7 16
#define DATA_NTIME 17
#define DATA_BITS 18
#define DATA_NONCE 19

#define WORK_MERKLE7 (16*4)
#define WORK_NTIME (17*4)
#define WORK_BITS (18*4)
#define WORK_NONCE (19*4)

#if UPDATE_HISTORY
static void process_history(struct cgpu_info *babcgpu, int chip, struct timeval *when, bool good, struct timeval *now)
{
	struct bab_info *babinfo = (struct bab_info *)(babcgpu->device_data);
	uint64_t good_nonces, bad_nonces;
	uint8_t chip_fast;
	double tune;
	K_ITEM *item;
	int i;

	K_WLOCK(babinfo->nfree_list);
	item = k_unlink_head(babinfo->nfree_list);
	memcpy(&(DATAN(item)->found), when, sizeof(*when));
	if (good)
		k_add_head(babinfo->good_nonces[chip], item);
	else
		k_add_head(babinfo->bad_nonces[chip], item);

	// Remove all expired history
	for (i = 0; i < babinfo->chips; i++) {
		item = babinfo->good_nonces[i]->tail;
		while (item) {
			if (tdiff(now, &(DATAN(item)->found)) < HISTORY_TIME_S)
				break;

			k_unlink_item(babinfo->good_nonces[i], item);
			k_add_head(babinfo->nfree_list, item);

			item = babinfo->good_nonces[i]->tail;
		}

		item = babinfo->bad_nonces[i]->tail;
		while (item) {
			if (tdiff(now, &(DATAN(item)->found)) < HISTORY_TIME_S)
				break;

			k_unlink_item(babinfo->bad_nonces[i], item);
			k_add_head(babinfo->nfree_list, item);

			item = babinfo->bad_nonces[i]->tail;
		}
	}
	good_nonces = babinfo->good_nonces[chip]->count;
	bad_nonces = babinfo->bad_nonces[chip]->count;

	K_WUNLOCK(babinfo->nfree_list);

	// Tuning ...
	if (tdiff(now, &(babinfo->first_work[chip])) >= HISTORY_TIME_S &&
	    tdiff(now, &(babinfo->last_tune[chip])) >= HISTORY_TIME_S &&
	    (good_nonces + bad_nonces) > 0) {

		chip_fast = babinfo->chip_fast[chip];

		/*
		 * If bad then step it down and remember the speed
		 * TODO: does a speed change reset the chip? Or is there a reset?
		 */
		if (good_nonces == 0) {
			if (chip_fast > babinfo->min_speed) {
				if (babinfo->bad_fast[chip] > chip_fast)
					babinfo->bad_fast[chip] = chip_fast;

				babinfo->chip_fast[chip]--;

				applog(LOG_WARNING, "%s%d: Chip %d BAD - speed down from %d to %d",
						    babcgpu->drv->name, babcgpu->device_id,
						    chip, (int)chip_fast, (int)chip_fast - 1);
			} else {
				/*
				 * Permanently BAD since we're already at the minumum speed
				 * but only getting bad nonces
				 */
				if (babinfo->bad_msg[chip] == false) {
					applog(LOG_WARNING, "%s%d: Chip %d BAD - at min speed %d",
							    babcgpu->drv->name, babcgpu->device_id,
							    chip, (int)chip_fast);

					babinfo->bad_msg[chip] = true;
				}
			}
			goto tune_over;
		}

		/*
		 * It 'was' permanently BAD but a good nonce came back!
		 */
		if (babinfo->bad_msg[chip]) {
			applog(LOG_WARNING, "%s%d: Chip %d REVIVED - at speed %d",
					    babcgpu->drv->name, babcgpu->device_id,
					    chip, (int)chip_fast);

			babinfo->bad_msg[chip] = false;
		}

		/*
		 * Since we have found 'some' nonces -
		 * make sure bad_fast is higher than current chip_fast
		 */
		if (babinfo->bad_fast[chip] <= chip_fast)
			babinfo->bad_fast[chip] = chip_fast + 1;

		tune = (double)bad_nonces / (double)(good_nonces + bad_nonces) * 100.0;

		/*
		 * TODO: it appears some chips just get a % bad at low speeds
		 * so we should handle them by weighting the speed reduction vs
		 * the HW% gained from the reduction (i.e. GH/s)
		 * Maybe handle that when they hit min_speed, then do a gradual speed
		 * up verifying if it is really making GH/s worse or better
		 */

		// Tune it down if error rate is too high (and it's above min)
		if (tune >= babinfo->tune_down && chip_fast > babinfo->min_speed) {
			babinfo->chip_fast[chip]--;

			applog(LOG_WARNING, "%s%d: Chip %d High errors %.2f%% - speed down %d to %d",
					    babcgpu->drv->name, babcgpu->device_id,
					    chip, tune, (int)chip_fast, (int)chip_fast - 1);

			goto tune_over;
		}

		/*
		 * TODO: if we are at bad_fast-1 and tune_up
		 * and bad_fast was set more than some time limit ago
		 * then consider increasing bad_fast by 1?
		 */

		// Tune it up if error rate is low enough
		if (tune <= babinfo->tune_up &&
		    chip_fast < babinfo->max_speed &&
		    chip_fast < (babinfo->bad_fast[chip] - 1)) {
			babinfo->chip_fast[chip]++;

			applog(LOG_WARNING, "%s%d: Chip %d Low errors %.2f%% - speed up %d to %d",
					    babcgpu->drv->name, babcgpu->device_id,
					    chip, tune, (int)chip_fast, (int)chip_fast + 1);

			goto tune_over;
		}
tune_over:
		cgtime(&(babinfo->last_tune[chip]));
	}
}
#endif

/*
 * Find the matching work item by checking each nonce against
 * work items for the nonces chip
 */
static K_ITEM *process_nonce(struct thr_info *thr, struct cgpu_info *babcgpu, K_ITEM *ritem, uint32_t raw_nonce, K_ITEM *newest_witem)
{
	struct bab_info *babinfo = (struct bab_info *)(babcgpu->device_data);
	unsigned int links, proc_links, work_links, tests;
	int try_sta, try_fin, offset;
	K_ITEM *witem, *wtail;
	struct timeval now;
	bool not_first_reply;
	uint32_t nonce;
	int chip;

	chip = DATAR(ritem)->chip;
	not_first_reply = DATAR(ritem)->not_first_reply;

	babinfo->chip_nonces[chip]++;

	/*
	 * We can grab the head of the chip work queue and then release
	 * the lock and follow it to the end and back, since the other
	 * thread will only add items above the head - it wont touch
	 * any of the prev/next pointers from the head to the end -
	 * except the head->prev pointer may get changed
	 */
	K_RLOCK(babinfo->chip_work[chip]);
	witem = babinfo->chip_work[chip]->head;
	K_RUNLOCK(babinfo->chip_work[chip]);

	if (!witem) {
		applog(LOG_ERR, "%s%i: chip %d has no work, 1 nonce discarded!",
				babcgpu->drv->name, babcgpu->device_id, chip);
		babinfo->untested_nonces++;
		return newest_witem;
	}

	babinfo->tested_nonces++;

	if ((raw_nonce & 0xff) < 0x1c) {
		// Will only be this offset
		try_sta = BAB_OFF_0x1C_STA;
		try_fin = BAB_OFF_0x1C_FIN;
	} else {
		// Will only be one of the other offsets
		try_sta = BAB_OFF_OTHER_STA;
		try_fin = BAB_OFF_OTHER_FIN;
	}

	nonce = bab_decnonce(raw_nonce);

	cgtime(&now);

	tests = links = proc_links = work_links = 0;
	wtail = witem;
	while (wtail && wtail->next) {
		work_links++;
		wtail = wtail->next;
	}
	while (wtail) {
		if (!(DATAW(wtail)->work)) {
			applog(LOG_ERR, "%s%i: chip %d witem links %d has no work!",
					babcgpu->drv->name,
					babcgpu->device_id,
					chip, links);
		} else {
			if (ms_tdiff(&now, &(DATAW(wtail)->work_start)) >= BAB_WORK_EXPIRE_mS)
				proc_links--;
			else {
				for (offset = try_sta; offset <= try_fin; offset++) {
					tests++;
					if (test_nonce(DATAW(wtail)->work, nonce + bab_nonce_offsets[offset])) {
						submit_tested_work(thr, DATAW(wtail)->work);
						babinfo->nonce_offset_count[offset]++;
						babinfo->chip_good[chip]++;
						DATAW(wtail)->nonces++;

						mutex_lock(&(babinfo->nonce_lock));
						babinfo->new_nonces++;
						mutex_unlock(&(babinfo->nonce_lock));

						babinfo->ok_nonces++;
						babinfo->total_tests += tests;
						if (babinfo->max_tests_per_nonce < tests)
							babinfo->max_tests_per_nonce = tests;
						babinfo->total_links += links;
						babinfo->total_proc_links += proc_links;
						if (babinfo->max_links < links)
							babinfo->max_links = links;
						if (babinfo->max_proc_links < proc_links)
							babinfo->max_proc_links = proc_links;
						babinfo->total_work_links += work_links;

						babinfo->chip_cont_bad[chip] = 0;
#if UPDATE_HISTORY
						process_history(babcgpu, chip,
								&(DATAR(ritem)->when),
								true, &now);
#endif

						if (newest_witem == NULL ||
						    ms_tdiff(&(DATAW(wtail)->work_start),
							&(DATAW(newest_witem)->work_start)) < 0)
								return wtail;

						return newest_witem;
					}
				}
			}
		}
		if (wtail == witem)
			break;
		wtail = wtail->prev;
		links++;
		proc_links++;
	}

	if (not_first_reply) {
		babinfo->chip_bad[chip]++;
		inc_hw_errors(thr);

		babinfo->fail++;
		babinfo->fail_total_tests += tests;
		babinfo->fail_total_links += links;
		babinfo->fail_total_work_links += work_links;

		babinfo->chip_cont_bad[chip]++;
		if (babinfo->chip_max_bad[chip] < babinfo->chip_cont_bad[chip])
			babinfo->chip_max_bad[chip] = babinfo->chip_cont_bad[chip];

		// Handle chips with only bad results
		if (babinfo->disabled[chip] == false &&
		    babinfo->chip_good[chip] == 0 &&
		    babinfo->chip_bad[chip] >= BAB_BAD_COUNT &&
		    tdiff(&now, &(babinfo->first_work[chip])) >= BAB_BAD_TO_MIN) {
			if (babinfo->chip_fast[chip] > babinfo->min_speed)
				babinfo->chip_fast[chip] = babinfo->min_speed;
			else if (tdiff(&now, &(babinfo->first_work[chip])) > BAB_BAD_DEAD) {
				babinfo->disabled[chip] = true;
				babinfo->total_disabled++;
				applog(LOG_ERR, "%s%i: chip %d disabled!",
						babcgpu->drv->name,
						babcgpu->device_id,
						chip);
			}
		}
#if UPDATE_HISTORY
		process_history(babcgpu, chip, &(DATAR(ritem)->when), false, &now);
#endif
	} else {
		babinfo->initial_ignored++;
		babinfo->ign_total_tests += tests;
		babinfo->ign_total_links += links;
		babinfo->ign_total_work_links += work_links;
	}

	return newest_witem;
}

/*
 * On completion discard any work items older than BAB_WORK_EXPIRE_mS
 * and any work items of the chip older than the work of the newest nonce work item
 */
static void oknonces(struct thr_info *thr, struct cgpu_info *babcgpu, K_ITEM *ritem)
{
	uint32_t raw_nonce;
	K_ITEM *witem;
	int nonces;

	witem = NULL;

	for (nonces = 0; nonces < DATAR(ritem)->nonces; nonces++) {
		raw_nonce = DATAR(ritem)->nonce[nonces];

		witem = process_nonce(thr, babcgpu, ritem, raw_nonce, witem);
	}

	cleanup_older(babcgpu, DATAR(ritem)->chip, witem);
}

// Check at least every ...
#define BAB_RESULT_DELAY_mS 999

// Results checking thread
static void *bab_res(void *userdata)
{
	struct cgpu_info *babcgpu = (struct cgpu_info *)userdata;
	struct bab_info *babinfo = (struct bab_info *)(babcgpu->device_data);
	struct thr_info *thr = babcgpu->thr[0];
	K_ITEM *ritem;

	applog(LOG_DEBUG, "%s%i: Results...",
			  babcgpu->drv->name, babcgpu->device_id);

	// Wait until we're ready
	while (babcgpu->shutdown == false) {
		if (babinfo->initialised) {
			break;
		}
		cgsleep_ms(3);
	}

	ritem = NULL;
	while (babcgpu->shutdown == false) {
		K_WLOCK(babinfo->res_list);
		if (ritem) {
			// Release the old one
			k_add_head(babinfo->rfree_list, ritem);
			ritem = NULL;
		}
		// Check for a new one
		ritem = k_unlink_tail(babinfo->res_list);
		K_WUNLOCK(babinfo->res_list);

		if (!ritem) {
			cgsem_mswait(&(babinfo->process_reply), BAB_RESULT_DELAY_mS);
			continue;
		}

		oknonces(thr, babcgpu, ritem);
	}

	return NULL;
}

/*
 * 1.0s per nonce = 4.2GH/s
 * 0.9s per nonce = 4.8GH/s
 * On a slow machine, reducing this may resolve:
 *  BaB0: SPI waiting 1.2...s
 */
#define BAB_STD_WORK_DELAY_uS 900000

static bool bab_do_work(struct cgpu_info *babcgpu)
{
	struct bab_info *babinfo = (struct bab_info *)(babcgpu->device_data);
	int work_items = 0;
	K_ITEM *witem, *sitem, *ritem;
	struct timeval when, now;
	double delay;
	int chip, rep, j, nonces, spie = 0, miso = 0;
	uint32_t nonce, spichk;
	bool res;

	cgtime(&now);
	mutex_lock(&(babinfo->did_lock));
	delay = us_tdiff(&now, &(babinfo->last_did));
	mutex_unlock(&(babinfo->did_lock));
	if (delay < BAB_STD_WORK_DELAY_uS)
		return false;

	K_WLOCK(babinfo->sfree_list);
	sitem = k_unlink_head_zero(babinfo->sfree_list);
	K_WUNLOCK(babinfo->sfree_list);

	for (chip = 0; chip < babinfo->chips; chip++) {
		if (!(babinfo->disabled[chip])) {
			// TODO: ignore stale work
			K_WLOCK(babinfo->available_work);
			witem = k_unlink_tail(babinfo->available_work);
			K_WUNLOCK(babinfo->available_work);
			if (!witem) {
				applog(LOG_ERR, "%s%i: short work list (%d) %d expected %d - reset",
						babcgpu->drv->name, babcgpu->device_id,
						chip, work_items,
						babinfo->chips - babinfo->total_disabled);

				// Put them back in the order they were taken
				K_WLOCK(babinfo->available_work);
				for (j = chip-1; j >= 0; j--) {
					witem = DATAS(sitem)->witems[j];
					if (witem)
						k_add_tail(babinfo->available_work, witem);
				}
				K_WUNLOCK(babinfo->available_work);

				K_WLOCK(babinfo->sfree_list);
				k_add_head(babinfo->sfree_list, sitem);
				K_WUNLOCK(babinfo->sfree_list);

				return false;
			}

			/*
			 * TODO: do this when we get work except on LP?
			 * (not LP so we only do ms3steps for work required)
			 * Though that may more likely trigger the applog(short work list) above?
			 */
			if (DATAW(witem)->ci_setup == false) {
				memcpy((void *)&(DATAW(witem)->chip_input.midstate[0]),
					DATAW(witem)->work->midstate,
					sizeof(DATAW(witem)->work->midstate));
				memcpy((void *)&(DATAW(witem)->chip_input.merkle7),
					(void *)&(DATAW(witem)->work->data[WORK_MERKLE7]),
					MERKLE_BYTES);

				bab_ms3steps((void *)&(DATAW(witem)->chip_input));

				DATAW(witem)->ci_setup = true;
			}

			DATAS(sitem)->witems[chip] = witem;
			work_items++;
		}
	}

	// Send
	bab_put(babinfo, sitem);

	// Receive
	res = bab_get(babcgpu, babinfo, &when);
	if (!res) {
		applog(LOG_DEBUG, "%s%i: didn't get work reply ...",
				  babcgpu->drv->name, babcgpu->device_id);
		return false;
	}

	applog(LOG_DEBUG, "%s%i: Did get work reply ...",
			  babcgpu->drv->name, babcgpu->device_id);

	for (chip = 0; chip < babinfo->chips; chip++) {
		if (!(babinfo->disabled[chip])) {
			K_WLOCK(babinfo->rfree_list);
			ritem = k_unlink_head(babinfo->rfree_list);
			K_WUNLOCK(babinfo->rfree_list);

			DATAR(ritem)->chip = chip;
			DATAR(ritem)->not_first_reply = babinfo->not_first_reply[chip];
			memcpy(&(DATAR(ritem)->when), &when, sizeof(when));

			spichk = babinfo->chip_results[chip].spichk;
			if (spichk != 0 && spichk != 0xffffffff) {
				babinfo->chip_spie[chip]++;
				spie++;
				// Test the results anyway
			}

			nonces = 0;
			for (rep = 0; rep < BAB_REPLY_NONCES; rep++) {
				nonce = babinfo->chip_results[chip].nonce[rep];
				if (nonce != babinfo->chip_prev[chip].nonce[rep]) {
					if ((nonce & BAB_EVIL_MASK) == BAB_EVIL_NONCE)
						babinfo->discarded_e0s++;
					else
						DATAR(ritem)->nonce[nonces++] = nonce;
				}
			}

			if (nonces == BAB_REPLY_NONCES) {
				babinfo->chip_miso[chip]++;
				miso++;
				// Test the results anyway
			}

			/*
			 * Send even with zero nonces
			 * so cleanup_older() is called for the chip
			 */
			DATAR(ritem)->nonces = nonces;
			K_WLOCK(babinfo->res_list);
			k_add_head(babinfo->res_list, ritem);
			K_WUNLOCK(babinfo->res_list);

			cgsem_post(&(babinfo->process_reply));

			babinfo->not_first_reply[chip] = true;

			memcpy((void *)(&(babinfo->chip_prev[chip])),
				(void *)(&(babinfo->chip_results[chip])),
				sizeof(struct bab_work_reply));
		}

	}

	applog(LOG_DEBUG, "Work: items:%d spie:%d miso:%d", work_items, spie, miso);

	return true;
}

static bool bab_thread_prepare(struct thr_info *thr)
{
	struct cgpu_info *babcgpu = thr->cgpu;
	struct bab_info *babinfo = (struct bab_info *)(babcgpu->device_data);

	if (thr_info_create(&(babinfo->spi_thr), NULL, bab_spi, (void *)babcgpu)) {
		applog(LOG_ERR, "%s%i: SPI thread create failed",
				babcgpu->drv->name, babcgpu->device_id);
		return false;
	}
	pthread_detach(babinfo->spi_thr.pth);

	/*
	 * We require a seperate results checking thread since there is a lot
	 * of work done checking the results multiple times - thus we don't
	 * want that delay affecting sending/receiving work to/from the device
	 */
	if (thr_info_create(&(babinfo->res_thr), NULL, bab_res, (void *)babcgpu)) {
		applog(LOG_ERR, "%s%i: Results thread create failed",
				babcgpu->drv->name, babcgpu->device_id);
		return false;
	}
	pthread_detach(babinfo->res_thr.pth);

	return true;
}

static void bab_shutdown(struct thr_info *thr)
{
	struct cgpu_info *babcgpu = thr->cgpu;
	struct bab_info *babinfo = (struct bab_info *)(babcgpu->device_data);
	int i;

	applog(LOG_DEBUG, "%s%i: shutting down",
			  babcgpu->drv->name, babcgpu->device_id);

	for (i = 0; i < babinfo->chips; i++)
// TODO:	bab_shutdown(babcgpu, babinfo, i);
		;

	babcgpu->shutdown = true;
}

static bool bab_queue_full(struct cgpu_info *babcgpu)
{
	struct bab_info *babinfo = (struct bab_info *)(babcgpu->device_data);
	int roll, roll_limit = BAB_MAX_ROLLTIME;
	struct work *work, *usework;
	K_ITEM *item;
	int count, need;
	bool ret, rolled;

	K_RLOCK(babinfo->available_work);
	count = babinfo->available_work->count;
	K_RUNLOCK(babinfo->available_work);

	if (count >= (babinfo->chips - babinfo->total_disabled))
		ret = true;
	else {
		need = (babinfo->chips - babinfo->total_disabled) - count;
		work = get_queued(babcgpu);
		if (work) {
			if (roll_limit > work->drv_rolllimit)
				roll_limit = work->drv_rolllimit;
			roll = 0;
			do {
				if (roll == 0) {
					usework = work;
					babinfo->work_unrolled++;
					rolled = false;
				} else {
					usework = copy_work_noffset(work, roll);
					babinfo->work_rolled++;
					rolled = true;
				}

				K_WLOCK(babinfo->wfree_list);
				item = k_unlink_head_zero(babinfo->wfree_list);
				DATAW(item)->work = usework;
				DATAW(item)->rolled = rolled;
				k_add_head(babinfo->available_work, item);
				K_WUNLOCK(babinfo->wfree_list);
			} while (--need > 0 && ++roll <= roll_limit);
		} else {
			// Avoid a hard loop when we can't get work fast enough
			cgsleep_us(42);
		}

		if (need > 0)
			ret = false;
		else
			ret = true;
	}

	return ret;
}

#define BAB_STD_DELAY_mS 100

/*
 * TODO: allow this to run through more than once - the second+
 * time not sending any new work unless a flush occurs since:
 * at the moment we have BAB_STD_WORK_mS latency added to earliest replies
 */
static int64_t bab_scanwork(__maybe_unused struct thr_info *thr)
{
	struct cgpu_info *babcgpu = thr->cgpu;
	struct bab_info *babinfo = (struct bab_info *)(babcgpu->device_data);
	int64_t hashcount = 0;
	int count;

	bab_do_work(babcgpu);

	K_RLOCK(babinfo->available_work);
	count = babinfo->available_work->count;
	K_RUNLOCK(babinfo->available_work);

	if (count >= babinfo->chips)
		cgsem_mswait(&(babinfo->scan_work), BAB_STD_DELAY_mS);

	mutex_lock(&(babinfo->nonce_lock));
	if (babinfo->new_nonces) {
		hashcount += 0xffffffffull * babinfo->new_nonces;
		babinfo->new_nonces = 0;
	}
	mutex_unlock(&(babinfo->nonce_lock));

	return hashcount;
}

#define CHIPS_PER_STAT 16
#define FMT_RANGE "%d-%d"

static struct api_data *bab_api_stats(struct cgpu_info *babcgpu)
{
	struct bab_info *babinfo = (struct bab_info *)(babcgpu->device_data);
	uint64_t history_good[BAB_MAXCHIPS], history_bad[BAB_MAXCHIPS];
	uint64_t his_good_tot, his_bad_tot;
	double history_elapsed[BAB_MAXCHIPS], diff;
	bool elapsed_is_good[BAB_MAXCHIPS];
	int speeds[BAB_CHIP_SPEEDS];
	struct api_data *root = NULL;
	char data[2048];
	char buf[32];
	int spi_work, chip_work, sp, chip, bank, chip_off, board, last_board;
	int i, to, j, k;
	bool bad;
	struct timeval now;
	double elapsed, ghs;
	float ghs_sum, his_ghs_tot;
	float tot, hw;
	K_ITEM *item;

	if (babinfo->initialised == false)
		return NULL;

	memset(&speeds, 0, sizeof(speeds));

	root = api_add_int(root, "Version", &(babinfo->version), true);
	root = api_add_int(root, "Chips", &(babinfo->chips), true);
	root = api_add_int(root, "Boards", &(babinfo->boards), true);
	root = api_add_int(root, "Banks", &(babinfo->banks), true);

	data[0] = '\0';
	for (i = 0; i <= BAB_MAXBANKS; i++) {
		snprintf(buf, sizeof(buf), "%s%d",
					   (i == 0) ? "" : " ",
					   babinfo->chips_per_bank[i]);
		strcat(data, buf);
	}
	root = api_add_string(root, "Chips Per Bank", data, true);

	data[0] = '\0';
	for (i = 0; i <= BAB_MAXBANKS; i++) {
		snprintf(buf, sizeof(buf), "%s%d",
					   (i == 0) ? "" : " ",
					   babinfo->missing_chips_per_bank[i]);
		strcat(data, buf);
	}
	root = api_add_string(root, "Missing Chips Per Bank", data, true);

	cgtime(&now);
	elapsed = tdiff(&now, &(babcgpu->dev_start_tv));

	root = api_add_elapsed(root, "Device Elapsed", &elapsed, true);

	root = api_add_string(root, "History Enabled",
#if UPDATE_HISTORY
				"true",
#else
				"false",
#endif
				true);

	int chs = HISTORY_TIME_S;
	root = api_add_int(root, "Chip History Limit", &chs, true);

	K_RLOCK(babinfo->nfree_list);
	for (i = 0; i < babinfo->chips; i++) {
		item = babinfo->good_nonces[i]->tail;
		elapsed_is_good[i] = true;
		if (!item)
			history_elapsed[i] = 0;
		else
			history_elapsed[i] = tdiff(&now, &(DATAN(item)->found));

		item = babinfo->bad_nonces[i]->tail;
		if (item) {
			diff = tdiff(&now, &(DATAN(item)->found));
			if (history_elapsed[i] < diff) {
				history_elapsed[i] = diff;
				elapsed_is_good[i] = false;
			}
		}
		history_good[i] = babinfo->good_nonces[i]->count;
		history_bad[i] = babinfo->bad_nonces[i]->count;
	}
	K_RUNLOCK(babinfo->nfree_list);

	his_ghs_tot = 0;
	for (i = 0; i < babinfo->chips; i += CHIPS_PER_STAT) {
		to = i + CHIPS_PER_STAT - 1;
		if (to >= babinfo->chips)
			to = babinfo->chips - 1;

		data[0] = '\0';
		for (j = i; j <= to; j++) {
			snprintf(buf, sizeof(buf),
					"%s%"PRIu64,
					j == i ? "" : " ",
					babinfo->chip_nonces[j]);
			strcat(data, buf);
		}
		snprintf(buf, sizeof(buf), "Nonces "FMT_RANGE, i, to);
		root = api_add_string(root, buf, data, true);

		data[0] = '\0';
		for (j = i; j <= to; j++) {
			snprintf(buf, sizeof(buf),
					"%s%"PRIu64,
					j == i ? "" : " ",
					babinfo->chip_good[j]);
			strcat(data, buf);
		}
		snprintf(buf, sizeof(buf), "Good "FMT_RANGE, i, to);
		root = api_add_string(root, buf, data, true);

		data[0] = '\0';
		for (j = i; j <= to; j++) {
			snprintf(buf, sizeof(buf),
					"%s%"PRIu64,
					j == i ? "" : " ",
					babinfo->chip_bad[j]);
			strcat(data, buf);
		}
		snprintf(buf, sizeof(buf), "Bad "FMT_RANGE, i, to);
		root = api_add_string(root, buf, data, true);

		data[0] = '\0';
		for (j = i; j <= to; j++) {
			snprintf(buf, sizeof(buf),
					"%s0x%02x",
					j == i ? "" : " ",
					(int)(babinfo->chip_conf[j]));
			strcat(data, buf);
		}
		snprintf(buf, sizeof(buf), "Conf "FMT_RANGE, i, to);
		root = api_add_string(root, buf, data, true);

		data[0] = '\0';
		for (j = i; j <= to; j++) {
			snprintf(buf, sizeof(buf),
					"%s%d",
					j == i ? "" : " ",
					(int)(babinfo->chip_fast[j]));
			strcat(data, buf);
		}
		snprintf(buf, sizeof(buf), "Fast "FMT_RANGE, i, to);
		root = api_add_string(root, buf, data, true);

		data[0] = '\0';
		for (j = i; j <= to; j++) {
			snprintf(buf, sizeof(buf),
					"%s%d",
					j == i ? "" : " ",
					(int)(babinfo->chip_spie[j]));
			strcat(data, buf);
		}
		snprintf(buf, sizeof(buf), "Spie "FMT_RANGE, i, to);
		root = api_add_string(root, buf, data, true);

		data[0] = '\0';
		for (j = i; j <= to; j++) {
			snprintf(buf, sizeof(buf),
					"%s%d",
					j == i ? "" : " ",
					(int)(babinfo->chip_miso[j]));
			strcat(data, buf);
		}
		snprintf(buf, sizeof(buf), "Miso "FMT_RANGE, i, to);
		root = api_add_string(root, buf, data, true);

		data[0] = '\0';
		for (j = i; j <= to; j++) {
			tot = (float)(babinfo->chip_good[j] + babinfo->chip_bad[j]);
			if (tot != 0)
				hw = 100.0 * (float)(babinfo->chip_bad[j]) / tot;
			else
				hw = 0;
			snprintf(buf, sizeof(buf),
					"%s%.3f",
					j == i ? "" : " ", hw);
			strcat(data, buf);
		}
		snprintf(buf, sizeof(buf), "HW%% "FMT_RANGE, i, to);
		root = api_add_string(root, buf, data, true);

		ghs_sum = 0;
		data[0] = '\0';
		for (j = i; j <= to; j++) {
			if (elapsed > 0) {
				ghs = (double)(babinfo->chip_good[j]) * 0xffffffffull /
					elapsed / 1000000000.0;
			} else
				ghs = 0;

			snprintf(buf, sizeof(buf),
					"%s%.3f",
					j == i ? "" : " ", ghs);
			strcat(data, buf);
			ghs_sum += (float)ghs;
		}
		snprintf(buf, sizeof(buf), "GHs "FMT_RANGE, i, to);
		root = api_add_string(root, buf, data, true);

		snprintf(buf, sizeof(buf), "Sum GHs "FMT_RANGE, i, to);
		root = api_add_avg(root, buf, &ghs_sum, true);

		data[0] = '\0';
		for (j = i; j <= to; j++) {
			snprintf(buf, sizeof(buf),
					"%s%"PRIu64,
					j == i ? "" : " ",
					babinfo->chip_cont_bad[j]);
			strcat(data, buf);
		}
		snprintf(buf, sizeof(buf), "Cont-Bad "FMT_RANGE, i, to);
		root = api_add_string(root, buf, data, true);

		data[0] = '\0';
		for (j = i; j <= to; j++) {
			snprintf(buf, sizeof(buf),
					"%s%"PRIu64,
					j == i ? "" : " ",
					babinfo->chip_max_bad[j]);
			strcat(data, buf);
		}
		snprintf(buf, sizeof(buf), "Max-Bad "FMT_RANGE, i, to);
		root = api_add_string(root, buf, data, true);

		data[0] = '\0';
		for (j = i; j <= to; j++) {
			snprintf(buf, sizeof(buf),
					"%s%"PRIu64,
					j == i ? "" : " ",
					history_good[j]);
			strcat(data, buf);
		}
		snprintf(buf, sizeof(buf), "History Good "FMT_RANGE, i, to);
		root = api_add_string(root, buf, data, true);

		data[0] = '\0';
		for (j = i; j <= to; j++) {
			snprintf(buf, sizeof(buf),
					"%s%"PRIu64,
					j == i ? "" : " ",
					history_bad[j]);
			strcat(data, buf);
		}
		snprintf(buf, sizeof(buf), "History Bad "FMT_RANGE, i, to);
		root = api_add_string(root, buf, data, true);

		data[0] = '\0';
		for (j = i; j <= to; j++) {
			tot = (float)(history_good[j] + history_bad[j]);
			if (tot != 0)
				hw = 100.0 * (float)(history_bad[j]) / tot;
			else
				hw = 0;
			snprintf(buf, sizeof(buf),
					"%s%.3f",
					j == i ? "" : " ", hw);
			strcat(data, buf);
		}
		snprintf(buf, sizeof(buf), "History HW%% "FMT_RANGE, i, to);
		root = api_add_string(root, buf, data, true);

		ghs_sum = 0;
		data[0] = '\0';
		for (j = i; j <= to; j++) {
			if (history_elapsed[j] > 0) {
				double num = history_good[j];
				// exclude the first nonce?
				if (elapsed_is_good[j])
					num--;
				ghs = num * 0xffffffffull /
					history_elapsed[j] / 1000000000.0;
			} else
				ghs = 0;

			snprintf(buf, sizeof(buf),
					"%s%.3f",
					j == i ? "" : " ", ghs);
			strcat(data, buf);

			ghs_sum += (float)ghs;

			// Setup speed range data
			for (sp = 0; sp < BAB_CHIP_SPEEDS - 1; sp++) {
				if (ghs <= chip_speed_ranges[sp]) {
					speeds[sp]++;
					break;
				}
			}
			if (sp >= (BAB_CHIP_SPEEDS - 1))
				speeds[BAB_CHIP_SPEEDS - 1]++;
		}
		snprintf(buf, sizeof(buf), "History GHs "FMT_RANGE, i, to);
		root = api_add_string(root, buf, data, true);

		snprintf(buf, sizeof(buf), "Sum History GHs "FMT_RANGE, i, to);
		root = api_add_avg(root, buf, &ghs_sum, true);

		his_ghs_tot += ghs_sum;
	}

	root = api_add_avg(root, "Total History GHs", &his_ghs_tot, true);

	his_good_tot = his_bad_tot = 0;
	for (i = 0; i < babinfo->chips; i++) {
		his_good_tot += history_good[i];
		his_bad_tot += history_bad[i];
	}
	if (his_good_tot + his_bad_tot)
		tot = 100.0 * (float)his_bad_tot / (float)(his_good_tot + his_bad_tot);
	else
		tot = 0.0;
	root = api_add_avg(root, "Total History HW%", &tot, true);

	for (sp = 0; sp < BAB_CHIP_SPEEDS; sp++) {
		if (sp < (BAB_CHIP_SPEEDS - 1))
			ghs = chip_speed_ranges[sp];
		else
			ghs = chip_speed_ranges[BAB_CHIP_SPEEDS - 2];

		snprintf(buf, sizeof(buf), "History Speed %s%.1f %s",
					   (sp < (BAB_CHIP_SPEEDS - 1)) ? "" : ">",
					   ghs, chip_speed_names[sp]);

		root = api_add_int(root, buf, &(speeds[sp]), true);
	}

	int len, str, siz = 1024;
	char *tmp = malloc(siz);
	if (!tmp)
		quithere(1, "OOM tmp1");
	for (sp = 0; sp < 2; sp++) {
		tmp[0] = '\0';
		len = 0;
		for (i = 0; i < babinfo->chips; i++) {
			if (history_elapsed[i] > 0) {
				double num = history_good[i];
				// exclude the first nonce?
				if (elapsed_is_good[i])
					num--;
				ghs = num * 0xffffffffull /
					history_elapsed[i] / 1000000000.0;
			} else
				ghs = 0;

			if ((sp == 0 || ghs > chip_speed_ranges[sp-1]) &&
			    (ghs <= chip_speed_ranges[sp])) {
				bank = babinfo->chip_bank[i];
				chip_off = i;
				for (j = 0; j < babinfo->chip_bank[i]; j++)
					chip_off -= babinfo->chips_per_bank[j];
				/*
				 * Bank/Board/Chip are all 1 based
				 * except V1 Bank = BAB_V1_BANK (0)
				 * If the bank has any missing chips then a "?"
				 * is placed after the board number
				 */
				snprintf(buf, sizeof(buf), "%s%d/%d%s/%d",
							   len ? " " : "", bank,
							   (int)(chip_off / BAB_BOARDCHIPS)+1,
							   babinfo->missing_chips_per_bank[bank] ?
							   "?" : "",
							   (chip_off % BAB_BOARDCHIPS)+1);
				str = strlen(buf);
				while ((len + str + 1) > siz) {
					siz += 1024;
					tmp = realloc(tmp, siz);
					if (!tmp)
						quithere(1, "OOM tmp2");
				}
				strcpy(tmp + len, buf);
				len += str;
			}
		}
		snprintf(buf, sizeof(buf), "History %s", chip_speed_names[sp]);

		root = api_add_string(root, buf, len ? tmp : "None", true);
	}
	free(tmp);
	tmp = NULL;

	switch (babinfo->version) {
		case 1:
			i = j = BAB_V1_BANK;
			break;
		case 2:
			i = 1;
			j = BAB_MAXBANKS;
			break;
	}
	data[0] = '\0';
	for (bank = i; bank <= j; bank++) {
		if (babinfo->bank_first_chip[bank] >= 0) {
			chip = babinfo->bank_first_chip[bank];
			to = babinfo->bank_last_chip[bank];
			for (; chip <= to; chip += BAB_BOARDCHIPS) {
				bad = true;
				for (k = chip; (k <= to) && (k < (chip+BAB_BOARDCHIPS)); k++) {
					if (history_elapsed[k] > 0) {
						double num = history_good[k];
						// exclude the first nonce?
						if (elapsed_is_good[k])
							num--;
						ghs = num * 0xffffffffull /
							history_elapsed[k] / 1000000000.0;
					} else
						ghs = 0;

					if (ghs > 0.0) {
						bad = false;
						break;
					}
				}
				if (bad) {
					board = (int)((float)(chip - babinfo->bank_first_chip[bank]) /
							BAB_BOARDCHIPS) + 1;
					snprintf(buf, sizeof(buf),
							"%s%d/%d%s",
							data[0] ? " " : "",
							bank, board,
							babinfo->missing_chips_per_bank[bank] ?
							"?" : "");
					strcat(data, buf);
				}
			}
		}
	}
	root = api_add_string(root, "History Bad Boards", data[0] ? data : "None", true);

	data[0] = '\0';
	for (bank = i; bank <= j; bank++) {
		if (babinfo->bank_first_chip[bank] >= 0) {
			to = babinfo->bank_first_chip[bank];
			chip = babinfo->bank_last_chip[bank];
			for (; chip >= to; chip--) {
				bad = true;
				if (history_elapsed[chip] > 0) {
					double num = history_good[chip];
					// exclude the first nonce?
					if (elapsed_is_good[chip])
						num--;
					ghs = num * 0xffffffffull /
						history_elapsed[chip] / 1000000000.0;
				} else
					ghs = 0;

				if (ghs > 0.0)
					break;
			}
			/*
			 * The output here is: a/b+c/d
			 * a/b is the SPI/board that starts the Bad Chain
			 * c is the number of boards after a
			 * d is the total number of chips in the Bad Chain
			 * A Bad Chain is a continous set of bad chips that
			 * finish at the end of an SPI chain of boards
			 * This might be caused by the first board, or the cables attached
			 * to the first board, in the Bad Chain i.e. a/b
			 * If c is zero, it's just the last board, so it's the same as any
			 * other board having bad chips
			 */
			if (chip < babinfo->bank_last_chip[bank]) {
				board = (int)((float)(chip - babinfo->bank_first_chip[bank]) /
						BAB_BOARDCHIPS) + 1;
				last_board = (int)((float)(babinfo->bank_last_chip[bank] -
						babinfo->bank_first_chip[bank]) /
						BAB_BOARDCHIPS) + 1;
				snprintf(buf, sizeof(buf),
						"%s%d/%d%s+%d/%d",
						data[0] ? " " : "",
						bank, board,
						babinfo->missing_chips_per_bank[bank] ?
						"?" : "",
						last_board - board,
						babinfo->bank_last_chip[bank] - chip);
				strcat(data, buf);
			}
		}
	}
	root = api_add_string(root, "History Bad Chains", data[0] ? data : "None", true);

	root = api_add_int(root, "Disabled Chips", &(babinfo->total_disabled), true);

	for (i = 0; i < BAB_NONCE_OFFSETS; i++) {
		snprintf(buf, sizeof(buf), "Nonce Offset 0x%08x", bab_nonce_offsets[i]);
		root = api_add_uint64(root, buf, &(babinfo->nonce_offset_count[i]), true);
	}

	root = api_add_uint64(root, "Discarded E0s", &(babinfo->discarded_e0s), true);
	root = api_add_uint64(root, "Tested", &(babinfo->tested_nonces), true);
	root = api_add_uint64(root, "OK", &(babinfo->ok_nonces), true);
	root = api_add_uint64(root, "Total Tests", &(babinfo->total_tests), true);
	root = api_add_uint64(root, "Max Tests", &(babinfo->max_tests_per_nonce), true);
	float avg = babinfo->ok_nonces ? (float)(babinfo->total_tests) /
					     (float)(babinfo->ok_nonces) : 0;
	root = api_add_avg(root, "Avg Tests", &avg, true);
	root = api_add_uint64(root, "Untested", &(babinfo->untested_nonces), true);

	root = api_add_uint64(root, "Work Links", &(babinfo->total_links), true);
	root = api_add_uint64(root, "Work Processed Links", &(babinfo->total_proc_links), true);
	root = api_add_uint64(root, "Max Links", &(babinfo->max_links), true);
	root = api_add_uint64(root, "Max Processed Links", &(babinfo->max_proc_links), true);
	root = api_add_uint64(root, "Total Work Links", &(babinfo->total_work_links), true);
	avg = babinfo->ok_nonces ? (float)(babinfo->total_links) /
					(float)(babinfo->ok_nonces) : 0;
	root = api_add_avg(root, "Avg Links", &avg, true);
	avg = babinfo->ok_nonces ? (float)(babinfo->total_proc_links) /
					(float)(babinfo->ok_nonces) : 0;
	root = api_add_avg(root, "Avg Proc Links", &avg, true);
	avg = babinfo->ok_nonces ? (float)(babinfo->total_work_links) /
					(float)(babinfo->ok_nonces) : 0;
	root = api_add_avg(root, "Avg Work Links", &avg, true);

	root = api_add_uint64(root, "Fail", &(babinfo->fail), true);
	root = api_add_uint64(root, "Fail Total Tests", &(babinfo->fail_total_tests), true);
	avg = babinfo->fail ? (float)(babinfo->fail_total_tests) /
					(float)(babinfo->fail) : 0;
	root = api_add_avg(root, "Fail Avg Tests", &avg, true);
	root = api_add_uint64(root, "Fail Work Links", &(babinfo->fail_total_links), true);
	root = api_add_uint64(root, "Fail Total Work Links", &(babinfo->fail_total_work_links), true);

	root = api_add_uint32(root, "Initial Ignored", &(babinfo->initial_ignored), true);
	root = api_add_uint64(root, "Ign Total Tests", &(babinfo->ign_total_tests), true);
	root = api_add_uint64(root, "Ign Work Links", &(babinfo->ign_total_links), true);
	root = api_add_uint64(root, "Ign Total Work Links", &(babinfo->ign_total_work_links), true);

	chip_work = 0;
	for (i = 0; i < babinfo->chips; i++)
		chip_work += babinfo->chip_work[i]->count;
	spi_work = babinfo->spi_list->count * babinfo->chips;

	root = api_add_int(root, "WFree Total", &(babinfo->wfree_list->total), true);
	root = api_add_int(root, "WFree Count", &(babinfo->wfree_list->count), true);
	root = api_add_int(root, "Available Work", &(babinfo->available_work->count), true);
	root = api_add_int(root, "SPI Work", &spi_work, true);
	root = api_add_int(root, "Chip Work", &chip_work, true);

	root = api_add_int(root, "SFree Total", &(babinfo->sfree_list->total), true);
	root = api_add_int(root, "SFree Count", &(babinfo->sfree_list->count), true);
	root = api_add_int(root, "SPI Waiting", &(babinfo->spi_list->count), true);
	root = api_add_int(root, "SPI Sent", &(babinfo->spi_sent->count), true);

	root = api_add_int(root, "RFree Total", &(babinfo->rfree_list->total), true);
	root = api_add_int(root, "RFree Count", &(babinfo->rfree_list->count), true);
	root = api_add_int(root, "Result Count", &(babinfo->res_list->count), true);

	int used = babinfo->nfree_list->total - babinfo->nfree_list->count;
	root = api_add_int(root, "NFree Total", &(babinfo->nfree_list->total), true);
	root = api_add_int(root, "NFree Used", &used, true);

	root = api_add_uint64(root, "Delay Count", &(babinfo->delay_count), true);
	root = api_add_double(root, "Delay Min", &(babinfo->delay_min), true);
	root = api_add_double(root, "Delay Max", &(babinfo->delay_max), true);

	data[0] = '\0';
	for (i = 0; i <= BAB_DELAY_BANDS; i++) {
		snprintf(buf, sizeof(buf),
				"%s<%.1f=%"PRIu64,
				i == 0 ? "" : " ",
				BAB_DELAY_BASE+(BAB_DELAY_STEP*i),
				babinfo->delay_bands[i]);
		strcat(data, buf);
	}
	snprintf(buf, sizeof(buf),
			" >=%.1f=%"PRIu64,
			BAB_DELAY_BASE+BAB_DELAY_STEP*(BAB_DELAY_BANDS+1),
			babinfo->delay_bands[BAB_DELAY_BANDS+1]);
	strcat(data, buf);
	root = api_add_string(root, "Delay Bands", data, true);

	root = api_add_uint64(root, "Send Count", &(babinfo->send_count), true);
	root = api_add_double(root, "Send Total", &(babinfo->send_total), true);
	avg = babinfo->send_count ? (float)(babinfo->send_total) /
					(float)(babinfo->send_count) : 0;
	root = api_add_avg(root, "Send Avg", &avg, true);
	root = api_add_double(root, "Send Min", &(babinfo->send_min), true);
	root = api_add_double(root, "Send Max", &(babinfo->send_max), true);

	root = api_add_int(root, "Reply Wait", &(babinfo->reply_wait), true);
	root = api_add_uint64(root, "Reply Waits", &(babinfo->reply_waits), true);

	root = api_add_uint64(root, "Work Unrolled", &(babinfo->work_unrolled), true);
	root = api_add_uint64(root, "Work Rolled", &(babinfo->work_rolled), true);

	i = (int)(babinfo->max_speed);
	root = api_add_int(root, bab_options[0], &i, true);
	i = (int)(babinfo->def_speed);
	root = api_add_int(root, bab_options[1], &i, true);
	i = (int)(babinfo->min_speed);
	root = api_add_int(root, bab_options[2], &i, true);
	root = api_add_double(root, bab_options[3], &(babinfo->tune_up), true);
	root = api_add_double(root, bab_options[4], &(babinfo->tune_down), true);
	i = (int)(babinfo->speed_hz);
	root = api_add_int(root, bab_options[5], &i, true);
	i = (int)(babinfo->delay_usecs);
	root = api_add_int(root, bab_options[6], &i, true);
	root = api_add_uint64(root, bab_options[7], &(babinfo->trf_delay), true);

	return root;
}

static void bab_get_statline_before(char *buf, size_t bufsiz, struct cgpu_info *babcgpu)
{
	struct bab_info *babinfo = (struct bab_info *)(babcgpu->device_data);
#if UPDATE_HISTORY
	struct timeval now;
	double elapsed;
	int i, bad = 0;

	cgtime(&now);
	elapsed = tdiff(&now, &(babcgpu->dev_start_tv));

	// At least get 15s of nonces before saying anything is bad
	if (elapsed > 15.0) {
		K_RLOCK(babinfo->nfree_list);
		for (i = 0; i < babinfo->chips; i++) {
			if (babinfo->good_nonces[i]->count == 0 &&
			     babinfo->bad_nonces[i]->count > 1)
				bad++;
		}
		K_RUNLOCK(babinfo->nfree_list);
	}

	tailsprintf(buf, bufsiz, "%d.%02d.%03d B:%03d D:%03d",
				 babinfo->banks,
				 babinfo->boards,
				 babinfo->chips,
				 bad,
				 babinfo->total_disabled);
#else
	tailsprintf(buf, bufsiz, "%d.%02d.%03d D:%03d",
				 babinfo->banks,
				 babinfo->boards,
				 babinfo->chips,
				 babinfo->total_disabled);
#endif
}
#endif

struct device_drv bab_drv = {
	.drv_id = DRIVER_bab,
	.dname = "BlackArrowBitFuryGPIO",
	.name = "BaB",
	.drv_detect = bab_detect,
#ifdef LINUX
	.get_api_stats = bab_api_stats,
	.get_statline_before = bab_get_statline_before,
	.identify_device = bab_identify,
	.thread_prepare = bab_thread_prepare,
	.hash_work = hash_queued_work,
	.scanwork = bab_scanwork,
	.queue_full = bab_queue_full,
	.flush_work = bab_flush_work,
	.thread_shutdown = bab_shutdown
#endif
};
