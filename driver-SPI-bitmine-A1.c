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

#include "A1-common.h"
#include "A1-board-selector.h"
#include "A1-trimpot-mcp4x.h"

/* one global board_selector and spi context is enough */
static struct board_selector *board_selector;
static struct spi_ctx *spi;

/********** work queue */
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

/*
 * if not cooled sufficiently, communication fails and chip is temporary
 * disabled. we let it inactive for 30 seconds to cool down
 *
 * TODO: to be removed after bring up / test phase
 */
#define COOLDOWN_MS (30 * 1000)
/* if after this number of retries a chip is still inaccessible, disable it */
#define DISABLE_CHIP_FAIL_THRESHOLD	3


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

/*
 * for now, we have one global config, defaulting values:
 * - ref_clk 16MHz / sys_clk 800MHz
 * - 2000 kHz SPI clock
 */
struct A1_config_options A1_config_options = {
	.ref_clk_khz = 16000, .sys_clk_khz = 800000, .spi_clk_khz = 2000,
};

/* override values with --bitmine-a1-options ref:sys:spi: - use 0 for default */
static struct A1_config_options *parsed_config_options;

/********** temporary helper for hexdumping SPI traffic */
static void applog_hexdump(char *prefix, uint8_t *buff, int len, int level)
{
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
	applog(level, "%s", line);
}

static void hexdump(char *prefix, uint8_t *buff, int len)
{
	applog_hexdump(prefix, buff, len, LOG_DEBUG);
}

static void hexdump_error(char *prefix, uint8_t *buff, int len)
{
	applog_hexdump(prefix, buff, len, LOG_ERR);
}

static void flush_spi(struct A1_chain *a1)
{
	memset(a1->spi_tx, 0, 64);
	spi_transfer(a1->spi_ctx, a1->spi_tx, a1->spi_rx, 64);
}


/********** upper layer SPI functions */
static uint8_t *exec_cmd(struct A1_chain *a1,
			  uint8_t cmd, uint8_t chip_id,
			  uint8_t *data, uint8_t len,
			  uint8_t resp_len)
{
	int tx_len = 4 + len;
	memset(a1->spi_tx, 0, tx_len);
	a1->spi_tx[0] = cmd;
	a1->spi_tx[1] = chip_id;

	if (data != NULL)
		memcpy(a1->spi_tx + 2, data, len);

	assert(spi_transfer(a1->spi_ctx, a1->spi_tx, a1->spi_rx, tx_len));
	hexdump("send: TX", a1->spi_tx, tx_len);
	hexdump("send: RX", a1->spi_rx, tx_len);

	int poll_len = resp_len;
	if (chip_id == 0) {
		if (a1->num_chips == 0) {
			applog(LOG_INFO, "%d: unknown chips in chain, "
			       "assuming 8", a1->chain_id);
			poll_len += 32;
		}
		poll_len += 4 * a1->num_chips;
	}
	else {
		poll_len += 4 * chip_id - 2;
	}

	assert(spi_transfer(a1->spi_ctx, NULL, a1->spi_rx + tx_len, poll_len));
	hexdump("poll: RX", a1->spi_rx + tx_len, poll_len);
	int ack_len = tx_len + resp_len;
	int ack_pos = tx_len + poll_len - ack_len;
	hexdump("poll: ACK", a1->spi_rx + ack_pos, ack_len - 2);

	return (a1->spi_rx + ack_pos);
}


/********** A1 SPI commands */
static uint8_t *cmd_BIST_FIX_BCAST(struct A1_chain *a1)
{
	uint8_t *ret = exec_cmd(a1, A1_BIST_FIX, 0x00, NULL, 0, 0);
	if (ret == NULL || ret[0] != A1_BIST_FIX) {
		applog(LOG_ERR, "%d: cmd_BIST_FIX_BCAST failed", a1->chain_id);
		return NULL;
	}
	return ret;
}

static uint8_t *cmd_RESET_BCAST(struct A1_chain *a1, uint8_t strategy)
{
	static uint8_t s[2];
	s[0] = strategy;
	s[1] = strategy;
	uint8_t *ret = exec_cmd(a1, A1_RESET, 0x00, s, 2, 0);
	if (ret == NULL || (ret[0] != A1_RESET && a1->num_chips != 0)) {
		applog(LOG_ERR, "%d: cmd_RESET_BCAST failed", a1->chain_id);
		return NULL;
	}
	return ret;
}

