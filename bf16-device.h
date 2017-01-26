#ifndef BF16_DEVICE_H
#define BF16_DEVICE_H

#include <stdint.h>

typedef struct {
	char        *device;
	int         fd;

	uint8_t     mode;
	uint8_t     bits;
	uint32_t    speed;
	uint16_t    delay;

	uint16_t    datalen;
	uint16_t    size;
	uint8_t     *rx;
	uint8_t     *tx;
} device_t;

#endif /* BF16_DEVICE_H */
