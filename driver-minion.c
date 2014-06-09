/*
 * Copyright 2013-2014 Andrew Smith - BlackArrow Ltd
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 3 of the License, or (at your option)
 * any later version.  See COPYING for more details.
 */

#include "config.h"
#include "compat.h"
#include "miner.h"
#include "klist.h"
#include <ctype.h>
#include <math.h>

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

// Define this to 1 to enable interrupt code and enable no_nonce
#define ENABLE_INT_NONO 0

// Define this to 1 if compiling on RockChip and not on RPi
#define MINION_ROCKCHIP 0

// The code is always in - this just decides if it does it
static bool minreread = true;

#define MINION_SPI_BUS 0
#define MINION_SPI_CHIP 0

//#define MINION_SPI_SPEED 2000000
#define MINION_SPI_SPEED 1000000
#define MINION_SPI_BUFSIZ 1024

#define MINION_CHIPS 32
#define MINION_CORES 99
#define FAKE_CORE MINION_CORES

/*
 * TODO: These will need adjusting for final hardware
 * Look them up and calculate them?
 */
#define MINION_QUE_MAX 64
#define MINION_QUE_HIGH 48
#define MINION_QUE_SEND 16
#define MINION_QUE_LOW 8

#define MINION_FFL " - from %s %s() line %d"
#define MINION_FFL_HERE __FILE__, __func__, __LINE__
#define MINION_FFL_PASS file, func, line
#define MINION_FFL_ARGS __maybe_unused const char *file, \
			__maybe_unused const char *func, \
			__maybe_unused const int line

#define minion_txrx(_task) _minion_txrx(minioncgpu, minioninfo, _task, MINION_FFL_HERE)

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
#define MINION_SYS_IDLE_CNT 0x0e

// How many 32 bit reports make up all the cores - 99 cores = 4 reps
#define MINION_CORE_REPS (int)((((MINION_CORES-1) >> 5) & 0xff) + 1)

// All SYS data sizes are DATA_SIZ
#define MINION_SYS_SIZ DATA_SIZ

// Header Pin 18 = GPIO5 = BCM 24
#define MINION_GPIO_RESULT_INT_PIN 24
// RockChip is pin 172 ...

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

#if ENABLE_INT_NONO
// enable 'no nonce' report
#define SYS_MISC_CTL_DEFAULT 0x04
#else
#define SYS_MISC_CTL_DEFAULT 0x00
#endif

// Temperature returned by MINION_SYS_CHIP_STA 0x01 STA_TEMP()
#define MINION_TEMP_40 0
#define MINION_TEMP_60 1
#define MINION_TEMP_80 3
#define MINION_TEMP_100 7
#define MINION_TEMP_OVER 15

static const char *min_temp_40 = "<40";
static const char *min_temp_60 = "40-60";
static const char *min_temp_80 = "60-80";
static const char *min_temp_100 = "80-100";
static const char *min_temp_over = ">100";
static const char *min_temp_invalid = "?";

/*
 * Temperature for MINION_SYS_TEMP_CTL 0x03 temp_thres [0:3]
 * i.e. it starts at 120 and goes up in steps of 5 to 160
 */
#define MINION_TEMP_CTL_MIN 1
#define MINION_TEMP_CTL_MAX 9
#define MINION_TEMP_CTL_BITS 0x0f
#define MINION_TEMP_CTL_DEF 135
#define MINION_TEMP_CTL_STEP 5
#define MINION_TEMP_CTL_MIN_VALUE 120
#define MINION_TEMP_CTL_MAX_VALUE (MINION_TEMP_CTL_MIN_VALUE + \
				(MINION_TEMP_CTL_STEP * \
				(MINION_TEMP_CTL_MAX - MINION_TEMP_CTL_MIN)))
#define MINION_TEMP_DISABLE "disable"
#define MINION_TEMP_CTL_DISABLE -1
#define MINION_TEMP_CTL_DISABLE_VALUE 0x20

// CORE data size is DATA_SIZ
#define MINION_CORE_ENA0_31 0x10
#define MINION_CORE_ENA32_63 0x11
#define MINION_CORE_ENA64_95 0x12
#define MINION_CORE_ENA96_98 0x13
#define MINION_CORE_ACT0_31 0x14
#define MINION_CORE_ACT32_63 0x15
#define MINION_CORE_ACT64_95 0x16
#define MINION_CORE_ACT96_98 0x17

// All CORE data sizes are DATA_SIZ
#define MINION_CORE_SIZ DATA_SIZ

#define MINION_CORE_ALL "all"

// RES data size is minion_result
#define MINION_RES_DATA 0x20
#define MINION_RES_PEEK 0x21

// QUE data size is minion_que
#define MINION_QUE_0 0x30
#define MINION_QUE_R 0x31

// RANGE data sizes are DATA_SIZ
#define MINION_NONCE_START 0x70
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
#define MINION_NOCHIP_SIG2 0xffffffff
#define MINION_CHIP_SIG 0xb1ac8a44

/*
 * Number of times to try and get the SIG with each chip,
 * if the chip returns neither of the above values
 * TODO: maybe need some reset between tries, to handle a shift value?
 */
#define MINION_SIG_TRIES 3

/*
 * TODO: Finding these means the chip is there - but how to fix it?
 * The extra &'s are to ensure there is no sign bit issue since
 * the sign bit carry in a C bit-shift is compiler dependent
 */
#define MINION_CHIP_SIG_SHIFT1 (((MINION_CHIP_SIG & 0x0000ffff) << 16) & 0xffff0000)
#define MINION_CHIP_SIG_SHIFT2 (((MINION_CHIP_SIG & 0x00ffffff) <<  8) & 0xffffff00)
#define MINION_CHIP_SIG_SHIFT3 (((MINION_CHIP_SIG & 0xffffff00) >>  8) & 0x00ffffff)
#define MINION_CHIP_SIG_SHIFT4 (((MINION_CHIP_SIG & 0xffff0000) >> 16) & 0x0000ffff)

#define MINION_FREQ_MIN 100
#define MINION_FREQ_DEF 1000
#define MINION_FREQ_MAX 1400
#define MINION_FREQ_FACTOR 100
#define MINION_FREQ_FACTOR_MIN 1
#define MINION_FREQ_FACTOR_MAX 14

static uint32_t minion_freq[] = {
	0x0,
	0x205032,	//  1 =  100Mhz
	0x203042,	//  2 =  200Mhz
	0x20204B,	//  3 =  300Mhz
	0x201042,	//  4 =  400Mhz
	0x201053,	//  5 =  500Mhz
	0x200032,	//  6 =  600Mhz
	0x20003A,	//  7 =  700Mhz
	0x200042,	//  8 =  800Mhz
	0x20004B,	//  9 =  900Mhz
	0x200053,	// 10 = 1000Mhz
	0x21005B,	// 11 = 1100Mhz
	0x210064,	// 12 = 1200Mhz
	0x21006C,	// 13 = 1300Mhz
	0x210074	// 14 = 1400Mhz
};

#define MINION_RESET_PERCENT 50.0

#define STA_TEMP(_sta) ((uint16_t)((_sta)[3] & 0x1f))
#define STA_CORES(_sta) ((uint16_t)((_sta)[2]))
#define STA_FREQ(_sta) ((uint32_t)((_sta)[1]) * 0x100 + (uint32_t)((_sta)[0]))

// Randomly between 1s and 2s per chip
#define MINION_STATS_UPDATE_TIME_mS 1000
#define MINION_STATS_UPDATE_RAND_mS 1000

// Don't report it more than once every ... 5s
#define MINION_IDLE_MESSAGE_ms 5000

struct minion_status {
	uint16_t temp;
	uint16_t cores;
	uint32_t freq;
	uint32_t quework;
	uint32_t chipwork;
	uint32_t realwork; // chipwork, but FIFO_STA can update it
	struct timeval last;
	bool overheat;
	bool islow;
	bool tohigh;
	int lowcount;
	uint32_t freqsent;
	uint32_t overheats;
	struct timeval lastoverheat;
	struct timeval lastrecover;
	double overheattime;
	uint32_t tempsent;
	uint32_t idle;
	uint32_t last_rpt_idle;
	struct timeval idle_rpt;
};

#define ENABLE_CORE(_core, _n) ((_core[_n >> 3]) |= (1 << (_n % 8)))
#define CORE_IDLE(_core, _n) ((_core[_n >> 3]) & (1 << (_n % 8)))

#define FIFO_RES(_fifo, _off) ((_fifo)[(_off) + 0])
#define FIFO_CMD(_fifo, _off) ((_fifo)[(_off) + 1])

#define RES_GOLD(_res) ((((_res)->status[3]) & 0x80) == 0)
#define RES_CHIP(_res) (((_res)->status[3]) & 0x1f)
#define RES_CORE(_res) ((_res)->status[2])
#define RES_TASK(_res) ((int)((_res)->status[1]) * 0x100 + (int)((_res)->status[0]))
#define RES_NONCE(_res) u8tou32((_res)->nonce, 0)

/*
 * This is only valid since we avoid using task_id 0 for work
 * However, it isn't really necessary since we only request
 * the number of results the result buffer says it has
 * However, it is a simple failsafe
 */
#define IS_RESULT(_res) ((_res)->status[1] || (_res)->status[0])

struct minion_result {
	uint8_t status[DATA_SIZ];
	uint8_t nonce[DATA_SIZ];
};

#define MINION_RES_DATA_SIZ sizeof(struct minion_result)

/*
 * (MINION_SPI_BUFSIZ - HSIZE()) / MINION_RES_DATA_SIZ
 * less a little bit to round it out
 */
#define MINION_MAX_RES 120

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

/*
 * Max time to wait before checking the task list
 * Required, since only urgent tasks trigger an immediate check
 * TODO: ? for 2TH/s
 */
#define MINION_TASK_mS 8

/*
 * Max time to wait before checking the result list for nonces
 * This can be long since it's only a failsafe
 * cgsem_post is always sent if there are nonces ready to check
 */
#define MINION_NONCE_mS 888

// Number of results to make a GPIO interrupt
//#define MINION_RESULT_INT_SIZE 1
#define MINION_RESULT_INT_SIZE 2

/*
 * Max time to wait before checking for results
 * The interrupt doesn't occur until MINION_RESULT_INT_SIZE results are found
 * See comment in minion_spi_reply() at poll()
 */
#define MINION_REPLY_mS 88

/*
 * Max time to wait before returning the amount of work done
 * A result interrupt will send a trigger for this also
 * See comment in minion_scanwork()
 * This avoids the cgminer master work loop spinning doing nothing
 */
#define MINION_SCAN_mS 88

#define ALLOC_WITEMS 4096
#define LIMIT_WITEMS 0

typedef struct witem {
	struct work *work;
	uint32_t task_id;
	struct timeval sent;
	int nonces;
	bool urgent;
	bool stale; // if stale, don't decrement que/chip/realwork when discarded
	bool rolled;
	int errors; // uncertain since the error could mean task_id is wrong
} WITEM;

#define ALLOC_TITEMS 256
#define LIMIT_TITEMS 0

typedef struct titem {
	uint64_t tid;
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
	uint8_t work_state;
	struct work *work;
	K_ITEM *witem;
} TITEM;

#define ALLOC_RITEMS 256
#define LIMIT_RITEMS 0

typedef struct ritem {
	int chip;
	int core;
	uint32_t task_id;
	uint32_t nonce;
	struct timeval when;
	/*
	 * Only once per task_id if no nonces were found
	 * Sent with core = 0
	 * However, currently it always sends it at the end of every task
	 * TODO: code assumes it doesn't - change later when we
	 * see what the final hardware does (minor code performance gain)
	 */
	bool no_nonce;
	// If we requested the result twice:
	bool another;
	uint32_t task_id2;
	uint32_t nonce2;
} RITEM;

#define ALLOC_HITEMS 4096
#define LIMIT_HITEMS 0

// How much history to keep (5min)
#define MINION_HISTORY_s 300

// History must be always generated for the reset check
#define MINION_MAX_RESET_CHECK 2

typedef struct hitem {
	struct timeval when;
} HITEM;

#define DATAW(_item) ((WITEM *)(_item->data))
#define DATAT(_item) ((TITEM *)(_item->data))
#define DATAR(_item) ((RITEM *)(_item->data))
#define DATAH(_item) ((HITEM *)(_item->data))

// Set this to 1 to enable iostats processing
// N.B. it slows down mining
#define DO_IO_STATS 0

#if DO_IO_STATS
#define IO_STAT_NOW(_tv) cgtime(_tv)
#define IO_STAT_STORE(_sta, _fin, _lsta, _lfin, _tsd, _buf, _siz, _reply, _ioc) \
		do { \
			double _diff, _ldiff, _lwdiff, _1time; \
			int _off; \
			_diff = us_tdiff(_fin, _sta); \
			_ldiff = us_tdiff(_lfin, _lsta); \
			_lwdiff = us_tdiff(_sta, _lsta); \
			_1time = us_tdiff(_tsd, _lfin); \
			_off = (int)(_buf[1]) + (_reply >= 0 ? 0 : 0x100); \
			minioninfo->summary.count++; \
			minioninfo->summary.tsd += _1time; \
			minioninfo->iostats[_off].count++; \
			minioninfo->iostats[_off].tsd += _1time; \
			if (_diff <= 0) { \
				minioninfo->summary.zero_delay++; \
				minioninfo->iostats[_off].zero_delay++; \
			} else { \
				minioninfo->summary.total_delay += _diff; \
				if (minioninfo->summary.max_delay < _diff) \
					minioninfo->summary.max_delay = _diff; \
				if (minioninfo->summary.min_delay == 0 || \
				    minioninfo->summary.min_delay > _diff) \
					minioninfo->summary.min_delay = _diff; \
				minioninfo->iostats[_off].total_delay += _diff; \
				if (minioninfo->iostats[_off].max_delay < _diff) \
					minioninfo->iostats[_off].max_delay = _diff; \
				if (minioninfo->iostats[_off].min_delay == 0 || \
				    minioninfo->iostats[_off].min_delay > _diff) \
					minioninfo->iostats[_off].min_delay = _diff; \
			} \
			if (_ldiff <= 0) { \
				minioninfo->summary.zero_dlock++; \
				minioninfo->iostats[_off].zero_dlock++; \
			} else { \
				minioninfo->summary.total_dlock += _ldiff; \
				if (minioninfo->summary.max_dlock < _ldiff) \
					minioninfo->summary.max_dlock = _ldiff; \
				if (minioninfo->summary.min_dlock == 0 || \
				    minioninfo->summary.min_dlock > _ldiff) \
					minioninfo->summary.min_dlock = _ldiff; \
				minioninfo->iostats[_off].total_dlock += _ldiff; \
				if (minioninfo->iostats[_off].max_dlock < _ldiff) \
					minioninfo->iostats[_off].max_dlock = _ldiff; \
				if (minioninfo->iostats[_off].min_dlock == 0 || \
				    minioninfo->iostats[_off].min_dlock > _ldiff) \
					minioninfo->iostats[_off].min_dlock = _ldiff; \
			} \
			minioninfo->summary.total_dlwait += _lwdiff; \
			minioninfo->iostats[_off].total_dlwait += _lwdiff; \
			if (_siz == 0) { \
				minioninfo->summary.zero_bytes++; \
				minioninfo->iostats[_off].zero_bytes++; \
			} else { \
				minioninfo->summary.total_bytes += _siz; \
				if (minioninfo->summary.max_bytes < _siz) \
					minioninfo->summary.max_bytes = _siz; \
				if (minioninfo->summary.min_bytes == 0 || \
				    minioninfo->summary.min_bytes > _siz) \
					minioninfo->summary.min_bytes = _siz; \
				minioninfo->iostats[_off].total_bytes += _siz; \
				if (minioninfo->iostats[_off].max_bytes < _siz) \
					minioninfo->iostats[_off].max_bytes = _siz; \
				if (minioninfo->iostats[_off].min_bytes == 0 || \
				    minioninfo->iostats[_off].min_bytes > _siz) \
					minioninfo->iostats[_off].min_bytes = _siz; \
			} \
		} while (0);

