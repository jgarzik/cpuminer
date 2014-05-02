/*
 * support for MCP46x digital trimpot used in Bitmine's products
 *
 * Copyright 2014 Zefir Kurtisi <zefir.kurtisi@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 3 of the License, or (at your option)
 * any later version.  See COPYING for more details.
 */


#include <assert.h>
#include <errno.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <linux/i2c.h>
#include <linux/i2c-dev.h>
#include <sys/ioctl.h>

#include <stdint.h>
#include <stdbool.h>
#include <fcntl.h>

#include "miner.h"

#include "A1-trimpot-mcp4x.h"


static bool mcp4x_check_status(int file)
{
	union i2c_smbus_data data;
	struct i2c_smbus_ioctl_data args;

	args.read_write = I2C_SMBUS_READ;
	args.command = ((5 & 0x0f) << 4) | 0x0c;
	args.size = I2C_SMBUS_WORD_DATA;
	args.data = &data;

	return ioctl(file, I2C_SMBUS, &args) >= 0;
}

static uint16_t mcp4x_get_wiper(struct mcp4x *me, uint8_t id)
{
	assert(id < 2);
	union i2c_smbus_data data;
	struct i2c_smbus_ioctl_data args;

	args.read_write = I2C_SMBUS_READ;
	args.command = ((id & 0x0f) << 4) | 0x0c;
	args.size = I2C_SMBUS_WORD_DATA;
	args.data = &data;

	if (ioctl(me->file, I2C_SMBUS, &args) < 0) {
		applog(LOG_ERR, "Failed to read id %d: %s\n", id,
		       strerror(errno));
		return 0xffff;
	}
	return htobe16(data.word & 0xffff);
}

static bool mcp4x_set_wiper(struct mcp4x *me, uint8_t id, uint16_t w)
{
	assert(id < 2);
	union i2c_smbus_data data;
	data.word = w;

	struct i2c_smbus_ioctl_data args;

	args.read_write = I2C_SMBUS_WRITE;
	args.command = (id & 0x0f) << 4;
	args.size = I2C_SMBUS_WORD_DATA;
	args.data = &data;

	if (ioctl(me->file, I2C_SMBUS, &args) < 0) {
		applog(LOG_ERR, "Failed to read id %d: %s\n", id,
		       strerror(errno));
		return false;
	}
	return me->get_wiper(me, id) == w;
}

void mcp4x_exit(struct mcp4x *me)
{
	close(me->file);
	free(me);
}

struct mcp4x *mcp4x_init(uint8_t addr)
{
	struct mcp4x *me;
	int file = open("/dev/i2c-1", O_RDWR);
	if (file < 0) {
		applog(LOG_INFO, "Failed to open i2c-1: %s\n", strerror(errno));
		return NULL;
	}

	if (ioctl(file, I2C_SLAVE, addr) < 0)
		return NULL;

	if (!mcp4x_check_status(file))
		return NULL;

	me = malloc(sizeof(*me));
	assert(me != NULL);

	me->addr = addr;
	me->file = file;
	me->exit = mcp4x_exit;
	me->get_wiper = mcp4x_get_wiper;
	me->set_wiper = mcp4x_set_wiper;
	return me;
}

