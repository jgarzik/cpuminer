/*
 * board selector support for TCA9535 used in Bitmine's CoinCraft Desk
 *
 * Copyright 2014 Zefir Kurtisi <zefir.kurtisi@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 3 of the License, or (at your option)
 * any later version.  See COPYING for more details.
 */


#include <sys/ioctl.h>
#include <errno.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <linux/i2c.h>
#include <linux/i2c-dev.h>

#include <stdint.h>
#include <stdbool.h>
#include <fcntl.h>

#include "miner.h"

#include "A1-board-selector.h"

static struct board_selector ccd_selector;

struct ccd_ctx {
	struct i2c_ctx U1_tca9535;
	uint8_t board_mask;
	uint8_t active_board;
	pthread_mutex_t lock;
};

static struct ccd_ctx ccd_ctx = {
	.U1_tca9535 = { .addr = 0x27, .file = -1 },
	.board_mask = 0xff,
	.active_board = 255,
};

#define UNUSED_BITS 0xe0

static void ccd_unlock(void)
{
	mutex_unlock(&ccd_ctx.lock);
}

static void ccd_exit(void)
{
	i2c_slave_close(&ccd_ctx.U1_tca9535);
}

extern struct board_selector *ccd_board_selector_init(void)
{
	mutex_init(&ccd_ctx.lock);
	struct i2c_ctx *ctx = &ccd_ctx.U1_tca9535;
	bool retval =	i2c_slave_open(ctx, I2C_BUS) &&
			i2c_slave_write(ctx, 0x06, 0xe0) &&
			i2c_slave_write(ctx, 0x07, 0xe0) &&
			i2c_slave_write(ctx, 0x02, 0x1f) &&
			i2c_slave_write(ctx, 0x03, 0x00);
	if (retval)
		return &ccd_selector;
	ccd_exit();
	return NULL;
}

static bool ccd_select(uint8_t board)
{
	if (board >= CCD_MAX_CHAINS)
		return false;

	mutex_lock(&ccd_ctx.lock);
	if (ccd_ctx.active_board == board)
		return true;

	ccd_ctx.active_board = board;
	ccd_ctx.board_mask = 1 << ccd_ctx.active_board;
	return i2c_slave_write(&ccd_ctx.U1_tca9535, 0x02, ~ccd_ctx.board_mask);
}

static bool __ccd_board_selector_reset(uint8_t mask)
{
	if (!i2c_slave_write(&ccd_ctx.U1_tca9535, 0x03, mask))
		return false;
	cgsleep_ms(RESET_LOW_TIME_MS);
	if (!i2c_slave_write(&ccd_ctx.U1_tca9535, 0x03, 0x00))
		return false;
	cgsleep_ms(RESET_HI_TIME_MS);
	return true;
}
// we assume we are already holding the mutex
static bool ccd_reset(void)
{
	return __ccd_board_selector_reset(ccd_ctx.board_mask);
}

static bool ccd_reset_all(void)
{
	mutex_lock(&ccd_ctx.lock);
	bool retval = __ccd_board_selector_reset(0xff & ~UNUSED_BITS);
	mutex_unlock(&ccd_ctx.lock);
	return retval;
}


static struct board_selector ccd_selector = {
	.select = ccd_select,
	.release = ccd_unlock,
	.exit = ccd_exit,
	.reset = ccd_reset,
	.reset_all = ccd_reset_all,
	.get_temp = dummy_u8,
};