static uint8_t *cmd_READ_RESULT_BCAST(struct A1_chain *a1)
{
	int tx_len = 8;
	memset(a1->spi_tx, 0, tx_len);
	a1->spi_tx[0] = A1_READ_RESULT;

	assert(spi_transfer(a1->spi_ctx, a1->spi_tx, a1->spi_rx, tx_len));
	hexdump("send: TX", a1->spi_tx, tx_len);
	hexdump("send: RX", a1->spi_rx, tx_len);

	int poll_len = tx_len + 4 * a1->num_chips;
	assert(spi_transfer(a1->spi_ctx, NULL, a1->spi_rx + tx_len, poll_len));
	hexdump("poll: RX", a1->spi_rx + tx_len, poll_len);

	uint8_t *scan = a1->spi_rx;
	int i;
	for (i = 0; i < poll_len; i += 2) {
		if ((scan[i] & 0x0f) == A1_READ_RESULT)
			return scan + i;
	}
	applog(LOG_ERR, "%d: cmd_READ_RESULT_BCAST failed", a1->chain_id);
	return NULL;
}

static uint8_t *cmd_WRITE_REG(struct A1_chain *a1, uint8_t chip, uint8_t *reg)
{
	uint8_t *ret = exec_cmd(a1, A1_WRITE_REG, chip, reg, 6, 0);
	if (ret == NULL || ret[0] != A1_WRITE_REG) {
		applog(LOG_ERR, "%d: cmd_WRITE_REG failed", a1->chain_id);
		return NULL;
	}
	return ret;
}

static uint8_t *cmd_READ_REG(struct A1_chain *a1, uint8_t chip)
{
	uint8_t *ret = exec_cmd(a1, A1_READ_REG, chip, NULL, 0, 6);
	if (ret == NULL || ret[0] != A1_READ_REG_RESP || ret[1] != chip) {
		applog(LOG_ERR, "%d: cmd_READ_REG chip %d failed",
		       a1->chain_id, chip);
		return NULL;
	}
	memcpy(a1->spi_rx, ret, 8);
	return ret;
}

static uint8_t *cmd_WRITE_JOB(struct A1_chain *a1, uint8_t chip_id,
			      uint8_t *job)
{
	/* ensure we push the SPI command to the last chip in chain */
	int tx_len = WRITE_JOB_LENGTH + 2;
	memcpy(a1->spi_tx, job, WRITE_JOB_LENGTH);
	memset(a1->spi_tx + WRITE_JOB_LENGTH, 0, tx_len - WRITE_JOB_LENGTH);

	assert(spi_transfer(a1->spi_ctx, a1->spi_tx, a1->spi_rx, tx_len));
	hexdump("send: TX", a1->spi_tx, tx_len);
	hexdump("send: RX", a1->spi_rx, tx_len);

	int poll_len = 4 * chip_id - 2;

	assert(spi_transfer(a1->spi_ctx, NULL, a1->spi_rx + tx_len, poll_len));
	hexdump("poll: RX", a1->spi_rx + tx_len, poll_len);

	int ack_len = tx_len;
	int ack_pos = tx_len + poll_len - ack_len;
	hexdump("poll: ACK", a1->spi_rx + ack_pos, tx_len);

	uint8_t *ret = a1->spi_rx + ack_pos;
	if (ret[0] != a1->spi_tx[0] || ret[1] != a1->spi_tx[1]){
		applog(LOG_ERR, "%d: cmd_WRITE_JOB failed: "
			"0x%02x%02x/0x%02x%02x", a1->chain_id,
			ret[0], ret[1], a1->spi_tx[0], a1->spi_tx[1]);
		return NULL;
	}
	return ret;
}

/********** A1 low level functions */
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

		cgsleep_ms(PLL_CYCLE_WAIT_TIME);
	}
	applog(LOG_ERR, "%d: chip %d failed PLL lock", a1->chain_id, chip_id);
	return false;
}

