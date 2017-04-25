#include "config.h"

#include "math.h"
#include "miner.h"

#include "bf16-communication.h"
#include "bf16-ctrldevice.h"
#include "bf16-mspcontrol.h"
#include "bf16-spidevice.h"
#include "bf16-uartdevice.h"
#include "driver-bitfury16.h"

#ifdef FILELOG
#define LOGFILE                 "/var/log/cgminer.log"
#endif

#define DISABLE_SEND_CMD_ERROR

#define POWER_WAIT_INTERVAL     100000
#define RENONCE_SEND            5
#define RENONCE_STAGE2_LIMIT    80
#define RENONCE_STAGE3_LIMIT    60
#define RENONCE_QUEUE_LEN       100

#define RENONCE_COUNT           29

/* chip nonce queue len */
#define NONCE_CHIP_QUEUE_LEN    7
#define RENONCE_CHIP_QUEUE_LEN  40

#define WORK_TIMEOUT            2.0

/* statistics intervals */
#define AVG_TIME_DELTA          5.0
#define AVG_TIME_INTERVAL       5.0

/* power chain alarm interval */
#define ICHAIN_ALARM_INTERVAL   60
#define U_LOSS                  0.2

/* power chain reenable timeout */
#define CHAIN_REENABLE_INTERVAL 10
#define CHAIN_WORK_INTERVAL     1200

/* disable chip if no good nonces received during CHIP_FAILING_INTERVAL */
#define RENONCE_CHIP_FAILING_INTERVAL   15.0
#define CHIP_FAILING_INTERVAL           30.0
#define CHIP_RECOVERY_INTERVAL          5.0

/* chip is considered to be failed after CHIP_ERROR_FAIL_LIMIT recovery attempts */
#define CHIP_ERROR_FAIL_LIMIT           10
/* this value should be less than CHIP_ERROR_FAIL_LIMIT */
#define RENONCE_CHIP_ERROR_FAIL_LIMIT   8
#define CHIP_ERROR_LIMIT                5

#define CHIP_TASK_STATUS_INTERVAL       7000
#define CHIP_TASK_SWITCH_INTERVAL       5000000

#define CHIP_RESTART_LIMIT              120

/* alarm intervals */
#define LED_GREEN_INTERVAL      1000000
#define LED_RED_INTERVAL        1000000
#define LED_RED_NET_INTERVAL    500000
#define BUZZER_INTERVAL         1000000

/* threads delay */
#define CHIPWORKER_DELAY        1000000
#define NONCEWORKER_DELAY       30000
#define RENONCEWORKER_DELAY     30000
#define HWMONITOR_DELAY         1000000
#define STATISTICS_DELAY        400000
#define ALARM_DELAY             500000

/* hold 3 works for each chip */
#define WORK_QUEUE_LEN          3 * CHIPS_NUM

/* set clock to all chips and exit */
bool opt_bf16_set_clock                 = false;

/* enable board mining statistics output */
bool opt_bf16_stats_enabled             = false;

/* disable automatic power management */
bool opt_bf16_power_management_disabled = false;

/* renonce configuration */
int opt_bf16_renonce                    = RENONCE_ONE_CHIP;

/* chip clock value */
char* opt_bf16_clock    = NULL;
uint8_t bf16_chip_clock = 0x32;

/* renonce chip clock value */
char* opt_bf16_renonce_clock    = NULL;
uint8_t bf16_renonce_chip_clock = 0x2d;

/* fan speed */
int opt_bf16_fan_speed = -1;

/* target temp */
int opt_bf16_target_temp = -1;

/* alarm temp */
int opt_bf16_alarm_temp = -1;

/* test chip communication */
char* opt_bf16_test_chip = NULL;

/* number of bits to fixate */
static uint32_t mask_bits = 10;

/* default chip mask */
static uint32_t mask = 0x00000000;

/* initial pid state */
static bf_pid_t pid = {
	.i_state = 0,
	.i_max   = 300,
	.i_min   = -10,
};

#ifdef MINER_X5
bool opt_bf16_manual_pid_enabled = false;
bool manual_pid_enabled          = false;

static bf_bcm250_map_t bcm250_map[CHIPBOARD_NUM][BCM250_NUM] = {
	{
		{
			.channel_path    = { BF250_LOCAL, BF250_NONE },
			.first_good_chip = 0,
			.last_good_chip  = BF16_NUM,
			.chips_num       = BF16_NUM
		},
		{
			.channel_path    = { BF250_CHAN1, BF250_LOCAL },
			.first_good_chip = 7,
			.last_good_chip  = BF16_NUM,
			.chips_num       = 4
		},
		{
			.channel_path    = { BF250_CHAN2, BF250_LOCAL },
			.first_good_chip = 0,
			.last_good_chip  = BF16_NUM,
			.chips_num       = BF16_NUM
		}
	},
	{
		{
			.channel_path    = { BF250_LOCAL, BF250_NONE },
			.first_good_chip = 0,
			.last_good_chip  = BF16_NUM,
			.chips_num       = BF16_NUM
		},
		{
			.channel_path    = { BF250_CHAN1, BF250_LOCAL },
			.first_good_chip = 7,
			.last_good_chip  = BF16_NUM,
			.chips_num       = 4
		},
		{
			.channel_path    = { BF250_CHAN2, BF250_LOCAL },
			.first_good_chip = 0,
			.last_good_chip  = BF16_NUM,
			.chips_num       = BF16_NUM
		}
	}
};
#endif

#ifdef MINER_X6
bool opt_bf16_manual_pid_disabled = false;
bool manual_pid_enabled           = false;

static bf_bcm250_map_t bcm250_map[CHIPBOARD_NUM][BCM250_NUM] = {
	{
		{
			.channel_path    = { BF250_LOCAL, BF250_NONE, BF250_NONE, BF250_NONE },
			.first_good_chip = 0,
			.last_good_chip  = BF16_NUM,
			.chips_num       = BF16_NUM
		},
		{
			.channel_path    = { BF250_CHAN1, BF250_LOCAL, BF250_NONE, BF250_NONE },
			.first_good_chip = 0,
			.last_good_chip  = BF16_NUM,
			.chips_num       = BF16_NUM
		},
		{
			.channel_path    = { BF250_CHAN2, BF250_LOCAL, BF250_NONE, BF250_NONE },
			.first_good_chip = 0,
			.last_good_chip  = 4,
			.chips_num       = 4
		},
		{
			.channel_path    = { BF250_CHAN2, BF250_CHAN2, BF250_LOCAL, BF250_NONE },
			.first_good_chip = 0,
			.last_good_chip  = BF16_NUM,
			.chips_num       = BF16_NUM
		},
		{
			.channel_path    = { BF250_CHAN2, BF250_CHAN2, BF250_CHAN1, BF250_LOCAL },
			.first_good_chip = 0,
			.last_good_chip  = BF16_NUM,
			.chips_num       = BF16_NUM
		},
		{
			.channel_path    = { BF250_CHAN2, BF250_CHAN2, BF250_CHAN2, BF250_LOCAL },
			.first_good_chip = 0,
			.last_good_chip  = 4,
			.chips_num       = 4
		}
	},
	{
		{
			.channel_path    = { BF250_LOCAL, BF250_NONE, BF250_NONE, BF250_NONE },
			.first_good_chip = 0,
			.last_good_chip  = BF16_NUM,
			.chips_num       = BF16_NUM
		},
		{
			.channel_path    = { BF250_CHAN1, BF250_LOCAL, BF250_NONE, BF250_NONE },
			.first_good_chip = 0,
			.last_good_chip  = BF16_NUM,
			.chips_num       = BF16_NUM
		},
		{
			.channel_path    = { BF250_CHAN2, BF250_LOCAL, BF250_NONE, BF250_NONE },
			.first_good_chip = 0,
			.last_good_chip  = 4,
			.chips_num       = 4
		},
		{
			.channel_path    = { BF250_CHAN2, BF250_CHAN2, BF250_LOCAL, BF250_NONE },
			.first_good_chip = 0,
			.last_good_chip  = BF16_NUM,
			.chips_num       = BF16_NUM
		},
		{
			.channel_path    = { BF250_CHAN2, BF250_CHAN2, BF250_CHAN1, BF250_LOCAL },
			.first_good_chip = 0,
			.last_good_chip  = BF16_NUM,
			.chips_num       = BF16_NUM
		},
		{
			.channel_path    = { BF250_CHAN2, BF250_CHAN2, BF250_CHAN2, BF250_LOCAL },
			.first_good_chip = 0,
			.last_good_chip  = 4,
			.chips_num       = 4
		}
	}
};
#endif

/* each array element contains chip address assosiated to corresponting chipboard *
 * e.g. first array element - renonce chip address for the first chipboard and so *
 * on */
static bf_chip_address_t renonce_chip_address[CHIPBOARD_NUM] = {
	{
		.board_id  = 0,
		.bcm250_id = 0,
		.chip_id   = 0
	},
	{
		.board_id  = 1,
		.bcm250_id = 0,
		.chip_id   = 0
	}
};

#ifdef FILELOG
static int filelog(struct bitfury16_info *info, const char* format, ...)
{
	char fmt[1024];
	char datetime[64];
	struct timeval tv = {0, 0};
	struct tm *tm;

	if (info->logfile == NULL)
		return -1;

	gettimeofday(&tv, NULL);

	const time_t tmp_time = tv.tv_sec;
	int ms = (int)(tv.tv_usec / 1000);
	tm = localtime(&tmp_time);

	snprintf(datetime, sizeof(datetime), " [%d-%02d-%02d %02d:%02d:%02d.%03d] ",
		tm->tm_year + 1900,
		tm->tm_mon + 1,
		tm->tm_mday,
		tm->tm_hour,
		tm->tm_min,
		tm->tm_sec, ms);

	memset(fmt, 0, sizeof(fmt));
	sprintf(fmt, "%s%s\n", datetime, format);

	va_list args;
	va_start(args, format);

	mutex_lock(&info->logfile_mutex);
	vfprintf(info->logfile, fmt, args);
	fflush(info->logfile);
	mutex_unlock(&info->logfile_mutex);

	va_end(args);

	return 0;
}
#endif

static double timediff(struct timeval time1, struct timeval time2)
{
	double time1_val = 1000000 * time1.tv_sec + time1.tv_usec;
	double time2_val = 1000000 * time2.tv_sec + time2.tv_usec;

	return (double)(time2_val - time1_val) / 1000000.0;
}

static uint32_t timediff_us(struct timeval time1, struct timeval time2)
{
	uint32_t time1_val = 1000000 * time1.tv_sec + time1.tv_usec;
	uint32_t time2_val = 1000000 * time2.tv_sec + time2.tv_usec;

	return (time2_val - time1_val);
}

static void get_average(float* average, float delta, float time_diff, float interval)
{
	float ftotal, fprop;

	fprop = 1.0 - 1 / (exp((float)time_diff/(float)interval));
	ftotal = 1.0 + fprop;
	*average += (delta / time_diff * fprop);
	*average /= ftotal;
}

static uint8_t renonce_chip(bf_chip_address_t chip_address)
{
	uint8_t board_id = chip_address.board_id;

	if ((renonce_chip_address[board_id].bcm250_id == chip_address.bcm250_id) &&
	    (renonce_chip_address[board_id].chip_id   == chip_address.chip_id))
		return 1;

	return 0;
}

static void get_next_chip_address(struct bitfury16_info *info, bf_chip_address_t* chip_address)
{
	uint8_t board_id  = chip_address->board_id;
	uint8_t bcm250_id = chip_address->bcm250_id;
	uint8_t chip_id   = chip_address->chip_id;

	uint8_t last_good_chip = info->chipboard[board_id].bcm250[bcm250_id].last_good_chip - 1;
	if (last_good_chip == chip_id) {
#ifdef MINER_X5
		bcm250_id = (bcm250_id + 1) % BCM250_NUM;
		chip_id = info->chipboard[board_id].bcm250[bcm250_id].first_good_chip;
#endif

#ifdef MINER_X6
		if (opt_bf16_power_management_disabled == false) {
			bcm250_id = (bcm250_id + 1) % BCM250_NUM;

			/* do not set bcm250_id to disabled chain */
			/* second power chain disabled */
			if ((info->chipboard[board_id].p_chain2_enabled == 0) &&
				(info->chipboard[board_id].power2_disabled == true) &&
				(bcm250_id >= BCM250_NUM / 2) && (bcm250_id < BCM250_NUM)) {
				bcm250_id = 0;
			}

			chip_id = info->chipboard[board_id].bcm250[bcm250_id].first_good_chip;
		} else {
			chip_id = info->chipboard[board_id].bcm250[bcm250_id].first_good_chip;
		}
#endif
	} else
		chip_id++;

	chip_address->bcm250_id = bcm250_id;
	chip_address->chip_id   = chip_id;
}

static int8_t change_renonce_chip_address(struct cgpu_info *bitfury, bf_chip_address_t chip_address)
{
	struct bitfury16_info *info = (struct bitfury16_info *)(bitfury->device_data);

	uint8_t board_id  = chip_address.board_id;
	uint8_t bcm250_id = chip_address.bcm250_id;
	uint8_t chip_id   = chip_address.chip_id;

	bf_chip_address_t new_chip_address = { board_id, bcm250_id, chip_id };

	bool found = false;
	uint8_t chip_count = 0;
	while (true) {
		get_next_chip_address(info, &new_chip_address);
		chip_count++;

		uint8_t new_board_id  = new_chip_address.board_id;
		uint8_t new_bcm250_id = new_chip_address.bcm250_id;
		uint8_t new_chip_id   = new_chip_address.chip_id;

		/* enabled chip found */
		if (info->chipboard[new_board_id].bcm250[new_bcm250_id].chips[new_chip_id].status != DISABLED) {
			found = true;
			break;
		}

		/* we have run full loop chipboard */
#ifdef MINER_X5
		if (chip_count == info->chipboard[board_id].chips_num)
			break;
#endif

#ifdef MINER_X6
		if (opt_bf16_power_management_disabled == false) {
			if ((info->chipboard[board_id].p_chain2_enabled == 0) &&
				(info->chipboard[board_id].power2_disabled == true)) {
				if (chip_count == info->chipboard[board_id].chips_num / 2)
					break;
			} else {
				if (chip_count == info->chipboard[board_id].chips_num)
					break;
			}
		} else {
			if (chip_count == info->chipboard[board_id].chips_num)
				break;
		}
#endif
	}

	if ((found == true) &&
		(memcmp(&new_chip_address, &renonce_chip_address[board_id], sizeof(bf_chip_address_t)) != 0)) {
		board_id  = new_chip_address.board_id;
		bcm250_id = new_chip_address.bcm250_id;
		chip_id   = new_chip_address.chip_id;

		renonce_chip_address[board_id].bcm250_id = bcm250_id;
		renonce_chip_address[board_id].chip_id   = chip_id;

		info->chipboard[board_id].bcm250[bcm250_id].chips[chip_id].status          = UNINITIALIZED;
		info->chipboard[board_id].bcm250[bcm250_id].chips[chip_id].last_nonce_time = time(NULL);
		gettimeofday(&info->chipboard[board_id].bcm250[bcm250_id].chips[chip_id].status_time, NULL);

		info->chipboard[board_id].bcm250[bcm250_id].chips[chip_id].recovery_count = 0;

		applog(LOG_NOTICE, "%s: changed renonce chip address to: [%d:%d:%2d]",
				bitfury->drv->name,
				board_id, bcm250_id, chip_id);

#ifdef FILELOG
		filelog(info, "%s: changed renonce chip address to: [%d:%d:%2d]",
				bitfury->drv->name,
				board_id, bcm250_id, chip_id);
#endif

		return 0;
	} else {
		info->chipboard[board_id].bcm250[bcm250_id].chips[chip_id].status = DISABLED;

		applog(LOG_NOTICE, "%s: failed to find working renonce chip. disabling...",
				bitfury->drv->name);

#ifdef FILELOG
		filelog(info, "%s: failed to find working renonce chip. disabling...",
				bitfury->drv->name);
#endif

		return -1;
	}
}

static void increase_good_nonces(struct bitfury16_info *info, bf_chip_address_t chip_address)
{
	uint8_t board_id  = chip_address.board_id;
	uint8_t bcm250_id = chip_address.bcm250_id;
	uint8_t chip_id   = chip_address.chip_id;

	info->chipboard[board_id].bcm250[bcm250_id].chips[chip_id].nonces_good_dx++;
	info->chipboard[board_id].bcm250[bcm250_id].nonces_good_dx++;
	info->chipboard[board_id].nonces_good_dx++;
	info->nonces_good_dx++;
}

static void increase_bad_nonces(struct bitfury16_info *info, bf_chip_address_t chip_address)
{
	uint8_t board_id  = chip_address.board_id;
	uint8_t bcm250_id = chip_address.bcm250_id;
	uint8_t chip_id   = chip_address.chip_id;

	info->chipboard[board_id].bcm250[bcm250_id].chips[chip_id].nonces_bad_dx++;
	info->chipboard[board_id].bcm250[bcm250_id].nonces_bad_dx++;
	info->chipboard[board_id].nonces_bad_dx++;
	info->nonces_bad_dx++;
}

static void increase_re_nonces(struct bitfury16_info *info, bf_chip_address_t chip_address)
{
	uint8_t board_id  = chip_address.board_id;
	uint8_t bcm250_id = chip_address.bcm250_id;
	uint8_t chip_id   = chip_address.chip_id;

	info->chipboard[board_id].bcm250[bcm250_id].chips[chip_id].nonces_re_dx++;
	info->chipboard[board_id].bcm250[bcm250_id].nonces_re_dx++;
	info->chipboard[board_id].nonces_re_dx++;
	info->nonces_re_dx++;
}

static void increase_re_good_nonces(struct bitfury16_info *info, bf_chip_address_t chip_address)
{
	uint8_t board_id  = chip_address.board_id;
	uint8_t bcm250_id = chip_address.bcm250_id;
	uint8_t chip_id   = chip_address.chip_id;

	info->chipboard[board_id].bcm250[bcm250_id].chips[chip_id].nonces_re_good_dx++;

	if (renonce_chip(chip_address) == 0) {
		info->chipboard[board_id].bcm250[bcm250_id].nonces_re_good_dx++;
		info->chipboard[board_id].nonces_re_good_dx++;
		info->nonces_re_good_dx++;
	}
}

static void increase_re_bad_nonces(struct bitfury16_info *info, bf_chip_address_t chip_address)
{
	uint8_t board_id  = chip_address.board_id;
	uint8_t bcm250_id = chip_address.bcm250_id;
	uint8_t chip_id   = chip_address.chip_id;

	info->chipboard[board_id].bcm250[bcm250_id].chips[chip_id].nonces_re_bad_dx++;

	if (renonce_chip(chip_address) == 0) {
		info->chipboard[board_id].bcm250[bcm250_id].nonces_re_bad_dx++;
		info->chipboard[board_id].nonces_re_bad_dx++;
		info->nonces_re_bad_dx++;
	}
}

static void increase_total_nonces(struct bitfury16_info *info, bf_chip_address_t chip_address)
{
	uint8_t board_id  = chip_address.board_id;
	uint8_t bcm250_id = chip_address.bcm250_id;
	uint8_t chip_id   = chip_address.chip_id;

	info->chipboard[board_id].bcm250[bcm250_id].chips[chip_id].nonces_dx++;

	if ((renonce_chip(chip_address) == 0) ||
		(opt_bf16_renonce == RENONCE_DISABLED)) {
		info->chipboard[board_id].bcm250[bcm250_id].nonces_dx++;
		info->chipboard[board_id].nonces_dx++;
		info->nonces_dx++;
	}
}

static void increase_task_switch(struct bitfury16_info *info, bf_chip_address_t chip_address)
{
	uint8_t board_id  = chip_address.board_id;
	uint8_t bcm250_id = chip_address.bcm250_id;
	uint8_t chip_id   = chip_address.chip_id;

	info->chipboard[board_id].bcm250[bcm250_id].chips[chip_id].task_switch_dx++;
	info->chipboard[board_id].bcm250[bcm250_id].task_switch_dx++;
	info->chipboard[board_id].task_switch_dx++;
	info->task_switch_dx++;
}

static void increase_errors(struct bitfury16_info *info, bf_chip_address_t chip_address)
{
	uint8_t board_id  = chip_address.board_id;
	uint8_t bcm250_id = chip_address.bcm250_id;
	uint8_t chip_id   = chip_address.chip_id;

	/* update timing interval */
	time_t curr_time = time(NULL);
	if (curr_time - info->chipboard[board_id].bcm250[bcm250_id].chips[chip_id].last_error_time <= 1)
		info->chipboard[board_id].bcm250[bcm250_id].chips[chip_id].error_rate++;
	else
		info->chipboard[board_id].bcm250[bcm250_id].chips[chip_id].error_rate = 0;

	info->chipboard[board_id].bcm250[bcm250_id].chips[chip_id].last_error_time = curr_time;

	info->chipboard[board_id].bcm250[bcm250_id].chips[chip_id].errors++;

	if ((renonce_chip(chip_address) == 0) ||
	    (opt_bf16_renonce == RENONCE_DISABLED))
		info->chipboard[board_id].bcm250[bcm250_id].chips[chip_id].status = UNINITIALIZED;

	/* mark chip as FAILING if error rate too high*/
	if (info->chipboard[board_id].bcm250[bcm250_id].chips[chip_id].error_rate >= CHIP_ERROR_LIMIT) {
#ifdef FILELOG
		filelog(info, "BF16: chip [%d:%d:%2d] error rate too high: [%d], "
				"marked as failing, recovery_count: [%d]",
				board_id, bcm250_id, chip_id,
				info->chipboard[board_id].bcm250[bcm250_id].chips[chip_id].error_rate,
				info->chipboard[board_id].bcm250[bcm250_id].chips[chip_id].recovery_count);
#endif

		info->chipboard[board_id].bcm250[bcm250_id].chips[chip_id].status = FAILING;
		info->chipboard[board_id].bcm250[bcm250_id].chips[chip_id].error_rate = 0;
	}
}

static uint8_t* init_channel_path(uint8_t board_id, uint8_t btc250_num, uint8_t* channel_depth, uint8_t channel_length)
{
	uint8_t i, j;
	uint8_t* channel_path = cgcalloc(channel_length, sizeof(uint8_t));

	for (i = 0, j = 0; i < CHANNEL_DEPTH; i++) {
		if (bcm250_map[board_id][btc250_num].channel_path[i] != BF250_NONE)
			(*channel_depth)++;

		int8_t shift = 8*(j + 1) - 3*(i + 1);
		if (shift < 0) {
			channel_path[j]     |= bcm250_map[board_id][btc250_num].channel_path[i] >> abs(shift);
			channel_path[j + 1] |= bcm250_map[board_id][btc250_num].channel_path[i] << (8 - abs(shift));
			j++;
		} else
			channel_path[j] |= bcm250_map[board_id][btc250_num].channel_path[i] << shift;
	}

	return channel_path;
}

static int8_t parse_chip_address(struct cgpu_info *bitfury, char* address, bf_chip_address_t* chip_address)
{
	struct bitfury16_info *info = (struct bitfury16_info *)(bitfury->device_data);

	int8_t board_id  = 0;
	int8_t bcm250_id = 0;
	int8_t chip_id   = 0;

	char buff[16];

	if (address == NULL)
		return -1;

	/* board_id */
	char* start = strchr(address, '[');
	char* end   = strchr(address, ':');
	if ((start == NULL) || (end == NULL))
		return -1;
	uint8_t len = end - start;

	memset(buff, 0, sizeof(buff));
	cg_memcpy(buff, start + 1, len);
	board_id = atoi(buff);

	/* bcm250_id */
	start = end;
	end = strchr(start + 1, ':');
	if (end == NULL)
		return -1;
	len = end - start;

	memset(buff, 0, sizeof(buff));
	cg_memcpy(buff, start + 1, len);
	bcm250_id = atoi(buff);

	/* chip_id */
	start = end;
	end = strchr(start + 1, ']');
	if (end == NULL)
		return -1;
	len = end - start;

	memset(buff, 0, sizeof(buff));
	cg_memcpy(buff, start + 1, len);
	chip_id = atoi(buff);

	if ((board_id < 0) || (board_id >= CHIPBOARD_NUM)) {
		applog(LOG_ERR, "%s: invalid board_id %d: [0 - %d] specified",
				bitfury->drv->name,
				board_id, CHIPBOARD_NUM);
		return -1;
	}

	if ((bcm250_id < 0) || (bcm250_id >= BCM250_NUM)) {
		applog(LOG_ERR, "%s: invalid bcm250_id %d: [0 - %d] specified",
				bitfury->drv->name,
				bcm250_id, BCM250_NUM);
		return -1;
	}


	uint8_t first_good_chip = info->chipboard[board_id].bcm250[bcm250_id].first_good_chip;
	uint8_t last_good_chip  = info->chipboard[board_id].bcm250[bcm250_id].last_good_chip;

	if ((chip_id >= first_good_chip) && (chip_id < last_good_chip)) {
		chip_address->board_id  = board_id;
		chip_address->bcm250_id = bcm250_id;
		chip_address->chip_id   = chip_id;
	} else {
		applog(LOG_ERR, "%s: invalid chip_id %d: [%d - %d] specified",
				bitfury->drv->name,
				chip_id, first_good_chip, last_good_chip);
		return -1;
	}

	applog(LOG_NOTICE, "%s: parsed chip address: [%d:%d:%2d]",
			bitfury->drv->name,
			chip_address->board_id,
			chip_address->bcm250_id,
			chip_address->chip_id);

	return 0;
}

