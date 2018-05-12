/*
 * Copyright 2018 Duan Hao
 * Copyright 2018 Con Kolivas <kernel@kolivas.org>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 3 of the License, or (at your option)
 * any later version.  See COPYING for more details.
 */

/******************************************************************************
 * Description:	fan control using simple PID
 ******************************************************************************/

#include <stdio.h>
#include <stdint.h>
#include <unistd.h>

#include "dragonmint_t1.h"

#include "dm_temp_ctrl.h"
#include "dm_fan_ctrl.h"

/******************************************************************************
 * Macros & Constants
 ******************************************************************************/
#define FAN_MODE_DEF			FAN_MODE_AUTO		// default fan control mode

#define WORK_CYCLE_DEF			(2)			// default time interval between temperature checks
#define DEV_TMP_CHK_CNT			(3)
#define DEV_TMP_CHK_SPAN		(6)
#define TIMEOUT_GET_TMP			(3)

/******************************************************************************
 * Global variables
 ******************************************************************************/
volatile c_fan_cfg	g_fan_cfg;				// fan config
volatile int		g_fan_profile;			// fan profile: normal / overheat / preheat

static c_temp			g_dev_tmp;	// device temperature sequence
static c_temp			g_dev_last_tmp;	// device temperature sequence

extern int			chain_flag[MAX_CHAIN_NUM];

/******************************************************************************
 * Prototypes
 ******************************************************************************/
static bool dm_fanctrl_get_tmp(void);
static void dm_fanctrl_update_fan_speed(void);
static bool dm_fanctrl_check_overheat(void);
static bool dm_fanctrl_check_preheat(void);


/******************************************************************************
 * Implementations
 ******************************************************************************/
void dm_fanctrl_get_defcfg(c_fan_cfg *p_cfg)
{
	p_cfg->fan_mode = FAN_MODE_DEF;
	p_cfg->fan_speed = FAN_SPEED_DEF;
	p_cfg->fan_speed_preheat = FAN_SPEED_PREHEAT;
	p_cfg->fan_ctrl_cycle = WORK_CYCLE_DEF;
	p_cfg->preheat = true;
}

void dm_fanctrl_init(c_fan_cfg *p_cfg)
{
	if (NULL == p_cfg) {
		c_fan_cfg cfg;
		dm_fanctrl_get_defcfg(&cfg);  // avoid to pass volatile pointer directly
		g_fan_cfg = cfg;
	} else
		g_fan_cfg = *p_cfg;

	g_fan_profile = FAN_PF_NORMAL;
	g_dev_tmp.tmp_avg = g_dev_last_tmp.tmp_avg = g_tmp_cfg.tmp_target;
}

void *dm_fanctrl_thread(void __maybe_unused *argv)
{
	int timeout_get_tmp = 0;

	// set default fan speed
//	dm_fanctrl_set_fan_speed(g_fan_cfg.fan_speed);

	while(true) {
		if (dm_fanctrl_get_tmp()) {
			dm_fanctrl_update_fan_speed();
			timeout_get_tmp = 0;
		} else
			timeout_get_tmp++;

		// force fan speed to 100% when failed to get temperature
		if (timeout_get_tmp >= TIMEOUT_GET_TMP && g_fan_cfg.fan_speed < FAN_SPEED_MAX) {
			applog(LOG_WARNING,
				"WARNING: unable to read temperature, force fan speed to %d", FAN_SPEED_MAX);
			dm_fanctrl_set_fan_speed(FAN_SPEED_MAX);
			timeout_get_tmp = 0;
		}

		sleep(g_fan_cfg.fan_ctrl_cycle);
	}

	return NULL;
}

void dm_fanctrl_set_fan_speed(char speed)
{
	if (speed > FAN_SPEED_MAX)
		speed = FAN_SPEED_MAX;
	else if (speed < g_fan_cfg.fan_speed_preheat)
		speed = g_fan_cfg.fan_speed_preheat;

	if (speed != g_fan_cfg.fan_speed) {
		g_fan_cfg.fan_speed = speed;
		mcompat_fan_speed_set(0, g_fan_cfg.fan_speed);   // fan id is ignored
		applog(LOG_ERR, "fan speed set to %d", g_fan_cfg.fan_speed);
	}
}

static bool dm_fanctrl_get_tmp(void)
{
	bool retval = false;
	int  i, chain_num = 0;
	c_temp dev_temp;

	// init
	chain_num = 0;
	dev_temp.tmp_hi	 = g_tmp_cfg.tmp_min;
	dev_temp.tmp_lo  = g_tmp_cfg.tmp_max;
	dev_temp.tmp_avg = 0;

	for(i = 0; i < MAX_CHAIN_NUM; ++i) {
		if (chain_flag[i]
			&& g_chain_tmp[i].tmp_avg > g_tmp_cfg.tmp_min
			&& g_chain_tmp[i].tmp_avg < g_tmp_cfg.tmp_max) {
			// temperature stat.
			dev_temp.tmp_lo = MIN(dev_temp.tmp_lo, g_chain_tmp[i].tmp_lo);
			dev_temp.tmp_hi = MAX(dev_temp.tmp_hi, g_chain_tmp[i].tmp_hi);
			dev_temp.tmp_avg = MAX(dev_temp.tmp_avg, g_chain_tmp[i].tmp_avg);
			chain_num++;
		}
	}

	if (chain_num > 0) {
		g_dev_tmp = dev_temp;

		retval = true;
	}

	return retval;
}

