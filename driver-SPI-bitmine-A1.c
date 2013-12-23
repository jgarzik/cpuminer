/*
 * cgminer SPI driver for Bitmine.ch A1 devices
 *
 * Copyright 2013, 2014 Zefir Kurtisi <zefir.kurtisi@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 3 of the License, or (at your option)
 * any later version.  See COPYING for more details.
 */

#include <stdlib.h>
#include <assert.h>
#include <fcntl.h>
#include <limits.h>
#include <unistd.h>
#include <stdbool.h>

#include "spi-context.h"
#include "logging.h"
#include "miner.h"
#include "util.h"

/********** work queue */
struct work_ent {
	struct work *work;
	struct list_head head;
};

struct work_queue {
	int num_elems;
	struct list_head head;
};

static bool wq_enqueue(struct work_queue *wq, struct work *work)
{
	if (work == NULL)
		return false;
	struct work_ent *we = malloc(sizeof(*we));
	assert(we != NULL);

	we->work = work;
	INIT_LIST_HEAD(&we->head);
	list_add_tail(&we->head, &wq->head);
	wq->num_elems++;
	return true;
}

static struct work *wq_dequeue(struct work_queue *wq)
{
	if (wq == NULL)
		return NULL;
	if (wq->num_elems == 0)
		return NULL;
	struct work_ent *we;
	we = list_entry(wq->head.next, struct work_ent, head);
	struct work *work = we->work;

	list_del(&we->head);
	free(we);
	wq->num_elems--;
	return work;
}

static FILE *log_file = NULL;
#if 1
#undef applog
#define applog(X,...) \
	do { \
		if (log_file == NULL) { \
			log_file = fopen("/run/A1.log", "w");\
			if (log_file==NULL) continue;\
		}\
		fprintf(log_file, __VA_ARGS__);\
		fprintf(log_file, "\n");\
	} while(0)
#endif

/********** chip and chain context structures */
/*
 * if not cooled sufficiently, communication fails and chip is temporary
 * disabled. we let it inactive for 10 seconds to cool down
 *
 * TODO: to be removed after bring up / test phase
 */
#define COOLDOWN_MS (10 * 1000)

/* the WRITE_JOB command is the largest (2 bytes command, 56 bytes payload) */
#define WRITE_JOB_LENGTH	58
#define MAX_CHAIN_LENGTH	255
/*
 * For commands to traverse the chain, we need to issue dummy writes to
 * keep SPI clock running. To reach the last chip in the chain, we need to
 * write the command, followed by chain-length words to pass it through the
 * chain and another chain-length words to get the ACK back to host
 */
#define MAX_CMD_LENGTH		(WRITE_JOB_LENGTH + MAX_CHAIN_LENGTH * 2 * 2)

struct A1_chip {
	int num_cores;
	int last_queued_id;
	struct work *work[4];
	/* stats */
	int hw_errors;
	int stales;
	int nonces_found;
	int nonce_ranges_done;

	/* systime in ms when chip was disabled */
	int cooldown_begin;
};

struct A1_chain {
	struct cgpu_info *cgpu;
	int num_chips;
	int num_cores;
	int num_active_chips;
	uint8_t spi_tx[MAX_CMD_LENGTH];
	uint8_t spi_rx[MAX_CMD_LENGTH];
	struct spi_ctx *spi_ctx;
	struct A1_chip *chips;
	pthread_mutex_t lock;

	struct work_queue active_wq;
};

enum A1_command {
	A1_BIST_START		= 0x01,
	A1_BIST_FIX		= 0x03,
	A1_RESET		= 0x04,
	A1_WRITE_JOB		= 0x07,
	A1_READ_RESULT		= 0x08,
	A1_WRITE_REG		= 0x09,
	A1_READ_REG		= 0x0a,
	A1_READ_REG_RESP	= 0x1a,
};

/********** config paramters */
struct A1_config_options {
	int ref_clk_khz;
	int sys_clk_khz;
	int spi_clk_khz;
	/* limit chip chain to this number of chips (testing only) */
	int override_chip_num;
};

