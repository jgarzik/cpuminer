/*
 * cgminer SPI driver for Dragonmint T1 devices
 *
 * Copyright 2013, 2014 Zefir Kurtisi <zefir.kurtisi@gmail.com>
 * Copyright 2018 Con Kolivas <kernel@kolivas.org>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 3 of the License, or (at your option)
 * any later version.  See COPYING for more details.
 */

#include <stdlib.h>
#include <assert.h>
#include <fcntl.h>
#include <limits.h>
#include <unistd.h>
#include <stdbool.h>
#include <pthread.h>

#include "logging.h"
#include "miner.h"
#include "util.h"

#include "dragonmint_t1.h"
#include "dm_temp_ctrl.h"
#include "dm_fan_ctrl.h"

#include "sys/time.h"

#define T1_FANSPEED_INIT	(opt_T1_target)
#define T1_TEMP_TARGET_INIT	(60)
#define T1_TEMP_TARGET_RUN	(75)

struct T1_chain *chain[MAX_CHAIN_NUM];
uint8_t chain_mask;

uint16_t T1Pll[MCOMPAT_CONFIG_MAX_CHAIN_NUM];

/* FAN CTRL */
//dragonmint_fan_temp_s g_fan_ctrl;
static volatile uint8_t g_debug_stats[MAX_CHAIN_NUM];

static int total_chains;
static int chains_tuned;

static dragonmint_reg_ctrl_t s_reg_ctrl;

hardware_version_e g_hwver;
//dragonmint_type_e g_type;
int g_reset_delay = 0xffff;
char volShowLog[MAX_CHAIN_NUM][256];
/* one global board_selector and spi context is enough */
//static struct board_selector *board_selector;

static int spi_status_err_cnt = 0;

static pthread_t fan_tid;

/*
 * for now, we have one global config, defaulting values:
 * - ref_clk 16MHz / sys_clk 800MHz
 * - 2000 kHz SPI clock
 */
struct T1_config_options T1_config_options = {
	.ref_clk_khz = 16000, .sys_clk_khz = 800000, .spi_clk_khz = 2000,
};

/* override values with --bitmine-t1-options ref:sys:spi: - use 0 for default */
static struct T1_config_options *parsed_config_options;

int chain_plug[MAX_CHAIN_NUM];
int chain_flag[MAX_CHAIN_NUM];

#define  LOG_VOL_PREFIX "/tmp/log/volAnalys"
void dragonmint_log_record(int cid, void* log, int len)
{
	FILE* fd;
	char fileName[128] = {0};

	sprintf(fileName, "%s%d.log", LOG_VOL_PREFIX, cid);
	fd = fopen(fileName, "w+");
	if (fd == NULL){
		//applog(LOG_ERR, "Open log File%d Failed!%d", cid, errno);
		applog(LOG_ERR, "Open log File%d Failed!%s", cid, strerror(errno));
		return;
	}

	fwrite(log, len, 1, fd);
	fflush(fd);
	fclose(fd);
}

static void wq_enqueue(struct thr_info *thr, struct T1_chain *t1)
{
	struct work *work = get_work(thr, thr->id);
	struct work_queue *wq;
	struct work_ent *we;
	int rolls = 0;

	wq = &t1->active_wq;

	while (42) {
		we = cgmalloc(sizeof(*we));

		we->work = work;
		INIT_LIST_HEAD(&we->head);

		mutex_lock(&t1->lock);
		list_add_tail(&we->head, &wq->head);
		wq->num_elems++;
		mutex_unlock(&t1->lock);

		if (wq->num_elems >= t1->num_active_chips * 2) {
			break;
		}
		if (rolls > work->drv_rolllimit) {
			work = get_work(thr, thr->id);
			continue;
		}
		work = make_clone(work);
		roll_work(work);
	}
}

static struct work *wq_dequeue(struct T1_chain *t1, bool sig)
{
	struct work_ent *we;
	struct work *work = NULL;
	struct work_queue *wq = &t1->active_wq;

	if (wq == NULL)
		return NULL;

	/* Sleep only a small duration if there is no work queued in case it's
	 * still refilling rather than we have no upstream work. */
	if (unlikely(!wq->num_elems && sig))
		cgsleep_ms(10);

	mutex_lock(&t1->lock);
	if (likely(wq->num_elems > 0)) {
		we = list_entry(wq->head.next, struct work_ent, head);
		work = we->work;

		list_del(&we->head);
		free(we);
		wq->num_elems--;
	}
	if (sig)
		pthread_cond_signal(&t1->cond);
	mutex_unlock(&t1->lock);

	return work;
}

/********** driver interface */
void exit_T1_chain(struct T1_chain *t1)
{
	if (t1 == NULL)
		return;
	free(t1->chips);
	t1->chips = NULL;
	chain[t1->chain_id] = NULL;
	chain_flag[t1->chain_id] = 0;

	mcompat_set_led(t1->chain_id, LED_OFF);
	mcompat_set_power_en(t1->chain_id, 0);

	free(t1);
}

static void get_temperatures(struct T1_chain *t1)
{
	int i;
	int temp[MAX_CHIP_NUM] = {0};

	mcompat_get_chip_temp(t1->chain_id, temp);

	for (i = 0; i < t1->num_active_chips; i++)
		t1->chips[i].temp = temp[i];
}

static void get_voltages(struct T1_chain *t1)
{
	int i;

	//configure for vsensor
	mcompat_configure_tvsensor(t1->chain_id, CMD_ADDR_BROADCAST, 0);
	for (i = 0; i < t1->num_active_chips; i++)
		dragonmint_check_voltage(t1, i + 1, &s_reg_ctrl);

	//configure for tsensor
	mcompat_configure_tvsensor(t1->chain_id, CMD_ADDR_BROADCAST, 1);

	dragonmint_get_voltage_stats(t1, &s_reg_ctrl);
}

