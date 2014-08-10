/*
 * Direct SPI transport layer for KnCminer Jupiters
 *
 * Copyright 2014 KnCminer
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 3 of the License, or (at your option)
 * any later version.  See COPYING for more details.
 */

#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <linux/spi/spidev.h>

#include "logging.h"
#include "miner.h"
#include "hexdump.c"
#include "knc-transport.h"

#define SPI_DEVICE_TEMPLATE	"/dev/spidev%d.%d"
#define SPI_MODE		(SPI_CPHA | SPI_CPOL | SPI_CS_HIGH)
#define SPI_BITS_PER_WORD	8
#define SPI_MAX_SPEED		3000000
#define SPI_DELAY_USECS		0

struct spidev_context {
	int fd;
	uint32_t speed;
	uint16_t delay;
	uint8_t mode;
	uint8_t bits;
};

/* Init SPI transport */
void *knc_trnsp_new(int dev_idx)
{
	struct spidev_context *ctx;
	char dev_name[PATH_MAX];

	if (NULL == (ctx = malloc(sizeof(struct spidev_context)))) {
		applog(LOG_ERR, "KnC transport: Out of memory");
		goto l_exit_error;
	}
	ctx->mode = SPI_MODE;
	ctx->bits = SPI_BITS_PER_WORD;
	ctx->speed = SPI_MAX_SPEED;
	ctx->delay = SPI_DELAY_USECS;

	ctx->fd = -1;
	sprintf(dev_name, SPI_DEVICE_TEMPLATE,
		dev_idx + 1, /* bus */
		0    /* chipselect */
	       );
	if (0 > (ctx->fd = open(dev_name, O_RDWR))) {
		applog(LOG_ERR, "KnC transport: Can not open SPI device %s: %m",
		       dev_name);
		goto l_free_exit_error;
	}

	/*
	 * spi mode
	 */
	if (0 > ioctl(ctx->fd, SPI_IOC_WR_MODE, &ctx->mode))
		goto l_ioctl_error;
	if (0 > ioctl(ctx->fd, SPI_IOC_RD_MODE, &ctx->mode))
		goto l_ioctl_error;

	/*
	 * bits per word
	 */
	if (0 > ioctl(ctx->fd, SPI_IOC_WR_BITS_PER_WORD, &ctx->bits))
		goto l_ioctl_error;
	if (0 > ioctl(ctx->fd, SPI_IOC_RD_BITS_PER_WORD, &ctx->bits))
		goto l_ioctl_error;

	/*
	 * max speed hz
	 */
	if (0 > ioctl(ctx->fd, SPI_IOC_WR_MAX_SPEED_HZ, &ctx->speed))
		goto l_ioctl_error;
	if (0 > ioctl(ctx->fd, SPI_IOC_RD_MAX_SPEED_HZ, &ctx->speed))
		goto l_ioctl_error;

	applog(LOG_INFO, "KnC transport: SPI device %s uses mode %hhu, bits %hhu, speed %u",
	       dev_name, ctx->mode, ctx->bits, ctx->speed);

	return ctx;

l_ioctl_error:
	applog(LOG_ERR, "KnC transport: ioctl error on SPI device %s: %m", dev_name);
	close(ctx->fd);
l_free_exit_error:
	free(ctx);
l_exit_error:
	return NULL;
}

void knc_trnsp_free(void *opaque_ctx)
{
	struct spidev_context *ctx = opaque_ctx;

	if (NULL == ctx)
		return;

	close(ctx->fd);
	free(ctx);
}

int knc_trnsp_transfer(void *opaque_ctx, uint8_t *txbuf, uint8_t *rxbuf, int len)
{
	struct spidev_context *ctx = opaque_ctx;
	struct spi_ioc_transfer xfr;
	int ret;

	memset(rxbuf, 0xff, len);

	ret = len;

	xfr.tx_buf = (unsigned long)txbuf;
	xfr.rx_buf = (unsigned long)rxbuf;
	xfr.len = len;
	xfr.speed_hz = ctx->speed;
	xfr.delay_usecs = ctx->delay;
	xfr.bits_per_word = ctx->bits;
	xfr.cs_change = 0;
	xfr.pad = 0;

        applog(LOG_DEBUG, "KnC spi:");
        hexdump(txbuf, len);
	if (1 > (ret = ioctl(ctx->fd, SPI_IOC_MESSAGE(1), &xfr)))
		applog(LOG_ERR, "KnC spi xfer: ioctl error on SPI device: %m");
        hexdump(rxbuf, len);

	return ret;
}

bool knc_trnsp_asic_detect(void *opaque_ctx, int chip_id)
{
	return true;
}

void knc_trnsp_periodic_check(void *opaque_ctx)
{
	return;
}