/*
 * for now, we have one global config, defaulting values:
 * - ref_clk 16MHz / sys_clk 250MHz
 * - 800 kHz SPI clock
 */
static struct A1_config_options config_options = {
	.ref_clk_khz = 16000, .sys_clk_khz = 250000, .spi_clk_khz = 800,
};

/* override values with --bitmine-a1-options ref:sys:spi: - use 0 for default */
static struct A1_config_options *parsed_config_options;

/********** temporary helper for hexdumping SPI traffic */
#define DEBUG_HEXDUMP 1
static void hexdump(char *prefix, uint8_t *buff, int len)
{
#if DEBUG_HEXDUMP
	static char line[256];
	char *pos = line;
	int i;
	if (len < 1)
		return;

	pos += sprintf(pos, "%s: %d bytes:", prefix, len);
	for (i = 0; i < len; i++) {
		if (i > 0 && (i % 32) == 0) {
			applog(LOG_DEBUG, "%s", line);
			pos = line;
			pos += sprintf(pos, "\t");
		}
		pos += sprintf(pos, "%.2X ", buff[i]);
	}
	applog(LOG_DEBUG, "%s", line);
#endif
}

/********** upper layer SPI functions */
static bool spi_send_command(struct A1_chain *a1, uint8_t cmd, uint8_t addr,
			     uint8_t *buff, int len)
{
	memset(a1->spi_tx + 2, 0, len);
	a1->spi_tx[0] = cmd;
	a1->spi_tx[1] = addr;
	if (len > 0 && buff != NULL)
		memcpy(a1->spi_tx + 2, buff, len);
	int tx_len = (2 + len + 1) & ~1;
	applog(LOG_DEBUG, "Processing command 0x%02x%02x\n", cmd, addr);
	bool retval = spi_transfer(a1->spi_ctx, a1->spi_tx, a1->spi_rx, tx_len);
	hexdump("TX", a1->spi_tx, tx_len);
	hexdump("RX", a1->spi_rx, tx_len);
	return retval;
}

static bool spi_poll_result(struct A1_chain *a1, uint8_t cmd,
			    uint8_t chip_id, int len)
{
	int i;
	int max_poll_words = a1->num_chips * 2 + 1;
	/* at startup, we don't know the chain-length */
	if (a1->num_chips == 0)
		max_poll_words += MAX_CHAIN_LENGTH * 2;
	for(i = 0; i < max_poll_words; i++) {
		bool s = spi_transfer(a1->spi_ctx, NULL, a1->spi_rx, 2);
		hexdump("RX", a1->spi_rx, 2);
		if (!s)
			return false;
		if (a1->spi_rx[0] == cmd && a1->spi_rx[1] == chip_id) {
			applog(LOG_DEBUG, "Cmd 0x%02x ACK'd", cmd);
			if (len == 0)
				return true;
			s = spi_transfer(a1->spi_ctx, NULL,
					 a1->spi_rx + 2, len);
			hexdump("RX", a1->spi_rx + 2, len);
			if (!s)
				return false;
			hexdump("poll_result", a1->spi_rx, len + 2);
			return true;
		}
	}
	applog(LOG_WARNING, "Failure: missing ACK for cmd 0x%02x", cmd);
	return false;
}

/********** A1 SPI commands */
static bool cmd_BIST_START(struct A1_chain *a1)
{
	return	spi_send_command(a1, A1_BIST_START, 0x00, NULL, 0) &&
		spi_poll_result(a1, A1_BIST_START, 0x00, 2);
}

static bool cmd_BIST_FIX_BCAST(struct A1_chain *a1)
{
	return	spi_send_command(a1, A1_BIST_FIX, 0x00, NULL, 0) &&
		spi_poll_result(a1, A1_BIST_FIX, 0x00, 0);
}

static bool cmd_RESET_BCAST(struct A1_chain *a1)
{
	return	spi_send_command(a1, A1_RESET, 0x00, NULL, 0) &&
		spi_poll_result(a1, A1_RESET, 0x00, 0);
}

