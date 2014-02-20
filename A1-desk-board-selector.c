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

struct pcf8575_ctx {
	uint8_t addr;
	uint8_t p0;
	uint8_t p1;
	int file;
	uint8_t active_board;
	pthread_mutex_t lock;
};

static struct pcf8575_ctx board_ctx = { 0x27, 0xff, 0xff, -1, .active_board = 255,};


#define UNUSED_BITS 0xe0
#define SLEEP_MS_AFTER_CS 0
static bool pcf8575_write(void)
{
	union i2c_smbus_data data;
	data.byte = board_ctx.p1 | UNUSED_BITS;

	struct i2c_smbus_ioctl_data args;
	__s32 err;

	args.read_write = I2C_SMBUS_WRITE;
	args.command = board_ctx.p0 | UNUSED_BITS;
	args.size = I2C_SMBUS_BYTE_DATA;
	args.data = &data;

	err = ioctl(board_ctx.file, I2C_SMBUS, &args);
	if (err == -1) {
		fprintf(stderr,
			"Error: Failed to write: %s\n",
			strerror(errno));
		err = -errno;
	} else {
		applog(LOG_DEBUG, "written: 0x%02x, 0x%02x", board_ctx.p0, board_ctx.p1);
//		usleep(25000);
		cgsleep_ms(SLEEP_MS_AFTER_CS);
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
			"Error: Could not open i2c-1: %s\n",
			board_ctx.addr, strerror(errno));
		return false;
	}

	if (ioctl(board_ctx.file, I2C_SLAVE, board_ctx.addr) < 0) {
		fprintf(stderr,
			"Error: Could not set address to 0x%02x: %s\n",
			board_ctx.addr, strerror(errno));
		return false;
	}
	return pcf8575_write();
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
	board_ctx.p0 = 1 << board_ctx.active_board;
	board_ctx.p1 = 0xff;
	bool retval = pcf8575_write();
	return retval;
}

static bool __board_selector_reset(void)
{
	board_ctx.p1 = ~board_ctx.p0;
	if (!pcf8575_write())
		return false;
	usleep(1000000);
	board_ctx.p1 = 0xff;
	if (!pcf8575_write())
		return false;
	usleep(1000000);
	return true;
}
// we assume we are already holding the mutex
bool a1_board_selector_reset_board(void)
{
//	lock_board_selector();
	bool retval = __board_selector_reset();
//	unlock_board_selector();
	return retval;
}

bool a1_board_selector_reset_all_boards(void)
{
	lock_board_selector();
	board_ctx.p1 = 0;
	bool retval = __board_selector_reset();
	unlock_board_selector();
	return retval;
}

#if 0
int main(void)
{
	if (init_pcf8575(&board_ctx)) {
		if (!pcf8575_write(&g_ctx)) {
			fprintf(stderr,
				"Error: Failed to write: %s\n",
				strerror(errno));
		}
		a1_board_selector_exit(&g_ctx);
	}
	return 0;
}
#endif
/////////////////////////////////////////////////////////////////////////////