static void update_bcm250_map(struct cgpu_info *bitfury, uint8_t board_id)
{
	struct bitfury16_info *info = (struct bitfury16_info *)(bitfury->device_data);

#ifdef MINER_X5
	info->chipboard[board_id].board_type = CHIPBOARD_X5;

	switch (info->chipboard[board_id].board_ver) {
		/* 23 chip board version */
		case 5:
			info->chipboard[board_id].board_rev     = CHIPBOARD_REV2;

			bcm250_map[board_id][0].last_good_chip  = BF16_NUM - 2;
			bcm250_map[board_id][0].chips_num       = BF16_NUM - 2;

			bcm250_map[board_id][1].first_good_chip = 6;
			bcm250_map[board_id][1].chips_num       = 5;

			bcm250_map[board_id][2].last_good_chip  = BF16_NUM - 2;
			bcm250_map[board_id][2].chips_num       = BF16_NUM - 2;
			break;

		/* 24 chip board version */
		case 7:
			info->chipboard[board_id].board_rev     = CHIPBOARD_REV2;

			bcm250_map[board_id][0].last_good_chip  = BF16_NUM - 2;
			bcm250_map[board_id][0].chips_num       = BF16_NUM - 2;

			bcm250_map[board_id][1].first_good_chip = 6;
			bcm250_map[board_id][1].chips_num       = 5;

			bcm250_map[board_id][2].last_good_chip  = BF16_NUM - 1;
			bcm250_map[board_id][2].chips_num       = BF16_NUM - 1;
			break;

		/* 25 chip board version */
		case 9:
			info->chipboard[board_id].board_rev     = CHIPBOARD_REV2;

			bcm250_map[board_id][0].last_good_chip  = BF16_NUM - 1;
			bcm250_map[board_id][0].chips_num       = BF16_NUM - 1;

			bcm250_map[board_id][1].first_good_chip = 6;
			bcm250_map[board_id][1].chips_num       = 5;

			bcm250_map[board_id][2].last_good_chip  = BF16_NUM - 1;
			bcm250_map[board_id][2].chips_num       = BF16_NUM - 1;
			break;

		/* 26 chip board version */
		case 11:
			info->chipboard[board_id].board_rev     = CHIPBOARD_REV2;

			bcm250_map[board_id][0].last_good_chip  = BF16_NUM - 1;
			bcm250_map[board_id][0].chips_num       = BF16_NUM - 1;

			bcm250_map[board_id][1].first_good_chip = 6;
			bcm250_map[board_id][1].chips_num       = 5;
			break;

		/* 27 chip board version */
		case 13:
			info->chipboard[board_id].board_rev     = CHIPBOARD_REV2;

			bcm250_map[board_id][1].first_good_chip = 6;
			bcm250_map[board_id][1].chips_num       = 5;
			break;

		/* 26 chip board version - default */
		case 1:
		case 2:
			info->chipboard[board_id].board_rev    = CHIPBOARD_REV1;
		default:
			break;
	}
#endif

#ifdef MINER_X6
	info->chipboard[board_id].board_type = CHIPBOARD_X6;

	switch (info->chipboard[board_id].board_ver) {
		/* 46 chip board version */
		case 4:
			info->chipboard[board_id].board_rev     = CHIPBOARD_REV2;

			bcm250_map[board_id][0].last_good_chip  = BF16_NUM - 2;
			bcm250_map[board_id][0].chips_num       = BF16_NUM - 2;

			bcm250_map[board_id][1].first_good_chip = 1;
			bcm250_map[board_id][1].last_good_chip  = BF16_NUM - 1;
			bcm250_map[board_id][1].chips_num       = BF16_NUM - 2;

			bcm250_map[board_id][2].last_good_chip  = 5;
			bcm250_map[board_id][2].chips_num       = 5;

			bcm250_map[board_id][3].last_good_chip  = BF16_NUM - 2;
			bcm250_map[board_id][3].chips_num       = BF16_NUM - 2;

			bcm250_map[board_id][4].first_good_chip = 1;
			bcm250_map[board_id][4].last_good_chip  = BF16_NUM - 1;
			bcm250_map[board_id][4].chips_num       = BF16_NUM - 1;

			bcm250_map[board_id][5].last_good_chip  = 5;
			bcm250_map[board_id][5].chips_num       = 5;
			break;

		/* 48 chip board version */
		case 6:
			info->chipboard[board_id].board_rev     = CHIPBOARD_REV2;

			bcm250_map[board_id][0].last_good_chip  = BF16_NUM - 1;
			bcm250_map[board_id][0].chips_num       = BF16_NUM - 1;

			bcm250_map[board_id][1].first_good_chip = 1;
			bcm250_map[board_id][1].last_good_chip  = BF16_NUM - 1;
			bcm250_map[board_id][1].chips_num       = BF16_NUM - 2;

			bcm250_map[board_id][2].last_good_chip  = 5;
			bcm250_map[board_id][2].chips_num       = 5;

			bcm250_map[board_id][3].last_good_chip  = BF16_NUM - 1;
			bcm250_map[board_id][3].chips_num       = BF16_NUM - 1;

			bcm250_map[board_id][4].first_good_chip = 1;
			bcm250_map[board_id][4].last_good_chip  = BF16_NUM - 1;
			bcm250_map[board_id][4].chips_num       = BF16_NUM - 1;

			bcm250_map[board_id][5].last_good_chip  = 5;
			bcm250_map[board_id][5].chips_num       = 5;
			break;

		/* 50 chip board version */
		case 8:
			info->chipboard[board_id].board_rev     = CHIPBOARD_REV2;

			bcm250_map[board_id][0].last_good_chip  = BF16_NUM - 1;
			bcm250_map[board_id][0].chips_num       = BF16_NUM - 1;

			bcm250_map[board_id][1].last_good_chip  = BF16_NUM - 1;
			bcm250_map[board_id][1].chips_num       = BF16_NUM - 1;

			bcm250_map[board_id][2].last_good_chip  = 5;
			bcm250_map[board_id][2].chips_num       = 5;

			bcm250_map[board_id][3].last_good_chip  = BF16_NUM - 1;
			bcm250_map[board_id][3].chips_num       = BF16_NUM - 1;

			bcm250_map[board_id][4].last_good_chip  = BF16_NUM - 1;
			bcm250_map[board_id][4].chips_num       = BF16_NUM - 1;

			bcm250_map[board_id][5].last_good_chip  = 5;
			bcm250_map[board_id][5].chips_num       = 5;
			break;

		/* 52 chip board version */
		case 10:
			info->chipboard[board_id].board_rev     = CHIPBOARD_REV2;

			bcm250_map[board_id][1].last_good_chip  = BF16_NUM - 1;
			bcm250_map[board_id][1].chips_num       = BF16_NUM - 1;

			bcm250_map[board_id][2].last_good_chip  = 5;
			bcm250_map[board_id][2].chips_num       = 5;

			bcm250_map[board_id][4].last_good_chip  = BF16_NUM - 1;
			bcm250_map[board_id][4].chips_num       = BF16_NUM - 1;

			bcm250_map[board_id][5].last_good_chip  = 5;
			bcm250_map[board_id][5].chips_num       = 5;
			break;

		/* 54 chip board version */
		case 12:
			info->chipboard[board_id].board_rev     = CHIPBOARD_REV2;

			bcm250_map[board_id][2].last_good_chip  = 5;
			bcm250_map[board_id][2].chips_num       = 5;

			bcm250_map[board_id][5].last_good_chip  = 5;
			bcm250_map[board_id][5].chips_num       = 5;
			break;

		/* 46 chip board version */
		case 14:;
			info->chipboard[board_id].board_rev     = CHIPBOARD_REV3;

			uint8_t bcm250_channel_path[3][CHANNEL_DEPTH] = {
				{ BF250_CHAN1, BF250_CHAN1, BF250_LOCAL, BF250_NONE  },
				{ BF250_CHAN1, BF250_CHAN1, BF250_CHAN1, BF250_LOCAL },
				{ BF250_CHAN1, BF250_CHAN1, BF250_CHAN2, BF250_LOCAL },
			};

			bcm250_map[board_id][0].first_good_chip = 0;
			bcm250_map[board_id][0].last_good_chip  = 1;
			bcm250_map[board_id][0].chips_num       = 1;

			bcm250_map[board_id][2].first_good_chip = 0;
			bcm250_map[board_id][2].last_good_chip  = BF16_NUM;
			bcm250_map[board_id][2].chips_num       = BF16_NUM;

			bcm250_map[board_id][3].first_good_chip = 0;
			bcm250_map[board_id][3].last_good_chip  = 1;
			bcm250_map[board_id][3].chips_num       = 1;
			cg_memcpy(bcm250_map[board_id][3].channel_path, bcm250_channel_path[0], sizeof(bcm250_map[board_id][3].channel_path));

			cg_memcpy(bcm250_map[board_id][4].channel_path, bcm250_channel_path[1], sizeof(bcm250_map[board_id][4].channel_path));

			bcm250_map[board_id][5].first_good_chip = 0;
			bcm250_map[board_id][5].last_good_chip  = BF16_NUM;
			bcm250_map[board_id][5].chips_num       = BF16_NUM;
			cg_memcpy(bcm250_map[board_id][5].channel_path, bcm250_channel_path[2], sizeof(bcm250_map[board_id][5].channel_path));
			break;

		/* 52 chip board version - default */
		case 3:
			info->chipboard[board_id].board_rev     = CHIPBOARD_REV1;
		default:
			break;
	}
#endif
}

static void reinit_x5(struct bitfury16_info *info, bool chip_reinit)
{
	uint8_t board_id, bcm250_id, chip_id;

	/* reinit board chips */
	for (board_id = 0; board_id < CHIPBOARD_NUM; board_id++) {
		for (bcm250_id = 0; bcm250_id < BCM250_NUM; bcm250_id++) {
			uint8_t first_good_chip = info->chipboard[board_id].bcm250[bcm250_id].first_good_chip;
			uint8_t last_good_chip  = info->chipboard[board_id].bcm250[bcm250_id].last_good_chip;

			for (chip_id = first_good_chip; chip_id < last_good_chip; chip_id++) {
				if (chip_reinit == true) {
					info->chipboard[board_id].bcm250[bcm250_id].chips[chip_id].recovery_count  = 0;
					info->chipboard[board_id].bcm250[bcm250_id].chips[chip_id].error_rate      = 0;
					info->chipboard[board_id].bcm250[bcm250_id].chips[chip_id].status          = UNINITIALIZED;
					info->chipboard[board_id].bcm250[bcm250_id].chips[chip_id].last_nonce_time = time(NULL);
					info->chipboard[board_id].bcm250[bcm250_id].chips[chip_id].last_error_time = time(NULL);
					gettimeofday(&info->chipboard[board_id].bcm250[bcm250_id].chips[chip_id].status_time, NULL);
					gettimeofday(&info->chipboard[board_id].bcm250[bcm250_id].chips[chip_id].switch_time, NULL);
				} else {
					if (info->chipboard[board_id].bcm250[bcm250_id].chips[chip_id].recovery_count < CHIP_ERROR_FAIL_LIMIT)
						info->chipboard[board_id].bcm250[bcm250_id].chips[chip_id].status = UNINITIALIZED;
				}

				info->chipboard[board_id].bcm250[bcm250_id].chips[chip_id].last_nonce_time = time(NULL);
				gettimeofday(&info->chipboard[board_id].bcm250[bcm250_id].chips[chip_id].status_time, NULL);
				gettimeofday(&info->chipboard[board_id].bcm250[bcm250_id].chips[chip_id].switch_time, NULL);
			}
		}
	}

	/* send reset to all boards */
	spi_emit_reset(SPI_CHANNEL1);
	spi_emit_reset(SPI_CHANNEL2);
}

static void init_x5(struct cgpu_info *bitfury)
{
	uint8_t board_id, bcm250_id, chip_id;
	struct bitfury16_info *info = (struct bitfury16_info *)(bitfury->device_data);

	info->chipboard = cgcalloc(CHIPBOARD_NUM, sizeof(bf_chipboard_t));

	/* channel size in bytes */
	info->channel_length = (CHANNEL_DEPTH * 3) / 8 + 1;
	info->ialarm_count = 1;

	info->work_list       = workd_list_init();
	info->stale_work_list = workd_list_init();

	info->noncework_list  = noncework_list_init();

	if (opt_bf16_renonce != RENONCE_DISABLED) {
		info->renoncework_list  = renoncework_list_init();
		info->renonce_id        = 1;
		info->renonce_list      = renonce_list_init();
	}

	for (board_id = 0; board_id < CHIPBOARD_NUM; board_id++) {
		/* detect board */
		char buff[256];
		memset(buff, 0, sizeof(buff));

		device_ctrl_txrx(board_id + 1, 0, F_BDET, buff);
		parse_board_detect(bitfury, board_id, buff);

		if (info->chipboard[board_id].detected == true) {
			applog(LOG_NOTICE, "%s: BOARD%d detected", bitfury->drv->name, board_id + 1);

			info->chipboard[board_id].bcm250 = cgcalloc(BCM250_NUM, sizeof(bf_bcm250_t));
			cmd_buffer_init(&info->chipboard[board_id].cmd_buffer);

			get_board_info(bitfury, board_id);
			update_bcm250_map(bitfury, board_id);

			info->chipboard_num++;
			info->chipboard[board_id].bcm250_num = BCM250_NUM;

			cg_memcpy(&info->chipboard[board_id].pid, &pid, sizeof(bf_pid_t));

			uint8_t chips_num = 0;
			for (bcm250_id = 0; bcm250_id < BCM250_NUM; bcm250_id++) {
				info->chipboard[board_id].bcm250[bcm250_id].channel_path    = init_channel_path(board_id, bcm250_id,
						&info->chipboard[board_id].bcm250[bcm250_id].channel_depth, info->channel_length);
				info->chipboard[board_id].bcm250[bcm250_id].first_good_chip = bcm250_map[board_id][bcm250_id].first_good_chip;
				info->chipboard[board_id].bcm250[bcm250_id].last_good_chip  = bcm250_map[board_id][bcm250_id].last_good_chip;
				info->chipboard[board_id].bcm250[bcm250_id].chips_num       = bcm250_map[board_id][bcm250_id].chips_num;
				chips_num += bcm250_map[board_id][bcm250_id].chips_num;

				uint8_t first_good_chip = info->chipboard[board_id].bcm250[bcm250_id].first_good_chip;
				uint8_t last_good_chip  = info->chipboard[board_id].bcm250[bcm250_id].last_good_chip;

				for (chip_id = first_good_chip; chip_id < last_good_chip; chip_id++) {
					info->chipboard[board_id].bcm250[bcm250_id].chips[chip_id].status          = UNINITIALIZED;
					info->chipboard[board_id].bcm250[bcm250_id].chips[chip_id].last_nonce_time = time(NULL);
					info->chipboard[board_id].bcm250[bcm250_id].chips[chip_id].nonce_list      = nonce_list_init();
					gettimeofday(&info->chipboard[board_id].bcm250[bcm250_id].chips[chip_id].status_time, NULL);
					gettimeofday(&info->chipboard[board_id].bcm250[bcm250_id].chips[chip_id].switch_time, NULL);
				}
			}

			info->chipboard[board_id].chips_num  = chips_num;
			info->chips_num += info->chipboard[board_id].chips_num;
		} else
			applog(LOG_NOTICE, "%s: BOARD%d not found", bitfury->drv->name, board_id + 1);

		applog(LOG_INFO, "%s: initialized board X5.%d", bitfury->drv->name, board_id);
	}

	applog(LOG_INFO, "%s: initialized X5", bitfury->drv->name);
}

static void deinit_x5(struct cgpu_info *bitfury)
{
	uint8_t board_id, bcm250_id, chip_id;
	struct bitfury16_info *info = (struct bitfury16_info *)(bitfury->device_data);

	workd_list_deinit(info->work_list,       bitfury);
	workd_list_deinit(info->stale_work_list, bitfury);

	noncework_list_deinit(info->noncework_list);

	if (opt_bf16_renonce != RENONCE_DISABLED) {
		renoncework_list_deinit(info->renoncework_list);
		info->renonce_id = 1;
		renonce_list_deinit(info->renonce_list);
	}

	for (board_id = 0; board_id < CHIPBOARD_NUM; board_id++) {
		for (bcm250_id = 0; bcm250_id < BCM250_NUM; bcm250_id++) {
			free(info->chipboard[board_id].bcm250[bcm250_id].channel_path);

			uint8_t first_good_chip = info->chipboard[board_id].bcm250[bcm250_id].first_good_chip;
			uint8_t last_good_chip  = info->chipboard[board_id].bcm250[bcm250_id].last_good_chip;

			for (chip_id = first_good_chip; chip_id < last_good_chip; chip_id++)
				nonce_list_deinit(info->chipboard[board_id].bcm250[bcm250_id].chips[chip_id].nonce_list);
		}
		free(info->chipboard[board_id].bcm250);

		cmd_buffer_deinit(&info->chipboard[board_id].cmd_buffer);
	}

	free(info->chipboard);
}

static void bitfury16_set_clock(struct cgpu_info *bitfury) 
{
	uint8_t board_id, bcm250_id, chip_id;
	struct bitfury16_info *info = (struct bitfury16_info *)bitfury->device_data;

	/* send reset to all boards */
	spi_emit_reset(SPI_CHANNEL1);
	spi_emit_reset(SPI_CHANNEL2);

	/* board loop */
	for (board_id = 0; board_id < CHIPBOARD_NUM; board_id++) {
		applog(LOG_NOTICE, "%s: CHIPBOARD [%d]:", bitfury->drv->name, board_id);
		/* concentrator loop */
		for (bcm250_id = 0; bcm250_id < BCM250_NUM; bcm250_id++) {
			applog(LOG_NOTICE, "%s: BCM250 [%d]:", bitfury->drv->name, bcm250_id);

			spi_emit_reset(board_id + 1);

			/* build channel */
			create_channel(board_id + 1, info->chipboard[board_id].bcm250[bcm250_id].channel_path, info->channel_length);

			uint8_t channel_depth   = info->chipboard[board_id].bcm250[bcm250_id].channel_depth;
			uint8_t first_good_chip = info->chipboard[board_id].bcm250[bcm250_id].first_good_chip;
			uint8_t last_good_chip  = info->chipboard[board_id].bcm250[bcm250_id].last_good_chip;

			uint8_t result;
			bool fail;
			/* chips loop */
			for (chip_id = first_good_chip; chip_id < last_good_chip; chip_id++) {
				fail = false;
				bf_chip_address_t chip_address = { board_id, bcm250_id, chip_id };

				result = send_toggle(board_id + 1, channel_depth, chip_address);
				if (result != 0)
					fail = true;

				result = set_clock(board_id + 1, channel_depth, chip_address, bf16_chip_clock);
				if ((result != 0) && (fail != true))
					fail = true;

				if (fail == false)
					applog(LOG_NOTICE, "%s: CHIP [%2d]: OK", bitfury->drv->name, chip_id);
				else
					applog(LOG_NOTICE, "%s: CHIP [%2d]: FAIL", bitfury->drv->name, chip_id);
			}

			/* destroy channel */
			destroy_channel(board_id + 1, info->chipboard[board_id].bcm250[bcm250_id].channel_depth);
		}
	}

	deinit_x5(bitfury);
}

static void bitfury16_test_chip(struct cgpu_info *bitfury, bf_chip_address_t chip_address) 
{
	struct bitfury16_info *info = (struct bitfury16_info *)bitfury->device_data;

	uint8_t board_id  = chip_address.board_id;
	uint8_t bcm250_id = chip_address.bcm250_id;
	uint8_t chip_id   = chip_address.chip_id;

	/* send reset to board */
	spi_emit_reset(board_id + 1);

	/* build channel */
	create_channel(board_id + 1, info->chipboard[board_id].bcm250[bcm250_id].channel_path, info->channel_length);

	uint8_t channel_depth   = info->chipboard[board_id].bcm250[bcm250_id].channel_depth;

	uint8_t result;
	bool fail = false;

	result = send_toggle(board_id + 1, channel_depth, chip_address);
	if (result != 0)
		fail = true;

	result = set_clock(board_id + 1, channel_depth, chip_address, bf16_chip_clock);
	if ((result != 0) && (fail != true))
		fail = true;

	if (fail == false)
		applog(LOG_NOTICE, "%s: CHIP [%2d]: OK", bitfury->drv->name, chip_id);
	else
		applog(LOG_NOTICE, "%s: CHIP [%2d]: FAIL", bitfury->drv->name, chip_id);

	/* destroy channel */
	destroy_channel(board_id + 1, info->chipboard[board_id].bcm250[bcm250_id].channel_depth);

	deinit_x5(bitfury);
}

static void bitfury16_identify(__maybe_unused struct cgpu_info *bitfury)
{
}

static void set_fan_speed(struct cgpu_info *bitfury)
{
	struct bitfury16_info *info = (struct bitfury16_info *)bitfury->device_data;
	uint8_t board_id;

	for (board_id = 0; board_id < CHIPBOARD_NUM; board_id++) {
		if (info->chipboard[board_id].detected == true) {
			if (opt_bf16_fan_speed == -1) {
				if (device_uart_transfer(board_id + 1, "F") < 0)
					quit(1, "%s: %s() failed to set BOARD%d fan speed",
							bitfury->drv->name, __func__, board_id + 1);

				applog(LOG_INFO, "%s: set BOARD%d fan speed to auto mode",
						bitfury->drv->name, board_id + 1);
			} else {
				char uart_cmd[8];
				sprintf(uart_cmd, "F:%d", opt_bf16_fan_speed);

				if (device_uart_transfer(board_id + 1, uart_cmd) < 0)
					quit(1, "%s: %s() failed to set BOARD%d fan speed",
							bitfury->drv->name, __func__, board_id + 1);

				applog(LOG_INFO, "%s: set BOARD%d fan speed to [%d]",
						bitfury->drv->name, board_id + 1, opt_bf16_fan_speed);
			}
		}
	}
}