static bool cmd_WRITE_REG_BCAST(struct A1_chain *a1, uint8_t *reg)
{
	return	spi_send_command(a1, A1_WRITE_REG, 0, reg, 6) &&
		spi_poll_result(a1, A1_WRITE_REG, 0, 6);
}

static bool cmd_WRITE_REG(struct A1_chain *a1, uint8_t chip, uint8_t *reg)
{
	/* ensure we push the SPI command to the last chip in chain */
	int tx_length = 8 + a1->num_chips * 4;
	memcpy(a1->spi_tx, reg, 8);
	memset(a1->spi_tx + 8, 0, tx_length - 8);

	return spi_transfer(a1->spi_ctx, a1->spi_tx, a1->spi_rx, tx_length);
}

static bool cmd_READ_REG(struct A1_chain *a1, uint8_t chip)
{
	return	spi_send_command(a1, A1_READ_REG, chip, NULL, 0) &&
		spi_poll_result(a1, A1_READ_REG_RESP, chip, 6);
}

static bool cmd_WRITE_JOB(struct A1_chain *a1, uint8_t chip_id, uint8_t *job)
{
	/* ensure we push the SPI command to the last chip in chain */
	int tx_length = WRITE_JOB_LENGTH + a1->num_chips * 4;
	memcpy(a1->spi_tx, job, WRITE_JOB_LENGTH);
	memset(a1->spi_tx + WRITE_JOB_LENGTH, 0, tx_length - WRITE_JOB_LENGTH);

	return spi_transfer(a1->spi_ctx, a1->spi_tx, a1->spi_rx, tx_length);
}

/********** A1 low level functions */
static void A1_hw_reset(void)
{
	/*
	 * TODO: issue cold reset
	 *
	 * NOTE: suggested sequence
	 * a) reset the RSTN pin for at least 1s
	 * b) release the RSTN pin
	 * c) wait at least 1s before sending the first CMD
	 */

#ifdef NOTYET
	a1_nreset_low();
	cgsleep_ms(1000);
	a1_nreset_hi();
	cgsleep_ms(1000);
#endif
}

static bool is_busy(struct A1_chain *a1, uint8_t chip)
{
	if (!cmd_READ_REG(a1, chip))
		return false;
	return (a1->spi_rx[5] & 0x01) == 0x01;
}

#define MAX_PLL_WAIT_CYCLES 25
#define PLL_CYCLE_WAIT_TIME 40
static bool check_chip_pll_lock(struct A1_chain *a1, int chip_id, uint8_t *wr)
{
	int n;
	for (n = 0; n < MAX_PLL_WAIT_CYCLES; n++) {
		/* check for PLL lock status */
		if (cmd_READ_REG(a1, chip_id) && (a1->spi_rx[4] & 1) == 1)
			/* double check that we read back what we set before */
			return wr[0] == a1->spi_rx[2] && wr[1] == a1->spi_rx[3];

		cgsleep_ms(40);
	}
	applog(LOG_ERR, "Chip %d failed PLL lock", chip_id);
	return false;
}

