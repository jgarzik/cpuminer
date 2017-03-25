#include "bf16-communication.h"
#include "bf16-ctrldevice.h"
#include "bf16-gpiodevice.h"
#include "miner.h"

#define MSP_BUFF_SIZE   96

static device_t*   spi0_device;
static device_t*   spi1_device;

static device_t*   uart1_device;
static device_t*   uart2_device;

static device_t*   ctrl_device;
static device_t*   gpio_device;

/* transfer functions */
int8_t device_spi_transfer(spi_channel_id_t channel_id, uint8_t* data, int size)
{
	switch (channel_id) {
	case SPI_CHANNEL1:
		memset(spi0_device->tx, 0, spi0_device->size);
		memset(spi0_device->rx, 0, spi0_device->size);
		spi0_device->datalen = size;

		cg_memcpy(spi0_device->tx, data, size);

		spi_transfer(spi0_device);

		cg_memcpy(data, spi0_device->rx, size);
		break;
	case SPI_CHANNEL2:
		memset(spi1_device->tx, 0, spi1_device->size);
		memset(spi1_device->rx, 0, spi1_device->size);
		spi1_device->datalen = size;

		cg_memcpy(spi1_device->tx, data, size);

		spi_transfer(spi1_device);

		cg_memcpy(data, spi1_device->rx, size);
		break;
	}

	return 0;
}

int8_t device_spi_txrx(spi_channel_id_t channel_id, uint8_t* tx, uint8_t* rx, int size)
{
	switch (channel_id) {
	case SPI_CHANNEL1:
		memset(spi0_device->tx, 0, spi0_device->size);
		memset(spi0_device->rx, 0, spi0_device->size);
		spi0_device->datalen = size;

		cg_memcpy(spi0_device->tx, tx, size);

		spi_transfer(spi0_device);

		cg_memcpy(rx, spi0_device->rx, size);
		break;
	case SPI_CHANNEL2:
		memset(spi1_device->tx, 0, spi1_device->size);
		memset(spi1_device->rx, 0, spi1_device->size);
		spi1_device->datalen = size;

		cg_memcpy(spi1_device->tx, tx, size);

		spi_transfer(spi1_device);

		cg_memcpy(rx, spi1_device->rx, size);
		break;
	}

	return 0;
}

static void add_crc(char* data)
{
	uint8_t crc = 0;

	while (*data) {
		crc += *data;
		data++;
	}

	sprintf(data, "#%d\n", crc);
}

int8_t device_uart_transfer(uart_channel_id_t channel_id, char* cmd)
{
	uint8_t buff[MSP_BUFF_SIZE];
	memset(buff, 0, MSP_BUFF_SIZE);

	uint16_t cmdlen = strlen(cmd);
	if (cmdlen > 0) {
		(cmdlen == 1) ? sprintf((char *)buff, "%s:", cmd) : sprintf((char *)buff, "%s", cmd);
		add_crc((char *)buff);
	} 

	switch (channel_id) {
	case UART_CHANNEL1:
		memset(uart1_device->tx, 0, uart1_device->size);
		memset(uart1_device->rx, 0, uart1_device->size);
		uart1_device->datalen = strlen((char *)buff);

		cg_memcpy(uart1_device->tx, buff, uart1_device->datalen);

		return uart_transfer(uart1_device);
		break;
	case UART_CHANNEL2:
		memset(uart2_device->tx, 0, uart2_device->size);
		memset(uart2_device->rx, 0, uart2_device->size);
		uart2_device->datalen = strlen((char *)buff);

		cg_memcpy(uart2_device->tx, buff, uart2_device->datalen);

		return uart_transfer(uart2_device);
		break;
	}

	return 0;
}

int16_t device_uart_txrx(uart_channel_id_t channel_id, char* cmd, char* data)
{
	uint8_t buff[MSP_BUFF_SIZE];
	memset(buff, 0, MSP_BUFF_SIZE);

	uint16_t cmdlen = strlen(cmd);
	if (cmdlen > 0) {
		(cmdlen == 1) ? sprintf((char *)buff, "%s:", cmd) : sprintf((char *)buff, "%s", cmd);
		add_crc((char *)buff);
	} 

	switch (channel_id) {
	case UART_CHANNEL1:
		memset(uart1_device->tx, 0, uart1_device->size);
		memset(uart1_device->rx, 0, uart1_device->size);
		uart1_device->datalen = strlen((char *)buff);

		cg_memcpy(uart1_device->tx, buff, uart1_device->datalen);

		if (uart_transfer(uart1_device) < 0)
			return -1;

		cg_memcpy(data, uart1_device->rx, uart1_device->datalen);

		return uart1_device->datalen;

		break;
	case UART_CHANNEL2:
		memset(uart2_device->tx, 0, uart2_device->size);
		memset(uart2_device->rx, 0, uart2_device->size);
		uart2_device->datalen = strlen((char *)buff);

		cg_memcpy(uart2_device->tx, buff, uart2_device->datalen);

		if (uart_transfer(uart2_device) < 0)
			return -1;

		cg_memcpy(data, uart2_device->rx, uart2_device->datalen);

		return uart2_device->datalen;

		break;
	}

	return 0;
}