typedef struct iostat {
	uint64_t count; // total ioctl()

	double total_delay; // total elapsed ioctl()
	double min_delay;
	double max_delay;
	uint64_t zero_delay; // how many had <= 0 delay

	// Above but including locking
	double total_dlock;
	double min_dlock;
	double max_dlock;
	uint64_t zero_dlock;

	// Total time waiting to get lock
	double total_dlwait;

	// these 3 fields are ignored for now since all are '1'
	uint64_t total_ioc; // SPI_IOC_MESSAGE(x)
	uint64_t min_ioc;
	uint64_t max_ioc;

	uint64_t total_bytes; // ioctl() bytes
	uint64_t min_bytes;
	uint64_t max_bytes;
	uint64_t zero_bytes; // how many had siz == 0

	double tsd; // total doing one extra cgtime() each time
} IOSTAT;
#else
#define IO_STAT_NOW(_tv)
#define IO_STAT_STORE(_sta, _fin, _lsta, _lfin, _tsd, _buf, _siz, _reply, _ioc)
#endif

struct minion_info {
	struct thr_info spiw_thr;
	struct thr_info spir_thr;
	struct thr_info res_thr;

	pthread_mutex_t spi_lock;
	pthread_mutex_t sta_lock;

	cgsem_t task_ready;
	cgsem_t nonce_ready;
	cgsem_t scan_work;

	int spifd;
	char gpiointvalue[64];
	int gpiointfd;

	// TODO: need to track disabled chips - done?
	int chips;
	bool chip[MINION_CHIPS];
	int init_freq[MINION_CHIPS];
	int init_temp[MINION_CHIPS];
	uint8_t init_cores[MINION_CHIPS][DATA_SIZ*MINION_CORE_REPS];

	uint32_t next_task_id;

	// Stats
	uint64_t chip_nonces[MINION_CHIPS];
	uint64_t chip_nononces[MINION_CHIPS];
	uint64_t chip_good[MINION_CHIPS];
	uint64_t chip_bad[MINION_CHIPS];
	uint64_t chip_err[MINION_CHIPS];
	uint64_t core_good[MINION_CHIPS][MINION_CORES+1];
	uint64_t core_bad[MINION_CHIPS][MINION_CORES+1];

	uint32_t chip_core_ena[MINION_CORE_REPS][MINION_CHIPS];
	uint32_t chip_core_act[MINION_CORE_REPS][MINION_CHIPS];

	struct minion_status chip_status[MINION_CHIPS];

	uint64_t interrupts;
	uint64_t result_interrupts;
	uint64_t command_interrupts;
	char last_interrupt[64];

	pthread_mutex_t nonce_lock;
	uint64_t new_nonces;

	uint64_t ok_nonces;
	uint64_t untested_nonces;
	uint64_t tested_nonces;

	uint64_t work_unrolled;
	uint64_t work_rolled;

	uint64_t spi_errors;
	uint64_t fifo_spi_errors[MINION_CHIPS];
	uint64_t res_spi_errors[MINION_CHIPS];
	uint64_t use_res2[MINION_CHIPS];

	uint64_t tasks_failed[MINION_CHIPS];
	uint64_t tasks_recovered[MINION_CHIPS];
	uint64_t nonces_failed[MINION_CHIPS];
	uint64_t nonces_recovered[MINION_CHIPS];
	struct timeval last_reset[MINION_CHIPS];
	double do_reset[MINION_CHIPS];

	// Work items
	K_LIST *wfree_list;
	K_STORE *wwork_list;
	K_STORE *wque_list[MINION_CHIPS];
	K_STORE *wchip_list[MINION_CHIPS];

	// Task list
	K_LIST *tfree_list;
	K_STORE *task_list;
	K_STORE *treply_list;

	uint64_t next_tid;

	// Nonce replies
	K_LIST *rfree_list;
	K_STORE *rnonce_list;

	struct timeval last_did;

	// Nonce history
	K_LIST *hfree_list;
	K_STORE *hchip_list[MINION_CHIPS];

	int history_gen;
	struct timeval chip_chk;
	struct timeval chip_rpt;
	double history_ghs[MINION_CHIPS];
	// Gets reset to zero each time it is used in reporting
	int res_err_count[MINION_CHIPS];

#if DO_IO_STATS
	// Total
	IOSTAT summary;

	// Two for each command plus wasted extras i.e. direct/fast lookup
	// No error uses 0x0 to 0xff, error uses 0x100 to 0x1ff
	IOSTAT iostats[0x200];
#endif

	bool initialised;
};

static void ready_work(struct cgpu_info *minioncgpu, struct work *work, bool rolled)
{
	struct minion_info *minioninfo = (struct minion_info *)(minioncgpu->device_data);
	K_ITEM *item = NULL;

	K_WLOCK(minioninfo->wfree_list);

	item = k_unlink_head(minioninfo->wfree_list);

	DATAW(item)->work = work;
	DATAW(item)->task_id = 0;
	memset(&(DATAW(item)->sent), 0, sizeof(DATAW(item)->sent));
	DATAW(item)->nonces = 0;
	DATAW(item)->urgent = false;
	DATAW(item)->rolled = rolled;
	DATAW(item)->errors = 0;

	k_add_head(minioninfo->wwork_list, item);

	K_WUNLOCK(minioninfo->wfree_list);
}

static bool oldest_nonce(struct cgpu_info *minioncgpu, int *chip, int *core, uint32_t *task_id,
			 uint32_t *nonce, bool *no_nonce, struct timeval *when,
			 bool *another, uint32_t *task_id2, uint32_t *nonce2)
{
	struct minion_info *minioninfo = (struct minion_info *)(minioncgpu->device_data);
	K_ITEM *item = NULL;
	bool found = false;

	K_WLOCK(minioninfo->rnonce_list);

	item = k_unlink_tail(minioninfo->rnonce_list);
	if (item) {
		found = true;
		*chip = DATAR(item)->chip;
		*core = DATAR(item)->core;
		*task_id = DATAR(item)->task_id;
		*nonce = DATAR(item)->nonce;
		*no_nonce = DATAR(item)->no_nonce;
		memcpy(when, &(DATAR(item)->when), sizeof(*when));
		*another = DATAR(item)->another;
		*task_id2 = DATAR(item)->task_id2;
		*nonce2 = DATAR(item)->nonce2;

		k_free_head(minioninfo->rfree_list, item);
	}

	K_WUNLOCK(minioninfo->rnonce_list);

	return found;
}

static const char *addr2txt(uint8_t addr)
{
	switch (addr) {
		case READ_ADDR(MINION_SYS_CHIP_SIG):
			return "RChipSig";
		case READ_ADDR(MINION_SYS_CHIP_STA):
			return "RChipSta";
		case WRITE_ADDR(MINION_SYS_MISC_CTL):
			return "WMiscCtrl";
		case WRITE_ADDR(MINION_SYS_RSTN_CTL):
			return "WResetCtrl";
		case READ_ADDR(MINION_SYS_FIFO_STA):
			return "RFifoSta";
		case READ_ADDR(MINION_CORE_ENA0_31):
			return "RCoreEna0-31";
		case WRITE_ADDR(MINION_CORE_ENA0_31):
			return "WCoreEna0-31";
		case READ_ADDR(MINION_CORE_ENA32_63):
			return "RCoreEna32-63";
		case WRITE_ADDR(MINION_CORE_ENA32_63):
			return "WCoreEna32-63";
		case READ_ADDR(MINION_CORE_ENA64_95):
			return "RCoreEna64-95";
		case WRITE_ADDR(MINION_CORE_ENA64_95):
			return "WCoreEna64-95";
		case READ_ADDR(MINION_CORE_ENA96_98):
			return "RCoreEna96-98";
		case WRITE_ADDR(MINION_CORE_ENA96_98):
			return "WCoreEna96-98";
		case READ_ADDR(MINION_CORE_ACT0_31):
			return "RCoreAct0-31";
		case READ_ADDR(MINION_CORE_ACT32_63):
			return "RCoreAct32-63";
		case READ_ADDR(MINION_CORE_ACT64_95):
			return "RCoreAct64-95";
		case READ_ADDR(MINION_CORE_ACT96_98):
			return "RCoreAct96-98";
		case READ_ADDR(MINION_RES_DATA):
			return "RResData";
		case READ_ADDR(MINION_RES_PEEK):
			return "RResPeek";
		case WRITE_ADDR(MINION_QUE_0):
			return "WQueWork";
		case READ_ADDR(MINION_NONCE_START):
			return "RNonceStart";
		case WRITE_ADDR(MINION_NONCE_START):
			return "WNonceStart";
		case READ_ADDR(MINION_NONCE_RANGE):
			return "RNonceRange";
		case WRITE_ADDR(MINION_NONCE_RANGE):
			return "WNonceRange";
		case READ_ADDR(MINION_SYS_INT_STA):
			return "RIntSta";
		case WRITE_ADDR(MINION_SYS_INT_ENA):
			return "WIntEna";
		case WRITE_ADDR(MINION_SYS_INT_CLR):
			return "WIntClear";
		case WRITE_ADDR(MINION_SYS_BUF_TRIG):
			return "WResTrigger";
		case WRITE_ADDR(MINION_SYS_QUE_TRIG):
			return "WCmdTrigger";
		case READ_ADDR(MINION_SYS_TEMP_CTL):
			return "RTempCtrl";
		case WRITE_ADDR(MINION_SYS_TEMP_CTL):
			return "WTempCtrl";
		case READ_ADDR(MINION_SYS_FREQ_CTL):
			return "RFreqCtrl";
		case WRITE_ADDR(MINION_SYS_FREQ_CTL):
			return "WFreqCtrl";
		case READ_ADDR(MINION_SYS_IDLE_CNT):
			return "RIdleCnt";
	}

	// gcc warning if this is in default:
	if (IS_ADDR_READ(addr))
		return "RUnhandled";
	else
		return "WUnhandled";
}

// For display_ioctl()
#define IOCTRL_LOG LOG_WARNING

// For all other debug so it can easily be switched always on
#define MINION_LOG LOG_DEBUG

// For task corruption logging
#define MINTASK_LOG LOG_DEBUG

// Set to 1 for debug
#define MINION_SHOW_IO 0

#define DATA_ALL 2048
#define DATA_OFF 512

#if MINION_SHOW_IO
static void display_ioctl(int reply, uint32_t osiz, uint8_t *obuf, uint32_t rsiz, uint8_t *rbuf)
{
	struct minion_result *res;
	const char *name, *dir, *ex;
	char buf[4096];
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
#endif

#define MINION_UNEXPECTED_TASK -999
#define MINION_OVERSIZE_TASK -998

static int __do_ioctl(struct minion_info *minioninfo, uint8_t *obuf, uint32_t osiz, uint8_t *rbuf, uint32_t rsiz, MINION_FFL_ARGS)
{
	struct spi_ioc_transfer tran;
	int ret;
#if MINION_SHOW_IO
	char dataw[DATA_ALL], datar[DATA_ALL];
#endif

#if DO_IO_STATS
	struct timeval sta, fin, lsta, lfin, tsd;
#endif

	if ((int)osiz > MINION_BUFSIZ)
		quitfrom(1, file, func, line, "%s() invalid osiz %u > %d (chip=%d reg=0x%02x)",
				__func__, osiz, MINION_BUFSIZ, (int)(obuf[0]), obuf[1]);

	if (rsiz >= osiz)
		quitfrom(1, file, func, line, "%s() invalid rsiz %u >= osiz %u (chip=%u reg=0x%02x)",
				__func__, rsiz, osiz, (int)(obuf[0]), obuf[1]);

	memset(&obuf[0] + osiz - rsiz, 0xff, rsiz);

#if MINION_SHOW_IO
	// if the a5/5a outside the data change, it means data overrun or corruption
	memset(dataw, 0xa5, sizeof(dataw));
	memset(datar, 0x5a, sizeof(datar));
	memcpy(&dataw[DATA_OFF], &obuf[0], osiz);

	char *buf = bin2hex((unsigned char *)&(dataw[DATA_OFF]), osiz);
	applog(IOCTRL_LOG, "*** %s() sending %02x %02x %s %02x %02x",
			   __func__,
			   dataw[0], dataw[DATA_OFF-1], buf,
			   dataw[DATA_OFF+osiz], dataw[DATA_ALL-1]);
	free(buf);
#endif

	memset((char *)rbuf, 0x00, osiz);

//	cgsleep_ms(5); // TODO: a delay ... based on the last command? But subtract elapsed
			// i.e. do any commands need a delay after the I/O has completed before the next I/O?

	memset(&tran, 0, sizeof(tran));
	if (osiz < MINION_SPI_BUFSIZ)
		tran.len = osiz;
	else
		return MINION_OVERSIZE_TASK;

	tran.delay_usecs = 0;
	tran.speed_hz = MINION_SPI_SPEED;

#if MINION_SHOW_IO
	tran.tx_buf = (uintptr_t)&(dataw[DATA_OFF]);
	tran.rx_buf = (uintptr_t)&(datar[DATA_OFF]);
#else
	tran.tx_buf = (uintptr_t)obuf;
	tran.rx_buf = (uintptr_t)rbuf;
#endif

	IO_STAT_NOW(&lsta);
	mutex_lock(&(minioninfo->spi_lock));
	IO_STAT_NOW(&sta);
	ret = ioctl(minioninfo->spifd, SPI_IOC_MESSAGE(1), (void *)&tran);
	IO_STAT_NOW(&fin);
	mutex_unlock(&(minioninfo->spi_lock));
	IO_STAT_NOW(&lfin);
	IO_STAT_NOW(&tsd);

	IO_STAT_STORE(&sta, &fin, &lsta, &lfin, &tsd, obuf, osiz, ret, 1);

#if MINION_SHOW_IO
	if (ret > 0) {
		buf = bin2hex((unsigned char *)&(datar[DATA_OFF]), ret);
		applog(IOCTRL_LOG, "*** %s() reply %d = %02x %02x %s %02x %02x",
				   __func__, ret,
				   datar[0], datar[DATA_OFF-1], buf,
				   datar[DATA_OFF+osiz], datar[DATA_ALL-1]);
		free(buf);
	} else
		applog(LOG_ERR, "*** %s() reply = %d", __func__, ret);

	memcpy(&rbuf[0], &datar[DATA_OFF], osiz);

	display_ioctl(ret, osiz, (uint8_t *)(&dataw[DATA_OFF]), rsiz, (uint8_t *)(&datar[DATA_OFF]));
#endif

	return ret;
}

#if 1
#define do_ioctl(_obuf, _osiz, _rbuf, _rsiz) __do_ioctl(minioninfo, _obuf, _osiz, _rbuf, _rsiz, MINION_FFL_HERE)
#else
#define do_ioctl(_obuf, _osiz, _rbuf, _rsiz) _do_ioctl(minioninfo, _obuf, _osiz, _rbuf, _rsiz, MINION_FFL_HERE)
// This sends an expected to work, SPI command before each SPI command
static int _do_ioctl(struct minion_info *minioninfo, uint8_t *obuf, uint32_t osiz, uint8_t *rbuf, uint32_t rsiz, MINION_FFL_ARGS)
{
	struct minion_header *head;
	uint8_t buf1[MINION_BUFSIZ];
	uint8_t buf2[MINION_BUFSIZ];
	uint32_t siz;

	head = (struct minion_header *)buf1;
	head->chip = 1; // Needs to be set to a valid chip
	head->reg = READ_ADDR(MINION_SYS_FIFO_STA);
	SET_HEAD_SIZ(head, DATA_SIZ);
	siz = HSIZE() + DATA_SIZ;
	__do_ioctl(minioninfo, buf1, siz, buf2, MINION_CORE_SIZ, MINION_FFL_PASS);

	return __do_ioctl(minioninfo, obuf, osiz, rbuf, rsiz, MINION_FFL_PASS);
}
#endif

static bool _minion_txrx(struct cgpu_info *minioncgpu, struct minion_info *minioninfo, TITEM *task, MINION_FFL_ARGS)
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
	if (task->reply < 0) {
		applog(LOG_ERR, "%s%d: chip=%d ioctl failed reply=%d err=%d" MINION_FFL,
				minioncgpu->drv->name, minioncgpu->device_id,
				task->chip, task->reply, errno, MINION_FFL_PASS);
	} else if (task->reply < (int)(task->osiz)) {
		applog(LOG_ERR, "%s%d: chip=%d ioctl failed to write %d only wrote %d (err=%d)" MINION_FFL,
				minioncgpu->drv->name, minioncgpu->device_id,
				task->chip, (int)(task->osiz), task->reply, errno, MINION_FFL_PASS);
	}

	return (task->reply >= (int)(task->osiz));
}