static bool set_pll_config(struct A1_chain *a1,
			   int ref_clock_khz, int sys_clock_khz)
{
	/*
	 * TODO: this is only an initial approach with binary adjusted
	 * dividers and thus not exploiting the whole divider range.
	 *
	 * If required, the algorithm can be adapted to find the PLL
	 * parameters after:
	 *
	 * sys_clk = (ref_clk * pll_fbdiv) / (pll_prediv * pll_postdiv)
	 *
	 * with a higher pll_postdiv being desired over a higher pll_prediv
	 */

	int i, n;
	static uint8_t writereg[128] = { 0x00, 0x00, 0x21, 0x84 /*0x80*/, };
	uint8_t pre_div = 2;
	uint8_t post_div = 4;
	uint32_t fb_div = (sys_clock_khz * pre_div * post_div) / ref_clock_khz;

	applog(LOG_WARNING, "Setting PLL: CLK_REF=%dMHz, SYS_CLK=%dMHz",
	       ref_clock_khz / 1000, sys_clock_khz / 1000);

	while (fb_div > 511) {
		post_div <<= 1;
		fb_div >>= 1;
	}
	if (post_div > 16) {
		applog(LOG_WARNING, "Can't set PLL parameters");
		return false;
	}
	writereg[0] = (pre_div << 6) | (post_div << 1) | (fb_div >> 8);
	writereg[1] = fb_div & 0xff;
	applog(LOG_WARNING, "Setting PLL to pre_div=%d, post_div=%d, fb_div=%d"
	       ": 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x",
	       pre_div, post_div, fb_div,
	       writereg[0], writereg[1], writereg[2],
	       writereg[3], writereg[4], writereg[5]);

	if (!cmd_WRITE_REG_BCAST(a1, writereg))
		return false;

	for (i = 0; i < a1->num_active_chips; i++) {
		int chip_id = i + 1;
		if (!check_chip_pll_lock(a1, chip_id, writereg)) {
			applog(LOG_ERR, "Chip %d failed PLL lock", chip_id);
			return false;
		}
	}
	return true;
}

/*
 * BIST_START works only once after HW reset, on subsequent calls it
 * returns 0 as number of chips.
 *
 * During testing / chip-bring up one might not be able or not want to reset
 * chips on each cgminer restart, therefore this is an indirect alternative
 * to enumerate chips in a chain by reading the chips' registers.
 *
 * TODO: to be removed after chip bring-up / testing
 */
static int manual_chain_detect(struct A1_chain *a1)
{
	spi_send_command(a1, A1_BIST_START, 0x00, NULL, 2);

	int i;
	int max_poll_words = MAX_CHAIN_LENGTH * 2;
	for(i = 1; i < max_poll_words; i++) {
		if (a1->spi_rx[0] == A1_BIST_START && a1->spi_rx[1] == 0) {
			spi_transfer(a1->spi_ctx, NULL, a1->spi_rx, 2);
			hexdump("RX", a1->spi_rx, 2);
			uint8_t n = a1->spi_rx[1];
			a1->num_chips = i / 2;
			if (a1->num_chips != n) {
				applog(LOG_ERR, "WARN: Enumeration: %d <-> %d",
				       a1->num_chips, n);
				if (n != 0)
					a1->num_chips = n;
			}
			applog(LOG_WARNING, "manually detected %d chips", a1->num_chips);
			return a1->num_chips;
		}
		bool s = spi_transfer(a1->spi_ctx, NULL, a1->spi_rx, 2);
		hexdump("RX", a1->spi_rx, 2);
		if (!s)
			return false;
	}
	applog(LOG_WARNING, "Failed to manual find chips");
	return false;
}

/********** disable / re-enable related section (temporary for testing) */
static int get_current_ms(void)
{
	cgtimer_t ct;
	cgtimer_time(&ct);
	return cgtimer_to_ms(&ct);
}

static bool is_chip_disabled(struct A1_chain *a1, uint8_t chip_id)
{
	struct A1_chip *chip = &a1->chips[chip_id - 1];
	return chip->cooldown_begin != 0;
}

/* check and disable chip, remember time */
static void disable_chip(struct A1_chain *a1, uint8_t chip_id)
{
	struct A1_chip *chip = &a1->chips[chip_id - 1];
	if (is_chip_disabled(a1, chip_id)) {
		applog(LOG_WARNING, "Chip %d already disabled", chip_id);
		return;
	}
	if (cmd_READ_REG(a1, chip_id)) {
		applog(LOG_WARNING, "Chip %d is working, not going to disable",
		       chip_id);
		return;
	}
	applog(LOG_WARNING, "Disabling chip %d", chip_id);
	chip->cooldown_begin = get_current_ms();
}

