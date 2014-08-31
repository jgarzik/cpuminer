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
#include <sys/mman.h>
#include <fcntl.h>
#include <poll.h>

// Define this to 1 to enable interrupt code and enable no_nonce
#define ENABLE_INT_NONO 0

// Define this to 1 if compiling on RockChip and not on RPi
#define MINION_ROCKCHIP 0

// The code is always in - this just decides if it does it
static bool minreread = false;

#if MINION_ROCKCHIP == 1
#define MINION_POWERCYCLE_GPIO 173
#define MINION_CHIP_OFF "1"
#define MINION_CHIP_ON "0"
#define MINION_CHIP_DELAY 100
#endif

// Power cycle if the xff_list is full and the tail is less than
// this long ago
#define MINION_POWER_TIME 60

/*
 * Use pins for board selection
 * If disabled, it will test chips just as 'pin 0'
 * but never do any gpio - the equivalent of the previous 'no pins' code
 */
static bool usepins = false;

#define MINION_PAGE_SIZE 4096

#define BCM2835_BASE 0x20000000
#define BCM2835_GPIO_BASE (BCM2835_BASE + 0x200000)

#define BCM2835_GPIO_SET0 0x001c // GPIO Pin Output Set 0
#define BCM2835_GPIO_CLR0 0x0028 // GPIO Pin Output Clear 0

#define BCM2835_GPIO_FSEL0 0x0000

#define BCM2835_GPIO_FSEL_INPUT  0b000
#define BCM2835_GPIO_FSEL_OUTPUT 0b001
#define BCM2835_GPIO_FSEL_MASK   0b111

#define BCM2835_PIN_HIGH 0x1
#define BCM2835_PIN_LOW 0x0

static const char *minion_memory = "/dev/mem";
static int minion_memory_addr = BCM2835_GPIO_BASE;

#define MINION_SPI_BUS 0
#define MINION_SPI_CHIP 0

#if MINION_ROCKCHIP == 0
#define MINION_SPI_SPEED 8000000
#else
#define MINION_SPI_SPEED 500000
#endif
#define MINION_SPI_BUFSIZ 1024

static struct minion_select_pins {
	int pin;
	int wpi;
	char *name;
	int bcm; // this is what we use
} minionPins[] = {
	{	24,	10,	"CE0",		8,	},
	{	26,	11,	"CE1",		7,	},
	{	16,	4,	"GPIO4",	23,	},
	{	22,	6,	"GPIO6",	25,	},
	{	12,	1,	"GPIO1",	18,	},
	{	18,	5,	"GPIO5",	24,	},
	{	11,	0,	"GPIO0",	17,	},
	{	13,	2,	"GPIO2",	27,	},
	{	15,	3,	"GPIO3",	22,	},
	{	7,	7,	"GPIO7",	4,	}

/* The rest on the RPi
	{	3,	8,	"SDA",		2,	}
	{	5,	9,	"SCL",		3,	}
	{	19,	12,	"MOSI",		10,	}
	{	21,	13,	"MISO",		9,	}
	{	23,	14,	"SCLK",		11,	}
	{	8,	15,	"TxD",		14,	}
	{	10,	16,	"RxD",		15,	}
*/
};

/*
 * uS delays for GPIO pin access
 */
#define MINION_PIN_BEFORE cgsleep_us(33)
#define MINION_PIN_SLEEP cgsleep_us(133)
#define MINION_PIN_AFTER

#define MINION_PIN_COUNT (sizeof(minionPins)/ \
			  sizeof(struct minion_select_pins))

#define CHIP_PIN(_chip) (minioninfo->chip_pin[_chip])

#define MINION_MIN_CHIP 0
#define MINION_MAX_CHIP 11

#define MINION_CHIP_PER_PIN (1 + MINION_MAX_CHIP - MINION_MIN_CHIP)

#define MINION_CHIPS (MINION_PIN_COUNT * MINION_CHIP_PER_PIN)
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
#define MINION_SYS_SPI_LED 0x02
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

// Block change
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
	uint8_t chipid;
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

#define MINION_SPI_LED_ON 0xa5a5
#define MINION_SPI_LED_OFF 0x0

// Time since first nonce/last reset before turning on the LED
#define MINION_LED_TEST_TIME 600

#define MINION_FREQ_MIN 100
#define MINION_FREQ_DEF 1200
#define MINION_FREQ_MAX 1400
#define MINION_FREQ_FACTOR 100
#define MINION_FREQ_RESET_STEP MINION_FREQ_FACTOR
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

// When hash rate falls below this in the history hash rate, reset it
#define MINION_RESET_PERCENT 75.0
// When hash rate falls below this after the longer test time
#define MINION_RESET2_PERCENT 85.0

// After the above resets, delay sending work for:
#define MINION_RESET_DELAY_s 0.088

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
	uint32_t realwork; // FIFO_STA
	struct timeval last;
	bool overheat;
	bool islow;
	bool tohigh;
	int lowcount;
	uint32_t overheats;
	struct timeval lastoverheat;
	struct timeval lastrecover;
	double overheattime;
	uint32_t tempsent;
	uint32_t idle;
	uint32_t last_rpt_idle;
	struct timeval idle_rpt;
	struct timeval first_nonce;
	uint64_t from_first_good;
};

#define ENABLE_CORE(_core, _n) ((_core[_n >> 3]) |= (1 << (_n % 8)))
#define CORE_IDLE(_core, _n) ((_core[_n >> 3]) & (1 << (_n % 8)))

#define FIFO_RES(_fifo, _off) ((_fifo)[(_off) + 0])
#define FIFO_CMD(_fifo, _off) ((_fifo)[(_off) + 1])

#define RES_GOLD(_res) ((((_res)->status[3]) & 0x80) == 0)
#define RES_CHIPID(_res) (((_res)->status[3]) & 0x1f)
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

// *** Work lists: generated, queued for a chip, sent to chip
typedef struct work_item {
	struct work *work;
	uint32_t task_id;
	struct timeval sent;
	int nonces;
	bool urgent;
	bool stale; // if stale, don't decrement que/chipwork when discarded
	bool rolled;
	int errors; // uncertain since the error could mean task_id is wrong
	struct timeval created; // when work was generated
	uint64_t ioseq;
} WORK_ITEM;

#define ALLOC_WORK_ITEMS 4096
#define LIMIT_WORK_ITEMS 0

// *** Task queue ready to be sent
typedef struct task_item {
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
	uint64_t ioseq;
} TASK_ITEM;

#define ALLOC_TASK_ITEMS 256
#define LIMIT_TASK_ITEMS 0

// *** Results queue ready to be checked
typedef struct res_item {
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
} RES_ITEM;

#define ALLOC_RES_ITEMS 256
#define LIMIT_RES_ITEMS 0

// *** Per chip nonce history
typedef struct hist_item {
	struct timeval when;
} HIST_ITEM;

#define ALLOC_HIST_ITEMS 4096
#define LIMIT_HIST_ITEMS 0

// How much history to keep (5min)
#define MINION_HISTORY_s 300
// History required to decide a reset at MINION_FREQ_DEF Mhz
#define MINION_RESET_s 10
// How many times to reset before changing Freq
// This doesn't include the secondary higher % check
#define MINION_RESET_COUNT 6

// To enable the 2nd check
static bool second_check = true;
// Longer time lapse to expect the higher %
// This intercepts a slow GHs drop earlier
#define MINION_RESET2_s 60

#if (MINION_RESET_s > MINION_HISTORY_s)
#error "MINION_RESET_s can't be greater than MINION_HISTORY_s"
#endif

#define FREQ_DELAY(freq) ((float)(MINION_RESET_s * MINION_FREQ_DEF) / (freq))

#if (MINION_RESET2_s > MINION_HISTORY_s)
#error "MINION_RESET2_s can't be greater than MINION_HISTORY_s"
#endif

// FREQ2_DELAY(MINION_FREQ_MIN) = FREQ2_FACTOR * MINION_RESET2_s
#define FREQ2_FACTOR 1.5

#define FREQ2_DELAY(freq) ((1.0 + (float)((freq - MINION_FREQ_DEF) * (1 - FREQ2_FACTOR)) / \
				(float)(MINION_FREQ_DEF - MINION_FREQ_MIN)) * MINION_RESET2_s)

#if (MINION_RESET2_s <= MINION_RESET_s)
#error "MINION_RESET2_s must be greater than MINION_RESET_s"
#endif

/* If there was no reset for this long, clear the reset history
 * (except the last one) since this means the current clock is ok
 * with rare resets */
#define MINION_CLR_s 300

#if (MINION_CLR_s <= MINION_RESET2_s)
#error "MINION_CLR_s must be greater than MINION_RESET2_s"
#endif

// History must be always generated for the reset check
#define MINION_MAX_RESET_CHECK 2

/* Floating point reset settings required for the code to work properly
 * Basically: RESET2 must be after RESET and CLR must be after RESET2 */
static void define_test()
{
	float test;

	if (MINION_RESET2_PERCENT <= MINION_RESET_PERCENT) {
		quithere(1, "MINION_RESET2_PERCENT=%f must be "
			    "> MINION_RESET_PERCENT=%f",
			    MINION_RESET2_PERCENT, MINION_RESET_PERCENT);
	}

	test = FREQ_DELAY(MINION_FREQ_MIN);
	if (test >= MINION_HISTORY_s) {
		quithere(1, "FREQ_DELAY(MINION_FREQ_MIN)=%f must be "
			    "< MINION_HISTORY_s=%d",
			    test, MINION_HISTORY_s);
	}

	if (MINION_CLR_s <= test) {
		quithere(1, "MINION_CLR_s=%d must be > "
			    "FREQ_DELAY(MINION_FREQ_MIN)=%f",
			    MINION_CLR_s, test);
	}

	if (FREQ2_FACTOR <= 1.0)
		quithere(1, "FREQ2_FACTOR=%f must be > 1.0", FREQ2_FACTOR);


	test = FREQ2_DELAY(MINION_FREQ_MIN);
	if (test >= MINION_HISTORY_s) {
		quithere(1, "FREQ2_DELAY(MINION_FREQ_MIN)=%f must be "
			    "< MINION_HISTORY_s=%d",
			    test, MINION_HISTORY_s);
	}

	if (MINION_CLR_s <= test) {
		quithere(1, "MINION_CLR_s=%d must be > "
			    "FREQ2_DELAY(MINION_FREQ_MIN)=%f",
			    MINION_CLR_s, test);
	}
}

// *** Chip freq/MHs performance history
typedef struct perf_item {
	double elapsed;
	uint64_t nonces;
	uint32_t freq;
	double ghs;
	struct timeval when;
} PERF_ITEM;

#define ALLOC_PERF_ITEMS 128
#define LIMIT_PERF_ITEMS 0

// *** 0xff error history
typedef struct xff_item {
	time_t when;
} XFF_ITEM;

#define ALLOC_XFF_ITEMS 100
#define LIMIT_XFF_ITEMS 100

#define DATA_WORK(_item) ((WORK_ITEM *)(_item->data))
#define DATA_TASK(_item) ((TASK_ITEM *)(_item->data))
#define DATA_RES(_item) ((RES_ITEM *)(_item->data))
#define DATA_HIST(_item) ((HIST_ITEM *)(_item->data))
#define DATA_PERF(_item) ((PERF_ITEM *)(_item->data))
#define DATA_XFF(_item) ((XFF_ITEM *)(_item->data))

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

static double time_bands[] = { 0.1, 0.5, 1.0, 2.0, 4.0, 8.0, 16.0, 32.0 };
#define TIME_BANDS ((int)(sizeof(time_bands)/sizeof(double)))

struct minion_info {
	struct thr_info *thr;
	struct thr_info spiw_thr;
	struct thr_info spir_thr;
	struct thr_info res_thr;

	pthread_mutex_t spi_lock;
	pthread_mutex_t sta_lock;

	cgsem_t task_ready;
	cgsem_t nonce_ready;
	cgsem_t scan_work;

	volatile unsigned *gpio;

	int spifd;
	char gpiointvalue[64];
	int gpiointfd;

	// I/O or seconds
	bool spi_reset_io;
	int spi_reset_count;
	time_t last_spi_reset;
	uint64_t spi_resets;

	// TODO: need to track disabled chips - done?
	int chips;
	bool has_chip[MINION_CHIPS];
	int init_temp[MINION_CHIPS];
	uint8_t init_cores[MINION_CHIPS][DATA_SIZ*MINION_CORE_REPS];

	uint8_t chipid[MINION_CHIPS]; // Chip Number
	int chip_pin[MINION_CHIPS];

	uint64_t ioseq;
	uint32_t next_task_id;

	// Stats
	uint64_t chip_nonces[MINION_CHIPS];
	uint64_t chip_nononces[MINION_CHIPS];
	uint64_t chip_good[MINION_CHIPS];
	uint64_t chip_bad[MINION_CHIPS];
	uint64_t chip_err[MINION_CHIPS];
	uint64_t chip_dup[MINION_CHIPS];
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
	bool flag_reset[MINION_CHIPS];

	// Work items
	K_LIST *wfree_list;
	K_STORE *wwork_list;
	K_STORE *wstale_list;
	K_STORE *wque_list[MINION_CHIPS];
	K_STORE *wchip_list[MINION_CHIPS];
	uint64_t wwork_flushed;
	uint64_t wque_flushed;
	uint64_t wchip_staled;

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
	// Point in history for MINION_RESET_s
	int reset_time[MINION_CHIPS];
	K_ITEM *reset_mark[MINION_CHIPS];
	int reset_count[MINION_CHIPS];
	// Point in history for MINION_RESET2_s
	int reset2_time[MINION_CHIPS];
	K_ITEM *reset2_mark[MINION_CHIPS];
	int reset2_count[MINION_CHIPS];

	// Performance history
	K_LIST *pfree_list;
	K_STORE *p_list[MINION_CHIPS];

	// 0xff history
	K_LIST *xfree_list;
	K_STORE *xff_list;
	time_t last_power_cycle;
	uint64_t power_cycles;
	time_t last_xff;
	uint64_t xffs;
	uint64_t last_displayed_xff;

	// Gets reset to zero each time it is used in reporting
	int res_err_count[MINION_CHIPS];

#if DO_IO_STATS
	// Total
	IOSTAT summary;

	// Two for each command plus wasted extras i.e. direct/fast lookup
	// No error uses 0x0 to 0xff, error uses 0x100 to 0x1ff
	IOSTAT iostats[0x200];
#endif