// Only for DATA_SIZ commands
static int build_cmd(struct cgpu_info *minioncgpu, struct minion_info *minioninfo, int chip, uint8_t reg, uint8_t *rbuf, uint32_t rsiz, uint8_t *data)
{
	struct minion_header *head;
	uint8_t wbuf[MINION_BUFSIZ];
	uint32_t wsiz;
	int reply;

	head = (struct minion_header *)wbuf;
	head->chip = chip;
	head->reg = reg;
	SET_HEAD_SIZ(head, DATA_SIZ);

	head->data[0] = data[0];
	head->data[1] = data[1];
	head->data[2] = data[2];
	head->data[3] = data[3];

	wsiz = HSIZE() + DATA_SIZ;
	reply = do_ioctl(wbuf, wsiz, rbuf, rsiz);

	if (reply != (int)wsiz) {
		applog(LOG_ERR, "%s: chip %d %s returned %d (should be %d)",
				minioncgpu->drv->dname, chip,
				addr2txt(head->reg),
				reply, (int)wsiz);
	}

	return reply;
}

static void init_chip(struct cgpu_info *minioncgpu, struct minion_info *minioninfo, int chip)
{
	uint8_t rbuf[MINION_BUFSIZ];
	uint8_t data[4];
	__maybe_unused int reply;
	int choice;
	uint32_t freq;

	// Complete chip reset
	data[0] = 0x00;
	data[1] = 0x00;
	data[2] = 0xa5;
	data[3] = 0xf5;

	reply = build_cmd(minioncgpu, minioninfo,
			  chip, WRITE_ADDR(MINION_SYS_RSTN_CTL),
			  rbuf, 0, data);

	// Default reset
	data[0] = SYS_RSTN_CTL_INIT;
	data[1] = 0x00;
	data[2] = 0x00;
	data[3] = 0x00;

	reply = build_cmd(minioncgpu, minioninfo,
			  chip, WRITE_ADDR(MINION_SYS_RSTN_CTL),
			  rbuf, 0, data);

	// Default initialisation
	data[0] = SYS_MISC_CTL_DEFAULT;
	data[1] = 0x00;
	data[2] = 0x00;
	data[3] = 0x00;

	reply = build_cmd(minioncgpu, minioninfo,
			  chip, WRITE_ADDR(MINION_SYS_MISC_CTL),
			  rbuf, 0, data);

	// Set chip frequency
	choice = minioninfo->init_freq[chip];
	if (choice < MINION_FREQ_MIN || choice > MINION_FREQ_MAX)
		choice = MINION_FREQ_DEF;
	choice /= MINION_FREQ_FACTOR;
	if (choice < MINION_FREQ_FACTOR_MIN)
		choice = MINION_FREQ_FACTOR_MIN;
	if (choice > MINION_FREQ_FACTOR_MAX)
		choice = MINION_FREQ_FACTOR_MAX;
	freq = minion_freq[choice];
	data[0] = (uint8_t)(freq & 0xff);
	data[1] = (uint8_t)(((freq & 0xff00) >> 8) & 0xff);
	data[2] = (uint8_t)(((freq & 0xff0000) >> 16) & 0xff);
	data[3] = (uint8_t)(((freq & 0xff000000) >> 24) & 0xff);

	minioninfo->chip_status[chip].freqsent = freq;

	reply = build_cmd(minioncgpu, minioninfo,
			  chip, WRITE_ADDR(MINION_SYS_FREQ_CTL),
			  rbuf, 0, data);

	// Set temp threshold
	choice = minioninfo->init_temp[chip];
	if (choice == MINION_TEMP_CTL_DISABLE)
		choice = MINION_TEMP_CTL_DISABLE_VALUE;
	else {
		if (choice < MINION_TEMP_CTL_MIN_VALUE || choice > MINION_TEMP_CTL_MAX_VALUE)
			choice = MINION_TEMP_CTL_DEF;
		choice -= MINION_TEMP_CTL_MIN_VALUE;
		choice /= MINION_TEMP_CTL_STEP;
		choice += MINION_TEMP_CTL_MIN;
		if (choice < MINION_TEMP_CTL_MIN)
			choice = MINION_TEMP_CTL_MIN;
		if (choice > MINION_TEMP_CTL_MAX)
			choice = MINION_TEMP_CTL_MAX;
	}
	data[0] = (uint8_t)choice;
	data[1] = 0;
	data[2] = 0;
	data[3] = 0;

	minioninfo->chip_status[chip].tempsent = choice;

	reply = build_cmd(minioncgpu, minioninfo,
			  chip, WRITE_ADDR(MINION_SYS_TEMP_CTL),
			  rbuf, 0, data);
}

static void enable_chip_cores(struct cgpu_info *minioncgpu, struct minion_info *minioninfo, int chip)
{
	uint8_t rbuf[MINION_BUFSIZ];
	uint8_t data[4];
	__maybe_unused int reply;
	int rep, i;

	for (i = 0; i < 4; i++)
		data[i] = minioninfo->init_cores[chip][i];

	reply = build_cmd(minioncgpu, minioninfo,
			  chip, WRITE_ADDR(MINION_CORE_ENA0_31),
			  rbuf, 0, data);

	for (i = 0; i < 4; i++)
		data[i] = minioninfo->init_cores[chip][i+4];

	reply = build_cmd(minioncgpu, minioninfo,
			  chip, WRITE_ADDR(MINION_CORE_ENA32_63),
			  rbuf, 0, data);

	for (i = 0; i < 4; i++)
		data[i] = minioninfo->init_cores[chip][i+8];

	reply = build_cmd(minioncgpu, minioninfo,
			  chip, WRITE_ADDR(MINION_CORE_ENA64_95),
			  rbuf, 0, data);

	for (i = 0; i < 4; i++)
		data[i] = minioninfo->init_cores[chip][i+12];

	reply = build_cmd(minioncgpu, minioninfo,
			  chip, WRITE_ADDR(MINION_CORE_ENA96_98),
			  rbuf, 0, data);

/* Below is for testing - disabled/use default
	// 1/3 range for each of the 3 cores
//	data[0] = 0x55;
//	data[1] = 0x55;
//	data[2] = 0x55;
//	data[3] = 0x55;

	// quicker replies
//	data[0] = 0x05;
//	data[1] = 0x05;
//	data[2] = 0x05;
//	data[3] = 0x05;

	// 0x00000100 at 20MH/s per core = 336TH/s if 1 nonce per work item
	// 0x00001000 = 21.0TH/s - so well above 2TH/s
	// 0x00002000 = 10.5TH/s - above 2TH/s
	// speed test
	data[0] = 0x00;
	data[1] = 0x01;
	data[2] = 0x00;
	data[3] = 0x00;
//	data[3] = 0x20; // slow it down for other testing

	// 2 cores
//	data[0] = 0xff;
//	data[1] = 0xff;
//	data[2] = 0xff;
//	data[3] = 0x7f;

	reply = build_cmd(minioncgpu, minioninfo,
			  chip, WRITE_ADDR(MINION_NONCE_RANGE),
			  rbuf, 0, data);

	// find lots more nonces in a short time on my test data
	// i.e. emulate a MUCH higher hash rate on SPI and work
	// generation/testing
	// Current test data (same repeated 10 times) has nonce 0x05e0ed6d
	data[0] = 0x00;
	data[1] = 0xed;
	data[2] = 0xe0;
	data[3] = 0x05;

	reply = build_cmd(minioncgpu, minioninfo,
			  chip, WRITE_ADDR(MINION_NONCE_START),
			  rbuf, 0, data);
*/

	// store the core ena state
	for (rep = 0; rep < MINION_CORE_REPS; rep++) {
		data[0] = 0x0;
		data[1] = 0x0;
		data[2] = 0x0;
		data[3] = 0x0;

		reply = build_cmd(minioncgpu, minioninfo,
				  chip, READ_ADDR(MINION_CORE_ENA0_31 + rep),
				  rbuf, MINION_CORE_SIZ, data);

		minioninfo->chip_core_ena[rep][chip] = *((uint32_t *)&(rbuf[HSIZE()]));
	}

	// store the core active state
	for (rep = 0; rep < MINION_CORE_REPS; rep++) {
		data[0] = 0x0;
		data[1] = 0x0;
		data[2] = 0x0;
		data[3] = 0x0;

		reply = build_cmd(minioncgpu, minioninfo,
				  chip, READ_ADDR(MINION_CORE_ACT0_31 + rep),
				  rbuf, MINION_CORE_SIZ, data);

		minioninfo->chip_core_act[rep][chip] = *((uint32_t *)&(rbuf[HSIZE()]));
	}
}

#if ENABLE_INT_NONO
static void enable_interrupt(struct cgpu_info *minioncgpu, struct minion_info *minioninfo, int chip)
{
	uint8_t rbuf[MINION_BUFSIZ];
	uint8_t data[4];
	__maybe_unused int reply;

	data[0] = MINION_RESULT_INT_SIZE;
	data[1] = 0x00;
	data[2] = 0x00;
	data[3] = 0x00;

	reply = build_cmd(minioncgpu, minioninfo,
			  chip, WRITE_ADDR(MINION_SYS_BUF_TRIG),
			  rbuf, 0, data);

//	data[0] = MINION_QUE_MAX; // spaces available ... i.e. empty
//	data[0] = MINION_QUE_LOW; // spaces in use
	data[0] = MINION_QUE_MAX - MINION_QUE_LOW; // spaces available
	data[1] = 0x00;
	data[2] = 0x00;
	data[3] = 0x00;

	reply = build_cmd(minioncgpu, minioninfo,
			  chip, WRITE_ADDR(MINION_SYS_QUE_TRIG),
			  rbuf, 0, data);

//	data[0] = MINION_RESULT_INT;
	data[0] = MINION_RESULT_INT | MINION_CMD_INT;
	data[1] = 0x00;
	data[2] = 0x00;
	data[3] = 0x00;

	reply = build_cmd(minioncgpu, minioninfo,
			  chip, WRITE_ADDR(MINION_SYS_INT_ENA),
			  rbuf, 0, data);
}
#endif

// Simple detect - just check each chip for the signature
static void minion_detect_chips(struct cgpu_info *minioncgpu, struct minion_info *minioninfo)
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
					if (sig == MINION_CHIP_SIG_SHIFT1 ||
					    sig == MINION_CHIP_SIG_SHIFT2 ||
					    sig == MINION_CHIP_SIG_SHIFT3 ||
					    sig == MINION_CHIP_SIG_SHIFT4) {
						applog(LOG_WARNING, "%s: chip %d detect offset got"
								    " 0x%08x wanted 0x%08x",
								    minioncgpu->drv->dname, chip, sig,
								    MINION_CHIP_SIG);
					} else {
						if (sig == MINION_NOCHIP_SIG ||
						    sig == MINION_NOCHIP_SIG2) // Assume no chip
							ok = true;
						else {
							applog(LOG_ERR, "%s: chip %d detect failed got"
									" 0x%08x wanted 0x%08x",
									minioncgpu->drv->dname, chip, sig,
									MINION_CHIP_SIG);
						}
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

#if ENABLE_INT_NONO
		// After everything is ready
		for (chip = 0; chip < MINION_CHIPS; chip++)
			if (minioninfo->chip[chip])
				enable_interrupt(minioncgpu, minioninfo, chip);
#endif
	}
}

static const char *minion_modules[] = {
#if MINION_ROCKCHIP == 0
	"i2c-dev",
	"i2c-bcm2708",
	"spidev",
	"spi-bcm2708",
#endif
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
	{ SPI_IOC_RD_MAX_SPEED_HZ, MINION_SPI_SPEED },
	{ SPI_IOC_WR_MAX_SPEED_HZ, MINION_SPI_SPEED },
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

#if ENABLE_INT_NONO
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
					err, (int)strlen(pin));
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
				err, (int)strlen(MINION_GPIO_DIR_READ));
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
				err, (int)strlen(MINION_GPIO_EDGE_RISING));
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
				err, (int)strlen(MINION_GPIO_ACT_HI));
		return false;
	}
	close(file);

	// Setup fd access to Value
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
#endif

// Default meaning all cores
static void default_all_cores(uint8_t *cores)
{
	int i;

	// clear all bits
	for (i = 0; i < (int)(DATA_SIZ * MINION_CORE_REPS); i++)
		cores[i] = 0x00;

	// enable (only) all cores
	for (i = 0; i < MINION_CORES; i++)
		ENABLE_CORE(cores, i);
}