static void bitfury16_detect(bool hotplug)
{
	struct cgpu_info *bitfury = NULL;
	struct bitfury16_info *info = NULL;
	uint8_t board_id, bcm250_id, chip_id;

	if (hotplug)
		return;

	bitfury = cgmalloc(sizeof(struct cgpu_info));
	if (unlikely(!bitfury))
		quit(1, "%s: %s() failed to malloc bitfury",
				bitfury->drv->name, __func__);

	bitfury->drv = &bitfury16_drv;
	bitfury->deven = DEV_ENABLED;
	bitfury->threads = 1;

	info = cgmalloc(sizeof(struct bitfury16_info));
	if (unlikely(!info))
		quit(1, "%s: %s() failed to malloc info",
				bitfury->drv->name, __func__);

	bitfury->device_data = info;

	/* manual PID option */
#ifdef MINER_X5
	manual_pid_enabled = opt_bf16_manual_pid_enabled;
#endif

#ifdef MINER_X6
	manual_pid_enabled = !opt_bf16_manual_pid_disabled;
#endif

	/* renonce chip address and renonce chip clock */
	if (opt_bf16_renonce != RENONCE_DISABLED) {
		if (opt_bf16_renonce_clock != NULL)
			bf16_renonce_chip_clock = strtol(opt_bf16_renonce_clock, NULL, 16);
	} else
		/* default chip clock if renonce is disabled */
		bf16_chip_clock = 0x2d;

	/* general chip clock */
	if (opt_bf16_clock != NULL)
		bf16_chip_clock = strtol(opt_bf16_clock, NULL, 16);
	else if ((opt_bf16_set_clock == true) || (opt_bf16_test_chip != NULL))
		/* default chip clock if set_clock option is set */
		bf16_chip_clock = 0x20;

	/* open devices */
	if (open_spi_device(SPI_CHANNEL1) < 0)
		quit(1, "%s: %s() failed to open [%s] device",
				bitfury->drv->name, __func__, spi0_device_name);

	applog(LOG_INFO, "%s: opened [%s] device", bitfury->drv->name, spi0_device_name);

	if (open_spi_device(SPI_CHANNEL2) < 0)
		quit(1, "%s: %s() failed to open [%s] device",
				bitfury->drv->name, __func__, spi1_device_name);

	applog(LOG_INFO, "%s: opened [%s] device", bitfury->drv->name, spi1_device_name);

	if (open_ctrl_device() < 0)
		quit(1, "%s: %s() failed to open [%s] device",
				bitfury->drv->name, __func__, ctrl_device_name);

	applog(LOG_INFO, "%s: opened [%s] device", bitfury->drv->name, ctrl_device_name);

	if (open_uart_device(UART_CHANNEL1) < 0)
		quit(1, "%s: %s() failed to open [%s] device",
				bitfury->drv->name, __func__, uart1_device_name);

	applog(LOG_INFO, "%s: opened [%s] device", bitfury->drv->name, uart1_device_name);

	if (open_uart_device(UART_CHANNEL2) < 0)
		quit(1, "%s: %s() failed to open [%s] device",
				bitfury->drv->name, __func__, uart2_device_name);

	applog(LOG_INFO, "%s: opened [%s] device", bitfury->drv->name, uart2_device_name);

	init_x5(bitfury);

	/* send reset to boards */
	for (board_id = 0; board_id < CHIPBOARD_NUM; board_id++) {
		if (info->chipboard[board_id].detected == true) {
			device_ctrl_transfer(board_id + 1, 1, F_BRST);
			cgsleep_us(POWER_WAIT_INTERVAL);
			device_ctrl_transfer(board_id + 1, 0, F_BRST);
			cgsleep_us(POWER_WAIT_INTERVAL);

			applog(LOG_INFO, "%s: sent reset to BOARD%d",
					bitfury->drv->name, board_id + 1);
		}
	}

	/* check if board power is present */
	for (board_id = 0; board_id < CHIPBOARD_NUM; board_id++) {
		/* read hw sensor data */
		char buff[256];

		if (info->chipboard[board_id].detected == true) {
			memset(buff, 0, sizeof(buff));
			if (device_uart_txrx(board_id + 1, "S", buff) < 0)
				quit(1, "%s: %s() failed to get BOARD%d status",
					bitfury->drv->name, __func__, board_id + 1);

			if (parse_hwstats(info, board_id, buff) < 0)
				applog(LOG_ERR, "%s: failed to parse hw stats",
						bitfury->drv->name);

			/* disable board if power voltage is incorrect */
			if ((info->chipboard[board_id].u_board < 10.0) ||
			    (info->chipboard[board_id].u_board > 15.0)) {

				/* concentrator loop */
				for (bcm250_id = 0; bcm250_id < BCM250_NUM; bcm250_id++) {
					uint8_t first_good_chip = info->chipboard[board_id].bcm250[bcm250_id].first_good_chip;
					uint8_t last_good_chip  = info->chipboard[board_id].bcm250[bcm250_id].last_good_chip;

					/* chips loop */
					for (chip_id = first_good_chip; chip_id < last_good_chip; chip_id++)
						info->chipboard[board_id].bcm250[bcm250_id].chips[chip_id].status = DISABLED;
				}
	
				applog(LOG_ERR, "%s: incorrect U board detected [%.1f] on BOARD%d, "
						"disabling board...",
						bitfury->drv->name,
						info->chipboard[board_id].u_board,
						board_id + 1);
			} else {
				info->chipboard[board_id].active = true;
				info->active_chipboard_num++;
			}
		}
	}

	/* enable power chain */
	for (board_id = 0; board_id < CHIPBOARD_NUM; board_id++) {
		if (enable_power_chain(bitfury, board_id, 0) < 0)
			info->chipboard[board_id].detected = false;
		else {
#ifdef MINER_X5
			info->chipboard[board_id].power_enable_time   = time(NULL);
			info->chipboard[board_id].power_disable_count = 1;
#endif

#ifdef MINER_X6
			info->chipboard[board_id].power1_enable_time   = time(NULL);
			info->chipboard[board_id].power2_enable_time   = time(NULL);
			info->chipboard[board_id].power1_disable_count = 1;
			info->chipboard[board_id].power2_disable_count = 1;
#endif
		}
	}

	/* wait for power chain to enable */
	cgsleep_us(POWER_WAIT_INTERVAL);

	if (opt_bf16_set_clock == true) {
		applog(LOG_INFO, "%s: setting clock [%02x] to all chips",
				bitfury->drv->name, bf16_chip_clock);

		bitfury16_set_clock(bitfury);

		quit(0, "Done.");
	}

	if (opt_bf16_test_chip != NULL) {
		bf_chip_address_t chip_address = { 0, 0, 0 };

		if (parse_chip_address(bitfury, opt_bf16_test_chip, &chip_address) < 0) {
			quit(1, "%s: %s() error parsing chip address...",
					bitfury->drv->name, __func__);
		}

		applog(LOG_INFO, "%s: testing communicaton with chip [%d:%d:%2d]",
				bitfury->drv->name,
				chip_address.board_id,
				chip_address.bcm250_id,
				chip_address.chip_id);

		bitfury16_test_chip(bitfury, chip_address);

		quit(0, "Done.");
	}

	/* fan speed */
	if ((opt_bf16_fan_speed != -1) &&
	    (manual_pid_enabled == true)) {
		manual_pid_enabled = false;
	}

	set_fan_speed(bitfury);

	for (board_id = 0; board_id < CHIPBOARD_NUM; board_id++) {
		if (info->chipboard[board_id].detected == true) {
			/* target temp */
			if (opt_bf16_target_temp == -1) {
				if (device_uart_transfer(board_id + 1, "T") < 0)
					quit(1, "%s: %s() failed to set BOARD%d target temp",
							bitfury->drv->name, __func__, board_id + 1);

				applog(LOG_INFO, "%s: set BOARD%d target temp to default value",
						bitfury->drv->name, board_id + 1);
			} else {
				char uart_cmd[8];
				sprintf(uart_cmd, "T:%d", opt_bf16_target_temp);

				if (device_uart_transfer(board_id + 1, uart_cmd) < 0)
					quit(1, "%s: %s() failed to set BOARD%d target temp",
							bitfury->drv->name, __func__, board_id + 1);

				applog(LOG_INFO, "%s: set BOARD%d target temp to [%d]",
						bitfury->drv->name, board_id + 1, opt_bf16_target_temp);
			}

			/* alarm temp */
			if (opt_bf16_alarm_temp == -1) {
				if (device_uart_transfer(board_id + 1, "C") < 0)
					quit(1, "%s: %s() failed to set BOARD%d alarm temp",
							bitfury->drv->name, __func__, board_id + 1);

				applog(LOG_INFO, "%s: set BOARD%d alarm temp to default value",
						bitfury->drv->name, board_id + 1);
			} else {
				char uart_cmd[8];
				sprintf(uart_cmd, "C:%d", opt_bf16_alarm_temp);

				if (device_uart_transfer(board_id + 1, uart_cmd) < 0)
					quit(1, "%s: %s() failed to set BOARD%d alarm temp",
							bitfury->drv->name, __func__, board_id + 1);

				applog(LOG_INFO, "%s: set BOARD%d alarm temp to [%d]",
						bitfury->drv->name, board_id + 1, opt_bf16_alarm_temp);
			}
		}
	}

	/* count number of renonce chips */
	if (opt_bf16_renonce != RENONCE_DISABLED) {
		info->renonce_chips = opt_bf16_renonce;

		uint8_t renonce_chips = 0;
		for (board_id = 0; board_id < CHIPBOARD_NUM; board_id++) {
			if ((info->chipboard[board_id].detected == true) &&
			    (info->chipboard[board_id].active   == true)) {
				if (opt_bf16_renonce == RENONCE_ONE_CHIP) {
					if (renonce_chips != info->renonce_chips)
						renonce_chips++;
					else {
						renonce_chip_address[board_id].board_id  = -1;
						renonce_chip_address[board_id].bcm250_id = -1;
						renonce_chip_address[board_id].chip_id   = -1;
					}
				} else
					renonce_chips++;
			} else {
				renonce_chip_address[board_id].board_id  = -1;
				renonce_chip_address[board_id].bcm250_id = -1;
				renonce_chip_address[board_id].chip_id   = -1;
			}
		}

		if (renonce_chips != info->renonce_chips) {
			applog(LOG_ERR, "%s: expected to find [%d] renonce chips, but found only [%d]",
					bitfury->drv->name, opt_bf16_renonce, renonce_chips);

			info->renonce_chips = renonce_chips;
		}
	} else {
		for (board_id = 0; board_id < CHIPBOARD_NUM; board_id++) {
			renonce_chip_address[board_id].board_id  = -1;
			renonce_chip_address[board_id].bcm250_id = -1;
			renonce_chip_address[board_id].chip_id   = -1;
		}
	}

	/* correct renonce chip address */
	if (opt_bf16_renonce != RENONCE_DISABLED) {
		for (board_id = 0; board_id < CHIPBOARD_NUM; board_id++) {
			if ((info->chipboard[board_id].detected == true) &&
			    (info->chipboard[board_id].active   == true) &&
			    (renonce_chip_address[board_id].board_id != -1)) {
				bcm250_id = renonce_chip_address[board_id].bcm250_id;

				uint8_t first_good_chip = info->chipboard[board_id].bcm250[bcm250_id].first_good_chip;
				uint8_t last_good_chip  = info->chipboard[board_id].bcm250[bcm250_id].last_good_chip;

				if (renonce_chip_address[board_id].chip_id >= last_good_chip)
					renonce_chip_address[board_id].chip_id = last_good_chip - 1;
				else if (renonce_chip_address[board_id].chip_id < first_good_chip)
					renonce_chip_address[board_id].chip_id = first_good_chip;
			}
		}
	}

#ifdef FILELOG
	info->logfile = fopen(LOGFILE, "a");
	if (info->logfile == NULL)
		applog(LOG_ERR, "%s: failed to open logfile [%s]: %s",
				bitfury->drv->name, LOGFILE, strerror(errno));
	else
		mutex_init(&info->logfile_mutex);
#endif

	/* exit if no boards present */
	if ((info->chipboard_num == 0) ||
		(info->active_chipboard_num == 0)) {
		deinit_x5(bitfury);

		/* close devices */
		close_spi_device(SPI_CHANNEL1);
		close_spi_device(SPI_CHANNEL2);
		close_ctrl_device();
		close_uart_device(UART_CHANNEL1);
		close_uart_device(UART_CHANNEL2);

#ifdef FILELOG
		fclose(info->logfile);
#endif

		applog(LOG_ERR, "%s: no boards present. exiting...",
				bitfury->drv->name);

		free(info);
		free(bitfury);

		return;
	}

	mutex_init(&info->nonces_good_lock);

	if (!add_cgpu(bitfury))
		quit(1, "%s: %s() failed to add_cgpu",
				bitfury->drv->name, __func__);

	info->initialised = true;

	applog(LOG_INFO, "%s: chip driver initialized", bitfury->drv->name);
#ifdef FILELOG
	filelog(info, "%s: cgminer started", bitfury->drv->name);
#endif
}

static uint8_t chip_task_update(struct cgpu_info *bitfury, bf_chip_address_t chip_address)
{
	uint8_t i;
	int8_t ret = 0;
	bf_works_t work;
	time_t curr_time_t;
	struct timeval curr_time;

	uint8_t board_id  = chip_address.board_id;
	uint8_t bcm250_id = chip_address.bcm250_id;
	uint8_t chip_id   = chip_address.chip_id;

	struct bitfury16_info *info = (struct bitfury16_info *)(bitfury->device_data);
	bf_cmd_buffer_t* cmd_buffer = &info->chipboard[board_id].cmd_buffer;

	if (cmd_buffer->status != EMPTY)
		return -1;

	switch (info->chipboard[board_id].bcm250[bcm250_id].chips[chip_id].status) {

	/* fill chip buffer with toggle task */
	case UNINITIALIZED:
		applog(LOG_DEBUG, "%s: chipworker_thr: chip [%d:%d:%2d], prepare toggle_cmd",
				bitfury->drv->name,
				board_id, bcm250_id, chip_id);

		uint8_t toggle[4] = { 0xa5, 0x00, 0x00, 0x02 };

		ret = cmd_buffer_push(cmd_buffer,
				info->chipboard[board_id].bcm250[bcm250_id].channel_depth, chip_address, chip_address,
				work, 0, CHIP_CMD_TOGGLE, 3, toggle);

		if (ret < 0)
			applog(LOG_ERR, "%s: chipworker_thr: chip [%d:%d:%2d], error prepare toggle_cmd",
					bitfury->drv->name,
					board_id, bcm250_id, chip_id);
		break;

	/* fill chip buffer with set clock task */
	case TOGGLE_SET:
		applog(LOG_DEBUG, "%s: chipworker_thr: chip [%d:%d:%2d], prepare set_clock_cmd",
				bitfury->drv->name,
				board_id, bcm250_id, chip_id);

		uint8_t clock_buf[4];
		memset(clock_buf, 0, sizeof(clock_buf));

		/* init renonce chip with lower clock */
		if ((renonce_chip(chip_address) == 1) &&
			(opt_bf16_renonce != RENONCE_DISABLED)) {
			gen_clock_data(bf16_renonce_chip_clock, 1, clock_buf);
			info->chipboard[board_id].bcm250[bcm250_id].chips[chip_id].clock = bf16_renonce_chip_clock;
		} else {
			gen_clock_data(bf16_chip_clock, 1, clock_buf);
			info->chipboard[board_id].bcm250[bcm250_id].chips[chip_id].clock = bf16_chip_clock;
		}

		ret = cmd_buffer_push(cmd_buffer,
				info->chipboard[board_id].bcm250[bcm250_id].channel_depth, chip_address, chip_address,
				work, 0, CHIP_CMD_SET_CLOCK, 3, clock_buf);

		if (ret < 0)
			applog(LOG_ERR, "%s: chipworker_thr: chip [%d:%d:%2d], error prepare set_clock_cmd",
					bitfury->drv->name,
					board_id, bcm250_id, chip_id);
		break;

	/* fill chip buffer with chip mask */
	case CLOCK_SET:
		applog(LOG_DEBUG, "%s: chipworker_thr: chip [%d:%d:%2d], prepare set_mask_cmd",
				bitfury->drv->name,
				board_id, bcm250_id, chip_id);

		uint8_t noncemask[4];
		memset(noncemask, 0, sizeof(noncemask));

		for (i = 0; i < 4; i++)
			noncemask[i] = (mask >> (8*(4 - i - 1))) & 0xff;

		ret = cmd_buffer_push(cmd_buffer,
				info->chipboard[board_id].bcm250[bcm250_id].channel_depth, chip_address, chip_address,
				work, 0, CHIP_CMD_SET_MASK, 3, noncemask);

		if (ret < 0)
			applog(LOG_ERR, "%s: chipworker_thr: chip [%d:%d:%2d], error prepare set_mask_cmd",
					bitfury->drv->name,
					board_id, bcm250_id, chip_id);
		break;

	/* fill chip buffer with new task */
	case MASK_SET:
		if ((renonce_chip(chip_address) == 0) ||
			(opt_bf16_renonce == RENONCE_DISABLED)) {
			applog(LOG_DEBUG, "%s: chipworker_thr: chip [%d:%d:%2d], prepare send_task_cmd",
					bitfury->drv->name,
					board_id, bcm250_id, chip_id);

			L_LOCK(info->work_list);
			L_LOCK(info->stale_work_list);
			if (info->work_list->count > 0) {
				memset(info->chipboard[board_id].bcm250[bcm250_id].chips[chip_id].cwork.task, 0,
						sizeof(info->chipboard[board_id].bcm250[bcm250_id].chips[chip_id].cwork.task));

				bf_data_t* wdata = info->work_list->head;

				workd_list_push(info->stale_work_list, WORKD(wdata));
				workd_list_remove(info->work_list, &info->chipboard[board_id].bcm250[bcm250_id].chips[chip_id].cwork);

				gen_task_data(info->chipboard[board_id].bcm250[bcm250_id].chips[chip_id].cwork.payload.midstate,
						info->chipboard[board_id].bcm250[bcm250_id].chips[chip_id].cwork.payload.m7,
						info->chipboard[board_id].bcm250[bcm250_id].chips[chip_id].cwork.payload.ntime,
						info->chipboard[board_id].bcm250[bcm250_id].chips[chip_id].cwork.payload.nbits, mask,
						info->chipboard[board_id].bcm250[bcm250_id].chips[chip_id].cwork.task);

				ret = cmd_buffer_push(cmd_buffer,
						info->chipboard[board_id].bcm250[bcm250_id].channel_depth, chip_address, chip_address,
						info->chipboard[board_id].bcm250[bcm250_id].chips[chip_id].cwork, 0,
						CHIP_CMD_TASK_WRITE, 79,
						info->chipboard[board_id].bcm250[bcm250_id].chips[chip_id].cwork.task);

				if (ret < 0)
					applog(LOG_ERR, "%s: chipworker_thr: chip [%d:%d:%2d], error prepare send_task_cmd",
							bitfury->drv->name,
							board_id, bcm250_id, chip_id);
			}
#if 0
			else
				applog(LOG_ERR, "%s: chipworker_thr: chip [%d:%d:%2d], error prepare send_task_cmd: no works available",
						bitfury->drv->name,
						board_id, bcm250_id, chip_id);
#endif
			L_UNLOCK(info->stale_work_list);
			L_UNLOCK(info->work_list);
		}

		break;

	/* fill chip buffer with check status task */
	case TASK_SENT:
		gettimeofday(&curr_time, NULL);

		if (((renonce_chip(chip_address) == 0) ||
			(opt_bf16_renonce == RENONCE_DISABLED)) &&
			(timediff_us(info->chipboard[board_id].bcm250[bcm250_id].chips[chip_id].status_time, curr_time) > CHIP_TASK_STATUS_INTERVAL)) {
			applog(LOG_DEBUG, "%s: chipworker_thr: chip [%d:%d:%2d], prepare task_status_cmd",
					bitfury->drv->name,
					board_id, bcm250_id, chip_id);

			ret = cmd_buffer_push(cmd_buffer,
					info->chipboard[board_id].bcm250[bcm250_id].channel_depth, chip_address, chip_address,
					work, 0, CHIP_CMD_TASK_STATUS, 0, NULL);

			info->chipboard[board_id].bcm250[bcm250_id].chips[chip_id].status_time.tv_sec  = curr_time.tv_sec;
			info->chipboard[board_id].bcm250[bcm250_id].chips[chip_id].status_time.tv_usec = curr_time.tv_usec;

			if (ret < 0)
				applog(LOG_ERR, "%s: chipworker_thr: chip [%d:%d:%2d], error prepare task_status_cmd",
						bitfury->drv->name,
						board_id, bcm250_id, chip_id);
		}
		break;

	/* fill chip buffer with read nonces task */
	case TASK_SWITCHED:
		if ((renonce_chip(chip_address) == 0) ||
			(opt_bf16_renonce == RENONCE_DISABLED)) {
			applog(LOG_DEBUG, "%s: chipworker_thr: chip [%d:%d:%2d], prepare read_nonce_cmd",
					bitfury->drv->name,
					board_id, bcm250_id, chip_id);

			ret = cmd_buffer_push(cmd_buffer,
					info->chipboard[board_id].bcm250[bcm250_id].channel_depth, chip_address, chip_address,
					work, 0, CHIP_CMD_READ_NONCE, 0, NULL);

			if (ret < 0)
				applog(LOG_ERR, "%s: chipworker_thr: chip [%d:%d:%2d], error prepare read_nonce_cmd",
						bitfury->drv->name,
						board_id, bcm250_id, chip_id);
		}

		break;

	/* mark chip as UNINITIALIZED and start all over again */
	case FAILING:
		curr_time_t = time(NULL);
		time_t time_diff = curr_time_t - info->chipboard[board_id].bcm250[bcm250_id].chips[chip_id].last_error_time;

		if (info->chipboard[board_id].bcm250[bcm250_id].chips[chip_id].recovery_count < CHIP_ERROR_FAIL_LIMIT) {
			if (time_diff >= info->chipboard[board_id].bcm250[bcm250_id].chips[chip_id].recovery_count * CHIP_RECOVERY_INTERVAL) {

				/* change renonce chip address if RENONCE_CHIP_ERROR_FAIL_LIMIT reached */
				if ((renonce_chip(chip_address) == 1) &&
					(opt_bf16_renonce != RENONCE_DISABLED)) {
					if (info->chipboard[board_id].bcm250[bcm250_id].chips[chip_id].recovery_count == RENONCE_CHIP_ERROR_FAIL_LIMIT) {
						applog(LOG_ERR, "%s: chipworker_thr: renonce chip [%d:%d:%2d] failed. trying to switch to another one...",
								bitfury->drv->name,
								chip_address.board_id,
								chip_address.bcm250_id,
								chip_address.chip_id);

						if (change_renonce_chip_address(bitfury, chip_address) == 0) {
							info->chipboard[board_id].bcm250[bcm250_id].chips[chip_id].status          = UNINITIALIZED;
							info->chipboard[board_id].bcm250[bcm250_id].chips[chip_id].last_nonce_time = curr_time_t;
							gettimeofday(&info->chipboard[board_id].bcm250[bcm250_id].chips[chip_id].status_time, NULL);
						}
					} else {
						info->chipboard[board_id].bcm250[bcm250_id].chips[chip_id].status          = UNINITIALIZED;
						info->chipboard[board_id].bcm250[bcm250_id].chips[chip_id].last_nonce_time = curr_time_t;
						gettimeofday(&info->chipboard[board_id].bcm250[bcm250_id].chips[chip_id].status_time, NULL);

						info->chipboard[board_id].bcm250[bcm250_id].chips[chip_id].recovery_count++;
					}
				} else {
					info->chipboard[board_id].bcm250[bcm250_id].chips[chip_id].status          = UNINITIALIZED;
					info->chipboard[board_id].bcm250[bcm250_id].chips[chip_id].last_nonce_time = curr_time_t;
					gettimeofday(&info->chipboard[board_id].bcm250[bcm250_id].chips[chip_id].status_time, NULL);
					gettimeofday(&info->chipboard[board_id].bcm250[bcm250_id].chips[chip_id].switch_time, NULL);

					info->chipboard[board_id].bcm250[bcm250_id].chips[chip_id].recovery_count++;

#ifdef FILELOG
					filelog(info, "BF16: chip [%d:%d:%2d] recovered time_diff: [%d], "
							"recovery_count: [%d], error_rate: [%d]",
							board_id, bcm250_id, chip_id,
							time_diff,
							info->chipboard[board_id].bcm250[bcm250_id].chips[chip_id].recovery_count,
							info->chipboard[board_id].bcm250[bcm250_id].chips[chip_id].error_rate);
#endif
				}
			}
		}
		/* mark chip as DISABLED and never communicate to it again */
		else if (info->chipboard[board_id].bcm250[bcm250_id].chips[chip_id].status != DISABLED) {
			info->chipboard[board_id].bcm250[bcm250_id].chips[chip_id].status = DISABLED;

#ifdef FILELOG
			filelog(info, "BF16: disabling chip [%d:%d:%2d] recovery_count: [%d], error_rate: [%d]",
					board_id, bcm250_id, chip_id,
					info->chipboard[board_id].bcm250[bcm250_id].chips[chip_id].recovery_count,
					info->chipboard[board_id].bcm250[bcm250_id].chips[chip_id].error_rate);
#endif
		}
		break;

	case DISABLED:
	default:
		break;
	}

	return ret;
}

