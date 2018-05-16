#ifndef _DRAGONMINT_T1_
#define _DRAGONMINT_T1_

#include "stdint.h"
#include "stdbool.h"

#include "util.h"
#include "elist.h"

#include "dm_compat.h"

#define MAX_CHIP_NUM						(68)
#define MAX_CHAIN_NUM						(3)
#define MAX_CORE_NUM						(32)

#define MAX_CORES						(MAX_CHIP_NUM * MAX_CORE_NUM)
#define MAX_CMD_LENGTH						(JOB_LENGTH + MAX_CHIP_NUM * 2 * 2)

#define CMD_TYPE_T1						(0x0)

#define JOB_LENGTH						(162)
#define NONCE_LEN						(6)

#define T1_PLL_LV_NUM                  	(324)
#define T1_PLL_SETSPI					(310)
#define T1_PLL_SETVID					(1000)

#define T1_SPI_SPEED_DEF				SPI_SPEED_390K

#define STARTUP_VID						(0)
#define DEFAULT_PLL						(1332)
#define MIN_PLL							(1200)
#define MAX_PLL							(1392)

#define DEFAULT_VOLT						(404)
#define TUNE_VOLT_START_EFF					(410)
#define TUNE_VOLT_START_BAL					(415)
#define TUNE_VOLT_START_PER					(420)

#define TUNE_VOLT_STOP						(390)
#define CHIP_VOLT_MAX						(445)
#define CHIP_VOLT_MIN						(380)

#define USE_BISTMASK
//#define USE_AUTONONCE
#define USE_AUTOCMD0A

#define WEAK_CHIP_THRESHOLD 5
#define BROKEN_CHIP_THRESHOLD 5

#define DRAGONMINT_MINER_TYPE_FILE				"/tmp/type"
#define DRAGONMINT_HARDWARE_VERSION_FILE			"/tmp/hwver"
#define DRAGONMINT_CHIP_NUM_FILE				"/tmp/chip_nums"
#define MINER_AGEING_STATUS_FILE				"/tmp/ageingStatus"

#define T1_PLL(prediv,fbdiv,postdiv) ((prediv<<(89-64))|fbdiv<<(80-64)|0b010<<(77-64)|postdiv<<(70-64))

#define T1_PLL_MIN						(0)	// 120 MHz
#define T1_PLL_TUNE_MIN						(290)	// 1200 MHz
#define T1_PLL_TUNE_MAX						(323)	// 1596 MHz
#define T1_PLL_MAX						(323)	// 1596 MHz

#define T1_PLL_TUNE_RANGE					(T1_PLL_TUNE_MAX - T1_PLL_TUNE_MIN + 1)

/* Low iVid corresponds with high voltage */
#define T1_VID_MIN						(0)
#define T1_VID_MAX						(31)

#define T1_VID_TUNE_RANGE					(T1_VID_MAX - T1_VID_MIN + 1)

#define T1_CYCLES_CHAIN						(666)

typedef enum {
	HARDWARE_VERSION_NONE = 0x00,
	HARDWARE_VERSION_G9 = 0x09,
	HARDWARE_VERSION_G19 = 0x13,
} hardware_version_e;

typedef enum {
	AGEING_BIST_START_FAILED = 1,
	AGEING_BIST_FIX_FAILED,
	AGEING_CONFIG_PLL_FAILED,
	AGEING_PLUG_STATUS_ERROR,
	AGEING_SPI_STATUS_ERROR,
	AGEING_RUNNING_CONNECT_POOL_FAILED,
	AGEING_INIT_CONNECT_POOL_FAILED,
	AGEING_ALL_SPI_STATUS_ERROR,
	AGEING_HW_VERSION_ERROR,
	AGEING_TEMP_IS_OVERHEAT,
	AGEING_STATUS_MAX,
} MINER_AGEING_STATUS;

typedef struct {
	double highest_vol[MAX_CHAIN_NUM];    /* chip temp bits */;
	double lowest_vol[MAX_CHAIN_NUM];    /* chip temp bits */;
	double average_vol[MAX_CHAIN_NUM];    /* chip temp bits */;
	int stat_val[MAX_CHAIN_NUM][MAX_CHIP_NUM];
	int stat_cnt[MAX_CHAIN_NUM][MAX_CHIP_NUM];
} dragonmint_reg_ctrl_t;

struct work_ent {
	struct work *work;
	struct list_head head;
};

struct work_queue {
	int num_elems;
	struct list_head head;
};

struct T1_chip {
	uint8_t reg[REG_LENGTH];
	int num_cores;
	int last_queued_id;
	struct work *work[4];
	/* stats */
	int hw_errors;
	int stales;
	int dupes;
	int nonces_found;
	int nonce_ranges_done;

