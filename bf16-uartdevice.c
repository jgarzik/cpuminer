#include <fcntl.h>
#include <unistd.h>

#include "bf16-uartdevice.h"
#include "miner.h"

#define SRV_TIMEOUT 2

char *uart1_device_name = "/dev/ttyO1";
char *uart2_device_name = "/dev/ttyO4";

int8_t uart_init(device_t* attr, uart_channel_id_t channel_id, int8_t mode, uint32_t speed, uint16_t size)
{
	switch (channel_id) {
	case UART_CHANNEL1:
		attr->device = uart1_device_name;
		break;
	case UART_CHANNEL2:
		attr->device = uart2_device_name;
		break;
	}

	attr->mode = mode;
	attr->speed = speed;
	attr->bits = 8;
	attr->size = size;
	attr->rx = malloc(size);
	attr->tx = malloc(size);

	int fd;
	if ((fd = open(attr->device, O_RDWR | O_NOCTTY | O_NDELAY)) < 0)
		quit(1, "BF16: %s() failed to open device [%s]: %s",
				__func__, attr->device, strerror(errno));

	attr->fd = fd;

	if (fcntl(fd, F_SETFL, 0) < 0)
		quit(1, "BF16: %s() failed to set descriptor status flags [%s]: %s",
				__func__, attr->device, strerror(errno));

	struct termios settings;
	if (tcgetattr(fd, &settings) < 0)
		quit(1, "BF16: %s() failed to get device attributes [%s]: %s",
				__func__, attr->device, strerror(errno));

	settings.c_cflag &= ~PARENB; /* no parity */
	settings.c_cflag &= ~CSTOPB; /* 1 stop bit */
	settings.c_cflag &= ~CSIZE;
	settings.c_cflag |= CS8 | CLOCAL | CREAD; /* 8 bits */
	settings.c_cc[VMIN] = 1;
	settings.c_cc[VTIME] = 2;
	settings.c_lflag = ICANON; /* canonical mode */
	settings.c_oflag &= ~OPOST; /* raw output */

	if (cfsetospeed(&settings, speed) < 0)
		quit(1, "BF16: %s() failed to set device output speed [%s]: %s",
				__func__, attr->device, strerror(errno));

	if (cfsetispeed(&settings, speed) < 0)
		quit(1, "BF16: %s() failed to set device input speed [%s]: %s",
				__func__, attr->device, strerror(errno));

	if (tcsetattr(fd, TCSANOW, &settings) < 0)
		quit(1, "BF16: %s() failed to get device attributes [%s]: %s",
				__func__, attr->device, strerror(errno));

	if (tcflush(fd, TCOFLUSH) < 0)
		quit(1, "BF16: %s() failed to flush device data [%s]: %s",
				__func__, attr->device, strerror(errno));

	return 0;
}

/* TODO: add errors processing */
static int read_to(int fd, uint8_t* buffer, int len, int timeout)
{
	fd_set readset;
	int result;
	struct timeval tv;

	FD_ZERO(&readset);
	FD_SET(fd, &readset);

	tv.tv_sec = timeout;
	tv.tv_usec = 0;
	result = select(fd + 1, &readset, NULL, NULL, &tv);
 
	if (result < 0)
		return result;
	else if (result > 0 && FD_ISSET(fd, &readset)) {
		result = read(fd, buffer, len);
		return result;
	}
	return -2;
}

static int write_to(int fd, uint8_t* buffer, int len, int timeout)
{
	fd_set writeset;
	int result;
	struct timeval tv;

	FD_ZERO(&writeset);
	FD_SET(fd, &writeset);

	tv.tv_sec = timeout;
	tv.tv_usec = 0;
	result = select(fd + 1, NULL, &writeset, NULL, &tv);

	if (result < 0)
		return result;
	else if (result > 0 && FD_ISSET(fd, &writeset)) {
		result = write(fd, buffer, len);
		return result;
	}

	return -2;
}

int8_t uart_transfer(device_t *attr)
{
	int ret;

	if ((ret = write_to(attr->fd, attr->tx, attr->datalen, SRV_TIMEOUT)) < 1) {
		applog(LOG_ERR, "BF16: %s() failed to send UART message to [%s]: %s",
				__func__, attr->device, strerror(errno));

		attr->datalen = 0;
		return -1;
	}

	if ((ret = read_to(attr->fd, attr->rx, attr->size, SRV_TIMEOUT)) < 1) {
		applog(LOG_ERR, "BF16: %s() failed to read UART message from [%s]: %s",
				__func__, attr->device, strerror(errno));

		attr->datalen = 0;
		return -1;
	}

	attr->datalen = ret;
	return 0;
}

void uart_release(device_t *attr)
{
	free(attr->rx);
	free(attr->tx);
	close(attr->fd);
}