static void minion_process_options(struct minion_info *minioninfo)
{
	int last_freq, last_temp;
	char *freq, *temp, *core, *comma, *buf, *plus, *minus;
	uint8_t last_cores[DATA_SIZ*MINION_CORE_REPS];
	int i, core1, core2;
	bool cleared;

	last_freq = MINION_FREQ_DEF;
	if (opt_minion_freq && *opt_minion_freq) {
		buf = freq = strdup(opt_minion_freq);
		comma = strchr(freq, ',');
		if (comma)
			*(comma++) = '\0';

		for (i = 0; i < MINION_CHIPS; i++) {
			if (freq && isdigit(*freq)) {
				last_freq = (int)(round((double)atoi(freq) / (double)MINION_FREQ_FACTOR)) * MINION_FREQ_FACTOR;
				if (last_freq < MINION_FREQ_MIN)
					last_freq = MINION_FREQ_MIN;
				if (last_freq > MINION_FREQ_MAX)
					last_freq = MINION_FREQ_MAX;

				freq = comma;
				if (comma) {
					comma = strchr(freq, ',');
					if (comma)
						*(comma++) = '\0';
				}
			}
			minioninfo->init_freq[i] = last_freq;
		}
		free(buf);
	}

	last_temp = MINION_TEMP_CTL_DEF;
	if (opt_minion_temp && *opt_minion_temp) {
		buf = temp = strdup(opt_minion_temp);
		comma = strchr(temp, ',');
		if (comma)
			*(comma++) = '\0';

		for (i = 0; i < MINION_CHIPS; i++) {
			if (temp) {
				if (isdigit(*temp)) {
					last_temp = atoi(temp);
					last_temp -= (last_temp % MINION_TEMP_CTL_STEP);
					if (last_temp < MINION_TEMP_CTL_MIN_VALUE)
						last_temp = MINION_TEMP_CTL_MIN_VALUE;
					if (last_temp > MINION_TEMP_CTL_MAX_VALUE)
						last_temp = MINION_TEMP_CTL_MAX_VALUE;
				} else {
					if (strcasecmp(temp, MINION_TEMP_DISABLE) == 0)
						last_temp = MINION_TEMP_CTL_DISABLE;
				}

				temp = comma;
				if (comma) {
					comma = strchr(temp, ',');
					if (comma)
						*(comma++) = '\0';
				}
			}
			minioninfo->init_temp[i] = last_temp;
		}
		free(buf);
	}

	default_all_cores(&(last_cores[0]));
	// default to all cores until we find valid data
	cleared = false;
	if (opt_minion_cores && *opt_minion_cores) {
		buf = core = strdup(opt_minion_cores);
		comma = strchr(core, ',');
		if (comma)
			*(comma++) = '\0';

		for (i = 0; i < MINION_CHIPS; i++) {
			// default to previous until we find valid data
			cleared = false;
			if (core) {
				plus = strchr(core, '+');
				if (plus)
					*(plus++) = '\0';
				while (core) {
					minus = strchr(core, '-');
					if (minus)
						*(minus++) = '\0';
					if (isdigit(*core)) {
						core1 = atoi(core);
						if (core1 >= 0 && core1 < MINION_CORES) {
							if (!minus) {
								if (!cleared) {
									memset(last_cores, 0, sizeof(last_cores));
									cleared = true;
								}
								ENABLE_CORE(last_cores, core1);
							} else {
								core2 = atoi(minus);
								if (core2 >= core1) {
									if (core2 >= MINION_CORES)
										core2 = MINION_CORES - 1;
									while (core1 <= core2) {
										if (!cleared) {
											memset(last_cores, 0,
												sizeof(last_cores));
											cleared = true;
										}
										ENABLE_CORE(last_cores, core1);
										core1++;
									}
								}
							}
						}
					} else {
						if (strcasecmp(core, MINION_CORE_ALL) == 0)
							default_all_cores(&(last_cores[0]));
					}
					core = plus;
					if (plus) {
						plus = strchr(core, '+');
						if (plus)
							*(plus++) = '\0';
					}
				}
				core = comma;
				if (comma) {
					comma = strchr(core, ',');
					if (comma)
						*(comma++) = '\0';
				}
			}
			memcpy(&(minioninfo->init_cores[i][0]), &(last_cores[0]), sizeof(last_cores));
		}
		free(buf);
	}
}

static void minion_detect(bool hotplug)
{
	struct cgpu_info *minioncgpu = NULL;
	struct minion_info *minioninfo = NULL;
	char buf[512];
	size_t off;
	int i;

	if (hotplug)
		return;

	minioncgpu = calloc(1, sizeof(*minioncgpu));
	if (unlikely(!minioncgpu))
		quithere(1, "Failed to calloc minioncgpu");

	minioncgpu->drv = &minion_drv;
	minioncgpu->deven = DEV_ENABLED;
	minioncgpu->threads = 1;

	minioninfo = calloc(1, sizeof(*minioninfo)); // everything '0'
	if (unlikely(!minioninfo))
		quithere(1, "Failed to calloc minioninfo");
	minioncgpu->device_data = (void *)minioninfo;

	if (!minion_init_spi(minioncgpu, minioninfo, MINION_SPI_BUS, MINION_SPI_CHIP))
		goto unalloc;

#if ENABLE_INT_NONO
	if (!minion_init_gpio_interrupt(minioncgpu, minioninfo))
		goto unalloc;
#endif

	mutex_init(&(minioninfo->spi_lock));
	mutex_init(&(minioninfo->sta_lock));

	for (i = 0; i < MINION_CHIPS; i++) {
		minioninfo->init_freq[i] = MINION_FREQ_DEF;
		minioninfo->init_temp[i] = MINION_TEMP_CTL_DEF;
		default_all_cores(&(minioninfo->init_cores[i][0]));
	}


	minion_process_options(minioninfo);

	applog(LOG_WARNING, "%s: checking for chips ...", minioncgpu->drv->dname);

	minion_detect_chips(minioncgpu, minioninfo);

	buf[0] = '\0';
	for (i = 0; i < MINION_CHIPS; i++) {
		if (minioninfo->chip[i]) {
			off = strlen(buf);
			snprintf(buf + off, sizeof(buf) - off, " %d", i);
		}
	}

	applog(LOG_WARNING, "%s: found %d chip%s:%s",
				minioncgpu->drv->dname, minioninfo->chips,
				(minioninfo->chips == 1) ? "" : "s", buf);

	if (minioninfo->chips == 0)
		goto cleanup;

	if (!add_cgpu(minioncgpu))
		goto cleanup;

	mutex_init(&(minioninfo->nonce_lock));

	minioninfo->wfree_list = k_new_list("Work", sizeof(WITEM), ALLOC_WITEMS, LIMIT_WITEMS, true);
	minioninfo->wwork_list = k_new_store(minioninfo->wfree_list);
	// Initialise them all in case we later decide to enable chips
	for (i = 0; i < MINION_CHIPS; i++) {
		minioninfo->wque_list[i] = k_new_store(minioninfo->wfree_list);
		minioninfo->wchip_list[i] = k_new_store(minioninfo->wfree_list);
	}

	minioninfo->tfree_list = k_new_list("Task", sizeof(TITEM), ALLOC_TITEMS, LIMIT_TITEMS, true);
	minioninfo->task_list = k_new_store(minioninfo->tfree_list);
	minioninfo->treply_list = k_new_store(minioninfo->tfree_list);

	minioninfo->rfree_list = k_new_list("Reply", sizeof(RITEM), ALLOC_RITEMS, LIMIT_RITEMS, true);
	minioninfo->rnonce_list = k_new_store(minioninfo->rfree_list);

	minioninfo->history_gen = MINION_MAX_RESET_CHECK;
	minioninfo->hfree_list = k_new_list("History", sizeof(HITEM), ALLOC_HITEMS, LIMIT_HITEMS, true);
	for (i = 0; i < MINION_CHIPS; i++)
		minioninfo->hchip_list[i] = k_new_store(minioninfo->hfree_list);

	cgsem_init(&(minioninfo->task_ready));
	cgsem_init(&(minioninfo->nonce_ready));
	cgsem_init(&(minioninfo->scan_work));

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
	// flash a led
}

/*
 * SPI/ioctl write thread
 * Non urgent work is to keep the queue full
 * Urgent work is when an LP occurs (or the queue is empty/low)
 */