	// Stats on how long work is waiting to move from wwork_list to wque_list
	uint64_t que_work;
	double que_time;
	double que_min;
	double que_max;
	uint64_t que_bands[TIME_BANDS+1];

	// From wwork_list to txrx
	uint64_t wt_work;
	double wt_time;
	double wt_min;
	double wt_max;
	uint64_t wt_bands[TIME_BANDS+1];

	bool lednow[MINION_CHIPS];
	bool setled[MINION_CHIPS];

	// When changing the frequency don't modify 'anything'
	bool changing[MINION_CHIPS];
	int init_freq[MINION_CHIPS];
	int want_freq[MINION_CHIPS];
	uint32_t freqsent[MINION_CHIPS];
	struct timeval lastfreq[MINION_CHIPS];
	int freqms[MINION_CHIPS];

	bool initialised;
};

#if MINION_ROCKCHIP == 1
static bool minion_toggle_gpio(struct cgpu_info *minioncgpu, int gpionum)
{
	struct minion_info *minioninfo = (struct minion_info *)(minioncgpu->device_data);
	char pindir[64], ena[64], pin[8], dir[64];
	char gpiointvalue[64];
	struct stat st;
	int file, err, chip;
	ssize_t ret;

	snprintf(pindir, sizeof(pindir), MINION_GPIO_SYS MINION_GPIO_PIN, gpionum);
	memset(&st, 0, sizeof(st));

	if (stat(pindir, &st) == 0) { // already exists
		if (!S_ISDIR(st.st_mode)) {
			applog(LOG_ERR, "%s: failed1 to enable GPIO pin %d"
					" - not a directory",
					minioncgpu->drv->dname, gpionum);
			return false;
		}
	} else {
		snprintf(ena, sizeof(ena), MINION_GPIO_SYS MINION_GPIO_ENA);
		file = open(ena, O_WRONLY | O_SYNC);
		if (file == -1) {
			applog(LOG_ERR, "%s: failed2 to export GPIO pin %d (%d)"
					" - you need to be root?",
					minioncgpu->drv->dname,
					gpionum, errno);
			return false;
		}
		snprintf(pin, sizeof(pin), MINION_GPIO_ENA_VAL, gpionum);
		ret = write(file, pin, (size_t)strlen(pin));
		if (ret != (ssize_t)strlen(pin)) {
			if (ret < 0)
				err = errno;
			else
				err = (int)ret;
			close(file);
			applog(LOG_ERR, "%s: failed3 to export GPIO pin %d (%d:%d)",
					minioncgpu->drv->dname,
					gpionum, err, (int)strlen(pin));
			return false;
		}
		close(file);

		// Check again if it exists
		memset(&st, 0, sizeof(st));
		if (stat(pindir, &st) != 0) {
			applog(LOG_ERR, "%s: failed4 to export GPIO pin %d (%d)",
					minioncgpu->drv->dname,
					gpionum, errno);
			return false;
		}
	}

	// Set the pin attributes
	// Direction
	snprintf(dir, sizeof(dir), MINION_GPIO_SYS MINION_GPIO_PIN MINION_GPIO_DIR, gpionum);
	file = open(dir, O_WRONLY | O_SYNC);
	if (file == -1) {
		applog(LOG_ERR, "%s: failed5 to configure GPIO pin %d (%d)"
				" - you need to be root?",
				minioncgpu->drv->dname,
				gpionum, errno);
		return false;
	}
	ret = write(file, MINION_GPIO_DIR_WRITE, sizeof(MINION_GPIO_DIR_WRITE)-1);
	if (ret != sizeof(MINION_GPIO_DIR_WRITE)-1) {
		if (ret < 0)
			err = errno;
		else
			err = (int)ret;
		close(file);
		applog(LOG_ERR, "%s: failed6 to configure GPIO pin %d (%d:%d)",
				minioncgpu->drv->dname, gpionum,
				err, (int)sizeof(MINION_GPIO_DIR_WRITE)-1);
		return false;
	}
	close(file);

	// Open it
	snprintf(gpiointvalue, sizeof(gpiointvalue),
		 MINION_GPIO_SYS MINION_GPIO_PIN MINION_GPIO_VALUE,
		 gpionum);
	int fd = open(gpiointvalue, O_WRONLY);
	if (fd == -1) {
		applog(LOG_ERR, "%s: failed7 to access GPIO pin %d (%d)",
				minioncgpu->drv->dname,
				gpionum, errno);
		return false;
	}

	ret = write(fd, MINION_CHIP_OFF, sizeof(MINION_CHIP_OFF)-1);
	if (ret != sizeof(MINION_CHIP_OFF)-1) {
		close(fd);
		applog(LOG_ERR, "%s: failed8 to toggle off GPIO pin %d (%d:%d)",
				minioncgpu->drv->dname,
				gpionum, (int)ret, errno);
		return false;
	}

	cgsleep_ms(MINION_CHIP_DELAY);

	ret = write(fd, MINION_CHIP_ON, sizeof(MINION_CHIP_ON)-1);
	if (ret != sizeof(MINION_CHIP_OFF)-1) {
		close(fd);
		applog(LOG_ERR, "%s: failed9 to toggle on GPIO pin %d (%d:%d)",
				minioncgpu->drv->dname,
				gpionum, (int)ret, errno);
		return false;
	}

	close(fd);
	minioninfo->last_power_cycle = time(NULL);
	minioninfo->power_cycles++;
	// Reset all chip led counters
	for (chip = 0; chip < (int)MINION_CHIPS; chip++) {
		if (minioninfo->has_chip[chip])
			minioninfo->chip_status[chip].first_nonce.tv_sec = 0L;
	}
	return true;
}
#endif

