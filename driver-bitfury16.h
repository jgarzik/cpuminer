#ifndef BITFURY16_H
#define BITFURY16_H

#include "miner.h"
#include "bf16-bitfury16.h"

/* file contains device build revision: one of following */
/* #define	MINER_X5                                     */
/* #define	MINER_X6                                     */
/* #include "bf16-devicerev.h"                           */
#define  MINER_X6

#if defined(MINER_X5) && defined(MINER_X6)
#error "MINER_X5 and MINER_X6 can not be defined both"
#endif

#if !defined(MINER_X5) && !defined(MINER_X6)
#error "At least one of MINER_X5 and MINER_X6 should be defined"
#endif

#define CHIPBOARD_NUM   2   /* chipboard number */
#define BF16_NUM        11  /* chips number per BTC250 concentrator */

#ifdef MINER_X5
#define BCM250_NUM      3   /* BCM250 chips per chipboard */
#define CHANNEL_DEPTH   2   /* maximum channel length */
#define CHIPS_NUM       26  /* BF16 chips per chipboard */
#endif

#ifdef MINER_X6
#define BCM250_NUM      6   /* BCM250 chips per chipboard */
#define CHANNEL_DEPTH   4   /* maximum channel length */
#define CHIPS_NUM       52  /* BF16 chips per chipboard */
#endif

#define FILELOG

enum bf_opt_renonce {
	RENONCE_DISABLED,
	RENONCE_ONE_CHIP,
	RENONCE_CHIP_PER_BOARD
};

/* chip structure */
typedef struct {
	uint8_t             clock;
	bf_chip_status_t    status;
	uint8_t             curr_buff;
	uint8_t             task_processed;

	/* nonces list to extract dups */
	bf_list_t*          nonce_list;

	/* chip statistics */
	float               nonces;
	float               task_switch;
	float               status_cmd;
	float               status_cmd_none;

	uint32_t            nonces_dx;
	uint32_t            nonces_good_dx;
	uint32_t            nonces_diff_dx;
	uint32_t            nonces_bad_dx;
	uint32_t            nonces_re_dx;
	uint32_t            nonces_re_good_dx;
	uint32_t            nonces_re_bad_dx;

	uint32_t            task_switch_dx;
	uint32_t            status_cmd_dx;
	uint32_t            status_cmd_none_dx;

	float               hashrate;
	float               hashrate_good;
	float               hashrate_diff;
	float               hashrate_bad;
	float               hashrate_re;
	float               hashrate_re_good;
	float               hashrate_re_bad;

	uint32_t            errors;         /* error counter */
	uint32_t            error_rate;     /* error rate */
	time_t              last_error_time;/* time since last error */
	uint16_t            recovery_count;

	time_t              last_nonce_time;/* time since last good nonce */
	struct timeval      status_time;    /* time since last status cmd */
	struct timeval      switch_time;    /* time since last task switch */

	/* command stuff */
	bf_works_t          owork;          /* old work */
	bf_works_t          cwork;          /* current work */
	uint32_t            rx[12];
	uint32_t            rx_prev[12];

} bf_chip_t;

/* chip concentrator init map structure */
typedef struct {
	uint8_t             channel_path[CHANNEL_DEPTH];
	uint8_t             first_good_chip;
	uint8_t             last_good_chip;
	uint8_t             chips_num;
} bf_bcm250_map_t;