static void *minion_spi_write(void *userdata)
{
	struct cgpu_info *minioncgpu = (struct cgpu_info *)userdata;
	struct minion_info *minioninfo = (struct minion_info *)(minioncgpu->device_data);
	K_ITEM *item, *tail, *task, *work;
	TITEM *titem;

	applog(MINION_LOG, "%s%i: SPI writing...",
				minioncgpu->drv->name, minioncgpu->device_id);

	// Wait until we're ready
	while (minioncgpu->shutdown == false) {
		if (minioninfo->initialised) {
			break;
		}
		cgsleep_ms(1); // asap to start mining
	}

	// TODO: combine all urgent into a single I/O?
	// Then combine all state 1 for the same chip into a single I/O ?
	// (then again for state 2?)
	while (minioncgpu->shutdown == false) {
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

			k_unlink_item(minioninfo->task_list, item);
		}
		K_WUNLOCK(minioninfo->task_list);

		if (item) {
			bool do_txrx = true;
			bool store_reply = true;

			titem = DATAT(item);

			switch (titem->address) {
				// TODO: case MINION_SYS_TEMP_CTL:
				// TODO: case MINION_SYS_FREQ_CTL:
				case READ_ADDR(MINION_SYS_CHIP_STA):
				case WRITE_ADDR(MINION_SYS_RSTN_CTL):
				case WRITE_ADDR(MINION_SYS_INT_CLR):
				case READ_ADDR(MINION_SYS_IDLE_CNT):
				case READ_ADDR(MINION_CORE_ENA0_31):
				case READ_ADDR(MINION_CORE_ENA32_63):
				case READ_ADDR(MINION_CORE_ENA64_95):
				case READ_ADDR(MINION_CORE_ENA96_98):
				case READ_ADDR(MINION_CORE_ACT0_31):
				case READ_ADDR(MINION_CORE_ACT32_63):
				case READ_ADDR(MINION_CORE_ACT64_95):
				case READ_ADDR(MINION_CORE_ACT96_98):
					store_reply = false;
					break;
				case WRITE_ADDR(MINION_QUE_0):
//applog(LOG_ERR, "%s%i: ZZZ send task_id 0x%04x - chip %d", minioncgpu->drv->name, minioncgpu->device_id, titem->task_id, titem->chip);
					store_reply = false;
					break;
				default:
					do_txrx = false;
					titem->reply = MINION_UNEXPECTED_TASK;
					applog(LOG_ERR, "%s%i: Unexpected task address 0x%02x (%s)",
							minioncgpu->drv->name, minioncgpu->device_id,
							(unsigned int)(titem->address),
							addr2txt(titem->address));

					break;
			}

			if (do_txrx) {
				minion_txrx(titem);

				int chip = titem->chip;
				switch (titem->address) {
					case READ_ADDR(MINION_SYS_CHIP_STA):
						if (titem->reply >= (int)(titem->osiz)) {
							uint8_t *rep = &(titem->rbuf[titem->osiz - titem->rsiz]);
							mutex_lock(&(minioninfo->sta_lock));
							minioninfo->chip_status[chip].temp = STA_TEMP(rep);
							minioninfo->chip_status[chip].cores = STA_CORES(rep);
							minioninfo->chip_status[chip].freq = STA_FREQ(rep);
							mutex_unlock(&(minioninfo->sta_lock));

							if (minioninfo->chip_status[chip].overheat) {
								switch (STA_TEMP(rep)) {
									case MINION_TEMP_40:
									case MINION_TEMP_60:
									case MINION_TEMP_80:
										cgtime(&(minioninfo->chip_status[chip].lastrecover));
										minioninfo->chip_status[chip].overheat = false;
										applog(LOG_WARNING, "%s%d: chip %d cooled, restarting",
												    minioncgpu->drv->name,
												    minioncgpu->device_id,
												    chip);
										cgtime(&(minioninfo->chip_status[chip].lastrecover));
										minioninfo->chip_status[chip].overheattime +=
											tdiff(&(minioninfo->chip_status[chip].lastrecover),
												&(minioninfo->chip_status[chip].lastoverheat));
										break;
									default:
										break;
								}
							} else {
								if (opt_minion_overheat && STA_TEMP(rep) == MINION_TEMP_OVER) {
									cgtime(&(minioninfo->chip_status[chip].lastoverheat));
									minioninfo->chip_status[chip].overheat = true;
									applog(LOG_WARNING, "%s%d: chip %d overheated! idling",
											    minioncgpu->drv->name,
											    minioncgpu->device_id,
											    chip);
									K_WLOCK(minioninfo->tfree_list);
									task = k_unlink_head(minioninfo->tfree_list);
									DATAT(task)->tid = ++(minioninfo->next_tid);
									DATAT(task)->chip = chip;
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
									k_add_head(minioninfo->task_list, task);
									K_WUNLOCK(minioninfo->tfree_list);
									minioninfo->chip_status[chip].overheats++;
								}
							}
						}
						break;
					case READ_ADDR(MINION_SYS_IDLE_CNT):
						{
							uint32_t *cnt = (uint32_t *)&(titem->rbuf[titem->osiz - titem->rsiz]);
							minioninfo->chip_status[chip].idle = *cnt;
						}
						break;
					case WRITE_ADDR(MINION_SYS_RSTN_CTL):
						// Do this here after it has actually been flushed
						if ((titem->wbuf[0] & SYS_RSTN_CTL_FLUSH) == SYS_RSTN_CTL_FLUSH) {
							K_WLOCK(minioninfo->wwork_list);
							work = minioninfo->wchip_list[chip]->head;
							while (work) {
								DATAW(work)->stale = true;
								minioninfo->chip_status[chip].chipwork--;
								if (minioninfo->chip_status[chip].realwork > 0)
									minioninfo->chip_status[chip].realwork--;
								work = work->next;
							}
							minioninfo->chip_status[chip].chipwork = 0;
							minioninfo->chip_status[chip].realwork = 0;
							K_WUNLOCK(minioninfo->wwork_list);
						}
						break;
					case WRITE_ADDR(MINION_QUE_0):
						K_WLOCK(minioninfo->wchip_list[chip]);
						k_unlink_item(minioninfo->wque_list[chip], titem->witem);
						k_add_head(minioninfo->wchip_list[chip], titem->witem);
						minioninfo->chip_status[chip].quework--;
						minioninfo->chip_status[chip].chipwork++;
						minioninfo->chip_status[chip].realwork++;
						K_WUNLOCK(minioninfo->wchip_list[chip]);
						break;
					case READ_ADDR(MINION_CORE_ENA0_31):
					case READ_ADDR(MINION_CORE_ENA32_63):
					case READ_ADDR(MINION_CORE_ENA64_95):
					case READ_ADDR(MINION_CORE_ENA96_98):
						{
							uint32_t *rep = (uint32_t *)&(titem->rbuf[titem->osiz - titem->rsiz]);
							int off = titem->address - READ_ADDR(MINION_CORE_ENA0_31);
							minioninfo->chip_core_ena[off][chip] = *rep;
						}
						break;
					case READ_ADDR(MINION_CORE_ACT0_31):
					case READ_ADDR(MINION_CORE_ACT32_63):
					case READ_ADDR(MINION_CORE_ACT64_95):
					case READ_ADDR(MINION_CORE_ACT96_98):
						{
							uint32_t *rep = (uint32_t *)&(titem->rbuf[titem->osiz - titem->rsiz]);
							int off = titem->address - READ_ADDR(MINION_CORE_ACT0_31);
							minioninfo->chip_core_act[off][chip] = *rep;
						}
						break;
					case WRITE_ADDR(MINION_SYS_INT_CLR):
						break;
					default:
						break;
				}
			}

			K_WLOCK(minioninfo->treply_list);
			if (store_reply)
				k_add_head(minioninfo->treply_list, item);
			else
				k_free_head(minioninfo->tfree_list, item);
			K_WUNLOCK(minioninfo->treply_list);

			/*
			 * Always check for the next task immediately if we just did one
			 * i.e. empty the task queue
			 */
			continue;
		}
		cgsem_mswait(&(minioninfo->task_ready), MINION_TASK_mS);
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
	struct minion_result *result1, *result2, *use1, *use2;
	K_ITEM *item;
	TITEM fifo_task, res1_task, res2_task;
	int chip, resoff;
	int chipwork, gap;
	bool somelow;
	struct timeval now;

#if ENABLE_INT_NONO
	TITEM clr_task;
	struct pollfd pfd;
	struct minion_header *head;
	uint8_t rbuf[MINION_BUFSIZ];
	uint8_t wbuf[MINION_BUFSIZ];
	uint32_t wsiz, rsiz;
	int ret, reply;
	bool gotreplies = false;
#endif

	applog(MINION_LOG, "%s%i: SPI replying...",
				minioncgpu->drv->name, minioncgpu->device_id);

	// Wait until we're ready
	while (minioncgpu->shutdown == false) {
		if (minioninfo->initialised) {
			break;
		}
		cgsleep_ms(2);
	}

	fifo_task.chip = 0;
	fifo_task.write = false;
	fifo_task.address = MINION_SYS_FIFO_STA;
	fifo_task.wsiz = 0;
	fifo_task.rsiz = MINION_SYS_SIZ;

	res1_task.chip = 0;
	res1_task.write = false;
	if (minreread)
		res1_task.address = MINION_RES_PEEK;
	else
		res1_task.address = MINION_RES_DATA;
	res1_task.wsiz = 0;
	res1_task.rsiz = MINION_RES_DATA_SIZ;

	res2_task.chip = 0;
	res2_task.write = false;
	res2_task.address = MINION_RES_DATA;
	res2_task.wsiz = 0;
	res2_task.rsiz = MINION_RES_DATA_SIZ;

#if ENABLE_INT_NONO
	// Clear RESULT_INT after reading all results
	clr_task.chip = 0;
	clr_task.write = true;
	clr_task.address = MINION_SYS_INT_CLR;
	clr_task.wsiz = MINION_SYS_SIZ;
	clr_task.rsiz = 0;
	clr_task.wbuf[0] = MINION_RESULT_INT;
	clr_task.wbuf[1] = 0;
	clr_task.wbuf[2] = 0;
	clr_task.wbuf[3] = 0;

	memset(&pfd, 0, sizeof(pfd));
	pfd.fd = minioninfo->gpiointfd;
	pfd.events = POLLPRI;

	head = (struct minion_header *)wbuf;
	SET_HEAD_SIZ(head, MINION_SYS_SIZ);
	wsiz = HSIZE() + MINION_SYS_SIZ;
	rsiz = MINION_SYS_SIZ; // for READ, use 0 for WRITE
#endif

	somelow = false;
	while (minioncgpu->shutdown == false) {
		for (chip = 0; chip < MINION_CHIPS; chip++) {
			if (minioninfo->chip[chip]) {
				int tries = 0;
				uint8_t res, cmd;
				while (++tries < 4) {
					res = cmd = 0;
					fifo_task.chip = chip;
					fifo_task.reply = 0;
					minion_txrx(&fifo_task);
					if (fifo_task.reply <= 0)
						break;
					else {
						if (fifo_task.reply < (int)(fifo_task.osiz)) {
							char *buf = bin2hex((unsigned char *)(&(fifo_task.rbuf[fifo_task.osiz - fifo_task.rsiz])),
										(int)(fifo_task.rsiz));
							applog(LOG_ERR, "%s%i: Bad fifo reply (%s) size %d, should be %d",
									minioncgpu->drv->name, minioncgpu->device_id, buf,
									fifo_task.reply, (int)(fifo_task.osiz));
							free(buf);
							minioninfo->spi_errors++;
							minioninfo->fifo_spi_errors[chip]++;
							minioninfo->res_err_count[chip]++;
						} else {
							if (fifo_task.reply > (int)(fifo_task.osiz)) {
								applog(LOG_ERR, "%s%i: Unexpected fifo reply size %d, expected only %d",
										minioncgpu->drv->name, minioncgpu->device_id,
										fifo_task.reply, (int)(fifo_task.osiz));
							}
							res = FIFO_RES(fifo_task.rbuf, fifo_task.osiz - fifo_task.rsiz);
							cmd = FIFO_CMD(fifo_task.rbuf, fifo_task.osiz - fifo_task.rsiz);
							// valid reply?
							if (res <= MINION_QUE_MAX && cmd <= MINION_QUE_HIGH)
								break;

							applog(LOG_ERR, "%s%i: Bad fifo reply res %d (max is %d) cmd %d (max is %d)",
									minioncgpu->drv->name, minioncgpu->device_id,
									(int)res, MINION_QUE_MAX, (int)cmd, MINION_QUE_HIGH);
							minioninfo->spi_errors++;
							minioninfo->fifo_spi_errors[chip]++;
							minioninfo->res_err_count[chip]++;
						}
					}
				}

				// Give up on this chip this round
				if (tries >= 4)
					continue;

				K_WLOCK(minioninfo->wwork_list);
				// it shouldn't go up
				if (cmd < minioninfo->chip_status[chip].realwork)
					minioninfo->chip_status[chip].realwork = (uint32_t)cmd;
				else {
					cmd = (uint8_t)(minioninfo->chip_status[chip].realwork);
					minioninfo->spi_errors++;
					minioninfo->fifo_spi_errors[chip]++;
					minioninfo->res_err_count[chip]++;
				}
				chipwork = (int)(minioninfo->chip_status[chip].chipwork);
				K_WUNLOCK(minioninfo->wwork_list);
				gap = chipwork - (int)cmd;
				if (gap < -1 || gap > 1) {
//					applog(LOG_ERR, "%s%i: fifo cmd difference > 1 for chip %d - work %d cmd %d gap %d",
//							minioncgpu->drv->name, minioncgpu->device_id,
//							chip, chipwork, (int)cmd, gap);
				}

				if (cmd < MINION_QUE_LOW) {
					somelow = true;
					// Flag it in case the count is wrong
					K_WLOCK(minioninfo->wwork_list);
					minioninfo->chip_status[chip].islow = true;
					minioninfo->chip_status[chip].lowcount = (int)cmd;
					K_WUNLOCK(minioninfo->wwork_list);
				}

				/*
				 * Chip has results?
				 * You can't request results unless it says it has some.
				 * We don't ever directly flush the output queue while processing
				 * (except at startup) so the answer is always valid
				 * i.e. there could be more, but never less ... unless the reply was corrupt
				 */
				if (res > MINION_MAX_RES) {
					applog(LOG_ERR, "%s%i: Large work reply chip %d res %d",
							minioncgpu->drv->name, minioncgpu->device_id, chip, res);
					minioninfo->spi_errors++;
					minioninfo->fifo_spi_errors[chip]++;
					minioninfo->res_err_count[chip]++;
					res = 1; // Just read one result
				}
//else
//applog(LOG_ERR, "%s%i: work reply res %d", minioncgpu->drv->name, minioncgpu->device_id, res);
				uint8_t left = res;
				int peeks = 0;
				while (left > 0) {
					res = left;
					if (res > MINION_MAX_RES)
						res = MINION_MAX_RES;
					left -= res;
repeek:
					res1_task.chip = chip;
					res1_task.reply = 0;
					res1_task.rsiz = res * MINION_RES_DATA_SIZ;
					minion_txrx(&res1_task);
					if (res1_task.reply <= 0)
						break;
					else {
						cgtime(&now);
						if (res1_task.reply < (int)MINION_RES_DATA_SIZ) {
							char *buf = bin2hex((unsigned char *)(&(res1_task.rbuf[res1_task.osiz - res1_task.rsiz])), (int)(res1_task.rsiz));
							applog(LOG_ERR, "%s%i: Bad work reply (%s) size %d, should be at least %d",
									minioncgpu->drv->name, minioncgpu->device_id, buf,
									res1_task.reply, (int)MINION_RES_DATA_SIZ);
							free(buf);
							minioninfo->spi_errors++;
							minioninfo->res_spi_errors[chip]++;
							minioninfo->res_err_count[chip]++;
						} else {
							if (res1_task.reply != (int)(res1_task.osiz)) {
								applog(LOG_ERR, "%s%i: Unexpected work reply size %d, expected %d",
										minioncgpu->drv->name, minioncgpu->device_id,
										res1_task.reply, (int)(res1_task.osiz));
								minioninfo->spi_errors++;
								minioninfo->res_spi_errors[chip]++;
								minioninfo->res_err_count[chip]++;
								// Can retry a PEEK without losing data
								if (minreread) {
									if (++peeks < 4)
										goto repeek;
									break;
								}
							}

							if (minreread) {
								res2_task.chip = chip;
								res2_task.reply = 0;
								res2_task.rsiz = res * MINION_RES_DATA_SIZ;
								minion_txrx(&res2_task);
								if (res2_task.reply <= 0) {
									minioninfo->spi_errors++;
									minioninfo->res_spi_errors[chip]++;
									minioninfo->res_err_count[chip]++;
								}
							}

							for (resoff = res1_task.osiz - res1_task.rsiz; resoff < (int)res1_task.osiz; resoff += MINION_RES_DATA_SIZ) {
								result1 = (struct minion_result *)&(res1_task.rbuf[resoff]);
								if (minreread && resoff < (int)res2_task.osiz)
									result2 = (struct minion_result *)&(res2_task.rbuf[resoff]);
								else
									result2 = NULL;

								if (IS_RESULT(result1) || (minreread && result2 && IS_RESULT(result2))) {
									K_WLOCK(minioninfo->rfree_list);
									item = k_unlink_head(minioninfo->rfree_list);
									K_WUNLOCK(minioninfo->rfree_list);

									if (IS_RESULT(result1)) {
										use1 = result1;
										if (minreread && result2 && IS_RESULT(result2))
											use2 = result2;
										else
											use2 = NULL;
									} else {
										use1 = result2;
										use2 = NULL;
										minioninfo->use_res2[chip]++;
									}

									//DATAR(item)->chip = RES_CHIP(use1);
									// We can avoid any SPI transmission error of the chip number
									DATAR(item)->chip = (uint8_t)chip;
									if ((uint8_t)chip != RES_CHIP(use1)) {
										minioninfo->spi_errors++;
										minioninfo->res_spi_errors[chip]++;
										minioninfo->res_err_count[chip]++;
									}
									if (use2 && (uint8_t)chip != RES_CHIP(use2)) {
										minioninfo->spi_errors++;
										minioninfo->res_spi_errors[chip]++;
										minioninfo->res_err_count[chip]++;
									}
									DATAR(item)->core = RES_CORE(use1);
									DATAR(item)->task_id = RES_TASK(use1);
									DATAR(item)->nonce = RES_NONCE(use1);
									DATAR(item)->no_nonce = !RES_GOLD(use1);
									memcpy(&(DATAR(item)->when), &now, sizeof(now));
									if (!use2)
										DATAR(item)->another = false;
									else {
										DATAR(item)->another = true;
										DATAR(item)->task_id2 = RES_TASK(use2);
										DATAR(item)->nonce2 = RES_NONCE(use2);
									}
//applog(MINTASK_LOG, "%s%i: ZZZ reply task_id 0x%04x - chip %d - gold %d", minioncgpu->drv->name, minioncgpu->device_id, RES_TASK(use1), (int)RES_CHIP(use1), (int)RES_GOLD(use1));

//if (RES_GOLD(use1))
//applog(MINTASK_LOG, "%s%i: found a result chip %d core %d task 0x%04x nonce 0x%08x gold=%d", minioncgpu->drv->name, minioncgpu->device_id, DATAR(item)->chip, DATAR(item)->core, DATAR(item)->task_id, DATAR(item)->nonce, (int)RES_GOLD(use1));

									K_WLOCK(minioninfo->rnonce_list);
									k_add_head(minioninfo->rnonce_list, item);
									K_WUNLOCK(minioninfo->rnonce_list);
									cgsem_post(&(minioninfo->nonce_ready));
								} else {
									minioninfo->res_err_count[chip]++;
									applog(MINTASK_LOG, "%s%i: Invalid res0 task_id 0x%04x - chip %d",
											    minioncgpu->drv->name, minioncgpu->device_id,
											    RES_TASK(result1), chip);
									if (minreread && result2) {
										applog(MINTASK_LOG, "%s%i: Invalid res1 task_id 0x%04x - chip %d",
												    minioncgpu->drv->name, minioncgpu->device_id,
												    RES_TASK(result2), chip);
									}
								}
							}
						}
					}
				}
			}
		}

		if (somelow)
			cgsem_post(&(minioninfo->scan_work));

#if ENABLE_INT_NONO
		if (gotreplies)
			minion_txrx(&clr_task);
#endif

#if !ENABLE_INT_NONO
		cgsleep_ms(MINION_REPLY_mS);
#else
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
			bool gotres;
			int c;

			minioninfo->interrupts++;

			read(minioninfo->gpiointfd, &c, 1);

//			applog(LOG_ERR, "%s%i: Interrupt2",
//					minioncgpu->drv->name,
//					minioncgpu->device_id);

			gotres = false;
			for (chip = 0; chip < MINION_CHIPS; chip++) {
				if (minioninfo->chip[chip]) {
					SET_HEAD_READ(head, MINION_SYS_INT_STA);
					head->chip = chip;
					reply = do_ioctl(wbuf, wsiz, rbuf, rsiz);
					if (reply != (int)wsiz) {
						applog(LOG_ERR, "%s: chip %d int status returned %d"
								" (should be %d)",
								minioncgpu->drv->dname,
								chip, reply, (int)wsiz);
					}

					snprintf(minioninfo->last_interrupt,
						 sizeof(minioninfo->last_interrupt),
						 "%d %d 0x%02x%02x%02x%02x%02x%02x%02x%02x %d %d 0x%02x %d %d",
						 (int)(minioninfo->interrupts), chip,
						 rbuf[0], rbuf[1], rbuf[2], rbuf[3],
						 rbuf[4], rbuf[5], rbuf[6], rbuf[7],
						 (int)wsiz, (int)rsiz, rbuf[wsiz - rsiz],
						 rbuf[wsiz - rsiz] & MINION_RESULT_INT,
						 rbuf[wsiz - rsiz] & MINION_CMD_INT);

					if ((rbuf[wsiz - rsiz] & MINION_RESULT_INT) != 0) {
						gotres = true;
						(minioninfo->result_interrupts)++;
//						applog(LOG_ERR, "%s%i: chip %d got RES interrupt",
//								minioncgpu->drv->name,
//								minioncgpu->device_id,
//								chip);
					}

					if ((rbuf[wsiz - rsiz] & MINION_CMD_INT) != 0) {
						// Work queue is empty
						(minioninfo->command_interrupts)++;
//						applog(LOG_ERR, "%s%i: chip %d got CMD interrupt",
//								minioncgpu->drv->name,
//								minioncgpu->device_id,
//								chip);
					}

//					char *tmp;
//					tmp = bin2hex(rbuf, wsiz);
//					applog(LOG_ERR, "%s%i: chip %d interrupt: %s",
//							minioncgpu->drv->name,
//							minioncgpu->device_id,
//							chip, tmp);
//					free(tmp);

					// Don't clear either interrupt until after send/recv
				}
			}

			// Doing this last means we can't miss an interrupt
			if (gotres)
				cgsem_post(&(minioninfo->scan_work));
		}
#endif
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
	NONCE_NO_WORK,
	NONCE_SPI_ERR
};

