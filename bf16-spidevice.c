#include <fcntl.h>
#include <sys/ioctl.h>
#include <linux/spi/spidev.h>
#include <unistd.h>

#include "bf16-spidevice.h"
#include "miner.h"

char *spi0_device_name  = "/dev/spidev1.1";
char *spi1_device_name  = "/dev/spidev2.1";

int8_t spi_init(device_t* attr, spi_channel_id_t channel_id, int8_t mode, uint32_t speed, uint16_t size)
{
	switch (channel_id) {
	case SPI_CHANNEL1:
		attr->device = spi1_device_name;
		break;
	case SPI_CHANNEL2:
		attr->device = spi0_device_name;
		break;
	}

	attr->mode = mode;
	attr->speed = speed;
	attr->bits = 8;
	attr->size = size;
	attr->rx = malloc(size);
	attr->tx = malloc(size);

	int fd;
	if ((fd = open(attr->device, O_RDWR)) < 0) {
		applog(LOG_ERR, "BF16: %s() failed to open device [%s]: %s",
				__func__, attr->device, strerror(errno));
		return -1;
	}

	/* SPI mode */
	if (ioctl(fd, SPI_IOC_WR_MODE, &(attr->mode)) < 0) {
		applog(LOG_ERR, "BF16: %s() failed to set SPI mode: %s", __func__, strerror(errno));
		return -1;
	}

	if (ioctl(fd, SPI_IOC_RD_MODE, &(attr->mode)) < 0) {
		applog(LOG_ERR, "BF16: %s() failed to get SPI mode: %s", __func__, strerror(errno));
		return -1;
	}

	/* bits per word */
	if (ioctl(fd, SPI_IOC_WR_BITS_PER_WORD, &(attr->bits)) < 0) {
		applog(LOG_ERR, "BF16: %s() failed to set SPI bits per word: %s", __func__, strerror(errno));
		return -1;
	}

	if (ioctl(fd, SPI_IOC_RD_BITS_PER_WORD, &(attr->bits)) < 0) {
		applog(LOG_ERR, "BF16: %s() failed to get SPI bits per word: %s", __func__, strerror(errno));
		return -1;
	}

	/* max speed hz */
	if (ioctl(fd, SPI_IOC_WR_MAX_SPEED_HZ, &(attr->speed)) < 0) {
		applog(LOG_ERR, "BF16: %s() failed to set SPI max speed hz: %s", __func__, strerror(errno));
		return -1;
	}

	if (ioctl(fd, SPI_IOC_RD_MAX_SPEED_HZ, &(attr->speed)) < 0) {
		applog(LOG_ERR, "BF16: %s() failed to get SPI max speed hz: %s", __func__, strerror(errno));
		return -1;
	}

	attr->fd = fd;

	return 0;
}

void spi_transfer(device_t *attr)
{
	struct spi_ioc_transfer tr = {
		.tx_buf = (unsigned long) (attr->tx),
		.rx_buf = (unsigned long) (attr->rx),
		.len = attr->datalen,
		.delay_usecs = attr->delay,
		.speed_hz = attr->speed,
		.bits_per_word = attr->bits
	};

	if (ioctl(attr->fd, SPI_IOC_MESSAGE(1), &tr) < 0)
		quit(1, "BF16: %s() failed to send SPI message: %s", __func__, strerror(errno));

#if 0
	uint16_t i;
	char data[2*4096];
	memset(data, 0, sizeof(data));
	for (i = 0; i < attr->datalen; i++)
		sprintf(data, "%s%02x", data, attr->tx[i]);
	applog(LOG_DEBUG, "BF16: TX -> [%s]", data);

	memset(data, 0, sizeof(data));
	for (i = 0; i < attr->datalen; i++)
		sprintf(data, "%s%02x", data, attr->rx[i]);
	applog(LOG_DEBUG, "BF16: RX <- [%s]", data);
#endif

}

void spi_release(device_t *attr)
{
	free(attr->rx);
	free(attr->tx);
	close(attr->fd);
}