static uint8_t renonce_task_update_loop(struct cgpu_info *bitfury, uint8_t board_id,
		bf_renonce_stage_t stage, uint8_t renonce_count)
{
	struct bitfury16_info *info = (struct bitfury16_info *)(bitfury->device_data);
	bf_cmd_buffer_t* cmd_buffer = &info->chipboard[board_id].cmd_buffer;

	uint8_t bcm250_id = renonce_chip_address[board_id].bcm250_id;
	uint8_t chip_id   = renonce_chip_address[board_id].chip_id;

	uint8_t nonces = 0;

	if (info->chipboard[board_id].bcm250[bcm250_id].chips[chip_id].status < FAILING) {
		L_LOCK(info->renonce_list);
		bf_data_t* rdata = info->renonce_list->head;
		while ((rdata != NULL) && (nonces < renonce_count)) {
			uint8_t ret = 1;

			if ((RENONCE(rdata)->sent == false) &&
				(RENONCE(rdata)->stage == stage)) {
				/* generate work mask */
				uint32_t nonce_mask = gen_mask(RENONCE(rdata)->nonce, mask_bits);
				nonce_mask = ntohl(nonce_mask);

				switch (RENONCE(rdata)->stage) {
					case RENONCE_STAGE0:
					case RENONCE_STAGE2:
						/* generate chip work with new mask */
						cg_memcpy(RENONCE(rdata)->owork.task + 19*4,
								&nonce_mask, sizeof(nonce_mask));

						/* send task and read nonces at the same time */
						ret = cmd_buffer_push(cmd_buffer,
								info->chipboard[board_id].bcm250[bcm250_id].channel_depth,
								renonce_chip_address[board_id], RENONCE(rdata)->src_address,
								RENONCE(rdata)->owork, RENONCE(rdata)->id,
								(CHIP_CMD_TASK_WRITE | CHIP_CMD_TASK_SWITCH | CHIP_CMD_READ_NONCE), 79,
								RENONCE(rdata)->owork.task);
						break;
					case RENONCE_STAGE1:
					case RENONCE_STAGE3:
						/* generate chip work with new mask */
						cg_memcpy(RENONCE(rdata)->cwork.task + 19*4,
								&nonce_mask, sizeof(nonce_mask));

						/* send task and read nonces at the same time */
						ret = cmd_buffer_push(cmd_buffer,
								info->chipboard[board_id].bcm250[bcm250_id].channel_depth,
								renonce_chip_address[board_id], RENONCE(rdata)->src_address,
								RENONCE(rdata)->cwork, RENONCE(rdata)->id,
								(CHIP_CMD_TASK_WRITE | CHIP_CMD_TASK_SWITCH | CHIP_CMD_READ_NONCE), 79,
								RENONCE(rdata)->cwork.task);
						break;
					case RENONCE_STAGE_FINISHED:
					default:
						break;
				}
			}

			if (ret == 0) {
				info->chipboard[board_id].bcm250[bcm250_id].chips[chip_id].status = TASK_SWITCHED;
				RENONCE(rdata)->sent     = true;
				RENONCE(rdata)->received = false;
				nonces++;
			}

			rdata = rdata->next;
		}
		L_UNLOCK(info->renonce_list);
	}

	return nonces;
}

static void renonce_task_update(struct cgpu_info *bitfury, uint8_t board_id, uint8_t renonce_count)
{
	struct bitfury16_info *info = (struct bitfury16_info *)(bitfury->device_data);
	bf_cmd_buffer_t* cmd_buffer = &info->chipboard[board_id].cmd_buffer;

	bf_works_t work;
	uint8_t i;

	uint8_t bcm250_id = renonce_chip_address[board_id].bcm250_id;
	uint8_t chip_id   = renonce_chip_address[board_id].chip_id;

	cmd_buffer_push_create_channel(cmd_buffer,
			info->chipboard[board_id].bcm250[bcm250_id].channel_path,
			info->channel_length);

	if (info->chipboard[board_id].bcm250[bcm250_id].chips[chip_id].status < FAILING) {
		uint8_t toggle[4] = { 0xa5, 0x00, 0x00, 0x02 };

		cmd_buffer_push(cmd_buffer,
				info->chipboard[board_id].bcm250[bcm250_id].channel_depth,
				renonce_chip_address[board_id], renonce_chip_address[board_id],
				work, 0, CHIP_CMD_TOGGLE, 3, toggle);

		uint8_t clock_buf[4];
		memset(clock_buf, 0, sizeof(clock_buf));
		gen_clock_data(bf16_renonce_chip_clock, 1, clock_buf);

		cmd_buffer_push(cmd_buffer,
				info->chipboard[board_id].bcm250[bcm250_id].channel_depth,
				renonce_chip_address[board_id], renonce_chip_address[board_id],
				work, 0, CHIP_CMD_SET_CLOCK, 3, clock_buf);

		uint8_t noncemask[4];
		memset(noncemask, 0, sizeof(noncemask));

		for (i = 0; i < 4; i++)
			noncemask[i] = (mask >> (8*(4 - i - 1))) & 0xff;

		cmd_buffer_push(cmd_buffer,
				info->chipboard[board_id].bcm250[bcm250_id].channel_depth,
				renonce_chip_address[board_id], renonce_chip_address[board_id],
				work, 0, CHIP_CMD_SET_MASK, 3, noncemask);

		bf_renonce_stage_t stage = RENONCE_STAGE0;
		while ((renonce_count > 0) && (stage != RENONCE_STAGE_FINISHED)) {
			renonce_count -= renonce_task_update_loop(bitfury, board_id, stage++, renonce_count);
		}
	}

	cmd_buffer_push_destroy_channel(cmd_buffer,
			info->chipboard[board_id].
			bcm250[renonce_chip_address[board_id].bcm250_id].channel_depth);
}

static void fill_cmd_buffer_loop(struct cgpu_info *bitfury, uint8_t board_id, bool do_renonce, uint16_t renonce_count)
{
	uint8_t bcm250_id, chip_id;
	struct bitfury16_info *info = (struct bitfury16_info *)(bitfury->device_data);
	bf_cmd_buffer_t* cmd_buffer = &info->chipboard[board_id].cmd_buffer;

	/* concentrator loop */
	for (bcm250_id = 0; bcm250_id < BCM250_NUM; bcm250_id++) {
		uint8_t first_good_chip = info->chipboard[board_id].bcm250[bcm250_id].first_good_chip;
		uint8_t last_good_chip  = info->chipboard[board_id].bcm250[bcm250_id].last_good_chip;

		cmd_buffer_push_create_channel(cmd_buffer,
				info->chipboard[board_id].bcm250[bcm250_id].channel_path,
				info->channel_length);

		/* chips loop */
		for (chip_id = first_good_chip; chip_id < last_good_chip; chip_id++) {
			bf_chip_address_t chip_address = { board_id, bcm250_id, chip_id };
			chip_task_update(bitfury, chip_address);
		}

		cmd_buffer_push_destroy_channel(cmd_buffer,
				info->chipboard[board_id].bcm250[bcm250_id].channel_depth);
	}

	if (do_renonce == true) {
		uint8_t count = (cmd_buffer->free_bytes - (2 + 11 + 11 + 11 + 8)) / 136;
		if (count < renonce_count) {
			if (count > 0)
				renonce_task_update(bitfury, board_id, count);
		} else if (renonce_count > 0)
			renonce_task_update(bitfury, board_id, renonce_count);
	}

	cmd_buffer->status = TX_READY;
}

static void fill_cmd_buffer(struct cgpu_info *bitfury, uint8_t board_id)
{
	static uint8_t do_renonce[CHIPBOARD_NUM];
	uint16_t renonce_count = 0;

	struct bitfury16_info *info = (struct bitfury16_info *)(bitfury->device_data);
	bf_cmd_buffer_t* cmd_buffer = &info->chipboard[board_id].cmd_buffer;

	if (cmd_buffer->status == EMPTY) {
		if ((opt_bf16_renonce == RENONCE_DISABLED) ||
			(renonce_chip_address[board_id].board_id == -1)) {

			fill_cmd_buffer_loop(bitfury, board_id, false, 0);
		} else {
			L_LOCK(info->renonce_list);
			bf_data_t* rdata = info->renonce_list->head;
			while (rdata != NULL) {
				if (RENONCE(rdata)->sent == false)
					renonce_count++;
				rdata = rdata->next;
			}
			L_UNLOCK(info->renonce_list);

			if (do_renonce[board_id] < RENONCE_SEND) {
				fill_cmd_buffer_loop(bitfury, board_id, true, renonce_count);

				do_renonce[board_id]++;
			} else {
				if (renonce_count >= RENONCE_COUNT) {
					renonce_task_update(bitfury, board_id, RENONCE_COUNT);

					do_renonce[board_id] = 0;
					cmd_buffer->status = TX_READY;
				} else {
					fill_cmd_buffer_loop(bitfury, board_id, true, renonce_count);

					do_renonce[board_id]++;
				}
			}
		}
	}
}

static uint8_t update_chip_status(struct cgpu_info *bitfury, bf_cmd_status_t cmd_status)
{
	struct bitfury16_info *info = (struct bitfury16_info *)(bitfury->device_data);
	uint8_t board_id  = cmd_status.chip_address.board_id;
	uint8_t bcm250_id = cmd_status.chip_address.bcm250_id;
	uint8_t chip_id   = cmd_status.chip_address.chip_id;

	switch (info->chipboard[board_id].bcm250[bcm250_id].chips[chip_id].status) {

	case UNINITIALIZED:
	case TOGGLE_SET:
	case CLOCK_SET:
	case MASK_SET:
		applog(LOG_DEBUG, "%s: chipworker_thr: chip [%d:%d:%2d], send_cmd [%s]",
				bitfury->drv->name,
				board_id, bcm250_id, chip_id,
				get_cmd_description(cmd_status.cmd_code));

		/* update chip status */
		if (cmd_status.checksum_error == false)
			info->chipboard[board_id].bcm250[bcm250_id].chips[chip_id].status++;

		else {
#ifndef DISABLE_SEND_CMD_ERROR
			applog(LOG_ERR, "%s: chipworker_thr: chip [%d:%d:%2d]: error send_cmd [%s]. "
					"checksum: expected: [%02x]; received: [%02x]",
					bitfury->drv->name,
					board_id, bcm250_id, chip_id,
					get_cmd_description(cmd_status.cmd_code),
					cmd_status.checksum_expected,
					cmd_status.checksum_received);
#endif

			/* increase error counters */
			increase_errors(info, cmd_status.chip_address);
		}
		break;

	case TASK_SENT:
		applog(LOG_DEBUG, "%s: chipworker_thr: chip [%d:%d:%2d], send_cmd [%s]",
				bitfury->drv->name,
				board_id, bcm250_id, chip_id,
			 	get_cmd_description(cmd_status.cmd_code));

		/* read task status from chip*/
		if (cmd_status.checksum_error == false) {
			uint8_t new_buff = ((cmd_status.status & 0x0f) == 0x0f) ? 1 : 0;

			/* status cmd counter */
			info->chipboard[board_id].bcm250[bcm250_id].chips[chip_id].status_cmd_dx++;
			info->chipboard[board_id].bcm250[bcm250_id].status_cmd_dx++;
			info->chipboard[board_id].status_cmd_dx++;
			info->status_cmd_dx++;

			/* check if chip task has switched */
			if (new_buff != info->chipboard[board_id].bcm250[bcm250_id].chips[chip_id].curr_buff) {
				gettimeofday(&info->chipboard[board_id].bcm250[bcm250_id].chips[chip_id].switch_time, NULL);
				info->chipboard[board_id].bcm250[bcm250_id].chips[chip_id].curr_buff = new_buff;
				info->chipboard[board_id].bcm250[bcm250_id].chips[chip_id].status++;

				/* task switch counter */
				increase_task_switch(info, cmd_status.chip_address);

			} else {
				/* task not switched  */
				info->chipboard[board_id].bcm250[bcm250_id].chips[chip_id].status_cmd_none_dx++;
				info->chipboard[board_id].bcm250[bcm250_id].status_cmd_none_dx++;
				info->chipboard[board_id].status_cmd_none_dx++;
				info->status_cmd_none_dx++;

				/* check if chip hang */
				struct timeval curr_time;
				gettimeofday(&curr_time, NULL);

				if ((timediff_us(info->chipboard[board_id].bcm250[bcm250_id].chips[chip_id].switch_time, curr_time) > CHIP_TASK_SWITCH_INTERVAL) &&
					(info->chipboard[board_id].bcm250[bcm250_id].chips[chip_id].status < FAILING)) {
					increase_errors(info, cmd_status.chip_address);

					info->chipboard[board_id].bcm250[bcm250_id].chips[chip_id].status          = FAILING;
					info->chipboard[board_id].bcm250[bcm250_id].chips[chip_id].last_error_time = time(NULL);

					applog(LOG_ERR, "%s: nonceworker_thr: chip [%d:%d:%2d], "
							"failed: no task switch during last [%.3f] seconds",
							bitfury->drv->name,
							board_id, bcm250_id, chip_id, CHIP_TASK_SWITCH_INTERVAL / 1000000.0);

#ifdef FILELOG
					filelog(info, "BF16: no task switch for chip [%d:%d:%2d], "
							"error count: [%d], recovery_count: [%d], error_rate: [%d]",
							board_id, bcm250_id, chip_id,
							info->chipboard[board_id].bcm250[bcm250_id].chips[chip_id].errors,
							info->chipboard[board_id].bcm250[bcm250_id].chips[chip_id].recovery_count,
							info->chipboard[board_id].bcm250[bcm250_id].chips[chip_id].error_rate);
#endif
				}
			}
		} else {
#ifndef DISABLE_SEND_CMD_ERROR
			applog(LOG_ERR, "%s: chipworker_thr: chip [%d:%d:%2d]: error send_cmd [%s]. "
					"checksum: expected: [%02x]; received: [%02x]",
					bitfury->drv->name,
					board_id, bcm250_id, chip_id,
					get_cmd_description(cmd_status.cmd_code),
					cmd_status.checksum_expected,
					cmd_status.checksum_received);
#endif

			/* increase error counters */
			increase_errors(info, cmd_status.chip_address);
		}
		break;

	case TASK_SWITCHED:
		applog(LOG_DEBUG, "%s: chipworker_thr: chip [%d:%d:%2d], send_cmd [%s]",
				bitfury->drv->name,
				board_id, bcm250_id, chip_id,
			 	get_cmd_description(cmd_status.cmd_code));

		if (cmd_status.checksum_error == false) {
			if ((renonce_chip(cmd_status.chip_address) == 1) &&
				(opt_bf16_renonce != RENONCE_DISABLED)) {

				/* task switch counter */
				if (cmd_status.cmd_code & CHIP_CMD_READ_NONCE)
					increase_task_switch(info, cmd_status.chip_address);
			} else if (info->chipboard[board_id].bcm250[bcm250_id].chips[chip_id].status < FAILING) {
				info->chipboard[board_id].bcm250[bcm250_id].chips[chip_id].task_processed++;

				if (info->chipboard[board_id].bcm250[bcm250_id].chips[chip_id].task_processed >= CHIP_RESTART_LIMIT) {
					info->chipboard[board_id].bcm250[bcm250_id].chips[chip_id].status = UNINITIALIZED;
					info->chipboard[board_id].bcm250[bcm250_id].chips[chip_id].task_processed = 0;
				} else
					info->chipboard[board_id].bcm250[bcm250_id].chips[chip_id].status = MASK_SET;
			}

			return 1;
		} else {
#ifndef DISABLE_SEND_CMD_ERROR
			applog(LOG_ERR, "%s: chipworker_thr: chip [%d:%d:%2d]: error send_cmd [%s]. "
					"checksum: expected: [%02x]; received: [%02x]",
					bitfury->drv->name,
					board_id, bcm250_id, chip_id,
					get_cmd_description(cmd_status.cmd_code),
					cmd_status.checksum_expected,
					cmd_status.checksum_received);
#endif

			/* increase error counters */
			increase_errors(info, cmd_status.chip_address);
		}

		break;

	case FAILING:
	case DISABLED:
	default:
		break;
	}

	return 0;
}

static uint8_t process_nonces(struct cgpu_info *bitfury, bf_cmd_status_t cmd_status, uint32_t* nonces)
{
	struct bitfury16_info *info = (struct bitfury16_info *)(bitfury->device_data);

	uint8_t i;
	uint32_t found_nonces[12];

	uint8_t board_id  = cmd_status.chip_address.board_id;
	uint8_t bcm250_id = cmd_status.chip_address.bcm250_id;
	uint8_t chip_id   = cmd_status.chip_address.chip_id;

	if ((cmd_status.checksum_error == false) &&
	    (cmd_status.nonce_checksum_error == false)) {
		cg_memcpy(info->chipboard[board_id].bcm250[bcm250_id].chips[chip_id].rx, nonces, sizeof(found_nonces));

		uint8_t found = find_nonces(info->chipboard[board_id].bcm250[bcm250_id].chips[chip_id].rx,
									info->chipboard[board_id].bcm250[bcm250_id].chips[chip_id].rx_prev,
									found_nonces);

		/* check if chip is still mining */
		if (found == 0) {
			time_t curr_time = time(NULL);
			time_t time_diff = curr_time - info->chipboard[board_id].bcm250[bcm250_id].chips[chip_id].last_nonce_time;

			if ((renonce_chip(cmd_status.chip_address) == 1) &&
				(opt_bf16_renonce != RENONCE_DISABLED)) {
				if ((time_diff >= RENONCE_CHIP_FAILING_INTERVAL) &&
					(info->chipboard[board_id].bcm250[bcm250_id].chips[chip_id].status < FAILING)) {
					increase_errors(info, cmd_status.chip_address);

					info->chipboard[board_id].bcm250[bcm250_id].chips[chip_id].status          = FAILING;
					info->chipboard[board_id].bcm250[bcm250_id].chips[chip_id].last_error_time = curr_time;

					applog(LOG_ERR, "%s: nonceworker_thr: renonce chip [%d:%d:%2d] "
							"failed: no good nonces during last [%.1f] seconds",
							bitfury->drv->name,
							board_id, bcm250_id, chip_id, RENONCE_CHIP_FAILING_INTERVAL);

#ifdef FILELOG
					filelog(info, "BF16: no good nonces from renonce chip [%d:%d:%2d], "
							"error count: [%d] recovery_count: [%d], error_rate: [%d]",
							board_id, bcm250_id, chip_id,
							info->chipboard[board_id].bcm250[bcm250_id].chips[chip_id].errors,
							info->chipboard[board_id].bcm250[bcm250_id].chips[chip_id].recovery_count,
							info->chipboard[board_id].bcm250[bcm250_id].chips[chip_id].error_rate);
#endif
				}
			} else {
				if ((time_diff >= CHIP_FAILING_INTERVAL) &&
					(info->chipboard[board_id].bcm250[bcm250_id].chips[chip_id].status < FAILING)) {
					increase_errors(info, cmd_status.chip_address);

					info->chipboard[board_id].bcm250[bcm250_id].chips[chip_id].status          = FAILING;
					info->chipboard[board_id].bcm250[bcm250_id].chips[chip_id].last_error_time = curr_time;

					applog(LOG_ERR, "%s: nonceworker_thr: chip [%d:%d:%2d], "
							"failed: no good nonces during last [%.1f] seconds",
							bitfury->drv->name,
							board_id, bcm250_id, chip_id, CHIP_FAILING_INTERVAL);

#ifdef FILELOG
					filelog(info, "BF16: no good nonces from chip [%d:%d:%2d], "
							"error count: [%d], recovery_count: [%d], error_rate: [%d]",
							board_id, bcm250_id, chip_id,
							info->chipboard[board_id].bcm250[bcm250_id].chips[chip_id].errors,
							info->chipboard[board_id].bcm250[bcm250_id].chips[chip_id].recovery_count,
							info->chipboard[board_id].bcm250[bcm250_id].chips[chip_id].error_rate);
#endif
				}
			}
		}

		/* increase stage if no nonces received */
		if ((renonce_chip(cmd_status.chip_address) == 1) &&
			(opt_bf16_renonce != RENONCE_DISABLED)) {
			L_LOCK(info->renonce_list);
			bf_data_t* rdata = info->renonce_list->head;
			while (rdata != NULL) {
				if (RENONCE(rdata)->id == cmd_status.id) {
					if (found == 0) {
						RENONCE(rdata)->stage++;
						RENONCE(rdata)->sent = false;

						/* clear renonces list if we are running too slow */
						if ((RENONCE(rdata)->stage >= RENONCE_STAGE2) &&
							(info->renonce_list->count > RENONCE_STAGE2_LIMIT)) {
							info->unmatched++;
							increase_re_bad_nonces(info, RENONCE(rdata)->src_address);
							bf_data_t* rndata = rdata->next;
							renonce_list_remove(info->renonce_list, rdata);
							rdata = rndata;
							continue;
						}

						if ((RENONCE(rdata)->stage >= RENONCE_STAGE3) &&
							(info->renonce_list->count > RENONCE_STAGE3_LIMIT)) {
							info->unmatched++;
							increase_re_bad_nonces(info, RENONCE(rdata)->src_address);
							bf_data_t* rndata = rdata->next;
							renonce_list_remove(info->renonce_list, rdata);
							rdata = rndata;
							continue;
						}

						/* remove expired renonce */
						if (RENONCE(rdata)->stage == RENONCE_STAGE_FINISHED) {
							info->unmatched++;
							increase_re_bad_nonces(info, RENONCE(rdata)->src_address);
							bf_data_t* rndata = rdata->next;
							renonce_list_remove(info->renonce_list, rdata);
							rdata = rndata;
							continue;
						}
					}

					RENONCE(rdata)->received = true;
					break;
				}
				rdata = rdata->next;
			}
			L_UNLOCK(info->renonce_list);
		}

		bf_list_t* nonce_list = info->chipboard[board_id].bcm250[bcm250_id].chips[chip_id].nonce_list;
		for (i = 0; i < found; i++) {
			L_LOCK(nonce_list);
			int8_t res = nonce_list_push(nonce_list, found_nonces[i]);
			L_UNLOCK(nonce_list);
			if (res < 0)
				continue;

			increase_total_nonces(info, cmd_status.chip_address);

			/* check if nonce has errors and add it to renonce list */
			if ((opt_bf16_renonce != RENONCE_DISABLED) &&
				(renonce_chip(cmd_status.chip_address) == 0) &&
				(found_nonces[i] & 0xfff00000) == 0xaaa00000) {
				increase_re_nonces(info, cmd_status.src_address);

				L_LOCK(info->renonce_list);
				if (info->renonce_list->count < RENONCE_QUEUE_LEN) {
					renonce_list_push(info->renonce_list,
							info->renonce_id++,
							found_nonces[i],
					   		cmd_status.chip_address,
					   		info->chipboard[board_id].bcm250[bcm250_id].chips[chip_id].cwork,
				   		info->chipboard[board_id].bcm250[bcm250_id].chips[chip_id].owork); 
				} else
					increase_re_bad_nonces(info, cmd_status.chip_address);
				L_UNLOCK(info->renonce_list);

				applog(LOG_DEBUG, "%s: chipworker_thr: pushing renonce task: nonce: [%08x]",
						bitfury->drv->name,
						found_nonces[i]);
				continue;
			}

			/* add nonces to noncework list */
			if ((renonce_chip(cmd_status.chip_address) == 1) &&
				(opt_bf16_renonce != RENONCE_DISABLED)) {
				L_LOCK(info->renoncework_list);
				renoncework_list_push(info->renoncework_list, cmd_status.chip_address, found_nonces[i]);
				L_UNLOCK(info->renoncework_list);
			} else {
				L_LOCK(info->noncework_list);
				noncework_list_push(info->noncework_list,
						cmd_status.chip_address, cmd_status.src_address,
						info->chipboard[board_id].bcm250[bcm250_id].chips[chip_id].cwork,
						info->chipboard[board_id].bcm250[bcm250_id].chips[chip_id].owork,
						found_nonces[i]);
				L_UNLOCK(info->noncework_list);
			}

			applog(LOG_DEBUG, "%s: chipworker_thr: pushing nonce task: nonce: [%08x]",
					bitfury->drv->name,
					found_nonces[i]);
		}

		/* rotate works and buffers */
		cg_memcpy(info->chipboard[board_id].bcm250[bcm250_id].chips[chip_id].rx_prev,
			   info->chipboard[board_id].bcm250[bcm250_id].chips[chip_id].rx, sizeof(found_nonces));

		cg_memcpy(&info->chipboard[board_id].bcm250[bcm250_id].chips[chip_id].owork,
				&info->chipboard[board_id].bcm250[bcm250_id].chips[chip_id].cwork, sizeof(bf_works_t));

		/* remove old nonces from nonce list */
		L_LOCK(nonce_list);
		if ((renonce_chip(cmd_status.chip_address) == 1) &&
			(opt_bf16_renonce != RENONCE_DISABLED)) {
			while (nonce_list->count > RENONCE_CHIP_QUEUE_LEN)
				nonce_list_pop(nonce_list);
		} else {
			while (nonce_list->count > NONCE_CHIP_QUEUE_LEN)
				nonce_list_pop(nonce_list);
		}
		L_UNLOCK(nonce_list);
	} else {
		if (cmd_status.checksum_error != 0) {
#ifndef DISABLE_SEND_CMD_ERROR
			applog(LOG_ERR, "%s: chipworker_thr: chip [%d:%d:%2d]: error send_cmd [%s]. "
					"checksum: expected: [%02x]; received: [%02x]",
					bitfury->drv->name,
					board_id, bcm250_id, chip_id,
					get_cmd_description(cmd_status.cmd_code),
					cmd_status.checksum_expected,
					cmd_status.checksum_received);
#endif

			/* increase error counters */
			increase_errors(info, cmd_status.chip_address);
		}

		if (cmd_status.nonce_checksum_error != 0) {
			if ((renonce_chip(cmd_status.chip_address) == 0) ||
				(opt_bf16_renonce == RENONCE_DISABLED)) {
				applog(LOG_ERR, "%s: chipworker_thr: chip [%d:%d:%2d]: error receiving data. "
						"nonce checksum: expected: [%02x]; received: [%02x]",
						bitfury->drv->name,
						board_id, bcm250_id, chip_id,
						cmd_status.checksum_expected,
						cmd_status.checksum_received);

				/* increase error counters */
				increase_errors(info, cmd_status.chip_address);
			}
		}
	}

	return 0;
}