static uint8_t *get_pll_reg(struct A1_chain *a1, int ref_clock_khz,
			    int sys_clock_khz)
{
	/*
	 * PLL parameters after:
	 * sys_clk = (ref_clk * pll_fbdiv) / (pll_prediv * 2^(pll_postdiv - 1))
	 *
	 * with a higher pll_postdiv being desired over a higher pll_prediv
	 */

	static uint8_t writereg[6] = { 0x00, 0x00, 0x21, 0x84, };
	uint8_t pre_div = 1;
	uint8_t post_div = 1;
	uint32_t fb_div;

	int cid = a1->chain_id;

	applog(LOG_WARNING, "%d: Setting PLL: CLK_REF=%dMHz, SYS_CLK=%dMHz",
	       cid, ref_clock_khz / 1000, sys_clock_khz / 1000);

	/* Euclidean search for GCD */
	int a = ref_clock_khz;
	int b = sys_clock_khz;
	while (b != 0) {
		int h = a % b;
		a = b;
		b = h;
	}
	fb_div = sys_clock_khz / a;
	int n = ref_clock_khz / a;
	/* approximate multiplier if not exactly matchable */
	if (fb_div > 511) {
		int f = fb_div / n;
		int m = (f < 32) ? 16 : (f < 64) ? 8 :
			(f < 128) ? 4 : (256 < 2) ? 2 : 1;
		fb_div = m * fb_div / n;
		n =  m;
	}
	/* try to maximize post divider */
	if ((n & 3) == 0)
		post_div = 3;
	else if ((n & 1) == 0)
		post_div = 2;
	else
		post_div = 1;
	/* remainder goes to pre_div */
	pre_div = n / (1 << (post_div - 1));
	/* correct pre_div overflow */
	if (pre_div > 31) {
		fb_div = 31 * fb_div / pre_div;
		pre_div = 31;
	}
	writereg[0] = (post_div << 6) | (pre_div << 1) | (fb_div >> 8);
	writereg[1] = fb_div & 0xff;
	applog(LOG_WARNING, "%d: setting PLL: pre_div=%d, post_div=%d, "
	       "fb_div=%d: 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x", cid,
	       pre_div, post_div, fb_div,
	       writereg[0], writereg[1], writereg[2],
	       writereg[3], writereg[4], writereg[5]);
	return writereg;
}

static bool set_pll_config(struct A1_chain *a1, int chip_id,
			   int ref_clock_khz, int sys_clock_khz)
{
	uint8_t *writereg = get_pll_reg(a1, ref_clock_khz, sys_clock_khz);
	if (writereg == NULL)
		return false;
	if (!cmd_WRITE_REG(a1, chip_id, writereg))
		return false;

	int from = (chip_id == 0) ? 0 : chip_id - 1;
	int to = (chip_id == 0) ? a1->num_active_chips : chip_id - 1;

	int i;
	for (i = from; i < to; i++) {
		int cid = i + 1;
		if (!check_chip_pll_lock(a1, chip_id, writereg)) {
			applog(LOG_ERR, "%d: chip %d failed PLL lock",
			       a1->chain_id, cid);
			return false;
		}
	}
	return true;
}

#define WEAK_CHIP_THRESHOLD	30
#define BROKEN_CHIP_THRESHOLD	26
#define WEAK_CHIP_SYS_CLK	(600 * 1000)
#define BROKEN_CHIP_SYS_CLK	(400 * 1000)
static bool check_chip(struct A1_chain *a1, int i)
{
	int chip_id = i + 1;
	int cid = a1->chain_id;
	if (!cmd_READ_REG(a1, chip_id)) {
		applog(LOG_WARNING, "%d: Failed to read register for "
		       "chip %d -> disabling", cid, chip_id);
		a1->chips[i].num_cores = 0;
		a1->chips[i].disabled = 1;
		return false;;
	}
	a1->chips[i].num_cores = a1->spi_rx[7];
	a1->num_cores += a1->chips[i].num_cores;
	applog(LOG_WARNING, "%d: Found chip %d with %d active cores",
	       cid, chip_id, a1->chips[i].num_cores);
	if (a1->chips[i].num_cores < BROKEN_CHIP_THRESHOLD) {
		applog(LOG_WARNING, "%d: broken chip %d with %d active "
		       "cores (threshold = %d)", cid, chip_id,
		       a1->chips[i].num_cores, BROKEN_CHIP_THRESHOLD);
		set_pll_config(a1, chip_id, A1_config_options.ref_clk_khz,
				BROKEN_CHIP_SYS_CLK);
		cmd_READ_REG(a1, chip_id);
		hexdump_error("new.PLL", a1->spi_rx, 8);
		a1->chips[i].disabled = true;
		a1->num_cores -= a1->chips[i].num_cores;
		return false;
	}

	if (a1->chips[i].num_cores < WEAK_CHIP_THRESHOLD) {
		applog(LOG_WARNING, "%d: weak chip %d with %d active "
		       "cores (threshold = %d)", cid,
		       chip_id, a1->chips[i].num_cores, WEAK_CHIP_THRESHOLD);
		set_pll_config(a1, chip_id, A1_config_options.ref_clk_khz,
			       WEAK_CHIP_SYS_CLK);
		cmd_READ_REG(a1, chip_id);
		hexdump_error("new.PLL", a1->spi_rx, 8);
		return false;
	}
	return true;
}

