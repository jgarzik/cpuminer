/*
 * Copyright 2014 Con Kolivas
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 3 of the License, or (at your option)
 * any later version.  See COPYING for more details.
 */
#include "miner.h"
#include "usbutils.h"
#include "mcp2210.h"

bool mcp2210_send_recv(struct cgpu_info *cgpu, char *buf, enum usb_cmds cmd)
{
	uint8_t mcp_cmd = buf[0];
	int amount, err;

	if (unlikely(cgpu->usbinfo.nodev))
		return false;

	err = usb_write(cgpu, buf, MCP2210_BUFFER_LENGTH, &amount, cmd);
	if (err || amount != MCP2210_BUFFER_LENGTH) {
		applog(LOG_WARNING, "%s %d: Error %d sending %s sent %d of %d",
		       cgpu->drv->name, cgpu->device_id, err, usb_cmdname(cmd),
		       amount, MCP2210_BUFFER_LENGTH);
		return false;
	}

	err = usb_read(cgpu, buf, MCP2210_BUFFER_LENGTH, &amount, cmd);
	if (err || amount != MCP2210_BUFFER_LENGTH) {
		applog(LOG_WARNING, "%s %d: Error %d receiving %s received %d of %d",
		       cgpu->drv->name, cgpu->device_id, err, usb_cmdname(cmd),
		       amount, MCP2210_BUFFER_LENGTH);
		return false;
	}

	/* Return code should always echo original command */
	if (buf[0] != mcp_cmd) {
		applog(LOG_WARNING, "%s %d: Response code mismatch, asked for %u got %u",
		       cgpu->drv->name, cgpu->device_id, mcp_cmd, buf[0]);
		return false;
	}
	return true;
}

bool mcp2210_get_gpio_pinvals(struct cgpu_info *cgpu, struct gpio_pin *gp)
{
	char buf[MCP2210_BUFFER_LENGTH];
	int i;

	memset(buf, 0, MCP2210_BUFFER_LENGTH);
	buf[0] = MCP2210_GET_GPIO_PIN_VAL;
	if (!mcp2210_send_recv(cgpu, buf, C_MCP_GETGPIOPINVAL))
		return false;

	for (i = 0; i < 8; i++)
		gp->pin[i] = buf[4] & (0x01u << i);
	gp->pin[8] = buf[5] & 0x01u;

	return true;
}

bool mcp2210_get_gpio_pindirs(struct cgpu_info *cgpu, struct gpio_pin *gp)
{
	char buf[MCP2210_BUFFER_LENGTH];
	int i;

	memset(buf, 0, MCP2210_BUFFER_LENGTH);
	buf[0] = MCP2210_GET_GPIO_PIN_DIR;
	if (!mcp2210_send_recv(cgpu, buf, C_MCP_GETGPIOPINDIR))
		return false;

	for (i = 0; i < 8; i++)
		gp->pin[i] = buf[4] & (0x01u << i);
	gp->pin[8] = buf[5] & 0x01u;

	return true;
}
