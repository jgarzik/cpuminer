/*
 * Copyright 2012-2013 Andrew Smith
 * Copyright 2012 Xiangfu <xiangfu@openmobilefree.com>
 * Copyright 2013-2014 Con Kolivas <kernel@kolivas.org>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 3 of the License, or (at your option)
 * any later version.  See COPYING for more details.
 */

/*
 * Those code should be works fine with V2 and V3 bitstream of Icarus.
 * Operation:
 *   No detection implement.
 *   Input: 64B = 32B midstate + 20B fill bytes + last 12 bytes of block head.
 *   Return: send back 32bits immediately when Icarus found a valid nonce.
 *           no query protocol implemented here, if no data send back in ~11.3
 *           seconds (full cover time on 32bit nonce range by 380MH/s speed)
 *           just send another work.
 * Notice:
 *   1. Icarus will start calculate when you push a work to them, even they
 *      are busy.
 *   2. The 2 FPGAs on Icarus will distribute the job, one will calculate the
 *      0 ~ 7FFFFFFF, another one will cover the 80000000 ~ FFFFFFFF.
 *   3. It's possible for 2 FPGAs both find valid nonce in the meantime, the 2
 *      valid nonce will all be send back.
 *   4. Icarus will stop work when: a valid nonce has been found or 32 bits
 *      nonce range is completely calculated.
 */


#include <float.h>
#include <limits.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <strings.h>
#include <sys/time.h>
#include <unistd.h>
#include <math.h>

#include "config.h"

#ifdef WIN32
#include <windows.h>
#endif

#include "compat.h"
#include "miner.h"
#include "usbutils.h"

// The serial I/O speed - Linux uses a define 'B115200' in bits/termios.h
#define ICARUS_IO_SPEED 115200

#define ICARUS_BUF_SIZE 8
// The size of a successful nonce read
#define ANT_READ_SIZE 5
#define ICARUS_READ_SIZE 4
#define ROCK_READ_SIZE 8

// Ensure the sizes are correct for the Serial read
#if (ICARUS_READ_SIZE != 4)
#error ICARUS_READ_SIZE must be 4
#endif
#define ASSERT1(condition) __maybe_unused static char sizeof_uint32_t_must_be_4[(condition)?1:-1]
ASSERT1(sizeof(uint32_t) == 4);

// TODO: USB? Different calculation? - see usbstats to work it out e.g. 1/2 of normal send time
//  or even use that number? 1/2
// #define ICARUS_READ_TIME(baud) ((double)ICARUS_READ_SIZE * (double)8.0 / (double)(baud))
// maybe 1ms?
#define ICARUS_READ_TIME(baud) (0.001)

// USB ms timeout to wait - user specified timeouts are multiples of this
#define ICA_WAIT_TIMEOUT 100
#define ANT_WAIT_TIMEOUT 10
#define AU3_WAIT_TIMEOUT 1
#define ICARUS_WAIT_TIMEOUT (info->u3 ? AU3_WAIT_TIMEOUT : (info->ant ? ANT_WAIT_TIMEOUT : ICA_WAIT_TIMEOUT))

#define ICARUS_CMR2_TIMEOUT 1

// Defined in multiples of ICARUS_WAIT_TIMEOUT
// Must of course be greater than ICARUS_READ_COUNT_TIMING/ICARUS_WAIT_TIMEOUT
// There's no need to have this bigger, since the overhead/latency of extra work
// is pretty small once you get beyond a 10s nonce range time and 10s also
// means that nothing slower than 429MH/s can go idle so most icarus devices
// will always mine without idling
#define ICARUS_READ_TIME_LIMIT_MAX 100

// In timing mode: Default starting value until an estimate can be obtained
// 5000 ms allows for up to a ~840MH/s device
#define ICARUS_READ_COUNT_TIMING	5000

// Antminer USB is > 1GH/s so use a shorter limit
// 1000 ms allows for up to ~4GH/s device
#define ANTUSB_READ_COUNT_TIMING	1000

#define ANTU3_READ_COUNT_TIMING		100

#define ICARUS_READ_COUNT_MIN		ICARUS_WAIT_TIMEOUT
#define SECTOMS(s)	((int)((s) * 1000))
// How many ms below the expected completion time to abort work
// extra in case the last read is delayed
#define ICARUS_READ_REDUCE	((int)(ICARUS_WAIT_TIMEOUT * 1.5))

// For a standard Icarus REV3 (to 5 places)
// Since this rounds up a the last digit - it is a slight overestimate
// Thus the hash rate will be a VERY slight underestimate
// (by a lot less than the displayed accuracy)
// Minor inaccuracy of these numbers doesn't affect the work done,
// only the displayed MH/s
#define ICARUS_REV3_HASH_TIME 0.0000000026316
#define LANCELOT_HASH_TIME 0.0000000025000
#define ASICMINERUSB_HASH_TIME 0.0000000029761
// TODO: What is it?
#define CAIRNSMORE1_HASH_TIME 0.0000000027000
// Per FPGA
#define CAIRNSMORE2_HASH_TIME 0.0000000066600
#define NANOSEC 1000000000.0
#define ANTMINERUSB_HASH_MHZ 0.000000125
#define ANTMINERUSB_HASH_TIME (ANTMINERUSB_HASH_MHZ / (double)(opt_anu_freq))
#define ANTU3_HASH_MHZ 0.0000000032
#define ANTU3_HASH_TIME (ANTU3_HASH_MHZ / (double)(opt_au3_freq))

#define CAIRNSMORE2_INTS 4

// Icarus Rev3 doesn't send a completion message when it finishes
// the full nonce range, so to avoid being idle we must abort the
// work (by starting a new work item) shortly before it finishes
//
// Thus we need to estimate 2 things:
//	1) How many hashes were done if the work was aborted
//	2) How high can the timeout be before the Icarus is idle,
//		to minimise the number of work items started
//	We set 2) to 'the calculated estimate' - ICARUS_READ_REDUCE
//	to ensure the estimate ends before idle
//
// The simple calculation used is:
//	Tn = Total time in seconds to calculate n hashes
//	Hs = seconds per hash
//	Xn = number of hashes
//	W  = code/usb overhead per work
//
// Rough but reasonable estimate:
//	Tn = Hs * Xn + W	(of the form y = mx + b)
//
// Thus:
//	Line of best fit (using least squares)
//
//	Hs = (n*Sum(XiTi)-Sum(Xi)*Sum(Ti))/(n*Sum(Xi^2)-Sum(Xi)^2)
//	W = Sum(Ti)/n - (Hs*Sum(Xi))/n
//
// N.B. W is less when aborting work since we aren't waiting for the reply
//	to be transferred back (ICARUS_READ_TIME)
//	Calculating the hashes aborted at n seconds is thus just n/Hs
//	(though this is still a slight overestimate due to code delays)
//

// Both below must be exceeded to complete a set of data
// Minimum how long after the first, the last data point must be
#define HISTORY_SEC 60
// Minimum how many points a single ICARUS_HISTORY should have
#define MIN_DATA_COUNT 5
// The value MIN_DATA_COUNT used is doubled each history until it exceeds:
#define MAX_MIN_DATA_COUNT 100

static struct timeval history_sec = { HISTORY_SEC, 0 };

// Store the last INFO_HISTORY data sets
// [0] = current data, not yet ready to be included as an estimate
// Each new data set throws the last old set off the end thus
// keeping a ongoing average of recent data
#define INFO_HISTORY 10

struct ICARUS_HISTORY {
	struct timeval finish;
	double sumXiTi;
	double sumXi;
	double sumTi;
	double sumXi2;
	uint32_t values;
	uint32_t hash_count_min;
	uint32_t hash_count_max;
};

enum timing_mode { MODE_DEFAULT, MODE_SHORT, MODE_LONG, MODE_VALUE };

static const char *MODE_DEFAULT_STR = "default";
static const char *MODE_SHORT_STR = "short";
static const char *MODE_SHORT_STREQ = "short=";
static const char *MODE_LONG_STR = "long";
static const char *MODE_LONG_STREQ = "long=";
static const char *MODE_VALUE_STR = "value";
static const char *MODE_UNKNOWN_STR = "unknown";

#define MAX_DEVICE_NUM 100
#define MAX_WORK_BUFFER_SIZE 2
#define MAX_CHIP_NUM 24
// Set it to 3, 5 or 9
#define	NONCE_CORRECTION_TIMES	5
#define MAX_TRIES	4
#define	RM_CMD_MASK		0x0F
#define	RM_STATUS_MASK		0xF0
#define	RM_CHIP_MASK		0x3F
#define	RM_PRODUCT_MASK		0xC0
#define	RM_PRODUCT_RBOX		0x00
#define	RM_PRODUCT_T1		0x40
#define	RM_PRODUCT_T2		0x80
#define	RM_PRODUCT_TEST		0xC0

#if (NONCE_CORRECTION_TIMES == 5)
static int32_t rbox_corr_values[] = {0, 1, -1, -2, -4};
#endif
#if (NONCE_CORRECTION_TIMES == 9)
static int32_t rbox_corr_values[] = {0, 1, -1, 2, -2, 3, -3, 4, -4};
#endif
#if (NONCE_CORRECTION_TIMES == 3)
static int32_t rbox_corr_values[] = {0, 1, -1};
#endif

#define ANT_QUEUE_NUM 36

typedef enum {
	NONCE_DATA1_OFFSET = 0,
	NONCE_DATA2_OFFSET,
	NONCE_DATA3_OFFSET,
	NONCE_DATA4_OFFSET,
	NONCE_TASK_CMD_OFFSET,
	NONCE_CHIP_NO_OFFSET,
	NONCE_TASK_NO_OFFSET,
	NONCE_COMMAND_OFFSET,
	NONCE_MAX_OFFSET
} NONCE_OFFSET;

typedef enum {
	NONCE_DATA_CMD = 0,
	NONCE_TASK_COMPLETE_CMD,
	NONCE_GET_TASK_CMD,
} NONCE_COMMAND;

typedef struct nonce_data {
	int chip_no;
	unsigned int task_no ;
	unsigned char work_state;
	int cmd_value;
} NONCE_DATA;

typedef enum {
	ROCKMINER_RBOX = 0,
	ROCKMINER_T1,
	ROCKMINER_T2,
	ROCKMINER_MAX
} ROCKMINER_PRODUCT_T;

typedef struct rockminer_chip_info {
	unsigned char freq;
	int error_cnt;
	time_t last_received_task_complete_time;
} ROCKMINER_CHIP_INFO;

typedef struct rockminer_device_info {
	unsigned char detect_chip_no;
	unsigned char chip_max;
	unsigned char product_id;
	float min_frq;
	float def_frq;
	float max_frq;
	ROCKMINER_CHIP_INFO chip[MAX_CHIP_NUM];
	time_t dev_detect_time;
} ROCKMINER_DEVICE_INFO;

struct ICARUS_INFO {
	enum sub_ident ident;
	int intinfo;

	// time to calculate the golden_ob
	uint64_t golden_hashes;
	struct timeval golden_tv;

	struct ICARUS_HISTORY history[INFO_HISTORY+1];
	uint32_t min_data_count;

	int timeout;

	// seconds per Hash
	double Hs;
	// ms til we abort
	int read_time;
	// ms limit for (short=/long=) read_time
	int read_time_limit;
	// How long without hashes is considered a failed device
	int fail_time;

	enum timing_mode timing_mode;
	bool do_icarus_timing;

	double fullnonce;
	int count;
	double W;
	uint32_t values;
	uint64_t hash_count_range;