/*
 * BIST_START works only once after HW reset, on subsequent calls it
 * returns 0 as number of chips.
 */
static int chain_detect(struct A1_chain *a1)
{
	int tx_len = 6;

	memset(a1->spi_tx, 0, tx_len);
	a1->spi_tx[0] = A1_BIST_START;
	a1->spi_tx[1] = 0;

	if (!spi_transfer(a1->spi_ctx, a1->spi_tx, a1->spi_rx, tx_len))
		return 0;
	hexdump("TX", a1->spi_tx, 6);
	hexdump("RX", a1->spi_rx, 6);

	int i;
	int cid = a1->chain_id;
	int max_poll_words = MAX_CHAIN_LENGTH * 2;
	for(i = 1; i < max_poll_words; i++) {
		if (a1->spi_rx[0] == A1_BIST_START && a1->spi_rx[1] == 0) {
			spi_transfer(a1->spi_ctx, NULL, a1->spi_rx, 2);
			hexdump("RX", a1->spi_rx, 2);
			uint8_t n = a1->spi_rx[1];
			a1->num_chips = (i / 2) + 1;
			if (a1->num_chips != n) {
				applog(LOG_ERR, "%d: enumeration: %d <-> %d",
				       cid, a1->num_chips, n);
				if (n != 0)
					a1->num_chips = n;
			}
			applog(LOG_WARNING, "%d: detected %d chips",
			       cid, a1->num_chips);
			return a1->num_chips;
		}
		bool s = spi_transfer(a1->spi_ctx, NULL, a1->spi_rx, 2);
		hexdump("RX", a1->spi_rx, 2);
		if (!s)
			return 0;
	}
	applog(LOG_WARNING, "%d: no A1 chip-chain detected", cid);
	return 0;
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
	return chip->disabled || chip->cooldown_begin != 0;
}

/* check and disable chip, remember time */
static void disable_chip(struct A1_chain *a1, uint8_t chip_id)
{
	flush_spi(a1);
	struct A1_chip *chip = &a1->chips[chip_id - 1];
	int cid = a1->chain_id;
	if (is_chip_disabled(a1, chip_id)) {
		applog(LOG_WARNING, "%d: chip %d already disabled",
		       cid, chip_id);
		return;
	}
	applog(LOG_WARNING, "%d: temporary disabling chip %d", cid, chip_id);
	chip->cooldown_begin = get_current_ms();
}

/* check if disabled chips can be re-enabled */
void check_disabled_chips(struct A1_chain *a1)
{
	int i;
	int cid = a1->chain_id;
	for (i = 0; i < a1->num_active_chips; i++) {
		int chip_id = i + 1;
		struct A1_chip *chip = &a1->chips[i];
		if (!is_chip_disabled(a1, chip_id))
			continue;
		/* do not re-enable fully disabled chips */
		if (chip->disabled)
			continue;
		if (chip->cooldown_begin + COOLDOWN_MS > get_current_ms())
			continue;
		if (!cmd_READ_REG(a1, chip_id)) {
			chip->fail_count++;
			applog(LOG_WARNING, "%d: chip %d not yet working - %d",
			       cid, chip_id, chip->fail_count);
			if (chip->fail_count > DISABLE_CHIP_FAIL_THRESHOLD) {
				applog(LOG_WARNING,
				       "%d: completely disabling chip %d at %d",
				       cid, chip_id, chip->fail_count);
				chip->disabled = true;
				a1->num_cores -= chip->num_cores;
				continue;
			}
			/* restart cooldown period */
			chip->cooldown_begin = get_current_ms();
			continue;
		}
		applog(LOG_WARNING, "%d: chip %d is working again",
		       cid, chip_id);
		chip->cooldown_begin = 0;
		chip->fail_count = 0;
	}
}

/********** job creation and result evaluation */
uint32_t get_diff(double diff)
{
	uint32_t n_bits;
	int shift = 29;
	double f = (double) 0x0000ffff / diff;
	while (f < (double) 0x00008000) {
		shift--;
		f *= 256.0;
	}
	while (f >= (double) 0x00800000) {
		shift++;
		f /= 256.0;
	}
	n_bits = (int) f + (shift << 24);
	return n_bits;
}