static bool prechain_detect(struct T1_chain *t1, int idxpll)
{
	int pll_lv_to_setspi;
	int pll_lv_to_setvid;
	int chain_id = t1->chain_id;

	assert(pll_lv_to_setvid < idxpll);

	cgsleep_us(1000);

	t1->pll = 0;
	t1->base_pll = idxpll;

	if (opt_T1auto) {
		/* Start tuning at a different voltage depending on tuning
		 * strategy. */
		if (opt_T1_performance)
			opt_T1Vol[chain_id] = TUNE_VOLT_START_PER;
		else if (opt_T1_efficient)
			opt_T1Vol[chain_id] = TUNE_VOLT_START_EFF;
		else
			opt_T1Vol[chain_id] = TUNE_VOLT_START_BAL;
	}

	pll_lv_to_setspi = T1_ConfigT1PLLClock(T1_PLL_SETSPI);
	if (!t1_set_pll(t1, CMD_ADDR_BROADCAST, pll_lv_to_setspi))
		return false;

	/* Using 390K spi speed at first and raising to 1.5M at 310M PLL
	 * to avoid spi failure on 150M PLL */
	applog(LOG_NOTICE, "chain%d: spi speed set to 1.5M", chain_id);
	mcompat_set_spi_speed(chain_id, SPI_SPEED_1562K);
	cgsleep_ms(10);

	pll_lv_to_setvid = T1_ConfigT1PLLClock(T1_PLL_SETVID);
	if (!t1_set_pll(t1, CMD_ADDR_BROADCAST, pll_lv_to_setvid))
		return false;

	/* Set voltage down at this point to avoid massive power draw as we
	 * increase frequency */
	if (!opt_T1auto && opt_T1VID[chain_id]) {
		/* If opt_T1VID values are set in non-auto mode, we use those
		 * from the config. */
		mcompat_set_vid_by_step(chain_id, t1->iVid, opt_T1VID[chain_id]);
		t1->iVid = opt_T1VID[chain_id];
	} else {
		t1->iVid = mcompat_find_chain_vid(chain_id, t1->num_active_chips,
						  STARTUP_VID, opt_T1Vol[chain_id]);
	}

	if (!t1_set_pll(t1, CMD_ADDR_BROADCAST, idxpll))
		return false;

	/* Now fine tune voltage to the target level as voltage will have
	 * changed due to changing frequency */
	if (opt_T1auto || !opt_T1VID[chain_id]) {
		t1->iVid = mcompat_find_chain_vid(chain_id, t1->num_active_chips,
						  t1->iVid, opt_T1Vol[chain_id]);
	}

	/* Read chip voltages */
	get_voltages(t1);
	applog(LOG_NOTICE, "chain%d: volt = %.1f, vid = %d after calibration", chain_id,
	       s_reg_ctrl.average_vol[chain_id], t1->iVid);

	return true;
}

/*
 * BIST_START works only once after HW reset, on subsequent calls it
 * returns 0 as number of chips.
 */
static int chain_detect(struct T1_chain *t1)
{
	int cid = t1->chain_id;
	uint8_t n_chips = mcompat_cmd_bist_start(cid, CMD_ADDR_BROADCAST);

	if (unlikely(n_chips == 0 || n_chips > MAX_CHIP_NUM)){
		write_miner_ageing_status(AGEING_BIST_START_FAILED);
		return 0;
	}

	applog(LOG_WARNING, "%d: detected %d chips", cid, n_chips);

	cgsleep_ms(10);
/*
	if (!mcompat_cmd_bist_collect(cid, CMD_ADDR_BROADCAST))
	{
		applog(LOG_WARNING, "bist collect fail");
		return 0;
	}
*/

	applog(LOG_WARNING, "collect core success");

	return n_chips;
}

static bool prepare_T1(struct T1_chain *t1, int chain_id)
{
	uint8_t buffer[4] = {};
	bool ret = false;

	//spi speed init
	applog(LOG_NOTICE, "chain%d: spi speed set to 390K", chain_id);
	mcompat_set_spi_speed(chain_id, T1_SPI_SPEED_DEF);
	cgsleep_ms(10);

	if (!dm_cmd_resetall(chain_id, CMD_ADDR_BROADCAST, buffer)) {
		applog(LOG_ERR, "failed to reset chain %d!", chain_id);
		goto out;
	}
	if (CMD_TYPE_T1 != (buffer[0] & 0xf0)) {
		applog(LOG_ERR, "incompatible chip type %02X for chain %d!", buffer[0] & 0xf0, chain_id);
		goto out;
	}

	t1->num_chips = chain_detect(t1);
	cgsleep_ms(10);

	if ((t1->num_chips <= 0) || (t1->num_chips > MAX_CHIP_NUM)){
		spi_status_err_cnt++;

		if (chain_id == (MAX_CHAIN_NUM - 1)){
			if (spi_status_err_cnt >= MAX_CHAIN_NUM){
				write_miner_ageing_status(AGEING_ALL_SPI_STATUS_ERROR);
			}
			if ((spi_status_err_cnt >= 1) && (spi_status_err_cnt < MAX_CHAIN_NUM)){
				write_miner_ageing_status(AGEING_SPI_STATUS_ERROR);
			}
		}
		goto out;
	}

	if (chain_id == (MAX_CHAIN_NUM - 1)){
		if (spi_status_err_cnt >= MAX_CHAIN_NUM){
			write_miner_ageing_status(AGEING_ALL_SPI_STATUS_ERROR);
		}
		if ((spi_status_err_cnt >= 1) && (spi_status_err_cnt < MAX_CHAIN_NUM)){
			write_miner_ageing_status(AGEING_SPI_STATUS_ERROR);
		}
	}

	/* override max number of active chips if requested */
	t1->num_active_chips = t1->num_chips;
	if (T1_config_options.override_chip_num > 0 &&
		t1->num_chips > T1_config_options.override_chip_num) {
		t1->num_active_chips = T1_config_options.override_chip_num;
		applog(LOG_WARNING, "%d: limiting chain to %d chips",
		       chain_id, t1->num_active_chips);
	}

	/* Free this in case we are re-initialising a chain */
	free(t1->chips);
	t1->chips = cgcalloc(t1->num_active_chips, sizeof(struct T1_chip));
	ret = true;
out:
	return ret;
}

static struct T1_chain *pre_init_T1_chain(int chain_id)
{
	struct T1_chain *t1 = cgcalloc(sizeof(*t1), 1);

	applog(LOG_INFO, "pre %d: T1 init chain", chain_id);

	t1->chain_id = chain_id;
	if (!prepare_T1(t1, chain_id)) {
		exit_T1_chain(t1);
		t1 = NULL;
	}
	return t1;
}

static bool init_T1_chain(struct T1_chain *t1)
{
	int i;
	uint8_t src_reg[REG_LENGTH] = {0};
	uint8_t reg[REG_LENGTH] = {0};
	int chain_id = t1->chain_id;
	int num_chips;
	bool ret = false;

	applog(LOG_INFO, "%d: T1 init chain", chain_id);

	applog(LOG_NOTICE, "chain%d: spi speed set to 6.25M", chain_id);
	mcompat_set_spi_speed(chain_id, SPI_SPEED_6250K);
	cgsleep_ms(1);

#ifdef USE_BISTMASK
	dm_cmd_resetbist(chain_id, CMD_ADDR_BROADCAST, reg);
	//cgsleep_ms(120);
	sleep(1);

	//bist mask
	mcompat_cmd_read_register(chain_id, 0x01, reg, REG_LENGTH);
	memcpy(src_reg, reg, REG_LENGTH);
	src_reg[7] = src_reg[7] | 0x10;
	mcompat_cmd_write_register(chain_id, CMD_ADDR_BROADCAST, src_reg, REG_LENGTH);
	cgsleep_us(200);
#endif

	applog(LOG_DEBUG, "%d: T1 init chain", chain_id);

	num_chips = chain_detect(t1);
	cgsleep_ms(10);

	if (num_chips != 0 && num_chips != t1->num_chips) {
		applog(LOG_WARNING, "T1 %d: Num chips failure", chain_id);
		goto out;
	}

	if (!mcompat_cmd_bist_fix(chain_id, CMD_ADDR_BROADCAST)) {
		write_miner_ageing_status(AGEING_BIST_FIX_FAILED);
		goto out;
	}

	cgsleep_us(200);

#if 0
	sprintf(volShowLog[chain_id], "+   %2d  |  %8f  |  %8f  |  %8f  |",chain_id,   \
	s_reg_ctrl.highest_vol[chain_id],s_reg_ctrl.average_vol[chain_id],s_reg_ctrl.lowest_vol[chain_id]);
	dragonmint_log_record(chain_id, volShowLog[chain_id], strlen(volShowLog[0]));
#endif
	applog(LOG_WARNING, 
		"Chain %d Voltage information. Highest Vol:%.0f, Average Vol:%.0f, Lowest Vol:%.0f",
		chain_id, s_reg_ctrl.highest_vol[chain_id], s_reg_ctrl.average_vol[chain_id],
		s_reg_ctrl.lowest_vol[chain_id]);

	/* Reset value in case we are re-initialising */
	t1->num_cores = 0;
	for (i = 0; i < t1->num_active_chips; i++)
		check_chip(t1, i);

	applog(LOG_WARNING, "%d: found %d chips with total %d active cores",
	       chain_id, t1->num_active_chips, t1->num_cores);

	if (!opt_T1auto)
		t1->VidOptimal = t1->pllOptimal = true;

	ret = true;
out:
	return ret;
}