/* chip concentrator structure */
typedef struct {
	uint8_t*            channel_path;  /* channel path to chip concentrator */
	uint8_t             channel_depth;
	uint8_t             first_good_chip;
	uint8_t             last_good_chip;
	uint8_t             chips_num;
	uint8_t             chips_failed;
	bf_chip_t           chips[BF16_NUM];

	/* chip statistics */
	float               nonces;
	float               task_switch;
	float               status_cmd;
	float               status_cmd_none;

	uint32_t            nonces_dx;
	uint32_t            nonces_good_dx;
	uint32_t            nonces_diff_dx;
	uint32_t            nonces_bad_dx;
	uint32_t            nonces_re_dx;
	uint32_t            nonces_re_good_dx;
	uint32_t            nonces_re_bad_dx;

	uint32_t            task_switch_dx;
	uint32_t            status_cmd_dx;
	uint32_t            status_cmd_none_dx;

	float               hashrate;
	float               hashrate_good;
	float               hashrate_diff;
	float               hashrate_bad;
	float               hashrate_re;
	float               hashrate_re_good;
	float               hashrate_re_bad;
} bf_bcm250_t;

/* pid structure */
typedef struct {
	int16_t     i_state;        /* integrator state                   */
	int16_t     i_max;          /* maximum allowable integrator state */
	int16_t     i_min;          /* minimum allowable integrator state */
} bf_pid_t;

typedef enum {
	CHIPBOARD_X5,
	CHIPBOARD_X6
} bf_chipboard_type_t;

typedef enum {
	CHIPBOARD_REV1,
	CHIPBOARD_REV2,
	CHIPBOARD_REV3
} bf_chipboard_rev_t;

/* chipboard structure */
typedef struct {
	bool                detected;
	bool                active;

	bf_pid_t            pid;
	bf_chipboard_type_t board_type;
	bf_chipboard_rev_t  board_rev;

	/* MSP version data */
	uint32_t            board_ver;
	uint32_t            board_fwver;
	char                board_hwid[32];

	/* MSP hw data */
	float               temp;
	float               u_board;
	float               p_board;
	uint8_t             p_chain1_enabled;
	uint8_t             p_chain2_enabled;
	float               u_chain1;
	float               u_chain2;
	float               i_chain1;
	float               i_chain2;
	float               p_chain1;
	float               p_chain2;
	float               p_fan;

	uint32_t            rpm;
	uint8_t             fan_speed;
	float               i_alarm;
	float               t_alarm;
	float               t_gisteresis;
	char                fan_mode;
	float               target_temp;

	uint8_t             a_temp;
	uint8_t             a_ichain1;
	uint8_t             a_ichain2;

	uint8_t             bcm250_num;
	uint8_t             chips_num;
	uint8_t             chips_failed;
	uint8_t             chips_disabled;
	bf_bcm250_t*        bcm250;

#ifdef MINER_X5
	bool                power_disabled;

	uint32_t            power_disable_count;

	time_t              power_disable_time;

	time_t              power_enable_time;
#endif
#ifdef MINER_X6
	bool                power1_disabled;
	bool                power2_disabled;

	uint32_t            power1_disable_count;
	uint32_t            power2_disable_count;

	time_t              power1_disable_time;
	time_t              power2_disable_time;

	time_t              power1_enable_time;
	time_t              power2_enable_time;
#endif

	bf_cmd_buffer_t     cmd_buffer;

	/* chip statistics */
	float               nonces;
	float               task_switch;
	float               status_cmd;
	float               status_cmd_none;

	uint32_t            nonces_dx;
	uint32_t            nonces_good_dx;
	uint32_t            nonces_diff_dx;
	uint32_t            nonces_bad_dx;
	uint32_t            nonces_re_dx;
	uint32_t            nonces_re_good_dx;
	uint32_t            nonces_re_bad_dx;

	uint32_t            task_switch_dx;
	uint32_t            status_cmd_dx;
	uint32_t            status_cmd_none_dx;

	float               hashrate;
	float               hashrate_good;
	float               hashrate_diff;
	float               hashrate_bad;
	float               hashrate_re;
	float               hashrate_re_good;
	float               hashrate_re_bad;

	float               txrx_speed;
	uint32_t            bytes_transmitted_dx;
	uint64_t            bytes_transmitted;
} bf_chipboard_t;

