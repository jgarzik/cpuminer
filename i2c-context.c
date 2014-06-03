/*
 * generic I2C slave access interface
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
#include <assert.h>

#include "miner.h"
#include "i2c-context.h"


static bool i2c_slave_write(struct i2c_ctx *ctx, uint8_t reg, uint8_t val)
{
	union i2c_smbus_data data;
	data.byte = val;

	struct i2c_smbus_ioctl_data args;

	args.read_write = I2C_SMBUS_WRITE;
	args.command = reg;
	args.size = I2C_SMBUS_BYTE_DATA;
	args.data = &data;

	if (ioctl(ctx->file, I2C_SMBUS, &args) == -1) {
		applog(LOG_INFO, "i2c 0x%02x: failed to write to fdesc %d: %s",
		       ctx->addr, ctx->file, strerror(errno));
		return false;
	}
	applog(LOG_DEBUG, "I2C-W(0x%02x/0x%02x)=0x%02x", ctx->addr, reg, val);
	return true;
}

static bool i2c_slave_read(struct i2c_ctx *ctx, uint8_t reg, uint8_t *val)
{
	union i2c_smbus_data data;
	struct i2c_smbus_ioctl_data args;

	args.read_write = I2C_SMBUS_READ;
	args.command = reg;
	args.size = I2C_SMBUS_BYTE_DATA;
	args.data = &data;

	if (ioctl(ctx->file, I2C_SMBUS, &args) == -1) {
		applog(LOG_INFO, "i2c 0x%02x: failed to read from fdesc %d: %s",
		       ctx->addr, ctx->file, strerror(errno));
		return false;
	}
	*val = data.byte;
	applog(LOG_DEBUG, "I2C-R(0x%02x/0x%02x)=0x%02x", ctx->addr, reg, *val);
	return true;
}

static void i2c_slave_exit(struct i2c_ctx *ctx)
{
	if (ctx->file == -1)
		return;
	close(ctx->file);
	free(ctx);
}

extern struct i2c_ctx *i2c_slave_open(char *i2c_bus, uint8_t slave_addr)
{
	int file = open(i2c_bus, O_RDWR);
	if (file < 0) {
		applog(LOG_INFO, "Failed to open i2c-1: %s", strerror(errno));
		return NULL;
	}

	if (ioctl(file, I2C_SLAVE, slave_addr) < 0) {
		close(file);
		return NULL;
	}
	struct i2c_ctx *ctx = malloc(sizeof(*ctx));
	assert(ctx != NULL);

	ctx->addr = slave_addr;
	ctx->file = file;
	ctx->exit = i2c_slave_exit;
	ctx->read = i2c_slave_read;
	ctx->write = i2c_slave_write;
	return ctx;
}

