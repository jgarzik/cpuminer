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

static struct board_selector ccr_selector;


bool i2c_slave_write(struct i2c_ctx *ctx, uint8_t reg, uint8_t val)
{
	union i2c_smbus_data data;
	data.byte = val;

	struct i2c_smbus_ioctl_data args;

	args.read_write = I2C_SMBUS_WRITE;
	args.command = reg;
	args.size = I2C_SMBUS_BYTE_DATA;
	args.data = &data;

	if (ioctl(ctx->file, I2C_SMBUS, &args) == -1) {
		applog(LOG_ERR, "Failed to write to fdesc %d: %s",
		       ctx->file, strerror(errno));
		return false;
	}
	applog(LOG_DEBUG, "W(0x%02x/0x%02x)=0x%02x", ctx->addr, reg, val);
	return true;
}

bool i2c_slave_read(struct i2c_ctx *ctx, uint8_t reg, uint8_t *val)
{
	union i2c_smbus_data data;
	struct i2c_smbus_ioctl_data args;

	args.read_write = I2C_SMBUS_READ;
	args.command = reg;
	args.size = I2C_SMBUS_BYTE_DATA;
	args.data = &data;

	if (ioctl(ctx->file, I2C_SMBUS, &args) == -1) {
		applog(LOG_ERR, "Failed to read from fdesc %d: %s",
		       ctx->file, strerror(errno));
		return false;
	}
	*val = data.byte;
	applog(LOG_DEBUG, "R(0x%02x/0x%02x)=0x%02x", ctx->addr, reg, *val);
	return true;
}

bool i2c_slave_open(struct i2c_ctx *ctx, char *i2c_bus)
{
	ctx->file = open(i2c_bus, O_RDWR);
	if (ctx->file < 0) {
		applog(LOG_INFO, "Failed to open i2c-1: %s", strerror(errno));
		return false;
	}

	if (ioctl(ctx->file, I2C_SLAVE, ctx->addr) < 0) {
		close(ctx->file);
		return false;
	}
	return true;
}

void i2c_slave_close(struct i2c_ctx *ctx)
{
	if (ctx->file == -1)
		return;
	close(ctx->file);
	ctx->file = -1;
}


struct ccr_ctx {
	struct i2c_ctx U1_tca9548;
	struct i2c_ctx U2_tca9535;
	struct i2c_ctx U3_tca9535;
	struct i2c_ctx U4_tca9535;
	uint16_t chain_mask;
	uint8_t active_chain;
	pthread_mutex_t lock;
	uint8_t boards_available;
};

static struct ccr_ctx ccr_ctx = {
	.U1_tca9548 = { .addr = 0x70, .file = -1, },
	.U2_tca9535 = { .addr = 0x20, .file = -1, },
	.U3_tca9535 = { .addr = 0x23, .file = -1, },
	.U4_tca9535 = { .addr = 0x22, .file = -1, },
	.chain_mask = 0xffff,
	.active_chain = 255,
	.boards_available = 0x00,
};

struct chain_mapping {
	uint8_t chain_id;
	uint8_t U1;
	uint8_t U2p0;
	uint8_t U2p1;
	uint8_t U3p0;
	uint8_t U3p1;
};

static const struct chain_mapping chain_mapping[CCR_MAX_CHAINS] = {
	{  0, 0x01, 0x01, 0x80, 0x01, 0x00, },
	{  1, 0x01, 0x01, 0x80, 0x00, 0x80, },
	{  2, 0x02, 0x02, 0x40, 0x02, 0x00, },
	{  3, 0x02, 0x02, 0x40, 0x00, 0x40, },
	{  4, 0x04, 0x04, 0x20, 0x04, 0x00, },
	{  5, 0x04, 0x04, 0x20, 0x00, 0x20, },
	{  6, 0x08, 0x08, 0x10, 0x08, 0x00, },
	{  7, 0x08, 0x08, 0x10, 0x00, 0x10, },
	{  8, 0x10, 0x10, 0x08, 0x10, 0x00, },
	{  9, 0x10, 0x10, 0x08, 0x00, 0x08, },
	{ 10, 0x20, 0x20, 0x04, 0x20, 0x00, },
	{ 11, 0x20, 0x20, 0x04, 0x00, 0x04, },
	{ 12, 0x40, 0x40, 0x02, 0x40, 0x00, },
	{ 13, 0x40, 0x40, 0x02, 0x00, 0x02, },
	{ 14, 0x80, 0x80, 0x01, 0x80, 0x00, },
	{ 15, 0x80, 0x80, 0x01, 0x00, 0x01, },
};

static void ccr_unlock(void)
{
	mutex_unlock(&ccr_ctx.lock);
}

static void ccr_exit(void)
{
	i2c_slave_close(&ccr_ctx.U1_tca9548);
	i2c_slave_close(&ccr_ctx.U2_tca9535);
	i2c_slave_close(&ccr_ctx.U3_tca9535);
	i2c_slave_close(&ccr_ctx.U4_tca9535);
}

