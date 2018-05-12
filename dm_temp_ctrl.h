/*
 * Copyright 2018 Duan Hao
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 3 of the License, or (at your option)
 * any later version.  See COPYING for more details.
 */

#ifndef _DM_TEMP_CTRL_H_
#define _DM_TEMP_CTRL_H_

#include "dragonmint_t1.h"

#define TEMP_INVALID				(-1)
#define TEMP_NORMAL				(0)
#define TEMP_TOO_LOW				(1)
#define TEMP_TOO_HIGH				(2)
#define TEMP_WARNING				(3)
#define TEMP_SHUTDOWN				(4)

#define TEMP_TOLERANCE			(5)

typedef struct _c_temp_cfg
{
	short	tmp_min;		// min value of temperature
	short	tmp_max;		// max value of temperature
	short	tmp_target;		// target temperature
	short	tmp_thr_lo;		// low temperature threshold
	short	tmp_thr_hi;		// high temperature threshold
	short	tmp_thr_warn;		// warning threshold
	short	tmp_thr_pd;		// power down threshold
	int	tmp_exp_time;	// temperature expiring time (ms)
} c_temp_cfg;

extern volatile c_temp_cfg	g_tmp_cfg;			// configs of temperature control
extern volatile c_temp		g_chain_tmp[MAX_CHAIN_NUM];	// current temperature for each chain

void		dm_tempctrl_get_defcfg(c_temp_cfg *p_cfg);
void		dm_tempctrl_set(c_temp_cfg *p_cfg);
void		dm_tempctrl_init(c_temp_cfg *p_cfg);
void		dm_tempctrl_update_temp(uint8_t chain_mask);
uint32_t	dm_tempctrl_update_chain_temp(int chain_id);
 
#endif // _DM_TEMP_CTRL_H_