/* Asynchronous work generation since get_work is a blocking function */
static void *T1_work_thread(void *arg)
{
	struct cgpu_info *cgpu = arg;
	struct T1_chain *t1 = cgpu->device_data;
	char tname[16];

	sprintf(tname, "T1_%dwork", t1->chain_id);
	RenameThread(tname);

	mutex_lock(&t1->lock);

	while (!pthread_cond_wait(&t1->cond, &t1->lock)) {
		mutex_unlock(&t1->lock);

		/* Only start filling the queue once we're 1/3 empty */
		if (t1->active_wq.num_elems < t1->num_active_chips * 4 / 3)
			wq_enqueue(cgpu->thr[0], t1);

		mutex_lock(&t1->lock);
	}

	return NULL;
}

static void start_T1_chain(int cid, int retries)
{
	mcompat_set_reset(cid, 1);
	sleep(retries);
	mcompat_set_power_en(cid, 1);
	sleep(retries);
	mcompat_set_reset(cid, 0);
	sleep(retries);
	mcompat_set_start_en(cid, 1);
	sleep(retries);
	mcompat_set_reset(cid, 1);
	sleep(retries);
}

static bool detect_T1_chain(void)
{
	int i, retries, chain_num = 0, chip_num = 0, iPll;
	c_temp_cfg tmp_cfg;

	applog(LOG_NOTICE, "T1: checking T1 chain");

	for(i = 0; i < MAX_CHAIN_NUM; i++) {
		if (chain_plug[i] != 1)
			continue;
		chain_num++;
	}


	/* Go back and try chains that have failed after cycling through all of
	 * them. */
	for (retries = 0; retries < 3; retries++) {
		for (i = 0; i < MAX_CHAIN_NUM; i++) {
			if (chain_plug[i] != 1)
				continue;
			if (chain[i])
				continue;
			start_T1_chain(i, retries);

			/* pre-init chain */
			if ((chain[i] = pre_init_T1_chain(i))) {
				chain_flag[i] = 1;
				if (chain[i]->num_chips > chip_num)
					chip_num = chain[i]->num_chips;
			}
		}
	}

	// reinit platform with real chain number and chip number
	applog(LOG_NOTICE, "platform re-init: chain_num(%d), chip_num(%d)", chain_num, chip_num);
	sys_platform_exit();
	sys_platform_init(PLATFORM_ZYNQ_HUB_G19, MCOMPAT_LIB_MINER_TYPE_T1, chain_num, chip_num);

	for (i = 0; i < MAX_CHAIN_NUM; i++) {
		if (chain_plug[i] != 1)
			continue;
		if (chain[i] == NULL){
			applog(LOG_ERR, "init %d T1 chain fail", i);
			continue;
		}

		// re-config spi speed after platform init
		mcompat_set_spi_speed(i, T1_SPI_SPEED_DEF);
		cgsleep_ms(10);

		mcompat_cfg_tsadc_divider(i, PLL_Clk_12Mhz[0].speedMHz);
	}

	// init temp ctrl
	dm_tempctrl_get_defcfg(&tmp_cfg);
	/* Set initial target temperature lower for more reliable startup */
	tmp_cfg.tmp_target = T1_TEMP_TARGET_INIT;	// target temperature
	dm_tempctrl_init(&tmp_cfg);

	// start fan ctrl thread
	c_fan_cfg fan_cfg;
	dm_fanctrl_get_defcfg(&fan_cfg);
	fan_cfg.preheat = false;		// disable preheat
	fan_cfg.fan_speed = T1_FANSPEED_INIT;
	dm_fanctrl_init(&fan_cfg);
//	dm_fanctrl_init(NULL);			// using default cfg
	pthread_create(&fan_tid, NULL, dm_fanctrl_thread, NULL);

	for(i = 0; i < MAX_CHAIN_NUM; i++) {
		if (chain_flag[i] != 1)
			continue;
		if (!prechain_detect(chain[i], T1Pll[i])) {
			chain_flag[i] = 0;
			exit_T1_chain(chain[i]);
		}
	}

	for(i = 0; i < MAX_CHAIN_NUM; i++) {
		if (chain_flag[i] != 1)
			continue;

		if (!init_T1_chain(chain[i])) {
			exit_T1_chain(chain[i]);
			applog(LOG_ERR, "init %d T1 chain fail", i);
			chain_flag[i] = 0;
			continue;
		}
	}

	for(i = 0; i < MAX_CHAIN_NUM; i++) {
		struct cgpu_info *cgpu;
		struct T1_chain *t1;
		pthread_t pth;

		if (chain_flag[i] != 1)
			continue;

		total_chains++;
		cgpu = cgcalloc(sizeof(*cgpu), 1);
		cgpu->drv = &dragonmintT1_drv;
		cgpu->name = "DragonmintT1.SingleChain";
		cgpu->threads = 1;
		cgpu->chainNum = i;
		cgpu->device_data = t1 = chain[i];
		cgtime(&cgpu->dev_start_tv);
		t1->lastshare = cgpu->dev_start_tv.tv_sec;

		iPll = T1Pll[i];

		if ((chain[i]->num_chips <= MAX_CHIP_NUM) && (chain[i]->num_cores <= MAX_CORES)) {
			cgpu->mhs_av = (double)PLL_Clk_12Mhz[iPll].speedMHz * 2ull * (chain[i]->num_cores);
		} else {
			cgpu->mhs_av = 0;
			chain_flag[i] = 0;
		}

		chain[i]->cgpu = cgpu;
		cgpu->device_id = i;
		add_cgpu(cgpu);

		mcompat_set_led(i, LED_ON);
		applog(LOG_WARNING, "Detected the %d T1 chain with %d chips / %d cores",
			i, chain[i]->num_active_chips, chain[i]->num_cores);

		INIT_LIST_HEAD(&t1->active_wq.head);

		mutex_init(&t1->lock);
		pthread_cond_init(&t1->cond, NULL);
		pthread_create(&pth, NULL, T1_work_thread, cgpu);
	}

	if (!total_chains)
		return false;

	/* Now adjust target temperature for runtime setting */
	tmp_cfg.tmp_target = T1_TEMP_TARGET_RUN;
	dm_tempctrl_set(&tmp_cfg);

	return true;
}