static void cleanup_older(struct cgpu_info *minioncgpu, int chip, K_ITEM *item, bool no_nonce)
{
	struct minion_info *minioninfo = (struct minion_info *)(minioncgpu->device_data);
	K_ITEM *tail;
//	bool errs;

	/*
	 * remove older work items (no_nonce means this 'item' has finished also)
	 */
	if (item->next || no_nonce) {
		K_WLOCK(minioninfo->wchip_list[chip]);
		tail = minioninfo->wchip_list[chip]->tail;
		while (tail && tail != item) {
			k_unlink_item(minioninfo->wchip_list[chip], tail);
			if (!(DATAW(tail)->stale)) {
				minioninfo->chip_status[chip].chipwork--;
				if (minioninfo->chip_status[chip].realwork > 0)
					minioninfo->chip_status[chip].realwork--;
/*
				// If it had no valid work (only errors) then it won't have been cleaned up
				errs = (DATAW(tail)->errors > 0);
				applog(errs ? LOG_DEBUG : LOG_ERR,
				applog(LOG_ERR,
					"%s%i: discarded old task 0x%04x chip %d no reply errs=%d",
					minioncgpu->drv->name, minioncgpu->device_id,
					DATAW(tail)->task_id, chip, DATAW(tail)->errors);
*/
			}
			K_WUNLOCK(minioninfo->wchip_list[chip]);
			applog(MINION_LOG, "%s%i: marking complete - old task 0x%04x chip %d",
					   minioncgpu->drv->name, minioncgpu->device_id,
					   DATAW(tail)->task_id, chip);
			if (DATAW(tail)->rolled)
				free_work(DATAW(tail)->work);
			else
				work_completed(minioncgpu, DATAW(tail)->work);
			K_WLOCK(minioninfo->wchip_list[chip]);
			k_free_head(minioninfo->wfree_list, tail);
			tail = minioninfo->wchip_list[chip]->tail;
		}
		if (no_nonce) {
			k_unlink_item(minioninfo->wchip_list[chip], item);
			if (!(DATAW(item)->stale)) {
				minioninfo->chip_status[chip].chipwork--;
				if (minioninfo->chip_status[chip].realwork > 0)
					minioninfo->chip_status[chip].realwork--;
			}
			K_WUNLOCK(minioninfo->wchip_list[chip]);
			applog(MINION_LOG, "%s%i: marking complete - no_nonce task 0x%04x chip %d",
					   minioncgpu->drv->name, minioncgpu->device_id,
					   DATAW(item)->task_id, chip);
			if (DATAW(item)->rolled)
				free_work(DATAW(item)->work);
			else
				work_completed(minioncgpu, DATAW(item)->work);
			K_WLOCK(minioninfo->wchip_list[chip]);
			k_free_head(minioninfo->wfree_list, item);
		}
		K_WUNLOCK(minioninfo->wchip_list[chip]);
	}
}

static enum nonce_state oknonce(struct thr_info *thr, struct cgpu_info *minioncgpu, int chip, int core,
				uint32_t task_id, uint32_t nonce, bool no_nonce, struct timeval *when,
				bool another, uint32_t task_id2, uint32_t nonce2)
{
	struct minion_info *minioninfo = (struct minion_info *)(minioncgpu->device_data);
	struct timeval now;
	K_ITEM *item, *tail;
	uint32_t min_task_id, max_task_id;
	bool redo;

	// if the chip has been disabled - but we don't do that - so not possible (yet)
	if (!(minioninfo->chip[chip])) {
		minioninfo->spi_errors++;
		applog(MINTASK_LOG, "%s%i: nonce error chip %d not present",
				    minioncgpu->drv->name, minioncgpu->device_id, chip);
		return NONCE_NO_WORK;
	}

	if (core < 0 || core >= MINION_CORES) {
		minioninfo->spi_errors++;
		minioninfo->res_spi_errors[chip]++;
		minioninfo->res_err_count[chip]++;
		applog(MINTASK_LOG, "%s%i: SPI nonce error invalid core %d (chip %d)",
				    minioncgpu->drv->name, minioncgpu->device_id, core, chip);

		// use the fake core number so we don't discard the result
		core = FAKE_CORE;
	}

	if (no_nonce)
		minioninfo->chip_nononces[chip]++;
	else
		minioninfo->chip_nonces[chip]++;

	redo = false;
retry:
	K_RLOCK(minioninfo->wchip_list[chip]);
	item = minioninfo->wchip_list[chip]->tail;

	if (!item) {
		K_RUNLOCK(minioninfo->wchip_list[chip]);
		minioninfo->spi_errors++;
		minioninfo->res_spi_errors[chip]++;
		minioninfo->res_err_count[chip]++;
		applog(MINTASK_LOG, "%s%i: chip %d has no tasks (core %d task 0x%04x)",
				    minioncgpu->drv->name, minioncgpu->device_id,
				chip, core, (int)task_id);
		if (!no_nonce) {
			minioninfo->untested_nonces++;
			minioninfo->chip_err[chip]++;
		}
		return NONCE_NO_WORK;
	}

	min_task_id = DATAW(item)->task_id;
	while (item) {
		if (DATAW(item)->task_id == task_id)
			break;

		item = item->prev;
	}
	max_task_id = DATAW(minioninfo->wchip_list[chip]->head)->task_id;
	K_RUNLOCK(minioninfo->wchip_list[chip]);


	if (!item) {
		if (another && task_id != task_id2) {
			minioninfo->tasks_failed[chip]++;
			task_id = task_id2;
			redo = true;
			goto retry;
		}

		minioninfo->spi_errors++;
		minioninfo->res_spi_errors[chip]++;
		minioninfo->res_err_count[chip]++;
		applog(MINTASK_LOG, "%s%i: chip %d core %d unknown task 0x%04x (min=0x%04x max=0x%04x no_nonce=%d)",
				    minioncgpu->drv->name, minioncgpu->device_id,
				    chip, core, (int)task_id, (int)min_task_id,
				    (int)max_task_id, no_nonce);
		if (!no_nonce) {
			minioninfo->untested_nonces++;
			minioninfo->chip_err[chip]++;
		}
		return NONCE_BAD_WORK;
	}
	if (redo)
		minioninfo->tasks_recovered[chip]++;

	if (no_nonce) {
		cleanup_older(minioncgpu, chip, item, no_nonce);
		return NONCE_NO_NONCE;
	}

	minioninfo->tested_nonces++;

	redo = false;
retest:
	if (test_nonce(DATAW(item)->work, nonce)) {
//applog(MINTASK_LOG, "%s%i: Valid Nonce chip %d core %d task 0x%04x nonce 0x%08x", minioncgpu->drv->name, minioncgpu->device_id, chip, core, task_id, nonce);
		submit_tested_work(thr, DATAW(item)->work);

		if (redo)
			minioninfo->nonces_recovered[chip]++;

		minioninfo->chip_good[chip]++;
		minioninfo->core_good[chip][core]++;
		DATAW(item)->nonces++;

		mutex_lock(&(minioninfo->nonce_lock));
		minioninfo->new_nonces++;
		mutex_unlock(&(minioninfo->nonce_lock));
		minioninfo->ok_nonces++;

		cleanup_older(minioncgpu, chip, item, no_nonce);

		int chip_tmp;
		cgtime(&now);
		K_WLOCK(minioninfo->hfree_list);
		item = k_unlink_head(minioninfo->hfree_list);
		memcpy(&(DATAH(item)->when), when, sizeof(*when));
		k_add_head(minioninfo->hchip_list[chip], item);
		for (chip_tmp = 0; chip_tmp < MINION_CHIPS; chip_tmp++) {
			tail = minioninfo->hchip_list[chip_tmp]->tail;
			while (tail && tdiff(&(DATAH(tail)->when), &now) > MINION_HISTORY_s) {
				tail = k_unlink_tail(minioninfo->hchip_list[chip_tmp]);
				k_add_head(minioninfo->hfree_list, item);
				tail = minioninfo->hchip_list[chip_tmp]->tail;
			}
		}
		K_WUNLOCK(minioninfo->hfree_list);

		return NONCE_GOOD_NONCE;
	}

	if (another && nonce != nonce2) {
		minioninfo->nonces_failed[chip]++;
		nonce = nonce2;
		redo = true;
		goto retest;
	}

	DATAW(item)->errors++;
	minioninfo->chip_bad[chip]++;
	minioninfo->core_bad[chip][core]++;
	inc_hw_errors(thr);
//applog(MINTASK_LOG, "%s%i: HW ERROR chip %d core %d task 0x%04x nonce 0x%08x", minioncgpu->drv->name, minioncgpu->device_id, chip, core, task_id, nonce);

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
	struct timeval when;
	bool another;
	uint32_t task_id2;
	uint32_t nonce2;

	applog(MINION_LOG, "%s%i: Results...",
				minioncgpu->drv->name, minioncgpu->device_id);

	// Wait until we're ready
	while (minioncgpu->shutdown == false) {
		if (minioninfo->initialised) {
			break;
		}
		cgsleep_ms(3);
	}

	while (minioncgpu->shutdown == false) {
		if (!oldest_nonce(minioncgpu, &chip, &core, &task_id, &nonce,
				  &no_nonce, &when, &another, &task_id2, &nonce2)) {
			cgsem_mswait(&(minioninfo->nonce_ready), MINION_NONCE_mS);
			continue;
		}

		oknonce(thr, minioncgpu, chip, core, task_id, nonce, no_nonce, &when,
			another, task_id2, nonce2);
	}

	return NULL;
}

static void minion_flush_work(struct cgpu_info *minioncgpu)
{
	struct minion_info *minioninfo = (struct minion_info *)(minioncgpu->device_data);
	K_ITEM *stale_unused_work, *prev_unused, *task, *prev_task, *witem;
	int i;

	applog(MINION_LOG, "%s%i: flushing work",
				minioncgpu->drv->name, minioncgpu->device_id);

	// TODO: N.B. scanwork also gets work locks - which master thread calls flush?
	K_WLOCK(minioninfo->wwork_list);

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
			minioninfo->chip_status[DATAT(task)->chip].quework--;
			witem = DATAT(task)->witem;
			k_unlink_item(minioninfo->wque_list[DATAT(task)->chip], witem);
			k_free_head(minioninfo->wfree_list, witem);
			k_unlink_item(minioninfo->task_list, task);
			k_free_head(minioninfo->tfree_list, task);
		}
		task = prev_task;
	}
	for (i = 0; i < MINION_CHIPS; i++) {
		if (minioninfo->chip[i]) {
			// TODO: consider sending it now rather than adding to the task list?
			task = k_unlink_head(minioninfo->tfree_list);
			DATAT(task)->tid = ++(minioninfo->next_tid);
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
			k_add_head(minioninfo->task_list, task);
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
			if (DATAW(prev_unused)->rolled)
				free_work(DATAW(prev_unused)->work);
			else
				work_completed(minioncgpu, DATAW(prev_unused)->work);
			prev_unused = prev_unused->prev;
		}

		// put the items back in the wfree_list (oldest first)
		K_WLOCK(minioninfo->wfree_list);
		while (stale_unused_work) {
			prev_unused = stale_unused_work->prev;
			k_free_head(minioninfo->wfree_list, stale_unused_work);
			stale_unused_work = prev_unused;
		}
		K_WUNLOCK(minioninfo->wfree_list);
	}
}

static void sys_chip_sta(struct cgpu_info *minioncgpu, int chip)
{
	struct minion_info *minioninfo = (struct minion_info *)(minioncgpu->device_data);
	struct timeval now;
	K_ITEM *item;
	int limit, rep;

	cgtime(&now);
	// No lock required since 'last' is only accessed here
	if (minioninfo->chip_status[chip].last.tv_sec == 0) {
		memcpy(&(minioninfo->chip_status[chip].last), &now, sizeof(now));
	} else {
		limit = MINION_STATS_UPDATE_TIME_mS +
			(int)(random() % MINION_STATS_UPDATE_RAND_mS);
		if (ms_tdiff(&now, &(minioninfo->chip_status[chip].last)) > limit) {
			memcpy(&(minioninfo->chip_status[chip].last), &now, sizeof(now));

			K_WLOCK(minioninfo->tfree_list);
			item = k_unlink_head(minioninfo->tfree_list);
			DATAT(item)->tid = ++(minioninfo->next_tid);
			K_WUNLOCK(minioninfo->tfree_list);

			DATAT(item)->chip = chip;
			DATAT(item)->write = false;
			DATAT(item)->address = READ_ADDR(MINION_SYS_CHIP_STA);
			DATAT(item)->task_id = 0;
			DATAT(item)->wsiz = 0;
			DATAT(item)->rsiz = MINION_SYS_SIZ;
			DATAT(item)->urgent = false;

			K_WLOCK(minioninfo->task_list);
			k_add_head(minioninfo->task_list, item);
			item = k_unlink_head(minioninfo->tfree_list);
			DATAT(item)->tid = ++(minioninfo->next_tid);
			K_WUNLOCK(minioninfo->task_list);

			DATAT(item)->chip = chip;
			DATAT(item)->write = false;
			DATAT(item)->address = READ_ADDR(MINION_SYS_IDLE_CNT);
			DATAT(item)->task_id = 0;
			DATAT(item)->wsiz = 0;
			DATAT(item)->rsiz = MINION_SYS_SIZ;
			DATAT(item)->urgent = false;

			K_WLOCK(minioninfo->task_list);
			k_add_head(minioninfo->task_list, item);
			K_WUNLOCK(minioninfo->task_list);

			// Get the core ena and act state
			for (rep = 0; rep < MINION_CORE_REPS; rep++) {
				// Ena
				K_WLOCK(minioninfo->tfree_list);
				item = k_unlink_head(minioninfo->tfree_list);
				DATAT(item)->tid = ++(minioninfo->next_tid);
				K_WUNLOCK(minioninfo->tfree_list);

				DATAT(item)->chip = chip;
				DATAT(item)->write = false;
				DATAT(item)->address = READ_ADDR(MINION_CORE_ENA0_31 + rep);
				DATAT(item)->task_id = 0;
				DATAT(item)->wsiz = 0;
				DATAT(item)->rsiz = MINION_SYS_SIZ;
				DATAT(item)->urgent = false;

				K_WLOCK(minioninfo->task_list);
				k_add_head(minioninfo->task_list, item);
				// Act
				item = k_unlink_head(minioninfo->tfree_list);
				DATAT(item)->tid = ++(minioninfo->next_tid);
				K_WUNLOCK(minioninfo->task_list);

				DATAT(item)->chip = chip;
				DATAT(item)->write = false;
				DATAT(item)->address = READ_ADDR(MINION_CORE_ACT0_31 + rep);
				DATAT(item)->task_id = 0;
				DATAT(item)->wsiz = 0;
				DATAT(item)->rsiz = MINION_SYS_SIZ;
				DATAT(item)->urgent = false;

				K_WLOCK(minioninfo->task_list);
				k_add_head(minioninfo->task_list, item);
				K_WUNLOCK(minioninfo->task_list);
			}
		}
	}
}