static uint8_t *create_job(uint8_t chip_id, uint8_t job_id, struct work *work)
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
	uint8_t *midstate = work->midstate;
	uint8_t *wdata = work->data + 64;

	uint32_t *p1 = (uint32_t *) &job[34];
	uint32_t *p2 = (uint32_t *) wdata;

	job[0] = (job_id << 4) | A1_WRITE_JOB;
	job[1] = chip_id;

	swab256(job + 2, midstate);
	p1[0] = bswap_32(p2[0]);
	p1[1] = bswap_32(p2[1]);
	p1[2] = bswap_32(p2[2]);
#ifdef USE_REAL_DIFF
	p1[4] = get_diff(work->sdiff);
#endif
	return job;
}

/* set work for given chip, returns true if a nonce range was finished */
static bool set_work(struct A1_chain *a1, uint8_t chip_id, struct work *work,
		     uint8_t queue_states)
{
	int cid = a1->chain_id;
	struct A1_chip *chip = &a1->chips[chip_id - 1];
	bool retval = false;

	int job_id = chip->last_queued_id + 1;

	applog(LOG_INFO, "%d: queuing chip %d with job_id %d, state=0x%02x",
	       cid, chip_id, job_id, queue_states);
	if (job_id == (queue_states & 0x0f) || job_id == (queue_states >> 4))
		applog(LOG_WARNING, "%d: job overlap: %d, 0x%02x",
		       cid, job_id, queue_states);

	if (chip->work[chip->last_queued_id] != NULL) {
		work_completed(a1->cgpu, chip->work[chip->last_queued_id]);
		chip->work[chip->last_queued_id] = NULL;
		retval = true;
	}
	uint8_t *jobdata = create_job(chip_id, job_id, work);
	if (!cmd_WRITE_JOB(a1, chip_id, jobdata)) {
		/* give back work */
		work_completed(a1->cgpu, work);

		applog(LOG_ERR, "%d: failed to set work for chip %d.%d",
		       cid, chip_id, job_id);
		disable_chip(a1, chip_id);
	} else {
		chip->work[chip->last_queued_id] = work;
		chip->last_queued_id++;
		chip->last_queued_id &= 3;
	}
	return retval;
}

static bool get_nonce(struct A1_chain *a1, uint8_t *nonce,
		      uint8_t *chip, uint8_t *job_id)
{
	uint8_t *ret = cmd_READ_RESULT_BCAST(a1);
	if (ret == NULL)
		return false;
	if (ret[1] == 0) {
		applog(LOG_DEBUG, "%d: output queue empty", a1->chain_id);
		return false;
	}
	*job_id = ret[0] >> 4;
	*chip = ret[1];
	memcpy(nonce, ret + 2, 4);
	return true;
}

/* reset input work queues in chip chain */
static bool abort_work(struct A1_chain *a1)
{
	/* drop jobs already queued: reset strategy 0xed */
	return cmd_RESET_BCAST(a1, 0xed);
}

/********** driver interface */
void exit_A1_chain(struct A1_chain *a1)
{
	if (a1 == NULL)
		return;
	free(a1->chips);
	a1->chips = NULL;
	a1->spi_ctx = NULL;
	free(a1);
}

struct A1_chain *init_A1_chain(struct spi_ctx *ctx, int chain_id)
{
	int i;
	struct A1_chain *a1 = malloc(sizeof(*a1));
	assert(a1 != NULL);

	applog(LOG_DEBUG, "%d: A1 init chain", chain_id);
	memset(a1, 0, sizeof(*a1));
	a1->spi_ctx = ctx;
	a1->chain_id = chain_id;

	a1->num_chips = chain_detect(a1);
	if (a1->num_chips == 0)
		goto failure;

	applog(LOG_WARNING, "spidev%d.%d: %d: Found %d A1 chips",
	       a1->spi_ctx->config.bus, a1->spi_ctx->config.cs_line,
	       a1->chain_id, a1->num_chips);

	if (!set_pll_config(a1, 0, A1_config_options.ref_clk_khz,
			    A1_config_options.sys_clk_khz))
		goto failure;

	/* override max number of active chips if requested */
	a1->num_active_chips = a1->num_chips;
	if (A1_config_options.override_chip_num > 0 &&
	    a1->num_chips > A1_config_options.override_chip_num) {
		a1->num_active_chips = A1_config_options.override_chip_num;
		applog(LOG_WARNING, "%d: limiting chain to %d chips",
		       a1->chain_id, a1->num_active_chips);
	}

	a1->chips = calloc(a1->num_active_chips, sizeof(struct A1_chip));
	assert (a1->chips != NULL);

	if (!cmd_BIST_FIX_BCAST(a1))
		goto failure;

	for (i = 0; i < a1->num_active_chips; i++)
		check_chip(a1, i);