/* routine sending-receiving data to chipboard */
static void process_cmd_buffer(struct cgpu_info *bitfury, uint8_t board_id)
{
	struct bitfury16_info *info = (struct bitfury16_info *)(bitfury->device_data);
	uint8_t i;
	bf_cmd_status_t cmd_status;
	bf_cmd_buffer_t* cmd_buffer = &info->chipboard[board_id].cmd_buffer;

	if (cmd_buffer->status == EXECUTED) {
		/* process extracted data */
		uint16_t cmd_number = cmd_buffer->cmd_list->count;
		for (i = 0; i < cmd_number; i++) {
			uint32_t nonces[12];

			cmd_buffer_pop(cmd_buffer, &cmd_status, nonces);
			if ((cmd_status.cmd_code == CHIP_CMD_CREATE_CHANNEL) ||
				(cmd_status.cmd_code == CHIP_CMD_TASK_SWITCH))
				continue;

			uint8_t task_switch = update_chip_status(bitfury, cmd_status);

			/* analyze nonces */
			if ((cmd_status.cmd_code & CHIP_CMD_READ_NONCE) &&
			    (task_switch == 1)) {
				process_nonces(bitfury, cmd_status, nonces);
			}
		}

		cmd_buffer_clear(cmd_buffer);
	}
}

static void *bitfury_chipworker(void *userdata)
{
	struct cgpu_info *bitfury = (struct cgpu_info *)userdata;
	struct bitfury16_info *info = (struct bitfury16_info *)(bitfury->device_data);
	uint8_t board_id;

	applog(LOG_INFO, "%s: started chipworker thread", bitfury->drv->name);

	while (bitfury->shutdown == false) {
		if (info->initialised) {
			break;
		}
		cgsleep_us(30);
	}

	/* send reset sequence to boards */
	spi_emit_reset(SPI_CHANNEL1);
	spi_emit_reset(SPI_CHANNEL2);

	while (bitfury->shutdown == false) {
		if ((info->a_temp == false) &&
			(info->a_ichain == false)) {
			for (board_id = 0; board_id < CHIPBOARD_NUM; board_id++) {
				if (info->chipboard[board_id].detected == true) {
					/* prepare send buffer */
					struct timeval start_time, stop_time;
					gettimeofday(&start_time, NULL);

					if (info->chipboard[board_id].cmd_buffer.status == EMPTY)
						fill_cmd_buffer(bitfury, board_id);

					gettimeofday(&stop_time, NULL);

#if 0
					applog(LOG_ERR, "%s: chipworker_thr: board %d buffer prepare: time elapsed: [%.6f]",
							bitfury->drv->name, board_id, timediff(start_time, stop_time));
#endif

					gettimeofday(&start_time, NULL);

					/* send buffer to chipboard */
					if (info->chipboard[board_id].cmd_buffer.status == TX_READY) {
						spi_emit_reset(board_id + 1);

						cmd_buffer_exec(board_id + 1, &info->chipboard[board_id].cmd_buffer);
						info->chipboard[board_id].bytes_transmitted_dx += info->chipboard[board_id].cmd_buffer.tx_offset;
						info->chipboard[board_id].bytes_transmitted    += info->chipboard[board_id].cmd_buffer.tx_offset;
					}

					gettimeofday(&stop_time, NULL);

#if 0
					applog(LOG_ERR, "%s: chipworker_thr: board %d TX/RX time: [%.6f]",
							bitfury->drv->name, board_id, timediff(start_time, stop_time));
#endif

					gettimeofday(&start_time, NULL);

					/* analyze received data */
					if (info->chipboard[board_id].cmd_buffer.status == EXECUTED) {
						process_cmd_buffer(bitfury, board_id);
					}

					gettimeofday(&stop_time, NULL);

#if 0
					applog(LOG_ERR, "%s: chipworker_thr: board %d buffer processing: time elapsed: [%.6f]",
							bitfury->drv->name, board_id, timediff(start_time, stop_time));
#endif
				}
			}
		} else
			cgsleep_us(CHIPWORKER_DELAY);
	}

	applog(LOG_INFO, "%s: chipworker_thr: exiting...", bitfury->drv->name);
	return NULL;
}

static int16_t cleanup_older(struct cgpu_info *bitfury)
{
	struct bitfury16_info *info = (struct bitfury16_info *)(bitfury->device_data);
	time_t curr_time = time(NULL);
	uint16_t released = 0;

	/* clear stale work list */
	L_LOCK(info->stale_work_list);
	bf_data_t* wdata = info->stale_work_list->head;
	while (wdata != NULL) {
		if (curr_time - WORKD(wdata)->generated >= WORK_TIMEOUT) {
			workd_list_pop(info->stale_work_list, bitfury);
			released++;
		} else
			break;
		wdata = info->stale_work_list->head;
	}
	L_UNLOCK(info->stale_work_list);

	applog(LOG_INFO, "%s: released %d works", bitfury->drv->name, released);
	return released;
}

static void *bitfury_nonceworker(void *userdata)
{
	struct cgpu_info *bitfury = (struct cgpu_info *)userdata;
	struct bitfury16_info *info = (struct bitfury16_info *)(bitfury->device_data);

	applog(LOG_INFO, "%s: started nonceworker thread", bitfury->drv->name);

	while (bitfury->shutdown == false) {
		if (info->initialised) {
			break;
		}
		cgsleep_us(30);
	}

	applog(LOG_INFO, "%s: nonceworker loop started", bitfury->drv->name);

	while (bitfury->shutdown == false) {
		struct timeval start_time, stop_time;
		gettimeofday(&start_time, NULL);

		uint32_t nonce_cnt = 0;

		/* general nonces processing */
		L_LOCK(info->noncework_list);
		bf_data_t* nwdata = info->noncework_list->head;
		while (nwdata != NULL) {
			uint8_t board_id  = NONCEWORK(nwdata)->chip_address.board_id;
			uint8_t bcm250_id = NONCEWORK(nwdata)->chip_address.bcm250_id;
			uint8_t chip_id   = NONCEWORK(nwdata)->chip_address.chip_id;

			nonce_cnt++;

			/* general chip results processing */
			if (test_nonce(&NONCEWORK(nwdata)->owork.work, NONCEWORK(nwdata)->nonce)) {
				applog(LOG_DEBUG, "%s: nonceworker_thr: chip [%d:%d:%2d], valid nonce [%08x]",
						bitfury->drv->name,
						board_id, bcm250_id, chip_id, NONCEWORK(nwdata)->nonce);

				submit_tested_work(info->thr, &NONCEWORK(nwdata)->owork.work);

				info->chipboard[board_id].bcm250[bcm250_id].chips[chip_id].last_nonce_time = time(NULL);

				if (info->chipboard[board_id].bcm250[bcm250_id].chips[chip_id].recovery_count > 0) {
					info->chipboard[board_id].bcm250[bcm250_id].chips[chip_id].recovery_count = 0;

#ifdef FILELOG
					filelog(info, "BF16: good nonce for chip [%d:%d:%2d] "
							"setting recovery_count to [0], error_rate: [%d]",
							board_id, bcm250_id, chip_id,
							info->chipboard[board_id].bcm250[bcm250_id].chips[chip_id].error_rate);
#endif
				}

				increase_good_nonces(info, NONCEWORK(nwdata)->chip_address);

				mutex_lock(&info->nonces_good_lock);
				info->nonces_good_cg++;
				mutex_unlock(&info->nonces_good_lock);
			} else if (test_nonce(&NONCEWORK(nwdata)->cwork.work, NONCEWORK(nwdata)->nonce)) {
				applog(LOG_DEBUG, "%s: nonceworker_thr: chip [%d:%d:%2d], valid nonce [%08x]",
						bitfury->drv->name,
						board_id, bcm250_id, chip_id, NONCEWORK(nwdata)->nonce);

				submit_tested_work(info->thr, &NONCEWORK(nwdata)->cwork.work);

				info->chipboard[board_id].bcm250[bcm250_id].chips[chip_id].last_nonce_time = time(NULL);

				if (info->chipboard[board_id].bcm250[bcm250_id].chips[chip_id].recovery_count > 0) {
					info->chipboard[board_id].bcm250[bcm250_id].chips[chip_id].recovery_count = 0;

#ifdef FILELOG
					filelog(info, "BF16: good nonce for chip [%d:%d:%2d] "
							"setting recovery_count to 0, error_rate: [%d]",
							board_id, bcm250_id, chip_id,
							info->chipboard[board_id].bcm250[bcm250_id].chips[chip_id].error_rate);
#endif
				}

				increase_good_nonces(info, NONCEWORK(nwdata)->chip_address);

				mutex_lock(&info->nonces_good_lock);
				info->nonces_good_cg++;
				mutex_unlock(&info->nonces_good_lock);
			} else {
				time_t curr_time = time(NULL);
				time_t time_diff = curr_time - info->chipboard[board_id].bcm250[bcm250_id].chips[chip_id].last_nonce_time;

				if ((time_diff >= CHIP_FAILING_INTERVAL) &&
					(info->chipboard[board_id].bcm250[bcm250_id].chips[chip_id].status < FAILING)) {
					increase_errors(info, NONCEWORK(nwdata)->chip_address);

					info->chipboard[board_id].bcm250[bcm250_id].chips[chip_id].status          = FAILING;
					info->chipboard[board_id].bcm250[bcm250_id].chips[chip_id].last_error_time = curr_time;

					applog(LOG_ERR, "%s: nonceworker_thr: chip [%d:%d:%2d], "
							"failed: no good nonces during last [%.1f] seconds",
							bitfury->drv->name,
							board_id, bcm250_id, chip_id, CHIP_FAILING_INTERVAL);

#ifdef FILELOG
					filelog(info, "BF16: no good nonces from chip [%d:%d:%2d], "
							"error count: [%d], recovery_count: [%d], error_rate: [%d]",
							board_id, bcm250_id, chip_id,
							info->chipboard[board_id].bcm250[bcm250_id].chips[chip_id].errors,
							info->chipboard[board_id].bcm250[bcm250_id].chips[chip_id].recovery_count,
							info->chipboard[board_id].bcm250[bcm250_id].chips[chip_id].error_rate);
#endif
				}

				if (opt_bf16_renonce != RENONCE_DISABLED) {
					/* add failed nonce to renonce list */
					increase_bad_nonces(info, NONCEWORK(nwdata)->chip_address);

					L_LOCK(info->renonce_list);
					if (info->renonce_list->count < RENONCE_QUEUE_LEN) {
						renonce_list_push(info->renonce_list,
								info->renonce_id++,
								NONCEWORK(nwdata)->nonce,
								NONCEWORK(nwdata)->chip_address,
								NONCEWORK(nwdata)->cwork,
								NONCEWORK(nwdata)->owork);
					} else
						increase_re_bad_nonces(info, NONCEWORK(nwdata)->chip_address);
					L_UNLOCK(info->renonce_list);

					applog(LOG_DEBUG, "%s: nonceworker_thr: pushing renonce task: nonce: [%08x]",
							bitfury->drv->name,
							NONCEWORK(nwdata)->nonce);
				} else
					increase_bad_nonces(info, NONCEWORK(nwdata)->chip_address);
			}

			/* remove nonce from list */
			noncework_list_pop(info->noncework_list);
			nwdata = info->noncework_list->head;
		}
		L_UNLOCK(info->noncework_list);

		/* cleanup older works */
		cleanup_older(bitfury);

		gettimeofday(&stop_time, NULL);

#if 0
		applog(LOG_DEBUG, "%s: nonceworker_thr: nonces processed [%d]: time elapsed: [%.6f]",
				bitfury->drv->name,
				nonce_cnt, timediff(start_time, stop_time));
#endif

		cgsleep_us(NONCEWORKER_DELAY);
	}

	applog(LOG_INFO, "%s: nonceworker_thr: exiting...", bitfury->drv->name);
	return NULL;
}

static bool test_renonce(struct cgpu_info *bitfury, bf_data_t* rdata, bf_data_t* rnwdata, bool owork)
{
	struct bitfury16_info *info = (struct bitfury16_info *)(bitfury->device_data);

	uint8_t board_id  = RENONCEWORK(rnwdata)->src_address.board_id;
	uint8_t bcm250_id = renonce_chip_address[board_id].bcm250_id;
	uint8_t chip_id   = renonce_chip_address[board_id].chip_id;

	if (owork == true) {
		if (test_nonce(&RENONCE(rdata)->owork.work, RENONCEWORK(rnwdata)->nonce)) {
			applog(LOG_DEBUG, "%s: renonceworker_thr: restored renonce: nonce: [%08x]",
					bitfury->drv->name,
					RENONCEWORK(rnwdata)->nonce);

			submit_tested_work(info->thr, &RENONCE(rdata)->owork.work);

			info->chipboard[board_id].bcm250[bcm250_id].chips[chip_id].last_nonce_time = time(NULL);

			if (info->chipboard[board_id].bcm250[bcm250_id].chips[chip_id].recovery_count > 0) {
				info->chipboard[board_id].bcm250[bcm250_id].chips[chip_id].recovery_count = 0;

#ifdef FILELOG
				filelog(info, "BF16: good nonce for renonce chip [%d:%d:%2d] "
						"setting recovery_count to 0, error_rate: [%d]",
						board_id, bcm250_id, chip_id,
						info->chipboard[board_id].bcm250[bcm250_id].chips[chip_id].error_rate);
#endif
			}

			increase_re_good_nonces(info, RENONCE(rdata)->src_address);
			increase_re_good_nonces(info, renonce_chip_address[board_id]);

			mutex_lock(&info->nonces_good_lock);
			info->nonces_good_cg++;
			mutex_unlock(&info->nonces_good_lock);

			RENONCE(rdata)->match = true;
		} else {
			time_t curr_time = time(NULL);
			time_t time_diff = curr_time - info->chipboard[board_id].bcm250[bcm250_id].chips[chip_id].last_nonce_time;

			if ((time_diff >= RENONCE_CHIP_FAILING_INTERVAL) &&
				(info->chipboard[board_id].bcm250[bcm250_id].chips[chip_id].status < FAILING)) {
				increase_errors(info, renonce_chip_address[board_id]);

				info->chipboard[board_id].bcm250[bcm250_id].chips[chip_id].status          = FAILING;
				info->chipboard[board_id].bcm250[bcm250_id].chips[chip_id].last_error_time = curr_time;

				applog(LOG_ERR, "%s: nonceworker_thr: renonce chip [%d:%d:%2d] "
						"failed: no good nonces during last [%.1f] seconds",
						bitfury->drv->name,
						board_id, bcm250_id, chip_id, RENONCE_CHIP_FAILING_INTERVAL);

#ifdef FILELOG
				filelog(info, "BF16: no good nonces from renonce chip [%d:%d:%2d], "
						"error count: [%d] recovery_count: [%d], error_rate: [%d]",
						board_id, bcm250_id, chip_id,
						info->chipboard[board_id].bcm250[bcm250_id].chips[chip_id].errors,
						info->chipboard[board_id].bcm250[bcm250_id].chips[chip_id].recovery_count,
						info->chipboard[board_id].bcm250[bcm250_id].chips[chip_id].error_rate);
#endif
			}
		}
	} else {
		if (test_nonce(&RENONCE(rdata)->cwork.work, RENONCEWORK(rnwdata)->nonce)) {
			applog(LOG_DEBUG, "%s: renonceworker_thr: restored renonce: nonce: [%08x]",
					bitfury->drv->name,
					RENONCEWORK(rnwdata)->nonce);

			submit_tested_work(info->thr, &RENONCE(rdata)->cwork.work);

			info->chipboard[board_id].bcm250[bcm250_id].chips[chip_id].last_nonce_time = time(NULL);

			if (info->chipboard[board_id].bcm250[bcm250_id].chips[chip_id].recovery_count > 0) {
				info->chipboard[board_id].bcm250[bcm250_id].chips[chip_id].recovery_count = 0;

#ifdef FILELOG
				filelog(info, "BF16: good nonce for renonce chip [%d:%d:%2d] "
						"setting recovery_count to 0, error_rate: [%d]",
						board_id, bcm250_id, chip_id,
						info->chipboard[board_id].bcm250[bcm250_id].chips[chip_id].error_rate);
#endif
			}

			increase_re_good_nonces(info, RENONCE(rdata)->src_address);
			increase_re_good_nonces(info, renonce_chip_address[board_id]);

			mutex_lock(&info->nonces_good_lock);
			info->nonces_good_cg++;
			mutex_unlock(&info->nonces_good_lock);

			RENONCE(rdata)->match = true;
		} else {
			time_t curr_time = time(NULL);
			time_t time_diff = curr_time - info->chipboard[board_id].bcm250[bcm250_id].chips[chip_id].last_nonce_time;

			if ((time_diff >= RENONCE_CHIP_FAILING_INTERVAL) &&
				(info->chipboard[board_id].bcm250[bcm250_id].chips[chip_id].status < FAILING)) {
				increase_errors(info, renonce_chip_address[board_id]);

				info->chipboard[board_id].bcm250[bcm250_id].chips[chip_id].status          = FAILING;
				info->chipboard[board_id].bcm250[bcm250_id].chips[chip_id].last_error_time = curr_time;

				applog(LOG_ERR, "%s: nonceworker_thr: renonce chip [%d:%d:%2d] "
						"failed: no good nonces during last [%.1f] seconds",
						bitfury->drv->name,
						board_id, bcm250_id, chip_id, RENONCE_CHIP_FAILING_INTERVAL);

#ifdef FILELOG
				filelog(info, "BF16: no good nonces from renonce chip [%d:%d:%2d], "
						"error count: [%d] recovery_count: [%d], error_rate: [%d]",
						board_id, bcm250_id, chip_id,
						info->chipboard[board_id].bcm250[bcm250_id].chips[chip_id].errors,
						info->chipboard[board_id].bcm250[bcm250_id].chips[chip_id].recovery_count,
						info->chipboard[board_id].bcm250[bcm250_id].chips[chip_id].error_rate);
#endif
			}
		}
	}

	return RENONCE(rdata)->match;
}

static void *bitfury_renonceworker(void *userdata)
{
	struct cgpu_info *bitfury = (struct cgpu_info *)userdata;
	struct bitfury16_info *info = (struct bitfury16_info *)(bitfury->device_data);
	bf_list_t* id_list = nonce_list_init();

	applog(LOG_INFO, "%s: started renonceworker thread", bitfury->drv->name);

	while (bitfury->shutdown == false) {
		if (info->initialised) {
			break;
		}
		cgsleep_us(30);
	}

	applog(LOG_INFO, "%s: renonceworker loop started", bitfury->drv->name);

	while (bitfury->shutdown == false) {
		struct timeval start_time, stop_time;
		gettimeofday(&start_time, NULL);

		uint32_t nonce_cnt = 0;

		/* renonce chip results processing */
		L_LOCK(info->renoncework_list);
		bf_data_t* rnwdata = info->renoncework_list->head;
		while (rnwdata != NULL) {
			nonce_cnt++;

			/* find nonce by id */
			L_LOCK(info->renonce_list);
			bf_data_t* rdata = info->renonce_list->head;
			while (rdata != NULL) {
				if (match_nonce(RENONCEWORK(rnwdata)->nonce, RENONCE(rdata)->nonce, mask_bits)) {
					if (RENONCE(rdata)->match == false) {
						switch (RENONCE(rdata)->stage) {
							case RENONCE_STAGE0:
								if (test_renonce(bitfury, rdata, rnwdata, true) == true)
									info->stage0_match++;
								else
									info->stage0_mismatch++;
								break;
							case RENONCE_STAGE1:
							case RENONCE_STAGE2:
							case RENONCE_STAGE3:
								/* test old work first */
								if (test_renonce(bitfury, rdata, rnwdata, true) == true) {
									if (RENONCE(rdata)->stage == RENONCE_STAGE1)
										info->stage1_mismatch++;

									if (RENONCE(rdata)->stage == RENONCE_STAGE2)
										info->stage2_match++;

									if (RENONCE(rdata)->stage == RENONCE_STAGE3)
										info->stage3_mismatch++;
								} else {
									if (test_renonce(bitfury, rdata, rnwdata, false) == true) {
										if (RENONCE(rdata)->stage == RENONCE_STAGE1)
											info->stage1_match++;

										if (RENONCE(rdata)->stage == RENONCE_STAGE2)
											info->stage2_mismatch++;

										if (RENONCE(rdata)->stage == RENONCE_STAGE3)
											info->stage3_match++;
									}
								}
								break;
							default:
								applog(LOG_ERR, "%s: renonceworker_thr: invalid renonce stage arrived: [%d]",
										bitfury->drv->name,
										RENONCE(rdata)->stage);
						}
					}
					nonce_list_push(id_list, RENONCE(rdata)->id);

					if (RENONCE(rdata)->match == true)
						break;
				}

				rdata = rdata->next;
			}
			L_UNLOCK(info->renonce_list);

			/* remove nonce from list */
			renoncework_list_pop(info->renoncework_list);
			rnwdata = info->renoncework_list->head;
		}
		L_UNLOCK(info->renoncework_list);

		L_LOCK(info->renonce_list);
		bf_data_t* rdata = info->renonce_list->head;
		while (rdata != NULL) {
			if (RENONCE(rdata)->match == true) {
				bf_data_t* rndata = rdata->next;
				renonce_list_remove(info->renonce_list, rdata);
				rdata = rndata;
				continue;
			}

			/* update stage */
			bf_data_t* ndata = id_list->head;
			while (ndata != NULL) {
				if (NONCE(ndata)->nonce == RENONCE(rdata)->id) {
					RENONCE(rdata)->stage++;
					RENONCE(rdata)->sent     = false;
					RENONCE(rdata)->received = false;
					break;
				}
				ndata = ndata->next;
			}

			/* update tasks with wrong or dup nonces recieved */
			if ((RENONCE(rdata)->stage != RENONCE_STAGE_FINISHED) &&
				(RENONCE(rdata)->received == true)) {
				RENONCE(rdata)->stage++;
				RENONCE(rdata)->sent     = false;
				RENONCE(rdata)->received = false;
			}

			/* clear renonces list if we are running too slow */
			if ((RENONCE(rdata)->stage >= RENONCE_STAGE2) &&
				(info->renonce_list->count > RENONCE_STAGE2_LIMIT)) {
				info->unmatched++;
				increase_re_bad_nonces(info, RENONCE(rdata)->src_address);
				bf_data_t* rndata = rdata->next;
				renonce_list_remove(info->renonce_list, rdata);
				rdata = rndata;
				continue;
			}

			if ((RENONCE(rdata)->stage >= RENONCE_STAGE3) &&
				(info->renonce_list->count > RENONCE_STAGE3_LIMIT)) {
				info->unmatched++;
				increase_re_bad_nonces(info, RENONCE(rdata)->src_address);
				bf_data_t* rndata = rdata->next;
				renonce_list_remove(info->renonce_list, rdata);
				rdata = rndata;
				continue;
			}

			if ((RENONCE(rdata)->stage == RENONCE_STAGE_FINISHED) &&
				(RENONCE(rdata)->sent == false)) {
				info->unmatched++;
				increase_re_bad_nonces(info, RENONCE(rdata)->src_address);
				bf_data_t* rndata = rdata->next;
				renonce_list_remove(info->renonce_list, rdata);
				rdata = rndata;
				continue;
			}

			rdata = rdata->next;
		}
		L_UNLOCK(info->renonce_list);

		/* clear id list */
		bf_data_t* ndata = id_list->head;
		while (ndata != NULL) {
			nonce_list_pop(id_list);
			ndata = id_list->head;
		}

		gettimeofday(&stop_time, NULL);

#if 0
		applog(LOG_DEBUG, "%s: renonceworker_thr: nonces processed [%d]: time elapsed: [%.6f]",
				bitfury->drv->name,
				nonce_cnt, timediff(start_time, stop_time));
#endif

		cgsleep_us(RENONCEWORKER_DELAY);
	}

	applog(LOG_INFO, "%s: renonceworker_thr: exiting...", bitfury->drv->name);
	return NULL;
}