/* Probe SPI channel and register chip chain */
void T1_detect(bool hotplug)
{
	int i;

	if (hotplug)
		return;

	/* parse bimine-t1-options */
	if (opt_dragonmint_t1_options != NULL && parsed_config_options == NULL) {
		int ref_clk = 0;
		int sys_clk = 0;
		int spi_clk = 0;
		int override_chip_num = 0;
		int wiper = 0;

		sscanf(opt_dragonmint_t1_options, "%d:%d:%d:%d:%d",
		 &ref_clk, &sys_clk, &spi_clk,  &override_chip_num,
	 &wiper);
		if (ref_clk != 0)
			T1_config_options.ref_clk_khz = ref_clk;
		if (sys_clk != 0) {
			if (sys_clk < 100000)
				quit(1, "system clock must be above 100MHz");
			T1_config_options.sys_clk_khz = sys_clk;
		}
		if (spi_clk != 0)
			T1_config_options.spi_clk_khz = spi_clk;
		if (override_chip_num != 0)
			T1_config_options.override_chip_num = override_chip_num;
		if (wiper != 0)
			T1_config_options.wiper = wiper;

		/* config options are global, scan them once */
		parsed_config_options = &T1_config_options;
	}

	applog(LOG_DEBUG, "T1 detect");
	memset(&s_reg_ctrl,0,sizeof(s_reg_ctrl));

	g_hwver = dragonmint_get_hwver();
//	g_type = dragonmint_get_miner_type();

	// FIXME: get correct hwver and chain num to init platform
	sys_platform_init(PLATFORM_ZYNQ_HUB_G19, MCOMPAT_LIB_MINER_TYPE_T1, MAX_CHAIN_NUM, MAX_CHIP_NUM);

	applog(LOG_NOTICE, "vid type detected: %d", misc_get_vid_type());

	// set fan speed high to get to a lower startup temperature
	dm_fanctrl_set_fan_speed(T1_FANSPEED_INIT);

	//dragonmint_miner_init_voltage_flag();

	for (i = MAX_CHAIN_NUM - 1; i >= 0; i--) {
		if (mcompat_get_plug(i) == 0) {
			chain_plug[i] = 1;
			applog(LOG_INFO, "chain:%d the plat is inserted", i);
		} else {
			applog(LOG_INFO, "chain:%d the plat is not inserted", i);
			write_miner_ageing_status(AGEING_PLUG_STATUS_ERROR);
		}
	}

	/* If hardware version is g19, continue init cgminer. Else power off*/
	if (HARDWARE_VERSION_G19 == g_hwver) {
		applog(LOG_INFO, "The hardware version is G19");
		for (i = 0; i < MAX_CHAIN_NUM; i++) {
			if (chain_plug[i] != 1)
				continue;

			/* Sets initial voltage to very high to get chips
			 * initialised. */
			mcompat_set_vid(i, STARTUP_VID);
		}
	} else if (HARDWARE_VERSION_G9 == g_hwver) {
		applog(LOG_INFO, "The hardware version is G9");
		mcompat_set_vid(0, STARTUP_VID);
	} else {
		for(i = 0; i < MAX_CHAIN_NUM; i++) {
			applog(LOG_ERR, "Unknown hwver, chain%d power down", i);
			mcompat_chain_power_down(i);
		}
		write_miner_ageing_status(AGEING_HW_VERSION_ERROR);
		return;
	}

	for(i = 0; i < MAX_CHAIN_NUM; ++i) {
		int pll = DEFAULT_PLL;

		/* Tune voltage to highest frequency in Performance mode, and
		 * lowest frequency in efficient mode. */
		if (opt_T1auto) {
			if (opt_T1_performance)
				pll = MAX_PLL;
			else if (opt_T1_efficient)
				pll = MIN_PLL;
		} else
			pll = opt_T1Pll[i];
		T1Pll[i] = T1_ConfigT1PLLClock(pll);
	}

	if (detect_T1_chain()) {
		if (misc_get_vid_type() == MCOMPAT_LIB_VID_I2C_TYPE)
			set_timeout_on_i2c(30);
		applog(LOG_WARNING, "T1 detect finish");
	}
}

/* Exit cgminer on failure, allowing systemd watchdog to restart */
static void reinit_T1_chain(struct T1_chain *t1, int cid)
{
	bool success = false;
	struct timeval now;
	int i;

	applog(LOG_WARNING, "T1: %d attempting to re-initialise!", cid);
	for (i = 0; i < 3; i++) {
		start_T1_chain(cid, i);
		if (prepare_T1(t1, cid)) {
			success = true;
			break;
		}
	}
	if (!success) {
		applog(LOG_EMERG, "T1: %d FAILED TO PREPARE, SHUTTING DOWN", cid);
		raise_cgminer();
	}
	if (!prechain_detect(t1, T1Pll[cid])) {
		applog(LOG_EMERG, "T1: %d FAILED TO PRECHAIN DETECT, SHUTTING DOWN", cid);
		raise_cgminer();
	}
	if (!init_T1_chain(t1)) {
		applog(LOG_EMERG, "T1: %d FAILED TO INIT, SHUTTING DOWN", cid);
		raise_cgminer();
	}
	cgtime(&now);
	t1->lastshare = now.tv_sec;
}

#define VOLTAGE_UPDATE_INT  121
#define WRITE_CONFIG_TIME   60
#define CHECK_DISABLE_TIME  59

#if 0
char szShowLog[MAX_CHAIN_NUM][MAX_CHIP_NUM][256] = {0};
#define  LOG_FILE_PREFIX "/tmp/log/analys"

char cLevelError1[3] = "!";
char cLevelError2[3] = "#";
char cLevelError3[3] = "$";
char cLevelError4[3] = "%";
char cLevelError5[3] = "*";
char cLevelNormal[3] = "+";

void Dragonmint_Log_Save(struct T1_chip *chip,int nChip,int nChain)
{
	char szInNormal[8] = {};

	if (chip->hw_errors > 0){
		strcat(szInNormal,cLevelError1);
	}
	if (chip->stales > 0){
		strcat(szInNormal,cLevelError2);
	}
	if (chip->num_cores < 32){
		strcat(szInNormal,cLevelError4);
	}
	if ((chip->nVol > 440) || (chip->nVol < 360)){
		strcat(szInNormal,cLevelError5);
	}

	if ((chip->hw_errors == 0) && (chip->stales == 0) && ((chip->nVol < 440) && (chip->nVol > 360)) && (chip->num_cores == 32)){
		strcat(szInNormal,cLevelNormal);
	}

	sprintf(szShowLog[nChain][nChip], "\n%-8s|%32d|%8d|%8d|%8d|%8d|%8d|%8d|%8d",szInNormal,chip->nonces_found,
		chip->hw_errors, chip->stales,chip->temp,chip->nVol,chip->num_cores,nChip,nChain);
}