	applog(LOG_WARNING, "%d: found %d chips with total %d active cores",
	       a1->chain_id, a1->num_active_chips, a1->num_cores);

	mutex_init(&a1->lock);
	INIT_LIST_HEAD(&a1->active_wq.head);

	return a1;

failure:
	exit_A1_chain(a1);
	return NULL;
}

static bool detect_single_chain(void)
{
	board_selector = (struct board_selector*)&dummy_board_selector;
	applog(LOG_WARNING, "A1: checking single chain");
	struct A1_chain *a1 = init_A1_chain(spi, 0);
	if (a1 == NULL)
		return false;

	struct cgpu_info *cgpu = malloc(sizeof(*cgpu));
	assert(cgpu != NULL);

	memset(cgpu, 0, sizeof(*cgpu));
	cgpu->drv = &bitmineA1_drv;
	cgpu->name = "BitmineA1.SingleChain";
	cgpu->threads = 1;

	cgpu->device_data = a1;

	a1->cgpu = cgpu;
	add_cgpu(cgpu);
	applog(LOG_WARNING, "Detected single A1 chain with %d chips / %d cores",
	       a1->num_active_chips, a1->num_cores);
	return true;
}

bool detect_coincraft_desk(void)
{
	static const uint8_t mcp4x_mapping[] = { 0x2c, 0x2b, 0x2a, 0x29, 0x28 };
	board_selector = ccd_board_selector_init();
	if (board_selector == NULL) {
		applog(LOG_INFO, "No CoinCrafd Desk backplane detected.");
		return false;
	}
	board_selector->reset_all();

	int boards_detected = 0;
	int board_id;
	for (board_id = 0; board_id < CCD_MAX_CHAINS; board_id++) {
		uint8_t mcp_slave = mcp4x_mapping[board_id];
		struct mcp4x *mcp = mcp4x_init(mcp_slave);
		if (mcp == NULL)
			continue;

		if (A1_config_options.wiper != 0)
			mcp->set_wiper(mcp, 0, A1_config_options.wiper);

		applog(LOG_WARNING, "checking board %d...", board_id);
		board_selector->select(board_id);

		struct A1_chain *a1 = init_A1_chain(spi, board_id);
		board_selector->release();
		if (a1 == NULL)
			continue;

		struct cgpu_info *cgpu = malloc(sizeof(*cgpu));
		assert(cgpu != NULL);

		memset(cgpu, 0, sizeof(*cgpu));
		cgpu->drv = &bitmineA1_drv;
		cgpu->name = "BitmineA1.CCD";
		cgpu->threads = 1;

		cgpu->device_data = a1;

		a1->cgpu = cgpu;
		add_cgpu(cgpu);
		boards_detected++;
	}
	if (boards_detected == 0)
		return false;

	applog(LOG_WARNING, "Detected CoinCraft Desk with %d boards",
	       boards_detected);
	return true;
}

bool detect_coincraft_rig_v3(void)
{
	board_selector = ccr_board_selector_init();
	if (board_selector == NULL)
		return false;

	board_selector->reset_all();
	int chains_detected = 0;
	int c;
	for (c = 0; c < CCR_MAX_CHAINS; c++) {
		applog(LOG_WARNING, "checking RIG chain %d...", c);

		if (!board_selector->select(c))
			continue;

		struct A1_chain *a1 = init_A1_chain(spi, c);
		board_selector->release();

		if (a1 == NULL)
			continue;

		if (A1_config_options.wiper != 0 && (c & 1) == 0) {
			struct mcp4x *mcp = mcp4x_init(0x28);
			if (mcp == NULL) {
				applog(LOG_ERR, "%d: Cant access poti", c);
			} else {
				mcp->set_wiper(mcp, 0, A1_config_options.wiper);
				mcp->set_wiper(mcp, 1, A1_config_options.wiper);
				mcp->exit(mcp);
				applog(LOG_WARNING, "%d: set wiper to 0x%02x",
					c, A1_config_options.wiper);
			}
		}

		struct cgpu_info *cgpu = malloc(sizeof(*cgpu));
		assert(cgpu != NULL);

		memset(cgpu, 0, sizeof(*cgpu));
		cgpu->drv = &bitmineA1_drv;
		cgpu->name = "BitmineA1.CCR";
		cgpu->threads = 1;

		cgpu->device_data = a1;

		a1->cgpu = cgpu;
		add_cgpu(cgpu);
		chains_detected++;
	}
	if (chains_detected == 0)
		return false;

	applog(LOG_WARNING, "Detected CoinCraft Rig with %d chains",
	       chains_detected);
	return true;
}