	// Determine the cost of history processing
	// (which will only affect W)
	uint64_t history_count;
	struct timeval history_time;

	// icarus-options
	int baud;
	int work_division;
	int fpga_count;
	uint32_t nonce_mask;

	uint8_t cmr2_speed;
	bool speed_next_work;
	bool flash_next_work;

	int nonce_size;

	bool failing;

	pthread_mutex_t lock;

	ROCKMINER_DEVICE_INFO rmdev;
	struct work *base_work; // For when we roll work
	struct work *g_work[MAX_CHIP_NUM][MAX_WORK_BUFFER_SIZE];
	uint32_t last_nonce[MAX_CHIP_NUM][MAX_WORK_BUFFER_SIZE];
	char rock_init[64];
	uint64_t nonces_checked;
	uint64_t nonces_correction_times;
	uint64_t nonces_correction_tests;
	uint64_t nonces_fail;
	uint64_t nonces_correction[NONCE_CORRECTION_TIMES];

	struct work **antworks;
	int nonces;
	int workid;
	bool ant;
	bool u3;
};

#define ICARUS_MIDSTATE_SIZE 32
#define ICARUS_UNUSED_SIZE 16
#define ICARUS_WORK_SIZE 12

#define ICARUS_WORK_DATA_OFFSET 64

#define ICARUS_CMR2_SPEED_FACTOR 2.5
#define ICARUS_CMR2_SPEED_MIN_INT 100
#define ICARUS_CMR2_SPEED_DEF_INT 180
#define ICARUS_CMR2_SPEED_MAX_INT 220
#define CMR2_INT_TO_SPEED(_speed) ((uint8_t)((float)_speed / ICARUS_CMR2_SPEED_FACTOR))
#define ICARUS_CMR2_SPEED_MIN CMR2_INT_TO_SPEED(ICARUS_CMR2_SPEED_MIN_INT)
#define ICARUS_CMR2_SPEED_DEF CMR2_INT_TO_SPEED(ICARUS_CMR2_SPEED_DEF_INT)
#define ICARUS_CMR2_SPEED_MAX CMR2_INT_TO_SPEED(ICARUS_CMR2_SPEED_MAX_INT)
#define ICARUS_CMR2_SPEED_INC 1
#define ICARUS_CMR2_SPEED_DEC -1
#define ICARUS_CMR2_SPEED_FAIL -10

#define ICARUS_CMR2_PREFIX ((uint8_t)0xB7)
#define ICARUS_CMR2_CMD_SPEED ((uint8_t)0)
#define ICARUS_CMR2_CMD_FLASH ((uint8_t)1)
#define ICARUS_CMR2_DATA_FLASH_OFF ((uint8_t)0)
#define ICARUS_CMR2_DATA_FLASH_ON ((uint8_t)1)
#define ICARUS_CMR2_CHECK ((uint8_t)0x6D)

#define ANT_UNUSED_SIZE 15

struct ICARUS_WORK {
	uint8_t midstate[ICARUS_MIDSTATE_SIZE];
	// These 4 bytes are for CMR2 bitstreams that handle MHz adjustment
	uint8_t check;
	uint8_t data;
	uint8_t cmd;
	uint8_t prefix;
	uint8_t unused[ANT_UNUSED_SIZE];
	uint8_t id; // Used only by ANT, otherwise unused by other icarus
	uint8_t work[ICARUS_WORK_SIZE];
};

#define ANT_U1_DEFFREQ 200
#define ANT_U3_DEFFREQ 225
#define ANT_U3_MAXFREQ 250
struct {
	float freq;
	uint16_t hex;
} u3freqtable[] = {
	{ 100,		0x0783 },
	{ 125,		0x0983 },
	{ 150,		0x0b83 },
	{ 175,		0x0d83 },
	{ 193.75,	0x0f03 },
	{ 196.88,	0x1f07 },
	{ 200,		0x0782 },
	{ 206.25,	0x1006 },
	{ 212.5,	0x1086 },
	{ 218.75,	0x1106 },
	{ 225,		0x0882 },
	{ 237.5,	0x1286 },
	{ 243.75,	0x1306 },
	{ 250,		0x0982 },
};

#define END_CONDITION 0x0000ffff

// Looking for options in --icarus-timing and --icarus-options:
//
// Code increments this each time we start to look at a device
// However, this means that if other devices are checked by
// the Icarus code (e.g. Avalon only as at 20130517)
// they will count in the option offset
//
// This, however, is deterministic so that's OK
//
// If we were to increment after successfully finding an Icarus
// that would be random since an Icarus may fail and thus we'd
// not be able to predict the option order
//
// Devices are checked in the order libusb finds them which is ?
//
static int option_offset = -1;

/*
#define ICA_BUFSIZ (0x200)

static void transfer_read(struct cgpu_info *icarus, uint8_t request_type, uint8_t bRequest, uint16_t wValue, uint16_t wIndex, char *buf, int bufsiz, int *amount, enum usb_cmds cmd)
{
	int err;

	err = usb_transfer_read(icarus, request_type, bRequest, wValue, wIndex, buf, bufsiz, amount, cmd);

	applog(LOG_DEBUG, "%s: cgid %d %s got err %d",
			icarus->drv->name, icarus->cgminer_id,
			usb_cmdname(cmd), err);
}
*/

static void _transfer(struct cgpu_info *icarus, uint8_t request_type, uint8_t bRequest, uint16_t wValue, uint16_t wIndex, uint32_t *data, int siz, enum usb_cmds cmd)
{
	int err;

	err = usb_transfer_data(icarus, request_type, bRequest, wValue, wIndex, data, siz, cmd);

	applog(LOG_DEBUG, "%s: cgid %d %s got err %d",
			icarus->drv->name, icarus->cgminer_id,
			usb_cmdname(cmd), err);
}

#define transfer(icarus, request_type, bRequest, wValue, wIndex, cmd) \
		_transfer(icarus, request_type, bRequest, wValue, wIndex, NULL, 0, cmd)

