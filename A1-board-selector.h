#ifndef A1_BOARD_SELECTOR_H
#define A1_BOARD_SELECTOR_H

#include <stdbool.h>
#include <stdint.h>

#define RESET_LOW_TIME_MS 200
#define RESET_HI_TIME_MS  100

struct board_selector {
	bool (*select)(uint8_t board);
	void (*release)(void);
	void (*exit)(void);
	bool (*reset)(void);
	bool (*reset_all)(void);
	uint8_t (*get_temp)(void);
};

static bool dummy_select(uint8_t b) { (void)b; return true; }
static void dummy_void(void) { };
static bool dummy_bool(void) { return true; }
static uint8_t dummy_u8(void) { return 0; }

static const struct board_selector dummy_board_selector = {
	.select = dummy_select,
	.release = dummy_void,
	.exit = dummy_void,
	.reset = dummy_bool,
	.reset_all = dummy_bool,
	.get_temp = dummy_u8,
};

/* CoinCraft Desk and Rig board selector constructors */
#define CCD_MAX_CHAINS	5
#define CCR_MAX_CHAINS	16
extern struct board_selector *ccd_board_selector_init(void);
extern struct board_selector *ccr_board_selector_init(void);


/* common i2c context */
struct i2c_ctx {
	uint8_t addr;
	int file;
};

/* the default I2C bus on RPi */
#define I2C_BUS		"/dev/i2c-1"

extern bool i2c_slave_write(struct i2c_ctx *ctx, uint8_t reg, uint8_t val);
extern bool i2c_slave_read(struct i2c_ctx *ctx, uint8_t reg, uint8_t *val);
extern bool i2c_slave_open(struct i2c_ctx *ctx, char *i2c_bus);
extern void i2c_slave_close(struct i2c_ctx *ctx);

#endif /* A1_BOARD_SELECTOR_H */