void dragonmint_log_print(int cid, void* log, int len)
{
	FILE* fd;
	char fileName[128] = {0};

	sprintf(fileName, "%s%d.log", LOG_FILE_PREFIX, cid);
	fd = fopen(fileName, "w+");
	if (fd == NULL){
		applog(LOG_ERR, "Open log File%d Failed!", cid);
		return;
	}

	fwrite(log, len, 1, fd);
	fflush(fd);
	fclose(fd);
}
#endif

/* Invalidate all statistics during buffer underruns and while hardware isn't
 * running at its optimal temperature. */
static void reset_tune(struct T1_chain *t1)
{
	struct timeval now;

	cgtime(&now);
	copy_time(&t1->cycle_start, &now);
	t1->cycles = 0;
	/* Reset last share time since the hardware could genuinely not be
	 * able to generate shares during extended underruns. */
	t1->lastshare = now.tv_sec;
}

static void T1_set_optimal_vid(struct T1_chain *t1, int cid)
{
	double best = 0, product[T1_VID_TUNE_RANGE] = {};
	int i, vid = t1->iVid;

	for (i = 0; i < T1_VID_TUNE_RANGE; i++) {
		product[i] = t1->vidproduct[i];
		/* In efficient mode divide by the square of the voltage */
		if (opt_T1_efficient && t1->vidvol[i])
			product[i] /= (double)(t1->vidvol[i] * t1->vidvol[i]);
	}

	for (i = 0; i < T1_VID_TUNE_RANGE; i++) {
		if (!t1->vidproduct[i])
			continue;
		applog(LOG_ERR, "vid%d: product=%.5f, hwerr=%.5f", 
			i, t1->vidproduct[i], t1->vidhwerr[i]);
		/* Allow up to 1% drop for the sake of lower voltage */
		if (!best || (product[i] > best * 0.99 && t1->vidhwerr[i] < 0.2)) {
			best = product[i];
			vid = i;
		}
		/* Reset values for clean reuse */
		t1->vidproduct[i] = 0;
	}

	t1->optimalVid = vid;
	t1->VidOptimal = true;

	mcompat_set_vid_by_step(cid, t1->iVid, vid);
	t1->iVid = vid;

	get_voltages(t1);
	/* Store the optimal voltage for readjusting after PLL changes */
	t1->optimal_vol = s_reg_ctrl.average_vol[cid];

	/* Set opt_T1VID for saving to config file */
	opt_T1VID[cid] = t1->iVid;

	for (i = 0; i < T1_PLL_TUNE_RANGE; i++)
		t1->pllvid[i] = t1->iVid;
}

/* This returns the best absolute product */
static double T1_best_vid_product(struct T1_chain *t1)
{
	double best = 0;
	int i;

	for (i = 0; i < T1_VID_TUNE_RANGE; i++) {
		if (t1->vidproduct[i] > best)
			best = t1->vidproduct[i];
	}
	return best;
}

static void T1_set_optimal_pll(struct T1_chain *t1, int cid)
{
	double best = 0, product[T1_PLL_TUNE_RANGE] = {};
	int i, pll = t1->pll, best_offset = 0;

	for (i = 0; i < T1_PLL_TUNE_RANGE; i++) {
		product[i] = t1->pllproduct[i];
		/* In efficient mode divide by the frequency */
		if (opt_T1_efficient)
			product[i] /= (double)PLL_Clk_12Mhz[i + T1_PLL_TUNE_MIN].speedMHz;
	}

	for (i = 0; i < T1_PLL_TUNE_RANGE; i++) {
		if (!t1->pllproduct[i])
			continue;
		applog(LOG_ERR, "pll%d: product=%.5f, hwerr=%.5f", 
			i + T1_PLL_TUNE_MIN, t1->pllproduct[i], t1->pllhwerr[i]);
		if (!best || (product[i] > best && t1->pllhwerr[i] < 0.2)) {
			best = product[i];
			pll = i + T1_PLL_TUNE_MIN;
			best_offset = i;
		}
		t1->pllproduct[i] = 0;
	}

	t1->pllOptimal = true;

	t1_set_pll(t1, CMD_ADDR_BROADCAST, pll);
	t1->base_pll = t1->pll;

	/* Set opt_T1Pll for saving to config file */
	opt_T1Pll[cid] = PLL_Clk_12Mhz[t1->pll].speedMHz;

	/* Readjust iVid if we changed it during tuning */
	if (t1->iVid != t1->pllvid[best_offset]) {
		mcompat_set_vid_by_step(cid, t1->iVid, t1->pllvid[best_offset]);
		t1->iVid = t1->pllvid[best_offset];
		opt_T1VID[cid] = t1->iVid;
	}
}

static double T1_best_pll_product(struct T1_chain *t1)
{
	double best = 0;
	int i;

	for (i = 0; i < T1_PLL_TUNE_RANGE; i++) {
		if (t1->pllproduct[i] > best)
			best = t1->pllproduct[i];
	}
	return best;
}

static void T1_save_config(void)
{
	FILE *fcfg;

	fcfg = fopen("/config/cgminer.conf", "w");
	if (unlikely(fcfg == NULL)) {
		applog(LOG_ERR, "Failed to open /config/cgminer.conf for writing!");
		return;
	}

	write_config(fcfg);
	fflush(fcfg);
	fclose(fcfg);
}

static void T1_tune_complete(struct T1_chain *t1, int cid)
{
	int i;

	applog(LOG_WARNING, "T1 %d tuning complete, optimal VID %d PLL %d", cid,
	       t1->iVid, t1->pll);
	t1->sampling = false;
	/* Reset hw error count to ignore noise during tuning. */
	for(i = 0; i < t1->num_active_chips; ++i)
		t1->chips[i].hw_errors = 0;
	if (++chains_tuned < total_chains)
		return;

	applog(LOG_WARNING, "Tuning complete, saving results to config file");
	/* Change t1auto to false to save to config to disable tuning
	 * on next run. */
	opt_T1auto = false;
	T1_save_config();
	/* Reset all stats after tuning */
	zero_stats();
}