/* check if disabled chips can be re-enabled */
void check_disabled_chips(struct A1_chain *a1)
{
	int i;
	for (i = 0; i < a1->num_active_chips; i++) {
		int chip_id = i + 1;
		struct A1_chip *chip = &a1->chips[i];
		if (!is_chip_disabled(a1, chip_id))
			continue;
		if (chip->cooldown_begin + COOLDOWN_MS > get_current_ms())
			continue;
		if (!cmd_READ_REG(a1, chip_id)) {
			applog(LOG_WARNING, "Chip %d not yet working", chip_id);
			/* restart cooldown period */
			chip->cooldown_begin = get_current_ms();
			continue;
		}
		applog(LOG_WARNING, "Chip %d is working again", chip_id);
		chip->cooldown_begin = 0;
	}
}

/********** job creation and result evaluation */
static uint8_t *create_job(uint8_t chip_id, uint8_t job_id,
			   const char *midstate, const char *wdata)
{
	static uint8_t job[WRITE_JOB_LENGTH] = {
		/* command */
		0x00, 0x00,
		/* midstate */
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		/* wdata */
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00,
		/* start nonce */
		0x00, 0x00, 0x00, 0x00,
		/* difficulty 1 */
		0xff, 0xff, 0x00, 0x1d,
		/* end nonce */
		0xff, 0xff, 0xff, 0xff,
	};
	uint32_t *p1 = (uint32_t *) &job[34];
	uint32_t *p2 = (uint32_t *) wdata;
	unsigned char mid[32], data[12];

	job[0] = (job_id << 4) | A1_WRITE_JOB;
	job[1] = chip_id;

	swab256(job + 2, midstate);
	p1 = (uint32_t *) &job[34];
	p2 = (uint32_t *) wdata;
	p1[0] = bswap_32(p2[0]);
	p1[1] = bswap_32(p2[1]);
	p1[2] = bswap_32(p2[2]);
	return job;
}

/* set work for given chip, returns true if a nonce range was finished */
static bool set_work(struct A1_chain *a1, uint8_t chip_id, struct work *work)
{
	unsigned char *midstate = work->midstate;
	unsigned char *wdata = work->data + 64;

	struct A1_chip *chip = &a1->chips[chip_id - 1];
	bool retval = false;

	chip->last_queued_id++;
	chip->last_queued_id &= 3;

	if (chip->work[chip->last_queued_id] != NULL) {
		work_completed(a1->cgpu, chip->work[chip->last_queued_id]);
		chip->work[chip->last_queued_id] = NULL;
		retval = true;
	}
	uint8_t *jobdata = create_job(chip_id, chip->last_queued_id + 1,
				      midstate, wdata);
	if (!cmd_WRITE_JOB(a1, chip_id, jobdata)) {
		/* give back work */
		work_completed(a1->cgpu, work);

		applog(LOG_ERR, "Failed to set work for chip %d.%d",
		       chip_id, chip->last_queued_id + 1);
		// TODO: what else?
	} else {
		chip->work[chip->last_queued_id] = work;
	}
	return retval;
}

/* check for pending results in a chain, returns false if output queue empty */
static bool get_nonce(struct A1_chain *a1, uint8_t *nonce,
		      uint8_t *chip, uint8_t *job_id)
{
	int i;
	if (!spi_send_command(a1, A1_READ_RESULT, 0x00, NULL, 0))
		return false;

	int max_poll_words = a1->num_chips * 2 + 1;
	for(i = 0; i < max_poll_words; i++) {
		if (!spi_transfer(a1->spi_ctx, NULL, a1->spi_rx, 2))
			return false;
		hexdump("RX", a1->spi_rx, 2);
		if (a1->spi_rx[0] == A1_READ_RESULT && a1->spi_rx[1] == 0x00) {
			applog(LOG_DEBUG, "Output queue empty");
			return false;
		}
		if ((a1->spi_rx[0] & 0x0f) == A1_READ_RESULT &&
		    a1->spi_rx[0] != 0) {
			*job_id = a1->spi_rx[0] >> 4;
			*chip = a1->spi_rx[1];

			if (!spi_transfer(a1->spi_ctx, NULL, nonce, 4))
				return false;

			applog(LOG_DEBUG, "Got nonce for chip %d / job_id %d",
			       *chip, *job_id);

			return true;
		}
	}
	applog(LOG_ERR, "Failed to poll for results");
	return false;
}