static void ready_work(struct cgpu_info *minioncgpu, struct work *work, bool rolled)
{
	struct minion_info *minioninfo = (struct minion_info *)(minioncgpu->device_data);
	K_ITEM *item = NULL;

	K_WLOCK(minioninfo->wfree_list);

	item = k_unlink_head(minioninfo->wfree_list);

	DATA_WORK(item)->work = work;
	DATA_WORK(item)->task_id = 0;
	memset(&(DATA_WORK(item)->sent), 0, sizeof(DATA_WORK(item)->sent));
	DATA_WORK(item)->nonces = 0;
	DATA_WORK(item)->urgent = false;
	DATA_WORK(item)->rolled = rolled;
	DATA_WORK(item)->errors = 0;
	cgtime(&(DATA_WORK(item)->created));

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
		*chip = DATA_RES(item)->chip;
		*core = DATA_RES(item)->core;
		*task_id = DATA_RES(item)->task_id;
		*nonce = DATA_RES(item)->nonce;
		*no_nonce = DATA_RES(item)->no_nonce;
		memcpy(when, &(DATA_RES(item)->when), sizeof(*when));
		*another = DATA_RES(item)->another;
		*task_id2 = DATA_RES(item)->task_id2;
		*nonce2 = DATA_RES(item)->nonce2;

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
		case WRITE_ADDR(MINION_SYS_SPI_LED):
			return "WLed";
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
		case WRITE_ADDR(MINION_SYS_SPI_LED):
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
		applog(IOCTRL_LOG, "%s %s chipid %d osiz %d%s%s",
				   name, dir, (int)obuf[0], (int)osiz, ex, buf);
		applog(IOCTRL_LOG, "  reply was error %d", reply);
	} else {
		if (IS_ADDR_WRITE(obuf[1])) {
			applog(IOCTRL_LOG, "%s %s chipid %d osiz %d%s%s",
					   name, dir, (int)obuf[0], (int)osiz, ex, buf);
			applog(IOCTRL_LOG, "  write ret was %d", reply);
		} else {
			switch (obuf[1]) {
				case READ_ADDR(MINION_RES_DATA):
					rescount = (int)((float)rsiz / (float)MINION_RES_DATA_SIZ);
					applog(IOCTRL_LOG, "%s %s chipid %d osiz %d%s%s",
							   name, dir, (int)obuf[0], (int)osiz, ex, buf);
					for (i = 0; i < rescount; i++) {
						res = (struct minion_result *)(rbuf + osiz - rsiz + (i * MINION_RES_DATA_SIZ));
						if (!IS_RESULT(res)) {
							applog(IOCTRL_LOG, "  %s reply %d of %d - none", name, i+1, rescount);
						} else {
							__bin2hex(buf, res->nonce, DATA_SIZ);
							applog(IOCTRL_LOG, "  %s reply %d of %d %d(%d) was task 0x%04x"
										   " chipid %d core %d gold %s nonce 0x%s",
									   name, i+1, rescount, reply, rsiz,
									   RES_TASK(res),
									   (int)RES_CHIPID(res),
									   (int)RES_CORE(res),
									   (int)RES_GOLD(res) ? "Y" : "N",
									   buf);
						}
					}
					break;
				case READ_ADDR(MINION_SYS_CHIP_SIG):
				case READ_ADDR(MINION_SYS_CHIP_STA):
				default:
					applog(IOCTRL_LOG, "%s %s chipid %d osiz %d%s%s",
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

static void set_pin(struct minion_info *minioninfo, int pin, bool on)
{
	volatile uint32_t *paddr;
	uint32_t value;
	int bcm;

	bcm = minionPins[pin].bcm;

	paddr = minioninfo->gpio + ((on ? BCM2835_GPIO_SET0 : BCM2835_GPIO_CLR0) / 4) + (bcm / 10);

	value = 1 << (bcm % 32);

	*paddr = value;
	*paddr = value;
}

static void init_pins(struct minion_info *minioninfo)
{
	int pin;

	// Initialise all pins high as required
	MINION_PIN_BEFORE;
	for (pin = 0; pin < (int)MINION_PIN_COUNT; pin++) {
		set_pin(minioninfo, pin, true);
		MINION_PIN_SLEEP;
	}
}

#define EXTRA_LOG_IO 0

static bool minion_init_spi(struct cgpu_info *minioncgpu, struct minion_info *minioninfo, int bus, int chip, bool reset);

static int __do_ioctl(struct cgpu_info *minioncgpu, struct minion_info *minioninfo,
		      int pin, uint8_t *obuf, uint32_t osiz, uint8_t *rbuf,
		      uint32_t rsiz, uint64_t *ioseq, MINION_FFL_ARGS)
{
	struct spi_ioc_transfer tran;
	bool fail = false, powercycle = false, show = false;
	double lastshow, total;
	K_ITEM *xitem;
	time_t now;
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
	applog(IOCTRL_LOG, "*** %s() pin %d cid %d sending %02x %02x %s %02x %02x",
			   __func__, pin, (int)(dataw[DATA_OFF]),
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

	tran.delay_usecs = opt_minion_spiusec;
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
	if (usepins) {
		// Pin low for I/O
		MINION_PIN_BEFORE;
		set_pin(minioninfo, pin, false);
		MINION_PIN_SLEEP;
	}
	IO_STAT_NOW(&sta);
	ret = ioctl(minioninfo->spifd, SPI_IOC_MESSAGE(1), (void *)&tran);
	*ioseq = minioninfo->ioseq++;
	IO_STAT_NOW(&fin);
	if (usepins) {
		MINION_PIN_AFTER;
		// Pin back high after I/O
		set_pin(minioninfo, pin, true);
	}
	now = time(NULL);
	if (ret >= 0 && rbuf[0] == 0xff && rbuf[ret-1] == 0xff &&
	    (obuf[1] == READ_ADDR(MINION_RES_DATA) || obuf[1] == READ_ADDR(MINION_SYS_FIFO_STA))) {
		int i;
		fail = true;
		for (i = 1; i < ret-2; i++) {
			if (rbuf[i] != 0xff) {
				fail = false;
				break;
			}
		}
		if (fail) {
			powercycle = show = false;
			minioninfo->xffs++;
			minioninfo->last_xff = now;

			if (minioninfo->xfree_list->count > 0)
				xitem = k_unlink_head(minioninfo->xfree_list);
			else
				xitem = k_unlink_tail(minioninfo->xff_list);
			DATA_XFF(xitem)->when = now;
			if (!minioninfo->xff_list->head)
				show = true;
			else {
				// if !changing and xff_list is full
				if (!minioninfo->changing[obuf[0]] &&
				    minioninfo->xfree_list->count == 0) {
					total = DATA_XFF(xitem)->when -
						DATA_XFF(minioninfo->xff_list->tail)->when;
					if (total <= MINION_POWER_TIME) {
						powercycle = true;
						// Discard the history
						k_list_transfer_to_head(minioninfo->xff_list,
									minioninfo->xfree_list);
						k_add_head(minioninfo->xfree_list, xitem);
						xitem = NULL;
					}
				}

				if (!powercycle) {
					lastshow = DATA_XFF(xitem)->when -
						   DATA_XFF(minioninfo->xff_list->head)->when;
					show = (lastshow >= 5);
				}
			}
			if (xitem)
				k_add_head(minioninfo->xff_list, xitem);

#if MINION_ROCKCHIP == 1
			if (powercycle)
				minion_toggle_gpio(minioncgpu, MINION_POWERCYCLE_GPIO);
#endif
			minion_init_spi(minioncgpu, minioninfo, 0, 0, true);
		}
	} else if (minioninfo->spi_reset_count) {
		if (minioninfo->spi_reset_io) {
			if (*ioseq > 0 && (*ioseq % minioninfo->spi_reset_count) == 0)
				minion_init_spi(minioncgpu, minioninfo, 0, 0, true);
		} else {
			if (minioninfo->last_spi_reset == 0)
				minioninfo->last_spi_reset = now;
			else {
				if ((now - minioninfo->last_spi_reset) >= minioninfo->spi_reset_count)
					minion_init_spi(minioncgpu, minioninfo, 0, 0, true);
					minioninfo->last_spi_reset = now;
			}
		}
	}
	if (opt_minion_spidelay)
		cgsleep_ms(opt_minion_spidelay);
	mutex_unlock(&(minioninfo->spi_lock));
	IO_STAT_NOW(&lfin);
	IO_STAT_NOW(&tsd);

	IO_STAT_STORE(&sta, &fin, &lsta, &lfin, &tsd, obuf, osiz, ret, 1);

	if (fail) {
		if (powercycle) {
			applog(LOG_ERR, "%s%d: power cycle ioctl %"PRIu64" (%"PRIu64")",
					minioncgpu->drv->name, minioncgpu->device_id, *ioseq,
					minioninfo->xffs - minioninfo->last_displayed_xff);
			minioninfo->last_displayed_xff = minioninfo->xffs;
		} else if (show) {
			char *what = "unk";
			switch (obuf[1]) {
				case READ_ADDR(MINION_RES_DATA):
					what = "nonce";
					break;
				case READ_ADDR(MINION_SYS_FIFO_STA):
					what = "fifo";
					break;
			}
			applog(LOG_ERR, "%s%d: reset ioctl %"PRIu64" %s all 0xff (%"PRIu64")",
					minioncgpu->drv->name, minioncgpu->device_id,
					*ioseq, what, minioninfo->xffs - minioninfo->last_displayed_xff);
			minioninfo->last_displayed_xff = minioninfo->xffs;
		}
	}

#if MINION_SHOW_IO
	if (ret > 0) {
		buf = bin2hex((unsigned char *)&(datar[DATA_OFF]), ret);
		applog(IOCTRL_LOG, "*** %s() reply %d = pin %d cid %d %02x %02x %s %02x %02x",
				   __func__, ret, pin, (int)(dataw[DATA_OFF]),
				   datar[0], datar[DATA_OFF-1], buf,
				   datar[DATA_OFF+osiz], datar[DATA_ALL-1]);
		free(buf);
	} else
		applog(LOG_ERR, "*** %s() reply = %d", __func__, ret);

	memcpy(&rbuf[0], &datar[DATA_OFF], osiz);

	display_ioctl(ret, osiz, (uint8_t *)(&dataw[DATA_OFF]), rsiz, (uint8_t *)(&datar[DATA_OFF]));
#endif
#if EXTRA_LOG_IO
	if (obuf[1] == READ_ADDR(MINION_RES_PEEK) ||
	    obuf[1] == READ_ADDR(MINION_RES_DATA) ||
	    obuf[1] == READ_ADDR(MINION_SYS_FIFO_STA)) {
		char *uf1, *uf2, c;
		uf1 = bin2hex(obuf, DATA_SIZ);
		uf2 = bin2hex(rbuf, (size_t)ret);
		switch (obuf[1]) {
			case READ_ADDR(MINION_RES_PEEK):
				c = 'P';
				break;
			case READ_ADDR(MINION_RES_DATA):
				c = 'D';
				break;
			case READ_ADDR(MINION_SYS_FIFO_STA):
				c = 'F';
				break;
		}
		applog(LOG_WARNING, "*** ioseq %"PRIu64" cmd %c %s rep %.8s %s",
				    *ioseq, c, uf1, uf2, uf2+8);
		free(uf2);
		free(uf1);
	}
	if (obuf[1] == WRITE_ADDR(MINION_QUE_0)) {
		char *uf;
		uf = bin2hex(obuf, osiz);
		applog(LOG_WARNING, "*** ioseq %"PRIu64" work %s",
				    *ioseq, uf);
		free(uf);
	}
#endif
	return ret;
}

#if 1
#define do_ioctl(_pin, _obuf, _osiz, _rbuf, _rsiz, _ioseq) \
		__do_ioctl(minioncgpu, minioninfo, _pin, _obuf, _osiz, _rbuf, \
			   _rsiz, _ioseq, MINION_FFL_HERE)
#else
#define do_ioctl(_pin, _obuf, _osiz, _rbuf, _rsiz, _ioseq) \
		_do_ioctl(minioninfo, _pin, _obuf, _osiz, _rbuf, \
			  _rsiz, _ioseq, MINION_FFL_HERE)
// This sends an expected to work, SPI command before each SPI command
static int _do_ioctl(struct minion_info *minioninfo, int pin, uint8_t *obuf, uint32_t osiz, uint8_t *rbuf, uint32_t rsiz, uint64_t *ioseq, MINION_FFL_ARGS)
{
	struct minion_header *head;
	uint8_t buf1[MINION_BUFSIZ];
	uint8_t buf2[MINION_BUFSIZ];
	uint32_t siz;

	head = (struct minion_header *)buf1;
	head->chipid = 1; // Needs to be set to a valid chip
	head->reg = READ_ADDR(MINION_SYS_FIFO_STA);
	SET_HEAD_SIZ(head, DATA_SIZ);
	siz = HSIZE() + DATA_SIZ;
	__do_ioctl(minioncgpu, minioninfo, pin, buf1, siz, buf2, MINION_CORE_SIZ, ioseq, MINION_FFL_PASS);

	return __do_ioctl(minioncgpu, minioninfo, pin, obuf, osiz, rbuf, rsiz, ioseq, MINION_FFL_PASS);
}
#endif

static bool _minion_txrx(struct cgpu_info *minioncgpu, struct minion_info *minioninfo, TASK_ITEM *task, MINION_FFL_ARGS)
{
	struct minion_header *head;

	head = (struct minion_header *)(task->obuf);
	head->chipid = minioninfo->chipid[task->chip];
	if (task->write)
		SET_HEAD_WRITE(head, task->address);
	else
		SET_HEAD_READ(head, task->address);

	SET_HEAD_SIZ(head, task->wsiz + task->rsiz);

	if (task->wsiz)
		memcpy(&(head->data[0]), task->wbuf, task->wsiz);
	task->osiz = HSIZE() + task->wsiz + task->rsiz;

	task->reply = do_ioctl(CHIP_PIN(task->chip), task->obuf, task->osiz, task->rbuf, task->rsiz,
			       &(task->ioseq));
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
	uint64_t ioseq;
	int reply;

	head = (struct minion_header *)wbuf;
	head->chipid = minioninfo->chipid[chip];
	head->reg = reg;
	SET_HEAD_SIZ(head, DATA_SIZ);

	head->data[0] = data[0];
	head->data[1] = data[1];
	head->data[2] = data[2];
	head->data[3] = data[3];

	wsiz = HSIZE() + DATA_SIZ;
	reply = do_ioctl(CHIP_PIN(chip), wbuf, wsiz, rbuf, rsiz, &ioseq);

	if (reply != (int)wsiz) {
		applog(LOG_ERR, "%s: chip %d %s returned %d (should be %d)",
				minioncgpu->drv->dname, chip,
				addr2txt(head->reg),
				reply, (int)wsiz);
	}

	return reply;
}

static void set_freq(struct cgpu_info *minioncgpu, struct minion_info *minioninfo, int chip, int freq)
{
	uint8_t rbuf[MINION_BUFSIZ];
	uint8_t data[4];
	uint32_t value;
	__maybe_unused int reply;

	freq /= MINION_FREQ_FACTOR;
	if (freq < MINION_FREQ_FACTOR_MIN)
		freq = MINION_FREQ_FACTOR_MIN;
	if (freq > MINION_FREQ_FACTOR_MAX)
		freq = MINION_FREQ_FACTOR_MAX;
	value = minion_freq[freq];
	data[0] = (uint8_t)(value & 0xff);
	data[1] = (uint8_t)(((value & 0xff00) >> 8) & 0xff);
	data[2] = (uint8_t)(((value & 0xff0000) >> 16) & 0xff);
	data[3] = (uint8_t)(((value & 0xff000000) >> 24) & 0xff);

	minioninfo->freqsent[chip] = value;

	reply = build_cmd(minioncgpu, minioninfo,
			  chip, WRITE_ADDR(MINION_SYS_FREQ_CTL),
			  rbuf, 0, data);

	cgtime(&(minioninfo->lastfreq[chip]));
	applog(LOG_DEBUG, "%s%i: chip %d freq %d sec %d usec %d",
			  minioncgpu->drv->name, minioncgpu->device_id,
			  chip, freq,
			  (int)(minioninfo->lastfreq[chip].tv_sec) % 10,
			  (int)(minioninfo->lastfreq[chip].tv_usec));

	// Reset all this info on chip reset or freq change
	minioninfo->reset_time[chip] = (int)FREQ_DELAY(minioninfo->init_freq[chip]);
	if (second_check)
		minioninfo->reset2_time[chip] = (int)FREQ2_DELAY(minioninfo->init_freq[chip]);

	minioninfo->chip_status[chip].first_nonce.tv_sec = 0L;

	// Discard chip history (if there is any)
	if (minioninfo->hfree_list) {
		K_WLOCK(minioninfo->hfree_list);
		k_list_transfer_to_head(minioninfo->hchip_list[chip], minioninfo->hfree_list);
		minioninfo->reset_mark[chip] = NULL;
		minioninfo->reset_count[chip] = 0;
		K_WUNLOCK(minioninfo->hfree_list);
	}
}

static void init_chip(struct cgpu_info *minioncgpu, struct minion_info *minioninfo, int chip)
{
	uint8_t rbuf[MINION_BUFSIZ];
	uint8_t data[4];
	__maybe_unused int reply;
	int choice;

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
	if (choice < MINION_FREQ_MIN)
		choice = MINION_FREQ_MIN;
	if (choice > MINION_FREQ_MAX)
		choice = MINION_FREQ_MAX;
	minioninfo->init_freq[chip] = choice;
	set_freq(minioncgpu, minioninfo, chip, choice);

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

static void minion_detect_one(struct cgpu_info *minioncgpu, struct minion_info *minioninfo, int pin, int chipid)
{
	struct minion_header *head;
	uint8_t wbuf[MINION_BUFSIZ];
	uint8_t rbuf[MINION_BUFSIZ];
	uint32_t wsiz, rsiz;
	int reply, tries, newchip;
	uint64_t ioseq;
	bool ok;

	head = (struct minion_header *)wbuf;
	head->chipid = chipid;
	rsiz = MINION_SYS_SIZ;
	SET_HEAD_READ(head, MINION_SYS_CHIP_SIG);
	SET_HEAD_SIZ(head, rsiz);
	wsiz = HSIZE() + rsiz;

	tries = 0;
	ok = false;
	do {
		reply = do_ioctl(pin, wbuf, wsiz, rbuf, rsiz, &ioseq);

		if (reply == (int)(wsiz)) {
			uint32_t sig = u8tou32(rbuf, wsiz - rsiz);

			if (sig == MINION_CHIP_SIG) {
				newchip = (minioninfo->chips)++;
				minioninfo->has_chip[newchip] = true;
				minioninfo->chipid[newchip] = chipid;
				minioninfo->chip_pin[newchip] = pin;
				ok = true;
			} else {
				if (sig == MINION_CHIP_SIG_SHIFT1 ||
				    sig == MINION_CHIP_SIG_SHIFT2 ||
				    sig == MINION_CHIP_SIG_SHIFT3 ||
				    sig == MINION_CHIP_SIG_SHIFT4) {
					applog(LOG_WARNING, "%s: pin %d chipid %d detect offset got"
							    " 0x%08x wanted 0x%08x",
							    minioncgpu->drv->dname, pin, chipid,
							    sig, MINION_CHIP_SIG);
				} else {
					if (sig == MINION_NOCHIP_SIG ||
					    sig == MINION_NOCHIP_SIG2) // Assume no chip
						ok = true;
					else {
						applog(LOG_ERR, "%s: pin %d chipid %d detect failed"
								" got 0x%08x wanted 0x%08x",
								minioncgpu->drv->dname, pin,
								chipid, sig, MINION_CHIP_SIG);
					}
				}
			}
		} else {
			applog(LOG_ERR, "%s: pin %d chipid %d reply %d ignored should be %d",
					minioncgpu->drv->dname, pin, chipid, reply, (int)(wsiz));
		}
	} while (!ok && ++tries <= MINION_SIG_TRIES);

	if (!ok) {
		applog(LOG_ERR, "%s: pin %d chipid %d - detect failure status",
				minioncgpu->drv->dname, pin, chipid);
	}
}

// Simple detect - just check each chip for the signature
static void minion_detect_chips(struct cgpu_info *minioncgpu, struct minion_info *minioninfo)
{
	int pin, chipid, chip;
	int pinend, start_freq, want_freq, freqms;

#if MINION_ROCKCHIP == 1
	minion_toggle_gpio(minioncgpu, MINION_POWERCYCLE_GPIO);
	cgsleep_ms(100);
#endif

	if (usepins) {
		init_pins(minioninfo);
		pinend = (int)MINION_PIN_COUNT;
	} else
		pinend = 1;

	for (pin = 0; pin < pinend; pin++) {
		for (chipid = MINION_MIN_CHIP; chipid <= MINION_MAX_CHIP; chipid++) {
			minion_detect_one(minioncgpu, minioninfo, pin, chipid);
		}
	}

	if (minioninfo->chips) {
		for (chip = 0; chip < (int)MINION_CHIPS; chip++) {
			if (minioninfo->has_chip[chip]) {
				want_freq = minioninfo->init_freq[chip];
				start_freq = want_freq * opt_minion_freqpercent / 100;
				start_freq -= (start_freq % MINION_FREQ_FACTOR);
				if (start_freq < MINION_FREQ_MIN)
					start_freq = MINION_FREQ_MIN;
				minioninfo->want_freq[chip] = want_freq;
				minioninfo->init_freq[chip] = start_freq;
				if (start_freq != want_freq) {
					freqms = opt_minion_freqchange;
					freqms /= ((want_freq - start_freq) / MINION_FREQ_FACTOR);
					if (freqms < 0)
						freqms = -freqms;
					minioninfo->freqms[chip] = freqms;
					minioninfo->changing[chip] = true;
				}
				init_chip(minioncgpu, minioninfo, chip);
				enable_chip_cores(minioncgpu, minioninfo, chip);
			}
		}

#if ENABLE_INT_NONO
		// After everything is ready
		for (chip = 0; chip < MINION_CHIPS; chip++)
			if (minioninfo->has_chip[chip])
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

static bool minion_init_spi(struct cgpu_info *minioncgpu, struct minion_info *minioninfo, int bus, int chip, bool reset)
{
	int i, err, data;
	char buf[64];

	if (reset) {
		// TODO: maybe slow it down?
		close(minioninfo->spifd);
		if (opt_minion_spisleep)
			cgsleep_ms(opt_minion_spisleep);
		minioninfo->spifd = open(minioncgpu->device_path, O_RDWR);
		if (minioninfo->spifd < 0)
			goto bad_out;
		minioninfo->spi_resets++;
//		minioninfo->chip_status[chip].first_nonce.tv_sec = 0L;
	} else {
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
	}

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

static bool minion_setup_chip_select(struct cgpu_info *minioncgpu, struct minion_info *minioninfo)
{
	volatile uint32_t *paddr;
	uint32_t mask, value, mem;
	int count, memfd, pin, bcm;

	memfd = open(minion_memory, O_RDWR | O_SYNC);
	if (memfd < 0) {
		applog(LOG_ERR, "%s: failed open %s (%d)",
				minioncgpu->drv->dname,
				minion_memory, errno);
		return false;
	}

	minioninfo->gpio = (volatile unsigned *)mmap(NULL, MINION_PAGE_SIZE,
						  PROT_READ | PROT_WRITE,
						  MAP_SHARED, memfd,
						  minion_memory_addr);
	if (minioninfo->gpio == MAP_FAILED) {
		close(memfd);
		applog(LOG_ERR, "%s: failed mmap gpio (%d)",
				minioncgpu->drv->dname,
				errno);
		return false;
	}

	close(memfd);

	for (pin = 0; pin < (int)MINION_PIN_COUNT; pin++) {
		bcm = minionPins[pin].bcm;

		paddr = minioninfo->gpio + (BCM2835_GPIO_FSEL0 / 4) + (bcm / 10);

		// Set each pin to be an output pin
		mask = BCM2835_GPIO_FSEL_MASK << ((bcm % 10) * 3);
		value = BCM2835_GPIO_FSEL_OUTPUT << ((bcm % 10) * 3);

		// Read settings
		mem = *paddr;
		*paddr;

		mem = (mem & ~mask) | (value & mask);

		// Write appended setting
		*paddr = mem;
		*paddr = mem;

		count++;
	}

	if (count == 0)
		return false;
	else
		return true;
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

	if (opt_minion_spireset && *opt_minion_spireset) {
		bool is_io = true;
		int val;

		switch (tolower(*opt_minion_spireset)) {
			case 'i':
				is_io = true;
				break;
			case 's':
				is_io = false;
				break;
			default:
				applog(LOG_WARNING, "ERR: Invalid SPI reset '%s'",
						    opt_minion_spireset);
				goto skip;
		}
		val = atoi(opt_minion_spireset+1);
		if (val < 0 || val > 9999) {
			applog(LOG_WARNING, "ERR: Invalid SPI reset '%s'",
					    opt_minion_spireset);
		} else {
			minioninfo->spi_reset_io = is_io;
			minioninfo->spi_reset_count = val;
			minioninfo->last_spi_reset = time(NULL);
		}
	}
skip:
	last_freq = MINION_FREQ_DEF;
	if (opt_minion_freq && *opt_minion_freq) {
		buf = freq = strdup(opt_minion_freq);
		comma = strchr(freq, ',');
		if (comma)
			*(comma++) = '\0';

		for (i = 0; i < (int)MINION_CHIPS; i++) {
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

		for (i = 0; i < (int)MINION_CHIPS; i++) {
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

		for (i = 0; i < (int)MINION_CHIPS; i++) {
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

	define_test();

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

	if (!minion_init_spi(minioncgpu, minioninfo, MINION_SPI_BUS, MINION_SPI_CHIP, false))
		goto unalloc;

#if ENABLE_INT_NONO
	if (!minion_init_gpio_interrupt(minioncgpu, minioninfo))
		goto unalloc;
#endif


	if (usepins) {
		if (!minion_setup_chip_select(minioncgpu, minioninfo))
			goto unalloc;
	}

	mutex_init(&(minioninfo->spi_lock));
	mutex_init(&(minioninfo->sta_lock));

	for (i = 0; i < (int)MINION_CHIPS; i++) {
		minioninfo->init_freq[i] = MINION_FREQ_DEF;
		minioninfo->init_temp[i] = MINION_TEMP_CTL_DEF;
		default_all_cores(&(minioninfo->init_cores[i][0]));
	}

	minion_process_options(minioninfo);

	applog(LOG_WARNING, "%s: checking for chips ...", minioncgpu->drv->dname);

	minion_detect_chips(minioncgpu, minioninfo);

	buf[0] = '\0';
	for (i = 0; i < (int)MINION_CHIPS; i++) {
		if (minioninfo->has_chip[i]) {
			off = strlen(buf);
			snprintf(buf + off, sizeof(buf) - off, " %d:%d/%d",
				 i, minioninfo->chip_pin[i], (int)(minioninfo->chipid[i]));
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

	minioninfo->wfree_list = k_new_list("Work", sizeof(WORK_ITEM),
					    ALLOC_WORK_ITEMS, LIMIT_WORK_ITEMS, true);
	minioninfo->wwork_list = k_new_store(minioninfo->wfree_list);
	minioninfo->wstale_list = k_new_store(minioninfo->wfree_list);
	// Initialise them all in case we later decide to enable chips
	for (i = 0; i < (int)MINION_CHIPS; i++) {
		minioninfo->wque_list[i] = k_new_store(minioninfo->wfree_list);
		minioninfo->wchip_list[i] = k_new_store(minioninfo->wfree_list);
	}

	minioninfo->tfree_list = k_new_list("Task", sizeof(TASK_ITEM),
					    ALLOC_TASK_ITEMS, LIMIT_TASK_ITEMS, true);
	minioninfo->task_list = k_new_store(minioninfo->tfree_list);
	minioninfo->treply_list = k_new_store(minioninfo->tfree_list);

	minioninfo->rfree_list = k_new_list("Reply", sizeof(RES_ITEM),
					    ALLOC_RES_ITEMS, LIMIT_RES_ITEMS, true);
	minioninfo->rnonce_list = k_new_store(minioninfo->rfree_list);

	minioninfo->history_gen = MINION_MAX_RESET_CHECK;
	minioninfo->hfree_list = k_new_list("History", sizeof(HIST_ITEM),
					    ALLOC_HIST_ITEMS, LIMIT_HIST_ITEMS, true);
	for (i = 0; i < (int)MINION_CHIPS; i++)
		minioninfo->hchip_list[i] = k_new_store(minioninfo->hfree_list);

	minioninfo->pfree_list = k_new_list("Performance", sizeof(PERF_ITEM),
					    ALLOC_PERF_ITEMS, LIMIT_PERF_ITEMS, true);
	for (i = 0; i < (int)MINION_CHIPS; i++)
		minioninfo->p_list[i] = k_new_store(minioninfo->pfree_list);

	minioninfo->xfree_list = k_new_list("0xff", sizeof(XFF_ITEM),
					    ALLOC_XFF_ITEMS, LIMIT_XFF_ITEMS, true);
	minioninfo->xff_list = k_new_store(minioninfo->xfree_list);

	cgsem_init(&(minioninfo->task_ready));
	cgsem_init(&(minioninfo->nonce_ready));
	cgsem_init(&(minioninfo->scan_work));

	minioninfo->initialised = true;

	dupalloc(minioncgpu, 10);

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

static char *minion_api_set(struct cgpu_info *minioncgpu, char *option, char *setting, char *replybuf)
{
	struct minion_info *minioninfo = (struct minion_info *)(minioncgpu->device_data);
	int chip, val;
	char *colon;

	if (strcasecmp(option, "help") == 0) {
		sprintf(replybuf, "reset: chip 0-%d freq: 0-%d:%d-%d "
				  "ledcount: 0-100 ledlimit: 0-200 "
				  "spidelay: 0-9999 spireset i|s0-9999 "
				  "spisleep: 0-9999",
				  minioninfo->chips - 1,
				  minioninfo->chips - 1,
				  MINION_FREQ_MIN, MINION_FREQ_MAX);
		return replybuf;
	}

	if (strcasecmp(option, "reset") == 0) {
		if (!setting || !*setting) {
			sprintf(replybuf, "missing chip to reset");
			return replybuf;
		}

		chip = atoi(setting);
		if (chip < 0 || chip >= minioninfo->chips) {
			sprintf(replybuf, "invalid reset: chip '%s' valid range 0-%d",
					  setting,
					  minioninfo->chips);
			return replybuf;
		}

		if (!minioninfo->has_chip[chip]) {
			sprintf(replybuf, "unable to reset chip %d - chip disabled",
					  chip);
			return replybuf;
		}
		minioninfo->flag_reset[chip] = true;
		return NULL;
	}

	// This sets up a freq step up/down to the given freq without a reset
	if (strcasecmp(option, "freq") == 0) {
		if (!setting || !*setting) {
			sprintf(replybuf, "missing chip:freq");
			return replybuf;
		}

		colon = strchr(setting, ':');
		if (!colon) {
			sprintf(replybuf, "missing ':' for chip:freq");
			return replybuf;
		}

		*(colon++) = '\0';
		if (!*colon) {
			sprintf(replybuf, "missing freq in chip:freq");
			return replybuf;
		}

		chip = atoi(setting);
		if (chip < 0 || chip >= minioninfo->chips) {
			sprintf(replybuf, "invalid freq: chip '%s' valid range 0-%d",
					  setting,
					  minioninfo->chips);
			return replybuf;
		}

		if (!minioninfo->has_chip[chip]) {
			sprintf(replybuf, "unable to modify chip %d - chip not enabled",
					  chip);
			return replybuf;
		}

		val = atoi(colon);
		if (val < MINION_FREQ_MIN || val > MINION_FREQ_MAX) {
			sprintf(replybuf, "invalid freq: '%s' valid range %d-%d",
					  setting,
					  MINION_FREQ_MIN, MINION_FREQ_MAX);
			return replybuf;
		}

		int want_freq = val - (val % MINION_FREQ_FACTOR);
		int start_freq = minioninfo->init_freq[chip];
		int freqms;

		if (want_freq != start_freq) {
			minioninfo->changing[chip] = false;
			freqms = opt_minion_freqchange;
			freqms /= ((want_freq - start_freq) / MINION_FREQ_FACTOR);
			if (freqms < 0)
				freqms = -freqms;
			minioninfo->freqms[chip] = freqms;
			minioninfo->want_freq[chip] = want_freq;
			cgtime(&(minioninfo->lastfreq[chip]));
			minioninfo->changing[chip] = true;
		}

		return NULL;
	}

	if (strcasecmp(option, "ledcount") == 0) {
		if (!setting || !*setting) {
			sprintf(replybuf, "missing ledcount value");
			return replybuf;
		}

		val = atoi(setting);
		if (val < 0 || val > 100) {
			sprintf(replybuf, "invalid ledcount: '%s' valid range 0-100",
					  setting);
			return replybuf;
		}

		opt_minion_ledcount = val;
		return NULL;
	}

	if (strcasecmp(option, "ledlimit") == 0) {
		if (!setting || !*setting) {
			sprintf(replybuf, "missing ledlimit value");
			return replybuf;
		}

		val = atoi(setting);
		if (val < 0 || val > 200) {
			sprintf(replybuf, "invalid ledlimit: GHs '%s' valid range 0-200",
					  setting);
			return replybuf;
		}

		opt_minion_ledlimit = val;
		return NULL;
	}

	if (strcasecmp(option, "spidelay") == 0) {
		if (!setting || !*setting) {
			sprintf(replybuf, "missing spidelay value");
			return replybuf;
		}

		val = atoi(setting);
		if (val < 0 || val > 9999) {
			sprintf(replybuf, "invalid spidelay: ms '%s' valid range 0-9999",
					  setting);
			return replybuf;
		}

		opt_minion_spidelay = val;
		return NULL;
	}

	if (strcasecmp(option, "spireset") == 0) {
		bool is_io = true;

		if (!setting || !*setting) {
			sprintf(replybuf, "missing spireset value");
			return replybuf;
		}

		switch (tolower(*setting)) {
			case 'i':
				is_io = true;
				break;
			case 's':
				is_io = false;
				break;
			default:
				sprintf(replybuf, "invalid spireset: '%s' must start with i or s",
						  setting);
				return replybuf;
		}
		val = atoi(setting+1);
		if (val < 0 || val > 9999) {
			sprintf(replybuf, "invalid spireset: %c '%s' valid range 0-9999",
					  *setting, setting+1);
			return replybuf;
		}

		minioninfo->spi_reset_io = is_io;
		minioninfo->spi_reset_count = val;
		minioninfo->last_spi_reset = time(NULL);

		return NULL;
	}

	if (strcasecmp(option, "spisleep") == 0) {
		if (!setting || !*setting) {
			sprintf(replybuf, "missing spisleep value");
			return replybuf;
		}

		val = atoi(setting);
		if (val < 0 || val > 9999) {
			sprintf(replybuf, "invalid spisleep: ms '%s' valid range 0-9999",
					  setting);
			return replybuf;
		}

		opt_minion_spisleep = val;
		return NULL;
	}

	if (strcasecmp(option, "spiusec") == 0) {
		if (!setting || !*setting) {
			sprintf(replybuf, "missing spiusec value");
			return replybuf;
		}

		val = atoi(setting);
		if (val < 0 || val > 9999) {
			sprintf(replybuf, "invalid spiusec: '%s' valid range 0-9999",
					  setting);
			return replybuf;
		}

		opt_minion_spiusec = val;
		return NULL;
	}

	sprintf(replybuf, "Unknown option: %s", option);
	return replybuf;
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
	TASK_ITEM *titem;

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
			while (item && !(DATA_TASK(item)->urgent))
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
			struct timeval now;
			double howlong;
			int i;

			titem = DATA_TASK(item);

			switch (titem->address) {
				// TODO: case MINION_SYS_TEMP_CTL:
				// TODO: case MINION_SYS_FREQ_CTL:
				case READ_ADDR(MINION_SYS_CHIP_STA):
				case WRITE_ADDR(MINION_SYS_SPI_LED):
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
				if (titem->witem) {
					cgtime(&now);
					howlong = tdiff(&now, &(DATA_WORK(titem->witem)->created));
					minioninfo->wt_work++;
					minioninfo->wt_time += howlong;
					if (minioninfo->wt_min == 0 || minioninfo->wt_min > howlong)
						minioninfo->wt_min = howlong;
					else if (minioninfo->wt_max < howlong)
						minioninfo->wt_max = howlong;
					for (i = 0; i < TIME_BANDS; i++) {
						if (howlong < time_bands[i]) {
							minioninfo->wt_bands[i]++;
							break;
						}
					}
					if (i >= TIME_BANDS)
						minioninfo->wt_bands[TIME_BANDS]++;
				}

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
									DATA_TASK(task)->tid = ++(minioninfo->next_tid);
									DATA_TASK(task)->chip = chip;
									DATA_TASK(task)->write = true;
									DATA_TASK(task)->address = MINION_SYS_RSTN_CTL;
									DATA_TASK(task)->task_id = 0; // ignored
									DATA_TASK(task)->wsiz = MINION_SYS_SIZ;
									DATA_TASK(task)->rsiz = 0;
									DATA_TASK(task)->wbuf[0] = SYS_RSTN_CTL_FLUSH;
									DATA_TASK(task)->wbuf[1] = 0;
									DATA_TASK(task)->wbuf[2] = 0;
									DATA_TASK(task)->wbuf[3] = 0;
									DATA_TASK(task)->urgent = true;
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
							int cnt = 0;
							K_WLOCK(minioninfo->wwork_list);
							work = minioninfo->wchip_list[chip]->head;
							while (work) {
								cnt++;
								DATA_WORK(work)->stale = true;
								work = work->next;
							}
							minioninfo->chip_status[chip].chipwork = 0;
							minioninfo->chip_status[chip].realwork = 0;
							minioninfo->wchip_staled += cnt;
#if MINION_SHOW_IO
							applog(IOCTRL_LOG, "RSTN chip %d (cnt=%d) cw0=%u rw0=%u qw=%u",
									   chip, cnt,
									   minioninfo->chip_status[chip].chipwork,
									   minioninfo->chip_status[chip].realwork,
									   minioninfo->chip_status[chip].quework);
#endif
							K_WUNLOCK(minioninfo->wwork_list);
						}
						break;
					case WRITE_ADDR(MINION_QUE_0):
						K_WLOCK(minioninfo->wchip_list[chip]);
						k_unlink_item(minioninfo->wque_list[chip], titem->witem);
						k_add_head(minioninfo->wchip_list[chip], titem->witem);
						DATA_WORK(titem->witem)->ioseq = titem->ioseq;
						minioninfo->chip_status[chip].quework--;
						minioninfo->chip_status[chip].chipwork++;
#if MINION_SHOW_IO
						applog(IOCTRL_LOG, "QUE_0 chip %d cw+1=%u rw=%u qw-1=%u",
								   chip,
								   minioninfo->chip_status[chip].chipwork,
								   minioninfo->chip_status[chip].realwork,
								   minioninfo->chip_status[chip].quework);
#endif
						K_WUNLOCK(minioninfo->wchip_list[chip]);
						applog(LOG_DEBUG, "%s%d: task 0x%04x sent to chip %d",
								  minioncgpu->drv->name, minioncgpu->device_id,
								  titem->task_id, chip);
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
					case WRITE_ADDR(MINION_SYS_SPI_LED):
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
	TASK_ITEM fifo_task, res1_task, res2_task;
	int chip, resoff;
	bool somelow;
	struct timeval now;

#if ENABLE_INT_NONO
	uint64_t ioseq;
	TASK_ITEM clr_task;
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
		for (chip = 0; chip < (int)MINION_CHIPS; chip++) {
			if (minioninfo->has_chip[chip]) {
				int tries = 0;
				uint8_t res, cmd;

				if (minioninfo->changing[chip] &&
				    ms_tdiff(&now, &minioninfo->lastfreq[chip]) >
				    minioninfo->freqms[chip]) {
					int want_freq = minioninfo->want_freq[chip];
					int init_freq = minioninfo->init_freq[chip];

					if (want_freq > init_freq) {
						minioninfo->init_freq[chip] += MINION_FREQ_FACTOR;
						init_freq += MINION_FREQ_FACTOR;

						set_freq(minioncgpu, minioninfo, chip, init_freq);
					} else if (want_freq < init_freq) {
						minioninfo->init_freq[chip] -= MINION_FREQ_FACTOR;
						init_freq -= MINION_FREQ_FACTOR;

						set_freq(minioncgpu, minioninfo, chip, init_freq);
					}

					if (init_freq == want_freq)
						minioninfo->changing[chip] = false;
				}

				while (++tries < 4) {
					res = cmd = 0;
					fifo_task.chip = chip;
					fifo_task.reply = 0;
					minion_txrx(&fifo_task);
					if (fifo_task.reply <= 0) {
						minioninfo->spi_errors++;
						minioninfo->fifo_spi_errors[chip]++;
						minioninfo->res_err_count[chip]++;
						break;
					} else {
						if (fifo_task.reply < (int)(fifo_task.osiz)) {
							char *buf = bin2hex((unsigned char *)(&(fifo_task.rbuf[fifo_task.osiz - fifo_task.rsiz])),
										(int)(fifo_task.rsiz));
							applog(LOG_DEBUG, "%s%i: Chip %d Bad fifo reply (%s) size %d, should be %d",
									  minioncgpu->drv->name, minioncgpu->device_id,
									  chip, buf,
									  fifo_task.reply, (int)(fifo_task.osiz));
							free(buf);
							minioninfo->spi_errors++;
							minioninfo->fifo_spi_errors[chip]++;
							minioninfo->res_err_count[chip]++;
						} else {
							if (fifo_task.reply > (int)(fifo_task.osiz)) {
								applog(LOG_DEBUG, "%s%i: Chip %d Unexpected fifo reply size %d, "
										  "expected only %d",
										  minioncgpu->drv->name, minioncgpu->device_id,
										  chip, fifo_task.reply, (int)(fifo_task.osiz));
							}
							res = FIFO_RES(fifo_task.rbuf, fifo_task.osiz - fifo_task.rsiz);
							cmd = FIFO_CMD(fifo_task.rbuf, fifo_task.osiz - fifo_task.rsiz);
							// valid reply?
							if (res <= MINION_QUE_MAX && cmd <= MINION_QUE_MAX)
								break;

							applog(LOG_DEBUG, "%s%i: Chip %d Bad fifo reply res %d (max is %d) "
									  "cmd %d (max is %d)",
									  minioncgpu->drv->name, minioncgpu->device_id,
									  chip, (int)res, MINION_QUE_MAX,
									  (int)cmd, MINION_QUE_MAX);
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
				// have to just assume it's always correct since we can't verify it
				minioninfo->chip_status[chip].realwork = (uint32_t)cmd;
#if MINION_SHOW_IO
				applog(IOCTRL_LOG, "SetReal chip %d cw=%u rw==%u qw=%u",
						   chip,
						   minioninfo->chip_status[chip].chipwork,
						   minioninfo->chip_status[chip].realwork,
						   minioninfo->chip_status[chip].quework);
#endif
				K_WUNLOCK(minioninfo->wwork_list);

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
							applog(LOG_ERR, "%s%i: Chip %d Bad work reply (%s) size %d, should be at least %d",
									minioncgpu->drv->name, minioncgpu->device_id,
									chip, buf,
									res1_task.reply, (int)MINION_RES_DATA_SIZ);
							free(buf);
							minioninfo->spi_errors++;
							minioninfo->res_spi_errors[chip]++;
							minioninfo->res_err_count[chip]++;
						} else {
							if (res1_task.reply != (int)(res1_task.osiz)) {
								applog(LOG_ERR, "%s%i: Chip %d Unexpected work reply size %d, expected %d",
										minioncgpu->drv->name, minioncgpu->device_id,
										chip, res1_task.reply, (int)(res1_task.osiz));
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

									//DATA_RES(item)->chip = RES_CHIPID(use1);
									// We can avoid any SPI transmission error of the chip number
									DATA_RES(item)->chip = (uint8_t)chip;
									if (minioninfo->chipid[chip] != RES_CHIPID(use1)) {
										minioninfo->spi_errors++;
										minioninfo->res_spi_errors[chip]++;
										minioninfo->res_err_count[chip]++;
									}
									if (use2 && minioninfo->chipid[chip] != RES_CHIPID(use2)) {
										minioninfo->spi_errors++;
										minioninfo->res_spi_errors[chip]++;
										minioninfo->res_err_count[chip]++;
									}
									DATA_RES(item)->core = RES_CORE(use1);
									DATA_RES(item)->task_id = RES_TASK(use1);
									DATA_RES(item)->nonce = RES_NONCE(use1);
									DATA_RES(item)->no_nonce = !RES_GOLD(use1);
									memcpy(&(DATA_RES(item)->when), &now, sizeof(now));
									applog(LOG_DEBUG, "%s%i: reply task_id 0x%04x"
											  " - chip %d - gold %d",
											  minioncgpu->drv->name,
											  minioncgpu->device_id,
											  RES_TASK(use1),
											  (int)RES_CHIPID(use1),
											  (int)RES_GOLD(use1));

									if (!use2)
										DATA_RES(item)->another = false;
									else {
										DATA_RES(item)->another = true;
										DATA_RES(item)->task_id2 = RES_TASK(use2);
										DATA_RES(item)->nonce2 = RES_NONCE(use2);
									}
//if (RES_GOLD(use1))
//applog(MINTASK_LOG, "%s%i: found a result chip %d core %d task 0x%04x nonce 0x%08x gold=%d", minioncgpu->drv->name, minioncgpu->device_id, DATA_RES(item)->chip, DATA_RES(item)->core, DATA_RES(item)->task_id, DATA_RES(item)->nonce, (int)RES_GOLD(use1));

									K_WLOCK(minioninfo->rnonce_list);
									k_add_head(minioninfo->rnonce_list, item);
									K_WUNLOCK(minioninfo->rnonce_list);

									if (!(minioninfo->chip_status[chip].first_nonce.tv_sec)) {
										cgtime(&(minioninfo->chip_status[chip].first_nonce));
										minioninfo->chip_status[chip].from_first_good = 0;
									}

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
			for (chip = 0; chip < (int)MINION_CHIPS; chip++) {
				if (minioninfo->has_chip[chip]) {
					SET_HEAD_READ(head, MINION_SYS_INT_STA);
					head->chipid = minioninfo->chipid[chip];
					reply = do_ioctl(CHIP_PIN(chip), wbuf, wsiz, rbuf, rsiz, &ioseq);
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
	NONCE_DUP_NONCE,
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

	/* remove older ioseq work items
	   no_nonce means this 'item' has finished also */
	tail = minioninfo->wchip_list[chip]->tail;
	while (tail && (DATA_WORK(tail)->ioseq < DATA_WORK(item)->ioseq)) {
		k_unlink_item(minioninfo->wchip_list[chip], tail);
		if (!(DATA_WORK(tail)->stale)) {
			minioninfo->chip_status[chip].chipwork--;
#if MINION_SHOW_IO
			applog(IOCTRL_LOG, "COld chip %d cw-1=%u rw=%u qw=%u",
					   chip,
					   minioninfo->chip_status[chip].chipwork,
					   minioninfo->chip_status[chip].realwork,
					   minioninfo->chip_status[chip].quework);
#endif
/*
			// If it had no valid work (only errors) then it won't have been cleaned up
			errs = (DATA_WORK(tail)->errors > 0);
			applog(errs ? LOG_DEBUG : LOG_ERR,
			applog(LOG_ERR,
				"%s%i: discarded old task 0x%04x chip %d no reply errs=%d",
				minioncgpu->drv->name, minioncgpu->device_id,
				DATA_WORK(tail)->task_id, chip, DATA_WORK(tail)->errors);
*/
		}
		applog(MINION_LOG, "%s%i: marking complete - old task 0x%04x chip %d",
				   minioncgpu->drv->name, minioncgpu->device_id,
				   DATA_WORK(tail)->task_id, chip);
		if (DATA_WORK(tail)->rolled)
			free_work(DATA_WORK(tail)->work);
		else
			work_completed(minioncgpu, DATA_WORK(tail)->work);
		k_free_head(minioninfo->wfree_list, tail);
		tail = minioninfo->wchip_list[chip]->tail;
	}
	if (no_nonce) {
		if (!(DATA_WORK(item)->stale)) {
			minioninfo->chip_status[chip].chipwork--;
#if MINION_SHOW_IO
		applog(IOCTRL_LOG, "CONoN chip %d cw-1=%u rw=%u qw=%u",
				   chip,
				   minioninfo->chip_status[chip].chipwork,
				   minioninfo->chip_status[chip].realwork,
				   minioninfo->chip_status[chip].quework);
#endif
		}
		applog(MINION_LOG, "%s%i: marking complete - no_nonce task 0x%04x chip %d",
				   minioncgpu->drv->name, minioncgpu->device_id,
				   DATA_WORK(item)->task_id, chip);
		if (DATA_WORK(item)->rolled)
			free_work(DATA_WORK(item)->work);
		else
			work_completed(minioncgpu, DATA_WORK(item)->work);
	}
}

// Need to put it back in the list where it was - according to ioseq
static void restorework(struct minion_info *minioninfo, int chip, K_ITEM *item)
{
	K_ITEM *look;

	look = minioninfo->wchip_list[chip]->tail;
	while (look && DATA_WORK(look)->ioseq < DATA_WORK(item)->ioseq)
		look = look->prev;
	if (!look)
		k_add_head(minioninfo->wchip_list[chip], item);
	else
		k_insert_after(minioninfo->wchip_list[chip], item, look);
}

static enum nonce_state oknonce(struct thr_info *thr, struct cgpu_info *minioncgpu, int chip, int core,
				uint32_t task_id, uint32_t nonce, bool no_nonce, struct timeval *when,
				bool another, uint32_t task_id2, uint32_t nonce2)
{
	struct minion_info *minioninfo = (struct minion_info *)(minioncgpu->device_data);
	struct timeval now;
	K_ITEM *item, *tail;
	uint32_t min_task_id, max_task_id;
//	uint64_t chip_good;
	bool redo;

	// if the chip has been disabled - but we don't do that - so not possible (yet)
	if (!(minioninfo->has_chip[chip])) {
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
	K_WLOCK(minioninfo->wchip_list[chip]);
	item = minioninfo->wchip_list[chip]->tail;

	if (!item) {
		K_WUNLOCK(minioninfo->wchip_list[chip]);
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

	min_task_id = DATA_WORK(item)->task_id;
	while (item) {
		if (DATA_WORK(item)->task_id == task_id)
			break;

		item = item->prev;
	}
	max_task_id = DATA_WORK(minioninfo->wchip_list[chip]->head)->task_id;

	if (!item) {
		K_WUNLOCK(minioninfo->wchip_list[chip]);
		if (another && task_id != task_id2) {
			minioninfo->tasks_failed[chip]++;
			task_id = task_id2;
			redo = true;
			goto retry;
		}

		minioninfo->spi_errors++;
		minioninfo->res_spi_errors[chip]++;
		minioninfo->res_err_count[chip]++;
		applog(MINTASK_LOG, "%s%i: chip %d core %d unknown task 0x%04x "
				    "(min=0x%04x max=0x%04x no_nonce=%d)",
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

	k_unlink_item(minioninfo->wchip_list[chip], item);
	if (no_nonce) {
		cleanup_older(minioncgpu, chip, item, no_nonce);
		k_free_head(minioninfo->wfree_list, item);
		K_WUNLOCK(minioninfo->wchip_list[chip]);
		return NONCE_NO_NONCE;
	}
	K_WUNLOCK(minioninfo->wchip_list[chip]);

	minioninfo->tested_nonces++;

	redo = false;
retest:
	if (test_nonce(DATA_WORK(item)->work, nonce)) {
/*
		if (isdupnonce(minioncgpu, DATA_WORK(item)->work, nonce)) {
			minioninfo->chip_dup[chip]++;
			applog(LOG_WARNING, " ... nonce %02x%02x%02x%02x chip %d core %d task 0x%04x",
					    (nonce & 0xff), ((nonce >> 8) & 0xff),
					    ((nonce >> 16) & 0xff), ((nonce >> 24) & 0xff),
					    chip, core, task_id);
			K_WLOCK(minioninfo->wchip_list[chip]);
			restorework(minioninfo, chip, item);
			K_WUNLOCK(minioninfo->wchip_list[chip]);
			return NONCE_DUP_NONCE;
		}
*/
//applog(MINTASK_LOG, "%s%i: Valid Nonce chip %d core %d task 0x%04x nonce 0x%08x", minioncgpu->drv->name, minioncgpu->device_id, chip, core, task_id, nonce);
//
		submit_tested_work(thr, DATA_WORK(item)->work);

		if (redo)
			minioninfo->nonces_recovered[chip]++;

		/* chip_good = */ ++(minioninfo->chip_good[chip]);
		minioninfo->chip_status[chip].from_first_good++;
		minioninfo->core_good[chip][core]++;
		DATA_WORK(item)->nonces++;

		mutex_lock(&(minioninfo->nonce_lock));
		minioninfo->new_nonces++;
		mutex_unlock(&(minioninfo->nonce_lock));
		minioninfo->ok_nonces++;

		K_WLOCK(minioninfo->wchip_list[chip]);
		cleanup_older(minioncgpu, chip, item, no_nonce);
		restorework(minioninfo, chip, item);
		K_WUNLOCK(minioninfo->wchip_list[chip]);

		// add to history and remove old history and keep track of the 2 reset marks
		int chip_tmp;
		cgtime(&now);
		K_WLOCK(minioninfo->hfree_list);
		item = k_unlink_head(minioninfo->hfree_list);
		memcpy(&(DATA_HIST(item)->when), when, sizeof(*when));
		k_add_head(minioninfo->hchip_list[chip], item);
		if (minioninfo->reset_mark[chip])
			minioninfo->reset_count[chip]++;
		if (second_check && minioninfo->reset2_mark[chip])
			minioninfo->reset2_count[chip]++;

		// N.B. this also corrects each reset_mark/reset_count within each hchip_list
		for (chip_tmp = 0; chip_tmp < (int)MINION_CHIPS; chip_tmp++) {
			tail = minioninfo->hchip_list[chip_tmp]->tail;
			while (tail && tdiff(&(DATA_HIST(tail)->when), &now) > MINION_HISTORY_s) {
				if (minioninfo->reset_mark[chip] == tail) {
					minioninfo->reset_mark[chip] = tail->prev;
					minioninfo->reset_count[chip]--;
				}
				if (second_check && minioninfo->reset2_mark[chip] == tail) {
					minioninfo->reset2_mark[chip] = tail->prev;
					minioninfo->reset2_count[chip]--;
				}
				tail = k_unlink_tail(minioninfo->hchip_list[chip_tmp]);
				k_add_head(minioninfo->hfree_list, item);
				tail = minioninfo->hchip_list[chip_tmp]->tail;
			}
			if (!(minioninfo->reset_mark[chip])) {
				minioninfo->reset_mark[chip] = minioninfo->hchip_list[chip]->tail;
				minioninfo->reset_count[chip] = minioninfo->hchip_list[chip]->count;
			}
			if (second_check && !(minioninfo->reset2_mark[chip])) {
				minioninfo->reset2_mark[chip] = minioninfo->hchip_list[chip]->tail;
				minioninfo->reset2_count[chip] = minioninfo->hchip_list[chip]->count;
			}
			tail = minioninfo->reset_mark[chip];
			while (tail && tdiff(&(DATA_HIST(tail)->when), &now) > minioninfo->reset_time[chip]) {
				tail = minioninfo->reset_mark[chip] = tail->prev;
				minioninfo->reset_count[chip]--;
			}
			if (second_check) {
				tail = minioninfo->reset2_mark[chip];
				while (tail && tdiff(&(DATA_HIST(tail)->when), &now) > minioninfo->reset2_time[chip]) {
					tail = minioninfo->reset2_mark[chip] = tail->prev;
					minioninfo->reset2_count[chip]--;
				}
			}
		}
		K_WUNLOCK(minioninfo->hfree_list);

/*
		// Reset the chip after 8 nonces found
		if (chip_good == 8) {
			memcpy(&(minioninfo->last_reset[chip]), &now, sizeof(now));
			init_chip(minioncgpu, minioninfo, chip);
		}
*/

		return NONCE_GOOD_NONCE;
	}

	if (another && nonce != nonce2) {
		minioninfo->nonces_failed[chip]++;
		nonce = nonce2;
		redo = true;
		goto retest;
	}

	DATA_WORK(item)->errors++;
	K_WLOCK(minioninfo->wchip_list[chip]);
	restorework(minioninfo, chip, item);
	K_WUNLOCK(minioninfo->wchip_list[chip]);

	minioninfo->chip_bad[chip]++;
	minioninfo->core_bad[chip][core]++;
	inc_hw_errors(thr);
//applog(MINTASK_LOG, "%s%i: HW ERROR chip %d core %d task 0x%04x nonce 0x%08x", minioncgpu->drv->name, minioncgpu->device_id, chip, core, task_id, nonce);

	return NONCE_BAD_NONCE;
}

/* Check each chip how long since the last nonce
 * Should normally be a fraction of a second
 * so (MINION_RESET_s * 1.5) will certainly be long enough,
 * but also will avoid lots of resets if there is trouble getting work
 * Should be longer than MINION_RESET_s to avoid interfering with normal resets */
static void check_last_nonce(struct cgpu_info *minioncgpu)
{
	struct minion_info *minioninfo = (struct minion_info *)(minioncgpu->device_data);
	struct timeval now;
	K_ITEM *head;
	double howlong;
	int chip;

	cgtime(&now);
	K_RLOCK(minioninfo->hfree_list);
	for (chip = 0; chip < (int)MINION_CHIPS; chip++) {
		if (minioninfo->has_chip[chip] &&  !(minioninfo->changing[chip])) {
			head = minioninfo->hchip_list[chip]->head;
			if (head) {
				howlong = tdiff(&now, &(DATA_HIST(head)->when));
				if (howlong > ((double)MINION_RESET_s * 1.5)) {
					// Setup a reset
					minioninfo->flag_reset[chip] = true;
					minioninfo->do_reset[chip] = 0.0;
				}
			}
		}
	}
	K_RUNLOCK(minioninfo->hfree_list);
}

// Results checking thread
static void *minion_results(void *userdata)
{
	struct cgpu_info *minioncgpu = (struct cgpu_info *)userdata;
	struct minion_info *minioninfo = (struct minion_info *)(minioncgpu->device_data);
	struct thr_info *thr;
	int chip = 0, core = 0;
	uint32_t task_id = 0;
	uint32_t nonce = 0;
	bool no_nonce = false;
	struct timeval when;
	bool another;
	uint32_t task_id2 = 0;
	uint32_t nonce2 = 0;
	int last_check;

	applog(MINION_LOG, "%s%i: Results...",
				minioncgpu->drv->name, minioncgpu->device_id);

	// Wait until we're ready
	while (minioncgpu->shutdown == false) {
		if (minioninfo->initialised) {
			break;
		}
		cgsleep_ms(3);
	}

	thr = minioninfo->thr;

	last_check = 0;
	while (minioncgpu->shutdown == false) {
		if (!oldest_nonce(minioncgpu, &chip, &core, &task_id, &nonce,
				  &no_nonce, &when, &another, &task_id2, &nonce2)) {
			check_last_nonce(minioncgpu);
			last_check = 0;
			cgsem_mswait(&(minioninfo->nonce_ready), MINION_NONCE_mS);
			continue;
		}

		oknonce(thr, minioncgpu, chip, core, task_id, nonce, no_nonce, &when,
			another, task_id2, nonce2);

		// Interrupt nonce checking if low CPU and oldest_nonce() is always true
		if (++last_check > 100) {
			check_last_nonce(minioncgpu);
			last_check = 0;
		}
	}

	return NULL;
}

static void minion_flush_work(struct cgpu_info *minioncgpu)
{
	struct minion_info *minioninfo = (struct minion_info *)(minioncgpu->device_data);
	K_ITEM *prev_unused, *task, *prev_task, *witem;
	int i;

	if (minioninfo->initialised == false)
		return;

	applog(MINION_LOG, "%s%i: flushing work",
				minioncgpu->drv->name, minioncgpu->device_id);

	// TODO: N.B. scanwork also gets work locks - which master thread calls flush?
	K_WLOCK(minioninfo->wwork_list);

	// Simply remove the whole unused wwork_list
	k_list_transfer_to_head(minioninfo->wwork_list, minioninfo->wstale_list);
	minioninfo->wwork_flushed += minioninfo->wstale_list->count;

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
		if (DATA_TASK(task)->address == WRITE_ADDR(MINION_QUE_0)) {
			minioninfo->chip_status[DATA_TASK(task)->chip].quework--;
#if MINION_SHOW_IO
			applog(IOCTRL_LOG, "QueFlush chip %d cw=%u rw=%u qw-1=%u",
					   (int)DATA_TASK(task)->chip,
					   minioninfo->chip_status[DATA_TASK(task)->chip].chipwork,
					   minioninfo->chip_status[DATA_TASK(task)->chip].realwork,
					   minioninfo->chip_status[DATA_TASK(task)->chip].quework);
#endif
			witem = DATA_TASK(task)->witem;
			k_unlink_item(minioninfo->wque_list[DATA_TASK(task)->chip], witem);
			minioninfo->wque_flushed++;
			if (DATA_WORK(witem)->rolled)
				free_work(DATA_WORK(witem)->work);
			else
				work_completed(minioncgpu, DATA_WORK(witem)->work);
			k_free_head(minioninfo->wfree_list, witem);
			k_unlink_item(minioninfo->task_list, task);
			k_free_head(minioninfo->tfree_list, task);
		}
		task = prev_task;
	}
	for (i = 0; i < (int)MINION_CHIPS; i++) {
		if (minioninfo->has_chip[i]) {
			// TODO: consider sending it now rather than adding to the task list?
			task = k_unlink_head(minioninfo->tfree_list);
			DATA_TASK(task)->tid = ++(minioninfo->next_tid);
			DATA_TASK(task)->chip = i;
			DATA_TASK(task)->write = true;
			DATA_TASK(task)->address = MINION_SYS_RSTN_CTL;
			DATA_TASK(task)->task_id = 0; // ignored
			DATA_TASK(task)->wsiz = MINION_SYS_SIZ;
			DATA_TASK(task)->rsiz = 0;
			DATA_TASK(task)->wbuf[0] = SYS_RSTN_CTL_FLUSH;
			DATA_TASK(task)->wbuf[1] = 0;
			DATA_TASK(task)->wbuf[2] = 0;
			DATA_TASK(task)->wbuf[3] = 0;
			DATA_TASK(task)->urgent = true;
			k_add_head(minioninfo->task_list, task);
		}
	}
	K_WUNLOCK(minioninfo->tfree_list);

	K_WUNLOCK(minioninfo->wwork_list);

	// TODO: send a signal to force getting and sending new work - needs cgsem_wait in the sending thread

	// TODO: should we use this thread to do the following work?
	if (minioninfo->wstale_list->count) {
		// mark complete all stale unused work (oldest first)
		prev_unused = minioninfo->wstale_list->tail;
		while (prev_unused) {
			if (DATA_WORK(prev_unused)->rolled)
				free_work(DATA_WORK(prev_unused)->work);
			else
				work_completed(minioncgpu, DATA_WORK(prev_unused)->work);
			prev_unused = prev_unused->prev;
		}

		// put them back in the wfree_list
		K_WLOCK(minioninfo->wfree_list);
		k_list_transfer_to_head(minioninfo->wstale_list, minioninfo->wfree_list);
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
			DATA_TASK(item)->tid = ++(minioninfo->next_tid);
			K_WUNLOCK(minioninfo->tfree_list);

			DATA_TASK(item)->chip = chip;
			DATA_TASK(item)->write = false;
			DATA_TASK(item)->address = READ_ADDR(MINION_SYS_CHIP_STA);
			DATA_TASK(item)->task_id = 0;
			DATA_TASK(item)->wsiz = 0;
			DATA_TASK(item)->rsiz = MINION_SYS_SIZ;
			DATA_TASK(item)->urgent = false;

			K_WLOCK(minioninfo->task_list);
			k_add_head(minioninfo->task_list, item);
			item = k_unlink_head(minioninfo->tfree_list);
			DATA_TASK(item)->tid = ++(minioninfo->next_tid);
			K_WUNLOCK(minioninfo->task_list);

			DATA_TASK(item)->chip = chip;
			DATA_TASK(item)->write = false;
			DATA_TASK(item)->address = READ_ADDR(MINION_SYS_IDLE_CNT);
			DATA_TASK(item)->task_id = 0;
			DATA_TASK(item)->wsiz = 0;
			DATA_TASK(item)->rsiz = MINION_SYS_SIZ;
			DATA_TASK(item)->urgent = false;

			K_WLOCK(minioninfo->task_list);
			k_add_head(minioninfo->task_list, item);
			K_WUNLOCK(minioninfo->task_list);

			// Get the core ena and act state
			for (rep = 0; rep < MINION_CORE_REPS; rep++) {
				// Ena
				K_WLOCK(minioninfo->tfree_list);
				item = k_unlink_head(minioninfo->tfree_list);
				DATA_TASK(item)->tid = ++(minioninfo->next_tid);
				K_WUNLOCK(minioninfo->tfree_list);

				DATA_TASK(item)->chip = chip;
				DATA_TASK(item)->write = false;
				DATA_TASK(item)->address = READ_ADDR(MINION_CORE_ENA0_31 + rep);
				DATA_TASK(item)->task_id = 0;
				DATA_TASK(item)->wsiz = 0;
				DATA_TASK(item)->rsiz = MINION_SYS_SIZ;
				DATA_TASK(item)->urgent = false;

				K_WLOCK(minioninfo->task_list);
				k_add_head(minioninfo->task_list, item);
				// Act
				item = k_unlink_head(minioninfo->tfree_list);
				DATA_TASK(item)->tid = ++(minioninfo->next_tid);
				K_WUNLOCK(minioninfo->task_list);

				DATA_TASK(item)->chip = chip;
				DATA_TASK(item)->write = false;
				DATA_TASK(item)->address = READ_ADDR(MINION_CORE_ACT0_31 + rep);
				DATA_TASK(item)->task_id = 0;
				DATA_TASK(item)->wsiz = 0;
				DATA_TASK(item)->rsiz = MINION_SYS_SIZ;
				DATA_TASK(item)->urgent = false;

				K_WLOCK(minioninfo->task_list);
				k_add_head(minioninfo->task_list, item);
				K_WUNLOCK(minioninfo->task_list);
			}

			if (minioninfo->lednow[chip] != minioninfo->setled[chip]) {
				uint32_t led;

				minioninfo->lednow[chip] = minioninfo->setled[chip];
				if (minioninfo->lednow[chip])
					led = MINION_SPI_LED_ON;
				else
					led = MINION_SPI_LED_OFF;

				K_WLOCK(minioninfo->tfree_list);
				item = k_unlink_head(minioninfo->tfree_list);
				DATA_TASK(item)->tid = ++(minioninfo->next_tid);
				K_WUNLOCK(minioninfo->tfree_list);

				DATA_TASK(item)->chip = chip;
				DATA_TASK(item)->write = true;
				DATA_TASK(item)->address = MINION_SYS_SPI_LED;
				DATA_TASK(item)->task_id = 0;
				DATA_TASK(item)->wsiz = MINION_SYS_SIZ;
				DATA_TASK(item)->rsiz = 0;
				DATA_TASK(item)->wbuf[0] = led & 0xff;
				DATA_TASK(item)->wbuf[1] = (led >> 8) & 0xff;
				DATA_TASK(item)->wbuf[2] = (led >> 16) & 0xff;
				DATA_TASK(item)->wbuf[3] = (led >> 24) & 0xff;
				DATA_TASK(item)->urgent = false;

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
	DATA_TASK(item)->tid = ++(minioninfo->next_tid);
	K_WUNLOCK(minioninfo->tfree_list);

	DATA_TASK(item)->chip = chip;
	DATA_TASK(item)->write = true;
	DATA_TASK(item)->address = MINION_QUE_0;

	// if threaded access to new_work_task() is added, this will need locking
	// Don't use task_id 0 so that we can ignore all '0' work replies
	// ... and report them as errors
	if (minioninfo->next_task_id == 0)
		minioninfo->next_task_id = 1;
	DATA_TASK(item)->task_id = minioninfo->next_task_id;
	DATA_WORK(witem)->task_id = minioninfo->next_task_id;
	minioninfo->next_task_id = (minioninfo->next_task_id + 1) & MINION_MAX_TASK_ID;

	DATA_TASK(item)->urgent = urgent;
	DATA_TASK(item)->work_state = state;
	DATA_TASK(item)->work = DATA_WORK(witem)->work;
	DATA_TASK(item)->witem = witem;

	que = (struct minion_que *)&(DATA_TASK(item)->wbuf[0]);
	que->task_id[0] = DATA_TASK(item)->task_id & 0xff;
	que->task_id[1] = (DATA_TASK(item)->task_id & 0xff00) >> 8;

	memcpy(&(que->midstate[0]), &(DATA_WORK(witem)->work->midstate[0]), MIDSTATE_BYTES);
	memcpy(&(que->merkle7[0]), &(DATA_WORK(witem)->work->data[MERKLE7_OFFSET]), MERKLE_BYTES);

	DATA_TASK(item)->wsiz = (int)sizeof(*que);
	DATA_TASK(item)->rsiz = 0;

	K_WLOCK(minioninfo->wque_list[chip]);
	k_add_head(minioninfo->wque_list[chip], witem);
	minioninfo->chip_status[chip].quework++;
#if MINION_SHOW_IO
	applog(IOCTRL_LOG, "Que chip %d cw=%u rw=%u qw+1=%u",
			   chip,
			   minioninfo->chip_status[chip].chipwork,
			   minioninfo->chip_status[chip].realwork,
			   minioninfo->chip_status[chip].quework);
#endif
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
	struct timeval now;
	double howlong;
	int i;

	K_WLOCK(minioninfo->wwork_list);
	item = k_unlink_tail(minioninfo->wwork_list);
	K_WUNLOCK(minioninfo->wwork_list);
	if (item) {
		cgtime(&now);
		howlong = tdiff(&now, &(DATA_WORK(item)->created));
		minioninfo->que_work++;
		minioninfo->que_time += howlong;
		if (minioninfo->que_min == 0 || minioninfo->que_min > howlong)
			minioninfo->que_min = howlong;
		else if (minioninfo->que_max < howlong)
			minioninfo->que_max = howlong;
		for (i = 0; i < TIME_BANDS; i++) {
			if (howlong < time_bands[i]) {
				minioninfo->que_bands[i]++;
				break;
			}
		}
		if (i >= TIME_BANDS)
			minioninfo->que_bands[TIME_BANDS]++;
	}

	return item;
}

static void minion_do_work(struct cgpu_info *minioncgpu)
{
	struct minion_info *minioninfo = (struct minion_info *)(minioncgpu->device_data);
	int count, chip, j, lowcount;
	TASK_ITEM fifo_task;
	uint8_t state, cmd;
	K_ITEM *item;
#if ENABLE_INT_NONO
	K_ITEM *task;
#endif
	bool islow, sentwork;

	fifo_task.chip = 0;
	fifo_task.write = false;
	fifo_task.address = MINION_SYS_FIFO_STA;
	fifo_task.wsiz = 0;
	fifo_task.rsiz = MINION_SYS_SIZ;

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
		for (chip = 0; chip < (int)MINION_CHIPS; chip++)
			minioninfo->chip_status[chip].tohigh = false;

		for (chip = 0; chip < (int)MINION_CHIPS; chip++) {
			if (minioninfo->has_chip[chip] && !minioninfo->chip_status[chip].overheat) {
				struct timeval now;
				double howlong;
				cgtime(&now);
				howlong = tdiff(&now, &(minioninfo->last_reset[chip]));
				if (howlong < MINION_RESET_DELAY_s)
					continue;

				int tries = 0;
				while (tries++ < 4) {
					cmd = 0;
					fifo_task.chip = chip;
					fifo_task.reply = 0;
					minion_txrx(&fifo_task);
					if (fifo_task.reply <= 0) {
						if (fifo_task.reply < (int)(fifo_task.osiz)) {
							char *buf = bin2hex((unsigned char *)(&(fifo_task.rbuf[fifo_task.osiz - fifo_task.rsiz])),
										(int)(fifo_task.rsiz));
							applog(LOG_ERR, "%s%i: Chip %d Bad fifo reply (%s) size %d, should be %d",
									minioncgpu->drv->name, minioncgpu->device_id,
									chip, buf,
									fifo_task.reply, (int)(fifo_task.osiz));
							free(buf);
							minioninfo->spi_errors++;
							minioninfo->fifo_spi_errors[chip]++;
							minioninfo->res_err_count[chip]++;
						} else {
							if (fifo_task.reply > (int)(fifo_task.osiz)) {
								applog(LOG_ERR, "%s%i: Chip %d Unexpected fifo reply size %d, expected only %d",
										minioncgpu->drv->name, minioncgpu->device_id,
										chip, fifo_task.reply, (int)(fifo_task.osiz));
							}
							cmd = FIFO_CMD(fifo_task.rbuf, fifo_task.osiz - fifo_task.rsiz);
							// valid reply?
							if (cmd < MINION_QUE_MAX) {
								K_WLOCK(minioninfo->wchip_list[chip]);
								minioninfo->chip_status[chip].realwork = cmd;
								K_WUNLOCK(minioninfo->wchip_list[chip]);
								if (cmd <= MINION_QUE_LOW || cmd >= MINION_QUE_HIGH) {
									applog(LOG_DEBUG, "%s%i: Chip %d fifo cmd %d",
											  minioncgpu->drv->name,
											  minioncgpu->device_id,
											  chip, (int)cmd);
								}
								break;
							}

							applog(LOG_ERR, "%s%i: Chip %d Bad fifo reply cmd %d (max is %d)",
									minioncgpu->drv->name, minioncgpu->device_id,
									chip, (int)cmd, MINION_QUE_MAX);
							minioninfo->spi_errors++;
							minioninfo->fifo_spi_errors[chip]++;
							minioninfo->res_err_count[chip]++;
						}
					}
				}

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
										   DATA_WORK(item)->task_id, chip);
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
											   DATA_WORK(item)->task_id, chip);
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
											   DATA_WORK(item)->task_id, chip);
								} else {
									applog(LOG_DEBUG, "%s%i: chip %d non-urgent hi "
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
				if (minioninfo->has_chip[chip] && minioninfo->chip_status[chip].overheat && state == 2)
					sys_chip_sta(minioncgpu, chip);
		}
	}

	sentwork = sentwork;
#if ENABLE_INT_NONO
	if (sentwork) {
		// Clear CMD interrupt since we've now sent more
		K_WLOCK(minioninfo->tfree_list);
		task = k_unlink_head(minioninfo->tfree_list);
		DATA_TASK(task)->tid = ++(minioninfo->next_tid);
		DATA_TASK(task)->chip = 0; // ignored
		DATA_TASK(task)->write = true;
		DATA_TASK(task)->address = MINION_SYS_INT_CLR;
		DATA_TASK(task)->task_id = 0; // ignored
		DATA_TASK(task)->wsiz = MINION_SYS_SIZ;
		DATA_TASK(task)->rsiz = 0;
		DATA_TASK(task)->wbuf[0] = MINION_CMD_INT;
		DATA_TASK(task)->wbuf[1] = 0;
		DATA_TASK(task)->wbuf[2] = 0;
		DATA_TASK(task)->wbuf[3] = 0;
		DATA_TASK(task)->urgent = false;
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

	minioninfo->thr = thr;
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

	for (i = 0; i < (int)MINION_CHIPS; i++)
		if (minioninfo->has_chip[i])
// TODO:		minion_shutdown(minioncgpu, minioninfo, i);
			i = i;

	minioncgpu->shutdown = true;
}

static bool minion_queue_full(struct cgpu_info *minioncgpu)
{
	struct minion_info *minioninfo = (struct minion_info *)(minioncgpu->device_data);
	struct work *work, *usework;
	int count, totneed, need, roll, roll_limit, chip;
	bool ret, rolled;

	if (minioninfo->initialised == false) {
		cgsleep_us(42);
		return true;
	}

	K_RLOCK(minioninfo->wwork_list);
	count = minioninfo->wwork_list->count;
	totneed = 0;
	for (chip = 0; chip < (int)MINION_CHIPS; chip++) {
		if (minioninfo->has_chip[chip] &&
		    !minioninfo->chip_status[chip].overheat) {
			totneed += MINION_QUE_HIGH;
			totneed -= minioninfo->chip_status[chip].quework;
			totneed -= minioninfo->chip_status[chip].realwork;
			// One for the pot :)
			totneed++;
		}
	}
	K_RUNLOCK(minioninfo->wwork_list);

	if (count >= totneed)
		ret = true;
	else {
		need = totneed - count;
		/* Ensure we do enough rolling to reduce CPU
		   but dont roll too much to have them end up stale */
		if (need < 16)
			need = 16;
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

	for (chip = 0; chip < (int)MINION_CHIPS; chip++) {
		if (minioninfo->has_chip[chip]) {
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
	double elapsed, ghs, ghs2, expect, howlong;
	char ghs2_display[64];
	K_ITEM *pitem;
	int msdiff, chip;
	int res_err_count;

	cgtime(&now);
	if (!(minioninfo->chip_chk.tv_sec)) {
		memcpy(&(minioninfo->chip_chk), &now, sizeof(now));
		memcpy(&(minioninfo->chip_rpt), &now, sizeof(now));
		return;
	}

	// Always run the calculations to check chip GHs for the LED
	buf[0] = '\0';
	res_err_msg[0] = '\0';
	res_err_msg[1] = '\0';
	K_RLOCK(minioninfo->hfree_list);
	for (chip = 0; chip < (int)MINION_CHIPS; chip++) {
		if (minioninfo->has_chip[chip]) {
			len = strlen(buf);
			if (minioninfo->hchip_list[chip]->count < 2)
				ghs = 0.0;
			else {
				ghs = 0xffffffffull * (minioninfo->hchip_list[chip]->count - 1);
				ghs /= 1000000000.0;
				ghs /= tdiff(&now, &(DATA_HIST(minioninfo->hchip_list[chip]->tail)->when));
			}
			if (minioninfo->chip_status[chip].first_nonce.tv_sec == 0L ||
			    tdiff(&now, &minioninfo->chip_status[chip].first_nonce) < MINION_LED_TEST_TIME) {
				ghs2_display[0] = '\0';
				minioninfo->setled[chip] = false;
			} else {
				ghs2 = 0xffffffffull * (minioninfo->chip_status[chip].from_first_good - 1);
				ghs2 /= 1000000000.0;
				ghs2 /= tdiff(&now, &minioninfo->chip_status[chip].first_nonce);
				minioninfo->setled[chip] = (ghs2 >= opt_minion_ledlimit);
				snprintf(ghs2_display, sizeof(ghs2_display), "[%.2f]", ghs2);
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
				 " %d=%s%.2f%s", chip, res_err_msg, ghs, ghs2_display);
			minioninfo->history_ghs[chip] = ghs;
		}
	}
	K_RUNLOCK(minioninfo->hfree_list);

	// But only display it if required
	if (opt_minion_chipreport > 0) {
		msdiff = ms_tdiff(&now, &(minioninfo->chip_rpt));
		if (msdiff >= (opt_minion_chipreport * 1000)) {
			memcpy(&(minioninfo->chip_chk), &now, sizeof(now));
			applogsiz(LOG_WARNING, 512,
				  "%s%d: Chip GHs%s",
				  minioncgpu->drv->name, minioncgpu->device_id, buf);
			memcpy(&(minioninfo->chip_rpt), &now, sizeof(now));
		}
	}

	msdiff = ms_tdiff(&now, &(minioninfo->chip_chk));
	if (total_secs >= MINION_RESET_s && msdiff >= (minioninfo->history_gen * 1000)) {
		K_RLOCK(minioninfo->hfree_list);
		for (chip = 0; chip < (int)MINION_CHIPS; chip++) {
			if (minioninfo->has_chip[chip]) {
				// Don't reset the chip while 'changing'
				if (minioninfo->changing[chip])
					continue;

				if (!minioninfo->reset_mark[chip] ||
				    minioninfo->reset_count[chip] < 2) {
					elapsed = 0.0;
					ghs = 0.0;
				} else {
					// 'now' includes that it may have stopped getting nonces
					elapsed = tdiff(&now, &(DATA_HIST(minioninfo->reset_mark[chip])->when));
					ghs = 0xffffffffull * (minioninfo->reset_count[chip] - 1);
					ghs /= 1000000000.0;
					ghs /= elapsed;
				}
				expect = (double)(minioninfo->init_freq[chip]) *
					 MINION_RESET_PERCENT / 1000.0;
				howlong = tdiff(&now, &(minioninfo->last_reset[chip]));
				if (ghs <= expect && howlong >= minioninfo->reset_time[chip]) {
					minioninfo->do_reset[chip] = expect;

					// For now - no lock required since no other code accesses it
					pitem = k_unlink_head(minioninfo->pfree_list);
					DATA_PERF(pitem)->elapsed = elapsed;
					DATA_PERF(pitem)->nonces = minioninfo->reset_count[chip] - 1;
					DATA_PERF(pitem)->freq = minioninfo->init_freq[chip];
					DATA_PERF(pitem)->ghs = ghs;
					memcpy(&(DATA_PERF(pitem)->when), &now, sizeof(now));
					k_add_head(minioninfo->p_list[chip], pitem);
				} else if (second_check) {
					expect = (double)(minioninfo->init_freq[chip]) *
						 MINION_RESET2_PERCENT / 1000.0;
					if (ghs < expect && howlong >= minioninfo->reset2_time[chip]) {
						/* Only do a reset, don't record it, since the ghs
						   is still above MINION_RESET_PERCENT */
						minioninfo->do_reset[chip] = expect;
					}
				}
				minioninfo->history_ghs[chip] = ghs;
				// Expire old perf items to stop clockdown
				if (minioninfo->do_reset[chip] <= 1.0 && howlong > MINION_CLR_s) {
					// Always remember the last reset
					while (minioninfo->p_list[chip]->count > 1) {
						pitem = k_unlink_tail(minioninfo->p_list[chip]);
						k_add_head(minioninfo->pfree_list, pitem);
					}
				}
			}
		}
		K_RUNLOCK(minioninfo->hfree_list);

		memcpy(&(minioninfo->chip_chk), &now, sizeof(now));
	}

	for (chip = 0; chip < (int)MINION_CHIPS; chip++) {
		if (minioninfo->has_chip[chip]) {
			// Don't reset the chip while 'changing'
			if (minioninfo->changing[chip])
				continue;

			if (minioninfo->do_reset[chip] > 1.0 ||
			    minioninfo->flag_reset[chip]) {
				bool std_reset = true;
				int curr_freq = minioninfo->init_freq[chip];
				int new_freq = 0.0;
				int count;

				// Adjust frequency down?
				if (!opt_minion_noautofreq &&
				    minioninfo->p_list[chip]->count >= MINION_RESET_COUNT) {
					pitem = minioninfo->p_list[chip]->head;
					count = 1;
					while (pitem && pitem->next && count++ < MINION_RESET_COUNT) {
						if (DATA_PERF(pitem)->freq != DATA_PERF(pitem->next)->freq)
							break;
						if (count >= MINION_RESET_COUNT) {
							new_freq = minioninfo->init_freq[chip] -
									MINION_FREQ_RESET_STEP;
							if (new_freq < MINION_FREQ_MIN)
								new_freq = MINION_FREQ_MIN;
							if (minioninfo->init_freq[chip] != new_freq) {
								minioninfo->init_freq[chip] = new_freq;
								std_reset = false;
							}
							break;
 						} else
							pitem = pitem->next;
					}
				}

				if (std_reset) {
					if (minioninfo->do_reset[chip] > 1.0) {
						applog(LOG_WARNING, "%s%d: Chip %d %dMHz threshold "
								    "%.2fGHs - resetting",
								    minioncgpu->drv->name,
								    minioncgpu->device_id,
								    chip, curr_freq,
								    minioninfo->do_reset[chip]);
					} else {
						applog(LOG_WARNING, "%s%d: Chip %d %dMhz flagged - "
								    "resetting",
								    minioncgpu->drv->name,
								    minioncgpu->device_id,
								    chip, curr_freq);
					}
				} else {
					if (minioninfo->do_reset[chip] > 1.0) {
						applog(LOG_WARNING, "%s%d: Chip %d %dMHz threshold "
								    "%.2fGHs - resetting to %dMhz",
								    minioncgpu->drv->name,
								    minioncgpu->device_id,
								    chip, curr_freq,
								    minioninfo->do_reset[chip],
								    new_freq);
					} else {
						applog(LOG_WARNING, "%s%d: Chip %d %dMhz flagged - "
								    "resetting to %dMHz",
								    minioncgpu->drv->name,
								    minioncgpu->device_id,
								    chip, curr_freq, new_freq);
					}
				}
				minioninfo->do_reset[chip] = 0.0;
				memcpy(&(minioninfo->last_reset[chip]), &now, sizeof(now));
				init_chip(minioncgpu, minioninfo, chip);
				minioninfo->flag_reset[chip] = false;
			}
		}
	}
}

static int64_t minion_scanwork(__maybe_unused struct thr_info *thr)
{
	struct cgpu_info *minioncgpu = thr->cgpu;
	struct minion_info *minioninfo = (struct minion_info *)(minioncgpu->device_data);
	int64_t hashcount = 0;

	if (minioninfo->initialised == false)
		return hashcount;

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
	for (chip = 0; chip < (int)MINION_CHIPS; chip++) {
		if (minioninfo->has_chip[chip]) {
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
	size_t datalen, nlen;
	int chip, max_chip, que_work, chip_work, temp;

	if (minioninfo->initialised == false)
		return NULL;

	root = api_add_uint64(root, "OK Nonces", &(minioninfo->ok_nonces), true);
	root = api_add_uint64(root, "New Nonces", &(minioninfo->new_nonces), true);
	root = api_add_uint64(root, "Tested Nonces", &(minioninfo->tested_nonces), true);
	root = api_add_uint64(root, "Untested Nonces", &(minioninfo->untested_nonces), true);

	root = api_add_int(root, "Chips", &(minioninfo->chips), true);
	i = MINION_PIN_COUNT;
	root = api_add_int(root, "GPIO Pins", &i, true);

	max_chip = 0;
	for (chip = 0; chip < (int)MINION_CHIPS; chip++)
		if (minioninfo->has_chip[chip]) {
			max_chip = chip;

			snprintf(buf, sizeof(buf), "Chip %d Pin", chip);
			root = api_add_int(root, buf, &(minioninfo->chip_pin[chip]), true);
			snprintf(buf, sizeof(buf), "Chip %d ChipID", chip);
			i = (int)(minioninfo->chipid[chip]);
			root = api_add_int(root, buf, &i, true);
			snprintf(buf, sizeof(buf), "Chip %d Temperature", chip);
			root = api_add_const(root, buf, temp_str(minioninfo->chip_status[chip].temp), false);
			snprintf(buf, sizeof(buf), "Chip %d Cores", chip);
			root = api_add_uint16(root, buf, &(minioninfo->chip_status[chip].cores), true);
			snprintf(buf, sizeof(buf), "Chip %d Frequency", chip);
			root = api_add_uint32(root, buf, &(minioninfo->chip_status[chip].freq), true);
			snprintf(buf, sizeof(buf), "Chip %d InitFreq", chip);
			root = api_add_int(root, buf, &(minioninfo->init_freq[chip]), true);
			snprintf(buf, sizeof(buf), "Chip %d FreqSent", chip);
			root = api_add_hex32(root, buf, &(minioninfo->freqsent[chip]), true);
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

			if (opt_minion_extra) {
				data[0] = '\0';
				datalen = 0;
				for (i = 0; i < MINION_CORES; i++) {
					if (datalen < sizeof(data)) {
						nlen = snprintf(data+datalen, sizeof(data)-datalen,
								"%s%"PRIu64"-%s%"PRIu64,
								i == 0 ? "" : "/",
								minioninfo->core_good[chip][i],
								minioninfo->core_bad[chip][i] ? "'" : "",
								minioninfo->core_bad[chip][i]);
						if (nlen < 1)
							break;
						datalen += nlen;
					}
				}
				snprintf(buf, sizeof(buf), "Chip %d Cores Good-Bad", chip);
				root = api_add_string(root, buf, data, true);
			}

			snprintf(buf, sizeof(buf), "Chip %d History GHs", chip);
			root = api_add_mhs(root, buf, &(minioninfo->history_ghs[chip]), true);
		}

	double his = MINION_HISTORY_s;
	root = api_add_double(root, "History length", &his, true);
	his = MINION_RESET_s;
	root = api_add_double(root, "Default reset length", &his, true);
	his = MINION_RESET2_s;
	root = api_add_double(root, "Default reset2 length", &his, true);
	root = api_add_bool(root, "Reset2 enabled", &second_check, true);

	for (i = 0; i <= max_chip; i += CHIPS_PER_STAT) {
		to = i + CHIPS_PER_STAT - 1;
		if (to > max_chip)
			to = max_chip;

		data[0] = '\0';
		for (j = i; j <= to; j++) {
			snprintf(buf, sizeof(buf),
					"%s%d",
					j == i ? "" : " ",
					minioninfo->has_chip[j] ? 1 : 0);
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
	for (chip = 0; chip <= max_chip; chip++) {
		if (minioninfo->has_chip[chip]) {
			que_work += minioninfo->wque_list[chip]->count;
			chip_work += minioninfo->wchip_list[chip]->count;
		}
	}

	root = api_add_int(root, "WFree Total", &(minioninfo->wfree_list->total), true);
	root = api_add_int(root, "WFree Count", &(minioninfo->wfree_list->count), true);
	root = api_add_int(root, "WWork Count", &(minioninfo->wwork_list->count), true);
	root = api_add_uint64(root, "WWork Flushed", &(minioninfo->wwork_flushed), true);
	root = api_add_int(root, "WQue Count", &que_work, true);
	root = api_add_uint64(root, "WQue Flushed", &(minioninfo->wque_flushed), true);
	root = api_add_int(root, "WChip Count", &chip_work, true);
	root = api_add_uint64(root, "WChip Stale", &(minioninfo->wchip_staled), true);

	root = api_add_int(root, "TFree Total", &(minioninfo->tfree_list->total), true);
	root = api_add_int(root, "TFree Count", &(minioninfo->tfree_list->count), true);
	root = api_add_int(root, "Task Count", &(minioninfo->task_list->count), true);
	root = api_add_int(root, "Reply Count", &(minioninfo->treply_list->count), true);

	root = api_add_int(root, "RFree Total", &(minioninfo->rfree_list->total), true);
	root = api_add_int(root, "RFree Count", &(minioninfo->rfree_list->count), true);
	root = api_add_int(root, "RNonce Count", &(minioninfo->rnonce_list->count), true);

	root = api_add_int(root, "XFree Count", &(minioninfo->xfree_list->count), true);
	root = api_add_int(root, "XFF Count", &(minioninfo->xff_list->count), true);
	root = api_add_uint64(root, "XFFs", &(minioninfo->xffs), true);
	root = api_add_uint64(root, "SPI Resets", &(minioninfo->spi_resets), true);
	root = api_add_uint64(root, "Power Cycles", &(minioninfo->power_cycles), true);

	root = api_add_int(root, "Chip Report", &opt_minion_chipreport, true);
	root = api_add_int(root, "LED Count", &opt_minion_ledcount, true);
	root = api_add_int(root, "LED Limit", &opt_minion_ledlimit, true);
	bool b = !opt_minion_noautofreq;
	root = api_add_bool(root, "Auto Freq", &b, true);
	root = api_add_int(root, "SPI Delay", &opt_minion_spidelay, true);
	root = api_add_bool(root, "SPI Reset I/O", &(minioninfo->spi_reset_io), true);
	root = api_add_int(root, "SPI Reset", &(minioninfo->spi_reset_count), true);
	root = api_add_int(root, "SPI Reset Sleep", &opt_minion_spisleep, true);

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

	double avg;
	root = api_add_uint64(root, "ToQue", &(minioninfo->que_work), true);
	if (minioninfo->que_work)
		avg = minioninfo->que_time / (double)(minioninfo->que_work);
	else
		avg = 0;
	root = api_add_double(root, "Que Avg", &avg, true);
	root = api_add_double(root, "Que Min", &(minioninfo->que_min), true);
	root = api_add_double(root, "Que Max", &(minioninfo->que_max), true);
	data[0] = '\0';
	for (i = 0; i <= TIME_BANDS; i++) {
		snprintf(buf, sizeof(buf),
				"%s%"PRIu64,
				i == 0 ? "" : "/",
				minioninfo->que_bands[i]);
		strcat(data, buf);
	}
	root = api_add_string(root, "Que Bands", data, true);

	root = api_add_uint64(root, "ToTxRx", &(minioninfo->wt_work), true);
	if (minioninfo->wt_work)
		avg = minioninfo->wt_time / (double)(minioninfo->wt_work);
	else
		avg = 0;
	root = api_add_double(root, "TxRx Avg", &avg, true);
	root = api_add_double(root, "TxRx Min", &(minioninfo->wt_min), true);
	root = api_add_double(root, "TxRx Max", &(minioninfo->wt_max), true);
	data[0] = '\0';
	for (i = 0; i <= TIME_BANDS; i++) {
		snprintf(buf, sizeof(buf),
				"%s%"PRIu64,
				i == 0 ? "" : "/",
				minioninfo->wt_bands[i]);
		strcat(data, buf);
	}
	root = api_add_string(root, "TxRx Bands", data, true);

	uint64_t checked, dups;
	dupcounters(minioncgpu, &checked, &dups);
	root = api_add_uint64(root, "Dups", &dups, true);

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
	.set_device = minion_api_set,
	.identify_device = minion_identify,
	.thread_prepare = minion_thread_prepare,
	.hash_work = hash_queued_work,
	.scanwork = minion_scanwork,
	.queue_full = minion_queue_full,
	.flush_work = minion_flush_work,
	.thread_shutdown = minion_shutdown
#endif
};
