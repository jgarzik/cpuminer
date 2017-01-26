#ifndef BF16_COMMUNICATION_H
#define BF16_COMMUNICATION_H

#include <stdint.h>
#include "bf16-spidevice.h"
#include "bf16-uartdevice.h"

int8_t open_spi_device(spi_channel_id_t channel_id);
int8_t open_uart_device(uart_channel_id_t channel_id);
int8_t open_ctrl_device(void);

int8_t close_spi_device(spi_channel_id_t channel_id);
int8_t close_uart_device(uart_channel_id_t channel_id);
int8_t close_ctrl_device(void);

int8_t device_spi_transfer(spi_channel_id_t channel_id, uint8_t* data, int size);
int8_t device_spi_txrx(spi_channel_id_t channel_id, uint8_t* tx, uint8_t* rx, int size);
int8_t device_uart_transfer(uart_channel_id_t channel_id, char* cmd);
int16_t device_uart_txrx(uart_channel_id_t channel_id, char* cmd, char* data);
int8_t device_ctrl_transfer(uint8_t channel_id, int state, int fn);
int8_t device_ctrl_txrx(uint8_t channel_id, int state, int fn, char* data);

#endif /* BF16_COMMUNICATION_H */