int8_t device_ctrl_transfer(uint8_t channel_id, int state, int fn)
{
	int8_t ret = 0;
	char* cmd = get_ctrl_data(channel_id, state, fn);

	memset(ctrl_device->tx, 0, ctrl_device->size);
	memset(ctrl_device->rx, 0, ctrl_device->size);
	ctrl_device->datalen = strlen(cmd) + 1;

	cg_memcpy(ctrl_device->tx, cmd, ctrl_device->datalen);

	ret = ctrl_transfer(ctrl_device);

	free(cmd);

	return ret;
}

int8_t device_ctrl_txrx(uint8_t channel_id, int state, int fn, char* data)
{
	int8_t ret = 0;
	char* cmd = get_ctrl_data(channel_id, state, fn);

	memset(ctrl_device->tx, 0, ctrl_device->size);
	memset(ctrl_device->rx, 0, ctrl_device->size);
	ctrl_device->datalen = strlen(cmd) + 1;

	cg_memcpy(ctrl_device->tx, cmd, ctrl_device->datalen);

	ret = ctrl_transfer(ctrl_device);

	cg_memcpy(data, ctrl_device->rx, ctrl_device->datalen);

	free(cmd);

	return ret;
}

/* open device functions */
int8_t open_spi_device(spi_channel_id_t channel_id)
{
	switch (channel_id) {
	case SPI_CHANNEL1:
		if ((spi0_device = malloc(sizeof(device_t))) == NULL)
			quit(1, "Failed to allocate spi_device1 memory: %s", strerror(errno));

		memset(spi0_device, 0, sizeof(device_t));
		return spi_init(spi0_device, channel_id, 1, SPI_SPEED, SPI_BUFFER_SIZE);
		break;
	case SPI_CHANNEL2:
		if ((spi1_device = malloc(sizeof(device_t))) == NULL)
			quit(1, "Failed to allocate spi_device2 memory: %s", strerror(errno));

		memset(spi1_device, 0, sizeof(device_t));
		return spi_init(spi1_device, channel_id, 1, SPI_SPEED, SPI_BUFFER_SIZE);
		break;
	}

	return 0;
}

int8_t open_uart_device(uart_channel_id_t channel_id)
{
	switch (channel_id) {
	case UART_CHANNEL1:
		if ((uart1_device = malloc(sizeof(device_t))) == NULL)
			quit(1, "Failed to allocate uart_device1 memory: %s", strerror(errno));

		memset(uart1_device, 0, sizeof(device_t));
		return uart_init(uart1_device, channel_id, 0, B115200, UART_BUFFER_SIZE);
		break;

	case UART_CHANNEL2:
		if ((uart2_device = malloc(sizeof(device_t))) == NULL)
			quit(1, "Failed to allocate uart_device2 memory: %s", strerror(errno));

		memset(uart2_device, 0, sizeof(device_t));
		return uart_init(uart2_device, channel_id, 0, B115200, UART_BUFFER_SIZE);
		break;
	}

	return 0;
}

int8_t open_ctrl_device(void)
{
	if ((ctrl_device = malloc(sizeof(device_t))) == NULL)
		quit(1, "Failed to allocate ctrl_device memory: %s", strerror(errno));

	memset(ctrl_device, 0, sizeof(device_t));

	if ((gpio_device = malloc(sizeof(device_t))) == NULL)
		quit(1, "Failed to allocate gpio_device memory: %s", strerror(errno));

	memset(gpio_device, 0, sizeof(device_t));

	if (gpio_init(gpio_device, gpio_device_name, GPIO_BUFFER_SIZE) < 0)
		quit(1, "Failed to open [%s] device in open_ctrl_device", gpio_device_name);

	applog(LOG_INFO, "BF16: opened [%s] device", gpio_device_name);

	return ctrl_init(ctrl_device, ctrl_device_name, CTRL_BUFFER_SIZE);
}

/* close device functions */
int8_t close_spi_device(spi_channel_id_t channel_id)
{
	switch (channel_id) {
	case SPI_CHANNEL1:
		spi_release(spi0_device);
		free(spi0_device);
		break;
	case SPI_CHANNEL2:
		spi_release(spi1_device);
		free(spi1_device);
		break;
	}

	return 0;
}

int8_t close_uart_device(uart_channel_id_t channel_id)
{
	switch (channel_id) {
	case UART_CHANNEL1:
		uart_release(uart1_device);
		free(uart1_device);
		break;
	case UART_CHANNEL2:
		uart_release(uart2_device);
		free(uart2_device);
		break;
	}

	return 0;
}

int8_t close_ctrl_device(void)
{
	ctrl_release(ctrl_device);
	gpio_release(gpio_device);
	free(ctrl_device);
	free(gpio_device);

	return 0;
}
