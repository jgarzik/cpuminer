/*
 * Copyright 2018 Duan Hao
 * Copyright 2018 Con Kolivas <kernel@kolivas.org>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 3 of the License, or (at your option)
 * any later version.  See COPYING for more details.
 */

#ifndef _DM_FAN_CTRL_H_
#define _DM_FAN_CTRL_H_

#define FAN_SPEED_MAX			(100)			// max fan speed (%)
#define FAN_SPEED_MIN			(0)			// min fan speed (%)
#define FAN_SPEED_DEF			(80)			// default fan speed (%)
#define FAN_SPEED_PREHEAT		(10)			// preheat fan speed

typedef enum _FAN_MODE
{
	FAN_MODE_MANUAL	= 0,				// manual fan control mode
	FAN_MODE_AUTO	= 1,				// auto fan control mode
} FAN_MODE;

typedef enum _FAN_PROFILE
{
	FAN_PF_NORMAL	= 0,				// normal fan control
	FAN_PF_OVERHEAT	= 1,				// overheat fan control
	FAN_PF_PREHEAT	= 2,				// preheat fan control
} FAN_PROFILE;

typedef struct _c_fan_cfg
{
	char fan_mode;						// fan mode: auto / manual
	char fan_speed;						// fan speed (percent)
	char fan_speed_preheat;				// preheat fan speed (percent)
	char fan_ctrl_cycle;				// time interval between fan controls (s)
	char tmp_chk_span;					// time span of temperature rising checks (s)
	bool preheat;						// true if preheat is enabled
} c_fan_cfg;

extern volatile c_fan_cfg	g_fan_cfg;		// fan config
extern volatile int			g_fan_profile;	// fan profile: normal / overheat / preheat
	

void dm_fanctrl_get_defcfg(c_fan_cfg *p_cfg);
void dm_fanctrl_init(c_fan_cfg *p_cfg);
void dm_fanctrl_set_fan_speed(char speed);
void *dm_fanctrl_thread(void *argv);

#endif // _DM_FAN_CTRL_H_

