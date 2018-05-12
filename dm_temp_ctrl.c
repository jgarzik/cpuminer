/*
 * Copyright 2018 Duan Hao
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 3 of the License, or (at your option)
 * any later version.  See COPYING for more details.
 */

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>

#include "dragonmint_t1.h"

#include "dm_temp_ctrl.h"

/******************************************************************************
 * Macros & Constants
 ******************************************************************************/
#define INVALID_TEMP			(9999)

/******************************************************************************
 * Global variables
 ******************************************************************************/
volatile c_temp_cfg	g_tmp_cfg;			// configs of temperature control
volatile c_temp		g_chain_tmp[MAX_CHAIN_NUM];	// current temperature per chain

volatile int		g_tmp_last_upd_time[MAX_CHAIN_NUM];
static uint32_t 	g_temp_status[MAX_CHAIN_NUM];

/******************************************************************************
 * Prototypes
 ******************************************************************************/

/******************************************************************************
 * Implementations
 ******************************************************************************/
/******************************************************************************
 * Function:	dm_tempctrl_get_defcfg
 * Description:	get default configs for temerature control
 * Arguments:	p_cfg		temperature configs
 * Return:		none
 ******************************************************************************/
void dm_tempctrl_get_defcfg(c_temp_cfg *p_cfg)
{
	p_cfg->tmp_min		= -40;
	p_cfg->tmp_max		= 125;
	p_cfg->tmp_target	= 75;
	p_cfg->tmp_thr_lo	= 30;
	p_cfg->tmp_thr_hi	= 95;
	p_cfg->tmp_thr_warn	= 105;
	p_cfg->tmp_thr_pd	= 115;
	p_cfg->tmp_exp_time	= 2000; // 2s
}


void dm_tempctrl_set(c_temp_cfg *p_cfg)
{
	g_tmp_cfg = *p_cfg;
}

 /******************************************************************************
 * Function:	dm_tempctrl_init
 * Description:	temperature control initializition
 * Arguments:	p_cfg		temperature configs
 * Return:		none
 ******************************************************************************/
void dm_tempctrl_init(c_temp_cfg *p_cfg)
{
	int i;

	// FIXME: add mutex here
	if (NULL == p_cfg) {
		c_temp_cfg cfg;
		dm_tempctrl_get_defcfg(&cfg);  // avoid to pass volatile pointer directly
		dm_tempctrl_set(&cfg);
	} else
		dm_tempctrl_set(p_cfg);

	for(i = 0; i < MAX_CHAIN_NUM; ++i) {
		g_chain_tmp[i].tmp_lo = g_chain_tmp[i].tmp_hi
			= g_chain_tmp[i].tmp_avg = INVALID_TEMP;
	}
}

#ifndef USE_AUTOCMD0A
static void dm_tempctrl_get_chain_temp(int *chip_temp, c_temp *chain_temp)
{
	int i, cnt, avg, index = -1;

	int compr_desc(const void *a, const void *b) {
		return (*(int*)b - *(int*)a);
	}
	/* Sort descending */
	qsort(chip_temp, g_chip_num, sizeof(int), compr_desc);

	cnt = avg = 0;
	for (i = 0; i < g_chip_num; ++i) {
		if (chip_temp[i] < g_tmp_cfg.tmp_max && chip_temp[i] > g_tmp_cfg.tmp_min) {
			/* Find the first valid temperature */
			if (index == -1)
				index = i;
			/* Get the average temperature */
			avg += chip_temp[i];
			cnt++;
		}
	}

	if (cnt > 6) {
		/* Ignore the highest one and get average of maximal two tempertures */
		chain_temp->tmp_hi = (chip_temp[index + 1] + chip_temp[index + 2]) >> 1;
		/* Ignore the lowest one and get average of minimal two tempertures */
		chain_temp->tmp_lo = (chip_temp[g_chip_num - 2] + chip_temp[g_chip_num - 3]) >> 1;
		chain_temp->tmp_avg = avg / cnt;
	} else {
		chain_temp->tmp_hi = INVALID_TEMP;
		chain_temp->tmp_lo = INVALID_TEMP;
		chain_temp->tmp_avg = INVALID_TEMP;
	}
}
#endif