static void T1_tune(struct T1_chain *t1, int cid)
{
	double product, tdiff, best, hw_rate;
	int offset, i, hwerr, hw_diff;
	struct timeval now;

	cgtime(&now);

	if (t1->pllOptimal)
		return;

	if (unlikely(!t1->cycle_start.tv_sec)) {
		copy_time(&t1->cycle_start, &now);
		t1->cycles = 0;
		return;
	}

	if (t1->cycles < T1_CYCLES_CHAIN)
		return;

	tdiff = ms_tdiff(&now, &t1->cycle_start);
	product = (double)t1->cycles / tdiff;

	// hwerr stat.
	hwerr = 0;
	for(i = 0; i < t1->num_active_chips; ++i)
		hwerr += t1->chips[i].hw_errors;
	hw_diff = hwerr - t1->hw_errors;
	t1->hw_errors = hwerr;
	hw_rate = (double) hw_diff / tdiff;

	applog(LOG_NOTICE, "Chain %d cycles %d, hw %d, vid %d, pll %d, %.1fms product %f, hwrate %f",
	       cid, t1->cycles, hw_diff, t1->iVid, t1->pll, tdiff, product, hw_rate);
	reset_tune(t1);

	if (!t1->sampling) {
		/* Discard the first lot of samples due to changing diff on
		 * startup and possible init times invalidating data. */
		t1->sampling = true;
		return;
	}

	if (t1->VidOptimal)
		goto tune_freq;

	best = T1_best_vid_product(t1);
	t1->vidproduct[t1->iVid] = product;
	t1->vidhwerr[t1->iVid] = hw_rate;
	/* Plenty of time has passed since we set this VID so reading the
	 * voltage will be accurate here */
	get_voltages(t1);
	t1->vidvol[t1->iVid] = s_reg_ctrl.average_vol[cid];

	/* Don't keep going lower voltage in Performance mode if there's been
	 * a large drop in product as any further may be unstable */
	if (t1->iVid < T1_VID_MAX && (!opt_T1_performance || product > best * 0.9)) {
		/* We don't need great accuracy here so no need to delay after
		 * setting VID */
		mcompat_set_vid(cid, ++t1->iVid);
		get_voltages(t1);
		if (s_reg_ctrl.average_vol[cid] > TUNE_VOLT_STOP) {
			applog(LOG_NOTICE, "Chain% d testing iVid %d avg voltage %.0f",
			       cid, t1->iVid, s_reg_ctrl.average_vol[cid]);
			return;
		}
	}

	/* Now find the iVid that corresponds with highest product */
	T1_set_optimal_vid(t1, cid);
	applog(LOG_WARNING, "T1 %d optimal iVid set to %d, beginning freq tune",
	       cid, t1->iVid);
	return;

tune_freq:
	best = T1_best_pll_product(t1);
	offset = t1->pll - T1_PLL_TUNE_MIN;
	t1->pllproduct[offset] = product;
	t1->pllhwerr[offset] = hw_rate;
	if (t1->pll < T1_PLL_TUNE_MAX && product > best) {
		/* Only keep increasing frequency if product has been
		 * increasing. */
		t1->base_pll = ++t1->pll;
		applog(LOG_NOTICE, "Chain %d testing pll %d", cid, t1->pll);
		T1_SetT1PLLClock(t1, t1->pll, 0);
		if (t1->iVid > T1_VID_MIN) {
			/* Vid was set a long time ago so it should be
			 * accurate to read voltages. */
			get_voltages(t1);
			if (s_reg_ctrl.average_vol[cid] < t1->optimal_vol - 2) {
				applog(LOG_WARNING, "Chain %d dropping VID to %d due to PLL %d lowering voltage to %.0f",
				       cid, --t1->iVid, t1->pll, s_reg_ctrl.average_vol[cid]);
				mcompat_set_vid(cid, t1->iVid);
			}
		}
		/* Store the iVid associated with this pll */
		t1->pllvid[offset] = t1->iVid;
		return;
	}

	T1_set_optimal_pll(t1, cid);
	applog(LOG_WARNING, "Chain %d optimal pll set to %d Mhz",
	       cid, PLL_Clk_12Mhz[t1->pll].speedMHz);

	T1_tune_complete(t1, cid);
}

#define MAX_NONCE_SLEEP		(100)
#define T1_THROTTLE_INTERVAL	(5)
#define T1_RAISE_INTERVAL	(15)

static void t1_throttle(struct T1_chain *t1, int cid)
{
	time_t now;

	/* Chain will have been shut down by the time we get to zero but it's
	 * possible with complete fan failures. */
	if (t1->pll <= T1_PLL_MIN)
		return;

	/* Only throttle further after 5 second intervals */
	now = time(NULL);
	if (now - t1->throttled < T1_THROTTLE_INTERVAL)
		return;
	t1->throttled = now;

	applog(LOG_WARNING, "T1 %d Chain throttling to %d MHz for overheat!", cid ,
	       PLL_Clk_12Mhz[--t1->pll].speedMHz);
	T1_SetT1PLLClock(t1, t1->pll, 0);
}

static void t1_raise(struct T1_chain *t1, int cid)
{
	time_t now = time(NULL);

	/* Same as throttling, but wait 15s before increasing frequency */
	if (now - t1->throttled < T1_RAISE_INTERVAL)
		return;
	t1->throttled = now;

	applog(LOG_WARNING, "T1 %d Chain increasing frequency to %d MHz from throttle due to cooldown",
	       cid, PLL_Clk_12Mhz[++t1->pll].speedMHz);
	T1_SetT1PLLClock(t1, t1->pll, 0);

	/* If we're back to base pll then throttling has ceased */
	if (t1->pll >= t1->base_pll) {
		t1->throttled = 0;
		/* Reset all the values in case we started throttling in the
		 * middle of tuning, rendering all the values inval. */
		reset_tune(t1);
	}
}

#define MAX_CMD_FAILS		(0)
#define MAX_CMD_RESETS		(50)

static int g_cmd_fails[MAX_CHAIN_NUM];
static int g_cmd_resets[MAX_CHAIN_NUM];

static void T1_overheated_blinking(int cid)
{
	// block thread and blink led
	while (42) {
		mcompat_set_led(cid, LED_OFF);
		cgsleep_ms(500);
		mcompat_set_led(cid, LED_ON);
		cgsleep_ms(500);
	}
}

