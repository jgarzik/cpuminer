#include <sys/ioctl.h>
#include <errno.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <linux/i2c.h>
#include <linux/i2c-dev.h>

#include <stdint.h>
#include <stdbool.h>
#include <fcntl.h>

#include "miner.h"

struct tca9535_ctx {
	uint8_t addr;
	uint8_t board_mask;
	int file;
	uint8_t active_board;
	pthread_mutex_t lock;
};

static struct tca9535_ctx board_ctx = {
	.addr = 0x27,
	.board_mask = 0xff,
	.file = -1,
	.active_board = 255,
};


#define UNUSED_BITS 0xe0
#define SLEEP_US_AFTER_CS 200
#define RESET_LOW_TIME_MS 200
#define RESET_HI_TIME_MS  100

static bool tca9535_write(int fdesc, uint8_t p0, uint8_t p1)
{
	union i2c_smbus_data data;
	data.byte = p1;

	struct i2c_smbus_ioctl_data args;
	__s32 err;

	args.read_write = I2C_SMBUS_WRITE;
	args.command = p0;
	args.size = I2C_SMBUS_BYTE_DATA;
	args.data = &data;

	err = ioctl(fdesc, I2C_SMBUS, &args);
	if (err == -1) {
		applog(LOG_ERR, "Failed to write to fdesc %d: %s\n",
		       fdesc, strerror(errno));
		err = -errno;
	} else {
		applog(LOG_DEBUG, "written: 0x%02x, 0x%02x", p0, p1);
		cgsleep_us(SLEEP_US_AFTER_CS);
	}
	return err == 0;
}

void lock_board_selector(void)
{
//	applog(LOG_WARNING, "lock_board_selector()");
	mutex_lock(&board_ctx.lock);
}

void unlock_board_selector(void)
{
//	applog(LOG_WARNING, "unlock_board_selector()");
	mutex_unlock(&board_ctx.lock);
}

bool a1_board_selector_init(void)
{
	mutex_init(&board_ctx.lock);
	applog(LOG_WARNING, "a1_board_selector_init()");

	board_ctx.file = open("/dev/i2c-1", O_RDWR);
	if (board_ctx.file < 0) {
		fprintf(stderr,
			"Error: Could not open i2c-1.%d: %s\n",
			board_ctx.addr, strerror(errno));
		return false;
	}

	if (ioctl(board_ctx.file, I2C_SLAVE, board_ctx.addr) < 0) {
		fprintf(stderr,
			"Error: Could not set address to 0x%02x: %s\n",
			board_ctx.addr, strerror(errno));
		return false;
	}
	bool retval =	tca9535_write(board_ctx.file, 0x06, 0xe0) &&
			tca9535_write(board_ctx.file, 0x07, 0xe0) &&
			tca9535_write(board_ctx.file, 0x02, 0x1f) &&
			tca9535_write(board_ctx.file, 0x03, 0x00);
	return retval;
}

void a1_board_selector_exit(void)
{
	close(board_ctx.file);
	board_ctx.file = -1;
}

bool a1_board_selector_select_board(uint8_t board)
{
	if (board > 7)
		return false;

//	applog(LOG_WARNING, "board_selector_select_board(%d)", board);
	lock_board_selector();
	if (board_ctx.active_board == board)
		return true;

	board_ctx.active_board = board;
	board_ctx.board_mask = 1 << board_ctx.active_board;
	return tca9535_write(board_ctx.file, 0x02, ~board_ctx.board_mask);
}

static bool __board_selector_reset(void)
{
	if (!tca9535_write(board_ctx.file, 0x03, board_ctx.board_mask))
		return false;
	cgsleep_ms(RESET_LOW_TIME_MS);
	if (!tca9535_write(board_ctx.file, 0x03, 0x00))
		return false;
	cgsleep_ms(RESET_HI_TIME_MS);
	return true;
}
// we assume we are already holding the mutex
bool a1_board_selector_reset_board(void)
{
	bool retval = __board_selector_reset();
	return retval;
}

bool a1_board_selector_reset_all_boards(void)
{
	lock_board_selector();
	board_ctx.board_mask = 0xff & ~UNUSED_BITS;
	bool retval = __board_selector_reset();
	unlock_board_selector();
	return retval;
}

/////////////////////////////////////////////////////////////////////////////