/* Probe SPI channel and register chip chain */
void A1_detect(bool hotplug)
{
	/* no hotplug support for SPI */
	if (hotplug)
		return;

	/* parse bimine-a1-options */
	if (opt_bitmine_a1_options != NULL && parsed_config_options == NULL) {
		int ref_clk = 0;
		int sys_clk = 0;
		int spi_clk = 0;
		int override_chip_num = 0;
		int wiper = 0;

		sscanf(opt_bitmine_a1_options, "%d:%d:%d:%d:%d",
		       &ref_clk, &sys_clk, &spi_clk,  &override_chip_num,
		       &wiper);
		if (ref_clk != 0)
			A1_config_options.ref_clk_khz = ref_clk;
		if (sys_clk != 0) {
			if (sys_clk < 100000)
				quit(1, "system clock must be above 100MHz");
			A1_config_options.sys_clk_khz = sys_clk;
		}
		if (spi_clk != 0)
			A1_config_options.spi_clk_khz = spi_clk;
		if (override_chip_num != 0)
			A1_config_options.override_chip_num = override_chip_num;
		if (wiper != 0)
			A1_config_options.wiper = wiper;

		/* config options are global, scan them once */
		parsed_config_options = &A1_config_options;
	}
	applog(LOG_DEBUG, "A1 detect");

	/* register global SPI context */
	struct spi_config cfg = default_spi_config;
	cfg.mode = SPI_MODE_1;
	cfg.speed = A1_config_options.spi_clk_khz * 1000;
	spi = spi_init(&cfg);
	if (spi == NULL)
		return;

	/* detect and register supported products */
	if (detect_coincraft_desk())
		return;
	if (detect_coincraft_rig_v3())
		return;
	if (detect_single_chain())
		return;
	/* release SPI context if no A1 products found */
	spi_exit(spi);
}

#define TEMP_UPDATE_INT_MS	2000
static int64_t A1_scanwork(struct thr_info *thr)
{
	int i;
	struct cgpu_info *cgpu = thr->cgpu;
	struct A1_chain *a1 = cgpu->device_data;
	int32_t nonce_ranges_processed = 0;

	if (a1->num_cores == 0) {
		cgpu->deven = DEV_DISABLED;
		return 0;
	}
	board_selector->select(a1->chain_id);

	applog(LOG_DEBUG, "A1 running scanwork");

	uint32_t nonce;
	uint8_t chip_id;
	uint8_t job_id;
	bool work_updated = false;

	mutex_lock(&a1->lock);

	if (a1->last_temp_time + TEMP_UPDATE_INT_MS < get_current_ms()) {
		a1->temp = board_selector->get_temp(0);
		a1->last_temp_time = get_current_ms();
	}
	int cid = a1->chain_id;
	/* poll queued results */
	while (true) {
		if (!get_nonce(a1, (uint8_t*)&nonce, &chip_id, &job_id))
			break;
		nonce = bswap_32(nonce);
		work_updated = true;
		if (chip_id < 1 || chip_id > a1->num_active_chips) {
			applog(LOG_WARNING, "%d: wrong chip_id %d",
			       cid, chip_id);
			continue;
		}
		if (job_id < 1 && job_id > 4) {
			applog(LOG_WARNING, "%d: chip %d: result has wrong "
			       "job_id %d", cid, chip_id, job_id);
			flush_spi(a1);
			continue;
		}

		struct A1_chip *chip = &a1->chips[chip_id - 1];
		struct work *work = chip->work[job_id - 1];
		if (work == NULL) {
			/* already been flushed => stale */
			applog(LOG_WARNING, "%d: chip %d: stale nonce 0x%08x",
			       cid, chip_id, nonce);
			chip->stales++;
			continue;
		}
		if (!submit_nonce(thr, work, nonce)) {
			applog(LOG_WARNING, "%d: chip %d: invalid nonce 0x%08x",
			       cid, chip_id, nonce);
			chip->hw_errors++;
			/* add a penalty of a full nonce range on HW errors */
			nonce_ranges_processed--;
			continue;
		}
		applog(LOG_DEBUG, "YEAH: %d: chip %d / job_id %d: nonce 0x%08x",
		       cid, chip_id, job_id, nonce);
		chip->nonces_found++;
	}

	/* check for completed works */
	for (i = a1->num_active_chips; i > 0; i--) {
		uint8_t c = i;
		if (is_chip_disabled(a1, c))
			continue;
		if (!cmd_READ_REG(a1, c)) {
			disable_chip(a1, c);
			continue;
		}
		uint8_t qstate = a1->spi_rx[5] & 3;
		uint8_t qbuff = a1->spi_rx[6];
		struct work *work;
		struct A1_chip *chip = &a1->chips[i - 1];
		switch(qstate) {
		case 3:
			continue;
		case 2:
			applog(LOG_ERR, "%d: chip %d: invalid state = 2",
			       cid, c);
			continue;
		case 1:
			/* fall through */
		case 0:
			work_updated = true;

			work = wq_dequeue(&a1->active_wq);
			if (work == NULL) {
				applog(LOG_INFO, "%d: chip %d: work underflow",
				       cid, c);
				break;
			}
			if (set_work(a1, c, work, qbuff)) {
				chip->nonce_ranges_done++;
				nonce_ranges_processed++;
			}
			applog(LOG_DEBUG, "%d: chip %d: job done: %d/%d/%d/%d",
			       cid, c,
			       chip->nonce_ranges_done, chip->nonces_found,
			       chip->hw_errors, chip->stales);
			break;
		}
	}
	check_disabled_chips(a1);
	mutex_unlock(&a1->lock);

	board_selector->release();

	if (nonce_ranges_processed < 0)
		nonce_ranges_processed = 0;

	if (nonce_ranges_processed != 0) {
		applog(LOG_DEBUG, "%d, nonces processed %d",
		       cid, nonce_ranges_processed);
	}
	/* in case of no progress, prevent busy looping */
	if (!work_updated)
		cgsleep_ms(40);

	return (int64_t)nonce_ranges_processed << 32;
}