/******************************************************************************
 * Function:	dm_tempctrl_update_chain_temp
 * Description:	update temperature of single chain
 * Arguments: 	chain_id		chain id
 * Return:	device temperature state
 ******************************************************************************/

uint32_t dm_tempctrl_update_chain_temp(int chain_id)
{
	uint32_t *tstatus = &g_temp_status[chain_id];
	c_temp chain_temp;

	/* Do not read temperature unless given time has passed or the last
	 * reading was invalid. Return the last value in that case. */
	int curr_time = get_current_ms();
	if (curr_time - g_tmp_last_upd_time[chain_id] < g_tmp_cfg.tmp_exp_time &&
	    *tstatus != (uint32_t)TEMP_INVALID)
		goto out;

	g_tmp_last_upd_time[chain_id] = curr_time;

	// FIXME: add mutex here
#ifdef USE_AUTOCMD0A
	if (!mcompat_get_chain_temp(chain_id, &chain_temp))
		applog(LOG_ERR, "chain%d: failed to read chain temperature", chain_id);
#else
	int chip_temp[MCOMPAT_CONFIG_MAX_CHIP_NUM];
	mcompat_get_chip_temp(chain_id, chip_temp);
	dm_tempctrl_get_chain_temp(chip_temp, &chain_temp);
#endif
	applog(LOG_DEBUG, "chain%d: Tmax=%d, Tmin=%d, Tavg=%d",
		chain_id, chain_temp.tmp_hi, chain_temp.tmp_lo, chain_temp.tmp_avg);

	if (chain_temp.tmp_hi > g_tmp_cfg.tmp_max || chain_temp.tmp_hi < g_tmp_cfg.tmp_min
	 	|| chain_temp.tmp_lo > g_tmp_cfg.tmp_max || chain_temp.tmp_lo < g_tmp_cfg.tmp_min
	 	|| chain_temp.tmp_avg > g_tmp_cfg.tmp_max || chain_temp.tmp_avg < g_tmp_cfg.tmp_min) {
		applog(LOG_ERR, "error temperature ignored: Tmax=%d, Tmin=%d, Tavg=%d",
			chain_temp.tmp_hi, chain_temp.tmp_lo, chain_temp.tmp_avg);
		*tstatus = TEMP_INVALID;
		goto out;
	}

	g_chain_tmp[chain_id] = chain_temp;

	g_chain_tmp[chain_id].optimal = false;
	if (g_chain_tmp[chain_id].tmp_hi >= g_tmp_cfg.tmp_thr_pd)
		*tstatus = TEMP_SHUTDOWN;
	else if (g_chain_tmp[chain_id].tmp_hi >= g_tmp_cfg.tmp_thr_warn)
		*tstatus = TEMP_WARNING;
	else if (g_chain_tmp[chain_id].tmp_hi >= g_tmp_cfg.tmp_thr_hi)
		*tstatus = TEMP_TOO_HIGH;
	else if (g_chain_tmp[chain_id].tmp_lo < g_tmp_cfg.tmp_thr_lo)
		*tstatus = TEMP_TOO_LOW;
	else {
		if (g_chain_tmp[chain_id].tmp_avg > g_tmp_cfg.tmp_target - TEMP_TOLERANCE)
			g_chain_tmp[chain_id].optimal = true;
		*tstatus = TEMP_NORMAL;
	}
out:
	return *tstatus;
}

/******************************************************************************
 * Function:	dm_tempctrl_update_temp
 * Description:	update temperature of one or more chains
 * Arguments: 	chain_mask		chain id mask
 * Return:		device temperture state
 ******************************************************************************/
void dm_tempctrl_update_temp(uint8_t chain_mask)
{
	int i;

	for (i = 0; i < MAX_CHAIN_NUM; ++i)
	{
		if(chain_mask & (1 << i))
		{
			dm_tempctrl_update_chain_temp(i);
		}
	}
}
