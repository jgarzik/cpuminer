#ifndef BF16_UARTDEVICE_H
#define BF16_UARTDEVICE_H

#include <termios.h>

#include "bf16-device.h"

#define UART_BUFFER_SIZE 128

typedef enum {
	UART_CHANNEL1 = 1,
	UART_CHANNEL2
} uart_channel_id_t;

extern char *uart1_device_name;
extern char *uart2_device_name;

int8_t uart_init(device_t* attr, uart_channel_id_t channel_id,
		int8_t mode, uint32_t speed, uint16_t size);
int8_t uart_transfer(device_t *attr);
void uart_release(device_t *attr);

#endif /* BF16_UARTDEVICE_H */
