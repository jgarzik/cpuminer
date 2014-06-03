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
#include "i2c-context.h"

static struct board_selector ccd_selector;

struct i2c_ctx *U1_tca9535;
uint8_t chain_mask = 0xff;
uint8_t active_chain = 255;
pthread_mutex_t lock;


#define UNUSED_BITS 0xe0

static void ccd_unlock(void)
{
	mutex_unlock(&lock);
}

static void ccd_exit(void)
{
	if (U1_tca9535 != NULL)
		U1_tca9535->exit(U1_tca9535);
}
uint8_t retval = 0;

extern struct board_selector *ccd_board_selector_init(void)
{
	mutex_init(&lock);
	U1_tca9535 = i2c_slave_open(I2C_BUS, 0x27);
	if (U1_tca9535 == NULL)
		return NULL;
	bool retval =	U1_tca9535->write(U1_tca9535, 0x06, 0xe0) &&
			U1_tca9535->write(U1_tca9535, 0x07, 0xe0) &&
			U1_tca9535->write(U1_tca9535, 0x02, 0x1f) &&
			U1_tca9535->write(U1_tca9535, 0x03, 0x00);
	if (retval)
		return &ccd_selector;
	ccd_exit();
	return NULL;
}

static bool ccd_select(uint8_t chain)
{
	if (chain >= CCD_MAX_CHAINS)
		return false;

	mutex_lock(&lock);
	if (active_chain == chain)
		return true;

	active_chain = chain;
	chain_mask = 1 << active_chain;
	return U1_tca9535->write(U1_tca9535, 0x02, ~chain_mask);
}

static bool __ccd_board_selector_reset(uint8_t mask)
{
	if (!U1_tca9535->write(U1_tca9535, 0x03, mask))
		return false;
	cgsleep_ms(RESET_LOW_TIME_MS);
	if (!U1_tca9535->write(U1_tca9535, 0x03, 0x00))
		return false;
	cgsleep_ms(RESET_HI_TIME_MS);
	return true;
}
// we assume we are already holding the mutex
static bool ccd_reset(void)
{
	return __ccd_board_selector_reset(chain_mask);
}

static bool ccd_reset_all(void)
{
	mutex_lock(&lock);
	bool retval = __ccd_board_selector_reset(0xff & ~UNUSED_BITS);
	mutex_unlock(&lock);
	return retval;
}


static struct board_selector ccd_selector = {
	.select = ccd_select,
	.release = ccd_unlock,
	.exit = ccd_exit,
	.reset = ccd_reset,
	.reset_all = ccd_reset_all,
	/* don't have a temp sensor dedicated to chain */
	.get_temp = dummy_get_temp,
};

