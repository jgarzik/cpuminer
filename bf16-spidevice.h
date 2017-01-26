#ifndef BF16_SPIDEVICE_H
#define BF16_SPIDEVICE_H

#include "bf16-device.h"

#define SPI_BUFFER_SIZE  4096
#define SPI_SPEED        20000000

typedef enum {
	SPI_CHANNEL1 = 1,
	SPI_CHANNEL2
} spi_channel_id_t;

extern char *spi0_device_name;
extern char *spi1_device_name;

int8_t spi_init(device_t* attr, spi_channel_id_t channel_id,
		int8_t mode, uint32_t speed, uint16_t size);
void spi_transfer(device_t *attr);
void spi_release(device_t *attr);

#endif /* BF16_SPIDEVICE_H */
