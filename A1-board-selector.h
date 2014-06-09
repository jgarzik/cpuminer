#ifndef A1_BOARD_SELECTOR_H
#define A1_BOARD_SELECTOR_H

#include <stdbool.h>
#include <stdint.h>

#define RESET_LOW_TIME_MS 200
#define RESET_HI_TIME_MS  100

struct board_selector {
	/* destructor */
	void (*exit)(void);
	/* select board and chip chain for given chain index*/
	bool (*select)(uint8_t chain);
	/* release access to selected chain */
	void (*release)(void);
	/* reset currently selected chain */
	bool (*reset)(void);
	/* reset all chains on board */
	bool (*reset_all)(void);
	/* get temperature for selected chain at given sensor */
	uint8_t (*get_temp)(uint8_t sensor);
	/* prepare board (voltage) for given sys_clock */
	bool (*prepare_clock)(int clock_khz);
};

static bool dummy_select(uint8_t b) { (void)b; return true; }
static void dummy_void(void) { };
static bool dummy_bool(void) { return true; }
//static uint8_t dummy_u8(void) { return 0; }
static uint8_t dummy_get_temp(uint8_t s) { (void)s; return 0; }
static bool dummy_prepare_clock(int c) { (void)c; return true; }

static const struct board_selector dummy_board_selector = {
	.exit = dummy_void,
	.select = dummy_select,
	.release = dummy_void,
	.reset = dummy_bool,
	.reset_all = dummy_bool,
	.get_temp = dummy_get_temp,
	.prepare_clock = dummy_prepare_clock,
};

/* CoinCraft Desk and Rig board selector constructors */
#define CCD_MAX_CHAINS	5
#define CCR_MAX_CHAINS	16
extern struct board_selector *ccd_board_selector_init(void);
extern struct board_selector *ccr_board_selector_init(void);

#endif /* A1_BOARD_SELECTOR_H */
