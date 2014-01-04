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
		gp->pin[i] = !!(buf[4 + i]);
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
		gp->pin[i] = !!(buf[4] & (0x01u << i));
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
		gp->pin[i] = !!(buf[4] & (0x01u << i));
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

/* Set one pin to gpio output and set the value */
bool mcp2210_set_gpio_output(struct cgpu_info *cgpu, int pin, int val)
{
	if (!mcp2210_set_gpio_pindes(cgpu, pin, MCP2210_PIN_GPIO))
		return false;
	if (!mcp2210_set_gpio_pindir(cgpu, pin, MCP2210_GPIO_OUTPUT))
		return false;
	if (!mcp2210_set_gpio_pinval(cgpu, pin, val))
		return false;
	return true;
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

bool mcp2210_spi_cancel(struct cgpu_info *cgpu)
{
	char buf[MCP2210_BUFFER_LENGTH];

	memset(buf, 0, MCP2210_BUFFER_LENGTH);
	buf[0] = MCP2210_SPI_CANCEL;
	return mcp2210_send_recv(cgpu, buf, C_MCP_SPICANCEL);
}

/* Abbreviations correspond to:
 * IdleChipSelectValue, ActiveChipSelectValue, CSToDataDelay, LastDataByteToCSDelay,
 * SubsequentDataByteDelay, BytesPerSPITransfer
 */
bool
mcp2210_get_spi_transfer_settings(struct cgpu_info *cgpu, unsigned int *bitrate, unsigned int *icsv,
				  unsigned int *acsv, unsigned int *cstdd, unsigned int *ldbtcsd,
				  unsigned int *sdbd, unsigned int *bpst, unsigned int *spimode)
{
	char buf[MCP2210_BUFFER_LENGTH];

	memset(buf, 0, MCP2210_BUFFER_LENGTH);
	buf[0] = MCP2210_GET_SPI_SETTING;

	if (!mcp2210_send_recv(cgpu, buf, C_MCP_GETSPISETTING))
		return false;
	*bitrate = buf[7] << 24 | buf[6] << 16 | buf[5] << 8 | buf[4];
	*icsv = (buf[9] & 0x1) << 8 | buf[8];
	*acsv = (buf[11] & 0x1) << 8 | buf[10];
	*cstdd = buf[13] << 8 | buf[12];
	*ldbtcsd = buf[15] << 8 | buf[14];
	*sdbd = buf[17] << 8 | buf[16];
	*bpst = buf[19] << 8 | buf[18];
	*spimode = buf[20];
	return true;
}

bool
mcp2210_set_spi_transfer_settings(struct cgpu_info *cgpu, unsigned int bitrate, unsigned int icsv,
				  unsigned int acsv, unsigned int cstdd, unsigned int ldbtcsd,
				  unsigned int sdbd, unsigned int bpst, unsigned int spimode)
{
	char buf[MCP2210_BUFFER_LENGTH];

	memset(buf, 0, MCP2210_BUFFER_LENGTH);
	buf[0] = MCP2210_SET_SPI_SETTING;

	buf[4] = bitrate & 0xfful;
	buf[5] = (bitrate & 0xff00ul) >> 8;
	buf[6] = (bitrate & 0xff0000ul) >> 16;
	buf[7] = (bitrate & 0xff000000ul) >> 24;

	buf[8] = icsv & 0xff;
	buf[9] = (icsv & 0x100) >> 8;

	buf[10] = acsv & 0xff;
	buf[11] = (acsv & 0x100) >> 8;

	buf[12] = cstdd & 0xff;
	buf[13] = (cstdd & 0xff00) >> 8;

	buf[14] = ldbtcsd & 0xff;
	buf[15] = (ldbtcsd & 0xff00) >> 8;

	buf[16] = sdbd & 0xff;
	buf[17] = (sdbd & 0xff00) >> 8;

	buf[18] = bpst & 0xff;
	buf[19] = (bpst & 0xff00) >> 8;

	buf[20] = spimode;
	return mcp2210_send_recv(cgpu, buf, C_MCP_SETSPISETTING);
}

/* Perform an spi transfer of *length bytes and return the amount of data
 * returned in the same buffer in *length */
bool mcp2210_spi_transfer(struct cgpu_info *cgpu, char *data, unsigned int *length)
{
	char buf[MCP2210_BUFFER_LENGTH];
	uint8_t res;

	if (unlikely(*length > MCP2210_TRANSFER_MAX || !*length)) {
		applog(LOG_ERR, "%s %d: Unable to spi transfer %u bytes", cgpu->drv->name,
		       cgpu->device_id, *length);
		return false;
	}
retry:
	applog(LOG_DEBUG, "%s %d: SPI sending %u bytes", cgpu->drv->name, cgpu->device_id,
	       *length);
	memset(buf, 0, MCP2210_BUFFER_LENGTH);
	buf[0] = MCP2210_SPI_TRANSFER;
	buf[1] = *length;

	memcpy(buf + 4, data, *length);
	if (!mcp2210_send_recv(cgpu, buf, C_MCP_SPITRANSFER))
		return false;

	res = (uint8_t)buf[1];
	switch(res) {
		case MCP2210_SPI_TRANSFER_SUCCESS:
			*length = buf[2];
			applog(LOG_DEBUG, "%s %d: SPI transfer success, received %u bytes",
			       cgpu->drv->name, cgpu->device_id, *length);
			if (*length)
				memcpy(data, buf + 4, *length);
			return true;
		case MCP2210_SPI_TRANSFER_ERROR_IP:
			applog(LOG_DEBUG, "%s %d: SPI transfer error in progress",
			       cgpu->drv->name, cgpu->device_id);
			cgsleep_ms(40);
			goto retry;
		case MCP2210_SPI_TRANSFER_ERROR_NA:
			applog(LOG_WARNING, "%s %d: External owner error on mcp2210 spi transfer",
			       cgpu->drv->name, cgpu->device_id);
		default:
			return false;
	}
}