struct bitfury16_info {
	struct thr_info *thr;
	struct thr_info chipworker_thr;
	struct thr_info nonceworker_thr;
	struct thr_info renonceworker_thr;
	struct thr_info hwmonitor_thr;
	struct thr_info alarm_thr;
	struct thr_info statistics_thr;

#ifdef FILELOG
	FILE*           logfile;
	pthread_mutex_t logfile_mutex;
#endif

	uint8_t         chipboard_num;
	/* boards with all sensors in propriate state */
	uint8_t         active_chipboard_num;

	uint8_t         chips_num;
	uint8_t         chips_failed;
	uint8_t         chips_disabled;
	uint8_t         renonce_chips;
	bf_chipboard_t* chipboard;

	/* work list */
	bf_list_t*      work_list;

	/* work list */
	bf_list_t*      stale_work_list;

	/* nonces for calculation */
	bf_list_t*      noncework_list;

	/* renonces for calculation */
	bf_list_t*      renoncework_list;

	/* nonces for recalculation */
	uint32_t        renonce_id;
	bf_list_t*      renonce_list;

	uint32_t        stage0_match;
	uint32_t        stage0_mismatch;
	uint32_t        stage1_match;
	uint32_t        stage1_mismatch;
	uint32_t        stage2_match;
	uint32_t        stage2_mismatch;
	uint32_t        stage3_match;
	uint32_t        stage3_mismatch;
	uint32_t        unmatched;

	uint8_t         channel_length;

	/* driver statistics */
	float           nonces;
	float           task_switch;
	float           status_cmd;
	float           status_cmd_none;

	uint32_t        nonces_dx;
	uint32_t        nonces_good_dx;
	uint32_t        nonces_diff_dx;
	uint32_t        nonces_bad_dx;
	uint32_t        nonces_re_dx;
	uint32_t        nonces_re_good_dx;
	uint32_t        nonces_re_bad_dx;

	uint32_t        task_switch_dx;
	uint32_t        status_cmd_dx;
	uint32_t        status_cmd_none_dx;

	float           hashrate;
	float           hashrate_good;
	float           hashrate_diff;
	float           hashrate_bad;
	float           hashrate_re;
	float           hashrate_re_good;
	float           hashrate_re_bad;

	pthread_mutex_t nonces_good_lock;
	uint32_t        nonces_good_cg;

	float           u_avg;
	float           i_total;
	float           p_total;
	float           u_chip;
	float           p_chip;

	bool            a_temp;
	bool            a_ichain;
	bool            a_net;
	time_t          ialarm_start;
	uint16_t        ialarm_count;
	bool            ialarm_buzzer;

	bool            led_red_enabled;
	struct timeval  led_red_switch;
	bool            led_green_enabled;
	struct timeval  led_green_switch;
	bool            buzzer_enabled;
	struct timeval  buzzer_switch;

	bool            initialised;
};

/* set clock to all chips and exit */
extern bool opt_bf16_set_clock;

/* enable board mining statistics output */
extern bool opt_bf16_stats_enabled;

/* chip clock value */
extern char* opt_bf16_clock;
extern uint8_t bf16_chip_clock;

/* renonce chip clock value */
extern char* opt_bf16_renonce_clock;
extern uint8_t bf16_renonce_chip_clock;

/* renonce configuration */
extern int opt_bf16_renonce;

/* manual PID enabled */
#ifdef MINER_X5
extern bool opt_bf16_manual_pid_enabled;
#endif

#ifdef MINER_X6
extern bool opt_bf16_manual_pid_disabled;
#endif

/* disable automatic power management */
extern bool opt_bf16_power_management_disabled;

/* fan speed */
extern int opt_bf16_fan_speed;

/* target temp */
extern int opt_bf16_target_temp;

/* alarm temp */
extern int opt_bf16_alarm_temp;

/* test chip communication */
extern char* opt_bf16_test_chip;

#endif /* BITFURY16_H */
