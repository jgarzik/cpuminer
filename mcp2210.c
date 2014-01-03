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

	err = usb_read_timeout(cgpu, buf, MCP2210_BUFFER_LENGTH, &amount, 10, cmd);
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

/* Get all the pin designations and store them in a gpio_pin struct */
bool mcp2210_get_gpio_pindes(struct cgpu_info *cgpu, struct gpio_pin *gp)
{
	char buf[MCP2210_BUFFER_LENGTH];
	int i;

	memset(buf, 0, MCP2210_BUFFER_LENGTH);
	buf[0] = MCP2210_GET_GPIO_SETTING;
	if (!mcp2210_send_recv(cgpu, buf, C_MCP_GETGPIOSETTING))
		return false;

	for (i = 0; i < 9; i++)
		gp->pin[i] = buf[4 + i];
	return true;
}


/* Get all the pin vals and store them in a gpio_pin struct */
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

/* Get all the pindirs */
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

/* Get the designation of one pin */
bool mcp2210_get_gpio_pin(struct cgpu_info *cgpu, int pin, int *des)
{
	struct gpio_pin gp;

	if (!mcp2210_get_gpio_pindes(cgpu, &gp))
		return false;

	*des = gp.pin[pin];
	return true;
}

/* Set the designation of one pin */
bool mcp2210_set_gpio_pindes(struct cgpu_info *cgpu, int pin, int des)
{
	char buf[MCP2210_BUFFER_LENGTH];

	/* Copy the current values */
	memset(buf, 0, MCP2210_BUFFER_LENGTH);
	buf[0] = MCP2210_GET_GPIO_SETTING;
	if (!mcp2210_send_recv(cgpu, buf, C_MCP_GETGPIOSETTING))
		return false;

	buf[4 + pin] = des;
	memset(buf + 18, 0, 45);
	buf[0] = MCP2210_SET_GPIO_SETTING;
	return (mcp2210_send_recv(cgpu, buf, C_MCP_SETGPIOSETTING));
}

/* Get one pinval */
bool mcp2210_get_gpio_pinval(struct cgpu_info *cgpu, int pin, int *val)
{
	char buf[MCP2210_BUFFER_LENGTH];

	memset(buf, 0, MCP2210_BUFFER_LENGTH);
	buf[0] = MCP2210_GET_GPIO_PIN_VAL;
	if (!mcp2210_send_recv(cgpu, buf, C_MCP_GETGPIOPINVAL))
		return false;

	buf[0] = MCP2210_GET_GPIO_PIN_VAL;

	if (pin < 8)
		*val = !!(buf[4] & (0x01u << pin));
	else
		*val = !!(buf[5] & 0x01u);

	return true;
}

/* Set one pinval */
bool mcp2210_set_gpio_pinval(struct cgpu_info *cgpu, int pin, int val)
{
	char buf[MCP2210_BUFFER_LENGTH];

	/* Get the current pin vals first since we're only changing one. */
	memset(buf, 0, MCP2210_BUFFER_LENGTH);
	buf[0] = MCP2210_GET_GPIO_PIN_VAL;
	if (!mcp2210_send_recv(cgpu, buf, C_MCP_GETGPIOPINVAL))
		return false;

	buf[0] = MCP2210_SET_GPIO_PIN_VAL;

	if (pin < 8) {
		if (val)
			buf[4] |= (0x01u << pin);
		else
			buf[4] &= ~(0x01u << pin);
	} else {
		if (val)
			buf[5] |= 0x01u;
		else
			buf[5] &= ~0x01u;
	}
	return mcp2210_send_recv(cgpu, buf, C_MCP_SETGPIOPINVAL);
}

/* Get one pindir */
bool mcp2210_get_gpio_pindir(struct cgpu_info *cgpu, int pin, int *dir)
{
	char buf[MCP2210_BUFFER_LENGTH];

	memset(buf, 0, MCP2210_BUFFER_LENGTH);
	buf[0] = MCP2210_GET_GPIO_PIN_DIR;
	if (!mcp2210_send_recv(cgpu, buf, C_MCP_GETGPIOPINDIR))
		return false;

	buf[0] = MCP2210_GET_GPIO_PIN_DIR;

	if (pin < 8)
		*dir = !!(buf[4] & (0x01u << pin));
	else
		*dir = !!(buf[5] & 0x01u);

	return true;
}

/* Set one pindir */
bool mcp2210_set_gpio_pindir(struct cgpu_info *cgpu, int pin, int dir)
{
	char buf[MCP2210_BUFFER_LENGTH];

	/* Get the current pin dirs first since we're only changing one. */
	memset(buf, 0, MCP2210_BUFFER_LENGTH);
	buf[0] = MCP2210_GET_GPIO_PIN_DIR;
	if (!mcp2210_send_recv(cgpu, buf, C_MCP_GETGPIOPINDIR))
		return false;

	buf[0] = MCP2210_SET_GPIO_PIN_DIR;

	if (pin < 8) {
		if (dir)
			buf[4] |= (0x01u << pin);
		else
			buf[4] &= ~(0x01u << pin);
	} else {
		if (dir)
			buf[5] |= 0x01u;
		else
			buf[5] &= ~0x01u;
	}
	return mcp2210_send_recv(cgpu, buf, C_MCP_SETGPIOPINDIR);
}