static void icarus_initialise(struct cgpu_info *icarus, int baud)
{
	struct ICARUS_INFO *info = (struct ICARUS_INFO *)(icarus->device_data);
	uint16_t wValue, wIndex;
	enum sub_ident ident;
	int interface;

	if (icarus->usbinfo.nodev)
		return;

	interface = _usb_interface(icarus, info->intinfo);
	ident = usb_ident(icarus);

	switch (ident) {
		case IDENT_BLT:
		case IDENT_LLT:
		case IDENT_CMR1:
		case IDENT_CMR2:
			// Reset
			transfer(icarus, FTDI_TYPE_OUT, FTDI_REQUEST_RESET, FTDI_VALUE_RESET,
				 interface, C_RESET);

			if (icarus->usbinfo.nodev)
				return;

			// Latency
			_usb_ftdi_set_latency(icarus, info->intinfo);

			if (icarus->usbinfo.nodev)
				return;

			// Set data control
			transfer(icarus, FTDI_TYPE_OUT, FTDI_REQUEST_DATA, FTDI_VALUE_DATA_BLT,
				 interface, C_SETDATA);

			if (icarus->usbinfo.nodev)
				return;

			// default to BLT/LLT 115200
			wValue = FTDI_VALUE_BAUD_BLT;
			wIndex = FTDI_INDEX_BAUD_BLT;

			if (ident == IDENT_CMR1 || ident == IDENT_CMR2) {
				switch (baud) {
					case 115200:
						wValue = FTDI_VALUE_BAUD_CMR_115;
						wIndex = FTDI_INDEX_BAUD_CMR_115;
						break;
					case 57600:
						wValue = FTDI_VALUE_BAUD_CMR_57;
						wIndex = FTDI_INDEX_BAUD_CMR_57;
						break;
					default:
						quit(1, "icarus_intialise() invalid baud (%d) for Cairnsmore1", baud);
						break;
				}
			}

			// Set the baud
			transfer(icarus, FTDI_TYPE_OUT, FTDI_REQUEST_BAUD, wValue,
				 (wIndex & 0xff00) | interface, C_SETBAUD);

			if (icarus->usbinfo.nodev)
				return;

			// Set Modem Control
			transfer(icarus, FTDI_TYPE_OUT, FTDI_REQUEST_MODEM, FTDI_VALUE_MODEM,
				 interface, C_SETMODEM);

			if (icarus->usbinfo.nodev)
				return;

			// Set Flow Control
			transfer(icarus, FTDI_TYPE_OUT, FTDI_REQUEST_FLOW, FTDI_VALUE_FLOW,
				 interface, C_SETFLOW);

			if (icarus->usbinfo.nodev)
				return;

			// Clear any sent data
			transfer(icarus, FTDI_TYPE_OUT, FTDI_REQUEST_RESET, FTDI_VALUE_PURGE_TX,
				 interface, C_PURGETX);

			if (icarus->usbinfo.nodev)
				return;

			// Clear any received data
			transfer(icarus, FTDI_TYPE_OUT, FTDI_REQUEST_RESET, FTDI_VALUE_PURGE_RX,
				 interface, C_PURGERX);
			break;
		case IDENT_ICA:
			// Set Data Control
			transfer(icarus, PL2303_CTRL_OUT, PL2303_REQUEST_CTRL, PL2303_VALUE_CTRL,
				 interface, C_SETDATA);

			if (icarus->usbinfo.nodev)
				return;

			// Set Line Control
			uint32_t ica_data[2] = { PL2303_VALUE_LINE0, PL2303_VALUE_LINE1 };
			_transfer(icarus, PL2303_CTRL_OUT, PL2303_REQUEST_LINE, PL2303_VALUE_LINE,
				 interface, &ica_data[0], PL2303_VALUE_LINE_SIZE, C_SETLINE);

			if (icarus->usbinfo.nodev)
				return;

			// Vendor
			transfer(icarus, PL2303_VENDOR_OUT, PL2303_REQUEST_VENDOR, PL2303_VALUE_VENDOR,
				 interface, C_VENDOR);
			break;
		case IDENT_AMU:
		case IDENT_ANU:
		case IDENT_AU3:
		case IDENT_LIN:
			// Enable the UART
			transfer(icarus, CP210X_TYPE_OUT, CP210X_REQUEST_IFC_ENABLE,
				 CP210X_VALUE_UART_ENABLE,
				 interface, C_ENABLE_UART);

			if (icarus->usbinfo.nodev)
				return;

			// Set data control
			transfer(icarus, CP210X_TYPE_OUT, CP210X_REQUEST_DATA, CP210X_VALUE_DATA,
				 interface, C_SETDATA);

			if (icarus->usbinfo.nodev)
				return;

			// Set the baud
			uint32_t data = CP210X_DATA_BAUD;
			_transfer(icarus, CP210X_TYPE_OUT, CP210X_REQUEST_BAUD, 0,
				 interface, &data, sizeof(data), C_SETBAUD);
			break;
 		case IDENT_AVA:
 			break;
		default:
			quit(1, "icarus_intialise() called with invalid %s cgid %i ident=%d",
				icarus->drv->name, icarus->cgminer_id, ident);
	}
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

#define ICA_NONCE_ERROR -1
#define ICA_NONCE_OK 0
#define ICA_NONCE_RESTART 1
#define ICA_NONCE_TIMEOUT 2

static int icarus_get_nonce(struct cgpu_info *icarus, unsigned char *buf, struct timeval *tv_start,
			    struct timeval *tv_finish, struct thr_info *thr, int read_time)
{
	struct ICARUS_INFO *info = (struct ICARUS_INFO *)(icarus->device_data);
	int err, amt, rc;

	if (icarus->usbinfo.nodev)
		return ICA_NONCE_ERROR;

	cgtime(tv_start);
	err = usb_read_ii_timeout_cancellable(icarus, info->intinfo, (char *)buf,
					      info->nonce_size, &amt, read_time,
					      C_GETRESULTS);
	cgtime(tv_finish);

	if (err < 0 && err != LIBUSB_ERROR_TIMEOUT) {
		applog(LOG_ERR, "%s %i: Comms error (rerr=%d amt=%d)", icarus->drv->name,
		       icarus->device_id, err, amt);
		dev_error(icarus, REASON_DEV_COMMS_ERROR);
		return ICA_NONCE_ERROR;
	}

	if (amt >= info->nonce_size)
		return ICA_NONCE_OK;

	rc = SECTOMS(tdiff(tv_finish, tv_start));
	if (thr && thr->work_restart) {
		applog(LOG_DEBUG, "Icarus Read: Work restart at %d ms", rc);
		return ICA_NONCE_RESTART;
	}

	if (amt > 0)
		applog(LOG_DEBUG, "Icarus Read: Timeout reading for %d ms", rc);
	else
		applog(LOG_DEBUG, "Icarus Read: No data for %d ms", rc);
	return ICA_NONCE_TIMEOUT;
}


static const char *timing_mode_str(enum timing_mode timing_mode)
{
	switch(timing_mode) {
	case MODE_DEFAULT:
		return MODE_DEFAULT_STR;
	case MODE_SHORT:
		return MODE_SHORT_STR;
	case MODE_LONG:
		return MODE_LONG_STR;
	case MODE_VALUE:
		return MODE_VALUE_STR;
	default:
		return MODE_UNKNOWN_STR;
	}
}

static void set_timing_mode(int this_option_offset, struct cgpu_info *icarus)
{
	struct ICARUS_INFO *info = (struct ICARUS_INFO *)(icarus->device_data);
	int read_count_timing = 0;
	enum sub_ident ident;
	double Hs, fail_time;
	char buf[BUFSIZ+1];
	char *ptr, *comma, *eq;
	size_t max;
	int i;

	if (opt_icarus_timing == NULL)
		buf[0] = '\0';
	else {
		ptr = opt_icarus_timing;
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

	ident = usb_ident(icarus);
	switch (ident) {
		case IDENT_ICA:
		case IDENT_AVA:
			info->Hs = ICARUS_REV3_HASH_TIME;
			read_count_timing = ICARUS_READ_COUNT_TIMING;
			break;
		case IDENT_BLT:
		case IDENT_LLT:
			info->Hs = LANCELOT_HASH_TIME;
			read_count_timing = ICARUS_READ_COUNT_TIMING;
			break;
		case IDENT_AMU:
			info->Hs = ASICMINERUSB_HASH_TIME;
			read_count_timing = ICARUS_READ_COUNT_TIMING;
			break;
		case IDENT_CMR1:
			info->Hs = CAIRNSMORE1_HASH_TIME;
			read_count_timing = ICARUS_READ_COUNT_TIMING;
			break;
		case IDENT_CMR2:
			info->Hs = CAIRNSMORE2_HASH_TIME;
			read_count_timing = ICARUS_READ_COUNT_TIMING;
			break;
		case IDENT_ANU:
			info->Hs = ANTMINERUSB_HASH_TIME;
			read_count_timing = ANTUSB_READ_COUNT_TIMING;
			break;
		case IDENT_AU3:
			info->Hs = ANTU3_HASH_TIME;
			read_count_timing = ANTU3_READ_COUNT_TIMING;
			break;
		default:
			quit(1, "Icarus get_options() called with invalid %s ident=%d",
				icarus->drv->name, ident);
	}

	info->read_time = 0;
	info->read_time_limit = 0; // 0 = no limit

	if (strcasecmp(buf, MODE_SHORT_STR) == 0) {
		// short
		info->read_time = read_count_timing;

		info->timing_mode = MODE_SHORT;
		info->do_icarus_timing = true;
	} else if (strncasecmp(buf, MODE_SHORT_STREQ, strlen(MODE_SHORT_STREQ)) == 0) {
		// short=limit
		info->read_time = read_count_timing;

		info->timing_mode = MODE_SHORT;
		info->do_icarus_timing = true;

		info->read_time_limit = atoi(&buf[strlen(MODE_SHORT_STREQ)]);
		if (info->read_time_limit < 0)
			info->read_time_limit = 0;
		if (info->read_time_limit > ICARUS_READ_TIME_LIMIT_MAX)
			info->read_time_limit = ICARUS_READ_TIME_LIMIT_MAX;
	} else if (strcasecmp(buf, MODE_LONG_STR) == 0) {
		// long
		info->read_time = read_count_timing;

		info->timing_mode = MODE_LONG;
		info->do_icarus_timing = true;
	} else if (strncasecmp(buf, MODE_LONG_STREQ, strlen(MODE_LONG_STREQ)) == 0) {
		// long=limit
		info->read_time = read_count_timing;

		info->timing_mode = MODE_LONG;
		info->do_icarus_timing = true;

		info->read_time_limit = atoi(&buf[strlen(MODE_LONG_STREQ)]);
		if (info->read_time_limit < 0)
			info->read_time_limit = 0;
		if (info->read_time_limit > ICARUS_READ_TIME_LIMIT_MAX)
			info->read_time_limit = ICARUS_READ_TIME_LIMIT_MAX;
	} else if ((Hs = atof(buf)) != 0) {
		// ns[=read_time]
		info->Hs = Hs / NANOSEC;
		info->fullnonce = info->Hs * (((double)0xffffffff) + 1);

		if ((eq = strchr(buf, '=')) != NULL)
			info->read_time = atoi(eq+1) * ICARUS_WAIT_TIMEOUT;

		if (info->read_time < ICARUS_READ_COUNT_MIN)
			info->read_time = SECTOMS(info->fullnonce) - ICARUS_READ_REDUCE;

		if (unlikely(info->read_time < ICARUS_READ_COUNT_MIN))
			info->read_time = ICARUS_READ_COUNT_MIN;

		info->timing_mode = MODE_VALUE;
		info->do_icarus_timing = false;
	} else {
		// Anything else in buf just uses DEFAULT mode

		info->fullnonce = info->Hs * (((double)0xffffffff) + 1);

		if ((eq = strchr(buf, '=')) != NULL)
			info->read_time = atoi(eq+1) * ICARUS_WAIT_TIMEOUT;

		if (info->read_time < ICARUS_READ_COUNT_MIN)
			info->read_time = SECTOMS(info->fullnonce) - ICARUS_READ_REDUCE;

		if (unlikely(info->read_time < ICARUS_READ_COUNT_MIN))
			info->read_time = ICARUS_READ_COUNT_MIN;

		info->timing_mode = MODE_DEFAULT;
		info->do_icarus_timing = false;
	}

	info->min_data_count = MIN_DATA_COUNT;

	// All values are in multiples of ICARUS_WAIT_TIMEOUT
	info->read_time_limit *= ICARUS_WAIT_TIMEOUT;

	applog(LOG_DEBUG, "%s: cgid %d Init: mode=%s read_time=%dms limit=%dms Hs=%e",
			icarus->drv->name, icarus->cgminer_id,
			timing_mode_str(info->timing_mode),
			info->read_time, info->read_time_limit, info->Hs);

	/* Set the time to detect a dead device to 30 full nonce ranges. */
	fail_time = info->Hs * 0xffffffffull * 30.0;
	/* Integer accuracy is definitely enough. */
	info->fail_time = fail_time;
}

static uint32_t mask(int work_division)
{
	uint32_t nonce_mask = 0x7fffffff;

	// yes we can calculate these, but this way it's easy to see what they are
	switch (work_division) {
	case 1:
		nonce_mask = 0xffffffff;
		break;
	case 2:
		nonce_mask = 0x7fffffff;
		break;
	case 4:
		nonce_mask = 0x3fffffff;
		break;
	case 8:
		nonce_mask = 0x1fffffff;
		break;
	default:
		quit(1, "Invalid2 icarus-options for work_division (%d) must be 1, 2, 4 or 8", work_division);
	}

	return nonce_mask;
}

static void get_options(int this_option_offset, struct cgpu_info *icarus, int *baud, int *work_division, int *fpga_count)
{
	char buf[BUFSIZ+1];
	char *ptr, *comma, *colon, *colon2;
	enum sub_ident ident;
	size_t max;
	int i, tmp;

	if (opt_icarus_options == NULL)
		buf[0] = '\0';
	else {
		ptr = opt_icarus_options;
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

	ident = usb_ident(icarus);
	switch (ident) {
		case IDENT_ICA:
		case IDENT_BLT:
		case IDENT_LLT:
		case IDENT_AVA:
			*baud = ICARUS_IO_SPEED;
			*work_division = 2;
			*fpga_count = 2;
			break;
		case IDENT_AMU:
		case IDENT_ANU:
		case IDENT_AU3:
			*baud = ICARUS_IO_SPEED;
			*work_division = 1;
			*fpga_count = 1;
			break;
		case IDENT_CMR1:
			*baud = ICARUS_IO_SPEED;
			*work_division = 2;
			*fpga_count = 2;
			break;
		case IDENT_CMR2:
			*baud = ICARUS_IO_SPEED;
			*work_division = 1;
			*fpga_count = 1;
			break;
		default:
			quit(1, "Icarus get_options() called with invalid %s ident=%d",
				icarus->drv->name, ident);
	}

	if (*buf) {
		colon = strchr(buf, ':');
		if (colon)
			*(colon++) = '\0';

		if (*buf) {
			tmp = atoi(buf);
			switch (tmp) {
			case 115200:
				*baud = 115200;
				break;
			case 57600:
				*baud = 57600;
				break;
			default:
				quit(1, "Invalid icarus-options for baud (%s) must be 115200 or 57600", buf);
			}
		}

		if (colon && *colon) {
			colon2 = strchr(colon, ':');
			if (colon2)
				*(colon2++) = '\0';

			if (*colon) {
				tmp = atoi(colon);
				if (tmp == 1 || tmp == 2 || tmp == 4 || tmp == 8) {
					*work_division = tmp;
					*fpga_count = tmp;	// default to the same
				} else {
					quit(1, "Invalid icarus-options for work_division (%s) must be 1, 2, 4 or 8", colon);
				}
			}

			if (colon2 && *colon2) {
				tmp = atoi(colon2);
				if (tmp > 0 && tmp <= *work_division)
					*fpga_count = tmp;
				else {
					quit(1, "Invalid icarus-options for fpga_count (%s) must be >0 and <=work_division (%d)", colon2, *work_division);
				}
			}
		}
	}
}

unsigned char crc5(unsigned char *ptr, unsigned char len)
{
	unsigned char i, j, k;
	unsigned char crc = 0x1f;

	unsigned char crcin[5] = {1, 1, 1, 1, 1};
	unsigned char crcout[5] = {1, 1, 1, 1, 1};
	unsigned char din = 0;

	j = 0x80;
	k = 0;
	for (i = 0; i < len; i++) {
		if (*ptr & j)
			din = 1;
		else
			din = 0;
		crcout[0] = crcin[4] ^ din;
		crcout[1] = crcin[0];
		crcout[2] = crcin[1] ^ crcin[4] ^ din;
		crcout[3] = crcin[2];
		crcout[4] = crcin[3];

		j = j >> 1;
		k++;
		if (k == 8) {
			j = 0x80;
			k = 0;
			ptr++;
		}
		memcpy(crcin, crcout, 5);
	}
	crc = 0;
	if (crcin[4])
		crc |= 0x10;
	if (crcin[3])
		crc |= 0x08;
	if (crcin[2])
		crc |= 0x04;
	if (crcin[1])
		crc |= 0x02;
	if (crcin[0])
		crc |= 0x01;
	return crc;
}

static uint16_t anu_find_freqhex(void)
{
	float fout, best_fout = opt_anu_freq;
	int od, nf, nr, no, n, m, bs;
	uint16_t anu_freq_hex = 0;
	float best_diff = 1000;

	if (!best_fout)
		best_fout = ANT_U1_DEFFREQ;

	for (od = 0; od < 4; od++) {
		no = 1 << od;
		for (n = 0; n < 16; n++) {
			nr = n + 1;
			for (m = 0; m < 64; m++) {
				nf = m + 1;
				fout = 25 * (float)nf /((float)(nr) * (float)(no));
				if (fabsf(fout - opt_anu_freq)  > best_diff)
					continue;
				if (500 <= (fout * no) && (fout * no) <= 1000)
					bs = 1;
				else
					bs = 0;
				best_diff = fabsf(fout - opt_anu_freq);
				best_fout = fout;
				anu_freq_hex = (bs << 14) | (m << 7) | (n << 2) | od;
				if (fout == opt_anu_freq) {
					applog(LOG_DEBUG, "ANU found exact frequency %.1f with hex %04x",
					       opt_anu_freq, anu_freq_hex);
					goto out;
				}
			}
		}
	}
	applog(LOG_NOTICE, "ANU found nearest frequency %.1f with hex %04x", best_fout,
	       anu_freq_hex);
out:
	return anu_freq_hex;
}

static uint16_t anu3_find_freqhex(void)
{
	int i = 0, freq = opt_au3_freq, u3freq;
	uint16_t anu_freq_hex = 0x0882;

	if (!freq)
		freq = ANT_U3_DEFFREQ;

	do {
		u3freq = u3freqtable[i].freq;
		if (u3freq <= freq)
			anu_freq_hex = u3freqtable[i].hex;
		i++;
	} while (u3freq < ANT_U3_MAXFREQ);

	return anu_freq_hex;
}

static bool set_anu_freq(struct cgpu_info *icarus, struct ICARUS_INFO *info, uint16_t anu_freq_hex)
{
	unsigned char cmd_buf[4], rdreg_buf[4];
	int amount, err;
	char buf[512];

	if (!anu_freq_hex)
		anu_freq_hex = anu_find_freqhex();
	memset(cmd_buf, 0, 4);
	memset(rdreg_buf, 0, 4);
	cmd_buf[0] = 2 | 0x80;
	cmd_buf[1] = (anu_freq_hex & 0xff00u) >> 8;
	cmd_buf[2] = (anu_freq_hex & 0x00ffu);
	cmd_buf[3] = crc5(cmd_buf, 27);

	rdreg_buf[0] = 4 | 0x80;
	rdreg_buf[1] = 0;	//16-23
	rdreg_buf[2] = 0x04;	//8-15
	rdreg_buf[3] = crc5(rdreg_buf, 27);

	applog(LOG_DEBUG, "%s %i: Send frequency %02x%02x%02x%02x", icarus->drv->name, icarus->device_id,
	       cmd_buf[0], cmd_buf[1], cmd_buf[2], cmd_buf[3]);
	err = usb_write_ii(icarus, info->intinfo, (char *)cmd_buf, 4, &amount, C_ANU_SEND_CMD);
	if (err != LIBUSB_SUCCESS || amount != 4) {
		applog(LOG_ERR, "%s %i: Write freq Comms error (werr=%d amount=%d)",
		       icarus->drv->name, icarus->device_id, err, amount);
		return false;
	}
	err = usb_read_ii_timeout(icarus, info->intinfo, buf, 512, &amount, 100, C_GETRESULTS);
	if (err < 0 && err != LIBUSB_ERROR_TIMEOUT) {
		applog(LOG_ERR, "%s %i: Read freq Comms error (rerr=%d amount=%d)",
		       icarus->drv->name, icarus->device_id, err, amount);
		return false;
	}

	applog(LOG_DEBUG, "%s %i: Send freq getstatus %02x%02x%02x%02x", icarus->drv->name, icarus->device_id,
	       rdreg_buf[0], rdreg_buf[1], rdreg_buf[2], rdreg_buf[3]);
	err = usb_write_ii(icarus, info->intinfo, (char *)cmd_buf, 4, &amount, C_ANU_SEND_RDREG);
	if (err != LIBUSB_SUCCESS || amount != 4) {
		applog(LOG_ERR, "%s %i: Write freq Comms error (werr=%d amount=%d)",
		       icarus->drv->name, icarus->device_id, err, amount);
		return false;
	}
	err = usb_read_ii_timeout(icarus, info->intinfo, buf, 512, &amount, 100, C_GETRESULTS);
	if (err < 0 && err != LIBUSB_ERROR_TIMEOUT) {
		applog(LOG_ERR, "%s %i: Read freq Comms error (rerr=%d amount=%d)",
		       icarus->drv->name, icarus->device_id, err, amount);
		return false;
	}

	return true;
}

static void set_anu_volt(struct cgpu_info *icarus)
{
	unsigned char voltage_data[2], cmd_buf[4];
	char volt_buf[8];
	int err, amount;

	/* Allow a zero setting to imply not to try and set voltage */
	if (!opt_au3_volt)
		return;
	if (opt_au3_volt < 725 || opt_au3_volt > 850) {
		applog(LOG_WARNING, "Invalid ANU voltage %d specified, must be 725-850", opt_au3_volt);
		return;
	}
	sprintf(volt_buf, "%04d", opt_au3_volt);
	hex2bin(voltage_data, volt_buf, 2);
	cmd_buf[0] = 0xaa;
	cmd_buf[1] = voltage_data[0];
	cmd_buf[1] &=0x0f;
	cmd_buf[1] |=0xb0;
	cmd_buf[2] = voltage_data[1];
	cmd_buf[3] = 0x00; //0-7
	cmd_buf[3] = crc5(cmd_buf, 4*8 - 5);
	cmd_buf[3] |= 0xc0;
	applog(LOG_INFO, "Send ANU voltage %02x%02x%02x%02x", cmd_buf[0], cmd_buf[1], cmd_buf[2], cmd_buf[3]);
	cgsleep_ms(500);
	err = usb_write(icarus, (char * )cmd_buf, 4, &amount, C_ANU_SEND_VOLT);
	if (err != LIBUSB_SUCCESS || amount != 4)
		applog(LOG_ERR, "Write voltage Comms error (werr=%d amount=%d)", err, amount);
}

static void rock_init_last_received_task_complete_time(struct ICARUS_INFO *info)
{
	int i;

	if (opt_rock_freq < info->rmdev.min_frq ||
	    opt_rock_freq > info->rmdev.max_frq)
		opt_rock_freq = info->rmdev.def_frq;

	for (i = 0; i < MAX_CHIP_NUM; ++i) {
		info->rmdev.chip[i].last_received_task_complete_time = time(NULL);
		info->rmdev.chip[i].freq = opt_rock_freq/10 - 1;
		info->rmdev.chip[i].error_cnt = 0;
	}

	info->rmdev.dev_detect_time = time(NULL);
}


static void icarus_clear(struct cgpu_info *icarus, struct ICARUS_INFO *info)
{
	char buf[512];
	int amt;

	do {
		usb_read_ii_timeout(icarus, info->intinfo, buf, 512, &amt, 100, C_GETRESULTS);
	} while (amt > 0);
}

static struct cgpu_info *icarus_detect_one(struct libusb_device *dev, struct usb_find_devices *found)
{
	int this_option_offset = ++option_offset;
	struct ICARUS_INFO *info;
	struct timeval tv_start, tv_finish;

	// Block 171874 nonce = (0xa2870100) = 0x000187a2
	// N.B. golden_ob MUST take less time to calculate
	//	than the timeout set in icarus_open()
	//	This one takes ~0.53ms on Rev3 Icarus
	const char golden_ob[] =
		"4679ba4ec99876bf4bfe086082b40025"
		"4df6c356451471139a3afa71e48f544a"
		"00000000000000000000000000000000"
		"0000000087320b1a1426674f2fa722ce";

	const char golden_nonce[] = "000187a2";
	const uint32_t golden_nonce_val = 0x000187a2;
	unsigned char nonce_bin[ICARUS_READ_SIZE];
	struct ICARUS_WORK workdata;
	char *nonce_hex;
	int baud, uninitialised_var(work_division), uninitialised_var(fpga_count);
	bool anu_freqset = false;
	struct cgpu_info *icarus;
	int ret, err, amount, tries, i;
	bool ok;
	bool cmr2_ok[CAIRNSMORE2_INTS];
	int cmr2_count;

	if ((sizeof(workdata) << 1) != (sizeof(golden_ob) - 1))
		quithere(1, "Data and golden_ob sizes don't match");

	icarus = usb_alloc_cgpu(&icarus_drv, 1);

	if (!usb_init(icarus, dev, found))
		goto shin;

	get_options(this_option_offset, icarus, &baud, &work_division, &fpga_count);

	hex2bin((void *)(&workdata), golden_ob, sizeof(workdata));

	info = (struct ICARUS_INFO *)calloc(1, sizeof(struct ICARUS_INFO));
	if (unlikely(!info))
		quit(1, "Failed to malloc ICARUS_INFO");
	icarus->device_data = (void *)info;

	info->ident = usb_ident(icarus);
	switch (info->ident) {
		case IDENT_ICA:
        case IDENT_AVA:
		case IDENT_BLT:
		case IDENT_LLT:
		case IDENT_AMU:
		case IDENT_CMR1:
			info->timeout = ICARUS_WAIT_TIMEOUT;
			break;
		case IDENT_ANU:
		case IDENT_AU3:
			info->timeout = ANT_WAIT_TIMEOUT;
			break;
		case IDENT_CMR2:
			if (found->intinfo_count != CAIRNSMORE2_INTS) {
				quithere(1, "CMR2 Interface count (%d) isn't expected: %d",
						found->intinfo_count,
						CAIRNSMORE2_INTS);
			}
			info->timeout = ICARUS_CMR2_TIMEOUT;
			cmr2_count = 0;
			for (i = 0; i < CAIRNSMORE2_INTS; i++)
				cmr2_ok[i] = false;
			break;
		default:
			quit(1, "%s icarus_detect_one() invalid %s ident=%d",
				icarus->drv->dname, icarus->drv->dname, info->ident);
	}

	info->nonce_size = ICARUS_READ_SIZE;
// For CMR2 test each USB Interface

retry:

	tries = 2;
	ok = false;
	while (!ok && tries-- > 0) {
		icarus_clear(icarus, info);
		icarus_initialise(icarus, baud);

		if (info->u3) {
			uint16_t anu_freq_hex = anu3_find_freqhex();

			set_anu_volt(icarus);
			if (!set_anu_freq(icarus, info, anu_freq_hex)) {
				applog(LOG_WARNING, "%s %i: Failed to set frequency, too much overclock?",
				       icarus->drv->name, icarus->device_id);
				continue;
			}
			icarus->usbdev->ident = info->ident = IDENT_AU3;
			info->Hs = ANTU3_HASH_TIME;
			icarus->drv->name = "AU3";
			applog(LOG_DEBUG, "%s %i: Detected Antminer U3", icarus->drv->name,
			       icarus->device_id);
		} else if (info->ident == IDENT_ANU && !info->u3) {
			if (!set_anu_freq(icarus, info, 0)) {
				applog(LOG_WARNING, "%s %i: Failed to set frequency, too much overclock?",
				       icarus->drv->name, icarus->device_id);
				continue;
			}
		}

		err = usb_write_ii(icarus, info->intinfo,
				   (char *)(&workdata), sizeof(workdata), &amount, C_SENDWORK);

		if (err != LIBUSB_SUCCESS || amount != sizeof(workdata))
			continue;

		memset(nonce_bin, 0, sizeof(nonce_bin));
		ret = icarus_get_nonce(icarus, nonce_bin, &tv_start, &tv_finish, NULL, 300);
		if (ret != ICA_NONCE_OK)
			continue;

		if (info->nonce_size == ICARUS_READ_SIZE && usb_buffer_size(icarus) == 4) {
			applog(LOG_DEBUG, "%s %i: Detected Rockminer, deferring detection",
			       icarus->drv->name, icarus->device_id);
			usb_buffer_clear(icarus);
			break;

		}
		if (info->nonce_size == ICARUS_READ_SIZE && usb_buffer_size(icarus) == 1) {
			info->ant = true;
			usb_buffer_clear(icarus);
			icarus->usbdev->ident = info->ident = IDENT_ANU;
			info->nonce_size = ANT_READ_SIZE;
			info->Hs = ANTMINERUSB_HASH_TIME;
			icarus->drv->name = "ANU";
			applog(LOG_DEBUG, "%s %i: Detected Antminer U1/2/3, changing nonce size to %d",
			       icarus->drv->name, icarus->device_id, ANT_READ_SIZE);
		}

		nonce_hex = bin2hex(nonce_bin, sizeof(nonce_bin));
		if (strncmp(nonce_hex, golden_nonce, 8) == 0) {
			if (info->ant && !anu_freqset)
				anu_freqset = true;
			else
				ok = true;
		} else {
			if (tries < 0 && info->ident != IDENT_CMR2) {
				applog(LOG_ERR,
					"Icarus Detect: "
					"Test failed at %s: get %s, should: %s",
					icarus->device_path, nonce_hex, golden_nonce);
			}
		}
		free(nonce_hex);
	}

	if (!ok) {
		if (info->ident != IDENT_CMR2) {
			if (info->u3)
				goto unshin;
			info->u3 = true;
			goto retry;
		}

		if (info->intinfo < CAIRNSMORE2_INTS-1) {
			info->intinfo++;
			goto retry;
		}
	} else {
		if (info->ident == IDENT_CMR2) {
			applog(LOG_DEBUG,
				"Icarus Detect: "
				"Test succeeded at %s i%d: got %s",
					icarus->device_path, info->intinfo, golden_nonce);

			cmr2_ok[info->intinfo] = true;
			cmr2_count++;
			if (info->intinfo < CAIRNSMORE2_INTS-1) {
				info->intinfo++;
				goto retry;
			}
		}
	}

	if (info->ident == IDENT_CMR2) {
		if (cmr2_count == 0) {
			applog(LOG_ERR,
				"Icarus Detect: Test failed at %s: for all %d CMR2 Interfaces",
				icarus->device_path, CAIRNSMORE2_INTS);
			goto unshin;
		}

		// set the interface to the first one that succeeded
		for (i = 0; i < CAIRNSMORE2_INTS; i++)
			if (cmr2_ok[i]) {
				info->intinfo = i;
				break;
			}
	} else {
		applog(LOG_DEBUG,
			"Icarus Detect: "
			"Test succeeded at %s: got %s",
				icarus->device_path, golden_nonce);
	}

	/* We have a real Icarus! */
	if (!add_cgpu(icarus))
		goto unshin;

	update_usb_stats(icarus);

	applog(LOG_INFO, "%s %d: Found at %s",
		icarus->drv->name, icarus->device_id, icarus->device_path);

	if (info->ident == IDENT_CMR2) {
		applog(LOG_INFO, "%s %d: with %d Interface%s",
				icarus->drv->name, icarus->device_id,
				cmr2_count, cmr2_count > 1 ? "s" : "");

		// Assume 1 or 2 are running FPGA pairs
		if (cmr2_count < 3) {
			work_division = fpga_count = 2;
			info->Hs /= 2;
		}
	}

	applog(LOG_DEBUG, "%s %d: Init baud=%d work_division=%d fpga_count=%d",
		icarus->drv->name, icarus->device_id, baud, work_division, fpga_count);

	info->baud = baud;
	info->work_division = work_division;
	info->fpga_count = fpga_count;
	info->nonce_mask = mask(work_division);

	info->golden_hashes = (golden_nonce_val & info->nonce_mask) * fpga_count;
	timersub(&tv_finish, &tv_start, &(info->golden_tv));

	set_timing_mode(this_option_offset, icarus);
	
	if (info->ident == IDENT_CMR2) {
		int i;
		for (i = info->intinfo + 1; i < icarus->usbdev->found->intinfo_count; i++) {
			struct cgpu_info *cgtmp;
			struct ICARUS_INFO *intmp;

			if (!cmr2_ok[i])
				continue;

			cgtmp = usb_copy_cgpu(icarus);
			if (!cgtmp) {
				applog(LOG_ERR, "%s %d: Init failed initinfo %d",
						icarus->drv->name, icarus->device_id, i);
				continue;
			}

			cgtmp->usbinfo.usbstat = USB_NOSTAT;

			intmp = (struct ICARUS_INFO *)malloc(sizeof(struct ICARUS_INFO));
			if (unlikely(!intmp))
				quit(1, "Failed2 to malloc ICARUS_INFO");

			cgtmp->device_data = (void *)intmp;

			// Initialise everything to match
			memcpy(intmp, info, sizeof(struct ICARUS_INFO));

			intmp->intinfo = i;

			icarus_initialise(cgtmp, baud);

			if (!add_cgpu(cgtmp)) {
				usb_uninit(cgtmp);
				free(intmp);
				continue;
			}

			update_usb_stats(cgtmp);
		}
	}

	return icarus;

unshin:

	usb_uninit(icarus);
	free(info);
	icarus->device_data = NULL;

shin:

	icarus = usb_free_cgpu(icarus);

	return NULL;
}

static int64_t rock_scanwork(struct thr_info *thr);

static void rock_statline_before(char *buf, size_t bufsiz, struct cgpu_info *cgpu)
{
	if (cgpu->temp)
		tailsprintf(buf, bufsiz, "%3.0fMHz %3.0fC", opt_rock_freq, cgpu->temp);
	else
		tailsprintf(buf, bufsiz, "%.0fMHz", opt_rock_freq);
}

/* The only thing to do on flush_work is to remove the base work to prevent us
 * rolling what is now stale work */
static void rock_flush(struct cgpu_info *icarus)
{
	struct ICARUS_INFO *info = icarus->device_data;
	struct work *work;

	mutex_lock(&info->lock);
	work = info->base_work;
	info->base_work = NULL;
	mutex_unlock(&info->lock);

	if (work)
		free_work(work);
}

static struct cgpu_info *rock_detect_one(struct libusb_device *dev, struct usb_find_devices *found)
{
	struct ICARUS_INFO *info;
	struct timeval tv_start, tv_finish;
	char *ob_hex = NULL;

	// Block 171874 nonce = (0xa2870100) = 0x000187a2
	// N.B. golden_ob MUST take less time to calculate
	//	than the timeout set in icarus_open()
	//	This one takes ~0.53ms on Rev3 Icarus
	const char golden_ob[] =
		"4679ba4ec99876bf4bfe086082b40025"
		"4df6c356451471139a3afa71e48f544a"
		"00000000000000000000000000000000"
		"aa1ff05587320b1a1426674f2fa722ce";

	const char golden_nonce[] = "000187a2";
	const uint32_t golden_nonce_val = 0x000187a2;
	unsigned char nonce_bin[ROCK_READ_SIZE];
	struct ICARUS_WORK workdata;
	char *nonce_hex;
	struct cgpu_info *icarus;
	int ret, err, amount, tries;
	bool ok;
	int correction_times = 0;
	NONCE_DATA nonce_data;
	uint32_t nonce;
	char *newname = NULL;

	if ((sizeof(workdata) << 1) != (sizeof(golden_ob) - 1))
		quithere(1, "Data and golden_ob sizes don't match");

	icarus = usb_alloc_cgpu(&icarus_drv, 1);

	if (!usb_init(icarus, dev, found))
		goto shin;

	hex2bin((void *)(&workdata), golden_ob, sizeof(workdata));
	rev((void *)(&(workdata.midstate)), ICARUS_MIDSTATE_SIZE);
	rev((void *)(&(workdata.work)), ICARUS_WORK_SIZE);
	if (opt_debug) {
		ob_hex = bin2hex((void *)(&workdata), sizeof(workdata));
		applog(LOG_WARNING, "%s %d: send_gold_nonce %s",
			icarus->drv->name, icarus->device_id, ob_hex);
		free(ob_hex);
	}

	info = (struct ICARUS_INFO *)calloc(1, sizeof(struct ICARUS_INFO));
	if (unlikely(!info))
		quit(1, "Failed to malloc ICARUS_INFO");
	(void)memset(info, 0, sizeof(struct ICARUS_INFO));
	icarus->device_data = (void *)info;
	icarus->usbdev->ident = info->ident = IDENT_LIN;
	info->nonce_size = ROCK_READ_SIZE;
	info->fail_time = 10;
	info->nonce_mask = 0xffffffff;
	update_usb_stats(icarus);

	tries = MAX_TRIES;
	ok = false;
	while (!ok && tries-- > 0) {
		icarus_initialise(icarus, info->baud);

		applog(LOG_DEBUG, "tries: %d", tries);
		workdata.unused[ICARUS_UNUSED_SIZE - 3] = opt_rock_freq/10 - 1;
		workdata.unused[ICARUS_UNUSED_SIZE - 2] = (MAX_TRIES-1-tries);
		info->rmdev.detect_chip_no++;
		if (info->rmdev.detect_chip_no >= MAX_TRIES)
			info->rmdev.detect_chip_no = 0;
		//g_detect_chip_no = (g_detect_chip_no + 1) & MAX_CHIP_NUM;

		usb_buffer_clear(icarus);
		err = usb_write_ii(icarus, info->intinfo,
				   (char *)(&workdata), sizeof(workdata), &amount, C_SENDWORK);
		if (err != LIBUSB_SUCCESS || amount != sizeof(workdata))
			continue;

		memset(nonce_bin, 0, sizeof(nonce_bin));
		ret = icarus_get_nonce(icarus, nonce_bin, &tv_start, &tv_finish, NULL, 100);

		applog(LOG_DEBUG, "Rockminer nonce_bin: %02x %02x %02x %02x %02x %02x %02x %02x",
				  nonce_bin[0], nonce_bin[1], nonce_bin[2], nonce_bin[3],
				  nonce_bin[4], nonce_bin[5], nonce_bin[6], nonce_bin[7]);
		if (ret != ICA_NONCE_OK) {
			applog(LOG_DEBUG, "detect_one get_gold_nonce error, tries = %d", tries);
			continue;
		}
		if (usb_buffer_size(icarus) == 1) {
			applog(LOG_INFO, "Rock detect found an ANU, skipping");
			usb_buffer_clear(icarus);
			break;
		}

		newname = NULL;
		switch (nonce_bin[NONCE_CHIP_NO_OFFSET] & RM_PRODUCT_MASK) {
			case RM_PRODUCT_T1:
				newname = "LIR"; // Rocketbox
				info->rmdev.product_id = ROCKMINER_T1;
				info->rmdev.chip_max = 12;
				info->rmdev.min_frq = 200;
				info->rmdev.def_frq = 330;
				info->rmdev.max_frq = 400;
				break;
			case RM_PRODUCT_T2: // what's this?
				newname = "LIX";
				info->rmdev.product_id = ROCKMINER_T2;
				info->rmdev.chip_max = 16;
				info->rmdev.min_frq = 200;
				info->rmdev.def_frq = 300;
				info->rmdev.max_frq = 400;
				break;
			case RM_PRODUCT_RBOX:
				newname = "LIN"; // R-Box
				info->rmdev.product_id = ROCKMINER_RBOX;
				info->rmdev.chip_max = 4;
				info->rmdev.min_frq = 200;
				info->rmdev.def_frq = 270;
				info->rmdev.max_frq = 400;
				break;
			default:
				continue;
		}

		snprintf(info->rock_init, sizeof(info->rock_init), "%02x %02x %02x %02x",
				  nonce_bin[4], nonce_bin[5], nonce_bin[6], nonce_bin[7]);

		nonce_data.chip_no = nonce_bin[NONCE_CHIP_NO_OFFSET] & RM_CHIP_MASK;
		if (nonce_data.chip_no >= info->rmdev.chip_max)
			nonce_data.chip_no = 0;

		nonce_data.cmd_value = nonce_bin[NONCE_TASK_CMD_OFFSET] & RM_CMD_MASK;
		if (nonce_data.cmd_value == NONCE_TASK_COMPLETE_CMD) {
			applog(LOG_DEBUG, "complete g_detect_chip_no: %d", info->rmdev.detect_chip_no);
			workdata.unused[ICARUS_UNUSED_SIZE - 3] = opt_rock_freq/10 - 1;
			workdata.unused[ICARUS_UNUSED_SIZE - 2] =  info->rmdev.detect_chip_no;
			info->rmdev.detect_chip_no++;
			if (info->rmdev.detect_chip_no >= MAX_TRIES)
				info->rmdev.detect_chip_no = 0;

			err = usb_write_ii(icarus, info->intinfo,
				   (char *)(&workdata), sizeof(workdata), &amount, C_SENDWORK);
			if (err != LIBUSB_SUCCESS || amount != sizeof(workdata))
				continue;
			applog(LOG_DEBUG, "send_gold_nonce usb_write_ii");
			continue;
		}

		memcpy((char *)&nonce, nonce_bin, ICARUS_READ_SIZE);
		nonce = htobe32(nonce);
		applog(LOG_DEBUG, "Rockminer nonce: %08X", nonce);
		correction_times = 0;
		while (correction_times < NONCE_CORRECTION_TIMES) {
			nonce_hex = bin2hex(nonce_bin, 4);
			if (golden_nonce_val == nonce + rbox_corr_values[correction_times]) {
				memset(&(info->g_work[0]), 0, sizeof(info->g_work));
				rock_init_last_received_task_complete_time(info);

				ok = true;
				break;
			} else {
				applog(LOG_DEBUG, "detect_one gold_nonce compare error times = %d",
						  correction_times);
				if (tries < 0 && info->ident != IDENT_CMR2) {
					applog(LOG_WARNING,
						"Icarus Detect: "
						"Test failed at %s: get %s, should: %s",
						icarus->device_path, nonce_hex, golden_nonce);
				}

				if (nonce == 0)
					break;
			}
			free(nonce_hex);
			correction_times++;
		}
	}

	if (!ok)
		goto unshin;

	if (newname) {
		if (!icarus->drv->copy)
			icarus->drv = copy_drv(icarus->drv);
		icarus->drv->name = newname;
	}

	applog(LOG_DEBUG, "Icarus Detect: Test succeeded at %s: got %s",
		          icarus->device_path, golden_nonce);

	/* We have a real Rockminer! */
	if (!add_cgpu(icarus))
		goto unshin;

	icarus->drv->scanwork = rock_scanwork;
	icarus->drv->dname = "Rockminer";
	icarus->drv->get_statline_before = &rock_statline_before;
	icarus->drv->flush_work = &rock_flush;
	mutex_init(&info->lock);

	applog(LOG_INFO, "%s %d: Found at %s",
			  icarus->drv->name, icarus->device_id,
			  icarus->device_path);

	timersub(&tv_finish, &tv_start, &(info->golden_tv));

	return icarus;

unshin:

	usb_uninit(icarus);
	free(info);
	icarus->device_data = NULL;

shin:

	icarus = usb_free_cgpu(icarus);

	return NULL;
}

static void icarus_detect(bool __maybe_unused hotplug)
{
	usb_detect(&icarus_drv, rock_detect_one);
	usb_detect(&icarus_drv, icarus_detect_one);
}

static bool icarus_prepare(struct thr_info *thr)
{
	struct cgpu_info *icarus = thr->cgpu;
	struct ICARUS_INFO *info = (struct ICARUS_INFO *)(icarus->device_data);

	if (info->ant)
		info->antworks = calloc(sizeof(struct work *), ANT_QUEUE_NUM);
	return true;
}

static void cmr2_command(struct cgpu_info *icarus, uint8_t cmd, uint8_t data)
{
	struct ICARUS_INFO *info = (struct ICARUS_INFO *)(icarus->device_data);
	struct ICARUS_WORK workdata;
	int amount;

	memset((void *)(&workdata), 0, sizeof(workdata));

	workdata.prefix = ICARUS_CMR2_PREFIX;
	workdata.cmd = cmd;
	workdata.data = data;
	workdata.check = workdata.data ^ workdata.cmd ^ workdata.prefix ^ ICARUS_CMR2_CHECK;

	usb_write_ii(icarus, info->intinfo, (char *)(&workdata), sizeof(workdata), &amount, C_SENDWORK);
}

static void cmr2_commands(struct cgpu_info *icarus)
{
	struct ICARUS_INFO *info = (struct ICARUS_INFO *)(icarus->device_data);

	if (info->speed_next_work) {
		info->speed_next_work = false;
		cmr2_command(icarus, ICARUS_CMR2_CMD_SPEED, info->cmr2_speed);
		return;
	}

	if (info->flash_next_work) {
		info->flash_next_work = false;
		cmr2_command(icarus, ICARUS_CMR2_CMD_FLASH, ICARUS_CMR2_DATA_FLASH_ON);
		cgsleep_ms(250);
		cmr2_command(icarus, ICARUS_CMR2_CMD_FLASH, ICARUS_CMR2_DATA_FLASH_OFF);
		cgsleep_ms(250);
		cmr2_command(icarus, ICARUS_CMR2_CMD_FLASH, ICARUS_CMR2_DATA_FLASH_ON);
		cgsleep_ms(250);
		cmr2_command(icarus, ICARUS_CMR2_CMD_FLASH, ICARUS_CMR2_DATA_FLASH_OFF);
		return;
	}
}

void rock_send_task(unsigned char chip_no, unsigned int current_task_id, struct thr_info *thr)
{
	struct cgpu_info *icarus = thr->cgpu;
	struct ICARUS_INFO *info = (struct ICARUS_INFO *)(icarus->device_data);
	int err, amount;
	struct ICARUS_WORK workdata;
	char *ob_hex;
	struct work *work = NULL;

	/* Only base_work needs locking since it can be asynchronously deleted
	 * by flush work */
	if (info->g_work[chip_no][current_task_id] == NULL) {
		mutex_lock(&info->lock);
		if (!info->base_work)
			info->base_work = get_work(thr, thr->id);
		if (info->base_work->drv_rolllimit > 0) {
			info->base_work->drv_rolllimit--;
			roll_work(info->base_work);
			work = make_clone(info->base_work);
		} else {
			work = info->base_work;
			info->base_work = NULL;
		}
		mutex_unlock(&info->lock);

		info->g_work[chip_no][current_task_id] = work;
	} else {
		work = info->g_work[chip_no][current_task_id];
		applog(LOG_DEBUG, "::resend work");
	}

	memset((void *)(&workdata), 0, sizeof(workdata));
	memcpy(&(workdata.midstate), work->midstate, ICARUS_MIDSTATE_SIZE);
	memcpy(&(workdata.work), work->data + ICARUS_WORK_DATA_OFFSET, ICARUS_WORK_SIZE);
	workdata.unused[ICARUS_UNUSED_SIZE - 4] = 0xaa;
	if (info->rmdev.chip[chip_no].freq > (info->rmdev.max_frq/10 - 1) || 
	    info->rmdev.chip[chip_no].freq < (info->rmdev.min_frq/10 - 1))
		rock_init_last_received_task_complete_time(info);

	workdata.unused[ICARUS_UNUSED_SIZE - 3] = info->rmdev.chip[chip_no].freq; //icarus->freq/10 - 1; ;
	workdata.unused[ICARUS_UNUSED_SIZE - 2] = chip_no ;
	workdata.id = 0x55;

	if (opt_debug) {
		ob_hex = bin2hex((void *)(work->data), 128);
		applog(LOG_WARNING, "%s %d: work->data %s",
			icarus->drv->name, icarus->device_id, ob_hex);
		free(ob_hex);
	}

	// We only want results for the work we are about to send
	usb_buffer_clear(icarus);

	err = usb_write_ii(icarus, info->intinfo, (char *)(&workdata), sizeof(workdata), &amount, C_SENDWORK);

	if (err < 0 || amount != sizeof(workdata)) {
		applog(LOG_ERR, "%s %i: Comms error (werr=%d amt=%d)",
				icarus->drv->name, icarus->device_id, err, amount);
		dev_error(icarus, REASON_DEV_COMMS_ERROR);
		icarus_initialise(icarus, info->baud);

		if (info->g_work[chip_no][current_task_id])
		{
			free_work(info->g_work[chip_no][current_task_id]);
			info->g_work[chip_no][current_task_id] = NULL;
		}

		return;
	}

	return;
}

static void process_history(struct cgpu_info *icarus, struct ICARUS_INFO *info, uint32_t nonce,
			    uint64_t hash_count, struct timeval *elapsed, struct timeval *tv_start)
{
	struct ICARUS_HISTORY *history0, *history;
	struct timeval tv_history_start, tv_history_finish;
	int count;
	double Hs, W, fullnonce;
	int read_time, i;
	bool limited;
	uint32_t values;
	int64_t hash_count_range;
	double Ti, Xi;

	// Ignore possible end condition values ...
	// TODO: set limitations on calculated values depending on the device
	// to avoid crap values caused by CPU/Task Switching/Swapping/etc
	if ((nonce & info->nonce_mask) <= END_CONDITION ||
	    (nonce & info->nonce_mask) >= (info->nonce_mask & ~END_CONDITION))
		return;

	cgtime(&tv_history_start);

	history0 = &(info->history[0]);

	if (history0->values == 0)
		timeradd(tv_start, &history_sec, &(history0->finish));

	Ti = (double)(elapsed->tv_sec)
		+ ((double)(elapsed->tv_usec))/((double)1000000)
		- ((double)ICARUS_READ_TIME(info->baud));
	Xi = (double)hash_count;
	history0->sumXiTi += Xi * Ti;
	history0->sumXi += Xi;
	history0->sumTi += Ti;
	history0->sumXi2 += Xi * Xi;

	history0->values++;

	if (history0->hash_count_max < hash_count)
		history0->hash_count_max = hash_count;
	if (history0->hash_count_min > hash_count || history0->hash_count_min == 0)
		history0->hash_count_min = hash_count;

	if (history0->values >= info->min_data_count
	&&  timercmp(tv_start, &(history0->finish), >)) {
		for (i = INFO_HISTORY; i > 0; i--)
			memcpy(&(info->history[i]),
				&(info->history[i-1]),
				sizeof(struct ICARUS_HISTORY));

		// Initialise history0 to zero for summary calculation
		memset(history0, 0, sizeof(struct ICARUS_HISTORY));

		// We just completed a history data set
		// So now recalc read_time based on the whole history thus we will
		// initially get more accurate until it completes INFO_HISTORY
		// total data sets
		count = 0;
		for (i = 1 ; i <= INFO_HISTORY; i++) {
			history = &(info->history[i]);
			if (history->values >= MIN_DATA_COUNT) {
				count++;

				history0->sumXiTi += history->sumXiTi;
				history0->sumXi += history->sumXi;
				history0->sumTi += history->sumTi;
				history0->sumXi2 += history->sumXi2;
				history0->values += history->values;

				if (history0->hash_count_max < history->hash_count_max)
					history0->hash_count_max = history->hash_count_max;
				if (history0->hash_count_min > history->hash_count_min || history0->hash_count_min == 0)
					history0->hash_count_min = history->hash_count_min;
			}
		}

		// All history data
		Hs = (history0->values*history0->sumXiTi - history0->sumXi*history0->sumTi)
			/ (history0->values*history0->sumXi2 - history0->sumXi*history0->sumXi);
		W = history0->sumTi/history0->values - Hs*history0->sumXi/history0->values;
		hash_count_range = history0->hash_count_max - history0->hash_count_min;
		values = history0->values;

		// Initialise history0 to zero for next data set
		memset(history0, 0, sizeof(struct ICARUS_HISTORY));

		fullnonce = W + Hs * (((double)0xffffffff) + 1);
		read_time = SECTOMS(fullnonce) - ICARUS_READ_REDUCE;
		if (info->read_time_limit > 0 && read_time > info->read_time_limit) {
			read_time = info->read_time_limit;
			limited = true;
		} else
			limited = false;

		info->Hs = Hs;
		info->read_time = read_time;

		info->fullnonce = fullnonce;
		info->count = count;
		info->W = W;
		info->values = values;
		info->hash_count_range = hash_count_range;

		if (info->min_data_count < MAX_MIN_DATA_COUNT)
			info->min_data_count *= 2;
		else if (info->timing_mode == MODE_SHORT)
			info->do_icarus_timing = false;

		applog(LOG_WARNING, "%s %d Re-estimate: Hs=%e W=%e read_time=%dms%s fullnonce=%.3fs",
				icarus->drv->name, icarus->device_id, Hs, W, read_time,
				limited ? " (limited)" : "", fullnonce);
	}
	info->history_count++;
	cgtime(&tv_history_finish);

	timersub(&tv_history_finish, &tv_history_start, &tv_history_finish);
	timeradd(&tv_history_finish, &(info->history_time), &(info->history_time));
}

static int64_t icarus_scanwork(struct thr_info *thr)
{
	struct cgpu_info *icarus = thr->cgpu;
	struct ICARUS_INFO *info = (struct ICARUS_INFO *)(icarus->device_data);
	int ret, err, amount;
	unsigned char nonce_bin[ICARUS_BUF_SIZE];
	struct ICARUS_WORK workdata;
	char *ob_hex;
	uint32_t nonce;
	int64_t hash_count = 0;
	struct timeval tv_start, tv_finish, elapsed;
	int curr_hw_errors;
	bool was_hw_error;
	struct work *work;
	int64_t estimate_hashes;
	uint8_t workid = 0;

	if (unlikely(share_work_tdiff(icarus) > info->fail_time)) {
		if (info->failing) {
			if (share_work_tdiff(icarus) > info->fail_time + 60) {
				applog(LOG_ERR, "%s %d: Device failed to respond to restart",
				       icarus->drv->name, icarus->device_id);
				usb_nodev(icarus);
				return -1;
			}
		} else {
			applog(LOG_WARNING, "%s %d: No valid hashes for over %d secs, attempting to reset",
			       icarus->drv->name, icarus->device_id, info->fail_time);
			usb_reset(icarus);
			info->failing = true;
		}
	}

	// Device is gone
	if (icarus->usbinfo.nodev)
		return -1;

	elapsed.tv_sec = elapsed.tv_usec = 0;

	work = get_work(thr, thr->id);
	memset((void *)(&workdata), 0, sizeof(workdata));
	memcpy(&(workdata.midstate), work->midstate, ICARUS_MIDSTATE_SIZE);
	memcpy(&(workdata.work), work->data + ICARUS_WORK_DATA_OFFSET, ICARUS_WORK_SIZE);
	rev((void *)(&(workdata.midstate)), ICARUS_MIDSTATE_SIZE);
	rev((void *)(&(workdata.work)), ICARUS_WORK_SIZE);
	if (info->ant) {
		workid = info->workid;
		if (++info->workid >= 0x1F)
			info->workid = 0;
		if (info->antworks[workid])
			free_work(info->antworks[workid]);
		info->antworks[workid] = work;
		workdata.id = workid;
	}

	if (info->speed_next_work || info->flash_next_work)
		cmr2_commands(icarus);

	// We only want results for the work we are about to send
	usb_buffer_clear(icarus);

	err = usb_write_ii(icarus, info->intinfo, (char *)(&workdata), sizeof(workdata), &amount, C_SENDWORK);
	if (err < 0 || amount != sizeof(workdata)) {
		applog(LOG_ERR, "%s %i: Comms error (werr=%d amt=%d)",
				icarus->drv->name, icarus->device_id, err, amount);
		dev_error(icarus, REASON_DEV_COMMS_ERROR);
		icarus_initialise(icarus, info->baud);
		goto out;
	}

	if (opt_debug) {
		ob_hex = bin2hex((void *)(&workdata), sizeof(workdata));
		applog(LOG_DEBUG, "%s %d: sent %s",
			icarus->drv->name, icarus->device_id, ob_hex);
		free(ob_hex);
	}
more_nonces:
	/* Icarus will return nonces or nothing. If we know we have enough data
	 * for a response in the buffer already, there will be no usb read
	 * performed. */
	memset(nonce_bin, 0, sizeof(nonce_bin));
	ret = icarus_get_nonce(icarus, nonce_bin, &tv_start, &tv_finish, thr, info->read_time);
	if (ret == ICA_NONCE_ERROR)
		goto out;

	// aborted before becoming idle, get new work
	if (ret == ICA_NONCE_TIMEOUT || ret == ICA_NONCE_RESTART) {
		if (info->ant)
			goto out;

		timersub(&tv_finish, &tv_start, &elapsed);

		// ONLY up to just when it aborted
		// We didn't read a reply so we don't subtract ICARUS_READ_TIME
		estimate_hashes = ((double)(elapsed.tv_sec)
					+ ((double)(elapsed.tv_usec))/((double)1000000)) / info->Hs;

		// If some Serial-USB delay allowed the full nonce range to
		// complete it can't have done more than a full nonce
		if (unlikely(estimate_hashes > 0xffffffff))
			estimate_hashes = 0xffffffff;

		applog(LOG_DEBUG, "%s %d: no nonce = 0x%08lX hashes (%ld.%06lds)",
				icarus->drv->name, icarus->device_id,
				(long unsigned int)estimate_hashes,
				(long)elapsed.tv_sec, (long)elapsed.tv_usec);

		hash_count = estimate_hashes;
		goto out;
	}

	if (info->ant) {
		workid = nonce_bin[4] & 0x1F;
		if (info->antworks[workid])
			work = info->antworks[workid];
		else
			goto out;
	}

	memcpy((char *)&nonce, nonce_bin, ICARUS_READ_SIZE);
	nonce = htobe32(nonce);
	curr_hw_errors = icarus->hw_errors;
	if (submit_nonce(thr, work, nonce))
		info->failing = false;
	was_hw_error = (curr_hw_errors < icarus->hw_errors);

	/* U3s return shares fast enough to use just that for hashrate
	 * calculation, otherwise the result is inaccurate instead. */
	if (info->ant) {
		info->nonces++;
		if (usb_buffer_size(icarus) >= ANT_READ_SIZE)
			goto more_nonces;
	} else {
		hash_count = (nonce & info->nonce_mask);
		hash_count++;
		hash_count *= info->fpga_count;
	}

#if 0
	// This appears to only return zero nonce values
	if (usb_buffer_size(icarus) > 3) {
		memcpy((char *)&nonce, icarus->usbdev->buffer, sizeof(nonce_bin));
		nonce = htobe32(nonce);
		applog(LOG_WARNING, "%s %d: attempting to submit 2nd nonce = 0x%08lX",
				icarus->drv->name, icarus->device_id,
				(long unsigned int)nonce);
		curr_hw_errors = icarus->hw_errors;
		submit_nonce(thr, work, nonce);
		was_hw_error = (curr_hw_errors > icarus->hw_errors);
	}
#endif

	if (opt_debug || info->do_icarus_timing)
		timersub(&tv_finish, &tv_start, &elapsed);

	applog(LOG_DEBUG, "%s %d: nonce = 0x%08x = 0x%08lX hashes (%ld.%06lds)",
			icarus->drv->name, icarus->device_id,
			nonce, (long unsigned int)hash_count,
			(long)elapsed.tv_sec, (long)elapsed.tv_usec);

	if (info->do_icarus_timing && !was_hw_error)
		process_history(icarus, info, nonce, hash_count, &elapsed, &tv_start);
out:
	if (!info->ant)
		free_work(work);
	else {
		/* Ant USBs free the work themselves. Return only one full
		 * nonce worth on each pass to smooth out displayed hashrate */
		if (info->nonces) {
			hash_count = 0xffffffff;
			info->nonces--;
		}
	}

	return hash_count;
}

static int64_t rock_scanwork(struct thr_info *thr)
{
	struct cgpu_info *icarus = thr->cgpu;
	struct ICARUS_INFO *info = (struct ICARUS_INFO *)(icarus->device_data);
	int ret;
	unsigned char nonce_bin[ICARUS_BUF_SIZE];
	uint32_t nonce;
	int64_t hash_count = 0;
	struct timeval tv_start, tv_finish, elapsed;
	struct work *work = NULL;
	int64_t estimate_hashes;
	int correction_times = 0;
	NONCE_DATA nonce_data;
	double temp;

	int chip_no = 0;
	time_t recv_time = 0;

	if (unlikely(share_work_tdiff(icarus) > info->fail_time)) {
		if (info->failing) {
			if (share_work_tdiff(icarus) > info->fail_time + 60) {
				applog(LOG_ERR, "%s %d: Device failed to respond to restart",
				       icarus->drv->name, icarus->device_id);
				usb_nodev(icarus);
				return -1;
			}
		} else {
			applog(LOG_WARNING, "%s %d: No valid hashes for over %d secs, attempting to reset",
			       icarus->drv->name, icarus->device_id, info->fail_time);
			usb_reset(icarus);
			info->failing = true;
		}
	}

	// Device is gone
	if (icarus->usbinfo.nodev)
		return -1;

	elapsed.tv_sec = elapsed.tv_usec = 0;

	for (chip_no = 0; chip_no < info->rmdev.chip_max; chip_no++) {
		recv_time = time(NULL);
		if (recv_time > info->rmdev.chip[chip_no].last_received_task_complete_time + 1) {
			info->rmdev.chip[chip_no].last_received_task_complete_time = recv_time;
			rock_send_task(chip_no, 0,thr);
			break;
		}
	}

	memset(nonce_bin, 0, sizeof(nonce_bin));
	ret = icarus_get_nonce(icarus, nonce_bin, &tv_start, &tv_finish, thr, 3000);//info->read_time);

	nonce_data.chip_no = nonce_bin[NONCE_CHIP_NO_OFFSET] & RM_CHIP_MASK;
	if (nonce_data.chip_no >= info->rmdev.chip_max)
		nonce_data.chip_no = 0;
	nonce_data.task_no = nonce_bin[NONCE_TASK_NO_OFFSET] & 0x1;
	nonce_data.cmd_value = nonce_bin[NONCE_TASK_CMD_OFFSET] & RM_CMD_MASK;
	nonce_data.work_state = nonce_bin[NONCE_TASK_CMD_OFFSET] & RM_STATUS_MASK;

	temp = (double)nonce_bin[NONCE_COMMAND_OFFSET];
	if (temp != 128)
		icarus->temp = temp;

	if (nonce_data.cmd_value == NONCE_TASK_COMPLETE_CMD) {
		info->rmdev.chip[nonce_data.chip_no].last_received_task_complete_time = time(NULL);
		if (info->g_work[nonce_data.chip_no][nonce_data.task_no]) {
			free_work(info->g_work[nonce_data.chip_no][nonce_data.task_no]);
			info->g_work[nonce_data.chip_no][nonce_data.task_no] = NULL;
		}
		goto out;
	}

	if (nonce_data.cmd_value == NONCE_GET_TASK_CMD) {
		rock_send_task(nonce_data.chip_no, nonce_data.task_no, thr);
		goto out;
	}

	if (ret == ICA_NONCE_TIMEOUT)
		rock_send_task(nonce_data.chip_no, nonce_data.task_no, thr);

	work = info->g_work[nonce_data.chip_no][nonce_data.task_no];
	if (work == NULL)
		goto out;

	if (ret == ICA_NONCE_ERROR)
		goto out;

	// aborted before becoming idle, get new work
	if (ret == ICA_NONCE_TIMEOUT || ret == ICA_NONCE_RESTART) {
		timersub(&tv_finish, &tv_start, &elapsed);

		// ONLY up to just when it aborted
		// We didn't read a reply so we don't subtract ICARUS_READ_TIME
		estimate_hashes = ((double)(elapsed.tv_sec)
					+ ((double)(elapsed.tv_usec))/((double)1000000)) / info->Hs;

		// If some Serial-USB delay allowed the full nonce range to
		// complete it can't have done more than a full nonce
		if (unlikely(estimate_hashes > 0xffffffff))
			estimate_hashes = 0xffffffff;

		applog(LOG_DEBUG, "%s %d: no nonce = 0x%08lX hashes (%ld.%06lds)",
				icarus->drv->name, icarus->device_id,
				(long unsigned int)estimate_hashes,
				(long)elapsed.tv_sec, (long)elapsed.tv_usec);

		goto out;
	}

	memcpy((char *)&nonce, nonce_bin, ICARUS_READ_SIZE);
	nonce = htobe32(nonce);
	recv_time = time(NULL);
	if ((recv_time-info->rmdev.dev_detect_time) >= 60) {
		unsigned char i;
		info->rmdev.dev_detect_time  = recv_time;
		for (i = 0; i < info->rmdev.chip_max; i ++) {
			if (info->rmdev.chip[i].error_cnt >= 12) {
				if (info->rmdev.chip[i].freq > info->rmdev.min_frq)
					info->rmdev.chip[i].freq--;
			} else if (info->rmdev.chip[i].error_cnt <= 1) {
				if (info->rmdev.chip[i].freq < (info->rmdev.def_frq / 10 - 1))
					info->rmdev.chip[i].freq++;
			}
			info->rmdev.chip[i].error_cnt = 0;
		}
	}

	correction_times = 0;
	info->nonces_checked++;
	while (correction_times < NONCE_CORRECTION_TIMES) {
		uint32_t new_nonce;

		if (correction_times > 0) {
			info->nonces_correction_tests++;
			if (correction_times == 1)
				info->nonces_correction_times++;
		}
		new_nonce = nonce + rbox_corr_values[correction_times];
		/* Basic dupe testing */
		if (new_nonce == info->last_nonce[nonce_data.chip_no][nonce_data.task_no])
			break;
		if (test_nonce(work, new_nonce)) {
			nonce = new_nonce;
			submit_tested_work(thr, work);
			info->last_nonce[nonce_data.chip_no][nonce_data.task_no] = nonce;
			info->nonces_correction[correction_times]++;
			hash_count++;
			info->failing = false;
			applog(LOG_DEBUG, "Rockminer nonce :::OK:::");
			break;
		} else {
			applog(LOG_DEBUG, "Rockminer nonce error times = %d", correction_times);
			if (new_nonce == 0)
				break;
		}
		correction_times++;
	}
	if (correction_times >= NONCE_CORRECTION_TIMES) {
		inc_hw_errors(thr);
		info->nonces_fail++;
	}

	hash_count = (hash_count * info->nonce_mask);

	if (opt_debug || info->do_icarus_timing)
		timersub(&tv_finish, &tv_start, &elapsed);

	applog(LOG_DEBUG, "%s %d: nonce = 0x%08x = 0x%08lX hashes (%ld.%06lds)",
			icarus->drv->name, icarus->device_id,
			nonce, (long unsigned int)hash_count,
			(long)elapsed.tv_sec, (long)elapsed.tv_usec);

out:

	return hash_count;
}

static struct api_data *icarus_api_stats(struct cgpu_info *cgpu)
{
	struct api_data *root = NULL;
	struct ICARUS_INFO *info = (struct ICARUS_INFO *)(cgpu->device_data);
	char data[4096];
	int i, off;
	size_t len;
	float avg;

	// Warning, access to these is not locked - but we don't really
	// care since hashing performance is way more important than
	// locking access to displaying API debug 'stats'
	// If locking becomes an issue for any of them, use copy_data=true also
	root = api_add_int(root, "read_time", &(info->read_time), false);
	root = api_add_int(root, "read_time_limit", &(info->read_time_limit), false);
	root = api_add_double(root, "fullnonce", &(info->fullnonce), false);
	root = api_add_int(root, "count", &(info->count), false);
	root = api_add_hs(root, "Hs", &(info->Hs), false);
	root = api_add_double(root, "W", &(info->W), false);
	root = api_add_uint(root, "total_values", &(info->values), false);
	root = api_add_uint64(root, "range", &(info->hash_count_range), false);
	root = api_add_uint64(root, "history_count", &(info->history_count), false);
	root = api_add_timeval(root, "history_time", &(info->history_time), false);
	root = api_add_uint(root, "min_data_count", &(info->min_data_count), false);
	root = api_add_uint(root, "timing_values", &(info->history[0].values), false);
	root = api_add_const(root, "timing_mode", timing_mode_str(info->timing_mode), false);
	root = api_add_bool(root, "is_timing", &(info->do_icarus_timing), false);
	root = api_add_int(root, "baud", &(info->baud), false);
	root = api_add_int(root, "work_division", &(info->work_division), false);
	root = api_add_int(root, "fpga_count", &(info->fpga_count), false);

	if (info->ident == IDENT_LIN) {
		root = api_add_string(root, "rock_init", info->rock_init, false);
		root = api_add_uint8(root, "rock_chips", &(info->rmdev.detect_chip_no), false);
		root = api_add_uint8(root, "rock_chip_max", &(info->rmdev.chip_max), false);
		root = api_add_uint8(root, "rock_prod_id", &(info->rmdev.product_id), false);
		root = api_add_avg(root, "rock_min_freq", &(info->rmdev.min_frq), false);
		root = api_add_avg(root, "rock_max_freq", &(info->rmdev.max_frq), false);
		root = api_add_uint64(root, "rock_check", &(info->nonces_checked), false);
		root = api_add_uint64(root, "rock_corr", &(info->nonces_correction_times), false);
		root = api_add_uint64(root, "rock_corr_tests", &(info->nonces_correction_tests), false);
		root = api_add_uint64(root, "rock_corr_fail", &(info->nonces_fail), false);
		if (info->nonces_checked <= 0)
			avg = 0;
		else
			avg = (float)(info->nonces_correction_tests) / (float)(info->nonces_checked);
		root = api_add_avg(root, "rock_corr_avg", &avg, true);
		data[0] = '\0';
		off = 0;
		for (i = 0; i < NONCE_CORRECTION_TIMES; i++) {
			len = snprintf(data+off, sizeof(data)-off,
						"%s%"PRIu64,
						i > 0 ? "/" : "",
						info->nonces_correction[i]);
			if (len >= (sizeof(data)-off))
				off = sizeof(data)-1;
			else {
				if (len > 0)
					off += len;
			}
		}
		root = api_add_string(root, "rock_corr_finds", data, true);
	}

	return root;
}

static void icarus_statline_before(char *buf, size_t bufsiz, struct cgpu_info *cgpu)
{
	struct ICARUS_INFO *info = (struct ICARUS_INFO *)(cgpu->device_data);

	if (info->ant) {
		if (info->u3)
			tailsprintf(buf, bufsiz, "%3.0fMHz %3dmV", opt_au3_freq, opt_au3_volt);
		else
			tailsprintf(buf, bufsiz, "%3.0fMHz", opt_anu_freq);
	} else if (info->ident == IDENT_CMR2 && info->cmr2_speed > 0)
		tailsprintf(buf, bufsiz, "%5.1fMhz", (float)(info->cmr2_speed) * ICARUS_CMR2_SPEED_FACTOR);
}

static void icarus_shutdown(__maybe_unused struct thr_info *thr)
{
	// TODO: ?
}

static void icarus_identify(struct cgpu_info *cgpu)
{
	struct ICARUS_INFO *info = (struct ICARUS_INFO *)(cgpu->device_data);

	if (info->ident == IDENT_CMR2)
		info->flash_next_work = true;
}

static char *icarus_set(struct cgpu_info *cgpu, char *option, char *setting, char *replybuf)
{
	struct ICARUS_INFO *info = (struct ICARUS_INFO *)(cgpu->device_data);
	int val;

	if (info->ident != IDENT_CMR2) {
		strcpy(replybuf, "no set options available");
		return replybuf;
	}

	if (strcasecmp(option, "help") == 0) {
		sprintf(replybuf, "clock: range %d-%d",
				  ICARUS_CMR2_SPEED_MIN_INT, ICARUS_CMR2_SPEED_MAX_INT);
		return replybuf;
	}

	if (strcasecmp(option, "clock") == 0) {
		if (!setting || !*setting) {
			sprintf(replybuf, "missing clock setting");
			return replybuf;
		}

		val = atoi(setting);
		if (val < ICARUS_CMR2_SPEED_MIN_INT || val > ICARUS_CMR2_SPEED_MAX_INT) {
			sprintf(replybuf, "invalid clock: '%s' valid range %d-%d",
					  setting,
					  ICARUS_CMR2_SPEED_MIN_INT,
					  ICARUS_CMR2_SPEED_MAX_INT);
		}

		info->cmr2_speed = CMR2_INT_TO_SPEED(val);
		info->speed_next_work = true;

		return NULL;
	}

	sprintf(replybuf, "Unknown option: %s", option);
	return replybuf;
}

struct device_drv icarus_drv = {
	.drv_id = DRIVER_icarus,
	.dname = "Icarus",
	.name = "ICA",
	.drv_detect = icarus_detect,
	.hash_work = &hash_driver_work,
	.get_api_stats = icarus_api_stats,
	.get_statline_before = icarus_statline_before,
	.set_device = icarus_set,
	.identify_device = icarus_identify,
	.thread_prepare = icarus_prepare,
	.scanwork = icarus_scanwork,
	.thread_shutdown = icarus_shutdown,
};