/* reset input work queues in chip chain */
static bool abort_work(struct A1_chain *a1)
{
	/*
	 * TODO: implement reliable abort method
	 * NOTE: until then, we are completing queued work => stales
	 */
	return true;
}

/********** driver interface */
void exit_A1_chain(struct A1_chain *a1)
{
	if (a1 == NULL)
		return;
	free(a1->chips);
	a1->chips = NULL;
	spi_exit(a1->spi_ctx);
	a1->spi_ctx = NULL;
	free(a1);
}


struct A1_chain *init_A1_chain(struct spi_ctx *ctx)
{
	int i;
	struct A1_chain *a1 = malloc(sizeof(*a1));
	assert(a1 != NULL);

	applog(LOG_DEBUG, "A1 init chain");
	memset(a1, 0, sizeof(*a1));
	a1->spi_ctx = ctx;

	a1->num_chips = manual_chain_detect(a1);
	if (a1->num_chips == 0)
		goto failure;

	applog(LOG_WARNING, "spidev%d.%d: Found %d A1 chips",
	       a1->spi_ctx->config.bus, a1->spi_ctx->config.cs_line,
	       a1->num_chips);

	a1->num_active_chips = a1->num_chips;
	if (config_options.override_chip_num > 0 &&
	    a1->num_chips > config_options.override_chip_num) {
		a1->num_active_chips = config_options.override_chip_num;
		applog(LOG_WARNING, "Limiting chain to %d chips",
				a1->num_active_chips);
	}


	if (!set_pll_config(a1, config_options.ref_clk_khz,
			    config_options.sys_clk_khz))
		goto failure;

	a1->chips = calloc(a1->num_active_chips, sizeof(struct A1_chip));
	assert (a1->chips != NULL);

	if (!cmd_BIST_FIX_BCAST(a1))
		goto failure;

	for (i = 0; i < a1->num_active_chips; i++) {
		int chip_id = i + 1;
		if (!cmd_READ_REG(a1, chip_id)) {
			applog(LOG_WARNING, "Failed to read register for "
			       "chip %d -> disabling", chip_id);
			a1->chips[i].num_cores = 0;
			continue;
		}
		a1->chips[i].num_cores = a1->spi_rx[7];
		a1->num_cores += a1->chips[i].num_cores;
		applog(LOG_WARNING, "Found chip %d with %d active cores",
		       chip_id, a1->chips[i].num_cores);
	}
	applog(LOG_WARNING, "Found %d chips with total %d active cores",
	       a1->num_active_chips, a1->num_cores);

	mutex_init(&a1->lock);
	INIT_LIST_HEAD(&a1->active_wq.head);

	return a1;

failure:
	exit_A1_chain(a1);
	return NULL;
}

static bool A1_detect_one_chain(struct spi_config *cfg)
{
	struct cgpu_info *cgpu;
	struct spi_ctx *ctx = spi_init(cfg);

	if (ctx == NULL)
		return false;

	struct A1_chain *a1 = init_A1_chain(ctx);
	if (a1 == NULL)
		return false;

	cgpu = malloc(sizeof(*cgpu));
	assert(cgpu != NULL);

	memset(cgpu, 0, sizeof(*cgpu));
	cgpu->drv = &bitmineA1_drv;
	cgpu->name = "BitmineA1";
	cgpu->threads = 1;

	cgpu->device_data = a1;

	a1->cgpu = cgpu;
	add_cgpu(cgpu);

	return true;
}