static int64_t T1_scanwork(struct thr_info *thr)
{
	struct cgpu_info *cgpu = thr->cgpu;
	struct T1_chain *t1 = cgpu->device_data;
	int cid = t1->chain_id, i;
	struct T1_chip *chip;
	int64_t hashes = 0;
	uint32_t nonce;
	uint8_t chip_id;
	uint8_t job_id;
	uint16_t micro_job_id;
	uint8_t reg[REG_LENGTH] = {0};
	int timer_sleep = 10;
	int slept = 0;
	bool nononce = false;
	int chain_temp_status;
#ifdef USE_AUTONONCE
	bool autononce = true;
#endif
	struct timeval now;

	if (unlikely((t1->num_cores == 0) || (t1->num_cores > MAX_CORES))) {
		cgpu->deven = DEV_DISABLED;
		return -1;
	}

	/* Spurious wakeups are harmless */
	pthread_cond_signal(&t1->cond);

	cgtime(&now);

	/* Poll queued results. A full nonce range takes about 200ms to scan so
	 * we're unlikely to need more work until then. Poll every 10ms for up
	 * to 100ms using a reentrant function to avoid having any latency from
	 * processing received nonces and avoid sleeping too long. */
	while (true) {
		struct work *work;

		if (!get_nonce(t1, (uint8_t*)&nonce, &chip_id, &job_id, (uint8_t*)&micro_job_id)) {
			if (timer_sleep < MAX_NONCE_SLEEP && !thr->work_restart && !nononce) {
				/* Do not sleep more than twice if no results */
				nononce = true;
				slept += cgsleep_ms_r(&t1->cgt, timer_sleep);
				timer_sleep += 10;
				continue;
			}
			/* Disable any further sleeps */
			timer_sleep = MAX_NONCE_SLEEP;
#ifdef USE_AUTONONCE
			if (autononce) {
				/* Check once more after autononce is disabled
				 * in case we just missed the last one */
				mcompat_cmd_auto_nonce(cid, 0, REG_LENGTH);  // disable auto get nonce
				autononce = false;
				continue;
			} else
#endif
				break;
		}
		nononce = false;
		nonce = bswap_32(nonce);
		chip = &t1->chips[chip_id - 1];
		if (nonce == chip->last_nonce) {
			applog(LOG_INFO, "%d: chip %d: duplicate nonce.", cid, chip_id);
			chip->dupes++;
			continue;
		}

		if (chip_id < 1 || chip_id > t1->num_active_chips) {
			applog(LOG_WARNING, "%d: wrong chip_id %d", cid, chip_id);
			continue;
		}
		if (job_id < 1 || job_id > 4) {
			applog(LOG_WARNING, "%d: chip %d: result has wrong ""job_id %d", cid, chip_id, job_id);
			continue;
		}
		chip->last_nonce = nonce;
		work = chip->work[job_id - 1];
		if (work == NULL) {
			/* already been flushed => stale */
			applog(LOG_INFO, "%d: chip %d: stale nonce 0x%08x", cid, chip_id, nonce);
			chip->stales++;
			continue;
		}
		work->micro_job_id = micro_job_id;
		memcpy(work->data, &(work->pool->vmask_001[micro_job_id]), 4);

		if (!submit_nonce(thr, work, nonce)) {
			applog(LOG_INFO, "%d: chip %d: invalid nonce 0x%08x", cid, chip_id, nonce);
			applog(LOG_INFO, "micro_job_id %d", micro_job_id);
			chip->hw_errors++;
			continue;
		}
		applog(LOG_INFO, "YEAH: %d: chip %d / job_id %d: nonce 0x%08x", cid, chip_id, job_id, nonce);
		chip->nonces_found++;
		hashes += work->device_diff;
		t1->cycles++;
		t1->lastshare = now.tv_sec;
	}

	if (unlikely(now.tv_sec - t1->lastshare > 300)) {
		applog(LOG_EMERG, "T1 chain %d not producing shares for more than 5 mins.",
		       cid);
		reinit_T1_chain(t1, cid);
	}

	cgsleep_prepare_r(&t1->cgt);

	if (thr->work_restart) {
		if (!dm_cmd_resetjob(cid, CMD_ADDR_BROADCAST, reg))
			 applog(LOG_WARNING, "T1 %d clear work failed", cid);

		/* Flush the work chips were currently hashing */
		for (i = 0; i < t1->num_active_chips; i++) {
			int j;

			struct T1_chip *chip = &t1->chips[i];
			for (j = 0; j < 2; j++) {
				if (!chip->work[j])
					continue;
				//applog(LOG_DEBUG, "%d: flushing chip %d, work %d: 0x%p",
				// cid, i, j + 1, work);
				free_work(chip->work[j]);
			}
			chip->last_queued_id = 0;
		}

		/* Flush any work in the driver queue */
		while (t1->active_wq.num_elems > 0) {
			/* We don't want to signal wq_dequeue to get more
			 * work until we've emptied the queue */
			struct work *work = wq_dequeue(t1, false);

			free_work(work);
		}
		pthread_cond_signal(&t1->cond);
		/* Reset tuning parameters since dropping work on block change
		 * can adversely affect hashrate */
		reset_tune(t1);
	}

	/* Clean spi buffer before read 0a reg */
	hub_spi_clean_chain(cid);

	if (thr->work_restart || mcompat_cmd_read_register(cid, MAX_CHIP_NUM >> 1, reg, REG_LENGTH)) {
		uint8_t qstate = reg[9] & 0x03;

		/* Clear counter */
		g_cmd_fails[cid] = 0;
		g_cmd_resets[cid] = 0;

		/* qstate will always be 0x0 when work_restart is set */
		if (qstate != 0x03) {
			if (qstate == 0x0) {
				for (i = t1->num_active_chips; i > 0; i--) {
					struct T1_chip *chip = &t1->chips[i - 1];
					struct work *work = wq_dequeue(t1, true);
					uint8_t c = i;

					if (unlikely(!work)) {
						reset_tune(t1);
						applog(LOG_WARNING, "T1 %d main work underrun", cid);
						cgsleep_ms(10);
						break;
					}
					if (set_work(t1, c, work, 0))
						chip->nonce_ranges_done++;
				}
			}

			//applog(LOG_NOTICE, "qstate is not 0x0,the number of work is %d. \t", t1->active_wq.num_elems);
			for (i = t1->num_active_chips; i > 0; i--) {
				struct T1_chip *chip = &t1->chips[i - 1];
				struct work *work = wq_dequeue(t1, true);
				uint8_t c = i;

				if (unlikely(!work)) {
					/* Demote this message since it's the
					 * backup queue and not critical */
					applog(LOG_INFO, "T1 %d backup work underrun", cid);
					break;
				}
				if (set_work(t1, c, work, 0))
					chip->nonce_ranges_done++;
			}
		}
	} else {
		g_cmd_fails[cid]++;
		if (g_cmd_fails[cid] > MAX_CMD_FAILS) {
			// TODO: replaced with mcompat_spi_reset()
			applog(LOG_ERR, "Chain %d reset spihub", cid);
			hub_spi_clean_chain(cid);
			g_cmd_resets[cid]++;
			if (g_cmd_resets[cid] > MAX_CMD_RESETS) {
				applog(LOG_ERR, "Chain %d is not working due to multiple resets.",
				       cid);
				reinit_T1_chain(t1, cid);
			}
		}
	}

	/* Temperature control */
	chain_temp_status = dm_tempctrl_update_chain_temp(cid);

	cgpu->temp_min = (double)g_chain_tmp[cid].tmp_lo;
	cgpu->temp_max = (double)g_chain_tmp[cid].tmp_hi;
	cgpu->temp     = (double)g_chain_tmp[cid].tmp_avg;

	if (chain_temp_status == TEMP_SHUTDOWN) {
		// shut down chain
		applog(LOG_ERR, "DANGEROUS TEMPERATURE(%.0f): power down chain %d",
			cgpu->temp_max, cid);
		/* Only time we power down is for dangerous overheat */
		mcompat_chain_power_down(cid);
		cgpu->status = LIFE_DEAD;
		cgtime(&thr->sick);

		/* Function doesn't currently return */
		T1_overheated_blinking(cid);
	} else if (chain_temp_status == TEMP_WARNING ||
		   (g_chain_tmp[cid].optimal && g_fan_cfg.fan_speed > opt_T1_target))
			t1_throttle(t1, cid);
	else if (t1->throttled && chain_temp_status == TEMP_NORMAL &&
		 g_fan_cfg.fan_speed < opt_T1_target)
			t1_raise(t1, cid);

	/* Tuning */
	if (!thr->work_restart && !t1->throttled && opt_T1auto)
		T1_tune(t1, cid);

	/* read chip temperatures and voltages */
	if (g_debug_stats[cid]) {
		cgsleep_ms(1);
		get_temperatures(t1);
		get_voltages(t1);
		g_debug_stats[cid] = 0;
	}

#ifdef USE_AUTONONCE
	mcompat_cmd_auto_nonce(cid, 1, REG_LENGTH);  // enable auto get nonce
#endif

	/* in case of no progress, prevent busy looping */
	pthread_cond_signal(&t1->cond);

	/* If we haven't slept at least 10ms we are at risk of polling too
	 * often and being CPU bound. Either the chain is too busy or internal
	 * clock based sleep behaviour may be off so revert to simple usleep. */
	if (slept < 10)
		usleep((10 - slept) * 1000);

	return hashes * 0x100000000ull;
}