static void new_work_task(struct cgpu_info *minioncgpu, K_ITEM *witem, int chip, bool urgent, uint8_t state)
{
	struct minion_info *minioninfo = (struct minion_info *)(minioncgpu->device_data);
	struct minion_que *que;
	K_ITEM *item;

	K_WLOCK(minioninfo->tfree_list);
	item = k_unlink_head(minioninfo->tfree_list);
	DATAT(item)->tid = ++(minioninfo->next_tid);
	K_WUNLOCK(minioninfo->tfree_list);

	DATAT(item)->chip = chip;
	DATAT(item)->write = true;
	DATAT(item)->address = MINION_QUE_0;

	// if threaded access to new_work_task() is added, this will need locking
	// Don't use task_id 0 so that we can ignore all '0' work replies
	// ... and report them as errors
	if (minioninfo->next_task_id == 0)
		minioninfo->next_task_id = 1;
	DATAT(item)->task_id = minioninfo->next_task_id;
	DATAW(witem)->task_id = minioninfo->next_task_id;
	minioninfo->next_task_id = (minioninfo->next_task_id + 1) & MINION_MAX_TASK_ID;

	DATAT(item)->urgent = urgent;
	DATAT(item)->work_state = state;
	DATAT(item)->work = DATAW(witem)->work;
	DATAT(item)->witem = witem;

	que = (struct minion_que *)&(DATAT(item)->wbuf[0]);
	que->task_id[0] = DATAT(item)->task_id & 0xff;
	que->task_id[1] = (DATAT(item)->task_id & 0xff00) >> 8;

	memcpy(&(que->midstate[0]), &(DATAW(witem)->work->midstate[0]), MIDSTATE_BYTES);
	memcpy(&(que->merkle7[0]), &(DATAW(witem)->work->data[MERKLE7_OFFSET]), MERKLE_BYTES);

	DATAT(item)->wsiz = (int)sizeof(*que);
	DATAT(item)->rsiz = 0;

	K_WLOCK(minioninfo->wque_list[chip]);
	k_add_head(minioninfo->wque_list[chip], witem);
	minioninfo->chip_status[chip].quework++;
	K_WUNLOCK(minioninfo->wque_list[chip]);

	K_WLOCK(minioninfo->task_list);
	k_add_head(minioninfo->task_list, item);
	K_WUNLOCK(minioninfo->task_list);

	if (urgent)
		cgsem_post(&(minioninfo->task_ready));

	// N.B. this will only update often enough if a chip is > ~2GH/s
	if (!urgent)
		sys_chip_sta(minioncgpu, chip);
}

// TODO: stale work ...
static K_ITEM *next_work(struct minion_info *minioninfo)
{
	K_ITEM *item;

	K_WLOCK(minioninfo->wwork_list);
	item = k_unlink_tail(minioninfo->wwork_list);
	K_WUNLOCK(minioninfo->wwork_list);

	return item;
}

