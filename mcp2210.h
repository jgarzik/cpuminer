/*
 * Copyright 2014 Con Kolivas
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 3 of the License, or (at your option)
 * any later version.  See COPYING for more details.
 */

#ifndef MCP2210_H
#define MCP2210_H

#define MCP2210_BUFFER_LENGTH		64
#define MCP2210_TRANSFER_MAX		60

#define MCP2210_PIN_GPIO		0x0
#define MCP2210_PIN_CS			0x1
#define MCP2210_PIN_DEDICATED		0x2

#define MCP2210_GPIO_PIN_LOW		0
#define MCP2210_GPIO_PIN_HIGH		1

#define MCP2210_GPIO_OUTPUT		0
#define MCP2210_GPIO_INPUT		1

#define MCP2210_SPI_CANCEL		0x11
#define MCP2210_GET_GPIO_SETTING	0x20
#define MCP2210_SET_GPIO_SETTING	0x21
#define MCP2210_SET_GPIO_PIN_VAL	0x30
#define MCP2210_GET_GPIO_PIN_VAL	0x31
#define MCP2210_SET_GPIO_PIN_DIR	0x32
#define MCP2210_GET_GPIO_PIN_DIR	0x33
#define MCP2210_SET_SPI_SETTING		0X40
#define MCP2210_GET_SPI_SETTING		0X41
#define MCP2210_SPI_TRANSFER		0x42

#define MCP2210_SPI_TRANSFER_SUCCESS	0x00
#define MCP2210_SPI_TRANSFER_ERROR_NA	0xF7	// SPI not available due to external owner
#define MCP2210_SPI_TRANSFER_ERROR_IP	0xF8	// SPI not available due to transfer in progress

struct gpio_pin {
	uint8_t pin[9];
};

struct mcp_settings {
	struct gpio_pin designation;
	struct gpio_pin value;
	struct gpio_pin direction;
	unsigned int bitrate, icsv, acsv, cstdd, ldbtcsd, sdbd, bpst, spimode;
};

bool mcp2210_send_recv(struct cgpu_info *cgpu, char *buf, enum usb_cmds cmd);
bool mcp2210_get_gpio_settings(struct cgpu_info *cgpu, struct mcp_settings *mcp);
bool mcp2210_set_gpio_settings(struct cgpu_info *cgpu, struct mcp_settings *mcp);
bool mcp2210_get_gpio_pindes(struct cgpu_info *cgpu, struct gpio_pin *gp);
bool mcp2210_get_gpio_pinvals(struct cgpu_info *cgpu, struct gpio_pin *gp);
bool mcp2210_get_gpio_pindirs(struct cgpu_info *cgpu, struct gpio_pin *gp);
bool mcp2210_get_gpio_pin(struct cgpu_info *cgpu, int pin, int *des);
bool mcp2210_get_gpio_pinval(struct cgpu_info *cgpu, int pin, int *val);
bool mcp2210_get_gpio_pindir(struct cgpu_info *cgpu, int pin, int *dir);
bool mcp2210_spi_cancel(struct cgpu_info *cgpu);
bool
mcp2210_get_spi_transfer_settings(struct cgpu_info *cgpu, unsigned int *bitrate, unsigned int *icsv,
				  unsigned int *acsv, unsigned int *cstdd, unsigned int *ldbtcsd,
				  unsigned int *sdbd, unsigned int *bpst, unsigned int *spimode);
bool
mcp2210_set_spi_transfer_settings(struct cgpu_info *cgpu, unsigned int bitrate, unsigned int icsv,
				  unsigned int acsv, unsigned int cstdd, unsigned int ldbtcsd,
				  unsigned int sdbd, unsigned int bpst, unsigned int spimode);
bool mcp2210_spi_transfer(struct cgpu_info *cgpu, struct mcp_settings *mcp,
			  char *data, unsigned int *length);

#endif /* MCP2210_H */