	/* systime in ms when chip was disabled */
	int cooldown_begin;
	/* number of consecutive failures to access the chip */
	int fail_count;
	int fail_reset;
	/* mark chip disabled, do not try to re-enable it */
	bool disabled;

	int temp;
	int nVol;

	uint32_t last_nonce;
};

struct T1_chain {
	int chain_id;
	struct cgpu_info *cgpu;
	int num_chips;
	int num_cores;
	int num_active_chips;
	int chain_skew;
	struct spi_ctx *spi_ctx;
	struct T1_chip *chips;

	pthread_mutex_t lock;
	pthread_cond_t cond;

	struct work_queue active_wq;

	/* mark chain disabled, do not try to re-enable it */
	bool disabled;
	bool throttle; /* Needs throttling */

	struct timeval cycle_start;

	int cycles; /* Cycles used for iVid tuning */
	int hw_errors;

	int pll; /* Current chain speed */
	int base_pll; /* Initial chain speed */

	int iVid; /* Current actual iVid */
	int base_iVid; /* Initial iVid */
	int optimalVid; /* Vid after last tune */
	int optimal_vol; /* Optimal voltage found after VID tuning */

	double vidproduct[T1_VID_TUNE_RANGE]; // Hashrate product vs vid level
	double vidhwerr[T1_VID_TUNE_RANGE]; // hwerr vs vid level
	/* Double may give more precision than int since it's an average voltage */
	double vidvol[T1_VID_TUNE_RANGE]; // What the voltage is per vid level

	double pllproduct[T1_PLL_TUNE_RANGE]; // Hashrate product vs pll level
	double pllhwerr[T1_PLL_TUNE_RANGE]; // hwerr vs pll level
	int pllvid[T1_PLL_TUNE_RANGE]; // Associated VID per pll

	bool VidOptimal; // We've stopped tuning voltage
	bool pllOptimal; // We've stopped tuning frequency
	bool sampling; // Results are valid for tuning

	time_t throttled; // Currently throttled time for heat
	time_t lastshare;

	cgtimer_t cgt; /* Main work loop reentrant timer */
};

struct PLL_Clock {
	uint32_t num;   // divider 1000
	uint32_t speedMHz;      // unit MHz
	uint32_t pll_reg;
};

struct T1_config_options {
	int ref_clk_khz;
	int sys_clk_khz;
	int spi_clk_khz;
	/* limit chip chain to this number of chips (testing only) */
	int override_chip_num;
	int wiper;
};

unsigned short CRC16_2(unsigned char* pchMsg, unsigned short wDataLen);
void hexdump_error(char *prefix, uint8_t *buff, int len);
void hexdump(char *prefix, uint8_t *buff, int len);

bool dm_cmd_resetall(uint8_t chain_id, uint8_t chip_id, uint8_t *result);
bool dm_cmd_resetjob(uint8_t chain_id, uint8_t chip_id, uint8_t *result);
bool dm_cmd_resetbist(uint8_t chain_id, uint8_t chip_id, uint8_t *result);

bool dragonmint_check_voltage(struct T1_chain *t1, int chip_id, dragonmint_reg_ctrl_t *s_reg_ctrl);

bool check_chip(struct T1_chain *t1, int i);
bool abort_work(struct T1_chain *t1);

int get_current_ms(void);
bool is_chip_disabled(struct T1_chain *t1, uint8_t chip_id);
void disable_chip(struct T1_chain *t1, uint8_t chip_id);

bool get_nonce(struct T1_chain *t1, uint8_t *nonce, uint8_t *chip_id, uint8_t *job_id, uint8_t *micro_job_id);
bool set_work(struct T1_chain *t1, uint8_t chip_id, struct work *work, uint8_t queue_states);
uint8_t *create_job(uint8_t chip_id, uint8_t job_id, struct work *work);
void test_bench_pll_config(struct T1_chain *t1,uint32_t uiPll);

hardware_version_e dragonmint_get_hwver(void);
//dragonmint_type_e dragonmint_get_miner_type(void);
uint32_t dragonmint_get_chipnum(void);

void chain_all_exit(void);
void power_down_all_chain(void);
void write_miner_ageing_status(uint32_t statusCode);
int dragonmint_get_voltage_stats(struct T1_chain *t1, dragonmint_reg_ctrl_t *s_reg_ctrl);

bool t1_set_pll(struct T1_chain *t1, int chip_id, int target_pll);

bool T1_SetT1PLLClock(struct T1_chain *t1,int pllClkIdx, int chip_id);
int T1_ConfigT1PLLClock(uint32_t optPll);

extern const struct PLL_Clock PLL_Clk_12Mhz[T1_PLL_LV_NUM];
extern const uint8_t default_reg[T1_PLL_LV_NUM][REG_LENGTH];

#endif