int16_t update_pid(bf_pid_t *pid, int16_t error)
{
	int16_t pTerm, iTerm;

	pTerm = 2 * error;

	pid->i_state += error;
	if(pid->i_state > pid->i_max)
		pid->i_state = pid->i_max; 
	else if(pid->i_state < pid->i_min)
		pid->i_state = pid->i_min;  

	iTerm = pid->i_state;

	return (pTerm + iTerm);
}

static void *bitfury_hwmonitor(void *userdata)
{
	struct cgpu_info *bitfury = (struct cgpu_info *)userdata;
	struct bitfury16_info *info = (struct bitfury16_info *)(bitfury->device_data);

	uint8_t board_id, bcm250_id, chip_id;
	time_t curr_time;

	applog(LOG_INFO, "%s: started hwmonitor thread", bitfury->drv->name);

	while (bitfury->shutdown == false) {
		if (info->initialised) {
			break;
		}
		cgsleep_us(30);
	}

	applog(LOG_INFO, "%s: hwmonitor loop started", bitfury->drv->name);

	while (bitfury->shutdown == false) {
		float max_temp = 0.0;
		float t_alarm = 100.0;

		bool a_temp = false;
		bool a_ichain = false;
		for (board_id = 0; board_id < CHIPBOARD_NUM; board_id++) {
			/* read hw sensor data */
			char buff[256];

			if (info->chipboard[board_id].detected == true) {
				memset(buff, 0, sizeof(buff));
				if (device_uart_txrx(board_id + 1, "S", buff) < 0)
					quit(1, "%s: %s() failed to get BOARD%d status",
						bitfury->drv->name, __func__, board_id + 1);

				if (parse_hwstats(info, board_id, buff) < 0)
					applog(LOG_ERR, "%s: failed to parse hw stats",
							bitfury->drv->name);

				info->chipboard[board_id].p_chain1 = info->chipboard[board_id].u_chain1 * info->chipboard[board_id].i_chain1;
				info->chipboard[board_id].p_chain2 = info->chipboard[board_id].u_chain2 * info->chipboard[board_id].i_chain2;

				/* fan power calculation */
				if (info->chipboard[board_id].rpm != 0) {
					if (info->chipboard[board_id].fan_speed > 40)
						info->chipboard[board_id].p_fan = (0.1 + 0.0236 * (info->chipboard[board_id].fan_speed - 40)) *
							(info->chipboard[board_id].u_board + U_LOSS);
					else
						info->chipboard[board_id].p_fan = 1.23;
				} else {
					info->chipboard[board_id].p_fan = 0.0;
				}

				/* board power calculation */
#ifdef MINER_X5
				float i_board = info->chipboard[board_id].i_chain1;
#endif

#ifdef MINER_X6
				float i_board = info->chipboard[board_id].i_chain1 + info->chipboard[board_id].i_chain2;
#endif
				info->chipboard[board_id].p_board = ((info->chipboard[board_id].u_board + U_LOSS) * i_board + 2.0 +
						info->chipboard[board_id].p_fan);

				if (info->chipboard[board_id].a_temp == 1)
					a_temp = true;

				if ((info->chipboard[board_id].a_ichain1 == 1) ||
					(info->chipboard[board_id].a_ichain2 == 1))
					a_ichain = true;

				if (max_temp < info->chipboard[board_id].temp)
					max_temp = info->chipboard[board_id].temp;

				if (t_alarm > info->chipboard[board_id].t_alarm)
					t_alarm = info->chipboard[board_id].t_alarm;
			}
		}
 
		/* enable temp alarm */
		if ((a_temp == true) && (info->a_temp == false)) {
			applog(LOG_ERR, "%s: temperature alarm enabled: [%5.1f]!!!",
					bitfury->drv->name, max_temp);

			/* enable fans to full speed*/
			for (board_id = 0; board_id < CHIPBOARD_NUM; board_id++) {
				if (info->chipboard[board_id].detected == true) {
					if (device_uart_transfer(board_id + 1, "F:100") < 0)
						quit(1, "%s: %s() failed to set BOARD%d fan speed",
								bitfury->drv->name, __func__, board_id + 1);

					applog(LOG_INFO, "%s: set BOARD%d fan speed to max speed",
							bitfury->drv->name, board_id + 1);
				}
			}
		}

		/* temp alarm recovery */
		if ((a_temp == false) && (info->a_temp == true)) {
			applog(LOG_ERR, "%s: temperature alarm recovery: [%5.1f]",
					bitfury->drv->name, max_temp);

			/* restore fans speed */
			set_fan_speed(bitfury);

			reinit_x5(info, false);
		}

		/* enable power chain alarm */
		if ((a_ichain == true) && (info->a_ichain == false)) {
			applog(LOG_ERR, "%s: power chain alarm enabled!!!",
					bitfury->drv->name);
			info->ialarm_start = time(NULL);
		}

		/* power chain alarm recovery */
		if ((a_ichain == false) && (info->a_ichain == true)) {
			if (info->ialarm_count > 1) {
				applog(LOG_ERR, "%s: power chain alarm recovery",
						bitfury->drv->name);

				info->ialarm_count  = 1;
				info->ialarm_buzzer = false;
				info->ialarm_start  = time(NULL);

				reinit_x5(info, false);
			}
		}

		info->a_temp   = a_temp;
		info->a_ichain = a_ichain;

		if (info->a_temp == true)
			applog(LOG_ERR, "%s: ALARM: board temp: [%5.1f] alarm temp: [%5.1f]",
					bitfury->drv->name, max_temp, t_alarm);

		if (info->a_ichain == true) {
			curr_time = time(NULL);

			if (curr_time - info->ialarm_start >= info->ialarm_count * ICHAIN_ALARM_INTERVAL) {
				info->ialarm_count *= 2;
				info->ialarm_start = curr_time;

				/* enable power chain */
				for (board_id = 0; board_id < CHIPBOARD_NUM; board_id++) {
					enable_power_chain(bitfury, board_id, 0);
#ifdef MINER_X5
					info->chipboard[board_id].power_enable_time = curr_time;
#endif
#ifdef MINER_X6
					info->chipboard[board_id].power1_enable_time = curr_time;
					info->chipboard[board_id].power2_enable_time = curr_time;
#endif
				}
			}
		} else if (opt_bf16_power_management_disabled == false) {
			if (info->a_net == true) {
				/* disable power chain if no internet available */
				for (board_id = 0; board_id < CHIPBOARD_NUM; board_id++) {
					if (info->chipboard[board_id].detected == true) {
#ifdef MINER_X5
						if (info->chipboard[board_id].p_chain1_enabled == 1) {
#endif
#ifdef MINER_X6
						if ((info->chipboard[board_id].p_chain1_enabled == 1) ||
							(info->chipboard[board_id].p_chain2_enabled == 1)) {
#endif
							disable_power_chain(bitfury, board_id, 0);
						}
					}
				}
			} else {
				/* enable power chain - internet recovery */
				bool recovery = false;
				for (board_id = 0; board_id < CHIPBOARD_NUM; board_id++) {
					if (info->chipboard[board_id].detected == true) {
#ifdef MINER_X5
						if ((info->chipboard[board_id].p_chain1_enabled == 0) &&
							(info->chipboard[board_id].power_disabled == false)) {
							enable_power_chain(bitfury, board_id, 0);
							info->chipboard[board_id].power_enable_time = time(NULL);

							/* wait for power chain to enable */
							cgsleep_us(POWER_WAIT_INTERVAL);

							reinit_x5(info, false);
						}
#endif

#ifdef MINER_X6
						if ((info->chipboard[board_id].p_chain1_enabled == 0) &&
							(info->chipboard[board_id].power1_disabled == false)){
							enable_power_chain(bitfury, board_id, 1);
							info->chipboard[board_id].power1_enable_time = time(NULL);
							recovery = true;
						}

						if ((info->chipboard[board_id].p_chain2_enabled == 0) &&
							(info->chipboard[board_id].power2_disabled == false)) {
							enable_power_chain(bitfury, board_id, 2);
							info->chipboard[board_id].power2_enable_time = time(NULL);
							recovery = true;
						}
#endif
					}
				}

				/* reinit chips on alarm recovery */
				if (recovery == true) {
					/* wait for power chain to enable */
					cgsleep_us(POWER_WAIT_INTERVAL);

					reinit_x5(info, false);
				}
			}
		}

		/* board temperature regulation */
		char uart_cmd[8];
		if (manual_pid_enabled == false) {
			sprintf(uart_cmd, "N:%d", (int)max_temp);
			for (board_id = 0; board_id < CHIPBOARD_NUM; board_id++) {
				if (info->chipboard[board_id].detected == true) {
					if (device_uart_transfer(board_id + 1, uart_cmd) < 0)
						quit(1, "%s: %s() failed to set BOARD%d next temp",
								bitfury->drv->name, __func__, board_id + 1);

					applog(LOG_INFO, "%s: set BOARD%d next temp to [%5.1f]",
							bitfury->drv->name, board_id + 1, max_temp);
				}
			}
		} else {
			uint8_t max_fan_speed = 0;
			for (board_id = 0; board_id < CHIPBOARD_NUM; board_id++) {
				if (info->chipboard[board_id].detected == true) {
					uint16_t pid_temp = 10 * info->chipboard[board_id].temp;
					if (10 * max_temp > pid_temp)
						pid_temp = 10 * max_temp;

					int16_t fan_speed = update_pid(&info->chipboard[board_id].pid,
							(pid_temp - 10 * info->chipboard[board_id].target_temp) / 10);

					if (fan_speed > 100)
						fan_speed = 100;
					else if (fan_speed < 0)
						fan_speed = 0;

					if (max_fan_speed < fan_speed)
						max_fan_speed = fan_speed;
				}
			}

			sprintf(uart_cmd, "F:%d", max_fan_speed);
			for (board_id = 0; board_id < CHIPBOARD_NUM; board_id++) {
				if (info->chipboard[board_id].detected == true) {
					if (device_uart_transfer(board_id + 1, uart_cmd) < 0)
						quit(1, "%s: %s() failed to set BOARD%d fan speed",
								bitfury->drv->name, __func__, board_id + 1);

					applog(LOG_INFO, "%s: set BOARD%d fan speed to [%d]",
							bitfury->drv->name, board_id + 1, max_fan_speed);
				}
			}
		}

		/* board power management */
		if (opt_bf16_power_management_disabled == false) {
			curr_time = time(NULL);

			for (board_id = 0; board_id < CHIPBOARD_NUM; board_id++) {
				if (info->chipboard[board_id].detected == true) {
#ifdef MINER_X5
					uint8_t disabled_chips = 0;
					uint8_t total_chips  = 0;
#endif

#ifdef MINER_X6
					uint8_t disabled_chips_chain1 = 0;
					uint8_t total_chips_chain1  = 0;
					uint8_t disabled_chips_chain2 = 0;
					uint8_t total_chips_chain2  = 0;
#endif
					for (bcm250_id = 0; bcm250_id < BCM250_NUM; bcm250_id++) {
						uint8_t first_good_chip = info->chipboard[board_id].bcm250[bcm250_id].first_good_chip;
						uint8_t last_good_chip  = info->chipboard[board_id].bcm250[bcm250_id].last_good_chip;

						/* chips loop */
						for (chip_id = first_good_chip; chip_id < last_good_chip; chip_id++) {
#ifdef MINER_X5
							total_chips++;

							if (info->chipboard[board_id].bcm250[bcm250_id].chips[chip_id].status == DISABLED)
								disabled_chips++;
#endif

#ifdef MINER_X6
							if (bcm250_id < BCM250_NUM / 2) {
								total_chips_chain1++;

								if (info->chipboard[board_id].bcm250[bcm250_id].chips[chip_id].status == DISABLED)
									disabled_chips_chain1++;
							}

							if ((bcm250_id >= BCM250_NUM / 2) && (bcm250_id < BCM250_NUM)) {
								total_chips_chain2++;

								if (info->chipboard[board_id].bcm250[bcm250_id].chips[chip_id].status == DISABLED)
									disabled_chips_chain2++;
							}
#endif
						}
					}

#ifdef MINER_X5
					/* disable chain power if all chips failed */
					if ((total_chips == disabled_chips) &&
						(info->chipboard[board_id].p_chain1_enabled == 1)) {
						disable_power_chain(bitfury, board_id, 0);

						/* increase disable counter until we reach long enough work time */
						time_t work_interval = curr_time - info->chipboard[board_id].power_enable_time;
						if (work_interval < CHAIN_WORK_INTERVAL)
							info->chipboard[board_id].power_disable_count *= 2;

						info->chipboard[board_id].power_disabled     = true;
						info->chipboard[board_id].power_disable_time = curr_time;
						info->chipboard[board_id].chips_disabled     = info->chipboard[board_id].chips_num;
					}

					/* enable disabled chain */
					if ((info->chipboard[board_id].p_chain1_enabled == 0) &&
					    (info->chipboard[board_id].power_disabled == true) &&
					    (curr_time - info->chipboard[board_id].power_disable_time >= info->chipboard[board_id].power_disable_count * CHAIN_REENABLE_INTERVAL)) {
						time_t disable_interval = curr_time - info->chipboard[board_id].power_disable_time;
						info->chipboard[board_id].power_disable_time = curr_time;
						info->chipboard[board_id].chips_disabled     = 0;

						enable_power_chain(bitfury, board_id, 0);
						info->chipboard[board_id].power_enable_time = curr_time;
						info->chipboard[board_id].power_disabled    = false;

						/* wait for power chain to enable */
						cgsleep_us(POWER_WAIT_INTERVAL);

						reinit_x5(info, true);
						applog(LOG_NOTICE, "%s: reenabled power on BOARD%d: time interval [%d]",
								bitfury->drv->name,
								board_id + 1, (int)disable_interval);
#ifdef FILELOG
						filelog(info, "%s: reenabled power on BOARD%d: time interval [%d]",
								bitfury->drv->name,
								board_id + 1, (int)disable_interval);
#endif
					}
#endif

#ifdef MINER_X6
					/* disable power on chain 1 only if chips on both chains failed
					 * as chain 2 data channel depends on chain 1                    */
					if ((total_chips_chain1 == disabled_chips_chain1) &&
						(info->chipboard[board_id].p_chain1_enabled == 1) &&
						(info->chipboard[board_id].power2_disabled == true)) {
						disable_power_chain(bitfury, board_id, 0);

						/* increase disable counter until we reach long enough work time */
						time_t work_interval = curr_time - info->chipboard[board_id].power1_enable_time;
						if (work_interval < CHAIN_WORK_INTERVAL)
							info->chipboard[board_id].power1_disable_count *= 2;

						info->chipboard[board_id].power1_disabled     = true;
						info->chipboard[board_id].power2_disabled     = true;
						info->chipboard[board_id].power1_disable_time = curr_time;
						info->chipboard[board_id].power2_disable_time = curr_time;
						info->chipboard[board_id].chips_disabled      = info->chipboard[board_id].chips_num;
					} else
					if ((total_chips_chain2 == disabled_chips_chain2) &&
						(info->chipboard[board_id].p_chain2_enabled == 1)) {
						disable_power_chain(bitfury, board_id, 2);
						
						/* increase disable counter until we reach long enough work time */
						time_t work_interval = curr_time - info->chipboard[board_id].power2_enable_time;
						if (work_interval < CHAIN_WORK_INTERVAL)
							info->chipboard[board_id].power2_disable_count *= 2;

						info->chipboard[board_id].power2_disabled     = true;
						info->chipboard[board_id].power2_disable_time = curr_time;
						info->chipboard[board_id].chips_disabled      = info->chipboard[board_id].chips_num / 2;
					}

					/* HW FIX: try to reenable disabled chain */
					if ((info->chipboard[board_id].p_chain1_enabled == 0) &&
					    (info->chipboard[board_id].power1_disabled == true) &&
					    (curr_time - info->chipboard[board_id].power1_disable_time >= info->chipboard[board_id].power1_disable_count * CHAIN_REENABLE_INTERVAL)) {
						time_t disable_interval = curr_time - info->chipboard[board_id].power1_disable_time;
						info->chipboard[board_id].power1_disable_time = curr_time;
						info->chipboard[board_id].power2_disable_time = curr_time;
						info->chipboard[board_id].chips_disabled      = 0;

						enable_power_chain(bitfury, board_id, 0);
						info->chipboard[board_id].power1_enable_time = curr_time;
						info->chipboard[board_id].power2_enable_time = curr_time;
						info->chipboard[board_id].power1_disabled    = false;
						info->chipboard[board_id].power2_disabled    = false;

						/* wait for power chain to enable */
						cgsleep_us(POWER_WAIT_INTERVAL);

						reinit_x5(info, true);
						applog(LOG_NOTICE, "%s: reenabled power on BOARD%d: time interval [%d]",
								bitfury->drv->name,
								board_id + 1, (int)disable_interval);
#ifdef FILELOG
						filelog(info, "%s: reenabled power on BOARD%d: time interval [%d]",
								bitfury->drv->name,
								board_id + 1, (int)disable_interval);
#endif

					} else
					if ((info->chipboard[board_id].p_chain2_enabled == 0) &&
					    (info->chipboard[board_id].power2_disabled == true) &&
					    (curr_time - info->chipboard[board_id].power2_disable_time >= info->chipboard[board_id].power2_disable_count * CHAIN_REENABLE_INTERVAL)) {
						time_t disable_interval = curr_time - info->chipboard[board_id].power2_disable_time;
						info->chipboard[board_id].power2_disable_time = curr_time;
						info->chipboard[board_id].chips_disabled      = 0;

						enable_power_chain(bitfury, board_id, 2);
						info->chipboard[board_id].power2_enable_time = curr_time;
						info->chipboard[board_id].power2_disabled    = false;

						/* wait for power chain to enable */
						cgsleep_us(POWER_WAIT_INTERVAL);

						reinit_x5(info, true);
						applog(LOG_NOTICE, "%s: reenabled chainboard 2 on BOARD%d: time interval [%d]",
								bitfury->drv->name,
								board_id + 1, (int)disable_interval);
#ifdef FILELOG
						filelog(info, "%s: reenabled chainboard 2 on BOARD%d: time interval [%d]",
								bitfury->drv->name,
								board_id + 1, (int)disable_interval);
#endif
					}
#endif
					/* switch renonce chip to next board */
					if ((info->renonce_chips == 1) &&
						(renonce_chip_address[board_id].board_id == board_id) &&
#ifdef MINER_X5
						(info->chipboard[board_id].power_disabled == true)) {
#endif
#ifdef MINER_X6
						(info->chipboard[board_id].power1_disabled == true) &&
						(info->chipboard[board_id].power2_disabled == true)) {
#endif
						uint8_t next_board_id  = (board_id + 1) % CHIPBOARD_NUM;
						uint8_t next_bcm250_id = renonce_chip_address[board_id].bcm250_id;
						uint8_t next_chip_id   = renonce_chip_address[board_id].chip_id;

						/* board id should be set last */
						renonce_chip_address[next_board_id].bcm250_id = next_bcm250_id;
						renonce_chip_address[next_board_id].chip_id   = next_chip_id;
						renonce_chip_address[next_board_id].board_id  = next_board_id;

						info->chipboard[next_board_id].bcm250[next_bcm250_id].chips[next_chip_id].status = UNINITIALIZED;

						applog(LOG_NOTICE, "%s: changed renonce chip address to: [%d:%d:%2d]",
								bitfury->drv->name,
								next_board_id, next_bcm250_id, next_chip_id);

						renonce_chip_address[board_id].board_id  = -1;
						renonce_chip_address[board_id].bcm250_id = -1;
						renonce_chip_address[board_id].chip_id   = -1;
					}
				}
			}
		}

		cgsleep_us(HWMONITOR_DELAY);
	}

	applog(LOG_INFO, "%s: hwmonitor_thr: exiting...", bitfury->drv->name);
	return NULL;
}

static void *bitfury_alarm(void *userdata)
{
	struct cgpu_info *bitfury = (struct cgpu_info *)userdata;
	struct bitfury16_info *info = (struct bitfury16_info *)(bitfury->device_data);

	struct pool *pool;
	int i;
	time_t curr_time_t;
	struct timeval curr_time;

	applog(LOG_INFO, "%s: started alarm thread", bitfury->drv->name);

	/* blink lamps once */
	led_red_enable(info);
	led_green_enable(info);

	cgsleep_ms(1000);

	led_red_disable(info);
	led_green_disable(info);
	buzzer_disable(info);

	applog(LOG_INFO, "%s: alarm loop started", bitfury->drv->name);

	while (bitfury->shutdown == false) {
		curr_time_t = time(NULL);
		gettimeofday(&curr_time, NULL);
		/* check if there are enabled pools present */
		bool idle = true;
		for (i = 0; i < total_pools; i++) {
			pool = pools[i];
			if (pool->idle == false)
				idle = false;
		}

		if (idle == true)
			info->a_net = true;
		else
			info->a_net = false;

		/* temperature alarm processing */
		if ((info->a_temp == true) && (info->a_ichain == false)) {
			if (timediff_us(info->led_red_switch, curr_time) >= LED_RED_INTERVAL) {
				if (info->led_red_enabled == true)
					led_red_disable(info);
				else
					led_red_enable(info);
			}

			/* enable buzzer */
			if (timediff_us(info->buzzer_switch, curr_time) >= BUZZER_INTERVAL) {
				if (info->buzzer_enabled == true)
					buzzer_disable(info);
				else
					buzzer_enable(info);
			}
		} else if ((info->a_ichain == false) && (info->a_net == false)) {
			if (info->led_red_enabled == true)
				led_red_disable(info);

			if (info->buzzer_enabled == true)
				buzzer_disable(info);
		}

		/* power chain alarm processing */
		if ((info->a_ichain == true) && (info->a_temp == false)) {
			if (timediff_us(info->led_red_switch, curr_time) >= LED_RED_INTERVAL) {
				if (info->led_red_enabled == true)
					led_red_disable(info);
				else
					led_red_enable(info);
			}

			if (info->ialarm_buzzer == false) {
				buzzer_enable(info);
				cgsleep_ms(1000);
				buzzer_disable(info);

				info->ialarm_buzzer = true;
			}

			/* enable buzzer */
			if (curr_time_t - info->ialarm_start >= info->ialarm_count * ICHAIN_ALARM_INTERVAL) {
				buzzer_enable(info);
				cgsleep_ms(1000);
				buzzer_disable(info);
			}
		} else if ((info->a_temp == false) && (info->a_net == false)) {
			if (info->led_red_enabled == true)
				led_red_disable(info);

			if (info->buzzer_enabled == true)
				buzzer_disable(info);
		}

		/* blink green lamp if there are enabled pools present and no alarms active */
		if ((info->a_net == false) && (info->a_temp == false) && (info->a_ichain == false)) {
			if (info->led_red_enabled == true)
				led_red_disable(info);

			if (timediff_us(info->led_green_switch, curr_time) >= LED_GREEN_INTERVAL) {
				if (info->led_green_enabled == true)
					led_green_disable(info);
				else
					led_green_enable(info);
			}
		} else {
			if (info->led_green_enabled == true)
				led_green_disable(info);

			if ((info->a_temp == false) && (info->a_ichain == false)) {
				if (timediff_us(info->led_red_switch, curr_time) >= LED_RED_NET_INTERVAL) {
					if (info->led_red_enabled == true)
						led_red_disable(info);
					else
						led_red_enable(info);
				}
			}
		}

		cgsleep_us(ALARM_DELAY);
	}

	led_red_disable(info);
	led_green_disable(info);
	buzzer_disable(info);

	applog(LOG_INFO, "%s: alarm_thr: exiting...", bitfury->drv->name);
	return NULL;
}

static void *bitfury_statistics(void *userdata)
{
	struct cgpu_info *bitfury = (struct cgpu_info *)userdata;
	struct bitfury16_info *info = (struct bitfury16_info *)(bitfury->device_data);

	struct timeval start_time, stop_time;
	uint8_t board_id, bcm250_id, chip_id;

	float u_board;
	float i_total;
	float p_total;
	float u_chip;
	float p_chip;

	time_t time0 = time(NULL);
	time_t time1 = time(NULL);
	time_t time2;

	/* statistics calculation loop */
	while (bitfury->shutdown == false) {
		time2 = time(NULL);
		if (time2 - time1 >= AVG_TIME_DELTA) {
			gettimeofday(&start_time, NULL);

			u_board = 0.0;
			i_total = 0.0;
			p_total = 0.0;
			u_chip  = 0.0;
			p_chip  = 0.0;

			info->chips_failed     = 0;
			info->chips_disabled   = 0;

			for (board_id = 0; board_id < CHIPBOARD_NUM; board_id++) {
				if (info->chipboard[board_id].detected == true) {
					info->chipboard[board_id].chips_failed = 0;
					info->chips_disabled += info->chipboard[board_id].chips_disabled;

					/* concentrator loop */
					for (bcm250_id = 0; bcm250_id < BCM250_NUM; bcm250_id++) {
						uint8_t first_good_chip = info->chipboard[board_id].bcm250[bcm250_id].first_good_chip;
						uint8_t last_good_chip  = info->chipboard[board_id].bcm250[bcm250_id].last_good_chip;

						info->chipboard[board_id].bcm250[bcm250_id].chips_failed = 0;

						/* chips loop */
						for (chip_id = first_good_chip; chip_id < last_good_chip; chip_id++) {

							if (info->chipboard[board_id].bcm250[bcm250_id].chips[chip_id].status >= FAILING) {
								info->chipboard[board_id].bcm250[bcm250_id].chips_failed++;
								info->chipboard[board_id].chips_failed++;
								info->chips_failed++;
							}

							if (opt_bf16_stats_enabled) {
								get_average(&info->chipboard[board_id].bcm250[bcm250_id].chips[chip_id].task_switch,
										   (float)info->chipboard[board_id].bcm250[bcm250_id].chips[chip_id].task_switch_dx,
										   (float)(time2 - time1), AVG_TIME_INTERVAL);
								get_average(&info->chipboard[board_id].bcm250[bcm250_id].chips[chip_id].status_cmd,
										   (float)info->chipboard[board_id].bcm250[bcm250_id].chips[chip_id].status_cmd_dx,
										   (float)(time2 - time1), AVG_TIME_INTERVAL);
								get_average(&info->chipboard[board_id].bcm250[bcm250_id].chips[chip_id].status_cmd_none,
										   (float)info->chipboard[board_id].bcm250[bcm250_id].chips[chip_id].status_cmd_none_dx,
										   (float)(time2 - time1), AVG_TIME_INTERVAL);

								get_average(&info->chipboard[board_id].bcm250[bcm250_id].chips[chip_id].nonces,
										   (float)info->chipboard[board_id].bcm250[bcm250_id].chips[chip_id].nonces_dx,
										   (float)(time2 - time1), AVG_TIME_INTERVAL);

								get_average(&info->chipboard[board_id].bcm250[bcm250_id].chips[chip_id].hashrate,
										   (float)info->chipboard[board_id].bcm250[bcm250_id].chips[chip_id].nonces_dx,
										   (float)(time2 - time1), AVG_TIME_INTERVAL);
								get_average(&info->chipboard[board_id].bcm250[bcm250_id].chips[chip_id].hashrate_good,
										   (float)info->chipboard[board_id].bcm250[bcm250_id].chips[chip_id].nonces_good_dx,
										   (float)(time2 - time1), AVG_TIME_INTERVAL);
								get_average(&info->chipboard[board_id].bcm250[bcm250_id].chips[chip_id].hashrate_diff,
										   (float)info->chipboard[board_id].bcm250[bcm250_id].chips[chip_id].nonces_diff_dx,
										   (float)(time2 - time1), AVG_TIME_INTERVAL);
								get_average(&info->chipboard[board_id].bcm250[bcm250_id].chips[chip_id].hashrate_bad,
										   (float)info->chipboard[board_id].bcm250[bcm250_id].chips[chip_id].nonces_bad_dx,
										   (float)(time2 - time1), AVG_TIME_INTERVAL);

								if (opt_bf16_renonce != RENONCE_DISABLED) {
									get_average(&info->chipboard[board_id].bcm250[bcm250_id].chips[chip_id].hashrate_re,
											   (float)info->chipboard[board_id].bcm250[bcm250_id].chips[chip_id].nonces_re_dx,
											   (float)(time2 - time1), AVG_TIME_INTERVAL);
									get_average(&info->chipboard[board_id].bcm250[bcm250_id].chips[chip_id].hashrate_re_good,
											   (float)info->chipboard[board_id].bcm250[bcm250_id].chips[chip_id].nonces_re_good_dx,
											   (float)(time2 - time1), AVG_TIME_INTERVAL);
									get_average(&info->chipboard[board_id].bcm250[bcm250_id].chips[chip_id].hashrate_re_bad,
											   (float)info->chipboard[board_id].bcm250[bcm250_id].chips[chip_id].nonces_re_bad_dx,
											   (float)(time2 - time1), AVG_TIME_INTERVAL);
								}
							}

							info->chipboard[board_id].bcm250[bcm250_id].chips[chip_id].task_switch_dx     = 0;
							info->chipboard[board_id].bcm250[bcm250_id].chips[chip_id].status_cmd_dx      = 0;
							info->chipboard[board_id].bcm250[bcm250_id].chips[chip_id].status_cmd_none_dx = 0;

							info->chipboard[board_id].bcm250[bcm250_id].chips[chip_id].nonces_dx      = 0;
							info->chipboard[board_id].bcm250[bcm250_id].chips[chip_id].nonces_good_dx = 0;
							info->chipboard[board_id].bcm250[bcm250_id].chips[chip_id].nonces_diff_dx = 0;
							info->chipboard[board_id].bcm250[bcm250_id].chips[chip_id].nonces_bad_dx  = 0;

							if (opt_bf16_renonce != RENONCE_DISABLED) {
								info->chipboard[board_id].bcm250[bcm250_id].chips[chip_id].nonces_re_dx      = 0;
								info->chipboard[board_id].bcm250[bcm250_id].chips[chip_id].nonces_re_good_dx = 0;
								info->chipboard[board_id].bcm250[bcm250_id].chips[chip_id].nonces_re_bad_dx  = 0;
							}

							if (opt_bf16_stats_enabled) {
								if (opt_bf16_renonce != RENONCE_DISABLED)
									applog(LOG_NOTICE, "STATS: chp [%d:%d:%2d], tsk/s [%4.0f], "
											"ncs/s [%4.0f], sts/s [%3.0f], none/s [%3.0f], hr/s [%8.3f] hr+/s [%8.3f] rhr/s [%8.3f]",
											board_id, bcm250_id, chip_id,
											info->chipboard[board_id].bcm250[bcm250_id].chips[chip_id].task_switch,
											info->chipboard[board_id].bcm250[bcm250_id].chips[chip_id].nonces,
											info->chipboard[board_id].bcm250[bcm250_id].chips[chip_id].status_cmd,
											info->chipboard[board_id].bcm250[bcm250_id].chips[chip_id].status_cmd_none,
											CHIP_COEFF * info->chipboard[board_id].bcm250[bcm250_id].chips[chip_id].hashrate,
											CHIP_COEFF * info->chipboard[board_id].bcm250[bcm250_id].chips[chip_id].hashrate_good,
											CHIP_COEFF * (info->chipboard[board_id].bcm250[bcm250_id].chips[chip_id].hashrate_good +
											              info->chipboard[board_id].bcm250[bcm250_id].chips[chip_id].hashrate_re_good));
								else
									applog(LOG_NOTICE, "STATS: chp [%d:%d:%2d], tsk/s [%4.0f], "
											"ncs/s [%3.0f], sts/s [%3.0f], none/s [%3.0f], hr/s [%8.3f] hr+/s [%8.3f]",
											board_id, bcm250_id, chip_id,
											info->chipboard[board_id].bcm250[bcm250_id].chips[chip_id].task_switch,
											info->chipboard[board_id].bcm250[bcm250_id].chips[chip_id].nonces,
											info->chipboard[board_id].bcm250[bcm250_id].chips[chip_id].status_cmd,
											info->chipboard[board_id].bcm250[bcm250_id].chips[chip_id].status_cmd_none,
											CHIP_COEFF * info->chipboard[board_id].bcm250[bcm250_id].chips[chip_id].hashrate,
											CHIP_COEFF * info->chipboard[board_id].bcm250[bcm250_id].chips[chip_id].hashrate_good);
							}
						}

						if (opt_bf16_stats_enabled) {
							get_average(&info->chipboard[board_id].bcm250[bcm250_id].task_switch,
									   (float)info->chipboard[board_id].bcm250[bcm250_id].task_switch_dx,
									   (float)(time2 - time1), AVG_TIME_INTERVAL);
							get_average(&info->chipboard[board_id].bcm250[bcm250_id].status_cmd,
									   (float)info->chipboard[board_id].bcm250[bcm250_id].status_cmd_dx,
									   (float)(time2 - time1), AVG_TIME_INTERVAL);
							get_average(&info->chipboard[board_id].bcm250[bcm250_id].status_cmd_none,
									   (float)info->chipboard[board_id].bcm250[bcm250_id].status_cmd_none_dx,
									   (float)(time2 - time1), AVG_TIME_INTERVAL);

							get_average(&info->chipboard[board_id].bcm250[bcm250_id].nonces,
									   (float)info->chipboard[board_id].bcm250[bcm250_id].nonces_dx,
									   (float)(time2 - time1), AVG_TIME_INTERVAL);

							get_average(&info->chipboard[board_id].bcm250[bcm250_id].hashrate,
									   (float)info->chipboard[board_id].bcm250[bcm250_id].nonces_dx,
									   (float)(time2 - time1), AVG_TIME_INTERVAL);
							get_average(&info->chipboard[board_id].bcm250[bcm250_id].hashrate_good,
									   (float)info->chipboard[board_id].bcm250[bcm250_id].nonces_good_dx,
									   (float)(time2 - time1), AVG_TIME_INTERVAL);
							get_average(&info->chipboard[board_id].bcm250[bcm250_id].hashrate_diff,
									   (float)info->chipboard[board_id].bcm250[bcm250_id].nonces_diff_dx,
									   (float)(time2 - time1), AVG_TIME_INTERVAL);
							get_average(&info->chipboard[board_id].bcm250[bcm250_id].hashrate_bad,
									   (float)info->chipboard[board_id].bcm250[bcm250_id].nonces_bad_dx,
									   (float)(time2 - time1), AVG_TIME_INTERVAL);

							if (opt_bf16_renonce != RENONCE_DISABLED) {
								get_average(&info->chipboard[board_id].bcm250[bcm250_id].hashrate_re,
										   (float)info->chipboard[board_id].bcm250[bcm250_id].nonces_re_dx,
										   (float)(time2 - time1), AVG_TIME_INTERVAL);
								get_average(&info->chipboard[board_id].bcm250[bcm250_id].hashrate_re_good,
										   (float)info->chipboard[board_id].bcm250[bcm250_id].nonces_re_good_dx,
										   (float)(time2 - time1), AVG_TIME_INTERVAL);
								get_average(&info->chipboard[board_id].bcm250[bcm250_id].hashrate_re_bad,
										   (float)info->chipboard[board_id].bcm250[bcm250_id].nonces_re_bad_dx,
										   (float)(time2 - time1), AVG_TIME_INTERVAL);
							}
						}

						info->chipboard[board_id].bcm250[bcm250_id].task_switch_dx     = 0;
						info->chipboard[board_id].bcm250[bcm250_id].status_cmd_dx      = 0;
						info->chipboard[board_id].bcm250[bcm250_id].status_cmd_none_dx = 0;

						info->chipboard[board_id].bcm250[bcm250_id].nonces_dx      = 0;
						info->chipboard[board_id].bcm250[bcm250_id].nonces_good_dx = 0;
						info->chipboard[board_id].bcm250[bcm250_id].nonces_diff_dx = 0;
						info->chipboard[board_id].bcm250[bcm250_id].nonces_bad_dx  = 0;

						if (opt_bf16_renonce != RENONCE_DISABLED) {
							info->chipboard[board_id].bcm250[bcm250_id].nonces_re_dx      = 0;
							info->chipboard[board_id].bcm250[bcm250_id].nonces_re_good_dx = 0;
							info->chipboard[board_id].bcm250[bcm250_id].nonces_re_bad_dx  = 0;
						}
					}

					if (opt_bf16_stats_enabled) {
						get_average(&info->chipboard[board_id].task_switch,
								   (float)info->chipboard[board_id].task_switch_dx,
								   (float)(time2 - time1), AVG_TIME_INTERVAL);
						get_average(&info->chipboard[board_id].status_cmd,
								   (float)info->chipboard[board_id].status_cmd_dx,
								   (float)(time2 - time1), AVG_TIME_INTERVAL);
						get_average(&info->chipboard[board_id].status_cmd_none,
								   (float)info->chipboard[board_id].status_cmd_none_dx,
								   (float)(time2 - time1), AVG_TIME_INTERVAL);

						get_average(&info->chipboard[board_id].nonces,
								   (float)info->chipboard[board_id].nonces_dx,
								   (float)(time2 - time1), AVG_TIME_INTERVAL);
					}

					get_average(&info->chipboard[board_id].hashrate,
							   (float)info->chipboard[board_id].nonces_dx,
							   (float)(time2 - time1), AVG_TIME_INTERVAL);
					get_average(&info->chipboard[board_id].hashrate_good,
							   (float)info->chipboard[board_id].nonces_good_dx,
							   (float)(time2 - time1), AVG_TIME_INTERVAL);
					get_average(&info->chipboard[board_id].hashrate_diff,
							   (float)info->chipboard[board_id].nonces_diff_dx,
							   (float)(time2 - time1), AVG_TIME_INTERVAL);
					get_average(&info->chipboard[board_id].hashrate_bad,
							   (float)info->chipboard[board_id].nonces_bad_dx,
							   (float)(time2 - time1), AVG_TIME_INTERVAL);

					info->chipboard[board_id].task_switch_dx     = 0;
					info->chipboard[board_id].status_cmd_dx      = 0;
					info->chipboard[board_id].status_cmd_none_dx = 0;

					info->chipboard[board_id].nonces_dx         = 0;
					info->chipboard[board_id].nonces_good_dx    = 0;
					info->chipboard[board_id].nonces_diff_dx    = 0;
					info->chipboard[board_id].nonces_bad_dx     = 0;

					u_board        += (info->chipboard[board_id].u_board + U_LOSS);
#ifdef MINER_X5
					i_total        += (info->chipboard[board_id].i_chain1);
					u_chip         += (info->chipboard[board_id].u_chain1);
					p_chip         += (info->chipboard[board_id].p_chain1);
#endif

#ifdef MINER_X6
					i_total        += (info->chipboard[board_id].i_chain1 + info->chipboard[board_id].i_chain2);
					u_chip         += (info->chipboard[board_id].u_chain1 + info->chipboard[board_id].u_chain2);
					p_chip         += (info->chipboard[board_id].p_chain1 + info->chipboard[board_id].p_chain2);
#endif
					p_total        +=  info->chipboard[board_id].p_board;

					if (opt_bf16_renonce != RENONCE_DISABLED) {
						get_average(&info->chipboard[board_id].hashrate_re,
								   (float)info->chipboard[board_id].nonces_re_dx,
								   (float)(time2 - time1), AVG_TIME_INTERVAL);
						get_average(&info->chipboard[board_id].hashrate_re_good,
								   (float)info->chipboard[board_id].nonces_re_good_dx,
								   (float)(time2 - time1), AVG_TIME_INTERVAL);
						get_average(&info->chipboard[board_id].hashrate_re_bad,
								   (float)info->chipboard[board_id].nonces_re_bad_dx,
								   (float)(time2 - time1), AVG_TIME_INTERVAL);

						info->chipboard[board_id].nonces_re_dx      = 0;
						info->chipboard[board_id].nonces_re_good_dx = 0;
						info->chipboard[board_id].nonces_re_bad_dx  = 0;
					}

					get_average(&info->chipboard[board_id].txrx_speed,
							   (float)info->chipboard[board_id].bytes_transmitted_dx,
							   (float)(time2 - time1), AVG_TIME_INTERVAL);

					info->chipboard[board_id].bytes_transmitted_dx = 0;

					if (opt_bf16_stats_enabled) {
						if (opt_bf16_renonce != RENONCE_DISABLED) {
							applog(LOG_NOTICE, "STATS: board [%d] hrate: good: [%8.3f] re_good: [%8.3f] => "
									"[%8.3f] toren: [%8.3f] bad: [%8.3f] total: [%8.3f]",
									board_id,
									CHIP_COEFF * (info->chipboard[board_id].hashrate_good),
									CHIP_COEFF * (info->chipboard[board_id].hashrate_re_good),
									CHIP_COEFF * (info->chipboard[board_id].hashrate_good + info->chipboard[board_id].hashrate_re_good),
									CHIP_COEFF * (info->chipboard[board_id].hashrate - info->chipboard[board_id].hashrate_good),
									CHIP_COEFF * (info->chipboard[board_id].hashrate_bad),
									CHIP_COEFF * (info->chipboard[board_id].hashrate));
						} else {
							applog(LOG_NOTICE, "STATS: board [%d] hrate: good: [%8.3f] "
									"bad: [%8.3f] total: [%8.3f]",
									board_id,
									CHIP_COEFF * (info->chipboard[board_id].hashrate_good),
									CHIP_COEFF * (info->chipboard[board_id].hashrate_bad),
									CHIP_COEFF * (info->chipboard[board_id].hashrate));
						}

#ifdef MINER_X5
						applog(LOG_NOTICE, "STATS: TX/RX: [%5.3f] Mbit/s UB: [%3.1f] "
								"U1: [%3.1f] I1: [%3.1f] T: [%3.1f] RPM: [%4d] FAN: [%3d]",
								8.0 * info->chipboard[board_id].txrx_speed / 1000000,
								info->chipboard[board_id].u_board + U_LOSS,
								info->chipboard[board_id].u_chain1,
								info->chipboard[board_id].i_chain1,
								info->chipboard[board_id].temp,
								info->chipboard[board_id].rpm,
								info->chipboard[board_id].fan_speed);
#endif

#ifdef MINER_X6
						applog(LOG_NOTICE, "STATS: TX/RX: [%5.3f] Mbit/s UB: [%3.1f] "
								"U1: [%3.1f] U2: [%3.1f] I1: [%3.1f] I2: [%3.1f] T: [%3.1f] RPM: [%4d] FAN: [%3d]",
								8.0 * info->chipboard[board_id].txrx_speed / 1000000,
								info->chipboard[board_id].u_board + U_LOSS,
								info->chipboard[board_id].u_chain1,
								info->chipboard[board_id].u_chain2,
								info->chipboard[board_id].i_chain1,
								info->chipboard[board_id].i_chain2,
								info->chipboard[board_id].temp,
								info->chipboard[board_id].rpm,
								info->chipboard[board_id].fan_speed);
#endif

						applog(LOG_NOTICE, "STATS: ver: [%d] fw: [%d] hwid: [%s]",
								info->chipboard[board_id].board_ver,
								info->chipboard[board_id].board_fwver,
								info->chipboard[board_id].board_hwid);
					}
				}
			}

			if (opt_bf16_stats_enabled) {
				get_average(&info->task_switch,
						   (float)info->task_switch_dx,
						   (float)(time2 - time1), AVG_TIME_INTERVAL);
				get_average(&info->status_cmd,
						   (float)info->status_cmd_dx,
						   (float)(time2 - time1), AVG_TIME_INTERVAL);
				get_average(&info->status_cmd_none,
						   (float)info->status_cmd_none_dx,
						   (float)(time2 - time1), AVG_TIME_INTERVAL);

				get_average(&info->chipboard[board_id].nonces,
						   (float)info->chipboard[board_id].nonces_dx,
						   (float)(time2 - time1), AVG_TIME_INTERVAL);
			}

			get_average(&info->hashrate,
					   (float)info->nonces_dx,
					   (float)(time2 - time1), AVG_TIME_INTERVAL);
			get_average(&info->hashrate_good,
					   (float)info->nonces_good_dx,
					   (float)(time2 - time1), AVG_TIME_INTERVAL);
			get_average(&info->hashrate_diff,
					   (float)info->nonces_diff_dx,
					   (float)(time2 - time1), AVG_TIME_INTERVAL);
			get_average(&info->hashrate_bad,
					   (float)info->nonces_bad_dx,
					   (float)(time2 - time1), AVG_TIME_INTERVAL);

			info->task_switch_dx     = 0;
			info->status_cmd_dx      = 0;
			info->status_cmd_none_dx = 0;

			info->nonces_dx         = 0;
			info->nonces_good_dx    = 0;
			info->nonces_diff_dx    = 0;
			info->nonces_bad_dx     = 0;

			if (opt_bf16_renonce != RENONCE_DISABLED) {
				get_average(&info->hashrate_re,
						   (float)info->nonces_re_dx,
						   (float)(time2 - time1), AVG_TIME_INTERVAL);
				get_average(&info->hashrate_re_good,
						   (float)info->nonces_re_good_dx,
						   (float)(time2 - time1), AVG_TIME_INTERVAL);
				get_average(&info->hashrate_re_bad,
						   (float)info->nonces_re_bad_dx,
						   (float)(time2 - time1), AVG_TIME_INTERVAL);

				info->nonces_re_dx      = 0;
				info->nonces_re_good_dx = 0;
				info->nonces_re_bad_dx  = 0;
			}

			time1 = time2;

			gettimeofday(&stop_time, NULL);

			uint32_t uptime = time2 - time0;
			uint32_t hours  = uptime / 3600;
			uint8_t minutes = (uptime - hours * 3600) / 60;
			uint8_t seconds = uptime - hours * 3600 - minutes * 60;

			info->u_avg   = u_board / info->active_chipboard_num;
			info->i_total = i_total;
			info->p_total = p_total;
			info->u_chip  = u_chip;
			info->p_chip  = p_chip;

			if (opt_bf16_stats_enabled) {
				if (opt_bf16_renonce != RENONCE_DISABLED) {
					applog(LOG_NOTICE, "STATS: rencs: [%d] stale: [%d] nws: [%d] rnws: [%d] failed: [%d]",
							info->renonce_list->count,
							info->stale_work_list->count,
							info->noncework_list->count,
							info->renoncework_list->count,
							info->chips_failed);

					applog(LOG_NOTICE, "STATS: %4.0fGH/s osc: 0x%02x re_osc: 0x%02x "
							"%3.1fV %3.1fA %4.1fW %.3fW/GH",
							CHIP_COEFF * (info->hashrate_good + info->hashrate_re_good),
							bf16_chip_clock,
							bf16_renonce_chip_clock,
							info->u_avg,
							info->i_total,
							info->p_total,
							(info->a_net == true) ?
								0.0 :
								info->p_total / (CHIP_COEFF * (info->hashrate_good + info->hashrate_re_good)));

					applog(LOG_NOTICE, "STATS: TOTAL: %5.1fGH/s %5.3fV %3.1fA %5.2fW "
							"CHIP: %5.1fGH/s GOOD: %4.1fGH/s RENONCE: %4.1fGH/s BAD: %4.1fGH/s",
							CHIP_COEFF * info->hashrate / (info->chips_num - info->renonce_chips - info->chips_failed),
							info->u_chip / (info->chips_num - info->chips_disabled),
							(info->a_net == true) ? 0.0 : info->p_chip / info->u_chip,
							info->p_chip / (info->chips_num - info->chips_disabled),
							CHIP_COEFF * (info->hashrate_good + info->hashrate_re_good) / (info->chips_num - info->renonce_chips - info->chips_failed),
							CHIP_COEFF * info->hashrate_good / (info->chips_num - info->renonce_chips - info->chips_failed),
							CHIP_COEFF * info->hashrate_re_good / (info->chips_num - info->renonce_chips - info->chips_failed),
							CHIP_COEFF * info->hashrate_re_bad / (info->chips_num - info->renonce_chips - info->chips_failed));

#if 1
					applog(LOG_NOTICE, "STATS: m0: [%lu] mm0: [%lu] m1: [%lu] mm1: [%lu] m2: [%lu] mm2: [%lu] "
							"m3: [%lu] mm3: [%lu] um: [%lu]",
							info->stage0_match / (time2 - time0),
							info->stage0_mismatch / (time2 - time0),
							info->stage1_match / (time2 - time0),
							info->stage1_mismatch / (time2 - time0),
							info->stage2_match / (time2 - time0),
							info->stage2_mismatch / (time2 - time0),
							info->stage3_match / (time2 - time0),
							info->stage3_mismatch / (time2 - time0),
							info->unmatched / (time2 - time0));
#endif
				} else {
					applog(LOG_NOTICE, "STATS: stale: [%d] nws: [%d]",
							info->stale_work_list->count, info->noncework_list->count);

					applog(LOG_NOTICE, "STATS: %4.0fGH/s osc: 0x%02x"
							"%3.1fV %3.1fA %4.1fW %.3fW/GH",
							CHIP_COEFF * info->hashrate_good,
							bf16_chip_clock,
							info->u_avg,
							info->i_total,
							info->p_total,
							(info->a_net == true) ?
								0.0 :
								info->p_total / (CHIP_COEFF * info->hashrate_good));

					applog(LOG_NOTICE, "STATS: TOTAL: %5.1fGH/s %5.3fV %3.1fA %5.2fW "
							"CHIP: %5.1fGH/s GOOD: %4.1fGH/s BAD: %4.1fGH/s",
							CHIP_COEFF * info->hashrate / (info->chips_num - info->chips_failed),
							info->u_chip / (info->chips_num - info->chips_disabled),
							(info->a_net == true) ? 0.0 : info->p_chip / info->u_chip,
							info->p_chip / (info->chips_num - info->chips_disabled),
							CHIP_COEFF * info->hashrate_good / (info->chips_num - info->chips_failed),
							CHIP_COEFF * info->hashrate_good / (info->chips_num - info->chips_failed),
							CHIP_COEFF * info->hashrate_bad / (info->chips_num - info->chips_failed));
				}

				applog(LOG_NOTICE, "STATS: uptime: [%4d:%02d:%02d] time elapsed: [%.6f] ",
						hours, minutes, seconds, timediff(start_time, stop_time));
			}
		}

		cgsleep_us(STATISTICS_DELAY);
	}

	applog(LOG_INFO, "%s: statistics_thr: exiting...", bitfury->drv->name);
	return NULL;
}

static bool bitfury16_thread_prepare(struct thr_info *thr)
{
	struct cgpu_info *bitfury = thr->cgpu;
	struct bitfury16_info *info = (struct bitfury16_info *)(bitfury->device_data);

	info->thr = thr;

	if (thr_info_create(&(info->chipworker_thr), NULL, bitfury_chipworker, (void *)bitfury)) {
		applog(LOG_ERR, "%s: %s() chipworker thread create failed",
				bitfury->drv->name, __func__);
		return false;
	}
	pthread_detach(info->chipworker_thr.pth);

	applog(LOG_INFO, "%s: thread prepare: starting chipworker thread", bitfury->drv->name);

	if (thr_info_create(&(info->nonceworker_thr), NULL, bitfury_nonceworker, (void *)bitfury)) {
		applog(LOG_ERR, "%s: %s() nonceworker thread create failed",
				bitfury->drv->name, __func__);
		return false;
	}
	pthread_detach(info->nonceworker_thr.pth);

	applog(LOG_INFO, "%s: thread prepare: starting nonceworker thread", bitfury->drv->name);

	if (opt_bf16_renonce != RENONCE_DISABLED) {
		if (thr_info_create(&(info->renonceworker_thr), NULL, bitfury_renonceworker, (void *)bitfury)) {
			applog(LOG_ERR, "%s: %s() renonceworker thread create failed",
					bitfury->drv->name, __func__);
			return false;
		}
		pthread_detach(info->renonceworker_thr.pth);

		applog(LOG_INFO, "%s: thread prepare: starting renonceworker thread", bitfury->drv->name);
	}

	if (thr_info_create(&(info->hwmonitor_thr), NULL, bitfury_hwmonitor, (void *)bitfury)) {
		applog(LOG_ERR, "%s: %s() hwmonitor thread create failed",
				bitfury->drv->name, __func__);
		return false;
	}
	pthread_detach(info->hwmonitor_thr.pth);

	applog(LOG_INFO, "%s: thread prepare: starting hwmonitor thread", bitfury->drv->name);

	if (thr_info_create(&(info->alarm_thr), NULL, bitfury_alarm, (void *)bitfury)) {
		applog(LOG_ERR, "%s: %s() alarm thread create failed",
				bitfury->drv->name, __func__);
		return false;
	}
	pthread_detach(info->alarm_thr.pth);

	applog(LOG_INFO, "%s: thread prepare: starting alarm thread", bitfury->drv->name);

	if (thr_info_create(&(info->statistics_thr), NULL, bitfury_statistics, (void *)bitfury)) {
		applog(LOG_ERR, "%s: %s() statistics thread create failed",
				bitfury->drv->name, __func__);
		return false;
	}
	pthread_detach(info->statistics_thr.pth);

	applog(LOG_INFO, "%s: thread prepare: starting statistics thread", bitfury->drv->name);

	return true;
}

static int64_t bitfury16_scanwork(struct thr_info *thr)
{
	struct cgpu_info *bitfury = thr->cgpu;
	struct bitfury16_info *info = bitfury->device_data;
	int64_t hashcount = 0;

	applog(LOG_INFO, "%s: scan work", bitfury->drv->name);

	mutex_lock(&info->nonces_good_lock);
	if (info->nonces_good_cg) {
		hashcount += 0xffffffffull * info->nonces_good_cg;
		info->nonces_good_cg = 0;
	}
	mutex_unlock(&info->nonces_good_lock);

	return hashcount;
}

static void prepare_work(struct cgpu_info *bitfury, struct work *work, bool rolled)
{
	struct bitfury16_info *info = (struct bitfury16_info *)bitfury->device_data;

	bf_workd_t* wdata = cgmalloc(sizeof(bf_workd_t));

	wdata->work = work;
	wdata->rolled = rolled;
	wdata->generated = time(NULL);

	/* generate task payload */
	cg_memcpy(wdata->payload.midstate, work->midstate, 32);
	wdata->payload.m7 = *(uint32_t *)(work->data + 64);
	wdata->payload.ntime = *(uint32_t *)(work->data + 68);
	wdata->payload.nbits = *(uint32_t *)(work->data + 72);

	workd_list_push(info->work_list, wdata);
}

static bool bitfury16_queue_full(struct cgpu_info *bitfury)
{
	struct bitfury16_info *info = (struct bitfury16_info *)bitfury->device_data;
	struct work *work, *usework;
	uint16_t need = 0;
	uint16_t generated = 0;
	uint16_t roll, roll_limit;
	bool rolled;

	if (info->initialised == false) {
		cgsleep_us(30);
		return true;
	}

	L_LOCK(info->work_list);
	uint16_t work_count = info->work_list->count;
	L_UNLOCK(info->work_list);
	if (work_count < WORK_QUEUE_LEN)
		need = WORK_QUEUE_LEN - work_count;
	else
		return true;

	/* Ensure we do enough rolling to reduce CPU
	   but dont roll too much to have them end up stale */
	work = get_queued(bitfury);
	if (work) {
		roll_limit = work->drv_rolllimit;
		roll = 0;

		L_LOCK(info->work_list);
		do {
			if (roll == 0) {
				usework = work;
				rolled = false;
			} else {
				usework = copy_work_noffset(work, roll);
				rolled = true;
			}

			prepare_work(bitfury, usework, rolled);
			generated++;
		} while ((--need > 0) && (++roll <= roll_limit));
		L_UNLOCK(info->work_list);

		applog(LOG_INFO, "%s: queue full: generated %d works", bitfury->drv->name, generated);
	} else
		cgsleep_us(30);

	if (need > 0)
		return false;
	else
		return true;
}

static void bitfury16_flush_work(struct cgpu_info *bitfury)
{
	struct bitfury16_info *info = (struct bitfury16_info *)bitfury->device_data;
	uint16_t flushed = 0;

	if (info->initialised == false)
		return;

	bf_works_t works;

	/* flush work list */
	L_LOCK(info->work_list);
	L_LOCK(info->stale_work_list);
	bf_data_t* wdata = info->work_list->head;
	while (wdata != NULL) {
		workd_list_push(info->stale_work_list, WORKD(wdata));
		workd_list_remove(info->work_list, &works);

		wdata = info->work_list->head;
		flushed++;
	}
	L_UNLOCK(info->stale_work_list);
	L_UNLOCK(info->work_list);

	/* flush nonces list */
	L_LOCK(info->noncework_list);
	bf_data_t* nwdata = info->noncework_list->head;
	while (nwdata != NULL) {
		noncework_list_pop(info->noncework_list);
		nwdata = info->noncework_list->head;
	}
	L_UNLOCK(info->noncework_list);

	if (opt_bf16_renonce != RENONCE_DISABLED) {
		/* flush renonces list */
		L_LOCK(info->renonce_list);
		bf_data_t* rdata = info->renonce_list->head;
		while (rdata != NULL) {
			renonce_list_pop(info->renonce_list);
			rdata = info->renonce_list->head;
		}
		L_UNLOCK(info->renonce_list);

		/* flush renoncework list */
		L_LOCK(info->renoncework_list);
		bf_data_t* rnwdata = info->renoncework_list->head;
		while (rnwdata != NULL) {
			renoncework_list_pop(info->renoncework_list);
			rnwdata = info->renoncework_list->head;
		}
		L_UNLOCK(info->renoncework_list);
	}

	applog(LOG_INFO, "%s: flushed %d works", bitfury->drv->name, flushed);
}

static struct api_data *bitfury16_api_stats(struct cgpu_info *bitfury)
{
	uint8_t board_id, bcm250_id, chip_id;
	struct bitfury16_info *info = bitfury->device_data;
	char data[128];
	char value[128];
	struct api_data *root = NULL;

	applog(LOG_INFO, "%s: API stats", bitfury->drv->name);

	if (info->initialised == false)
		return NULL;

#ifdef MINER_X5
	root = api_add_string(root, "Device name", "Bitfury X5", true);
#endif
#ifdef MINER_X6
	root = api_add_string(root, "Device name", "Bitfury X6", true);
#endif
	root = api_add_uint8(root, "Boards number", &info->chipboard_num, false);
	root = api_add_uint8(root, "Boards detected", &info->chipboard_num, false);
	root = api_add_uint8(root, "Chips number", &info->chips_num, false);

	/* software revision chages according to comments in CHANGELOG */
	root = api_add_string(root, "hwv1", "1", true);
	root = api_add_string(root, "hwv2", "2", true);
	root = api_add_string(root, "hwv3", "11", true);
	root = api_add_string(root, "hwv4", "0", true);
	root = api_add_string(root, "hwv5", "0", true);

	/* U avg */
	sprintf(value, "%.1f", info->u_avg);
	root = api_add_string(root, "U avg", value, true);

	/* I total */
	sprintf(value, "%.1f", info->i_total);
	root = api_add_string(root, "I total", value, true);

	/* P total */
	sprintf(value, "%.1f", info->p_total);
	root = api_add_string(root, "P total", value, true);

	if (opt_bf16_renonce != RENONCE_DISABLED) {
		/* Efficiency */
		sprintf(value, "%.3f",
				(info->a_net == true) ?
					0.0 :
					info->p_total / (CHIP_COEFF * (info->hashrate_good + info->hashrate_re_good)));
		root = api_add_string(root, "Efficiency", value, true);

		/* Chip GHS total avg */
		sprintf(value, "%.1f", CHIP_COEFF * info->hashrate / (info->chips_num - info->renonce_chips - info->chips_failed));
		root = api_add_string(root, "Chip GHS total avg", value, true);

		/* Chip GHS avg */
		sprintf(value, "%.1f", CHIP_COEFF * (info->hashrate_good + info->hashrate_re_good) / (info->chips_num - info->renonce_chips - info->chips_failed));
		root = api_add_string(root, "Chip GHS avg", value, true);

		/* Chip GHS good avg */
		sprintf(value, "%.1f", CHIP_COEFF * info->hashrate_good / (info->chips_num - info->renonce_chips - info->chips_failed));
		root = api_add_string(root, "Chip GHS good avg", value, true);

		/* Chip GHS re avg */
		sprintf(value, "%.1f", CHIP_COEFF * info->hashrate_re_good / (info->chips_num - info->renonce_chips - info->chips_failed));
		root = api_add_string(root, "Chip GHS re avg", value, true);

		/* Chip GHS bad avg */
		sprintf(value, "%.1f", CHIP_COEFF * info->hashrate_re_bad / (info->chips_num - info->renonce_chips - info->chips_failed));
		root = api_add_string(root, "Chip GHS bad avg", value, true);
	} else {
		/* Efficiency */
		sprintf(value, "%.3f",
				(info->a_net == true) ?
					0.0 :
					info->p_total / (CHIP_COEFF * info->hashrate_good));
		root = api_add_string(root, "Efficiency", value, true);

		/* Chip GHS total avg */
		sprintf(value, "%.1f", CHIP_COEFF * info->hashrate / (info->chips_num - info->chips_failed));
		root = api_add_string(root, "Chip GHS total avg", value, true);

		/* Chip GHS avg */
		sprintf(value, "%.1f", CHIP_COEFF * info->hashrate_good / (info->chips_num - info->chips_failed));
		root = api_add_string(root, "Chip GHS avg", value, true);

		/* Chip GHS good avg */
		sprintf(value, "%.1f", CHIP_COEFF * info->hashrate_good / (info->chips_num - info->chips_failed));
		root = api_add_string(root, "Chip GHS good avg", value, true);

		/* Chip GHS re avg */
		sprintf(value, "%.1f", 0.0);
		root = api_add_string(root, "Chip GHS re avg", value, true);

		/* Chip GHS bad avg */
		sprintf(value, "%.1f", CHIP_COEFF * info->hashrate_bad / (info->chips_num - info->chips_failed));
		root = api_add_string(root, "Chip GHS bad avg", value, true);
	}

	/* U Chip avg */
	sprintf(value, "%.3f", info->u_chip / (info->chips_num - info->chips_disabled));
	root = api_add_string(root, "U Chip avg", value, true);

	/* I Chip avg */
	sprintf(value, "%.1f", (info->a_net == true) ? 0.0 : info->p_chip / info->u_chip);
	root = api_add_string(root, "I Chip avg", value, true);

	/* P Chip avg */
	sprintf(value, "%.2f", info->p_chip / (info->chips_num - info->chips_disabled));
	root = api_add_string(root, "P Chip avg", value, true);

	for (board_id = 0; board_id < CHIPBOARD_NUM; board_id++) {
		/* board status */
		sprintf(data, "Board%d detected", board_id);
		if (info->chipboard[board_id].detected == true)
			root = api_add_string(root, data, "1", true);
		else
			root = api_add_string(root, data, "0", true);

		/* number of concentratord on board */
		sprintf(data, "Board%d BTC250", board_id);
		root = api_add_uint8(root, data, &info->chipboard[board_id].bcm250_num, false);

		/* number of chips on board */
		sprintf(data, "Board%d BTC16", board_id);
		root = api_add_uint8(root, data, &info->chipboard[board_id].chips_num, false);

		/* number of good chips on board */
		sprintf(data, "Board%d BTC16 good", board_id);
		uint8_t chips_good = info->chipboard[board_id].chips_num - info->chipboard[board_id].chips_failed;
		root = api_add_uint8(root, data, &chips_good, true);

		sprintf(data, "Board%d Clock", board_id);
		if (info->chipboard[board_id].detected == true)
			sprintf(value, "%d (0x%02x)", bf16_chip_clock, bf16_chip_clock);
		else
			sprintf(value, "%d (0x%02x)", 0, 0);

		root = api_add_string(root, data, value, true);

		sprintf(data, "Board%d GHS av", board_id);
		if (opt_bf16_renonce != RENONCE_DISABLED) {
			sprintf(value, "%.2f",
					CHIP_COEFF * (info->chipboard[board_id].hashrate_good + info->chipboard[board_id].hashrate_re_good));
		} else {
			sprintf(value, "%.2f", CHIP_COEFF * (info->chipboard[board_id].hashrate_good));
		}
		root = api_add_string(root, data, value, true);

		/* number of chips per concentrator */
		uint8_t i = 0;
		memset(value, 0, sizeof(value));

		for (bcm250_id = 0; bcm250_id < BCM250_NUM; bcm250_id++) {
			sprintf(data, "Board%d BTC250_%d BTC16", board_id, bcm250_id);
			root = api_add_uint8(root, data, &info->chipboard[board_id].bcm250[bcm250_id].chips_num, false);

			uint8_t first_good_chip = info->chipboard[board_id].bcm250[bcm250_id].first_good_chip;
			uint8_t last_good_chip  = info->chipboard[board_id].bcm250[bcm250_id].last_good_chip;

			/* chips loop */
			for (chip_id = first_good_chip; chip_id < last_good_chip; chip_id++, i++) {
				bf_chip_address_t chip_address;
				chip_address.board_id  = board_id;
				chip_address.bcm250_id = bcm250_id;
				chip_address.chip_id   = chip_id;

				if (info->chipboard[board_id].bcm250[bcm250_id].chips[chip_id].status < FAILING) {
					if ((opt_bf16_renonce != RENONCE_DISABLED) &&
						(renonce_chip(chip_address) == 1))
						value[i] = 'O';
					else
						value[i] = 'o';
				} else {
					if ((opt_bf16_renonce != RENONCE_DISABLED) &&
						(renonce_chip(chip_address) == 1))
						value[i] = 'X';
					else
						value[i] = 'x';
				}
			}

			if (bcm250_id != BCM250_NUM - 1)
				value[i++] = ' ';
		}

		sprintf(data, "Board%d BF16 Status", board_id);
		root = api_add_string(root, data, value, true);

		/* MSP version data */
		/* board version */
		sprintf(data, "Board%d Ver", board_id);
		root = api_add_uint32(root, data, &info->chipboard[board_id].board_ver, false);

		/* board firmware version */
		sprintf(data, "Board%d FW", board_id);
		root = api_add_uint32(root, data, &info->chipboard[board_id].board_fwver, false);

		/* board hardware id */
		sprintf(data, "Board%d HWID", board_id);
		root = api_add_string(root, data, info->chipboard[board_id].board_hwid, true);

		/* MSP hw data */
		/* T */
		sprintf(data, "Board%d T", board_id);
		sprintf(value, "%.1f", info->chipboard[board_id].temp);
		root = api_add_string(root, data, value, true);

		/* UB */
		sprintf(data, "Board%d UB", board_id);
		sprintf(value, "%.1f", info->chipboard[board_id].u_board);
		root = api_add_string(root, data, value, true);

		/* P1 */
		sprintf(data, "Board%d P1", board_id);
		root = api_add_uint8(root, data, &info->chipboard[board_id].p_chain1_enabled, false);

		/* P2 */
		sprintf(data, "Board%d P2", board_id);
		root = api_add_uint8(root, data, &info->chipboard[board_id].p_chain2_enabled, false);

		/* U1 */
		sprintf(data, "Board%d U1", board_id);
		sprintf(value, "%.1f", info->chipboard[board_id].u_chain1);
		root = api_add_string(root, data, value, true);

		/* U2 */
		sprintf(data, "Board%d U2", board_id);
		sprintf(value, "%.1f", info->chipboard[board_id].u_chain2);
		root = api_add_string(root, data, value, true);

		/* I1 */
		sprintf(data, "Board%d I1", board_id);
		sprintf(value, "%.1f", info->chipboard[board_id].i_chain1);
		root = api_add_string(root, data, value, true);

		/* I2 */
		sprintf(data, "Board%d I2", board_id);
		sprintf(value, "%.1f", info->chipboard[board_id].i_chain2);
		root = api_add_string(root, data, value, true);

		/* RPM */
		sprintf(data, "Board%d PRM", board_id);
		root = api_add_uint32(root, data, &info->chipboard[board_id].rpm, true);

		/* A */
		sprintf(data, "Board%d T_Alarm", board_id);
		root = api_add_uint8(root, data, &info->chipboard[board_id].a_temp, true);

		sprintf(data, "Board%d I1_Alarm", board_id);
		root = api_add_uint8(root, data, &info->chipboard[board_id].a_ichain1, true);

		sprintf(data, "Board%d I2_Alarm", board_id);
		root = api_add_uint8(root, data, &info->chipboard[board_id].a_ichain2, true);

		/* F */
		sprintf(data, "Board%d F", board_id);
		root = api_add_uint8(root, data, &info->chipboard[board_id].fan_speed, false);

		/* AI */
		sprintf(data, "Board%d AI", board_id);
		sprintf(value, "%.1f", info->chipboard[board_id].i_alarm);
		root = api_add_string(root, data, value, true);

		/* AT */
		sprintf(data, "Board%d AT", board_id);
		sprintf(value, "%.0f", info->chipboard[board_id].t_alarm);
		root = api_add_string(root, data, value, true);

		/* AG */
		sprintf(data, "Board%d AG", board_id);
		sprintf(value, "%.0f", info->chipboard[board_id].t_gisteresis);
		root = api_add_string(root, data, value, true);

		/* FM */
		sprintf(data, "Board%d FM", board_id);
#ifdef MINER_X5
		sprintf(value, "%c", info->chipboard[board_id].fan_mode);
#endif

#ifdef MINER_X6
		if (manual_pid_enabled == true)
			sprintf(value, "A");
		else
			sprintf(value, "%c", info->chipboard[board_id].fan_mode);
#endif
		root = api_add_string(root, data, value, true);

		/* TT */
		sprintf(data, "Board%d TT", board_id);
		sprintf(value, "%.0f", info->chipboard[board_id].target_temp);
		root = api_add_string(root, data, value, true);
	}

	return root;
}

static void bitfury16_shutdown(struct thr_info *thr)
{
	struct cgpu_info *bitfury = thr->cgpu;
	struct bitfury16_info *info = (struct bitfury16_info *)bitfury->device_data;
	uint8_t board_id;

	bitfury->shutdown = true;

	cgsleep_ms(300);

	/* flush next board temp to default value */
	if (manual_pid_enabled == false) {
		for (board_id = 0; board_id < CHIPBOARD_NUM; board_id++) {
			if (info->chipboard[board_id].detected == true) {
				if (device_uart_transfer(board_id + 1, "N") < 0)
					quit(1, "%s: %s() failed to set BOARD%d next temp",
							bitfury->drv->name, __func__, board_id + 1);

				applog(LOG_INFO, "%s: set BOARD%d next temp to default value",
						bitfury->drv->name, board_id + 1);
			}
		}
	} else {
		for (board_id = 0; board_id < CHIPBOARD_NUM; board_id++) {
			if (info->chipboard[board_id].detected == true) {
				if (device_uart_transfer(board_id + 1, "F") < 0)
					quit(1, "%s: %s() failed to set BOARD%d fan speed to auto mode",
							bitfury->drv->name, __func__, board_id + 1);

				applog(LOG_INFO, "%s: set BOARD%d fan speed to auto mode",
						bitfury->drv->name, board_id + 1);
			}
		}
	}

	/* disable power chain */
	for (board_id = 0; board_id < CHIPBOARD_NUM; board_id++) {
		disable_power_chain(bitfury, board_id, 0);
	}	

	mutex_destroy(&info->nonces_good_lock);

	/* close devices */
	close_spi_device(SPI_CHANNEL1);
	close_spi_device(SPI_CHANNEL2);
	close_ctrl_device();
	close_uart_device(UART_CHANNEL1);
	close_uart_device(UART_CHANNEL2);

#ifdef FILELOG
	filelog(info, "%s: cgminer stopped", bitfury->drv->name);
	fclose(info->logfile);
	mutex_destroy(&info->logfile_mutex);
#endif

	applog(LOG_INFO, "%s: driver shutdown", bitfury->drv->name);
}

/* Currently hardcoded to BF1 devices */
struct device_drv bitfury16_drv = {
	.drv_id = DRIVER_bitfury16,
	.dname = "bitfury16",
	.name = "BF16",
	.drv_detect = bitfury16_detect,
	.get_api_stats = bitfury16_api_stats,
	.identify_device = bitfury16_identify,
	.thread_prepare = bitfury16_thread_prepare,
	.hash_work = hash_queued_work,
	.scanwork = bitfury16_scanwork,
	.queue_full = bitfury16_queue_full,
	.flush_work = bitfury16_flush_work,
	.thread_shutdown = bitfury16_shutdown
};