/* queue two work items per chip in chain */
static bool A1_queue_full(struct cgpu_info *cgpu)
{
	struct A1_chain *a1 = cgpu->device_data;
	int queue_full = false;

	mutex_lock(&a1->lock);
	applog(LOG_DEBUG, "%d, A1 running queue_full: %d/%d",
	       a1->chain_id, a1->active_wq.num_elems, a1->num_active_chips);

	if (a1->active_wq.num_elems >= a1->num_active_chips * 2)
		queue_full = true;
	else
		wq_enqueue(&a1->active_wq, get_queued(cgpu));

	mutex_unlock(&a1->lock);

	return queue_full;
}

static void A1_flush_work(struct cgpu_info *cgpu)
{
	struct A1_chain *a1 = cgpu->device_data;
	int cid = a1->chain_id;
	board_selector->select(cid);

	applog(LOG_DEBUG, "%d: A1 running flushwork", cid);

	int i;

	mutex_lock(&a1->lock);
	/* stop chips hashing current work */
	if (!abort_work(a1)) {
		applog(LOG_ERR, "%d: failed to abort work in chip chain!", cid);
	}
	/* flush the work chips were currently hashing */
	for (i = 0; i < a1->num_active_chips; i++) {
		int j;
		struct A1_chip *chip = &a1->chips[i];
		for (j = 0; j < 4; j++) {
			struct work *work = chip->work[j];
			if (work == NULL)
				continue;
			applog(LOG_DEBUG, "%d: flushing chip %d, work %d: 0x%p",
			       cid, i, j + 1, work);
			work_completed(cgpu, work);
			chip->work[j] = NULL;
		}
		chip->last_queued_id = 0;
	}
	/* flush queued work */
	applog(LOG_DEBUG, "%d: flushing queued work...", cid);
	while (a1->active_wq.num_elems > 0) {
		struct work *work = wq_dequeue(&a1->active_wq);
		assert(work != NULL);
		work_completed(cgpu, work);
	}
	mutex_unlock(&a1->lock);

	board_selector->release();
}

static void A1_get_statline_before(char *buf, size_t len,
				   struct cgpu_info *cgpu)
{
	struct A1_chain *a1 = cgpu->device_data;
	char temp[10];
	if (a1->temp != 0)
		snprintf(temp, 9, "%2dC", a1->temp);
	tailsprintf(buf, len, " %2d:%2d/%3d %s",
		    a1->chain_id, a1->num_active_chips, a1->num_cores,
		    a1->temp == 0 ? "   " : temp);
}

struct device_drv bitmineA1_drv = {
	.drv_id = DRIVER_bitmineA1,
	.dname = "BitmineA1",
	.name = "BA1",
	.drv_detect = A1_detect,

	.hash_work = hash_queued_work,
	.scanwork = A1_scanwork,
	.queue_full = A1_queue_full,
	.flush_work = A1_flush_work,
	.get_statline_before = A1_get_statline_before,
};