static bool dm_fanctrl_check_overheat(void)
{
	int tmp_tolerance = 0;

	// if already in overheat mode, apply a small tolerance
	if (FAN_PF_OVERHEAT == g_fan_profile)
		tmp_tolerance = TEMP_TOLERANCE;

	// overheat mode: force to max fan speed while tmp_hi >= tmp_thr_hi
	if (g_dev_tmp.tmp_hi >= g_tmp_cfg.tmp_thr_hi - tmp_tolerance) {
		dm_fanctrl_set_fan_speed(FAN_SPEED_MAX);
		if (FAN_PF_OVERHEAT != g_fan_profile) {
			g_fan_profile = FAN_PF_OVERHEAT;
			applog(LOG_ERR, "OVERHEAT: temp_hi over %d, force fan speed to %d", 
				g_tmp_cfg.tmp_thr_hi, FAN_SPEED_MAX);
		}
		return true;
	}

	g_fan_profile = FAN_PF_NORMAL;

	return false;
}

static bool dm_fanctrl_check_preheat(void)
{
	int tmp_tolerance = 0;

	// preheat mode: do preheating when tmp_avg < tmp_thr_lo
	if (FAN_PF_PREHEAT != g_fan_profile)
		tmp_tolerance = TEMP_TOLERANCE;

	if (g_dev_tmp.tmp_avg < g_tmp_cfg.tmp_thr_lo - tmp_tolerance) {
		dm_fanctrl_set_fan_speed(FAN_SPEED_PREHEAT);
		g_fan_profile = FAN_PF_PREHEAT;
		applog(LOG_ERR, "PREHEAT: tmp_avg under %d, force fan speed to %d", 
			g_tmp_cfg.tmp_thr_lo, FAN_SPEED_PREHEAT);
		return true;
	}

	g_fan_profile = FAN_PF_NORMAL;

	return false;
}

static int8_t last_tmp_rise[8];
static int64_t *last_tmp_int = (int64_t *)last_tmp_rise;
static int tmp_rise_cnt;

static void dm_fanctrl_update_fan_speed(void)
{
	int fan_speed;
	int delta_tmp_avg, delta_tmp_hi;
	int tmp_rise, hi_raise;

	// detect overheat first
	if (dm_fanctrl_check_overheat())
		return;
	
	// preheat
	if (g_fan_cfg.preheat && dm_fanctrl_check_preheat())
		return;

	// check average temperature rising to determining fan speed target
	tmp_rise = g_dev_tmp.tmp_avg - g_dev_last_tmp.tmp_avg;
	delta_tmp_avg = g_dev_tmp.tmp_avg - g_tmp_cfg.tmp_target;
	hi_raise = g_dev_tmp.tmp_hi - g_dev_last_tmp.tmp_hi;
	delta_tmp_hi = g_dev_tmp.tmp_hi - g_tmp_cfg.tmp_thr_hi;

	/* If we have a hot spot, use that for fan speed control
	 * instead of the average temperature */
	if (hi_raise > tmp_rise || delta_tmp_hi > delta_tmp_avg) {
		tmp_rise = hi_raise;
		delta_tmp_avg = delta_tmp_hi;
	}

	g_dev_last_tmp.tmp_avg = g_dev_tmp.tmp_avg;
	g_dev_last_tmp.tmp_hi = g_dev_tmp.tmp_hi;
	g_dev_last_tmp.tmp_lo = g_dev_tmp.tmp_lo;

	if (delta_tmp_avg > 0) {
		/* Over target temperature */

		/* Is the temp already coming down */
		if (tmp_rise < 0)
			goto out;
		/* Adjust fanspeed by temperature over and any further rise */
		fan_speed = g_fan_cfg.fan_speed + delta_tmp_avg + tmp_rise;
	} else {
		/* Below target temperature */
		int diff = tmp_rise;

		if (tmp_rise > 0) {
			int divisor = -delta_tmp_avg / TEMP_TOLERANCE + 1;

			/* Adjust fanspeed by temperature change proportional to
			 * diff from optimal. */
			diff /= divisor;
		} else if (!tmp_rise) {
			/* Is the temp below optimal and unchanging, gently
			 * lower speed. Allow tighter temperature tolerance if
			 * temperature is unchanged for longer. */
			if ((g_dev_tmp.tmp_avg < g_tmp_cfg.tmp_target - TEMP_TOLERANCE) ||
			    (!(*last_tmp_int) && (g_dev_tmp.tmp_avg < g_tmp_cfg.tmp_target))) {
				*last_tmp_int = 0xFFFFFFFFFFFFFFFF;
				diff -= 1;
			}
		}
		fan_speed = g_fan_cfg.fan_speed + diff;
	}

	// set fan speed
	dm_fanctrl_set_fan_speed(fan_speed);
out:
	last_tmp_rise[(tmp_rise_cnt++) % 8] = tmp_rise;
}