#define MAX_SPI_BUS	1
#define MAX_SPI_CS	2
/* Probe SPI channel and register chip chain */
void A1_detect(bool hotplug)
{
	int bus;
	int cs_line;

	/* no hotplug support for now */
	if (hotplug)
		return;

	if (opt_bitmine_a1_options != NULL && parsed_config_options == NULL) {
		int ref_clk;
		int sys_clk;
		int spi_clk;
		int override_chip_num;

		sscanf(opt_bitmine_a1_options, "%d:%d:%d:%d",
		       &ref_clk, &sys_clk, &spi_clk,  &override_chip_num);
		if (ref_clk != 0)
			config_options.ref_clk_khz = ref_clk;
		if (sys_clk != 0)
			config_options.sys_clk_khz = sys_clk;
		if (spi_clk != 0)
			config_options.spi_clk_khz = spi_clk;
		if (override_chip_num != 0)
			config_options.override_chip_num = override_chip_num;

		/* config options are global, scan them once */
		parsed_config_options = &config_options;
	}

	applog(LOG_DEBUG, "A1 detect");
	A1_hw_reset();
	for (bus = 0; bus < MAX_SPI_BUS; bus++) {
		for (cs_line = 0; cs_line < MAX_SPI_CS; cs_line++) {
			struct spi_config cfg = default_spi_config;
			cfg.mode = SPI_MODE_1;
			cfg.speed = config_options.spi_clk_khz * 1000;
			cfg.bus = bus;
			cfg.cs_line = cs_line;
			A1_detect_one_chain(&cfg);
		}
	}
}

/* return value is nonces processed since previous call */
static int64_t A1_scanwork(struct thr_info *thr)
{
	int i;
	struct cgpu_info *cgpu = thr->cgpu;
	struct A1_chain *a1 = cgpu->device_data;
	int32_t nonce_ranges_processed = 0;

	applog(LOG_DEBUG, "A1 running scanwork");

	uint32_t nonce;
	uint8_t chip_id;
	uint8_t job_id;
	bool work_updated = false;

	mutex_lock(&a1->lock);

	/* poll queued results */
	while (get_nonce(a1, (uint8_t*)&nonce, &chip_id, &job_id)) {
		nonce = bswap_32(nonce);
		work_updated = true;
		if (chip_id < 1 || chip_id > a1->num_active_chips) {
			applog(LOG_WARNING, "wrong chip_id %d",
			       chip_id);
			continue;
		}
		assert(job_id > 0 && job_id <= 4);
		if (job_id < 1 || job_id > 4) {
			applog(LOG_WARNING, "chip_id %d: wrong job_id %d",
			       chip_id, job_id);
			continue;
		}

		struct A1_chip *chip = &a1->chips[chip_id - 1];
		struct work *work = chip->work[job_id - 1];
		if (work == NULL) {
			/* already been flushed => stale */
			applog(LOG_WARNING, "chip %d: stale nonce 0x%08x",
			       chip_id, nonce);
			chip->stales++;
			continue;
		}
		if (!submit_nonce(thr, work, nonce)) {
			applog(LOG_WARNING, "chip %d: invalid nonce 0x%08x",
			       chip_id, nonce);
			chip->hw_errors++;
			/* add a penalty of a full nonce range on HW errors */
			nonce_ranges_processed--;
			continue;
		}
		applog(LOG_WARNING, "YEAH: chip %d / job_id %d: nonce 0x%08x",
		       chip_id, job_id, nonce);
		chip->nonces_found++;
	}

	/* check for completed works */
	for (i = 0; i < a1->num_active_chips; i++) {
		uint8_t c = i + 1;
		if (is_chip_disabled(a1, c))
			continue;
		if (!cmd_READ_REG(a1, c)) {
			applog(LOG_ERR, "Failed to read reg from chip %d", c);
			// TODO: what to do now?
			disable_chip(a1, c);
			continue;
		}
		if ((a1->spi_rx[5] & 0x02) != 0)
			continue;

		work_updated = true;
		struct work *work;
		work = wq_dequeue(&a1->active_wq);
		if (work == NULL) {
			applog(LOG_ERR, "Chip %d: work underflow", c);
			break;
		}
		if (!set_work(a1, c, work))
			continue;

		nonce_ranges_processed++;
		struct A1_chip *chip = &a1->chips[i];
		chip->nonce_ranges_done++;
		applog(LOG_DEBUG, "chip %d: job done => %d/%d/%d/%d", c,
		       chip->nonce_ranges_done, chip->nonces_found,
		       chip->hw_errors, chip->stales);

		// repeat twice in case both jobs were done
		if (!cmd_READ_REG(a1, i + 1)) {
			applog(LOG_ERR, "Failed to read reg from chip %d", c);
			// TODO: what to do now?
			disable_chip(a1, c);
			continue;
		}
		if ((a1->spi_rx[5] & 0x02) != 0)
			continue;

		work = wq_dequeue(&a1->active_wq);
		if (work == NULL) {
			applog(LOG_ERR, "Chip %d: work underflow", c);
			break;
		}
		if (!set_work(a1, i + 1, work))
			continue;

		nonce_ranges_processed++;
		chip->nonce_ranges_done++;
		applog(LOG_DEBUG, "chip %d: job done => %d/%d/%d/%d",
		       i + 1,
		       chip->nonce_ranges_done, chip->nonces_found,
		       chip->hw_errors, chip->stales);
	}
	check_disabled_chips(a1);
	mutex_unlock(&a1->lock);

	if (nonce_ranges_processed < 0) {
		applog(LOG_ERR, "Negative nonce_processed");
		nonce_ranges_processed = 0;
	}

	if (nonce_ranges_processed != 0) {
		applog(LOG_DEBUG, "nonces processed %d",
		       nonce_ranges_processed);
	}
	/* in case of no progress, prevent busy looping */
	if (!work_updated)
		cgsleep_ms(100);

	return (int64_t)nonce_ranges_processed << 32;
}

