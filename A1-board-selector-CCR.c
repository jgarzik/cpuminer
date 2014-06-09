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


#include "miner.h"

#include "A1-board-selector.h"
#include "i2c-context.h"


static struct board_selector ccr_selector;

static struct i2c_ctx *U1_tca9548;
static struct i2c_ctx *U3_tca9535;
static struct i2c_ctx *U4_tca9535;
static uint8_t active_chain;
static pthread_mutex_t lock;

struct chain_mapping {
	uint8_t chain_id;
	uint8_t U1;
	uint8_t U3p0;
	uint8_t U3p1;
};

static const struct chain_mapping chain_mapping[CCR_MAX_CHAINS] = {
	{  0, 0x01, 0x01, 0x00, },
	{  1, 0x01, 0x00, 0x80, },
	{  2, 0x02, 0x02, 0x00, },
	{  3, 0x02, 0x00, 0x40, },
	{  4, 0x04, 0x04, 0x00, },
	{  5, 0x04, 0x00, 0x20, },
	{  6, 0x08, 0x08, 0x00, },
	{  7, 0x08, 0x00, 0x10, },
	{  8, 0x10, 0x10, 0x00, },
	{  9, 0x10, 0x00, 0x08, },
	{ 10, 0x20, 0x20, 0x00, },
	{ 11, 0x20, 0x00, 0x04, },
	{ 12, 0x40, 0x40, 0x00, },
	{ 13, 0x40, 0x00, 0x02, },
	{ 14, 0x80, 0x80, 0x00, },
	{ 15, 0x80, 0x00, 0x01, },
};

static void ccr_unlock(void)
{
	mutex_unlock(&lock);
}

static void ccr_exit(void)
{
	if (U1_tca9548 != NULL)
		U1_tca9548->exit(U1_tca9548);
	if (U3_tca9535 != NULL)
		U3_tca9535->exit(U3_tca9535);
	if (U4_tca9535 != NULL)
		U4_tca9535->exit(U4_tca9535);
}


extern struct board_selector *ccr_board_selector_init(void)
{
	mutex_init(&lock);
	applog(LOG_INFO, "ccr_board_selector_init()");

	/* detect all i2c slaves */
	U1_tca9548 = i2c_slave_open(I2C_BUS, 0x70);
	U3_tca9535 = i2c_slave_open(I2C_BUS, 0x23);
	U4_tca9535 = i2c_slave_open(I2C_BUS, 0x22);
	if (U1_tca9548 == NULL || U3_tca9535 == NULL || U4_tca9535 == NULL)
		goto fail;

			/* init I2C multiplexer */
	bool res =	U1_tca9548->write(U1_tca9548, 0x00, 0x00) &&
			/* init reset selector */
			U3_tca9535->write(U3_tca9535, 0x06, 0x00) &&
			U3_tca9535->write(U3_tca9535, 0x07, 0x00) &&
			U3_tca9535->write(U3_tca9535, 0x02, 0x00) &&
			U3_tca9535->write(U3_tca9535, 0x03, 0x00) &&
			/* init chain selector */
			U4_tca9535->write(U4_tca9535, 0x06, 0x00) &&
			U4_tca9535->write(U4_tca9535, 0x07, 0x00) &&
			U4_tca9535->write(U4_tca9535, 0x02, 0x00) &&
			U4_tca9535->write(U4_tca9535, 0x03, 0x00);

	if (!res)
		goto fail;

	return &ccr_selector;

fail:
	ccr_exit();
	return NULL;
}

static bool ccr_select(uint8_t chain)
{
	if (chain >= CCR_MAX_CHAINS)
		return false;

	mutex_lock(&lock);
	if (active_chain == chain)
		return true;

	active_chain = chain;
	const struct chain_mapping *cm = &chain_mapping[chain];

	if (!U1_tca9548->write(U1_tca9548, cm->U1, cm->U1))
		return false;

	if (!U4_tca9535->write(U4_tca9535, 0x02, cm->U3p0) ||
	    !U4_tca9535->write(U4_tca9535, 0x03, cm->U3p1))
		return false;

	/* sanity check: ensure i2c command has been written before we leave */
	uint8_t tmp;
	if (!U4_tca9535->read(U4_tca9535, 0x02, &tmp) || tmp != cm->U3p0) {
		applog(LOG_ERR, "ccr_select: wrote 0x%02x, read 0x%02x",
		       cm->U3p0, tmp);
	}
	applog(LOG_DEBUG, "selected chain %d", chain);
	return true;
}

static bool __ccr_board_selector_reset(uint8_t p0, uint8_t p1)
{
	if (!U3_tca9535->write(U3_tca9535, 0x02, p0) ||
	    !U3_tca9535->write(U3_tca9535, 0x03, p1))
		return false;
	cgsleep_ms(RESET_LOW_TIME_MS);
	if (!U3_tca9535->write(U3_tca9535, 0x02, 0x00) ||
	    !U3_tca9535->write(U3_tca9535, 0x03, 0x00))
		return false;
	cgsleep_ms(RESET_HI_TIME_MS);
	return true;
}
// we assume we are already holding the mutex
static bool ccr_reset(void)
{
	const struct chain_mapping *cm = &chain_mapping[active_chain];
	applog(LOG_DEBUG, "resetting chain %d", cm->chain_id);
	bool retval = __ccr_board_selector_reset(cm->U3p0, cm->U3p1);
	return retval;
}

static bool ccr_reset_all(void)
{
	mutex_lock(&lock);
	bool retval = __ccr_board_selector_reset(0xff, 0xff);
	mutex_unlock(&lock);
	return retval;
}

static uint8_t ccr_get_temp(uint8_t sensor_id)
{
	if ((active_chain & 1) != 0 || sensor_id != 0)
		return 0;

	struct i2c_ctx *U7 = i2c_slave_open(I2C_BUS, 0x4c);
	if (U7 == NULL)
		return 0;

	uint8_t retval = 0;
	if (!U7->read(U7, 0, &retval))
		retval = 0;
	U7->exit(U7);
	return retval;
}

static struct board_selector ccr_selector = {
	.select = ccr_select,
	.release = ccr_unlock,
	.exit = ccr_exit,
	.reset = ccr_reset,
	.reset_all = ccr_reset_all,
	.get_temp = ccr_get_temp,
};