static bool ccr_power_on_one_board(uint8_t chain)
{
	const struct chain_mapping *cm = &chain_mapping[chain];
	if (chain & 1)
		return false;
	uint8_t new_power_mask = ccr_ctx.boards_available | cm->U2p0;
	if (!i2c_slave_write(&ccr_ctx.U2_tca9535, 0x03, new_power_mask))
		return false;
	int i;
	for (i = 0; i < 8; i ++) {
		uint8_t val;
		if (!i2c_slave_read(&ccr_ctx.U2_tca9535, 0x00, &val))
			return false;
		if (val & cm->U2p1) {
			applog(LOG_INFO, "Power OK for chain %d after %d",
			       chain, i);
			ccr_ctx.boards_available = new_power_mask;
			return true;
		}
		cgsleep_ms(10);
	}
	applog(LOG_INFO, "Power NOK for chain %d", chain);
	return false;
}
static int ccr_power_on_boards(void)
{
	int i;
	int boards = 0;
	for (i = 0; i < CCR_MAX_CHAINS / 2; i++) {
		if (ccr_power_on_one_board(i * 2))
			boards++;
	}
	return boards;
}

extern struct board_selector *ccr_board_selector_init(void)
{
	mutex_init(&ccr_ctx.lock);
	applog(LOG_INFO, "ccr_board_selector_init()");

			/* detect all i2c slaves */
	bool res =	i2c_slave_open(&ccr_ctx.U1_tca9548, I2C_BUS) &&
			i2c_slave_open(&ccr_ctx.U2_tca9535, I2C_BUS) &&
			i2c_slave_open(&ccr_ctx.U3_tca9535, I2C_BUS) &&
			i2c_slave_open(&ccr_ctx.U4_tca9535, I2C_BUS) &&
			/* init I2C multiplexer */
			i2c_slave_write(&ccr_ctx.U1_tca9548, 0x00, 0x00) &&
			/* init power selector */
			i2c_slave_write(&ccr_ctx.U2_tca9535, 0x06, 0xff) &&
			i2c_slave_write(&ccr_ctx.U2_tca9535, 0x07, 0x00) &&
			i2c_slave_write(&ccr_ctx.U2_tca9535, 0x03, 0x00) &&
			/* init reset selector */
			i2c_slave_write(&ccr_ctx.U3_tca9535, 0x06, 0x00) &&
			i2c_slave_write(&ccr_ctx.U3_tca9535, 0x07, 0x00) &&
			i2c_slave_write(&ccr_ctx.U3_tca9535, 0x02, 0x00) &&
			i2c_slave_write(&ccr_ctx.U3_tca9535, 0x03, 0x00) &&
			/* init chain selector */
			i2c_slave_write(&ccr_ctx.U4_tca9535, 0x06, 0x00) &&
			i2c_slave_write(&ccr_ctx.U4_tca9535, 0x07, 0x00) &&
			i2c_slave_write(&ccr_ctx.U4_tca9535, 0x02, 0x00) &&
			i2c_slave_write(&ccr_ctx.U4_tca9535, 0x03, 0x00);

	if (!res)
		goto fail;

	if (ccr_power_on_boards() == 0)
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

	mutex_lock(&ccr_ctx.lock);
	if (ccr_ctx.active_chain == chain)
		return true;

	ccr_ctx.active_chain = chain;
	const struct chain_mapping *cm = &chain_mapping[chain];
	if (!i2c_slave_write(&ccr_ctx.U4_tca9535, 0x02, cm->U3p0) ||
	    !i2c_slave_write(&ccr_ctx.U4_tca9535, 0x03, cm->U3p1) ||
	    !i2c_slave_write(&ccr_ctx.U1_tca9548, cm->U1, cm->U1))
		return false;

	applog(LOG_DEBUG, "selected chain %d", chain);
	return true;
}

static bool __ccr_board_selector_reset(uint8_t p0, uint8_t p1)
{
	if (!i2c_slave_write(&ccr_ctx.U3_tca9535, 0x02, p0) ||
	    !i2c_slave_write(&ccr_ctx.U3_tca9535, 0x03, p1))
		return false;
	cgsleep_ms(RESET_LOW_TIME_MS);
	if (!i2c_slave_write(&ccr_ctx.U3_tca9535, 0x02, 0x00) ||
	    !i2c_slave_write(&ccr_ctx.U3_tca9535, 0x03, 0x00))
		return false;
	cgsleep_ms(RESET_HI_TIME_MS);
	return true;
}
// we assume we are already holding the mutex
static bool ccr_reset(void)
{
	const struct chain_mapping *cm = &chain_mapping[ccr_ctx.active_chain];
	bool retval = __ccr_board_selector_reset(cm->U3p0, cm->U3p1);
	return retval;
}

static bool ccr_reset_all(void)
{
	mutex_lock(&ccr_ctx.lock);
	bool retval = __ccr_board_selector_reset(0xff, 0xff);
	mutex_unlock(&ccr_ctx.lock);
	return retval;
}

static uint8_t ccr_get_temp(void)
{
	if (ccr_ctx.active_chain & 1)
		return 0;

	uint8_t retval = 0;
	static struct i2c_ctx U7 = { .addr = 0x4c, .file = -1 };
	if (i2c_slave_open(&U7, I2C_BUS)) {
		i2c_slave_read(&U7, 0, &retval);
		i2c_slave_close(&U7);
	}
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