static int chains_shutdown;

/* Shut down the chains gracefully. We do not want to power them down as it
 * makes the next start unreliable, so we decrease power usage to a minimum. */
static void T1_shutdown(struct thr_info *thr)
{
	struct cgpu_info *cgpu = thr->cgpu;
	struct T1_chain *t1 = cgpu->device_data;
	int cid = t1->chain_id;

	mcompat_set_spi_speed(cid, T1_SPI_SPEED_DEF);

	/* Set a very low frequency. */
	t1_set_pll(t1, CMD_ADDR_BROADCAST, 0);

	/* Set a very low voltage */
	mcompat_set_vid_by_step(cid, t1->iVid, T1_VID_MAX);

	/* Confirm we have actually reset the chains */
	if (!mcompat_set_reset(cid, 0)) {
		applog(LOG_ERR, "Failed to reset chain %d on shutdown", cid);
		return;
	}
	/* Only once all chains are successfully shut down is it safe to turn
	 * down fan speed */
	if (++chains_shutdown < total_chains)
		return;

	pthread_cancel(fan_tid);
	dm_fanctrl_set_fan_speed(FAN_SPEED_PREHEAT);
}

static void T1_get_statline_before(char *buf, size_t len, struct cgpu_info *cgpu)
{
	struct T1_chain *t1 = cgpu->device_data;
	char temp[10];
	if (cgpu->temp != 0)
		snprintf(temp, 9, "%2.0fC", cgpu->temp);
	tailsprintf(buf, len, " %2d:%2d/%3d %s",
		    t1->chain_id, t1->num_active_chips, t1->num_cores,
	     cgpu->temp == 0 ? "   " : temp);
}

static struct api_data *T1_api_stats(struct cgpu_info *cgpu)
{
	struct T1_chain *t1 = cgpu->device_data;
	int fan_speed = g_fan_cfg.fan_speed;
	unsigned long long int chipmap = 0;
	struct api_data *root = NULL;
	bool fake;
	int i;
	char s[32];

	ROOT_ADD_API(int, "Chain ID", t1->chain_id, false);
	ROOT_ADD_API(int, "Num chips", t1->num_chips, false);
	ROOT_ADD_API(int, "Num cores", t1->num_cores, false);
	ROOT_ADD_API(int, "Num active chips", t1->num_active_chips, false);
	ROOT_ADD_API(int, "Chain skew", t1->chain_skew, false);
	ROOT_ADD_API(double, "Temp max", cgpu->temp_max, false);
	ROOT_ADD_API(double, "Temp min", cgpu->temp_min, false);
	ROOT_ADD_API(int, "Fan duty", fan_speed, true);
	ROOT_ADD_API(int, "iVid", t1->iVid, false);
	ROOT_ADD_API(int, "PLL", t1->pll, false);
	ROOT_ADD_API(double, "Voltage Max", s_reg_ctrl.highest_vol[t1->chain_id], false);
	ROOT_ADD_API(double, "Voltage Min", s_reg_ctrl.lowest_vol[t1->chain_id], false);
	ROOT_ADD_API(double, "Voltage Avg", s_reg_ctrl.average_vol[t1->chain_id], false);
	ROOT_ADD_API(bool, "VidOptimal", t1->VidOptimal, false);
	ROOT_ADD_API(bool, "pllOptimal", t1->pllOptimal, false);
	ROOT_ADD_API(int, "Chain num", cgpu->chainNum, false);
	ROOT_ADD_API(double, "MHS av", cgpu->mhs_av, false);
	ROOT_ADD_API(bool, "Disabled", t1->disabled, false);
	fake = !!t1->throttled;
	ROOT_ADD_API(bool, "Throttled", fake, true);
	for (i = 0; i < t1->num_chips; i++) {
		if (!t1->chips[i].disabled)
			chipmap |= 1 << i;
	}
	sprintf(s, "%Lx", chipmap);
	ROOT_ADD_API(string, "Enabled chips", s[0], true);
	ROOT_ADD_API(double, "Temp", cgpu->temp, false);

	for (i = 0; i < t1->num_chips; i++) {
		sprintf(s, "%02d HW errors", i);
		ROOT_ADD_API(int, s, t1->chips[i].hw_errors, true);
		sprintf(s, "%02d Stales", i);
		ROOT_ADD_API(int, s, t1->chips[i].stales, true);
		sprintf(s, "%02d Duplicates", i);
		ROOT_ADD_API(int, s, t1->chips[i].dupes, true);
		sprintf(s, "%02d Nonces found", i);
		ROOT_ADD_API(int, s, t1->chips[i].nonces_found, true);
		sprintf(s, "%02d Nonce ranges", i);
		ROOT_ADD_API(int, s, t1->chips[i].nonce_ranges_done, true);
		sprintf(s, "%02d Cooldown", i);
		ROOT_ADD_API(int, s, t1->chips[i].cooldown_begin, true);
		sprintf(s, "%02d Fail count", i);
		ROOT_ADD_API(int, s, t1->chips[i].fail_count, true);
		sprintf(s, "%02d Fail reset", i);
		ROOT_ADD_API(int, s, t1->chips[i].fail_reset, true);
		sprintf(s, "%02d Temp", i);
		ROOT_ADD_API(int, s, t1->chips[i].temp, true);
		sprintf(s, "%02d nVol", i);
		ROOT_ADD_API(int, s, t1->chips[i].nVol, true);
	}
	return root;
}

static struct api_data *T1_api_debug(struct cgpu_info *cgpu)
{
	struct T1_chain *t1 = cgpu->device_data;
	int timeout = 1000;

	g_debug_stats[t1->chain_id] = 1;

	// Wait for g_debug_stats cleared or timeout
	while (g_debug_stats[t1->chain_id] && timeout) {
		timeout -= 10;
		cgsleep_ms(10);
	}

	return T1_api_stats(cgpu);
}

struct device_drv dragonmintT1_drv = {
	.drv_id = DRIVER_dragonmintT1,
	.dname = "DragonmintT1",
	.name = "DT1",
	.drv_detect = T1_detect,
	/* Set to lowest diff we can reliably use to get accurate hashrates. */
	.max_diff = 129,

	.hash_work = hash_driver_work,
	.scanwork = T1_scanwork,
	.thread_shutdown = T1_shutdown,
	.get_api_stats = T1_api_stats,
	.get_api_debug = T1_api_debug,
	.get_statline_before = T1_get_statline_before,
};