/* queue two work items per chip in chain */
static bool A1_queue_full(struct cgpu_info *cgpu)
{
	struct A1_chain *a1 = cgpu->device_data;
	int queue_full = false;
	struct work *work;

	mutex_lock(&a1->lock);
	applog(LOG_DEBUG, "A1 running queue_full: %d/%d",
	       a1->active_wq.num_elems, a1->num_active_chips);

	if (a1->active_wq.num_elems >= a1->num_active_chips * 2) {
		applog(LOG_DEBUG, "active_wq full");
		queue_full = true;
	} else {
		wq_enqueue(&a1->active_wq, get_queued(cgpu));
	}
	mutex_unlock(&a1->lock);

	return queue_full;
}

static void A1_flush_work(struct cgpu_info *cgpu)
{
	struct A1_chain *a1 = cgpu->device_data;
	applog(LOG_DEBUG, "A1 running flushwork");

	int i;

	mutex_lock(&a1->lock);
	/* stop chips hashing current work */
	if (!abort_work(a1)) {
		applog(LOG_ERR, "failed to abort work in chip chain!");
	}
	/* flush the work chips were currently hashing */
	for (i = 0; i < a1->num_active_chips; i++) {
		int j;
		struct A1_chip *chip = &a1->chips[i];
		for (j = 0; j < 4; j++) {
			struct work *work = chip->work[j];
			if (work == NULL)
				continue;
			applog(LOG_DEBUG, "flushing chip %d, work %d: 0x%p",
			       i, j + 1, work);
			work_completed(cgpu, work);
			chip->work[j] = NULL;
		}
	}
	/* flush queued work */
	applog(LOG_DEBUG, "flushing queued work...");
	while (a1->active_wq.num_elems > 0) {
		struct work *work = wq_dequeue(&a1->active_wq);
		assert(work != NULL);
		applog(LOG_DEBUG, "flushing 0x%p", work);
		work_completed(cgpu, work);
	}
	mutex_unlock(&a1->lock);
}

static bool A1_thread_init(struct thr_info *t)
{
	return true;
}
static void A1_thread_shutdown(struct thr_info *t)
{
	if (log_file != NULL)
		fclose(log_file);
	log_file = NULL;
}
struct device_drv bitmineA1_drv = {
	.drv_id = DRIVER_bitmineA1,
	.dname = "BitmineA1",
	.name = "BA1",
	.drv_detect = A1_detect,
	.thread_init = A1_thread_init,
	.thread_shutdown = A1_thread_shutdown,

	.hash_work = hash_queued_work,
	.scanwork = A1_scanwork,
	.queue_full = A1_queue_full,
	.flush_work = A1_flush_work,
};