static void minion_do_work(struct cgpu_info *minioncgpu)
{
	struct minion_info *minioninfo = (struct minion_info *)(minioncgpu->device_data);
	int count, chip, j, lowcount;
	uint8_t state;
	K_ITEM *item;
#if ENABLE_INT_NONO
	K_ITEM *task;
#endif
	bool islow, sentwork;

	// TODO: (remove this) Fake starved of work to test CMD Interrupt
//	if (total_secs > 120) {
//		cgsleep_ms(888);
//		return;
//	}

	/*
	 * Fill the queues as follows:
	 *	1) put at least 1 in each queue or if islow then add 1
	 *	2) push each queue up to LOW or if count is high but islow, then add LOW-1
	 *	3) push each LOW queue up to HIGH
	 */

	sentwork = false;
	for (state = 0; state < 3; state++) {
#define CHP 0
//applog(LOG_ERR, "%s%i: chip %d presta %d: quew %d chw %d", minioncgpu->drv->name, minioncgpu->device_id, CHP, state, minioninfo->chip_status[CHP].quework, minioninfo->chip_status[CHP].chipwork);
		for (chip = 0; chip < MINION_CHIPS; chip++)
			minioninfo->chip_status[chip].tohigh = false;

		for (chip = 0; chip < MINION_CHIPS; chip++) {
			if (minioninfo->chip[chip] && !minioninfo->chip_status[chip].overheat) {
				K_WLOCK(minioninfo->wchip_list[chip]);
				count = minioninfo->chip_status[chip].quework +
					minioninfo->chip_status[chip].realwork;
				islow = minioninfo->chip_status[chip].islow;
				minioninfo->chip_status[chip].islow = false;
				lowcount = minioninfo->chip_status[chip].lowcount;
				K_WUNLOCK(minioninfo->wchip_list[chip]);

				switch (state) {
					case 0:
						if (count == 0 || islow) {
							item = next_work(minioninfo);
							if (item) {
								new_work_task(minioncgpu, item, chip, true, state);
								sentwork = true;
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
						if (count < MINION_QUE_LOW || islow) {
							// do case 2: after we've done other chips
							minioninfo->chip_status[chip].tohigh = true;
							j = count;
							if (count >= MINION_QUE_LOW) {
								// islow means run a full case 1
								j = 1;
								applog(LOG_ERR, "%s%i: chip %d low que (%d) with high count %d",
										minioncgpu->drv->name,
										minioncgpu->device_id,
										chip, lowcount, count);
							}
							for (; j < MINION_QUE_LOW; j++) {
								item = next_work(minioninfo);
								if (item) {
									new_work_task(minioncgpu, item, chip, false, state);
									sentwork = true;
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
						if (count <= MINION_QUE_LOW || minioninfo->chip_status[chip].tohigh) {
							for (j = count; j < MINION_QUE_HIGH; j++) {
								item = next_work(minioninfo);
								if (item) {
									new_work_task(minioncgpu, item, chip, false, state);
									sentwork = true;
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
			} else
				if (minioninfo->chip[chip] && minioninfo->chip_status[chip].overheat && state == 2)
					sys_chip_sta(minioncgpu, chip);
		}
	}

	sentwork = sentwork;
#if ENABLE_INT_NONO
	if (sentwork) {
		// Clear CMD interrupt since we've now sent more
		K_WLOCK(minioninfo->tfree_list);
		task = k_unlink_head(minioninfo->tfree_list);
		DATAT(task)->tid = ++(minioninfo->next_tid);
		DATAT(task)->chip = 0; // ignored
		DATAT(task)->write = true;
		DATAT(task)->address = MINION_SYS_INT_CLR;
		DATAT(task)->task_id = 0; // ignored
		DATAT(task)->wsiz = MINION_SYS_SIZ;
		DATAT(task)->rsiz = 0;
		DATAT(task)->wbuf[0] = MINION_CMD_INT;
		DATAT(task)->wbuf[1] = 0;
		DATAT(task)->wbuf[2] = 0;
		DATAT(task)->wbuf[3] = 0;
		DATAT(task)->urgent = false;
		k_add_head(minioninfo->task_list, task);
		K_WUNLOCK(minioninfo->tfree_list);
	}
#endif

//applog(LOG_ERR, "%s%i: chip %d fin: quew %d chw %d", minioncgpu->drv->name, minioncgpu->device_id, CHP, minioninfo->chip_status[CHP].quework, minioninfo->chip_status[CHP].chipwork);
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

	for (i = 0; i < MINION_CHIPS; i++)
		if (minioninfo->chip[i])
// TODO:		minion_shutdown(minioncgpu, minioninfo, i);
			i = i;

	minioncgpu->shutdown = true;
}

static bool minion_queue_full(struct cgpu_info *minioncgpu)
{
	struct minion_info *minioninfo = (struct minion_info *)(minioncgpu->device_data);
	struct work *work, *usework;
	int count, need, roll, roll_limit;
	bool ret, rolled;

	K_RLOCK(minioninfo->wwork_list);
	count = minioninfo->wwork_list->count;
	K_RUNLOCK(minioninfo->wwork_list);

	if (count >= (MINION_QUE_HIGH * minioninfo->chips))
		ret = true;
	else {
		need = (MINION_QUE_HIGH * minioninfo->chips) - count;
		work = get_queued(minioncgpu);
		if (work) {
			roll_limit = work->drv_rolllimit;
			roll = 0;
			do {
				if (roll == 0) {
					usework = work;
					minioninfo->work_unrolled++;
					rolled = false;
				} else {
					usework = copy_work_noffset(work, roll);
					minioninfo->work_rolled++;
					rolled = true;
				}
				ready_work(minioncgpu, usework, rolled);
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

static void idle_report(struct cgpu_info *minioncgpu)
{
	struct minion_info *minioninfo = (struct minion_info *)(minioncgpu->device_data);
	struct timeval now;
	uint32_t idle;
	int msdiff;
	int chip;

	for (chip = 0; chip < MINION_CHIPS; chip++) {
		if (minioninfo->chip[chip]) {
			idle = minioninfo->chip_status[chip].idle;
			if (idle != minioninfo->chip_status[chip].last_rpt_idle) {
				cgtime(&now);
				msdiff = ms_tdiff(&now, &(minioninfo->chip_status[chip].idle_rpt));
				if (msdiff >= MINION_IDLE_MESSAGE_ms) {
					memcpy(&(minioninfo->chip_status[chip].idle_rpt), &now, sizeof(now));
					applog(LOG_WARNING,
						"%s%d: chip %d internal idle changed %08x",
						minioncgpu->drv->name, minioncgpu->device_id,
						chip, idle);
					minioninfo->chip_status[chip].last_rpt_idle = idle;
				}
			}
		}
	}
}

static void chip_report(struct cgpu_info *minioncgpu)
{
	struct minion_info *minioninfo = (struct minion_info *)(minioncgpu->device_data);
	struct timeval now;
	char buf[512];
	char res_err_msg[2];
	size_t len;
	double ghs, expect, howlong;
	int msdiff;
	int chip;
	int res_err_count;

	cgtime(&now);
	if (!(minioninfo->chip_chk.tv_sec)) {
		memcpy(&(minioninfo->chip_chk), &now, sizeof(now));
		memcpy(&(minioninfo->chip_rpt), &now, sizeof(now));
		return;
	}

	if (opt_minion_chipreport > 0) {
		msdiff = ms_tdiff(&now, &(minioninfo->chip_rpt));
		if (msdiff >= (opt_minion_chipreport * 1000)) {
			buf[0] = '\0';
			res_err_msg[0] = '\0';
			res_err_msg[1] = '\0';
			K_RLOCK(minioninfo->hfree_list);
			for (chip = 0; chip < MINION_CHIPS; chip++) {
				if (minioninfo->chip[chip]) {
					len = strlen(buf);
					if (minioninfo->hchip_list[chip]->count < 2)
						ghs = 0.0;
					else {
						ghs = 0xffffffffull * (minioninfo->hchip_list[chip]->count - 1);
						ghs /= 1000000000.0;
						ghs /= tdiff(&now, &(DATAH(minioninfo->hchip_list[chip]->tail)->when));
					}
					res_err_count = minioninfo->res_err_count[chip];
					minioninfo->res_err_count[chip] = 0;
					if (res_err_count > 100)
						res_err_msg[0] = '!';
					else if (res_err_count > 50)
						res_err_msg[0] = '*';
					else if (res_err_count > 0)
						res_err_msg[0] = '\'';
					else
						res_err_msg[0] = '\0';
					snprintf(buf + len, sizeof(buf) - len,
						 " %d=%s%.2f", chip, res_err_msg, ghs);
					minioninfo->history_ghs[chip] = ghs;
				}
			}
			K_RUNLOCK(minioninfo->hfree_list);
			memcpy(&(minioninfo->chip_chk), &now, sizeof(now));
			applogsiz(LOG_WARNING, 512,
				  "%s%d: Chip GHs%s",
				  minioncgpu->drv->name, minioncgpu->device_id, buf);
			memcpy(&(minioninfo->chip_rpt), &now, sizeof(now));
		}
	}

	msdiff = ms_tdiff(&now, &(minioninfo->chip_chk));
	if (total_secs >= MINION_HISTORY_s && msdiff >= (minioninfo->history_gen * 1000)) {
		K_RLOCK(minioninfo->hfree_list);
		for (chip = 0; chip < MINION_CHIPS; chip++) {
			if (minioninfo->chip[chip]) {
				if (minioninfo->hchip_list[chip]->count < 2)
					ghs = 0.0;
				else {
					ghs = 0xffffffffull * (minioninfo->hchip_list[chip]->count - 1);
					ghs /= 1000000000.0;
					ghs /= tdiff(&now, &(DATAH(minioninfo->hchip_list[chip]->tail)->when));
				}
				expect = (double)(minioninfo->init_freq[chip]) *
					 MINION_RESET_PERCENT / 1000.0;
				howlong = tdiff(&now, &(minioninfo->last_reset[chip]));
				if (ghs <= expect && howlong >= MINION_HISTORY_s)
					minioninfo->do_reset[chip] = expect;
				minioninfo->history_ghs[chip] = ghs;
			}
		}
		K_RUNLOCK(minioninfo->hfree_list);

		for (chip = 0; chip < MINION_CHIPS; chip++) {
			if (minioninfo->chip[chip]) {
				if (minioninfo->do_reset[chip] > 1.0) {
					applog(LOG_WARNING, "%s%d: Chip %d down to threshold %.2fGHs - resetting ...",
							    minioncgpu->drv->name, minioncgpu->device_id,
							    chip, minioninfo->do_reset[chip]);
					minioninfo->do_reset[chip] = 0.0;
					memcpy(&(minioninfo->last_reset[chip]), &now, sizeof(now));
					init_chip(minioncgpu, minioninfo, chip);
				}
			}
		}
		memcpy(&(minioninfo->chip_chk), &now, sizeof(now));
	}
}

static int64_t minion_scanwork(__maybe_unused struct thr_info *thr)
{
	struct cgpu_info *minioncgpu = thr->cgpu;
	struct minion_info *minioninfo = (struct minion_info *)(minioncgpu->device_data);
	int64_t hashcount = 0;

	minion_do_work(minioncgpu);

	mutex_lock(&(minioninfo->nonce_lock));
	if (minioninfo->new_nonces) {
		hashcount += 0xffffffffull * minioninfo->new_nonces;
		minioninfo->new_nonces = 0;
	}
	mutex_unlock(&(minioninfo->nonce_lock));

	if (opt_minion_idlecount)
		idle_report(minioncgpu);

	// Must always generate data to check/allow for chip reset
	chip_report(minioncgpu);

	/*
	 * To avoid wasting CPU, wait until we get an interrupt
	 * before returning back to the main cgminer work loop
	 * i.e. we then know we'll need more work
	 */
	cgsem_mswait(&(minioninfo->scan_work), MINION_SCAN_mS);

	return hashcount;
}

static const char *temp_str(uint16_t temp)
{
	switch (temp) {
		case MINION_TEMP_40:
			return min_temp_40;
		case MINION_TEMP_60:
			return min_temp_60;
		case MINION_TEMP_80:
			return min_temp_80;
		case MINION_TEMP_100:
			return min_temp_100;
		case MINION_TEMP_OVER:
			return min_temp_over;
	}
	return min_temp_invalid;
}

static void minion_get_statline_before(char *buf, size_t bufsiz, struct cgpu_info *minioncgpu)
{
	struct minion_info *minioninfo = (struct minion_info *)(minioncgpu->device_data);
	uint16_t max_temp, cores;
	int chip, core;

	max_temp = 0;
	cores = 0;
	mutex_lock(&(minioninfo->sta_lock));
	for (chip = 0; chip < MINION_CHIPS; chip++) {
		if (minioninfo->chip[chip]) {
			if (max_temp < minioninfo->chip_status[chip].temp)
				max_temp = minioninfo->chip_status[chip].temp;
			for (core = 0; core < MINION_CORES; core++) {
				if (minioninfo->chip_core_ena[core >> 5][chip] & (0x1 << (core % 32)))
					cores++;
			}
		}
	}
	mutex_unlock(&(minioninfo->sta_lock));

	tailsprintf(buf, bufsiz, "max%sC Ch:%d Co:%d",
				 temp_str(max_temp), minioninfo->chips, (int)cores);
}

#define CHIPS_PER_STAT 5

static struct api_data *minion_api_stats(struct cgpu_info *minioncgpu)
{
	struct minion_info *minioninfo = (struct minion_info *)(minioncgpu->device_data);
	struct api_data *root = NULL;
	char cores[MINION_CORES+1];
	char data[2048];
	char buf[32];
	int i, to, j;
	int chip, max_chip, que_work, chip_work, temp;

	if (minioninfo->initialised == false)
		return NULL;

	root = api_add_uint64(root, "OK Nonces", &(minioninfo->ok_nonces), true);
	root = api_add_uint64(root, "New Nonces", &(minioninfo->new_nonces), true);
	root = api_add_uint64(root, "Tested Nonces", &(minioninfo->tested_nonces), true);
	root = api_add_uint64(root, "Untested Nonces", &(minioninfo->untested_nonces), true);

	root = api_add_int(root, "Chips", &(minioninfo->chips), true);

	max_chip = 0;
	for (chip = 0; chip < MINION_CHIPS; chip++)
		if (minioninfo->chip[chip]) {
			max_chip = chip;

			snprintf(buf, sizeof(buf), "Chip %d Temperature", chip);
			root = api_add_const(root, buf, temp_str(minioninfo->chip_status[chip].temp), false);
			snprintf(buf, sizeof(buf), "Chip %d Cores", chip);
			root = api_add_uint16(root, buf, &(minioninfo->chip_status[chip].cores), true);
			snprintf(buf, sizeof(buf), "Chip %d Frequency", chip);
			root = api_add_uint32(root, buf, &(minioninfo->chip_status[chip].freq), true);
			snprintf(buf, sizeof(buf), "Chip %d InitFreq", chip);
			root = api_add_int(root, buf, &(minioninfo->init_freq[chip]), true);
			snprintf(buf, sizeof(buf), "Chip %d FreqSent", chip);
			root = api_add_hex32(root, buf, &(minioninfo->chip_status[chip].freqsent), true);
			snprintf(buf, sizeof(buf), "Chip %d InitTemp", chip);
			temp = minioninfo->init_temp[chip];
			if (temp == MINION_TEMP_CTL_DISABLE)
				root = api_add_string(root, buf, MINION_TEMP_DISABLE, true);
			else {
				snprintf(data, sizeof(data), "%d", temp);
				root = api_add_string(root, buf, data, true);
			}
			snprintf(buf, sizeof(buf), "Chip %d TempSent", chip);
			root = api_add_hex32(root, buf, &(minioninfo->chip_status[chip].tempsent), true);
			__bin2hex(data, (unsigned char *)(&(minioninfo->init_cores[chip][0])),
				  sizeof(minioninfo->init_cores[chip]));
			snprintf(buf, sizeof(buf), "Chip %d InitCores", chip);
			root = api_add_string(root, buf, data, true);
			snprintf(buf, sizeof(buf), "Chip %d IdleCount", chip);
			root = api_add_hex32(root, buf, &(minioninfo->chip_status[chip].idle), true);
			snprintf(buf, sizeof(buf), "Chip %d QueWork", chip);
			root = api_add_uint32(root, buf, &(minioninfo->chip_status[chip].quework), true);
			snprintf(buf, sizeof(buf), "Chip %d ChipWork", chip);
			root = api_add_uint32(root, buf, &(minioninfo->chip_status[chip].chipwork), true);
			snprintf(buf, sizeof(buf), "Chip %d RealWork", chip);
			root = api_add_uint32(root, buf, &(minioninfo->chip_status[chip].realwork), true);
			snprintf(buf, sizeof(buf), "Chip %d QueListCount", chip);
			root = api_add_int(root, buf, &(minioninfo->wque_list[chip]->count), true);
			snprintf(buf, sizeof(buf), "Chip %d WorkListCount", chip);
			root = api_add_int(root, buf, &(minioninfo->wchip_list[chip]->count), true);
			snprintf(buf, sizeof(buf), "Chip %d Overheat", chip);
			root = api_add_bool(root, buf, &(minioninfo->chip_status[chip].overheat), true);
			snprintf(buf, sizeof(buf), "Chip %d Overheats", chip);
			root = api_add_uint32(root, buf, &(minioninfo->chip_status[chip].overheats), true);
			snprintf(buf, sizeof(buf), "Chip %d LastOverheat", chip);
			root = api_add_timeval(root, buf, &(minioninfo->chip_status[chip].lastoverheat), true);
			snprintf(buf, sizeof(buf), "Chip %d LastRecover", chip);
			root = api_add_timeval(root, buf, &(minioninfo->chip_status[chip].lastrecover), true);
			snprintf(buf, sizeof(buf), "Chip %d OverheatIdle", chip);
			root = api_add_double(root, buf, &(minioninfo->chip_status[chip].overheattime), true);
			for (i = 0; i < MINION_CORES; i++) {
				if (minioninfo->chip_core_ena[i >> 5][chip] & (0x1 << (i % 32)))
					cores[i] = 'o';
				else
					cores[i] = 'x';
			}
			cores[MINION_CORES] = '\0';
			snprintf(buf, sizeof(buf), "Chip %d CoresEna", chip);
			root = api_add_string(root, buf, cores, true);
			for (i = 0; i < MINION_CORES; i++) {
				if (minioninfo->chip_core_act[i >> 5][chip] & (0x1 << (i % 32)))
					cores[i] = '-';
				else
					cores[i] = 'o';
			}
			cores[MINION_CORES] = '\0';
			snprintf(buf, sizeof(buf), "Chip %d CoresAct", chip);
			root = api_add_string(root, buf, cores, true);
			snprintf(buf, sizeof(buf), "Chip %d History GHs", chip);
			root = api_add_mhs(root, buf, &(minioninfo->history_ghs[chip]), true);
		}

	double his = MINION_HISTORY_s;
	root = api_add_double(root, "History length", &his, true);

	for (i = 0; i <= max_chip; i += CHIPS_PER_STAT) {
		to = i + CHIPS_PER_STAT - 1;
		if (to > max_chip)
			to = max_chip;

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
					minioninfo->chip_nononces[j]);
			strcat(data, buf);
		}
		snprintf(buf, sizeof(buf), "NoNonces %02d - %02d", i, to);
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

		data[0] = '\0';
		for (j = i; j <= to; j++) {
			snprintf(buf, sizeof(buf),
					"%s%8"PRIu64,
					j == i ? "" : " ",
					minioninfo->chip_err[j]);
			strcat(data, buf);
		}
		snprintf(buf, sizeof(buf), "Err %02d - %02d", i, to);
		root = api_add_string(root, buf, data, true);

		data[0] = '\0';
		for (j = i; j <= to; j++) {
			snprintf(buf, sizeof(buf),
					"%s%8"PRIu64,
					j == i ? "" : " ",
					minioninfo->fifo_spi_errors[j]);
			strcat(data, buf);
		}
		snprintf(buf, sizeof(buf), "FifoSpiErr %02d - %02d", i, to);
		root = api_add_string(root, buf, data, true);

		data[0] = '\0';
		for (j = i; j <= to; j++) {
			snprintf(buf, sizeof(buf),
					"%s%8"PRIu64,
					j == i ? "" : " ",
					minioninfo->res_spi_errors[j]);
			strcat(data, buf);
		}
		snprintf(buf, sizeof(buf), "ResSpiErr %02d - %02d", i, to);
		root = api_add_string(root, buf, data, true);

		data[0] = '\0';
		for (j = i; j <= to; j++) {
			snprintf(buf, sizeof(buf),
					"%s%"PRIu64"/%"PRIu64"/%"PRIu64"/%"PRIu64"/%"PRIu64,
					j == i ? "" : " ",
					minioninfo->use_res2[j],
					minioninfo->tasks_failed[j],
					minioninfo->tasks_recovered[j],
					minioninfo->nonces_failed[j],
					minioninfo->nonces_recovered[j]);
			strcat(data, buf);
		}
		snprintf(buf, sizeof(buf), "Redo %02d - %02d", i, to);
		root = api_add_string(root, buf, data, true);
	}

	que_work = chip_work = 0;
	for (chip = 0; chip <= max_chip; chip++)
		if (minioninfo->chip[chip]) {
			que_work += minioninfo->wchip_list[chip]->count;
			chip_work += minioninfo->wchip_list[chip]->count;
		}

	root = api_add_int(root, "WFree Total", &(minioninfo->wfree_list->total), true);
	root = api_add_int(root, "WFree Count", &(minioninfo->wfree_list->count), true);
	root = api_add_int(root, "WWork Count", &(minioninfo->wwork_list->count), true);
	root = api_add_int(root, "WQue Count", &que_work, true);
	root = api_add_int(root, "WChip Count", &chip_work, true);

	root = api_add_int(root, "TFree Total", &(minioninfo->tfree_list->total), true);
	root = api_add_int(root, "TFree Count", &(minioninfo->tfree_list->count), true);
	root = api_add_int(root, "Task Count", &(minioninfo->task_list->count), true);
	root = api_add_int(root, "Reply Count", &(minioninfo->treply_list->count), true);

	root = api_add_int(root, "RFree Total", &(minioninfo->rfree_list->total), true);
	root = api_add_int(root, "RFree Count", &(minioninfo->rfree_list->count), true);
	root = api_add_int(root, "RNonce Count", &(minioninfo->rnonce_list->count), true);

#if DO_IO_STATS
#define sta_api(_name, _iostat) \
	do { \
		if ((_iostat).count) { \
			float _davg = (float)((_iostat).total_delay) / (float)((_iostat).count); \
			float _dlavg = (float)((_iostat).total_dlock) / (float)((_iostat).count); \
			float _dlwavg = (float)((_iostat).total_dlwait) / (float)((_iostat).count); \
			float _bavg = (float)((_iostat).total_bytes) / (float)((_iostat).count); \
			float _tavg = (float)((_iostat).tsd) / (float)((_iostat).count); \
			snprintf(data, sizeof(data), "%s Count=%"PRIu64 \
				" Delay=%.0fus DAvg=%.3f" \
				" DMin=%.0f DMax=%.0f DZ=%"PRIu64 \
				" DLock=%.0fus DLAvg=%.3f" \
				" DLMin=%.0f DLMax=%.0f DZ=%"PRIu64 \
				" DLWait=%.0fus DLWAvg=%.3f" \
				" Bytes=%"PRIu64" BAvg=%.3f" \
				" BMin=%"PRIu64" BMax=%"PRIu64" BZ=%"PRIu64 \
				" TSD=%.0fus TAvg=%.03f", \
				_name, (_iostat).count, \
				(_iostat).total_delay, _davg, (_iostat).min_delay, \
				(_iostat).max_delay, (_iostat).zero_delay, \
				(_iostat).total_dlock, _dlavg, (_iostat).min_dlock, \
				(_iostat).max_dlock, (_iostat).zero_dlock, \
				(_iostat).total_dlwait, _dlwavg, \
				(_iostat).total_bytes, _bavg, (_iostat).min_bytes, \
				(_iostat).max_bytes, (_iostat).zero_bytes, \
				(_iostat).tsd, _tavg); \
			root = api_add_string(root, buf, data, true); \
		} \
	} while(0);

	for (i = 0; i < 0x200; i++) {
		snprintf(buf, sizeof(buf), "Stat-0x%02x", i);
		sta_api(addr2txt((uint8_t)(i & 0xff)), minioninfo->iostats[i]);
	}

	// Test to avoid showing applog
	if (minioninfo->summary.count) {
		snprintf(buf, sizeof(buf), "Stat-S");
		sta_api("Summary", minioninfo->summary);
		applog(LOG_WARNING, "%s %d: (%.0f) %s - %s",
				    minioncgpu->drv->name, minioncgpu->device_id,
				    total_secs, buf, data);
	}
#endif

	root = api_add_uint64(root, "Total SPI Errors", &(minioninfo->spi_errors), true);
	root = api_add_uint64(root, "Work Unrolled", &(minioninfo->work_unrolled), true);
	root = api_add_uint64(root, "Work Rolled", &(minioninfo->work_rolled), true);
	root = api_add_uint64(root, "Ints", &(minioninfo->interrupts), true);
	root = api_add_uint64(root, "Res Ints", &(minioninfo->result_interrupts), true);
	root = api_add_uint64(root, "Cmd Ints", &(minioninfo->command_interrupts), true);
	root = api_add_string(root, "Last Int", minioninfo->last_interrupt, true);
	root = api_add_hex32(root, "Next TaskID", &(minioninfo->next_task_id), true);

	root = api_add_elapsed(root, "Elapsed", &(total_secs), true);

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
