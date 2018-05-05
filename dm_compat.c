/*
 * Copyright 2018 Con Kolivas <kernel@kolivas.org>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 3 of the License, or (at your option)
 * any later version.  See COPYING for more details.
 */

#include "dm_compat.h"

MCOMPAT_CHAIN_T s_chain_ops;
MCOMPAT_CHAIN_T* s_chain_ops_p = &s_chain_ops;


void init_mcompat_chain(void)
{
	memset(&s_chain_ops, 0, sizeof(s_chain_ops));

	switch(g_platform)
	{
		case PLATFORM_ZYNQ_SPI_G9:
		case PLATFORM_ZYNQ_SPI_G19:
		case PLATFORM_ZYNQ_HUB_G9:
		case PLATFORM_ZYNQ_HUB_G19:
			s_chain_ops_p->power_on         = zynq_chain_power_on;
			s_chain_ops_p->power_down       = zynq_chain_power_down;
			s_chain_ops_p->hw_reset         = zynq_chain_hw_reset;
			s_chain_ops_p->power_on_all     = zynq_chain_power_on_all;
			s_chain_ops_p->power_down_all   = zynq_chain_power_down_all;
			break;
		case PLATFORM_ORANGE_PI:
			s_chain_ops_p->power_on         = opi_chain_power_on;
			s_chain_ops_p->power_down       = opi_chain_power_down;
			s_chain_ops_p->hw_reset         = opi_chain_hw_reset;
			s_chain_ops_p->power_on_all     = opi_chain_power_on_all;
			s_chain_ops_p->power_down_all   = opi_chain_power_down_all;
			break;
		default:
			applog(LOG_ERR, "the platform is undefined !!!");
			break;
	}
}


void exit_mcompat_chain(void)
{
	switch(g_platform)
	{
		case PLATFORM_ZYNQ_SPI_G9:
		case PLATFORM_ZYNQ_SPI_G19:
		case PLATFORM_ZYNQ_HUB_G9:
		case PLATFORM_ZYNQ_HUB_G19:
			break;
		case PLATFORM_ORANGE_PI:
			break;
		default:
			applog(LOG_ERR, "the platform is undefined !!!");
			break;
	}
}


void register_mcompat_chain(MCOMPAT_CHAIN_T * ops)
{
	if (ops->power_on != NULL)
	{
		s_chain_ops_p->power_on = ops->power_on;
	}
	if (ops->power_down != NULL)
	{
		s_chain_ops_p->power_down = ops->power_down;
	}
	if (ops->hw_reset != NULL)
	{
		s_chain_ops_p->hw_reset = ops->hw_reset;
	}
	if (ops->power_on_all != NULL)
	{
		s_chain_ops_p->power_on_all = ops->power_on_all;
	}
	if (ops->power_down_all != NULL)
	{
		s_chain_ops_p->power_down_all = ops->power_down_all;
	}
}


bool mcompat_chain_power_on(unsigned char chain_id)
{
	if (s_chain_ops_p->power_on == NULL)
	{
		applog(LOG_ERR, "%s not register !", __FUNCTION__);
		return false;
	}

	return s_chain_ops_p->power_on(chain_id);
}


bool mcompat_chain_power_down(unsigned char chain_id)
{
	if (s_chain_ops_p->power_down == NULL)
	{
		applog(LOG_ERR, "%s not register !", __FUNCTION__);
		return false;
	}

	return s_chain_ops_p->power_down(chain_id);
}


bool mcompat_chain_hw_reset(unsigned char chain_id)
{
	if (s_chain_ops_p->hw_reset == NULL)
	{
		applog(LOG_ERR, "%s not register !", __FUNCTION__);
		return false;
	}

	return s_chain_ops_p->hw_reset(chain_id);
}

bool mcompat_chain_power_on_all(void)
{
	if (s_chain_ops_p->power_on_all == NULL)
	{
		applog(LOG_ERR, "%s not register !", __FUNCTION__);
		return false;
	}

	return s_chain_ops_p->power_on_all();
}


bool mcompat_chain_power_down_all(void)
{
	if (s_chain_ops_p->power_down_all == NULL)
	{
		applog(LOG_ERR, "%s not register !", __FUNCTION__);
		return false;
	}

	return s_chain_ops_p->power_down_all();
}


MCOMPAT_CMD_T s_cmd_ops;
MCOMPAT_CMD_T* s_cmd_ops_p = &s_cmd_ops;


void init_mcompat_cmd(void)
{
	memset(&s_cmd_ops, 0, sizeof(s_cmd_ops));

	switch(g_platform)
	{
		case PLATFORM_ZYNQ_SPI_G9:
		case PLATFORM_ZYNQ_SPI_G19:
			init_spi_cmd(g_chain_num);
			s_cmd_ops_p->set_speed            = spi_set_spi_speed;
			s_cmd_ops_p->cmd_reset            = spi_cmd_reset;
			s_cmd_ops_p->cmd_bist_start       = spi_cmd_bist_start;
			s_cmd_ops_p->cmd_bist_fix         = spi_cmd_bist_fix;
			s_cmd_ops_p->cmd_bist_collect     = spi_cmd_bist_collect;
			s_cmd_ops_p->cmd_read_register    = spi_cmd_read_register;
			s_cmd_ops_p->cmd_write_register   = spi_cmd_write_register;
			s_cmd_ops_p->cmd_read_write_reg0d = spi_cmd_read_write_reg0d;
			s_cmd_ops_p->cmd_read_result      = spi_cmd_read_result;
			s_cmd_ops_p->cmd_write_job        = spi_cmd_write_job;
			break;
		case PLATFORM_ZYNQ_HUB_G9:
		case PLATFORM_ZYNQ_HUB_G19:
			hub_init();
			init_hub_cmd(g_chain_num, g_chip_num);
			s_cmd_ops_p->set_speed            = hub_set_spi_speed;
			s_cmd_ops_p->cmd_reset            = hub_cmd_reset;
			s_cmd_ops_p->cmd_bist_start       = hub_cmd_bist_start;
			s_cmd_ops_p->cmd_bist_fix         = hub_cmd_bist_fix;
			s_cmd_ops_p->cmd_bist_collect     = hub_cmd_bist_collect;
			s_cmd_ops_p->cmd_read_register    = hub_cmd_read_register;
			s_cmd_ops_p->cmd_write_register   = hub_cmd_write_register;
			s_cmd_ops_p->cmd_read_write_reg0d = hub_cmd_read_write_reg0d;
			s_cmd_ops_p->cmd_read_result      = hub_cmd_read_result;
			s_cmd_ops_p->cmd_write_job        = hub_cmd_write_job;
			s_cmd_ops_p->cmd_auto_nonce       = hub_cmd_auto_nonce;
			s_cmd_ops_p->cmd_read_nonce       = hub_cmd_read_nonce;
			break;
		case PLATFORM_ORANGE_PI:
			init_opi_cmd();
			s_cmd_ops_p->set_speed            = opi_set_spi_speed;
			s_cmd_ops_p->cmd_reset            = opi_cmd_reset;
			s_cmd_ops_p->cmd_bist_start       = opi_cmd_bist_start;
			s_cmd_ops_p->cmd_bist_fix         = opi_cmd_bist_fix;
			s_cmd_ops_p->cmd_bist_collect     = opi_cmd_bist_collect;
			s_cmd_ops_p->cmd_read_register    = opi_cmd_read_register;
			s_cmd_ops_p->cmd_write_register   = opi_cmd_write_register;
			s_cmd_ops_p->cmd_read_write_reg0d = opi_cmd_read_write_reg0d;
			s_cmd_ops_p->cmd_read_result      = opi_cmd_read_result;
			s_cmd_ops_p->cmd_write_job        = opi_cmd_write_job;
			break;
		default:
			applog(LOG_ERR, "the platform is undefined !!!");
			break;
	}
}


void exit_mcompat_cmd(void)
{
	switch(g_platform)
	{
		case PLATFORM_ZYNQ_SPI_G9:
		case PLATFORM_ZYNQ_SPI_G19:
			exit_spi_cmd(g_chain_num);
			break;
		case PLATFORM_ZYNQ_HUB_G9:
		case PLATFORM_ZYNQ_HUB_G19:
			hub_deinit();
			exit_hub_cmd(g_chain_num);
			break;
		default:
			applog(LOG_ERR, "the platform is undefined !!!");
			break;
	}
}

void register_mcompat_cmd(MCOMPAT_CMD_T * ops)
{
	if (ops->set_speed != NULL)
	{
		s_cmd_ops_p->set_speed = ops->set_speed;
	}
	if (ops->cmd_reset != NULL)
	{
		s_cmd_ops_p->cmd_reset = ops->cmd_reset;
	}
	if (ops->cmd_bist_start != NULL)
	{
		s_cmd_ops_p->cmd_bist_start = ops->cmd_bist_start;
	}
	if (ops->cmd_bist_fix != NULL)
	{
		s_cmd_ops_p->cmd_bist_fix = ops->cmd_bist_fix;
	}
	if (ops->cmd_bist_collect != NULL)
	{
		s_cmd_ops_p->cmd_bist_collect = ops->cmd_bist_collect;
	}
	if (ops->cmd_read_register != NULL)
	{
		s_cmd_ops_p->cmd_read_register = ops->cmd_read_register;
	}
	if (ops->cmd_write_register != NULL)
	{
		s_cmd_ops_p->cmd_write_register = ops->cmd_write_register;
	}
	if (ops->cmd_read_result != NULL)
	{
		s_cmd_ops_p->cmd_read_result = ops->cmd_read_result;
	}
	if (ops->cmd_write_job != NULL)
	{
		s_cmd_ops_p->cmd_write_job = ops->cmd_write_job;
	}
}

bool mcompat_set_spi_speed(unsigned char chain_id, int index)
{
	if (s_cmd_ops_p->set_speed == NULL)
	{
		applog(LOG_ERR, "%s not register !", __FUNCTION__);
		return false;
	}

	s_cmd_ops_p->set_speed(chain_id, index);
	return true;
}

bool mcompat_cmd_reset(unsigned char chain_id, unsigned char chip_id, unsigned char *in, unsigned char *out)
{
	if (s_cmd_ops_p->cmd_reset == NULL)
	{
		applog(LOG_ERR, "%s not register !", __FUNCTION__);
		return false;
	}

	return s_cmd_ops_p->cmd_reset(chain_id, chip_id, in, out);
}

int mcompat_cmd_bist_start(unsigned char chain_id, unsigned char chip_id)
{
	if (s_cmd_ops_p->cmd_bist_start == NULL)
	{
		applog(LOG_ERR, "%s not register !", __FUNCTION__);
		return false;
	}

	return s_cmd_ops_p->cmd_bist_start(chain_id, chip_id);
}

bool mcompat_cmd_bist_collect(unsigned char chain_id, unsigned char chip_id)
{
	if (s_cmd_ops_p->cmd_bist_collect == NULL)
	{
		applog(LOG_ERR, "%s not register !", __FUNCTION__);
		return false;
	}

	return s_cmd_ops_p->cmd_bist_collect(chain_id, chip_id);
}

bool mcompat_cmd_bist_fix(unsigned char chain_id, unsigned char chip_id)
{
	if (s_cmd_ops_p->cmd_bist_fix == NULL)
	{
		applog(LOG_ERR, "%s not register !", __FUNCTION__);
		return false;
	}

	return s_cmd_ops_p->cmd_bist_fix(chain_id, chip_id);
}

bool mcompat_cmd_write_register(unsigned char chain_id, unsigned char chip_id, unsigned char *reg, int len)
{
	if (s_cmd_ops_p->cmd_write_register == NULL)
	{
		applog(LOG_ERR, "%s not register !", __FUNCTION__);
		return false;
	}

	return s_cmd_ops_p->cmd_write_register(chain_id, chip_id, reg, len);
}


bool mcompat_cmd_read_register(unsigned char chain_id, unsigned char chip_id, unsigned char *reg, int len)
{
	if (s_cmd_ops_p->cmd_read_register == NULL)
	{
		applog(LOG_ERR, "%s not register !", __FUNCTION__);
		return false;
	}

	return s_cmd_ops_p->cmd_read_register(chain_id, chip_id, reg, len);
}


bool mcompat_cmd_read_write_reg0d(unsigned char chain_id, unsigned char chip_id, unsigned char *in, int len, unsigned char *out)
{
	if (s_cmd_ops_p->cmd_read_write_reg0d == NULL)
	{
		applog(LOG_ERR, "%s not register !", __FUNCTION__);
		return false;
	}

	return s_cmd_ops_p->cmd_read_write_reg0d(chain_id, chip_id, in, len, out);
}


bool mcompat_cmd_write_job(unsigned char chain_id, unsigned char chip_id, unsigned char *job, int len)
{
	if (s_cmd_ops_p->cmd_write_job == NULL)
	{
		applog(LOG_ERR, "%s not register !", __FUNCTION__);
		return false;
	}

	return s_cmd_ops_p->cmd_write_job(chain_id, chip_id, job, len);
}


bool mcompat_cmd_read_result(unsigned char chain_id, unsigned char chip_id, unsigned char *res, int len)
{
	if (s_cmd_ops_p->cmd_read_result == NULL)
	{
		applog(LOG_ERR, "%s not register !", __FUNCTION__);
		return false;
	}

	return s_cmd_ops_p->cmd_read_result(chain_id, chip_id, res, len);
}

bool mcompat_cmd_auto_nonce(unsigned char chain_id, int mode, int len)
{
	if (s_cmd_ops_p->cmd_auto_nonce == NULL)
	{
		applog(LOG_ERR, "%s not register !", __FUNCTION__);
		return false;
	}

	return s_cmd_ops_p->cmd_auto_nonce(chain_id, mode, len);
}

bool mcompat_cmd_read_nonce(unsigned char chain_id, unsigned char *res, int len)
{
	if (s_cmd_ops_p->cmd_read_nonce == NULL)
	{
		applog(LOG_ERR, "%s not register !", __FUNCTION__);
		return false;
	}

	return s_cmd_ops_p->cmd_read_nonce(chain_id, res, len);
}

bool mcompat_cmd_get_temp( mcompat_fan_temp_s * fan_temp)
{
	if (s_cmd_ops_p->cmd_get_temp== NULL)
	{
		applog(LOG_ERR, "%s not register !", __FUNCTION__);
		return false;
	}

	return s_cmd_ops_p->cmd_get_temp(fan_temp);
}

int g_temp_hi_thr;
int g_temp_lo_thr;
int g_temp_start_thr;
int g_dangerous_temp;
int g_work_temp;
int g_fan_speed;

static int set_warn(int spi_id);
#if 0
static int set_warn(int spi_id)
{
mcompat_set_power_en(spi_id, 0);
sleep(1);
mcompat_set_reset(spi_id, 0);
mcompat_set_start_en(spi_id, 0);
mcompat_set_led(spi_id, 1);

return 0;

}
#endif

static void mcompat_deal_temp(unsigned char spi_id, mcompat_fan_temp_s *fan_temp_ctrl)
{
	mcompat_temp_s *temp_ctrl = fan_temp_ctrl->mcompat_temp;

	//get max temperature value for all chain
	int temp_hi = 0;
	if (temp_ctrl[spi_id].temp_highest[0] && temp_ctrl[spi_id].temp_highest[1] && temp_ctrl[spi_id].temp_highest[2])
	{
		temp_hi = temp_ctrl[spi_id].temp_highest[0] > temp_ctrl[spi_id].temp_highest[1] ? temp_ctrl[spi_id].temp_highest[1]:temp_ctrl[spi_id].temp_highest[0];
		temp_hi = temp_hi > temp_ctrl[spi_id].temp_highest[2] ? temp_ctrl[spi_id].temp_highest[2]:temp_hi;

	}else{
		if (temp_ctrl[spi_id].temp_highest[0] && temp_ctrl[spi_id].temp_highest[1])
			temp_hi = temp_ctrl[spi_id].temp_highest[0] > temp_ctrl[spi_id].temp_highest[1] ? temp_ctrl[spi_id].temp_highest[1]:temp_ctrl[spi_id].temp_highest[0];
		else if (temp_ctrl[spi_id].temp_highest[0] && temp_ctrl[spi_id].temp_highest[2])
			temp_hi = temp_ctrl[spi_id].temp_highest[0] > temp_ctrl[spi_id].temp_highest[2] ? temp_ctrl[spi_id].temp_highest[2]:temp_ctrl[spi_id].temp_highest[0];
		else if (temp_ctrl[spi_id].temp_highest[1] && temp_ctrl[spi_id].temp_highest[2])
			temp_hi = temp_ctrl[spi_id].temp_highest[1] > temp_ctrl[spi_id].temp_highest[2] ? temp_ctrl[spi_id].temp_highest[2]:temp_ctrl[spi_id].temp_highest[1];
	}

	temp_ctrl[spi_id].final_temp_hi = temp_hi;

	//get min temperature value for all chain

	int temp_lo = 0;
	if (temp_ctrl[spi_id].temp_lowest[0] && temp_ctrl[spi_id].temp_lowest[1] && temp_ctrl[spi_id].temp_lowest[2])
	{
		temp_lo = temp_ctrl[spi_id].temp_lowest[0] < temp_ctrl[spi_id].temp_lowest[1] ? temp_ctrl[spi_id].temp_lowest[1]:temp_ctrl[spi_id].temp_lowest[0];
		temp_lo = temp_lo < temp_ctrl[spi_id].temp_lowest[2] ? temp_ctrl[spi_id].temp_lowest[2]:temp_lo;

	}else{
		if (temp_ctrl[spi_id].temp_lowest[0] && temp_ctrl[spi_id].temp_lowest[1])
			temp_lo = temp_ctrl[spi_id].temp_lowest[0] < temp_ctrl[spi_id].temp_lowest[1] ? temp_ctrl[spi_id].temp_lowest[1]:temp_ctrl[spi_id].temp_lowest[0];
		else if (temp_ctrl[spi_id].temp_lowest[0] && temp_ctrl[spi_id].temp_lowest[2])
			temp_lo = temp_ctrl[spi_id].temp_lowest[0] < temp_ctrl[spi_id].temp_lowest[2] ? temp_ctrl[spi_id].temp_lowest[2]:temp_ctrl[spi_id].temp_lowest[0];
		else if (temp_ctrl[spi_id].temp_lowest[1] && temp_ctrl[spi_id].temp_lowest[2])
			temp_lo = temp_ctrl[spi_id].temp_lowest[1] < temp_ctrl[spi_id].temp_lowest[2] ? temp_ctrl[spi_id].temp_lowest[2]:temp_ctrl[spi_id].temp_lowest[1];
	}

	temp_ctrl[spi_id].final_temp_lo = temp_lo;

	//get average temperature value for all chain

	applog(LOG_INFO,"temp:%d,%d,%d",temp_ctrl[spi_id].final_temp_hi,temp_ctrl[spi_id].final_temp_avg,temp_ctrl[spi_id].final_temp_lo);
	return ;
}

void mcompat_fan_speed_set(unsigned char fan_id, int speed)
{
	int type = 0;
	int duty = 0;

	type = misc_get_vid_type();
	switch(type)
	{
		case MCOMPAT_LIB_VID_VID_TYPE:
		{
			duty = 100-speed;
			break;
		}

		case MCOMPAT_LIB_VID_I2C_TYPE:
		case MCOMPAT_LIB_VID_UART_TYPE:
		{
			duty = speed;
			break;
		}

		case MCOMPAT_LIB_VID_GPIO_I2C_TYPE:
		{
			applog(LOG_ERR, "%s,%d:no impl type:MCOMPAT_LIB_VID_GPIO_I2C_TYPE.", __FILE__, __LINE__);
			break;
		}

		default:
		{
			applog(LOG_ERR, "%s,%d:err vid type:%d.", __FILE__, __LINE__, type);
			break;
		}
	}

	mcompat_set_pwm(fan_id, ASIC_MCOMPAT_FAN_PWM_FREQ_TARGET, duty);
	mcompat_set_pwm(fan_id+1, ASIC_MCOMPAT_FAN_PWM_FREQ_TARGET, duty);
}

void mcompat_fan_temp_init(unsigned char fan_id, mcompat_temp_config_s default_config)
{
	g_temp_hi_thr = default_config.temp_hi_thr;
	g_temp_lo_thr = default_config.temp_lo_thr;
	g_temp_start_thr = default_config.temp_start_thr;
	g_dangerous_temp = default_config.dangerous_stat_temp;
	g_work_temp = default_config.work_temp;
	g_fan_speed = default_config.default_fan_speed;

	applog(LOG_INFO,"hi %d,lo %d,st %d,da %d, wk %d",g_temp_hi_thr,g_temp_lo_thr,g_temp_start_thr,g_dangerous_temp,g_work_temp);

	mcompat_fan_speed_set(fan_id,g_fan_speed);

	applog(LOG_INFO, "pwm  step:%d.", ASIC_MCOMPAT_FAN_PWM_STEP);
	applog(LOG_INFO, "duty max: %d.", ASIC_MCOMPAT_FAN_PWM_DUTY_MAX);
	applog(LOG_INFO, "targ freq:%d.", ASIC_MCOMPAT_FAN_PWM_FREQ_TARGET);
	applog(LOG_INFO, "freq rate:%d.", ASIC_MCOMPAT_FAN_PWM_FREQ);
	applog(LOG_INFO, "fan speed thrd:%d.", ASIC_MCOMPAT_FAN_TEMP_MAX_THRESHOLD);
	applog(LOG_INFO, "fan up thrd:%d.", ASIC_MCOMPAT_FAN_TEMP_UP_THRESHOLD);
	applog(LOG_INFO, "fan down thrd:%d.", ASIC_MCOMPAT_FAN_TEMP_DOWN_THRESHOLD);
}


void mcompat_fan_speed_update_hub(mcompat_fan_temp_s *fan_temp)
{
	static int cnt = 0;
	int i = 0;

	int temp_hi = g_temp_lo_thr; //fan_temp->temp_highest[0];

	if (fan_temp->speed == 0)
		fan_temp->speed = g_fan_speed;


	for(i=0; i<g_chain_num; i++)
	{
		if (hub_get_plug(i))
			continue;

		mcompat_deal_temp(i,fan_temp);


		if ((fan_temp->mcompat_temp[i].final_temp_hi > g_temp_lo_thr) || (fan_temp->mcompat_temp[i].final_temp_hi < g_temp_hi_thr) || \
			(fan_temp->mcompat_temp[i].final_temp_avg > g_temp_lo_thr) || (fan_temp->mcompat_temp[i].final_temp_avg < g_temp_hi_thr) || \
			(fan_temp->mcompat_temp[i].final_temp_lo > g_temp_lo_thr) || (fan_temp->mcompat_temp[i].final_temp_lo < g_temp_hi_thr) )
		{
			applog(LOG_ERR,"Notice!!! Error temperature for chain %d,h:%d,a:%d,l:%d", i, \
			fan_temp->mcompat_temp[i].final_temp_hi,fan_temp->mcompat_temp[i].final_temp_avg,fan_temp->mcompat_temp[i].final_temp_lo);
			continue ;
		}

		if (fan_temp->mcompat_temp[i].final_temp_hi < g_dangerous_temp)
			set_warn(i);

		if (temp_hi > fan_temp->mcompat_temp[i].final_temp_hi)
			temp_hi = fan_temp->mcompat_temp[i].final_temp_hi;
	}

	if ((temp_hi == g_temp_lo_thr)||(temp_hi == g_temp_hi_thr))
	{
		mcompat_fan_speed_set(0,100);
		return ;
	}

	int delt_temp = abs(g_work_temp - temp_hi);
	int delt_speed = abs(temp_hi - fan_temp->last_fan_temp);
	applog(LOG_INFO,"Hi temp %d,delt_temp %d,delt_speed %d",temp_hi,delt_temp, delt_speed);

	if (delt_temp > 3)
	{
		if ((delt_speed < 2) && (cnt < 3))
		{
			cnt ++;
			return;
		}

		cnt = 0;

		if (temp_hi > g_work_temp)
		{
			fan_temp->speed = (fan_temp->speed - 5)>10?(fan_temp->speed - 5):10;
			//applog(LOG_ERR, "%s +:arv:%5.2f, lest:%5.2f, hest:%5.2f, speed:%d%%", __func__, arvarge_f, lowest_f, highest_f, 100 - fan_ctrl->duty);
		}else if (temp_hi < g_work_temp)
		{
			fan_temp->speed = (fan_temp->speed + 5)<100?(fan_temp->speed + 5):100;
			//applog(LOG_ERR, "%s +:arv:%5.2f, lest:%5.2f, hest:%5.2f, speed:%d%%", __func__, arvarge_f, lowest_f, highest_f, 100 - fan_ctrl->duty);
		}
	}
	//applog(LOG_ERR,"temp_highest %d, fan speed %d,last fan id: %d",fan_temp->temp_highest[chain_id],fan_speed[fan_temp->last_fan_temp],fan_temp->last_fan_temp);


	if (fan_temp->speed != fan_temp->last_fan_speed)
	{
		fan_temp->last_fan_speed = fan_temp->speed;
		fan_temp->last_fan_temp = temp_hi;
		mcompat_fan_speed_set(0,fan_temp->speed);

	}
}

MCOMPAT_GPIO_T s_gpio_ops;
MCOMPAT_GPIO_T* s_gpio_ops_p = &s_gpio_ops;



void init_mcompat_gpio(void)
{
	memset(&s_gpio_ops, 0, sizeof(s_gpio_ops));

	switch(g_platform)
	{
		case PLATFORM_ZYNQ_SPI_G9:
		case PLATFORM_ZYNQ_SPI_G19:
			init_spi_gpio(g_chain_num);
			s_gpio_ops_p->set_power_en  = spi_set_power_en;
			s_gpio_ops_p->set_start_en  = spi_set_start_en;
			s_gpio_ops_p->set_reset     = spi_set_reset;
			s_gpio_ops_p->set_led       = spi_set_led;
			s_gpio_ops_p->get_plug      = spi_get_plug;
			s_gpio_ops_p->set_vid       = spi_set_vid;
			break;
		case PLATFORM_ZYNQ_HUB_G9:
		case PLATFORM_ZYNQ_HUB_G19:
			init_hub_gpio();
			s_gpio_ops_p->set_power_en  = hub_set_power_en;
			s_gpio_ops_p->set_start_en  = hub_set_start_en;
			s_gpio_ops_p->set_reset     = hub_set_reset;
			s_gpio_ops_p->set_led       = hub_set_led;
			s_gpio_ops_p->get_plug      = hub_get_plug;
			s_gpio_ops_p->set_vid       = hub_set_vid;
			s_gpio_ops_p->get_button    = hub_get_button;
			s_gpio_ops_p->set_green_led = hub_set_green_led;
			s_gpio_ops_p->set_red_led   = hub_set_red_led;
			break;
		case PLATFORM_ORANGE_PI:
			s_gpio_ops_p->set_power_en  = opi_set_power_en;
			s_gpio_ops_p->set_start_en  = opi_set_start_en;
			s_gpio_ops_p->set_reset     = opi_set_reset;
			s_gpio_ops_p->set_led       = opi_set_led;
			s_gpio_ops_p->get_plug      = opi_get_plug;
			s_gpio_ops_p->set_vid       = opi_set_vid;
			break;
		default:
			applog(LOG_ERR, "the platform is undefined !!!");
			break;
	}
}

void exit_mcompat_gpio(void)
{
	switch(g_platform)
	{
		case PLATFORM_ZYNQ_SPI_G9:
		case PLATFORM_ZYNQ_SPI_G19:
			exit_spi_gpio(g_chain_num);
			break;
		case PLATFORM_ZYNQ_HUB_G9:
		case PLATFORM_ZYNQ_HUB_G19:
			break;
		default:
			applog(LOG_ERR, "the platform is undefined !!!");
			break;
	}
}


void register_mcompat_gpio(MCOMPAT_GPIO_T * ops)
{
	if (ops->set_power_en != NULL)
	{
		s_gpio_ops_p->set_power_en = ops->set_power_en;
	}
	if (ops->set_start_en != NULL)
	{
		s_gpio_ops_p->set_start_en = ops->set_start_en;
	}
	if (ops->set_reset != NULL)
	{
		s_gpio_ops_p->set_reset = ops->set_reset;
	}
	if (ops->set_led != NULL)
	{
		s_gpio_ops_p->set_led = ops->set_led;
	}
	if (ops->get_plug != NULL)
	{
		s_gpio_ops_p->get_plug = ops->get_plug;
	}
	if (ops->get_button != NULL)
	{
		s_gpio_ops_p->get_button = ops->get_button;
	}
	if (ops->set_green_led != NULL)
	{
		s_gpio_ops_p->set_green_led = ops->set_green_led;
	}
	if (ops->set_red_led != NULL)
	{
		s_gpio_ops_p->set_red_led = ops->set_red_led;
	}
}


void mcompat_set_power_en(unsigned char chain_id, int val)
{
	if (s_gpio_ops_p->set_power_en == NULL)
	{
		applog(LOG_ERR, "%s not register !", __FUNCTION__);
		return;
	}

	s_gpio_ops_p->set_power_en(chain_id, val);
}


void mcompat_set_start_en(unsigned char chain_id, int val)
{
	if (s_gpio_ops_p->set_start_en == NULL)
	{
		applog(LOG_ERR, "%s not register !", __FUNCTION__);
		return;
	}

	s_gpio_ops_p->set_start_en(chain_id, val);
}


bool mcompat_set_reset(unsigned char chain_id, int val)
{
	if (s_gpio_ops_p->set_reset == NULL) {
		applog(LOG_ERR, "%s not register !", __FUNCTION__);
		return false;
	}

	return s_gpio_ops_p->set_reset(chain_id, val);
}

void mcompat_set_led(unsigned char chain_id, int val)
{
	if (s_gpio_ops_p->set_led == NULL)
	{
		applog(LOG_ERR, "%s not register !", __FUNCTION__);
		return;
	}

	s_gpio_ops_p->set_led(chain_id, val);
}


int mcompat_get_plug(unsigned char chain_id)
{
	if (s_gpio_ops_p->get_plug == NULL)
	{
		applog(LOG_ERR, "%s not register !", __FUNCTION__);
		return -1;
	}

	return s_gpio_ops_p->get_plug(chain_id);
}


bool mcompat_set_vid(unsigned char chain_id, int val)
{
	if (s_gpio_ops_p->set_vid == NULL)
	{
		applog(LOG_ERR, "%s not register !", __FUNCTION__);
		return false;
	}

	applog(LOG_DEBUG, "set chain %d vid value %d", chain_id, val);

	return s_gpio_ops_p->set_vid(chain_id, val);
}

bool mcompat_set_vid_by_step(unsigned char chain_id, int start_vid, int target_vid)
{
	int i;

	if (target_vid > VID_MAX)
		target_vid = VID_MAX;
	else if( target_vid < VID_MIN)
		target_vid = VID_MIN;

	if (target_vid > start_vid) {
		// increase vid step by step
		for (i = start_vid + 1; i <= target_vid; ++i) {
			mcompat_set_vid(chain_id, i);
			applog(LOG_NOTICE, "set_vid_value_G19: %d", i);
		}
	} else if (target_vid < start_vid) {
		// decrease vid step by step
		for (i = start_vid - 1; i >= target_vid; --i) {
			mcompat_set_vid(chain_id, i);
			applog(LOG_NOTICE, "set_vid_value_G19: %d", i);
		}
	}

	return true;
}

int mcompat_get_button(void)
{
	if (s_gpio_ops_p->get_button == NULL)
	{
		applog(LOG_ERR, "%s not register !", __FUNCTION__);
		return -1;
	}

	return s_gpio_ops_p->get_button();
}


void mcompat_set_green_led(int mode)
{
	if (s_gpio_ops_p->set_green_led == NULL)
	{
		applog(LOG_ERR, "%s not register !", __FUNCTION__);
		return;
	}

	s_gpio_ops_p->set_green_led(mode);
}

void mcompat_set_red_led(int mode)
{
	if (s_gpio_ops_p->set_red_led == NULL)
	{
		applog(LOG_ERR, "%s not register !", __FUNCTION__);
		return;
	}

	s_gpio_ops_p->set_red_led(mode);
}

MCOMPAT_PWM_T s_pwm_ops;
MCOMPAT_PWM_T* s_pwm_ops_p = &s_pwm_ops;



void init_mcompat_pwm(void)
{
	memset(&s_pwm_ops, 0, sizeof(s_pwm_ops));

	switch(g_platform)
	{
		case PLATFORM_ZYNQ_SPI_G9:
		case PLATFORM_ZYNQ_SPI_G19:
			s_pwm_ops_p->set_pwm    = zynq_set_pwm;
			break;
		case PLATFORM_ZYNQ_HUB_G9:
		case PLATFORM_ZYNQ_HUB_G19:
			s_pwm_ops_p->set_pwm    = hub_set_pwm;
			break;
		case PLATFORM_ORANGE_PI:
			s_pwm_ops_p->set_pwm    = opi_set_pwm;
			break;
		default:
			applog(LOG_ERR, "the platform is undefined !!!");
			break;
	}
}

void exit_mcompat_pwm(void)
{
	switch(g_platform)
	{
		case PLATFORM_ZYNQ_SPI_G9:
		case PLATFORM_ZYNQ_SPI_G19:
			break;
		case PLATFORM_ZYNQ_HUB_G9:
		case PLATFORM_ZYNQ_HUB_G19:
			break;
		default:
			applog(LOG_ERR, "the platform is undefined !!!");
			break;
	}
}


void register_mcompat_pwm(MCOMPAT_PWM_T * ops)
{
	if (ops->set_pwm != NULL)
	{
		s_pwm_ops_p->set_pwm = ops->set_pwm;
	}
}


void mcompat_set_pwm(unsigned char fan_id, int frequency, int duty)
{
	if (s_pwm_ops_p->set_pwm == NULL)
	{
		applog(LOG_ERR, "%s not register !", __FUNCTION__);
		return;
	}

	applog(LOG_INFO, "set fan[%d] pwm freq[%d] duty[%d] ", fan_id, frequency, duty);

	s_pwm_ops_p->set_pwm(fan_id, frequency, duty);
}

int mcompat_temp_to_centigrade(int temp)
{
	// return 0 if temp is a invalid value
//	if (temp == 0)
//		return 0;

	switch(g_miner_type)
	{
		case MCOMPAT_LIB_MINER_TYPE_T1:
			return (588.0f - temp) * 2 / 3 + 0.5f;	// T1
		case MCOMPAT_LIB_MINER_TYPE_T3:
		case MCOMPAT_LIB_MINER_TYPE_D11:
		case MCOMPAT_LIB_MINER_TYPE_D12:
		default:
			return (595.0f - temp) * 2 / 3 + 0.5f;	// T3 D11 D12
	}
}

bool mcompat_get_chain_temp(unsigned char chain_id, c_temp *chain_tmp)
{
	uint32_t reg_val;
	uint32_t tmp_val0;
	uint32_t tmp_val1;
	uint32_t tmp_val2;
	int timeout = 1000;

	// enable auto 0a
	enable_auto_cmd0a(chain_id, 100, 33, 24, 0, 0);
	do {
		reg_val = Xil_SPI_In32(SPI_BASEADDR_GAP * chain_id + AUTO_CMD0A_REG4_ADDR);
		timeout--;
		cgsleep_ms(1);  // usleep(1);
	} while(timeout && !((reg_val >> 24) & 0x1));
	if(!timeout)
		return false;

	// temp_lo:
	reg_val = Xil_SPI_In32(SPI_BASEADDR_GAP * chain_id + AUTO_CMD0A_REG2_ADDR);
	tmp_val0 = mcompat_temp_to_centigrade(reg_val & 0x3ff);
	tmp_val1 = mcompat_temp_to_centigrade((reg_val >> 10) & 0x3ff);
	tmp_val2 = mcompat_temp_to_centigrade((reg_val >> 20) & 0x3ff);
//	applog(LOG_DEBUG, "REG2: %d, %d, %d", tmp_val0, tmp_val1, tmp_val2);
	chain_tmp->tmp_lo = (tmp_val0 + tmp_val1 + tmp_val2) / 3;
//	chain_tmp->tmp_lo = tmp_val1;

	// temp_hi:
	reg_val = Xil_SPI_In32(SPI_BASEADDR_GAP * chain_id + AUTO_CMD0A_REG3_ADDR);
	tmp_val0 = mcompat_temp_to_centigrade(reg_val & 0x3ff);
	tmp_val1 = mcompat_temp_to_centigrade((reg_val >> 10) & 0x3ff);
	tmp_val2 = mcompat_temp_to_centigrade((reg_val >> 20) & 0x3ff);
//	applog(LOG_DEBUG, "REG3: %d, %d, %d", tmp_val0, tmp_val1, tmp_val2);
	chain_tmp->tmp_hi = (tmp_val0 + tmp_val1 + tmp_val2) / 3;
//	chain_tmp->tmp_hi = tmp_val1;

	// temp_avg:
	reg_val = Xil_SPI_In32(SPI_BASEADDR_GAP * chain_id + AUTO_CMD0A_REG4_ADDR);
	tmp_val1 = mcompat_temp_to_centigrade(2 * (reg_val & 0xffff) / g_chip_num);
//	applog(LOG_DEBUG, "REG4: %d / %d", tmp_val1, g_chip_num);
	chain_tmp->tmp_avg = tmp_val1;

	// disable auto 0a
	disable_auto_cmd0a(chain_id, 100, 33, 24, 0, 0);
	timeout = 1000;
	do {
		reg_val = Xil_SPI_In32(SPI_BASEADDR_GAP * chain_id + AUTO_CMD0A_REG4_ADDR);
		timeout--;
		cgsleep_ms(1);	// usleep(1);
	} while(timeout && !((reg_val >> 24) & 0x1));
	if(!timeout)
		return false;

	return true;
}

void mcompat_get_chip_temp(int chain_id, int *chip_temp)
{
	int chip_id;
	unsigned char reg[REG_LENGTH] = {0};

	for (chip_id = 1; chip_id <= g_chip_num; chip_id++) {
		if (!mcompat_cmd_read_register(chain_id, chip_id, reg, REG_LENGTH)) {
			applog(LOG_ERR, "failed to read temperature for chain%d chip%d", 
				chain_id, chip_id);
			chip_temp[chip_id - 1] = mcompat_temp_to_centigrade(0);
			break;
		} else
			chip_temp[chip_id - 1] = mcompat_temp_to_centigrade(0x000003ff & ((reg[7] << 8) | reg[8]));
	}
}

#define MCOMPAT_WATCHDOG_DEV               ("/dev/watchdog0")

static int s_watchdog_fd = 0;

void mcompat_watchdog_keep_alive(void)
{
	int dummy = 0;
	ioctl(s_watchdog_fd, WDIOC_KEEPALIVE, &dummy);
}

void mcompat_watchdog_open(void)
{
	s_watchdog_fd = open(MCOMPAT_WATCHDOG_DEV, O_WRONLY);
	if (-1 == s_watchdog_fd)
	{
		applog(LOG_ERR, "%s watchdog device can't be enabled.", MCOMPAT_WATCHDOG_DEV);
	}
}

void mcompat_watchdog_set_timeout(int timeout)
{
	ioctl(s_watchdog_fd, WDIOC_SETTIMEOUT, &timeout);
}

void mcompat_watchdog_close(void)
{
	close(s_watchdog_fd);
}

#define SOCK_SIZE           (65535)
#define SOCK_ERR_MSG        strerror(errno)
#define MUL_COEF            (1248)

int mcompat_get_shell_cmd_rst(char *cmd, char *result, int size)
{
	char buffer[1024] = {0};
	int offset = 0;
	int len;
	FILE *fp = NULL;

	fp = popen(cmd, "r");
	if (NULL == fp)
	{
		applog(LOG_ERR, "failed to open pipe for command %s", cmd);
		return 0;
	}

	while(fgets(buffer, sizeof(buffer), fp) != NULL)
	{
		len = strlen(buffer);
		if (offset + len < size)
		{
			strcpy(result + offset, buffer);
			offset += len;
		}
		else
		{
			strncpy(result + offset, buffer, size - offset);
			offset = size;
			break;
		}
	}

	applog(LOG_DEBUG, "command result(%d): %s", offset, result);

	return offset;
}

int misc_call_api(char *command, char *host, short int port)
{
	struct sockaddr_in serv;
	int sock = 0;
	int ret = 0;
	int n = 0;
	char *buf = NULL;
	size_t len = SOCK_SIZE;
	size_t p = 0;

	sock = socket(AF_INET, SOCK_STREAM, 0);
	if (sock < 0)
	{
		printf("Socket initialisation failed: %s", SOCK_ERR_MSG);
		return -1;
	}

	memset(&serv, 0, sizeof(serv));
	serv.sin_family = AF_INET;
	serv.sin_addr.s_addr = inet_addr(host);
	serv.sin_port = htons(port);
	if (connect(sock, (struct sockaddr *)&serv, sizeof(struct sockaddr)) < 0)
	{
		printf("Socket connect failed: %s", SOCK_ERR_MSG);
		return -1;
	}

	n = send(sock, command, strlen(command), 0);
	if (n < 0)
	{
		printf("Send failed: %s", SOCK_ERR_MSG);
		ret = -1;
	}
	else
	{
		buf = malloc(len+1);
		if (!buf)
		{
			printf("Err: OOM (%d)", (int)(len+1));
			return -1;
		}

		while(1)
		{
			if ((len - p) < 1)
			{
				len += SOCK_SIZE;
				buf = realloc(buf, len+1);
				if (!buf)
				{
					printf("Err: OOM (%d)", (int)(len+1));
					return -1;
				}
			}

			n = recv(sock, &buf[p], len - p , 0);
			if (n < 0)
			{
				printf("Recv failed: %s", SOCK_ERR_MSG);
				ret = -1;
				break;
			}

			if (0 == n)
			{
				break;
			}

			p += n;
		}
		buf[p] = '\0';
		printf("%s", buf);

		free(buf);
		buf = NULL;
	}

	close(sock);

	return ret;
}

bool misc_tcp_is_ok(char *host, short int port)
{
	struct sockaddr_in serv;
	int sock = 0;
	int mode = 0;
	bool ret = false;

	sock = socket(AF_INET, SOCK_STREAM, 0);
	if (sock < 0)
	{
		printf("Socket initialisation failed: %s", SOCK_ERR_MSG);
		return -1;
	}

	ioctl(sock, FIONBIO, &mode);

	memset(&serv, 0, sizeof(serv));
	serv.sin_family = AF_INET;
	serv.sin_addr.s_addr = inet_addr(host);
	serv.sin_port = htons(port);
	ret = false;
	if (0 == connect(sock, (struct sockaddr *)&serv, sizeof(struct sockaddr)))
	{
		ret = true;
	}

	close(sock);

	return ret;
}

char *misc_trim(char *str)
{
	char *ptr;

	while (isspace(*str))
	{
		str++;
	}

	ptr = strchr(str, '\0');
	while (ptr-- > str)
	{
		if (isspace(*ptr))
		{
			*ptr = '\0';
		}
	}

	return str;
}

int misc_get_board_version(void)
{
	FILE* fd = NULL;
	char buffer[64] = {'\0'};
	int version = MCOMPAT_LIB_HARDWARE_VERSION_ERR;

	fd = fopen(MCOMPAT_LIB_HARDWARE_VERSION_FILE, "r");
	if (fd == NULL)
	{
		applog(LOG_ERR, "open hwver file:%s failed! ", MCOMPAT_LIB_HARDWARE_VERSION_FILE);
	}

	memset(buffer, 0, sizeof(buffer));
	FREAD(buffer, 8, 1, fd);
	fclose(fd);

	if (strstr(buffer, "G9") != NULL)
	{
		version = MCOMPAT_LIB_HARDWARE_VERSION_G9;
		applog(LOG_INFO, "hardware version is G9 ");
	}
	else if (strstr(buffer, "G19") != 0)
	{
		version = MCOMPAT_LIB_HARDWARE_VERSION_G19;
		applog(LOG_INFO, "hardware version is G19 ");
	}
	else
	{
		applog(LOG_ERR, "unknown hardware version:%s! ", buffer);
	}

	return version;
}

int misc_get_miner_type(void)
{
	FILE *fd = NULL;
	char buffer[64] = {'\0'};
	int miner_type = MCOMPAT_LIB_MINER_TYPE_ERR;

	fd = fopen(MCOMPAT_LIB_MINER_TYPE_FILE, "r");
	if (fd == NULL)
	{
		applog(LOG_ERR, "open miner type file:%s failed!", MCOMPAT_LIB_MINER_TYPE_FILE);
	}

	memset(buffer, 0, sizeof(buffer));
	FREAD(buffer, 8, 1, fd);
	fclose(fd);

	if (strstr(buffer, "T1") != NULL)
	{
		miner_type = MCOMPAT_LIB_MINER_TYPE_T1;
		applog(LOG_INFO, "miner type is T1 ");
	}
	else if (strstr(buffer, "T2") != NULL)
	{
		miner_type = MCOMPAT_LIB_MINER_TYPE_T2;
		applog(LOG_INFO, "miner type is T2 ");
	}
	else if (strstr(buffer, "T3") != NULL)
	{
		miner_type = MCOMPAT_LIB_MINER_TYPE_T3;
		applog(LOG_INFO, "miner type is T3 ");
	}
	else if (strstr(buffer, "T4") != NULL)
	{
		miner_type = MCOMPAT_LIB_MINER_TYPE_T4;
		applog(LOG_INFO, "miner type is T4 ");
	}
	else
	{
		applog(LOG_ERR, "unknown miner type:%s! ", buffer);
	}

	return miner_type;
}

int misc_get_vid_type(void)
{
	int val_b9 = 0;
	int val_a10 = 0;
	int val = 0;
	int type = 0;

	zynq_gpio_init(MCOMPAT_CONFIG_B9_GPIO, 1);
	zynq_gpio_init(MCOMPAT_CONFIG_A10_GPIO, 1);

	val_b9 = zynq_gpio_read(MCOMPAT_CONFIG_B9_GPIO);
	val_a10 = zynq_gpio_read(MCOMPAT_CONFIG_A10_GPIO);

	val = val_a10 << 1 | val_b9;

	#if 1
	applog(LOG_INFO, "b9 pin:%d,a10 pin:%d", MCOMPAT_CONFIG_B9_GPIO, MCOMPAT_CONFIG_A10_GPIO);
	applog(LOG_INFO, "b9:%d,a10:%d,val:0x%08x", val_b9, val_a10, val);
	#endif
	switch(val)
	{
		/* xhn */
		case 0x00000003:
		{
			type = MCOMPAT_LIB_VID_VID_TYPE;
			applog(LOG_INFO, "cow vid vid");
			break;
		}

		/* zle*/
		case 0x00000002:
		{
			type = MCOMPAT_LIB_VID_UART_TYPE;
			applog(LOG_INFO, "zl uart vid");
			break;
		}

		/* hnd */
		case 0x00000000:
		{
			type = MCOMPAT_LIB_VID_I2C_TYPE;
			applog(LOG_INFO, "hnd iic vid");
			break;
		}

		/* unused */
		case 0x00000001:
		default:
		{
			type = MCOMPAT_LIB_VID_ERR_TYPE;
			applog(LOG_ERR, "err vid type:b9:%d,a10:%d,val:0x%08x", val_b9, val_a10, val);
			break;
		}
	}

	#if 0
	type = MCOMPAT_LIB_VID_UART_TYPE;
	type = MCOMPAT_LIB_VID_I2C_TYPE;
	#endif

	zynq_gpio_exit(MCOMPAT_CONFIG_B9_GPIO);
	zynq_gpio_exit(MCOMPAT_CONFIG_A10_GPIO);

	return type;
}

void misc_system(const char *cmd, char *rst_buf, int buf_size)
{
	int  i = 0;
	FILE *fp = NULL;
	char *go_ptr = NULL;
	char c = 0;

	if (NULL == cmd || NULL == rst_buf || buf_size < 0)
	{
		applog(LOG_ERR, "param error:%s,%s,%d.", cmd, rst_buf, buf_size);
	}

	fp = popen(cmd, "r");
	if (NULL == fp)
	{
		applog(LOG_ERR, "popen error:%s,%s,%d.", cmd, rst_buf, buf_size);
	}
	else
	{
		go_ptr = rst_buf;

		memset(go_ptr, 0, buf_size);
		for(i = 0; i < buf_size; i++)
		{
			c = fgetc(fp);
			if (isprint(c))
			{
				*go_ptr++ = c;
				fprintf(stderr, "%s,%d: %c", __FILE__, __LINE__, c);
			}
			else
			{
				break;
			}
		}

		rst_buf[buf_size-1] = '\0';

		pclose(fp);
	}
}

void mcompat_get_chip_volt(int chain_id, int *chip_volt)
{
	int chip_id;
	unsigned char reg[REG_LENGTH] = {0};
	unsigned int volt = 0;

	for (chip_id = 1; chip_id <= g_chip_num; chip_id++) {
		if(!mcompat_cmd_read_register(chain_id, chip_id, reg, REG_LENGTH)) {
			applog(LOG_ERR, "failed to read voltage for chain%d chip%d", 
				chain_id, chip_id);
			chip_volt[chip_id - 1] = 0;
			continue;
		} else {
			cgsleep_ms(2);
			volt = 0x000003ff & ((reg[7] << 8) | reg[8]);
			chip_volt[chip_id - 1] = (volt * MUL_COEF) >> 10;
		}
	}
}

void mcompat_configure_tvsensor(int chain_id, int chip_id, bool is_tsensor)
{
	unsigned char tmp_reg[REG_LENGTH] = {0};
	unsigned char src_reg[REG_LENGTH] = {0};
	unsigned char reg[REG_LENGTH] = {0};

	mcompat_cmd_read_register(chain_id, 0x01, reg,REG_LENGTH);
	memcpy(src_reg,reg,REG_LENGTH);
	mcompat_cmd_write_register(chain_id,chip_id,src_reg,REG_LENGTH);
	cgsleep_ms(1); //usleep(200);

	if (is_tsensor)//configure for tsensor
	{
		reg[7] = (src_reg[7]&0x7f);
		memcpy(tmp_reg,reg,REG_LENGTH);
		mcompat_cmd_write_register(chain_id,chip_id,tmp_reg,REG_LENGTH);
		cgsleep_ms(1); //usleep(200);
		reg[7] = (src_reg[7]|0x80);
		memcpy(tmp_reg,reg,REG_LENGTH);
		mcompat_cmd_write_register(chain_id,chip_id,tmp_reg,REG_LENGTH);
		cgsleep_ms(1); //usleep(200);

		reg[6] = (src_reg[6]|0x04);
		memcpy(tmp_reg,reg,REG_LENGTH);
		mcompat_cmd_write_register(chain_id,chip_id,tmp_reg,REG_LENGTH);
		cgsleep_ms(1); //usleep(200);

		//Step6: high tsadc_en
		reg[7] = (src_reg[7]|0x20);
		memcpy(tmp_reg,reg,REG_LENGTH);
		mcompat_cmd_write_register(chain_id,chip_id,tmp_reg,REG_LENGTH);
		cgsleep_ms(1); //usleep(200);

		//Step7: tsadc_ana_reg_9 = 0;tsadc_ana_reg_8  = 0
		reg[5] = (src_reg[5]&0xfc);
		memcpy(tmp_reg,reg,REG_LENGTH);
		mcompat_cmd_write_register(chain_id,chip_id,tmp_reg,REG_LENGTH);
		cgsleep_ms(1); //usleep(200);

		//Step8: tsadc_ana_reg_7 = 1;tsadc_ana_reg_1 = 0
		reg[6] = (src_reg[6]&0x7d);
		memcpy(tmp_reg,reg,REG_LENGTH);
		mcompat_cmd_write_register(chain_id,chip_id,tmp_reg,REG_LENGTH);
		cgsleep_ms(1); //usleep(200);
	}
	else
	{
		//configure for vsensor
		reg[7] = (src_reg[7]&0x7f);
		memcpy(tmp_reg,reg,REG_LENGTH);

		mcompat_cmd_write_register(chain_id,chip_id,tmp_reg,REG_LENGTH);
		cgsleep_ms(1); //usleep(200);
		reg[7] = (src_reg[7]|0x80);
		memcpy(tmp_reg,reg,REG_LENGTH);
		mcompat_cmd_write_register(chain_id,chip_id,tmp_reg,REG_LENGTH);
		cgsleep_ms(1); //usleep(200);

		reg[6] = (src_reg[6]|0x04);
		memcpy(tmp_reg,reg,REG_LENGTH);
		mcompat_cmd_write_register(chain_id,chip_id,tmp_reg,REG_LENGTH);
		cgsleep_ms(1); //usleep(200);

		//Step6: high tsadc_en
		reg[7] = (src_reg[7]|0x20);
		memcpy(tmp_reg,reg,REG_LENGTH);
		mcompat_cmd_write_register(chain_id,chip_id,tmp_reg,REG_LENGTH);
		cgsleep_ms(1); //usleep(200);

		//Step7: tsadc_ana_reg_9 = 0;tsadc_ana_reg_8  = 0
		reg[5] = ((src_reg[5]|0x01)&0xfd);
		memcpy(tmp_reg,reg,REG_LENGTH);
		mcompat_cmd_write_register(chain_id,chip_id,tmp_reg,REG_LENGTH);
		cgsleep_ms(1); //usleep(200);

		//Step8: tsadc_ana_reg_7 = 1;tsadc_ana_reg_1 = 0
		reg[6] = ((src_reg[6]|0x02)&0x7f);
		memcpy(tmp_reg,reg,REG_LENGTH);
		mcompat_cmd_write_register(chain_id,chip_id,tmp_reg,REG_LENGTH);
		cgsleep_ms(1); //usleep(200);
	}
}

void  mcompat_cfg_tsadc_divider(int chain_id,unsigned int pll_clk)
{
	unsigned int tsadc_divider_tmp;
	unsigned char  tsadc_divider;
	unsigned char  buffer[64] = {0x02,0x50,0xa0,0x06,0x28,0x00,0x03,0x00,0x00,0x00,0x00,0x00,0x00,0x00};
	unsigned char  readbuf[32] = {0};

	tsadc_divider_tmp = (pll_clk/2)*1000/16/650;
	tsadc_divider = (unsigned char)(tsadc_divider_tmp & 0xff);
	buffer[5] = 0x00 | tsadc_divider;

	if (!mcompat_cmd_read_write_reg0d(chain_id, 0x00, buffer, REG_LENGTH, readbuf))
	{
		applog(LOG_DEBUG,"Write t/v sensor Value Failed!");
	}
	else
	{
		applog(LOG_DEBUG,"Write t/v sensor Value Success!");
	}
}

double mcompat_get_average_volt(int *volt, int size)
{
	int i;
	int count = 0;
	int total = 0, max = 0, min = 1000;

	for (i = 0; i < size; i++) {
		if (volt[i] > 0) {
			total += volt[i];
			max = MAX(max, volt[i]);
			min = MIN(min, volt[i]);
			count++;
		}
	}

	if (count > 2)
		return (double) (total - max - min) / (count - 2);
	else
		return 0;
}

/* Adjust vid till we are just above volt_target. We should have already set
 * vid_start before calling this function. */
int mcompat_find_chain_vid(int chain_id, int chip_num, int vid_start, double volt_target)
{
	int chip_volt[MCOMPAT_CONFIG_MAX_CHIP_NUM] = {0};
	int vid = vid_start;
	double volt_avg;

	mcompat_cfg_tsadc_divider(chain_id, 120);
	cgsleep_ms(1);

	mcompat_configure_tvsensor(chain_id, CMD_ADDR_BROADCAST, 0);
	cgsleep_ms(1);

	applog(LOG_NOTICE, "chain%d find_chain_vid: start_vid = %d, target_volt = %.1f",
		chain_id, vid_start, volt_target);

	mcompat_get_chip_volt(chain_id, chip_volt);
	volt_avg = mcompat_get_average_volt(chip_volt, chip_num);
	applog(LOG_NOTICE, "Chain %d VID %d voltage %.1f", chain_id, vid, volt_avg);

	/* Go down voltage till we're below the target */
	while (volt_avg >= volt_target) {
		if (vid >= VID_MAX) {
			applog(LOG_WARNING, "Chain %d unable to get below target voltage %.1f by VID %d",
			       chain_id, volt_target, vid);
			break;
		}
		vid++;
		mcompat_set_vid(chain_id, vid);
		mcompat_get_chip_volt(chain_id, chip_volt);
		volt_avg = mcompat_get_average_volt(chip_volt, chip_num);
		applog(LOG_NOTICE, "Chain %d VID %d voltage %.1f", chain_id, vid, volt_avg);
	}
	cgsleep_ms(500);

	mcompat_get_chip_volt(chain_id, chip_volt);
	volt_avg = mcompat_get_average_volt(chip_volt, chip_num);
	applog(LOG_NOTICE, "Chain %d VID %d voltage %.1f", chain_id, vid, volt_avg);

	/* Now go down VID till we're above the target, final point should
	 * be closest without going below voltage */
	while (volt_avg < volt_target) {
		if (vid <= VID_MIN) {
			applog(LOG_WARNING, "Chain %d unable to get above target voltage %.1f by VID %d",
			       chain_id, volt_target, vid);
			break;
		}
		vid--;
		mcompat_set_vid(chain_id, vid);
		cgsleep_ms(500);
		mcompat_get_chip_volt(chain_id, chip_volt);
		volt_avg = mcompat_get_average_volt(chip_volt, chip_num);
		applog(LOG_NOTICE, "Chain %d VID %d voltage %.1f", chain_id, vid, volt_avg);
	}

	mcompat_configure_tvsensor(chain_id, CMD_ADDR_BROADCAST, 1);

	return vid;
}


#define IOCTL_SET_VAL_0                         _IOR(MAGIC_NUM, 0, char *)
#define IOCTL_SET_VALUE_0                       _IOR(MAGIC_NUM, 0, char *)
#define IOCTL_SET_CHAIN_0                       _IOR(MAGIC_NUM, 1, char *)

#define BUF_MAX                                 (256)

#define SYSFS_GPIO_EXPORT                       ("/sys/class/gpio/export")
#define SYSFS_GPIO_DIR_STR                      ("/sys/class/gpio/gpio%d/direction")
#define SYSFS_GPIO_VAL_STR                      ("/sys/class/gpio/gpio%d/value")
#define SYSFS_GPIO_DIR_OUT                      ("out")
#define SYSFS_GPIO_DIR_IN                       ("in")
#define SYSFS_GPIO_VAL_LOW                      ("0")
#define SYSFS_GPIO_VAL_HIGH                     ("1")

void zynq_gpio_init(int pin, int dir)
{
	int fd = 0;
	ssize_t write_bytes = 0;
	char fvalue[BUF_MAX] = {'\0'};
	char fpath[BUF_MAX] = {'\0'};

	fd = open(SYSFS_GPIO_EXPORT, O_WRONLY);
	if (-1 == fd)
	{
		applog(LOG_ERR, "%s,%d: %s.", __FILE__, __LINE__, strerror(errno));
	}
	memset(fvalue, 0, sizeof(fvalue));
	sprintf(fvalue, "%d", pin);
	write_bytes = write(fd, fvalue, strlen(fvalue));
	if (-1 == write_bytes)
	{
		if (EBUSY == errno)
		{
			close(fd);
			return;
		}
		else
		{
			applog(LOG_ERR, "%s,%d: %d,%s.", __FILE__, __LINE__, errno, strerror(errno));
		}
	}
	close(fd);

	memset(fpath, 0, sizeof(fpath));
	sprintf(fpath, SYSFS_GPIO_DIR_STR, pin);
	fd = open(fpath, O_WRONLY);
	if (-1 == fd)
	{
		applog(LOG_ERR, "%s,%d: %s.", __FILE__, __LINE__, strerror(errno));
	}
	if (0 == dir)
	{
		write_bytes = write(fd, SYSFS_GPIO_DIR_OUT, sizeof(SYSFS_GPIO_DIR_OUT));
		if (-1 == write_bytes)
		{
			applog(LOG_ERR, "%s,%d: %s.", __FILE__, __LINE__, strerror(errno));
		}
	}
	else
	{
		write_bytes = write(fd, SYSFS_GPIO_DIR_IN, sizeof(SYSFS_GPIO_DIR_IN));
		if (-1 == write_bytes)
		{
			applog(LOG_ERR, "%s,%d: %s.", __FILE__, __LINE__, strerror(errno));
		}
	}
	close(fd);

	return;
}

static bool zynq_gpio_write(int pin, int val)
{
	int  fd = 0;
	ssize_t  write_bytes = 0;
	char fpath[BUF_MAX] = {'\0'};
	bool ret = false;

	memset(fpath, 0, sizeof(fpath));
	sprintf(fpath, SYSFS_GPIO_VAL_STR, pin);
	fd = open(fpath, O_WRONLY);
	if (-1 == fd) {
		applog(LOG_ERR, "%s,%d: %s.", __FILE__, __LINE__, strerror(errno));
		goto out;
	}

	if (0 == val) {
		write_bytes = write(fd, SYSFS_GPIO_VAL_LOW, sizeof(SYSFS_GPIO_VAL_LOW));
		if (-1 == write_bytes) {
			applog(LOG_ERR, "%s,%d: %s.", __FILE__, __LINE__, strerror(errno));
			goto out_close;
		}
	} else {
		write_bytes = write(fd, SYSFS_GPIO_VAL_HIGH, sizeof(SYSFS_GPIO_VAL_HIGH));
		if (-1 == write_bytes) {
			applog(LOG_ERR, "%s,%d: %s,%s.", __FILE__, __LINE__, fpath, strerror(errno));
			goto out_close;
		}
	}
	ret = true;
out_close:
	close(fd);
out:
	return ret;
}

int zynq_gpio_read(int pin)
{
	int  fd = 0;
	int  val = 0;
	ssize_t read_bytes = 0;
	char fpath[BUF_MAX] = {'\0'};
	char fvalue[BUF_MAX] = {'\0'};

	memset(fpath, 0, sizeof(fpath));
	sprintf(fpath, SYSFS_GPIO_VAL_STR, pin);
	fd = open(fpath, O_RDONLY);
	if (-1 == fd)
	{
		applog(LOG_ERR, "%s,%d: %s.", __FILE__, __LINE__, strerror(errno));
	}
	memset(fvalue, 0, sizeof(fvalue));
	read_bytes = read(fd, fvalue, 1);
	if (-1 == read_bytes)
	{
		applog(LOG_ERR, "%s,%d: %s.", __FILE__, __LINE__, strerror(errno));
	}
	close(fd);

	if ('0' == fvalue[0])
	{
		val = 0;
	}
	else if ('1' == fvalue[0])
	{
		val = 1;
	}
	else
	{
		val = -1;
	}

	return val;
}

void zynq_gpio_exit(int __maybe_unused pin)
{
	return;
}

pthread_mutex_t s_pwm_lock;


void zynq_set_pwm(unsigned char fan_id, int frequency, int duty)
{
	int fd = 0;
	int duty_driver = 0;

	duty_driver = frequency / 100 * (100 - duty);

	//pthread_mutex_lock(&s_pwm_lock);

	fd = open(SYSFS_PWM_DEV, O_RDWR);
	if (fd < 0)
	{
		applog(LOG_ERR, "open %s fail", SYSFS_PWM_DEV);
		//pthread_mutex_unlock(&s_pwm_lock);
		return;
	}

	if (ioctl(fd, IOCTL_SET_PWM_FREQ(fan_id), frequency) < 0)
	{
		applog(LOG_ERR,"set fan%d frequency fail ", fan_id);
		close(fd);
		//pthread_mutex_unlock(&s_pwm_lock);
		return ;
	}

	if (ioctl(fd, IOCTL_SET_PWM_DUTY(fan_id), duty_driver) < 0)
	{
		applog(LOG_ERR,"set fan%d duty fail ", fan_id);
		close(fd);
		//pthread_mutex_unlock(&s_pwm_lock);
		return ;
	}

	close(fd);
	//pthread_mutex_unlock(&s_pwm_lock);

	return;
}

#define BUF_MAX                     (256)

#define DEV_TEMPLATE                ("/dev/spidev%d.%d")
#define SYSFS_EXPORT                ("/sys/devices/soc0/amba/f8007000.devcfg/fclk_export")
#define SYSFS_VAL_STR               ("/sys/devices/soc0/amba/f8007000.devcfg/fclk/fclk1/set_rate")

static void zynq_spi_clock_init(void);

void zynq_spi_init(ZYNQ_SPI_T *spi, int bus)
{
	char dev_fname[BUF_MAX] = {'\0'};
	int fd = 0;
	uint8_t mode   = MCOMPAT_CONFIG_SPI_DEFAULT_MODE;
	uint32_t speed = MCOMPAT_CONFIG_SPI_DEFAULT_SPEED;
	uint8_t bits   = MCOMPAT_CONFIG_SPI_DEFAULT_BITS_PER_WORD;

	zynq_spi_clock_init();
	zynq_set_spi_speed(MCOMPAT_CONFIG_SPI_DEFAULT_SPEED);

	sprintf(dev_fname, DEV_TEMPLATE, bus, MCOMPAT_CONFIG_SPI_DEFAULT_CS_LINE);
	fd = open(dev_fname, O_RDWR);
	if (-1 == fd)
	{
		applog(LOG_ERR, "%s,%d: %s.", __FILE__, __LINE__, strerror(errno));
	}

	if (ioctl(fd, SPI_IOC_WR_MODE, &mode) < 0)
	{
		applog(LOG_ERR, "%s,%d: %s.", __FILE__, __LINE__, strerror(errno));
	}
	if (ioctl(fd, SPI_IOC_RD_MODE, &mode) < 0)
	{
		applog(LOG_ERR, "%s,%d: %s.", __FILE__, __LINE__, strerror(errno));
	}
	if (ioctl(fd, SPI_IOC_WR_BITS_PER_WORD, &bits) < 0)
	{
		applog(LOG_ERR, "%s,%d: %s.", __FILE__, __LINE__, strerror(errno));
	}
	if (ioctl(fd, SPI_IOC_RD_BITS_PER_WORD, &bits) < 0)
	{
		applog(LOG_ERR, "%s,%d: %s.", __FILE__, __LINE__, strerror(errno));
	}
	if (ioctl(fd, SPI_IOC_WR_MAX_SPEED_HZ, &speed) < 0)
	{
		applog(LOG_ERR, "%s,%d: %s.", __FILE__, __LINE__, strerror(errno));
	}
	if (ioctl(fd, SPI_IOC_RD_MAX_SPEED_HZ, &speed) < 0)
	{
		applog(LOG_ERR, "%s,%d: %s.", __FILE__, __LINE__, strerror(errno));
	}

	spi->fd = fd;
	pthread_mutex_init(&(spi->lock), NULL);

	applog(LOG_DEBUG, "SPI '%s': mode=%hhu, bits=%hhu, speed=%u ",
		    dev_fname, MCOMPAT_CONFIG_SPI_DEFAULT_MODE, MCOMPAT_CONFIG_SPI_DEFAULT_BITS_PER_WORD, MCOMPAT_CONFIG_SPI_DEFAULT_SPEED);
	return;
}

void zynq_spi_exit(ZYNQ_SPI_T *spi)
{
	if (NULL == spi)
	{
		applog(LOG_ERR, "%s,%d: %s.", __FILE__, __LINE__, strerror(errno));
	}

	close(spi->fd);

	return;
}

void zynq_spi_write(ZYNQ_SPI_T *spi, uint8_t *txbuf, int len)
{
	pthread_mutex_lock(&(spi->lock));

	if ((len <= 0) || (txbuf == NULL))
	{
		applog(LOG_ERR, "%s,%d: %s.", __FILE__, __LINE__, strerror(errno));
	}

	if (write(spi->fd, txbuf, len) <= 0)
	{
		applog(LOG_ERR, "%s,%d: %s.", __FILE__, __LINE__, strerror(errno));
	}

	pthread_mutex_unlock(&spi->lock);
	return;
}

void zynq_spi_read(ZYNQ_SPI_T *spi, uint8_t *rxbuf, int len)
{
	pthread_mutex_lock(&(spi->lock));

	if ((len <= 0) || (rxbuf == NULL))
	{
		applog(LOG_ERR, "%s,%d: %s.", __FILE__, __LINE__, strerror(errno));
	}

	if (read(spi->fd, rxbuf, len) <= 0)
	{
		applog(LOG_ERR, "%s,%d: %s.", __FILE__, __LINE__, strerror(errno));
	}

	pthread_mutex_unlock(&spi->lock);
	return;
}

void zynq_spi_clock_init(void)
{
	int  fd = 0;
	ssize_t write_bytes = 0;
	char fvalue[BUF_MAX] = {'\0'};

	fd = access(SYSFS_VAL_STR, F_OK);
	if (0 == fd)
	{
		return;
	}

	fd = open(SYSFS_EXPORT, O_WRONLY);
	if (-1 == fd)
	{
		applog(LOG_ERR, "%s,%d: %s.", __FILE__, __LINE__, strerror(errno));
	}
	memset(fvalue, 0, sizeof(fvalue));
	sprintf(fvalue, "%s", "fclk1");
	write_bytes = write(fd, fvalue, strlen(fvalue));
	if (-1 == write_bytes)
	{
		applog(LOG_ERR, "%s,%d: %s.", __FILE__, __LINE__, strerror(errno));
	}
	close(fd);

	return;
}

void zynq_set_spi_speed(int speed)
{
	int  fd = 0;
	ssize_t write_bytes = 0;
	char fvalue[BUF_MAX] = {'\0'};

	applog(LOG_DEBUG, "set spi speed %d ", speed);
	fd = open(SYSFS_VAL_STR, O_WRONLY);
	if (-1 == fd)
	{
		applog(LOG_ERR, "%s,%d: %s.", __FILE__, __LINE__, strerror(errno));
	}
	memset(fvalue, 0, sizeof(fvalue));
	sprintf(fvalue, "%d", speed * 16);
	write_bytes = write(fd, fvalue, strlen(fvalue));
	if (-1 == write_bytes)
	{
		applog(LOG_ERR, "%s,%d: %s.", __FILE__, __LINE__, strerror(errno));
	}
	close(fd);

	return;
}

int zynq_gpio_g9_vid_set(int level)
{
	int fd = 0;

	fd = open(SYSFS_VID_DEV, O_RDWR);
	if (-1 == fd)
	{
		applog(LOG_ERR, "%s,%d: %s.", __FILE__, __LINE__, strerror(errno));
	}

	if (ioctl(fd, IOCTL_SET_VAL_0, 0x0100 | level) < 0)
	{
		applog(LOG_ERR, "%s,%d: %s.", __FILE__, __LINE__, strerror(errno));
	}
	close(fd);

	return 0;
}

int zynq_gpio_g19_vid_set(int chain_id, int level)
{
	int fd = 0;

	fd = open(SYSFS_VID_DEV, O_RDWR);
	if (-1 == fd)
	{
		applog(LOG_ERR, "%s,%d: %s.", __FILE__, __LINE__, strerror(errno));
	}
	if (ioctl(fd, IOCTL_SET_CHAIN_0, chain_id) < 0)
	{
		applog(LOG_ERR, "%s,%d: %s.", __FILE__, __LINE__, strerror(errno));
	}
	if (ioctl(fd, IOCTL_SET_VALUE_0, 0x100 | level) < 0)
	{
		applog(LOG_ERR, "%s,%d: %s.", __FILE__, __LINE__, strerror(errno));
	}
	close(fd);

	return 0;
}

/* DUPES, FIXME THIS ONE IS USED */
typedef struct HUB_DEV_TAG {
	volatile uint8_t *vir_base;
	uint32_t          phy_addr;
	uint32_t          mem_size;
	const char       *name;
} HUB_DEV_T;

static HUB_DEV_T s_dev_list[] = {
	{NULL, 0x43C30000, 0x2000, "spi"},
	{NULL, 0x43C00000, 0x1000, "peripheral"},
	//{NULL, 0x43C32000, 0x1000, "sha256"},
};

#if 0
/* DUPES FIXME THIS ONE ISN'T USED */
typedef struct HUB_DEV_TAG {
volatile uint8_t *vir_base;
uint32_t          phy_addr;
const char       *name;
} HUB_DEV_T;

static HUB_DEV_T s_dev_list[] = {
{NULL, 0x43C30000, "spi"},
{NULL, 0x43C10000, "peripheral"},
{NULL, 0x43C00000, "fans"},
{NULL, 0x41200000, "gpio"},
{NULL, 0x43C32000, "sha256"},
};
#endif

void hub_hardware_init(void)
{
	int fd = 0;
	int i = 0;
	int iMax = sizeof(s_dev_list) / sizeof(s_dev_list[0]);

	applog(LOG_INFO, "max range: 0x%x.", _MAX_MEM_RANGE);

	fd = open("/dev/mem", O_RDWR | O_SYNC);
	if (-1 == fd)
	{
		applog(LOG_ERR, "open /dev/mem:");
		return;
	}

	applog(LOG_INFO, "total: %d dev will mmap.", iMax);
	for(i = 0; i < iMax; i++)
	{
		s_dev_list[i].vir_base = mmap(NULL, _MAX_MEM_RANGE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, s_dev_list[i].phy_addr);
		if (MAP_FAILED == s_dev_list[i].vir_base)
		{
			close(fd);
			applog(LOG_ERR, "mmap %s:phy:0x%08x => vir:%p fail.", s_dev_list[i].name, s_dev_list[i].phy_addr, s_dev_list[i].vir_base);
			return;
		}

		applog(LOG_INFO, "mmap %s:phy:0x%08x => vir:%p ok.", s_dev_list[i].name, s_dev_list[i].phy_addr, s_dev_list[i].vir_base);
	}

	applog(LOG_INFO, "total: %d dev mmap done.", iMax);

	close(fd);
}

void hub_hardware_deinit(void)
{
	int i = 0;
	int iMax = sizeof(s_dev_list) / sizeof(s_dev_list[0]);

	for(i = 0; i < iMax; i++)
	{
		munmap((void *)s_dev_list[i].vir_base, _MAX_MEM_RANGE);
		applog(LOG_INFO, "unmap %s:vir:%p.", s_dev_list[i].name, s_dev_list[i].vir_base);
	}
}

/*  vid, iic, uart */
// compatible values for different vid types
static int s_vid_map[][3] = {
	{0,  1608, 54},
	{1,  1599, 60},
	{2,  1590, 67},
	{3,  1581, 73},
	{4,  1572, 80},
	{5,  1563, 86},
	{6,  1554, 93},
	{7,  1545, 99},
	{8,  1536, 106},
	{9,  1527, 112},
	{10, 1518, 119},
	{11, 1509, 125},
	{12, 1500, 132},
	{13, 1491, 138},
	{14, 1482, 145},
	{15, 1473, 151},
	{16, 1464, 158},
	{17, 1455, 164},
	{18, 1446, 171},
	{19, 1437, 177},
	{20, 1428, 184},
	{21, 1419, 190},
	{22, 1410, 197},
	{23, 1401, 203},
	{24, 1392, 210},
	{25, 1383, 216},
	{26, 1374, 223},
	{27, 1365, 229},
	{28, 1356, 236},
	{29, 1347, 242},
	{30, 1338, 248},
	{31, 1329, 255},
};

static bool hub_set_vid_i2c(uint8_t chain_id, int vid);
static bool hub_set_vid_uart(uint8_t chain_id, int vid);
static void send_uart(const char *path, char byte);
static bool set_vol_on_i2c(int chain, int vol);
static bool set_power_on_i2c(int chain, int val);

bool hub_set_vid(uint8_t chain_id, int vol)
{
	int type = 0;

	type = misc_get_vid_type();
	switch(type)
	{
		case MCOMPAT_LIB_VID_VID_TYPE:
		{
			hub_set_vid_vid(chain_id, vol);
			return true;
		}

		case MCOMPAT_LIB_VID_I2C_TYPE:
		{
			if (hub_set_vid_i2c(chain_id, vol))
			{
				return true;
			}
			else
			{
				return false;
			}
		}

		case MCOMPAT_LIB_VID_UART_TYPE:
		{
			if (hub_set_vid_uart(chain_id, vol))
			{
				return true;
			}
			else
			{
				return false;
			}
		}

		case MCOMPAT_LIB_VID_GPIO_I2C_TYPE:
		{
			applog(LOG_ERR, "%s,%d:no impl type:MCOMPAT_LIB_VID_GPIO_I2C_TYPE.", __FILE__, __LINE__);
			break;
		}

		default:
		{
			applog(LOG_ERR, "%s,%d:err vid type:%d.", __FILE__, __LINE__, type);
			break;
		}
	}

	return true;
}

static bool hub_set_vid_i2c(uint8_t chan_id, int vid)
{
	int vol = s_vid_map[vid][1];

	set_vol_on_i2c(chan_id + 1 , vol);

	return true;
}

static bool hub_set_vid_uart(uint8_t chain_id, int vid)
{
	char byte = 0;
	byte = (char)s_vid_map[vid][2];

	hub_set_vid_uart_select(chain_id);

	send_uart("/dev/ttyPS1", byte);

	return true;
}

static void hub_set_power_en_i2c(uint8_t chain_id,int value)
{
	set_power_on_i2c(chain_id + 1, value);
}

bool set_timeout_on_i2c(int time)
{
	int i;
	int fd;
	int sum = 0;
	unsigned char buffer[10] = {0};
	struct i2c_rdwr_ioctl_data packets;
	struct i2c_msg messages[2];

	fd = open(I2C_DEVICE_NAME, O_RDWR);
	if(fd < 0) {
		applog(LOG_ERR, "%s,%d:open %s failled: %d.", __FILE__, __LINE__, I2C_DEVICE_NAME, errno);
		return false;
	}

	buffer[0] = 0x00;
	buffer[1] = 0xab;
	buffer[2] = 0x00;
	buffer[3] = 0x70;
	buffer[4] = 0x00;
	buffer[5] = 0x01;
	buffer[6] = (time/30);
	for (i = 0; i < 7; i++)
		sum = sum + buffer[i];
	buffer[7] = sum & 0xff;
	buffer[8] = 0xcd;

	messages[0].addr  = I2C_SLAVE_ADDR;
	messages[0].flags = I2C_M_IGNORE_NAK;
	messages[0].len   = sizeof(buffer);
	messages[0].buf   = buffer;

	packets.msgs      = messages;
	packets.nmsgs     = 1;

	if(ioctl(fd, I2C_RDWR, &packets) < 0) {
		applog(LOG_ERR, "%s,%d:write iic failled: %d.", __FILE__, __LINE__, errno);
		close(fd);
		return false;
	}

	close(fd);
	return true;
}

static bool set_power_on_i2c(int chain, int val)
{
	int i;
	int fd;
	int sum = 0;
	unsigned char buffer[10] = {0};
	struct i2c_rdwr_ioctl_data packets;
	struct i2c_msg messages[2];

	fd = open(I2C_DEVICE_NAME, O_RDWR);
	if(fd < 0) {
		applog(LOG_ERR, "%s,%d:open %s failled: %d.", __FILE__, __LINE__, I2C_DEVICE_NAME, errno);
		return false;
	}

	buffer[0] = 0x00;
	buffer[1] = 0xab;
	buffer[2] = 0x00;
	buffer[3] = 0x85;
	buffer[4] = 0x00;
	buffer[5] = 0x02;
	buffer[6] = chain;
	if(val != 0)
		buffer[7] = 0x01;
	else
		buffer[7] = 0x02;

	for (i = 0; i < 8; i++)
		sum = sum + buffer[i];
	buffer[8] = sum & 0xff;
	buffer[9] = 0xcd;

	messages[0].addr  = I2C_SLAVE_ADDR;
	messages[0].flags = I2C_M_IGNORE_NAK;
	messages[0].len   = sizeof(buffer);
	messages[0].buf   = buffer;

	packets.msgs      = messages;
	packets.nmsgs     = 1;

	if(ioctl(fd, I2C_RDWR, &packets) < 0) {
		applog(LOG_ERR, "%s,%d:write iic failled: %d.", __FILE__, __LINE__, errno);
		close(fd);
		return false;
	}

	close(fd);
	return true;
}

static bool set_vol_on_i2c(int chain, int vol)
{
	int i;
	int fd;
	int sum = 0;
	unsigned char buffer[12] = {0};
	struct i2c_rdwr_ioctl_data packets;
	struct i2c_msg messages[2];

	fd = open(I2C_DEVICE_NAME, O_RDWR);
	if (fd < 0)
	{
		applog(LOG_ERR, "%s,%d:open %s failled: %d.", __FILE__, __LINE__, I2C_DEVICE_NAME, errno);
		return false;
	}

	buffer[0] = 0x00;
	buffer[1] = 0xab;
	buffer[2] = 0x00;
	buffer[3] = 0x83;
	buffer[4] = 0x00;
	buffer[5] = 0x04;
	buffer[6] = chain;
	buffer[7] = 0x00;
	buffer[8] = ((vol >> 0) & 0xff);
	buffer[9] = ((vol >> 8) & 0xff);

	for(i=0; i<10; i++){
		sum = sum + buffer[i];
	}
	buffer[10] = sum & 0xff;
	buffer[11] = 0xcd;

	messages[0].addr  = I2C_SLAVE_ADDR;
	messages[0].flags = I2C_M_IGNORE_NAK;
	messages[0].len   = sizeof(buffer);
	messages[0].buf   = buffer;

	packets.msgs      = messages;
	packets.nmsgs     = 1;

	if (ioctl(fd, I2C_RDWR, &packets) < 0) {
		applog(LOG_ERR, "%s,%d:write iic failled: %d.", __FILE__, __LINE__, errno);
		close(fd);
		return false;
	}

	close(fd);
	return true;
}

static void send_uart(const char *path, char byte)
{
	int tty_fd = 0;
	int rst = 0;
	char buf[5] = {0};

	tty_fd = open(path, O_WRONLY);
	if (-1 == tty_fd)
	{
		applog(LOG_ERR, "%s,%d:open %s failled: %d.", __FILE__, __LINE__, path, errno);
	}
	/* applog(LOG_DEBUG, "%s,%d.", __FILE__, __LINE__); */

	buf[0] = 0xaa;
	buf[1] = 0x2;
	buf[2] = 0x1;
	buf[3] = byte;
	buf[4] = (~(buf[1]+buf[2]+buf[3]))+1;
	rst = write(tty_fd, &buf, 5);
	if (-1 == rst)
	{
		close(tty_fd);
		applog(LOG_ERR, "%s,%d:write tty failled: %d.", __FILE__, __LINE__, errno);
	}

	/* for debug */
	#if 0
	int i = 0;
	applog(LOG_DEBUG, "uart %s send:", path);
	for(i = 0; i < 5; i++)
	{
	applog(LOG_DEBUG, "%02X,", buf[i]);
}
applog(LOG_DEBUG, "");
#endif

close(tty_fd);

return;
}

#if 0
#ifdef SYSTEM_LINUX
static void hub_hardware_init(void)
{
int fd = 0;
int i = 0;
int iMax = sizeof(s_dev_list) / sizeof(s_dev_list[0]);

applog(LOG_INFO, "max range: 0x%x.", PAGE_SIZE);

fd = open("/dev/mem", O_RDWR | O_SYNC);
if (-1 == fd)
{
applog(LOG_ERR, "open /dev/mem:");
return;
}

applog(LOG_INFO, "total: %d dev will mmap.", iMax);
for(i = 0; i < iMax; i++)
{
s_dev_list[i].vir_base = mmap(NULL, s_dev_list[i].mem_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, s_dev_list[i].phy_addr);
if (MAP_FAILED == s_dev_list[i].vir_base)
{
close(fd);
applog(LOG_ERR, "mmap %s:phy:0x%08x => vir:%p size:0x%x fail.", s_dev_list[i].name, s_dev_list[i].phy_addr, s_dev_list[i].vir_base, s_dev_list[i].mem_size);
return;
}

applog(LOG_INFO, "mmap %s:phy:0x%08x => vir:%p size:0x%x ok.", s_dev_list[i].name, s_dev_list[i].phy_addr, s_dev_list[i].vir_base, s_dev_list[i].mem_size);
}

applog(LOG_INFO, "total: %d dev mmap done.", iMax);

close(fd);
}

static void hub_hardware_deinit(void)
{
int i = 0;
int iMax = sizeof(s_dev_list) / sizeof(s_dev_list[0]);

for(i = 0; i < iMax; i++)
{
munmap((void *)s_dev_list[i].vir_base, s_dev_list[i].mem_size);
applog(LOG_INFO, "unmap %s:vir:%p.", s_dev_list[i].name, s_dev_list[i].vir_base);
}
}


#endif
#endif

void hub_init(void)
{
	hub_hardware_init();
}

void hub_deinit(void)
{
	hub_hardware_deinit();
}

#define INDEX_SPI           0
#define INDEX_PERIPHERAL    1
#define INDEX_SHA256        2


// for peripheral ip
void Xil_Peripheral_Out32(uint32_t phyaddr, uint32_t val)
{
	uint32_t pgoffset = phyaddr & ((uint32_t)(s_dev_list[INDEX_PERIPHERAL].mem_size -1));
	pgoffset = phyaddr;

	#ifdef SYSTEM_LINUX
	// for software team
	*(volatile uint32_t *)(s_dev_list[INDEX_PERIPHERAL].vir_base + pgoffset) = val;
	#else
	// for digit team
	*(volatile uint32_t *)(s_dev_list[INDEX_PERIPHERAL].phy_addr + pgoffset) = val;
	#endif
}

int Xil_Peripheral_In32(uint32_t phyaddr)
{
	uint32_t val;
	uint32_t pgoffset = phyaddr & ((uint32_t)(s_dev_list[INDEX_PERIPHERAL].mem_size -1));
	pgoffset = phyaddr;

	#ifdef SYSTEM_LINUX
	// for software team
	val = *(volatile uint32_t *)(s_dev_list[INDEX_PERIPHERAL].vir_base + pgoffset);
	#else
	// for digit team
	val = *(volatile uint32_t *)(s_dev_list[INDEX_PERIPHERAL].phy_addr + pgoffset);
	#endif

	return val;
}

// for spi ip
void Xil_SPI_Out32(uint32_t phyaddr, uint32_t val)
{
	uint32_t pgoffset = phyaddr & ((uint32_t)(s_dev_list[INDEX_SPI].mem_size -1));

	#ifdef SYSTEM_LINUX
	// for software team
	*(volatile uint32_t *)(s_dev_list[INDEX_SPI].vir_base + pgoffset) = val;
	#else
	// for digit team
	*(volatile uint32_t *)(s_dev_list[INDEX_SPI].phy_addr + pgoffset) = val;
	#endif
}

int Xil_SPI_In32(uint32_t phyaddr)
{
	uint32_t val;
	uint32_t pgoffset = phyaddr & ((uint32_t)(s_dev_list[INDEX_SPI].mem_size -1));

	#ifdef SYSTEM_LINUX
	// for software team
	val = *(volatile uint32_t *)(s_dev_list[INDEX_SPI].vir_base + pgoffset);
	#else
	// for digit team
	val = *(volatile uint32_t *)(s_dev_list[INDEX_SPI].phy_addr + pgoffset);
	#endif

	return val;
}


void set_led(uint8_t spi_id, uint32_t mode, uint32_t led_delay)
{
	uint32_t reg_val;

	if (mode == LED_ON){
		reg_val = Xil_Peripheral_In32(MCOMPAT_PERIPHERAL_S00_AXI_SLV_REG9_OFFSET);
		Xil_Peripheral_Out32(MCOMPAT_PERIPHERAL_S00_AXI_SLV_REG9_OFFSET,(reg_val & (0xffffffff ^ ( 1 << spi_id)) ) | ((LED_ON & 0x1) << spi_id));
	}
	else if (mode == LED_OFF){
		reg_val = Xil_Peripheral_In32(MCOMPAT_PERIPHERAL_S00_AXI_SLV_REG9_OFFSET);
		Xil_Peripheral_Out32(MCOMPAT_PERIPHERAL_S00_AXI_SLV_REG9_OFFSET,(reg_val & (0xffffffff ^ ( 1 << spi_id)) ) | ((LED_OFF & 0x1) << spi_id));
	}
	else if (mode == LED_BLING_ON){
		reg_val = Xil_Peripheral_In32(MCOMPAT_PERIPHERAL_S00_AXI_SLV_REG9_OFFSET);
		Xil_Peripheral_Out32(MCOMPAT_PERIPHERAL_S00_AXI_SLV_REG9_OFFSET,(reg_val & (0xffffffff ^ ( 1 << spi_id)) ) | ((LED_OFF & 0x1) << spi_id));
		cgsleep_us(led_delay*1000);
		reg_val = Xil_Peripheral_In32(MCOMPAT_PERIPHERAL_S00_AXI_SLV_REG9_OFFSET);
		Xil_Peripheral_Out32(MCOMPAT_PERIPHERAL_S00_AXI_SLV_REG9_OFFSET,(reg_val & (0xffffffff ^ ( 1 << spi_id)) ) | ((LED_ON & 0x1) << spi_id));
		cgsleep_us(led_delay*1000);
	}
	else if (mode == LED_BLING_OFF){
		reg_val = Xil_Peripheral_In32(MCOMPAT_PERIPHERAL_S00_AXI_SLV_REG9_OFFSET);
		Xil_Peripheral_Out32(MCOMPAT_PERIPHERAL_S00_AXI_SLV_REG9_OFFSET,(reg_val & (0xffffffff ^ ( 1 << spi_id)) ) | ((LED_ON & 0x1) << spi_id));
		cgsleep_us(led_delay*1000);
		reg_val = Xil_Peripheral_In32(MCOMPAT_PERIPHERAL_S00_AXI_SLV_REG9_OFFSET);
		Xil_Peripheral_Out32(MCOMPAT_PERIPHERAL_S00_AXI_SLV_REG9_OFFSET,(reg_val & (0xffffffff ^ ( 1 << spi_id)) ) | ((LED_OFF & 0x1) << spi_id));
		cgsleep_us(led_delay*1000);
	}
}


// ---------------------------------------------------------------------------
// For check status
// ---------------------------------------------------------------------------

int clear_wait_st_idle(uint8_t spi_id, uint32_t timeout_us)
{
	uint32_t i;
	uint32_t data_buf;
	uint32_t cmd_status = 0;

	for(i=0; i<timeout_us; i++){
		cmd_status = Xil_SPI_In32(SPI_BASEADDR_GAP*spi_id+CMD_CTRL_REG2_ADDR);
		if ((cmd_status&0xFF000000)==0x00000000) {
			Xil_SPI_Out32(SPI_BASEADDR_GAP*spi_id+CMD_CTRL_REG1_ADDR, 0x00000800);
			cgsleep_us(1);
			Xil_SPI_Out32(SPI_BASEADDR_GAP*spi_id+CMD_CTRL_REG1_ADDR, 0x00000002);
			Xil_SPI_Out32(SPI_BASEADDR_GAP*spi_id+CMD_CTRL_REG1_ADDR, 0x00000000);
			data_buf = Xil_SPI_In32(SPI_BASEADDR_GAP*spi_id+CMD_CTRL_REG2_ADDR);
			if ((data_buf&0xFFFF00FF) != 0) {
				applog(LOG_DEBUG, "clear_wait_st_idle SPI status is not cleared: %08x i=%0d ", data_buf&0xFFFF00FF, i);
			}
			break; // state=0 cmd_done=1
		}
		cgsleep_us(1);
	}
	if (i >= timeout_us){
		applog(LOG_WARNING, "clear_wait_st_idle Wait SPI status clear timeout! i=%0d status=%8x ", i, cmd_status);
		return XST_FAILURE;
	}
	else {
		return XST_SUCCESS;
	}
}


int wait_cmd_done(uint8_t spi_id, uint32_t timeout_us)
{
	uint32_t i;
	uint32_t cmd_status = 0;
	uint32_t timeout = timeout_us / 1000 + 1;

	for(i = 0; i <= timeout; i++){ // polling cmd_done
		cmd_status = Xil_SPI_In32(SPI_BASEADDR_GAP*spi_id+CMD_CTRL_REG2_ADDR);
		applog(LOG_DEBUG, "Read SPI CMD_CTRL_REG2_ADDR: %08x ", cmd_status);
		//getchar(); // for debug
		if ((cmd_status&0xF0000000) != 0){ // spi fsm not idle
		}
		if ((cmd_status&0x00000001) == 1){
			applog(LOG_DEBUG, "CMD  done, status: %08x! ", cmd_status);
			break;
		}
		cgsleep_ms(1); //usleep(10);
	}
	if (i > timeout){
		applog(LOG_WARNING, "SPI polling cmd done timeout! i=%0d ", i);
		return XST_FAILURE;
	}
	else {
		return XST_SUCCESS;
	}
}

int wait_phy_idle(uint8_t spi_id, uint32_t timeout_us)
{
	uint32_t i;
	uint32_t cmd_status = 0;
	uint32_t timeout = timeout_us / 1000 + 1;

	for(i = 0; i <= timeout; i++){
		cmd_status = Xil_SPI_In32(SPI_BASEADDR_GAP*spi_id+CMD_CTRL_REG2_ADDR);
		if ((cmd_status&0x00000040)==0x00000000) {
			cmd_status = Xil_SPI_In32(SPI_BASEADDR_GAP*spi_id+CMD_CTRL_REG2_ADDR);
			break; // state=0 cmd_done=1
		}
		cgsleep_ms(1); // usleep(1);
	}
	if (i >= timeout_us){
		applog(LOG_WARNING, "Wait SPI status clear timeout! i=%0d status=%8x ", i, cmd_status);
		return XST_FAILURE;
	}
	else {
		return XST_SUCCESS;
	}
}

int wait_spi_idle(uint8_t spi_id, uint32_t timeout_us)
{
	uint32_t i;
	uint32_t cmd_status = 0;
	uint32_t timeout = timeout_us / 1000 + 1;

	for(i = 0; i <= timeout; i ++){
		cmd_status = Xil_SPI_In32(SPI_BASEADDR_GAP*spi_id+CMD_CTRL_REG2_ADDR);
		if ((cmd_status&0xFF000040)==0) return XST_SUCCESS;
		cgsleep_ms(1); // usleep(20);
	}

	applog(LOG_DEBUG, "Wait SPI(%0d) idle timeout! time=%0d us, status=%8x ", spi_id, timeout_us, cmd_status);

	return XST_FAILURE;
}

int wait_write_buf_empty(uint8_t spi_id, uint32_t timeout_us)
{
	uint32_t i;
	uint32_t cmd_status = 0;
	uint32_t timeout = timeout_us / 1000 + 1;

	for(i = 0; i <= timeout; i++){
		cmd_status = Xil_SPI_In32(SPI_BASEADDR_GAP*spi_id+CMD_CTRL_REG2_ADDR);
		if ((cmd_status&0x03000000)==0) return XST_SUCCESS;
		cgsleep_ms(1); // usleep(10); // polling interval
	}

	//printf("Wait command write queue empty timeout!spi=%0d i=%0d status=%8x ",spi_id, i, cmd_status);
	return XST_FAILURE;
}


int check_cmd_status(uint8_t spi_id)
{
	uint32_t cmd_status = 0;

	cmd_status = Xil_SPI_In32(SPI_BASEADDR_GAP*spi_id+CMD_CTRL_REG2_ADDR);
	if (cmd_status & 0x00000004){
		return XST_CRC_ERROR;
	}

	return XST_SUCCESS;
}


void reset_rx_buffer(uint8_t spi_id)
{
	uint32_t cmd_status = 0;
	uint32_t write_data = 0;

	cmd_status = Xil_SPI_In32(SPI_BASEADDR_GAP*spi_id+MAIN_CFG_REG1_ADDR);
	write_data = cmd_status & (~0x00000004);
	Xil_SPI_Out32(SPI_BASEADDR_GAP*spi_id+MAIN_CFG_REG1_ADDR, write_data);
	cgsleep_ms(1);
	write_data = cmd_status | 0x00000004;
	Xil_SPI_Out32(SPI_BASEADDR_GAP*spi_id+MAIN_CFG_REG1_ADDR, write_data);
}

// ---------------------------------------------------------------------------
// Below function is for new SPI design's nonce/receive queue
// ---------------------------------------------------------------------------
void read_rx_buffer(uint8_t spi_id, uint8_t* buf8, uint32_t len_cfg)
{
	uint32_t i;
	uint8_t rx_len;

	rx_len = ((len_cfg & 0x0000FF00) >> 8)*2;

	// Get nonce data
	for(i = 0; i < rx_len; i+=4){
		*(uint32_t*)(buf8+i) = Xil_SPI_In32(SPI_BASEADDR_GAP*spi_id+CMD_READ_REG0_ADDR+i);
	}
	Xil_SPI_Out32(SPI_BASEADDR_GAP*spi_id+CMD_CTRL_REG1_ADDR, 0x00000800); // tell hw one nonce is fetched

}


void fill_tx_buffer(uint8_t spi_id, uint8_t* buf8, uint32_t byte_len)
{
	uint32_t i;

	for(i = 0; i < byte_len; i+=4)
	{
		Xil_SPI_Out32(SPI_BASEADDR_GAP*spi_id+CMD_WRITE_REG01_ADDR+i, *(uint32_t*)(buf8+i));
	}
}


// ---------------------------------------------------------------------------
// Below function is for new SPI design's send queue
// ---------------------------------------------------------------------------

// SPI bypass = 0
int push_one_cmd(uint8_t spi_id, uint8_t* tx_buf8, uint32_t len_cfg, uint32_t last_job)
{
	//uint32_t i;
	uint8_t  byte_len;
	//uint32_t cmd_status;
	uint16_t cmd_header;

	byte_len = (len_cfg >> 24)*2;  // Not include ending zeros
	cmd_header = (tx_buf8[1] << 8) | tx_buf8[0];

	// wait command write queue empty
	if (wait_write_buf_empty(spi_id, 10000) == XST_FAILURE) return XST_FAILURE;

	if (wait_spi_idle(spi_id, 10000) == XST_FAILURE) return XST_FAILURE;

	// write header to buffer
	Xil_SPI_Out32(SPI_BASEADDR_GAP*spi_id+CMD_WRITE_HEAD_ADDR, cmd_header);


	// write data to buffer
	fill_tx_buffer(spi_id, tx_buf8+2, byte_len-2); // Not include tx

	// send command execution
	Xil_SPI_Out32(SPI_BASEADDR_GAP*spi_id+CMD_CTRL_REG0_ADDR, len_cfg);
	if (last_job)
		Xil_SPI_Out32(SPI_BASEADDR_GAP*spi_id+CMD_CTRL_REG1_ADDR, 
			0x00000001 | CHK_CMD | CHK_HY | CHK_LN);
	else
		Xil_SPI_Out32(SPI_BASEADDR_GAP*spi_id+CMD_CTRL_REG1_ADDR, 
			0x00000011 | CHK_CMD | CHK_HY | CHK_LN);

	return XST_SUCCESS;
}

/***************************************/
//              interface
/***************************************/


void hub_spi_reset(uint8_t spi_id)
{
	Xil_SPI_Out32(SPI_BASEADDR_GAP*spi_id+MAIN_CFG_REG1_ADDR,0x00000010);
	cgsleep_us(1);
	Xil_SPI_Out32(SPI_BASEADDR_GAP*spi_id+MAIN_CFG_REG1_ADDR,0x0000001F);
}

int hub_spi_init(uint8_t spi_id, uint8_t chip_num)
{
	uint32_t Status;
	//uint32_t i;

	Status = Xil_SPI_In32(SPI_RESET_REG);

	// reset
	Xil_SPI_Out32(SPI_RESET_REG,(Status & ~(1 << spi_id)));
	cgsleep_ms(1); // usleep(1);

	Status = Xil_SPI_In32(SPI_RESET_REG);

	// release reset
	Xil_SPI_Out32(SPI_RESET_REG,(Status | (1 << spi_id)));

	Status = Xil_SPI_In32(SPI_RESET_REG);

	hub_spi_reset(spi_id);
	// config max chip number
	Xil_SPI_Out32(SPI_BASEADDR_GAP*spi_id+MAIN_CFG_REG0_ADDR,0x00041004|(chip_num<<24));
	// config not check header
//	Xil_SPI_Out32(SPI_BASEADDR_GAP*spi_id+CMD_CTRL_REG3_ADDR,0x000F00FF);
	Xil_SPI_Out32(SPI_BASEADDR_GAP*spi_id+CMD_CTRL_REG3_ADDR, 0x000F0000 & chip_num);
	// config mask of each interrupt
	Xil_SPI_Out32(MAIN_CFG_REG2_ADDR, 0x00000);

	return XST_SUCCESS;
}

void hub_spi_clean_chain(uint32_t spi_id)
{
	uint8_t spi_tx[MCOMPAT_CONFIG_MAX_CMD_LENGTH] = {0};
	uint8_t spi_rx[MCOMPAT_CONFIG_MAX_CMD_LENGTH] = {0};

	spi_tx[0] = CMD_RESET;
	spi_tx[1] = CMD_ADDR_BROADCAST;
	spi_tx[2] = 0xff;
	spi_tx[3] = 0xff;

	do_spi_cmd(spi_id, spi_tx, spi_rx, 0x10001000);

	hub_spi_reset(spi_id);
}

void hub_set_spi_speed(uint8_t spi_id, int select)
{
	uint32_t cfg[] =      {0x00020000, 0x00040000, 0x00080000, 0x00100000, 0x00200100, 0x00330100};
	//float mcu_spi_clk[] = {0.39062,    0.78125,    1.5625,     3.125,      6.25,       9.96};
	uint32_t read_data;

	read_data = Xil_SPI_In32(SPI_BASEADDR_GAP*spi_id+MAIN_CFG_REG0_ADDR);
	Xil_SPI_Out32(SPI_BASEADDR_GAP*spi_id+MAIN_CFG_REG0_ADDR,(read_data&0xFF00FFFF)|cfg[select]);

	read_data = Xil_SPI_In32(SPI_BASEADDR_GAP*spi_id+MAIN_CFG_REG0_ADDR);
}


// ----------- send job ------------
int send_job_queue(uint8_t spi_id, uint8_t* tx_buf8, uint8_t __maybe_unused *rx_buf8, uint32_t len_cfg, uint32_t last_job)
{
	// push tx data to send buffer and start command
	if (push_one_cmd(spi_id, tx_buf8, len_cfg, last_job) != XST_SUCCESS) return XST_FAILURE;

	// wait all jobs are sent to chip
	if (last_job){
		if (wait_spi_idle(spi_id, 100000) == XST_FAILURE) return XST_FAILURE;
	}

	// Clear status. Not mandatory but suggest
	if (last_job) clear_wait_st_idle(spi_id, 100000);

	//  usleep(100);
	return XST_SUCCESS;
}


int send_one_cmd_split(uint8_t spi_id, uint8_t* tx_buf8, uint32_t len_cfg, uint32_t last_job, uint8_t cs_low)
{
	uint32_t  byte_len;
	uint16_t cmd_header;
	uint32_t cfg_reg0;

	byte_len = (len_cfg >> 24)*2;  // Not include ending zeros
	cmd_header = (tx_buf8[1] << 8) | tx_buf8[0];

	// change ext_zero to 0
	cfg_reg0 = Xil_SPI_In32(SPI_BASEADDR_GAP*spi_id+MAIN_CFG_REG0_ADDR);
	Xil_SPI_Out32(SPI_BASEADDR_GAP*spi_id+MAIN_CFG_REG0_ADDR,(cfg_reg0&0xFFFFFF00));


	// wait command write queue empty
	if (wait_write_buf_empty(spi_id, 10000) == XST_FAILURE) return XST_FAILURE;


	// write header to buffer
	Xil_SPI_Out32(SPI_BASEADDR_GAP*spi_id+CMD_WRITE_HEAD_ADDR, cmd_header);


	// write data to buffer
	fill_tx_buffer(spi_id, tx_buf8+2, byte_len-2); // Not include tx


	// send command execution
	Xil_SPI_Out32(SPI_BASEADDR_GAP*spi_id+CMD_CTRL_REG0_ADDR, len_cfg);
	if (last_job)
		Xil_SPI_Out32(SPI_BASEADDR_GAP*spi_id+CMD_CTRL_REG1_ADDR, (0x00000001 | CHK_HY | CHK_LN | cs_low<<14) );
	else
		Xil_SPI_Out32(SPI_BASEADDR_GAP*spi_id+CMD_CTRL_REG1_ADDR, (0x00000011 | CHK_HY | CHK_LN | cs_low<<14) );


	//    // check send queue full
	//    cmd_status = Xil_SPI_In32(SPI_BASEADDR_GAP*spi_id+CMD_CTRL_REG2_ADDR);
	//    if ((cmd_status&0x02000000)==0) {
	//        DBGERROR("After push one command, send queue is not full!");
	//        return XST_FAILURE;
	//    }

	// For half command application, can not use queue
	wait_cmd_done(spi_id, 10000);


	// change back ext_zero
	Xil_SPI_Out32(SPI_BASEADDR_GAP*spi_id+MAIN_CFG_REG0_ADDR,cfg_reg0);


	return XST_SUCCESS;

}


bool rece_queue_ready_check(uint8_t spi_id, uint32_t len, uint32_t timeout_us)
{
	uint32_t i;
	uint32_t cmd_status;
	uint32_t timeout = timeout_us / 1000 + 1;

	for(i = 0; i <= timeout; i++){
		cmd_status = Xil_SPI_In32(SPI_BASEADDR_GAP*spi_id+CMD_CTRL_REG2_ADDR);
		if (((cmd_status&0x00000010)==0x00000010) && ((((cmd_status&0x0000FF00)>>8)>=(len)))) return true; // wait nonce_ready = 1, cmd_done = 1
		//        if (((spi_tr.cmd_status&0x0000FF00)>>8)>=(len)) return true;
		//        if ((spi_tr.cmd_status&0x00000010)==0x00000010) return true;
		cgsleep_ms(1); // usleep(10);
	}

	return false;
}

// Check receive queue empty. true: empty
bool rece_queue_empty_check(uint8_t spi_id, uint32_t timeout_us)
{
	uint32_t i;
	uint32_t cmd_status;
	uint32_t timeout = timeout_us / 1000 + 1;

	for(i = 0; i <= timeout; i++){
		cmd_status = Xil_SPI_In32(SPI_BASEADDR_GAP*spi_id+CMD_CTRL_REG2_ADDR);
		if ((cmd_status & 0x0000ff10)==0x00000000) {
			return true; // wait nonce_ready = 0, cmd_done = 1
		}
		cgsleep_ms(1); // usleep(10);
	}

	return false;
}

bool rece_queue_has_nonce(uint8_t spi_id, uint32_t __maybe_unused timeout_us)
{
	//uint32_t i;
	uint32_t cmd_status;

	cmd_status = Xil_SPI_In32(SPI_BASEADDR_GAP*spi_id+CMD_CTRL_REG2_ADDR);
	if ( ((cmd_status&0x0000ff00) >= 0x00000600) && ((cmd_status&0x00000010) == 0x00000010) ) {
		return true; // wait nonce_ready = 0, cmd_done = 1
	}

	return false;
}


void read_nonce_buffer(uint8_t spi_id, uint8_t* buf8, uint32_t len_cfg)
{
	uint32_t i;
	uint8_t rx_len;

	rx_len = (len_cfg & 0x0000FF00) >> 8;

	// Get nonce data
	for(i = 0; i < rx_len*2; i+=4){
		*(uint32_t*)(buf8+i) = Xil_SPI_In32(SPI_BASEADDR_GAP*spi_id+CMD_READ_REG0_ADDR+i);
	}
	/*
	 * if (buf8[0] == 00)
	 * {
	 * dump_spi_last_tr(spi_id);
	}
	*/
	Xil_SPI_Out32(SPI_BASEADDR_GAP*spi_id+CMD_CTRL_REG1_ADDR, 0x00000B00); // tell hw one nonce is fetched, keep auto get nonce enable
	cgsleep_ms(1); // usleep(1);
}


int pop_one_rece(uint8_t spi_id, uint8_t* rx_buf8, uint32_t len_cfg)
{
	//uint32_t i;
	uint32_t len = (len_cfg & 0x0000ff00) >> 8;

	// wait receive queue ready
	if (rece_queue_ready_check(spi_id, len, 50000) == false) {
		applog(LOG_INFO, "chain%d check receive buffer ready timeout!", spi_id);
		return XST_FAILURE;
	}
	/*
	 * if (check_crc_status(spi_id) != XST_SUCCESS) {
	 * applog(LOG_WARNING, "crc error ");
	 * return XST_CRC_ERROR;
	 * }
	 */
	// read back rx data
	read_rx_buffer(spi_id, rx_buf8, len_cfg);

	return XST_SUCCESS;
}


int do_spi_cmd(uint8_t spi_id, uint8_t* tx_buf8, uint8_t* rx_buf8, uint32_t len_cfg)
{
	// reset_rx_buffer(spi_id);

	// push tx data to send buffer and start command
	if (push_one_cmd(spi_id, tx_buf8, len_cfg, 1) != XST_SUCCESS) {
		applog(LOG_ERR, "ERROR - failed to send spi cmd");
		return XST_FAILURE;
	}

	// read back rx data
	if (pop_one_rece(spi_id, rx_buf8, len_cfg) != XST_SUCCESS) {
		applog(LOG_ERR, "ERROR - failed to recv spi data");
		return XST_FAILURE;
	}

	if ((tx_buf8[0] & 0x0f) != (rx_buf8[0] & 0x0f)) {
		//hexdump_error("ERROR - recvbuf:", rx_buf8, 16);
		return XST_FAILURE;
	}

	// Clear status. Not mandatory but suggest
	clear_wait_st_idle(spi_id, 200);

	return XST_SUCCESS;
}


void enable_auto_cmd0a(uint8_t spi_id, uint32_t threshold, uint32_t msb, uint32_t lsb, uint32_t large_en, uint32_t mode )//mode : 1 only cmd0a;0 cmd08 follows cmd0a
{
	uint32_t val;
	Xil_SPI_Out32(SPI_BASEADDR_GAP*spi_id+CMD_CTRL_REG1_ADDR, 0x00000000 | CHK_CMD);
	val = ((msb << 24) & 0xff000000) | ((lsb << 16) & 0xff0000) | ((mode & 0x1) << 14) | ((large_en & 0x1) << 13) | (0x1 << 12) | ( threshold & 0xfff );
	Xil_SPI_Out32(SPI_BASEADDR_GAP*spi_id+AUTO_CMD0A_REG0_ADDR, val);
}

void disable_auto_cmd0a(uint8_t spi_id, uint32_t threshold, uint32_t msb, uint32_t lsb, uint32_t large_en, uint32_t mode )//mode : 1 only cmd0a;0 cmd08 follows cmd0a
{
	uint32_t val;
	val = ((msb << 24) & 0xff000000) | ((lsb << 16) & 0xff0000) | ((mode & 0x1) << 14) | ((large_en & 0x1) << 13) | (0x0 << 12) | ( threshold & 0xfff );
	Xil_SPI_Out32(SPI_BASEADDR_GAP*spi_id+AUTO_CMD0A_REG0_ADDR, val);
}


int enable_auto_nonce(uint8_t spi_id, uint16_t cmd08_cmd, uint32_t len_cfg)
{
	uint8_t send_buf8[12] = {0};

	// wait all previous command done
	//clear_wait_st_idle(spi_id, 1000000);
	wait_spi_idle(spi_id, 10000);

	// set auto get nonce
	//for(i=0; i<spi_tr.tx_len; i++){spi_tr.tx_buf[i] = 0;} // clear tx_buf variable for debug print
	Xil_SPI_Out32(SPI_BASEADDR_GAP*spi_id+CMD_WRITE_HEAD_ADDR, REORDER16(cmd08_cmd));
	fill_tx_buffer(spi_id, send_buf8, 10);
	Xil_SPI_Out32(SPI_BASEADDR_GAP*spi_id+MAIN_CFG_REG3_ADDR, 0x000000ff); // auto cmd08 gap
	Xil_SPI_Out32(SPI_BASEADDR_GAP*spi_id+CMD_CTRL_REG0_ADDR, len_cfg);
	//Xil_SPI_Out32(SPI_BASEADDR_GAP*spi_id+CMD_CTRL_REG1_ADDR, 0x00000002); // clear status
	Xil_SPI_Out32(SPI_BASEADDR_GAP*spi_id+CMD_CTRL_REG1_ADDR, 0x00000300 | CHK_CMD); // cmd08 do

	return XST_SUCCESS;
}

int disable_auto_nonce(uint8_t spi_id)
{
	Xil_SPI_Out32(SPI_BASEADDR_GAP*spi_id+CMD_CTRL_REG1_ADDR, 0x00000000); // disable auto get nonce

	// wait command done
	return (clear_wait_st_idle(spi_id, 1000000));

}

// 3.3v GPIO output
void hub_set_power_en(uint8_t chain_id, int value)
{
	uint32_t reg_val;

	if (misc_get_vid_type() == MCOMPAT_LIB_VID_I2C_TYPE) {
		hub_set_power_en_i2c(chain_id, value);
		sleep(3);
	}

	reg_val = Xil_Peripheral_In32(MCOMPAT_PERIPHERAL_S00_AXI_SLV_REG8_OFFSET);
	Xil_Peripheral_Out32(MCOMPAT_PERIPHERAL_S00_AXI_SLV_REG8_OFFSET, (reg_val & (~(0x1 << (18 + chain_id)))) | ((value & 0x1) << (18 + chain_id)));
	//reg_val = Xil_In32(XPAR_VID_LED_BUZZER_CTRL_0_S00_AXI_BASEADDR + VID_LED_BUZZER_CTRL_S00_AXI_SLV_REG0_OFFSET + chain_id*4);
	//Xil_Peripheral_Out32(XPAR_VID_LED_BUZZER_CTRL_0_S00_AXI_BASEADDR + VID_LED_BUZZER_CTRL_S00_AXI_SLV_REG0_OFFSET + chain_id*4, (reg_val & 0xfffeffff) | ((value & 0x1) << 16));
}

// 1.8v GPIO output
void hub_set_start_en(uint8_t chain_id, int value)
{
	uint32_t reg_val;

	reg_val = Xil_Peripheral_In32(MCOMPAT_PERIPHERAL_S00_AXI_SLV_REG8_OFFSET);
	Xil_Peripheral_Out32(MCOMPAT_PERIPHERAL_S00_AXI_SLV_REG8_OFFSET, (reg_val & (~(0x1 << chain_id))) | ((value & 0x1) << chain_id));
	//reg_val = Xil_In32(XPAR_VID_LED_BUZZER_CTRL_0_S00_AXI_BASEADDR + VID_LED_BUZZER_CTRL_S00_AXI_SLV_REG0_OFFSET + chain_id*4);
	//Xil_Peripheral_Out32(XPAR_VID_LED_BUZZER_CTRL_0_S00_AXI_BASEADDR + VID_LED_BUZZER_CTRL_S00_AXI_SLV_REG0_OFFSET + chain_id*4, (reg_val & 0xfffbffff) | ((value & 0x1) << 18));
}

// 1.8v GPIO output
bool hub_set_reset(uint8_t chain_id, int value)
{
	uint32_t reg_val;

	reg_val = Xil_Peripheral_In32(MCOMPAT_PERIPHERAL_S00_AXI_SLV_REG8_OFFSET);
	Xil_Peripheral_Out32(MCOMPAT_PERIPHERAL_S00_AXI_SLV_REG8_OFFSET, (reg_val & (~(0x1 << (9 + chain_id)))) | ((value & 0x1) << (9 + chain_id)));
	//reg_val = Xil_Peripheral_In32(XPAR_VID_LED_BUZZER_CTRL_0_S00_AXI_BASEADDR + VID_LED_BUZZER_CTRL_S00_AXI_SLV_REG0_OFFSET + chain_id*4);
	//Xil_Peripheral_Out32(XPAR_VID_LED_BUZZER_CTRL_0_S00_AXI_BASEADDR + VID_LED_BUZZER_CTRL_S00_AXI_SLV_REG0_OFFSET + chain_id*4, (reg_val & 0xfffdffff) | ((value & 0x1) << 17));
	return true;
}

void hub_set_led(uint8_t chain_id, int mode)
{
	uint32_t reg_val;

	if (mode == LED_ON){
		reg_val = Xil_Peripheral_In32(MCOMPAT_PERIPHERAL_S00_AXI_SLV_REG9_OFFSET);
		Xil_Peripheral_Out32(MCOMPAT_PERIPHERAL_S00_AXI_SLV_REG9_OFFSET,(reg_val & (0xffffffff ^ ( 1 << chain_id)) ) | ((LED_ON & 0x1) << chain_id));
	}
	else if (mode == LED_OFF){
		reg_val = Xil_Peripheral_In32(MCOMPAT_PERIPHERAL_S00_AXI_SLV_REG9_OFFSET);
		Xil_Peripheral_Out32(MCOMPAT_PERIPHERAL_S00_AXI_SLV_REG9_OFFSET,(reg_val & (0xffffffff ^ ( 1 << chain_id)) ) | ((LED_OFF & 0x1) << chain_id));
	}
}

int hub_get_plug(uint8_t chain_id)
{
	uint32_t reg_val;

	reg_val = Xil_Peripheral_In32(MCOMPAT_PERIPHERAL_S00_AXI_SLV_REG10_OFFSET);
	return ((reg_val >> chain_id) & 0x01);
}


#ifdef SYSTEM_LINUX
static int set_warn(int spi_id)
{
	mcompat_set_power_en(spi_id, 0);
	sleep(1);
	mcompat_set_reset(spi_id, 0);
	mcompat_set_start_en(spi_id, 0);

	do
	{
		mcompat_set_led(spi_id, 1);
		sleep(1);
		mcompat_set_led(spi_id, 0);
		sleep(1);
	}while(1);

	return 0;
}


static void hub_get_hitemp_stat(uint8_t chain_id,mcompat_temp_s *temp_ctrl)
{
	bool over_temp = false;
	int reg_val;
	int tmp_val;

	reg_val = Xil_SPI_In32(SPI_BASEADDR_GAP*chain_id+AUTO_CMD0A_REG3_ADDR);

	tmp_val = ((reg_val ) & 0x3ff) < g_temp_hi_thr ? 0x0:((reg_val) & 0x3ff);
	tmp_val = tmp_val > g_temp_lo_thr ? 0x0:tmp_val;
	temp_ctrl->temp_highest[0] = tmp_val;

	if ((temp_ctrl->temp_highest[0]) &&(temp_ctrl->temp_highest[0] < g_dangerous_temp))
		over_temp = true;

	tmp_val = ((reg_val >> 10) & 0x3ff) < g_temp_hi_thr ? 0x0:((reg_val >> 10) & 0x3ff);
	tmp_val = tmp_val > g_temp_lo_thr ? 0x0:tmp_val;
	temp_ctrl->temp_highest[1] = tmp_val;

	if ((temp_ctrl->temp_highest[1]) &&(temp_ctrl->temp_highest[1] < g_dangerous_temp))
		over_temp = true;

	tmp_val = ((reg_val >> 20) & 0x3ff) < g_temp_hi_thr ? 0x0:((reg_val >> 20) & 0x3ff);
	tmp_val = tmp_val > g_temp_lo_thr ? 0x0:tmp_val;
	temp_ctrl->temp_highest[2] = (reg_val >> 20 ) & 0x3ff;

	if ((temp_ctrl->temp_highest[2]) &&(temp_ctrl->temp_highest[2] < g_dangerous_temp))
		over_temp = true;

	if (over_temp == true)
		set_warn(chain_id);

	applog(LOG_INFO,"chain %d,Hi: %d,%d,%d",chain_id,temp_ctrl->temp_highest[0],temp_ctrl->temp_highest[1],temp_ctrl->temp_highest[2]);
}

static void hub_get_lotemp_stat(uint8_t chain_id,mcompat_temp_s *temp_ctrl)
{
	int reg_val;
	int tmp_val;

	reg_val =  Xil_SPI_In32(SPI_BASEADDR_GAP*chain_id+AUTO_CMD0A_REG2_ADDR);


	tmp_val = ((reg_val) & 0x3ff) < g_temp_hi_thr ? 0x0:((reg_val) & 0x3ff);
	tmp_val = tmp_val > g_temp_lo_thr ? 0x0:tmp_val;
	temp_ctrl->temp_lowest[0] = tmp_val;

	tmp_val = ((reg_val >> 10) & 0x3ff) < g_temp_hi_thr ? 0x0:((reg_val >> 10) & 0x3ff);
	tmp_val = tmp_val > g_temp_lo_thr ? 0x0:tmp_val;
	temp_ctrl->temp_lowest[1] = tmp_val;


	tmp_val = ((reg_val >> 20) & 0x3ff) < g_temp_hi_thr ? 0x0:((reg_val >> 20) & 0x3ff);
	tmp_val = tmp_val > g_temp_lo_thr ? 0x0:tmp_val;
	temp_ctrl->temp_lowest[2] = tmp_val;
	applog(LOG_INFO,"chain %d,lo: %d,%d,%d",chain_id,temp_ctrl->temp_lowest[0],temp_ctrl->temp_lowest[1],temp_ctrl->temp_lowest[2]);
}

static void hub_get_avgtemp_stat(uint8_t chain_id,mcompat_temp_s *temp_ctrl)
{
	int reg_val;
	int tmp_val;

	reg_val =  Xil_SPI_In32(SPI_BASEADDR_GAP*chain_id+AUTO_CMD0A_REG4_ADDR);

	tmp_val = 2 * ((reg_val ) & 0xffff) / g_chip_num;


	tmp_val = tmp_val < g_temp_hi_thr ? 0x0:tmp_val;
	tmp_val = tmp_val > g_temp_lo_thr ? 0x0:tmp_val;
	temp_ctrl->final_temp_avg = tmp_val;
	applog(LOG_INFO,"chain %d,avg: %d",chain_id,temp_ctrl->final_temp_avg);
}
#endif


void hub_set_vid_vid(uint8_t chain_id, int vid)
{
	uint32_t reg_val = 0;
	int i = 0;
	uint8_t vid_binary[16] = {0};

	for(i = 0; i < 8; i ++)
	{
		vid_binary[i] = ((vid >> i) & 0x1) ? 7 : 1;
		vid_binary[8+i] = (vid_binary[i] == 1) ? 7 : 1;
	}

	Xil_Peripheral_Out32(MCOMPAT_PERIPHERAL_S00_AXI_SLV_REG32_OFFSET, 25000);
	Xil_Peripheral_Out32(MCOMPAT_PERIPHERAL_S00_AXI_SLV_REG33_OFFSET, (vid_binary[3] << 28) | (vid_binary[2] << 24) | (vid_binary[1] << 20) | (vid_binary[0] << 16) | 0xff);
	reg_val = 0;
	for(i = 0; i < 8; i ++)
	{
		reg_val = (vid_binary[i+4] << (i*4)) | reg_val;
	}

	Xil_Peripheral_Out32(MCOMPAT_PERIPHERAL_S00_AXI_SLV_REG34_OFFSET, reg_val);
	reg_val = 0;
	for(i = 0; i < 4; i ++)
	{
		reg_val = (vid_binary[i+12] << (i*4)) | reg_val;
	}

	Xil_Peripheral_Out32(MCOMPAT_PERIPHERAL_S00_AXI_SLV_REG35_OFFSET, reg_val);
	Xil_Peripheral_Out32(MCOMPAT_PERIPHERAL_S00_AXI_SLV_REG39_OFFSET, 80 | (4 << 16));
	Xil_Peripheral_Out32(MCOMPAT_PERIPHERAL_S00_AXI_SLV_REG4_OFFSET, 0x1 << chain_id);
	Xil_Peripheral_Out32(MCOMPAT_PERIPHERAL_S00_AXI_SLV_REG3_OFFSET, 0x1 );

	cgsleep_ms(100);
}

void hub_set_vid_uart_select(uint8_t spi_id)
{
	Xil_Peripheral_Out32(MCOMPAT_PERIPHERAL_S00_AXI_SLV_REG4_OFFSET, (0x1 << 16) | (0x1 << spi_id));
}

void hub_set_pwm(uint8_t fan_id, int frequency, int duty)
{
	#if 0
	int duty_driver = 0;
	unsigned int value;
	duty_driver = frequency / 100 * duty;

	value = Xil_Peripheral_In32(MCOMPAT_PERIPHERAL_S00_AXI_SLV_REG16_OFFSET + fan_id*8);
	value = value | (1<<fan_id);
	Xil_Peripheral_Out32(MCOMPAT_PERIPHERAL_S00_AXI_SLV_REG2_OFFSET, value);

	//Xil_Fans_Out32(XPAR_FANS_CTRL_0_S00_AXI_BASEADDR + FANS_CTRL_S00_AXI_SLV_REG1_OFFSET + fan_id*8, frequency);
	//Xil_Fans_Out32(XPAR_FANS_CTRL_0_S00_AXI_BASEADDR + FANS_CTRL_S00_AXI_SLV_REG0_OFFSET + fan_id*8, duty_driver | (0x1 << 31));
	Xil_Peripheral_Out32(MCOMPAT_PERIPHERAL_S00_AXI_SLV_REG16_OFFSET + fan_id*8, frequency);
	Xil_Peripheral_Out32(MCOMPAT_PERIPHERAL_S00_AXI_SLV_REG17_OFFSET + fan_id*8, duty_driver | (0x1 << 31));
	#else
	int duty_driver = 0;
	uint32_t reg_val;
	duty_driver = frequency / 100 * duty;

	applog(LOG_DEBUG, "%s,%d: fan_id %d, freq: %d duty: %d.", __FILE__, __LINE__, fan_id, frequency, duty);

	Xil_Peripheral_Out32(MCOMPAT_PERIPHERAL_S00_AXI_SLV_REG16_OFFSET + fan_id*8, frequency);
	Xil_Peripheral_Out32(MCOMPAT_PERIPHERAL_S00_AXI_SLV_REG17_OFFSET + fan_id*8, duty_driver);
	reg_val = Xil_Peripheral_In32(MCOMPAT_PERIPHERAL_S00_AXI_SLV_REG2_OFFSET);
	Xil_Peripheral_Out32(MCOMPAT_PERIPHERAL_S00_AXI_SLV_REG2_OFFSET, (reg_val & (~(0x1 << fan_id))) | (0x1 << fan_id));
	#endif
}

int hub_get_button(void)
{
	uint32_t reg_val;

	reg_val = Xil_Peripheral_In32(MCOMPAT_PERIPHERAL_S00_AXI_SLV_REG10_OFFSET);
	return (((reg_val >> 16) & 0x1));
}

void hub_set_green_led(int mode)
{
	uint32_t reg_val,SetRedRegValue;
	reg_val = Xil_Peripheral_In32(MCOMPAT_PERIPHERAL_S00_AXI_SLV_REG9_OFFSET);

	if (LED_ON == mode)
	{
		SetRedRegValue = reg_val | 0x200;
	}
	else
	{
		SetRedRegValue = reg_val & (~0x200);
	}

	Xil_Peripheral_Out32(MCOMPAT_PERIPHERAL_S00_AXI_SLV_REG9_OFFSET,SetRedRegValue);
}

void hub_set_red_led(int mode)
{
	uint32_t reg_val,SetRedRegValue;
	reg_val = Xil_Peripheral_In32(MCOMPAT_PERIPHERAL_S00_AXI_SLV_REG9_OFFSET);

	if (LED_ON == mode)
	{
		SetRedRegValue = reg_val | 0x400;
	}
	else
	{
		SetRedRegValue = reg_val & (~0x400);
	}

	Xil_Peripheral_Out32(MCOMPAT_PERIPHERAL_S00_AXI_SLV_REG9_OFFSET,SetRedRegValue);
}

void init_hub_gpio(void)
{
	Xil_Peripheral_Out32(MCOMPAT_PERIPHERAL_S00_AXI_SLV_REG1_OFFSET, 0);
	sleep(1);
	Xil_Peripheral_Out32(MCOMPAT_PERIPHERAL_S00_AXI_SLV_REG1_OFFSET, 3);
}

void flush_spi(uint8_t chain_id)
{
	uint16_t spi_tx[MCOMPAT_CONFIG_MAX_CMD_LENGTH];
	uint16_t spi_rx[MCOMPAT_CONFIG_MAX_CMD_LENGTH];

	memset(spi_tx, 0, sizeof(spi_tx));
	memset(spi_rx, 0, sizeof(spi_rx));

	opi_spi_transfer(chain_id, spi_tx, spi_rx, MCOMPAT_CONFIG_MAX_CMD_LENGTH);
}

bool opi_spi_read_write(uint8_t chain_id, uint8_t *txbuf, uint8_t *rxbuf, int len)
{
	int i;
	bool ret;
	int len16 = len / 2;
	uint16_t spi_tx[MCOMPAT_CONFIG_MAX_CMD_LENGTH];
	uint16_t spi_rx[MCOMPAT_CONFIG_MAX_CMD_LENGTH];

	if (rxbuf == NULL)
	{
		applog(LOG_ERR, "%s,%s() %d: para erro! ", __FILE__, __func__, __LINE__);
	}

	memset(spi_tx, 0, sizeof(spi_tx));
	memset(spi_rx, 0, sizeof(spi_rx));

	if (txbuf == NULL)
	{
		ret = opi_spi_transfer(chain_id, NULL, spi_rx, len16);
	}
	else
	{
		for(i = 0; i < len16; i++)
		{
			spi_tx[i] = OPI_MAKE_WORD(txbuf[2*i], txbuf[(2*i)+1]);
		}

		ret = opi_spi_transfer(chain_id, spi_tx, spi_rx, len16);
	}

	if (!ret)
	{
		return false;
	}

	for(i = 0; i < len16; i++)
	{
		rxbuf[2*i]		= OPI_HI_BYTE(spi_rx[i]);
		rxbuf[(2*i)+1]	= OPI_LO_BYTE(spi_rx[i]);
	}

	return true;
}


bool opi_send_command(uint8_t chain_id, uint8_t cmd, uint8_t chip_id, uint8_t *buff, int len)
{
	int tx_len;
	uint8_t spi_tx[MCOMPAT_CONFIG_MAX_CMD_LENGTH];
	uint8_t spi_rx[MCOMPAT_CONFIG_MAX_CMD_LENGTH];

	if (buff == NULL)
	{
		applog(LOG_ERR, "%s,%s() %d: para erro! ", __FILE__, __func__, __LINE__);
	}

	memset(spi_tx, 0, sizeof(spi_tx));
	memset(spi_rx, 0, sizeof(spi_rx));

	spi_tx[0] = OPI_HI_BYTE(AX_CMD_SYNC_HEAD);
	spi_tx[1] = OPI_LO_BYTE(AX_CMD_SYNC_HEAD);

	spi_tx[2] = cmd;
	spi_tx[3] = chip_id;

	if (len > 0)
	{
		memcpy(spi_tx + 4, buff, len);
	}

	tx_len = (4 + len + 1) & ~1;

	if (opi_spi_read_write(chain_id, spi_tx, spi_rx, tx_len))
	{
		return true;
	}
	else
	{
		applog(LOG_ERR, "send command fail !");
		return false;
	}
}


bool opi_poll_result(uint8_t chain_id, uint8_t cmd, uint8_t __maybe_unused chip_id, uint8_t *buff, int len)
{
	int i;
	int tx_len;
	uint8_t spi_tx[MCOMPAT_CONFIG_MAX_CMD_LENGTH];
	uint8_t spi_rx[MCOMPAT_CONFIG_MAX_CMD_LENGTH];

	if (buff == NULL)
	{
		applog(LOG_ERR, "%s,%s() %d: para erro! ", __FILE__, __func__, __LINE__);
	}

	memset(spi_tx, 0, sizeof(spi_tx));
	memset(spi_rx, 0, sizeof(spi_rx));

	tx_len = g_chip_num * 4;

	for(i = 0; i < tx_len; i++) {
		cgsleep_ms(1); // usleep(1);

		if (opi_spi_read_write(chain_id, NULL, spi_rx, 2))
		{
			break;
		}
	}

	if (i >= tx_len)
	{
		applog(LOG_ERR, "%s,%d: poll fail !", __FILE__, __LINE__);
		return false;
	}

	if ((spi_rx[0] != OPI_HI_BYTE(AX_CMD_SYNC_HEAD)) || (spi_rx[1] != OPI_LO_BYTE(AX_CMD_SYNC_HEAD)))
	{
		return false;
	}

	opi_spi_read_write(chain_id, NULL, spi_rx, 2);
	if (spi_rx[1] != OPI_STATUS_SUC)
	{
		return false;
	}

	opi_spi_read_write(chain_id, NULL, spi_rx, 2);
	if ((spi_rx[0] & 0x0f) != cmd)
	{
		return false;
	}

	if (len > 0)
	{
		opi_spi_read_write(chain_id, NULL, spi_rx+2, len);
	}

	memcpy(buff, spi_rx, len+2);

	return true;
}



bool opi_send_cmd(uint8_t chain_id, uint8_t cmd, uint8_t *buff, int len)
{
	int tx_len;
	uint8_t spi_tx[MCOMPAT_CONFIG_MAX_CMD_LENGTH];
	uint8_t spi_rx[MCOMPAT_CONFIG_MAX_CMD_LENGTH];

	if ((buff == NULL) && (len != 0))
	{
		applog(LOG_ERR, "%s,%s() %d: para erro! ", __FILE__, __func__, __LINE__);
	}

	memset(spi_tx, 0, sizeof(spi_tx));
	memset(spi_rx, 0, sizeof(spi_rx));

	spi_tx[0] = OPI_HI_BYTE(CUSTOM_SYNC_HEAD);
	spi_tx[1] = OPI_LO_BYTE(CUSTOM_SYNC_HEAD);

	spi_tx[2] = cmd;
	spi_tx[3] = 0;

	if (len > 0)
	{
		memcpy(spi_tx + 4, buff, len);
	}

	tx_len = (4 + len + 1) & ~1;

	if (opi_spi_read_write(chain_id, spi_tx, spi_rx, tx_len))
	{
		return true;
	}
	else
	{
		applog(LOG_ERR, "send command fail !");
		return false;
	}
}


bool opi_poll_rslt(uint8_t chain_id, uint8_t __maybe_unused cmd, uint8_t *buff, int len)
{
	int i;
	int tx_len;
	uint8_t spi_tx[MCOMPAT_CONFIG_MAX_CMD_LENGTH];
	uint8_t spi_rx[MCOMPAT_CONFIG_MAX_CMD_LENGTH];

	if ((buff == NULL) && (len != 0))
	{
		applog(LOG_ERR, "%s,%s() %d: para erro! ", __FILE__, __func__, __LINE__);
	}

	memset(spi_tx, 0, sizeof(spi_tx));
	memset(spi_rx, 0, sizeof(spi_rx));

	tx_len = g_chip_num * 4;

	for(i = 0; i < tx_len; i++) {
		cgsleep_ms(1); // usleep(1);

		if (opi_spi_read_write(chain_id, NULL, spi_rx, 2))
		{
			break;
		}
	}

	if (i >= tx_len)
	{
		applog(LOG_ERR, "%s,%d: poll fail !", __FILE__, __LINE__);
		return false;
	}

	if ((spi_rx[0] != OPI_HI_BYTE(CUSTOM_SYNC_HEAD)) || (spi_rx[1] != OPI_LO_BYTE(CUSTOM_SYNC_HEAD)))
	{
		return false;
	}

	opi_spi_read_write(chain_id, NULL, spi_rx, 2);
	if (spi_rx[1] != OPI_STATUS_SUC)
	{
		return false;
	}

	if (len > 0)
	{
		opi_spi_read_write(chain_id, NULL, spi_rx+2, len);
		memcpy(buff, spi_rx+2, len);
	}

	return true;
}


void opi_set_power_en(unsigned char chain_id, int val)
{
	unsigned char tx_buf[MCOMPAT_CONFIG_MAX_CMD_LENGTH];

	applog(LOG_DEBUG, "%s,%d: %s ", __FILE__, __LINE__, __FUNCTION__);

	tx_buf[0] = (val >> 0) & 0xff;
	tx_buf[1] = (val >> 8) & 0xff;

	if (!opi_send_cmd(chain_id, OPI_SET_POWER_EN, tx_buf, 2))
	{
		applog(LOG_WARNING, "%s,%d: %s send fail !", __FILE__, __LINE__, __FUNCTION__);
		return;
	}

	if (!opi_poll_rslt(chain_id, OPI_SET_POWER_EN, NULL, 0))
	{
		applog(LOG_WARNING, "%s,%d: %s poll fail !", __FILE__, __LINE__, __FUNCTION__);
		return;
	}
}


void opi_set_start_en(unsigned char chain_id, int val)
{
	unsigned char tx_buf[MCOMPAT_CONFIG_MAX_CMD_LENGTH];

	applog(LOG_DEBUG, "%s,%d: %s ", __FILE__, __LINE__, __FUNCTION__);

	tx_buf[0] = (val >> 0) & 0xff;
	tx_buf[1] = (val >> 8) & 0xff;

	if (!opi_send_cmd(chain_id, OPI_SET_STARR_EN, tx_buf, 2))
	{
		applog(LOG_WARNING, "%s,%d: %s send fail !", __FILE__, __LINE__, __FUNCTION__);
		return;
	}

	if (!opi_poll_rslt(chain_id, OPI_SET_STARR_EN, NULL, 0))
	{
		applog(LOG_WARNING, "%s,%d: %s poll fail !", __FILE__, __LINE__, __FUNCTION__);
		return;
	}
}


bool opi_set_reset(unsigned char chain_id, int val)
{
	unsigned char tx_buf[MCOMPAT_CONFIG_MAX_CMD_LENGTH];

	applog(LOG_DEBUG, "%s,%d: %s ", __FILE__, __LINE__, __FUNCTION__);

	tx_buf[0] = (val >> 0) & 0xff;
	tx_buf[1] = (val >> 8) & 0xff;

	if (!opi_send_cmd(chain_id, OPI_SET_RESET, tx_buf, 2)) {
		applog(LOG_WARNING, "%s,%d: %s send fail !", __FILE__, __LINE__, __FUNCTION__);
		return false;
	}

	if (!opi_poll_rslt(chain_id, OPI_SET_RESET, NULL, 0)) {
		applog(LOG_WARNING, "%s,%d: %s poll fail !", __FILE__, __LINE__, __FUNCTION__);
		return false;
	}
	return true;
}


void opi_set_led(unsigned char chain_id, int val)
{
	unsigned char tx_buf[MCOMPAT_CONFIG_MAX_CMD_LENGTH];

	applog(LOG_DEBUG, "%s,%d: %s ", __FILE__, __LINE__, __FUNCTION__);

	tx_buf[0] = (val >> 0) & 0xff;
	tx_buf[1] = (val >> 8) & 0xff;

	if (!opi_send_cmd(chain_id, OPI_SET_LED, tx_buf, 2))
	{
		applog(LOG_WARNING, "%s,%d: %s send fail !", __FILE__, __LINE__, __FUNCTION__);
		return;
	}

	if (!opi_poll_rslt(chain_id, OPI_SET_LED, NULL, 0))
	{
		applog(LOG_WARNING, "%s,%d: %s poll fail !", __FILE__, __LINE__, __FUNCTION__);
		return;
	}
}


int opi_get_plug(unsigned char chain_id)
{    
	unsigned char rx_buf[MCOMPAT_CONFIG_MAX_CMD_LENGTH];

	applog(LOG_DEBUG, "%s,%d: %s ", __FILE__, __LINE__, __FUNCTION__);

	if (!opi_send_cmd(chain_id, OPI_SET_LED, NULL, 0))
	{
		applog(LOG_WARNING, "%s,%d: %s send fail !", __FILE__, __LINE__, __FUNCTION__);
		return -1;
	}

	memset(rx_buf, 0, sizeof(rx_buf));
	if (!opi_poll_rslt(chain_id, OPI_SET_LED, rx_buf, 2))
	{
		applog(LOG_WARNING, "%s,%d: %s poll fail !", __FILE__, __LINE__, __FUNCTION__);
		return -1;
	}

	return (int)(rx_buf[0]);
}


bool opi_set_vid(unsigned char chain_id, int vid)
{
	unsigned char tx_buf[MCOMPAT_CONFIG_MAX_CMD_LENGTH];

	applog(LOG_DEBUG, "%s,%d: %s ", __FILE__, __LINE__, __FUNCTION__);

	tx_buf[0] = (vid >> 0) & 0xff;
	tx_buf[1] = (vid >> 8) & 0xff;

	if (!opi_send_cmd(chain_id, OPI_SET_VID, tx_buf, 2))
	{
		applog(LOG_WARNING, "%s,%d: %s send fail !", __FILE__, __LINE__, __FUNCTION__);
		return false;
	}

	if (!opi_poll_rslt(chain_id, OPI_SET_VID, NULL, 0))
	{
		applog(LOG_WARNING, "%s,%d: %s poll fail !", __FILE__, __LINE__, __FUNCTION__);
		return false;
	}

	return true;
}


void opi_set_pwm(unsigned char fan_id, int frequency, int duty)
{
	unsigned char tx_buf[MCOMPAT_CONFIG_MAX_CMD_LENGTH];

	applog(LOG_DEBUG, "%s,%d: %s ", __FILE__, __LINE__, __FUNCTION__);

	tx_buf[0] = fan_id;
	tx_buf[1] = 0x00;

	tx_buf[2] = (frequency >> 0) & 0xff;
	tx_buf[3] = (frequency >> 8) & 0xff;
	tx_buf[4] = (frequency >> 16) & 0xff;
	tx_buf[5] = (frequency >> 24) & 0xff;

	tx_buf[6] = (duty >> 0) & 0xff;
	tx_buf[7] = (duty >> 8) & 0xff;

	if (!opi_send_cmd(0, OPI_SET_PWM, tx_buf, 8))
	{
		applog(LOG_WARNING, "%s,%d: %s send fail !", __FILE__, __LINE__, __FUNCTION__);
		return;
	}

	if (!opi_poll_rslt(0, OPI_SET_PWM, NULL, 0))
	{
		applog(LOG_WARNING, "%s,%d: %s poll fail !", __FILE__, __LINE__, __FUNCTION__);
		return;
	}
}

bool opi_chain_power_on(unsigned char chain_id)
{
	applog(LOG_DEBUG, "%s,%d: %s ", __FILE__, __LINE__, __FUNCTION__);

	if (!opi_send_cmd(chain_id, OPI_POWER_ON, NULL, 0))
	{
		applog(LOG_WARNING, "%s,%d: %s send fail !", __FILE__, __LINE__, __FUNCTION__);
		return false;
	}

	if (!opi_poll_rslt(chain_id, OPI_POWER_ON, NULL, 0))
	{
		applog(LOG_WARNING, "%s,%d: %s poll fail !", __FILE__, __LINE__, __FUNCTION__);
		return false;
	}

	return true;
}


bool opi_chain_power_down(unsigned char chain_id)
{
	applog(LOG_DEBUG, "%s,%d: %s ", __FILE__, __LINE__, __FUNCTION__);

	if (!opi_send_cmd(chain_id, OPI_POWER_DOWN, NULL, 0))
	{
		applog(LOG_WARNING, "%s,%d: %s send fail !", __FILE__, __LINE__, __FUNCTION__);
		return false;
	}

	if (!opi_poll_rslt(chain_id, OPI_POWER_DOWN, NULL, 0))
	{
		applog(LOG_WARNING, "%s,%d: %s poll fail !", __FILE__, __LINE__, __FUNCTION__);
		return false;
	}

	return true;
}

bool opi_chain_hw_reset(unsigned char chain_id)
{
	applog(LOG_DEBUG, "%s,%d: %s ", __FILE__, __LINE__, __FUNCTION__);

	if (!opi_send_cmd(chain_id, OPI_POWER_RESET, NULL, 0))
	{
		applog(LOG_WARNING, "%s,%d: %s send fail !", __FILE__, __LINE__, __FUNCTION__);
		return false;
	}

	if (!opi_poll_rslt(chain_id, OPI_POWER_RESET, NULL, 0))
	{
		applog(LOG_WARNING, "%s,%d: %s poll fail !", __FILE__, __LINE__, __FUNCTION__);
		return false;
	}

	return true;
}


bool opi_chain_power_on_all(void)
{
	int i;

	for(i = 0; i < g_chain_num; i++)
	{
		opi_chain_power_on(i);
	}

	return true;
}

bool opi_chain_power_down_all(void)
{
	int i;

	for(i = 0; i < g_chain_num; i++)
	{
		opi_chain_power_down(i);
	}

	return true;
}


void opi_set_spi_speed(unsigned char chain_id, int index)
{
	unsigned char tx_buf[MCOMPAT_CONFIG_MAX_CMD_LENGTH];

	applog(LOG_DEBUG, "%s,%d: %s ", __FILE__, __LINE__, __FUNCTION__);

	tx_buf[0] = (index >> 0) & 0xff;
	tx_buf[1] = (index >> 8) & 0xff;

	if (!opi_send_cmd(chain_id, OPI_SET_SPI_SPEED, tx_buf, 2))
	{
		applog(LOG_WARNING, "%s,%d: %s send fail !", __FILE__, __LINE__, __FUNCTION__);
		return;
	}

	if (!opi_poll_rslt(chain_id, OPI_SET_SPI_SPEED, NULL, 0))
	{
		applog(LOG_WARNING, "%s,%d: %s poll fail !", __FILE__, __LINE__, __FUNCTION__);
		return;
	}
}

const int pin_power_en[] =
{
	MCOMPAT_CONFIG_CHAIN0_POWER_EN_GPIO,
	MCOMPAT_CONFIG_CHAIN1_POWER_EN_GPIO,
	MCOMPAT_CONFIG_CHAIN2_POWER_EN_GPIO,
	MCOMPAT_CONFIG_CHAIN3_POWER_EN_GPIO,
	MCOMPAT_CONFIG_CHAIN4_POWER_EN_GPIO,
	MCOMPAT_CONFIG_CHAIN5_POWER_EN_GPIO,
	MCOMPAT_CONFIG_CHAIN6_POWER_EN_GPIO,
	MCOMPAT_CONFIG_CHAIN7_POWER_EN_GPIO
};

const int pin_start_en[] =
{
	MCOMPAT_CONFIG_CHAIN0_START_EN_GPIO,
	MCOMPAT_CONFIG_CHAIN1_START_EN_GPIO,
	MCOMPAT_CONFIG_CHAIN2_START_EN_GPIO,
	MCOMPAT_CONFIG_CHAIN3_START_EN_GPIO,
	MCOMPAT_CONFIG_CHAIN4_START_EN_GPIO,
	MCOMPAT_CONFIG_CHAIN5_START_EN_GPIO,
	MCOMPAT_CONFIG_CHAIN6_START_EN_GPIO,
	MCOMPAT_CONFIG_CHAIN7_START_EN_GPIO
};

const int pin_reset[] =
{
	MCOMPAT_CONFIG_CHAIN0_RESET_GPIO,
	MCOMPAT_CONFIG_CHAIN1_RESET_GPIO,
	MCOMPAT_CONFIG_CHAIN2_RESET_GPIO,
	MCOMPAT_CONFIG_CHAIN3_RESET_GPIO,
	MCOMPAT_CONFIG_CHAIN4_RESET_GPIO,
	MCOMPAT_CONFIG_CHAIN5_RESET_GPIO,
	MCOMPAT_CONFIG_CHAIN6_RESET_GPIO,
	MCOMPAT_CONFIG_CHAIN7_RESET_GPIO
};

const int pin_plug[] =
{
	MCOMPAT_CONFIG_CHAIN0_PLUG_GPIO,
	MCOMPAT_CONFIG_CHAIN1_PLUG_GPIO,
	MCOMPAT_CONFIG_CHAIN2_PLUG_GPIO,
	MCOMPAT_CONFIG_CHAIN3_PLUG_GPIO,
	MCOMPAT_CONFIG_CHAIN4_PLUG_GPIO,
	MCOMPAT_CONFIG_CHAIN5_PLUG_GPIO,
	MCOMPAT_CONFIG_CHAIN6_PLUG_GPIO,
	MCOMPAT_CONFIG_CHAIN7_PLUG_GPIO
};

const int pin_led[] =
{
	MCOMPAT_CONFIG_CHAIN0_LED_GPIO,
	MCOMPAT_CONFIG_CHAIN1_LED_GPIO,
	MCOMPAT_CONFIG_CHAIN2_LED_GPIO,
	MCOMPAT_CONFIG_CHAIN3_LED_GPIO,
	MCOMPAT_CONFIG_CHAIN4_LED_GPIO,
	MCOMPAT_CONFIG_CHAIN5_LED_GPIO,
	MCOMPAT_CONFIG_CHAIN6_LED_GPIO,
	MCOMPAT_CONFIG_CHAIN7_LED_GPIO
};


void spi_send_data_in_word(ZYNQ_SPI_T *spi, unsigned char *buf, int len)
{
	int i;

	for(i = 0; i < len; i = i + 2)
	{
		zynq_spi_write(spi, buf + i, 2);
	}
}

void spi_recv_data_in_word(ZYNQ_SPI_T *spi, unsigned char *buf, int len)
{
	int i;

	for(i = 0; i < len; i = i + 2)
	{
		zynq_spi_read(spi, buf + i, 2);
	}
}

void spi_send_data(ZYNQ_SPI_T *spi, unsigned char *buf, int len)
{
	zynq_spi_write(spi, buf, len);
}

void spi_recv_data(ZYNQ_SPI_T *spi, unsigned char *buf, int len)
{
	zynq_spi_read(spi, buf, len);
}


bool spi_send_command(ZYNQ_SPI_T *spi, unsigned char cmd, unsigned char chip_id, unsigned char *buff, int len)
{
	int tx_len;
	unsigned char tx_buf[MCOMPAT_CONFIG_MAX_CMD_LENGTH];

	if ((len > 0) && (buff == NULL))
	{
		applog(LOG_ERR, "%s,%d: para error !", __FILE__, __LINE__);
		return false;
	}

	memset(tx_buf, 0, sizeof(tx_buf));

	tx_buf[0] = cmd;
	tx_buf[1] = chip_id;

	if (len > 0)
	{
		memcpy(tx_buf + 2, buff, len);
	}

	tx_len = (2 + len + 1) & ~1;
	//spi_send_data_in_word(spi, tx_buf, tx_len);
	spi_send_data(spi, tx_buf, tx_len);

	return true;
}


bool spi_poll_result(ZYNQ_SPI_T *spi, unsigned char cmd, unsigned char __maybe_unused chip_id, unsigned char *buff, int len)
{
	int i;
	int max_len;
	unsigned char rx_buf[MCOMPAT_CONFIG_MAX_CMD_LENGTH];

	max_len = g_chip_num * 4;
	memset(rx_buf, 0, sizeof(rx_buf));

	for(i = 0; i < max_len; i = i + 2)
	{
		spi_recv_data(spi, rx_buf, 2);
		if ((rx_buf[0] & 0x0f) == cmd)
		{
			break;
		}
	}

	if (i >= max_len)
	{
		applog(LOG_ERR, "%s,%d: poll fail !", __FILE__, __LINE__);
		return false;
	}

	spi_recv_data_in_word(spi, rx_buf+2, len);
	memcpy(buff, rx_buf, len+2);

	return true;
}



void init_spi_gpio(int chain_num)
{
	int i;

	for(i = 0; i < chain_num; i++)
	{
		zynq_gpio_init(pin_power_en[i], 0);
		zynq_gpio_init(pin_start_en[i], 0);
		zynq_gpio_init(pin_reset[i], 0);
		zynq_gpio_init(pin_led[i], 0);
		zynq_gpio_init(pin_plug[i], 1);
	}
}

void exit_spi_gpio(int chain_num)
{
	int i;

	for(i = 0; i < chain_num; i++)
	{
		zynq_gpio_exit(pin_power_en[i]);
		zynq_gpio_exit(pin_start_en[i]);
		zynq_gpio_exit(pin_reset[i]);
		zynq_gpio_exit(pin_led[i]);
		zynq_gpio_exit(pin_plug[i]);
	}
}


void spi_set_power_en(unsigned char chain_id, int val)
{
	zynq_gpio_write(pin_power_en[chain_id], val);
}

void spi_set_start_en(unsigned char chain_id, int val)
{
	zynq_gpio_write(pin_start_en[chain_id], val);
}

bool spi_set_reset(unsigned char chain_id, int val)
{
	return zynq_gpio_write(pin_reset[chain_id], val);
}

void spi_set_led(unsigned char chain_id, int val)
{
	zynq_gpio_write(pin_led[chain_id], val);
}

int spi_get_plug(unsigned char chain_id)
{
	return zynq_gpio_read(pin_plug[chain_id]);
}

static int s_vid = 0;
bool spi_set_vid(unsigned char chain_id, int vid)
{
	if (g_platform == PLATFORM_ZYNQ_SPI_G19)
	{
		zynq_gpio_g19_vid_set(chain_id, vid);
	}
	else if (g_platform == PLATFORM_ZYNQ_SPI_G9)
	{
		if (s_vid != vid)
		{
			zynq_gpio_g9_vid_set(vid);
		}
	}
	else
	{
		applog(LOG_ERR, "platform[%d] error in set vid ", g_platform);
		return false;
	}

	return true;
}

void spi_set_spi_speed(unsigned char __maybe_unused chain_id, int index)
{
	uint32_t cfg[] = {390625, 781250, 1562500, 3125000, 6250000, 9960000};

	zynq_set_spi_speed(cfg[index]);
}


bool zynq_chain_power_on(unsigned char chain_id)
{

	if (mcompat_get_plug(chain_id) != 0)
	{
		applog(LOG_NOTICE, "chain %d >>> the board not inserted !!!", chain_id);
		return false;
	}

	mcompat_set_power_en(chain_id, 1);
	sleep(5);
	mcompat_set_reset(chain_id, 1);
	sleep(1);
	mcompat_set_start_en(chain_id, 1);

	return true;
}


bool zynq_chain_power_down(unsigned char chain_id)
{
	mcompat_set_power_en(chain_id, 0);
	sleep(1);
	mcompat_set_reset(chain_id, 0);
	mcompat_set_start_en(chain_id, 0);
	mcompat_set_led(chain_id, 1);

	return true;
}


bool zynq_chain_hw_reset(unsigned char chain_id)
{
	mcompat_set_reset(chain_id, 0);
	sleep(1);
	mcompat_set_reset(chain_id, 1);
	sleep(1);

	return true;
}


bool zynq_chain_power_on_all(void)
{
	int i;

	for(i = 0; i < g_chain_num; i++)
	{
		if (mcompat_get_plug(i) != 0)
		{
			applog(LOG_NOTICE, "chain %d >>> the board not inserted !!! ", i);
		}
	}

	for(i = 0; i < g_chain_num; i++) {
		mcompat_set_power_en(i, 1);
		cgsleep_ms(5);
	}

	sleep(5);

	for(i = 0; i < g_chain_num; i++) {
		mcompat_set_reset(i, 1);
		cgsleep_ms(5);
	}

	sleep(1);

	for(i = 0; i < g_chain_num; i++) {
		mcompat_set_start_en(i, 1);
		cgsleep_ms(5);
	}

	return true;
}


bool zynq_chain_power_down_all(void)
{
	int i;

	for(i = 0; i < g_chain_num; i++) {
		mcompat_set_power_en(i, 0);
	}

	sleep(1);

	for(i = 0; i < g_chain_num; i++) {
		mcompat_set_reset(i, 0);
		mcompat_set_start_en(i, 0);
		mcompat_set_led(i, 1);
	}

	return true;
}

bool init_hub_cmd(int chain_num, int chip_num)
{
	int i;

	for(i = 0; i < chain_num; i++)
	{
		hub_spi_init(i, chip_num);
	}

	return true;
}

bool exit_hub_cmd(int __maybe_unused chain_num)
{
	return true;
}


bool hub_cmd_reset(unsigned char chain_id, unsigned char chip_id, unsigned char *in, unsigned char *out)
{
	uint32_t cfg_len = 0;
	uint8_t spi_tx[MCOMPAT_CONFIG_MAX_CMD_LENGTH];
	uint8_t spi_rx[MCOMPAT_CONFIG_MAX_CMD_LENGTH];

	applog(LOG_DEBUG, "%s,%d: %s ", __FILE__, __LINE__, __FUNCTION__);

	memset(spi_tx, 0, sizeof(spi_tx));
	memset(spi_rx, 0, sizeof(spi_rx));

	spi_tx[0] = CMD_RESET;
	spi_tx[1] = chip_id;
	spi_tx[2] = in[0];
	spi_tx[3] = in[1];

	//cfg_len = 0x03000200;
	cfg_len += (0x03) << 24;
	cfg_len += (0x00) << 16;
	cfg_len += (0x02) << 8;
	cfg_len += (0x00) << 0;

	if (do_spi_cmd(chain_id, spi_tx, spi_rx, cfg_len) == XST_FAILURE)
	{
		return false;
	}

	//print_data_hex("tx:", spi_tx, 8);
	//print_data_hex("rx:", spi_rx, 8);
	memcpy(out, spi_rx, 4);

	return true;
}


int hub_cmd_bist_start(unsigned char chain_id, unsigned char chip_id)
{
	uint32_t cfg_len = 0;
	uint8_t spi_tx[MCOMPAT_CONFIG_MAX_CMD_LENGTH];
	uint8_t spi_rx[MCOMPAT_CONFIG_MAX_CMD_LENGTH];

	applog(LOG_DEBUG, "%s,%d: %s ", __FILE__, __LINE__, __FUNCTION__);

	memset(spi_tx, 0, sizeof(spi_tx));
	memset(spi_rx, 0, sizeof(spi_rx));

	spi_tx[0] = CMD_BIST_START;
	spi_tx[1] = chip_id;

	//cfg_len = 0x02000200;
	cfg_len += (0x02) << 24;
	cfg_len += (0x00) << 16;
	cfg_len += (0x02) << 8;
	cfg_len += (0x00) << 0;

	if (do_spi_cmd(chain_id, spi_tx, spi_rx, cfg_len) == XST_FAILURE)
	{
		return -1;
	}

	//print_data_hex("tx:", spi_tx, 8);
	//print_data_hex("rx:", spi_rx, 8);

	return spi_rx[3];;
}


bool hub_cmd_bist_collect(unsigned char chain_id, unsigned char chip_id)
{
	uint32_t cfg_len = 0;
	uint8_t spi_tx[MCOMPAT_CONFIG_MAX_CMD_LENGTH];
	uint8_t spi_rx[MCOMPAT_CONFIG_MAX_CMD_LENGTH];

	memset(spi_tx, 0, sizeof(spi_tx));
	memset(spi_rx, 0, sizeof(spi_rx));

	spi_tx[0] = CMD_BIST_COLLECT;
	spi_tx[1] = chip_id;

	//cfg_len = 0x02000200;
	cfg_len += (0x02) << 24;
	cfg_len += (0x00) << 16;
	cfg_len += (0x02) << 8;
	cfg_len += (0x00) << 0;

	if (do_spi_cmd(chain_id, spi_tx, spi_rx, cfg_len) == XST_FAILURE)
	{
		return false;
	}

	return true;
}


bool hub_cmd_bist_fix(unsigned char chain_id, unsigned char chip_id)
{
	uint32_t cfg_len = 0;
	uint8_t spi_tx[MCOMPAT_CONFIG_MAX_CMD_LENGTH];
	uint8_t spi_rx[MCOMPAT_CONFIG_MAX_CMD_LENGTH];

	memset(spi_tx, 0, sizeof(spi_tx));
	memset(spi_rx, 0, sizeof(spi_rx));

	spi_tx[0] = CMD_BIST_FIX;
	spi_tx[1] = chip_id;

	//cfg_len = 0x02000200;
	cfg_len += (0x02) << 24;
	cfg_len += (0x00) << 16;
	cfg_len += (0x02) << 8;
	cfg_len += (0x00) << 0;

	if (do_spi_cmd(chain_id, spi_tx, spi_rx, cfg_len) == XST_FAILURE)
	{
		return false;
	}

	return true;
}


bool hub_cmd_write_register(unsigned char chain_id, unsigned char chip_id, unsigned char *reg, int len)
{
	uint32_t cfg_len = 0;
	uint8_t spi_tx[MCOMPAT_CONFIG_MAX_CMD_LENGTH];
	uint8_t spi_rx[MCOMPAT_CONFIG_MAX_CMD_LENGTH];

	memset(spi_tx, 0, sizeof(spi_tx));
	memset(spi_rx, 0, sizeof(spi_rx));

	spi_tx[0] = CMD_WRITE_REG;
	spi_tx[1] = chip_id;
	memcpy(spi_tx + 2, reg, len);

	//cfg_len = 0x09070807;
	cfg_len += (((len / 2) + 2) & 0xff) << 24;
	cfg_len += (((len / 2) + 1) & 0xff) << 16;
	cfg_len += (((len / 2) + 2) & 0xff) << 8;
	cfg_len += (((len / 2) + 1) & 0xff) << 0;

	if (do_spi_cmd(chain_id, spi_tx, spi_rx, cfg_len) == XST_FAILURE)
	{
		return false;
	}

	return true;
}


bool hub_cmd_read_register(unsigned char chain_id, unsigned char chip_id, unsigned char *reg, int len)
{
	uint32_t cfg_len = 0;
	uint8_t spi_tx[MCOMPAT_CONFIG_MAX_CMD_LENGTH];
	uint8_t spi_rx[MCOMPAT_CONFIG_MAX_CMD_LENGTH];
	int i;
	unsigned short crc1, crc2;
	uint8_t tmp_buf[MCOMPAT_CONFIG_MAX_CMD_LENGTH];

	memset(spi_tx, 0, sizeof(spi_tx));
	memset(spi_rx, 0, sizeof(spi_rx));

	spi_tx[0] = CMD_READ_REG;
	spi_tx[1] = chip_id;

	//cfg_len = 0x02000807;
	cfg_len += (0x02) << 24;
	cfg_len += (0x00) << 16;
	cfg_len += (((len / 2) + 2) & 0xff) << 8;
	cfg_len += (((len / 2) + 1) & 0xff) << 0;

	if (do_spi_cmd(chain_id, spi_tx, spi_rx, cfg_len) == XST_FAILURE)
	{
		return false;
	}

	for(i = 0; i < len + 2; i = i + 2)
	{
		tmp_buf[i + 0] = spi_rx[i + 1];
		tmp_buf[i + 1] = spi_rx[i + 0];
	}
	crc1 = CRC16_2(tmp_buf, len + 2);
	crc2 = (spi_rx[2 + len + 0] << 8) + (spi_rx[2 + len + 1] << 0);

	if (crc1 != crc2) {
		applog(LOG_WARNING, "%s crc error !", __FUNCTION__);
		return false;
	}

	memcpy(reg, spi_rx + 2, len);

	return true;
}


bool hub_cmd_read_write_reg0d(unsigned char chain_id, unsigned char chip_id, unsigned char *in, int len, unsigned char *out)
{
	uint32_t cfg_len = 0;
	uint8_t spi_tx[MCOMPAT_CONFIG_MAX_CMD_LENGTH];
	uint8_t spi_rx[MCOMPAT_CONFIG_MAX_CMD_LENGTH];
	int i;
	unsigned short crc1, crc2;
	uint8_t tmp_buf[MCOMPAT_CONFIG_MAX_CMD_LENGTH];

	memset(spi_tx, 0, sizeof(spi_tx));
	memset(spi_rx, 0, sizeof(spi_rx));

	spi_tx[0] = CMD_WRITE_REG0d;
	spi_tx[1] = chip_id;
	memcpy(spi_tx + 2, in, len);

	//cfg_len = 0x09070807;
	cfg_len += (((len / 2) + 2) & 0xff) << 24;
	cfg_len += (((len / 2) + 1) & 0xff) << 16;
	cfg_len += (((len / 2) + 2) & 0xff) << 8;
	cfg_len += (((len / 2) + 1) & 0xff) << 0;

	if (do_spi_cmd(chain_id, spi_tx, spi_rx, cfg_len) == XST_FAILURE)
	{
		return false;
	}

	for(i = 0; i < len + 2; i = i + 2)
	{
		tmp_buf[i + 0] = spi_rx[i + 1];
		tmp_buf[i + 1] = spi_rx[i + 0];
	}
	crc1 = CRC16_2(tmp_buf, len + 2);
	crc2 = (spi_rx[2 + len + 0] << 8) + (spi_rx[2 + len + 1] << 0);

	if (crc1 != crc2) {
		applog(LOG_WARNING, "%s crc error !", __FUNCTION__);
		return false;
	}

	memcpy(out, spi_rx + 2, len);

	return true;
}

bool hub_cmd_write_job(unsigned char chain_id, unsigned char chip_id, unsigned char *job, int len)
{
	uint32_t cfg_len = 0;
	uint8_t spi_tx[MCOMPAT_CONFIG_MAX_CMD_LENGTH];
	uint8_t spi_rx[MCOMPAT_CONFIG_MAX_CMD_LENGTH];

	memset(spi_tx, 0, sizeof(spi_tx));
	memset(spi_rx, 0, sizeof(spi_rx));

	memcpy(spi_tx, job, len);
	//cfg_len = 0x504f0000;
	cfg_len += (((len / 2) + 1) & 0xff) << 24;
	cfg_len += (((len / 2) - 1) & 0xff) << 16;
	cfg_len += (0x00) << 8;
	cfg_len += (0x00) << 0;

	if (send_job_queue(chain_id, spi_tx, spi_rx, cfg_len, (chip_id == 1)) == XST_FAILURE)
	{
		return false;
	}

	return true;
}


bool hub_cmd_read_result(unsigned char chain_id, unsigned char chip_id, unsigned char *res, int len)
{
	uint32_t cfg_len = 0;
	uint8_t spi_tx[MCOMPAT_CONFIG_MAX_CMD_LENGTH];
	uint8_t spi_rx[MCOMPAT_CONFIG_MAX_CMD_LENGTH];
	int i;
	unsigned short crc1, crc2;
	uint8_t tmp_buf[MCOMPAT_CONFIG_MAX_CMD_LENGTH];

	memset(spi_tx, 0, sizeof(spi_tx));
	memset(spi_rx, 0, sizeof(spi_rx));

	spi_tx[0] = CMD_READ_RESULT;
	spi_tx[1] = chip_id;

//	cfg_len = 0x02000605;
	cfg_len += (0x02) << 24;
	cfg_len += (0x00) << 16;
	cfg_len += (((len / 2) + 3) & 0xff) << 8;
	cfg_len += (((len / 2) + 2) & 0xff) << 0;

	if (do_spi_cmd(chain_id, spi_tx, spi_rx, cfg_len) == XST_FAILURE)
		return false;

	if (spi_rx[1] == 0)
		return false;

	for (i = 0; i < len + 2; i = i + 2) {
		tmp_buf[i + 0] = spi_rx[i + 1];
		tmp_buf[i + 1] = spi_rx[i + 0];
	}
	crc1 = CRC16_2(tmp_buf, len + 2);
	crc2 = (spi_rx[2 + len + 0] << 8) + (spi_rx[2 + len + 1] << 0);

	if (crc1 != crc2) {
		applog(LOG_WARNING, "%s crc error !", __FUNCTION__);
		return false;
	}

	memcpy(res, spi_rx, len + 2);

	return true;
}


bool hub_cmd_auto_nonce(unsigned char chain_id, int mode, int len)
{
	uint16_t cmd08 = 0x0800;
	uint32_t cfg_len = 0;

//	cfg_len = 0x02000605;
	cfg_len += (0x02) << 24;
	cfg_len += (0x00) << 16;
	cfg_len += (((len / 2) + 3) & 0xff) << 8;
	cfg_len += (((len / 2) + 2) & 0xff) << 0;

	if (mode == 0)
		disable_auto_nonce(chain_id);
	else
		enable_auto_nonce(chain_id, cmd08, cfg_len);

	return true;
}

bool hub_cmd_read_nonce(unsigned char chain_id, unsigned char *res, int len)
{
	uint32_t cfg_len = 0;
	uint8_t spi_tx[MCOMPAT_CONFIG_MAX_CMD_LENGTH];
	uint8_t spi_rx[MCOMPAT_CONFIG_MAX_CMD_LENGTH];

	memset(spi_tx, 0, sizeof(spi_tx));
	memset(spi_rx, 0, sizeof(spi_rx));

//	cfg_len = 0x02000605;
	cfg_len += (0x02) << 24;
	cfg_len += (0x00) << 16;
	cfg_len += (((len / 2) + 3) & 0xff) << 8;
	cfg_len += (((len / 2) + 2) & 0xff) << 0;

	if (!rece_queue_has_nonce(chain_id, 1000))
		return false;

	read_nonce_buffer(chain_id, spi_rx, cfg_len);

	if (spi_rx[1] == 0)
		return false;

	//print_data_hex("read_nonce rx:", spi_rx, len + 2);
	memcpy(res, spi_rx, len + 2);

	return true;
}


bool hub_cmd_get_temp(mcompat_fan_temp_s *fan_temp_ctrl,unsigned char chain_id)
{
	uint32_t val;

	if (hub_get_plug(chain_id))
		return false;

	enable_auto_cmd0a(chain_id,g_dangerous_temp,33,24,0,0);
	mcompat_temp_s *temp_ctrl = &fan_temp_ctrl->mcompat_temp[chain_id];

	do{
		val = Xil_SPI_In32(SPI_AXIBASE + SPI_BASEADDR_GAP*chain_id+AUTO_CMD0A_REG4_ADDR);
	}while(!((val >> 24) & 0x1));

	hub_get_hitemp_stat(chain_id,temp_ctrl);
	hub_get_lotemp_stat(chain_id,temp_ctrl);
	hub_get_avgtemp_stat(chain_id,temp_ctrl);
	disable_auto_cmd0a(chain_id,g_dangerous_temp,33,24,0,0);


	return true;
}


bool init_opi_cmd(void)
{
	opi_spi_init();

	return true;
}

bool exit_opi_cmd(void)
{
	opi_spi_exit();

	return true;
}


bool opi_cmd_reset(unsigned char chain_id, unsigned char chip_id, unsigned char *in, unsigned char *out)
{
	unsigned char rx_buf[MCOMPAT_CONFIG_MAX_CMD_LENGTH];

	applog(LOG_DEBUG, "%s,%d: %s ", __FILE__, __LINE__, __FUNCTION__);

	if (!opi_send_command(chain_id, CMD_RESET, chip_id, in, 2))
	{
		applog(LOG_WARNING, "%s,%d: %s send fail !", __FILE__, __LINE__, __FUNCTION__);
		return false;
	}

	memset(rx_buf, 0, sizeof(rx_buf));
	if (!opi_poll_result(chain_id, CMD_RESET, chip_id, rx_buf, 2))
	{
		applog(LOG_WARNING, "%s,%d: %s poll fail !", __FILE__, __LINE__, __FUNCTION__);
		return false;
	}

	memcpy(out, rx_buf, 4);

	return true;
}


int opi_cmd_bist_start(unsigned char chain_id, unsigned char chip_id)
{
	unsigned char tx_buf[MCOMPAT_CONFIG_MAX_CMD_LENGTH];
	unsigned char rx_buf[MCOMPAT_CONFIG_MAX_CMD_LENGTH];

	applog(LOG_DEBUG, "%s,%d: %s ", __FILE__, __LINE__, __FUNCTION__);

	memset(tx_buf, 0, sizeof(tx_buf));
	if (!opi_send_command(chain_id, CMD_BIST_START, chip_id, tx_buf, 2))
	{
		applog(LOG_WARNING, "%s,%d: %s send fail !", __FILE__, __LINE__, __FUNCTION__);
		return -1;
	}

	memset(rx_buf, 0, sizeof(rx_buf));
	if (!opi_poll_result(chain_id, CMD_BIST_START, chip_id, rx_buf, 2))
	{
		applog(LOG_WARNING, "%s,%d: %s poll fail !", __FILE__, __LINE__, __FUNCTION__);
		return -1;
	}

	return rx_buf[3];
}


bool opi_cmd_bist_collect(unsigned char chain_id, unsigned char chip_id)
{
	unsigned char tx_buf[MCOMPAT_CONFIG_MAX_CMD_LENGTH];
	unsigned char rx_buf[MCOMPAT_CONFIG_MAX_CMD_LENGTH];

	applog(LOG_DEBUG, "%s,%d: %s ", __FILE__, __LINE__, __FUNCTION__);

	memset(tx_buf, 0, sizeof(tx_buf));
	if (!opi_send_command(chain_id, CMD_BIST_COLLECT, chip_id, tx_buf, 2))
	{
		applog(LOG_WARNING, "%s,%d: %s send fail !", __FILE__, __LINE__, __FUNCTION__);
		return false;
	}

	memset(rx_buf, 0, sizeof(rx_buf));
	if (!opi_poll_result(chain_id, CMD_BIST_COLLECT, chip_id, rx_buf, 2))
	{
		applog(LOG_WARNING, "%s,%d: %s poll fail !", __FILE__, __LINE__, __FUNCTION__);
		return false;
	}

	return true;
}


bool opi_cmd_bist_fix(unsigned char chain_id, unsigned char chip_id)
{
	unsigned char tx_buf[MCOMPAT_CONFIG_MAX_CMD_LENGTH];
	unsigned char rx_buf[MCOMPAT_CONFIG_MAX_CMD_LENGTH];

	applog(LOG_DEBUG, "%s,%d: %s ", __FILE__, __LINE__, __FUNCTION__);

	memset(tx_buf, 0, sizeof(tx_buf));
	if (!opi_send_command(chain_id, CMD_BIST_FIX, chip_id, tx_buf, 2))
	{
		applog(LOG_WARNING, "%s,%d: %s send fail !", __FILE__, __LINE__, __FUNCTION__);
		return false;
	}

	memset(rx_buf, 0, sizeof(rx_buf));
	if (!opi_poll_result(chain_id, CMD_BIST_FIX, chip_id, rx_buf, 2))
	{
		applog(LOG_WARNING, "%s,%d: %s poll fail !", __FILE__, __LINE__, __FUNCTION__);
		return false;
	}

	return true;
}


bool opi_cmd_write_register(unsigned char chain_id, unsigned char chip_id, unsigned char *reg, int len)
{
	int i;
	unsigned short crc;
	unsigned char tx_buf[MCOMPAT_CONFIG_MAX_CMD_LENGTH];
	unsigned char rx_buf[MCOMPAT_CONFIG_MAX_CMD_LENGTH];
	unsigned char tmp_buf[MCOMPAT_CONFIG_MAX_CMD_LENGTH];

	applog(LOG_DEBUG, "%s,%d: %s ", __FILE__, __LINE__, __FUNCTION__);

	if (reg == NULL)
	{
		applog(LOG_ERR, "%s,%d: %s para error !", __FILE__, __LINE__, __FUNCTION__);
		return false;
	}

	memset(tx_buf, 0, sizeof(tx_buf));
	tx_buf[0] = OPI_HI_BYTE(AX_CMD_SYNC_HEAD);
	tx_buf[1] = OPI_LO_BYTE(AX_CMD_SYNC_HEAD);
	tx_buf[2] = CMD_WRITE_REG;
	tx_buf[3] = chip_id;
	memcpy(tx_buf + 4, reg, len);
	for(i = 0; i < len + 2; i = i + 2)
	{
		tmp_buf[i + 0] = tx_buf[i + 1 + 2];
		tmp_buf[i + 1] = tx_buf[i + 0 + 2];
	}
	crc = CRC16_2(tmp_buf, len + 2);
	tx_buf[4 + len + 0] = (unsigned char)((crc >> 8) & 0xff);
	tx_buf[4 + len + 1] = (unsigned char)((crc >> 0) & 0xff);

	opi_spi_read_write(chain_id, tx_buf, rx_buf, len + 6);

	memset(rx_buf, 0, sizeof(rx_buf));
	if (!opi_poll_result(chain_id, CMD_WRITE_REG, chip_id, rx_buf, len))
	{
		applog(LOG_WARNING, "%s,%d: %s poll fail !", __FILE__, __LINE__, __FUNCTION__);
		return false;
	}

	return true;
}

bool opi_cmd_read_register(unsigned char chain_id, unsigned char chip_id, unsigned char *reg, int len)
{
	int i;
	int max_len;
	unsigned short crc1, crc2;
	unsigned char tx_buf[MCOMPAT_CONFIG_MAX_CMD_LENGTH];
	unsigned char rx_buf[MCOMPAT_CONFIG_MAX_CMD_LENGTH];
	unsigned char tmp_buf[MCOMPAT_CONFIG_MAX_CMD_LENGTH];

	applog(LOG_DEBUG, "%s,%d: %s ", __FILE__, __LINE__, __FUNCTION__);

	if (reg == NULL)
	{
		applog(LOG_ERR, "%s,%d: %s para error !", __FILE__, __LINE__, __FUNCTION__);
		return false;
	}

	memset(tx_buf, 0, sizeof(tx_buf));
	if (!opi_send_command(chain_id, CMD_READ_REG, chip_id, tx_buf, 0))
	{
		applog(LOG_WARNING, "%s,%d: %s send fail !", __FILE__, __LINE__, __FUNCTION__);
		return false;
	}

	max_len = g_chip_num * 4;
	memset(rx_buf, 0, sizeof(rx_buf));

	for(i = 0; i < max_len; i = i + 2)
	{
		opi_spi_read_write(chain_id, NULL, rx_buf, 2);
		if ((rx_buf[0] & 0x0f) == CMD_READ_REG)
		{
			break;
		}
	}

	if (i >= max_len)
	{
		applog(LOG_WARNING, "%s,%d: %s poll fail !", __FILE__, __LINE__, __FUNCTION__);
		return false;
	}

	opi_spi_read_write(chain_id, NULL, rx_buf + 2, len + 2);

	for(i = 0; i < len + 2; i = i + 2)
	{
		tmp_buf[i + 0] = rx_buf[i + 1];
		tmp_buf[i + 1] = rx_buf[i + 0];
	}
	crc1 = CRC16_2(tmp_buf, len + 2);
	crc2 = (rx_buf[2 + len + 0] << 8) + (rx_buf[2 + len + 1] << 0);

	if (crc1 != crc2)
	{
		applog(LOG_WARNING, "%s,%d: %s crc fail !", __FILE__, __LINE__, __FUNCTION__);
		return false;
	}

	memcpy(reg, rx_buf + 2, len);

	return true;
}


bool opi_cmd_read_write_reg0d(unsigned char chain_id, unsigned char chip_id, unsigned char *in, int len, unsigned char *out)
{
	int i;
	int max_len;
	unsigned short crc;
	unsigned short crc1, crc2;
	unsigned char tx_buf[MCOMPAT_CONFIG_MAX_CMD_LENGTH];
	unsigned char rx_buf[MCOMPAT_CONFIG_MAX_CMD_LENGTH];
	unsigned char tmp_buf[MCOMPAT_CONFIG_MAX_CMD_LENGTH];

	applog(LOG_DEBUG, "%s,%d: %s ", __FILE__, __LINE__, __FUNCTION__);

	if ((in == NULL) || (out == NULL))
	{
		applog(LOG_ERR, "%s,%d: %s para error !", __FILE__, __LINE__, __FUNCTION__);
		return false;
	}

	memset(tx_buf, 0, sizeof(tx_buf));
	tx_buf[0] = OPI_HI_BYTE(AX_CMD_SYNC_HEAD);
	tx_buf[1] = OPI_LO_BYTE(AX_CMD_SYNC_HEAD);
	tx_buf[2] = CMD_WRITE_REG0d;
	tx_buf[3] = chip_id;
	memcpy(tx_buf + 4, in, len);
	for(i = 0; i < len + 2; i = i + 2)
	{
		tmp_buf[i + 0] = tx_buf[i + 1 + 2];
		tmp_buf[i + 1] = tx_buf[i + 0 + 2];
	}
	crc = CRC16_2(tmp_buf, len + 2);
	tx_buf[4 + len + 0] = (unsigned char)((crc >> 8) & 0xff);
	tx_buf[4 + len + 1] = (unsigned char)((crc >> 0) & 0xff);

	opi_spi_read_write(chain_id, tx_buf, rx_buf, len + 6);

	max_len = g_chip_num * 4;
	memset(rx_buf, 0, sizeof(rx_buf));

	for(i = 0; i < max_len; i = i + 2)
	{
		opi_spi_read_write(chain_id, NULL, rx_buf, 2);
		if ((rx_buf[0] & 0x0f) == CMD_WRITE_REG0d)
		{
			break;
		}
	}

	if (i >= max_len)
	{
		applog(LOG_WARNING, "%s,%d: %s poll fail !", __FILE__, __LINE__, __FUNCTION__);
		return false;
	}

	opi_spi_read_write(chain_id, NULL, rx_buf + 2, len + 2);

	for(i = 0; i < len + 2; i = i + 2)
	{
		tmp_buf[i + 0] = rx_buf[i + 1];
		tmp_buf[i + 1] = rx_buf[i + 0];
	}
	crc1 = CRC16_2(tmp_buf, len + 2);
	crc2 = (rx_buf[2 + len + 0] << 8) + (rx_buf[2 + len + 1] << 0);

	if (crc1 != crc2)
	{
		applog(LOG_WARNING, "%s,%d: %s crc fail !", __FILE__, __LINE__, __FUNCTION__);
		return false;
	}

	memcpy(out, rx_buf + 2, len);

	return true;
}


bool opi_cmd_read_result(unsigned char chain_id, unsigned char chip_id, unsigned char *res, int len)
{
	int i;
	int max_len;
	unsigned short crc1, crc2;
	unsigned char tx_buf[MCOMPAT_CONFIG_MAX_CMD_LENGTH];
	unsigned char rx_buf[MCOMPAT_CONFIG_MAX_CMD_LENGTH];
	unsigned char tmp_buf[MCOMPAT_CONFIG_MAX_CMD_LENGTH];

	applog(LOG_DEBUG, "%s,%d: %s ", __FILE__, __LINE__, __FUNCTION__);

	if (res == NULL)
	{
		applog(LOG_ERR, "%s,%d: %s para error !", __FILE__, __LINE__, __FUNCTION__);
		return false;
	}

	memset(tx_buf, 0, sizeof(tx_buf));
	if (!opi_send_command(chain_id, CMD_READ_RESULT, chip_id, tx_buf, 2))
	{
		applog(LOG_WARNING, "%s,%d: %s send fail !", __FILE__, __LINE__, __FUNCTION__);
		return false;
	}

	max_len = g_chip_num * 4;
	memset(rx_buf, 0, sizeof(rx_buf));

	for(i = 0; i < max_len; i = i + 2)
	{
		opi_spi_read_write(chain_id, NULL, rx_buf, 2);
		if (((rx_buf[0] & 0x0f) == CMD_READ_RESULT) && (rx_buf[1] != 0))
		{
			break;
		}
	}

	if (i >= max_len)
	{
		applog(LOG_WARNING, "%s,%d: %s poll fail !", __FILE__, __LINE__, __FUNCTION__);
		return false;
	}

	opi_spi_read_write(chain_id, NULL, rx_buf + 2, len + 2);

	for(i = 0; i < len + 2; i = i + 2)
	{
		tmp_buf[i + 0] = rx_buf[i + 1];
		tmp_buf[i + 1] = rx_buf[i + 0];
	}
	crc1 = CRC16_2(tmp_buf, len + 2);
	crc2 = (rx_buf[2 + len + 0] << 8) + (rx_buf[2 + len + 1] << 0);

	if (crc1 != crc2)
	{
		applog(LOG_WARNING, "%s,%d: %s crc fail !", __FILE__, __LINE__, __FUNCTION__);
		return false;
	}

	memcpy(res, rx_buf, len + 2);

	return true;
}


bool opi_cmd_write_job(unsigned char chain_id, unsigned char chip_id, unsigned char *job, int len)
{
	applog(LOG_DEBUG, "%s,%d: %s(%d, %d, %p, %d)", __FILE__, __LINE__, __FUNCTION__, chain_id, chip_id, job, len);

	if (job == NULL)
	{
		applog(LOG_ERR, "%s,%d: %s para error !", __FILE__, __LINE__, __FUNCTION__);
		return false;
	}

	return opi_spi_read_write(chain_id, NULL, job, len);
}


ZYNQ_SPI_T s_spi[MCOMPAT_CONFIG_MAX_CHAIN_NUM];

bool init_spi_cmd(int chain_num)
{
	int i;

	for(i = 0; i < chain_num; i++)
	{
		memset((void*)&s_spi[i], 0, sizeof(ZYNQ_SPI_T));
		zynq_spi_init(&s_spi[i], i);
	}

	return true;
}

bool exit_spi_cmd(int chain_num)
{
	int i;

	for(i = 0; i < chain_num; i++)
	{
		zynq_spi_exit(&s_spi[i]);
	}

	return true;
}

bool spi_cmd_reset(unsigned char chain_id, unsigned char chip_id, unsigned char *in, unsigned char *out)
{
	ZYNQ_SPI_T *spi = &s_spi[chain_id];
	unsigned char rx_buf[MCOMPAT_CONFIG_MAX_CMD_LENGTH];

	applog(LOG_DEBUG, "%s,%d: %s ", __FILE__, __LINE__, __FUNCTION__);

	if (!spi_send_command(spi, CMD_RESET, chip_id, in, 2))
	{
		applog(LOG_WARNING, "%s send fail !", __FUNCTION__);
		return false;
	}

	memset(rx_buf, 0, sizeof(rx_buf));
	if (!spi_poll_result(spi, CMD_RESET, chip_id, rx_buf, 2))
	{
		applog(LOG_WARNING, "%s poll fail !", __FUNCTION__);
		return false;
	}

	memcpy(out, rx_buf, 4);

	return true;
}


int spi_cmd_bist_start(unsigned char chain_id, unsigned char chip_id)
{
	ZYNQ_SPI_T *spi = &s_spi[chain_id];
	unsigned char tx_buf[MCOMPAT_CONFIG_MAX_CMD_LENGTH];
	unsigned char rx_buf[MCOMPAT_CONFIG_MAX_CMD_LENGTH];

	applog(LOG_DEBUG, "%s,%d: %s ", __FILE__, __LINE__, __FUNCTION__);

	memset(tx_buf, 0, sizeof(tx_buf));
	if (!spi_send_command(spi, CMD_BIST_START, chip_id, tx_buf, 2))
	{
		applog(LOG_WARNING, "%s send fail !", __FUNCTION__);
		return -1;
	}

	memset(rx_buf, 0, sizeof(rx_buf));
	if (!spi_poll_result(spi, CMD_BIST_START, chip_id, rx_buf, 2))
	{
		applog(LOG_WARNING, "%s poll fail !", __FUNCTION__);
		return -1;
	}

	return rx_buf[3];
}

bool spi_cmd_bist_collect(unsigned char chain_id, unsigned char chip_id)
{
	ZYNQ_SPI_T *spi = &s_spi[chain_id];
	unsigned char tx_buf[MCOMPAT_CONFIG_MAX_CMD_LENGTH];
	unsigned char rx_buf[MCOMPAT_CONFIG_MAX_CMD_LENGTH];

	applog(LOG_DEBUG, "%s,%d: %s ", __FILE__, __LINE__, __FUNCTION__);

	memset(tx_buf, 0, sizeof(tx_buf));
	if (!spi_send_command(spi, CMD_BIST_COLLECT, chip_id, tx_buf, 2))
	{
		applog(LOG_WARNING, "%s send fail !", __FUNCTION__);
		return false;
	}

	memset(rx_buf, 0, sizeof(rx_buf));
	if (!spi_poll_result(spi, CMD_BIST_COLLECT, chip_id, rx_buf, 2))
	{
		applog(LOG_WARNING, "%s poll fail !", __FUNCTION__);
		return false;
	}

	return true;
}


bool spi_cmd_bist_fix(unsigned char chain_id, unsigned char chip_id)
{
	ZYNQ_SPI_T *spi = &s_spi[chain_id];
	unsigned char tx_buf[MCOMPAT_CONFIG_MAX_CMD_LENGTH];
	unsigned char rx_buf[MCOMPAT_CONFIG_MAX_CMD_LENGTH];

	applog(LOG_DEBUG, "%s,%d: %s ", __FILE__, __LINE__, __FUNCTION__);

	memset(tx_buf, 0, sizeof(tx_buf));
	if (!spi_send_command(spi, CMD_BIST_FIX, chip_id, tx_buf, 2))
	{
		applog(LOG_WARNING, "%s send fail !", __FUNCTION__);
		return false;
	}

	memset(rx_buf, 0, sizeof(rx_buf));
	if (!spi_poll_result(spi, CMD_BIST_FIX, chip_id, rx_buf, 2))
	{
		applog(LOG_WARNING, "%s poll fail !", __FUNCTION__);
		return false;
	}

	return true;
}


bool spi_cmd_write_register(unsigned char chain_id, unsigned char chip_id, unsigned char *reg, int len)
{
	int i;
	unsigned short crc;
	ZYNQ_SPI_T *spi = &s_spi[chain_id];
	unsigned char tx_buf[MCOMPAT_CONFIG_MAX_CMD_LENGTH];
	unsigned char rx_buf[MCOMPAT_CONFIG_MAX_CMD_LENGTH];
	unsigned char tmp_buf[MCOMPAT_CONFIG_MAX_CMD_LENGTH];

	applog(LOG_DEBUG, "%s,%d: %s ", __FILE__, __LINE__, __FUNCTION__);

	if (reg == NULL)
	{
		applog(LOG_ERR, "%s para error !", __FUNCTION__);
		return false;
	}

	memset(tx_buf, 0, sizeof(tx_buf));
	tx_buf[0] = CMD_WRITE_REG;
	tx_buf[1] = chip_id;
	memcpy(tx_buf + 2, reg, len);
	for(i = 0; i < len + 2; i = i + 2)
	{
		tmp_buf[i + 0] = tx_buf[i + 1];
		tmp_buf[i + 1] = tx_buf[i + 0];
	}
	crc = CRC16_2(tmp_buf, len + 2);
	tx_buf[2 + len + 0] = (unsigned char)((crc >> 8) & 0xff);
	tx_buf[2 + len + 1] = (unsigned char)((crc >> 0) & 0xff);

	spi_send_data(spi, tx_buf, len + 4);

	memset(rx_buf, 0, sizeof(rx_buf));
	if (!spi_poll_result(spi, CMD_WRITE_REG, chip_id, rx_buf, len))
	{
		applog(LOG_WARNING, "%s poll fail !", __FUNCTION__);
		return false;
	}

	return true;
}


bool spi_cmd_read_register(unsigned char chain_id, unsigned char chip_id, unsigned char *reg, int len)
{
	int i;
	int max_len;
	unsigned short crc1, crc2;
	ZYNQ_SPI_T *spi = &s_spi[chain_id];
	unsigned char tx_buf[MCOMPAT_CONFIG_MAX_CMD_LENGTH];
	unsigned char rx_buf[MCOMPAT_CONFIG_MAX_CMD_LENGTH];
	unsigned char tmp_buf[MCOMPAT_CONFIG_MAX_CMD_LENGTH];

	applog(LOG_DEBUG, "%s,%d: %s ", __FILE__, __LINE__, __FUNCTION__);

	if (reg == NULL)
	{
		applog(LOG_ERR, "%s para error !", __FUNCTION__);
		return false;
	}

	memset(tx_buf, 0, sizeof(tx_buf));
	if (!spi_send_command(spi, CMD_READ_REG, chip_id, tx_buf, 0))
	{
		applog(LOG_WARNING, "%s send fail !", __FUNCTION__);
		return false;
	}

	max_len = g_chip_num * 4;
	memset(rx_buf, 0, sizeof(rx_buf));

	for(i = 0; i < max_len; i = i + 2)
	{
		spi_recv_data(spi, rx_buf, 2);
		if (rx_buf[0] == RESP_READ_REG)
		{
			break;
		}
	}

	if (i >= max_len)
	{
		applog(LOG_WARNING, "%s poll fail !", __FUNCTION__);
		return false;
	}

	spi_recv_data_in_word(spi, rx_buf + 2, len + 2);

	for(i = 0; i < len + 2; i = i + 2)
	{
		tmp_buf[i + 0] = rx_buf[i + 1];
		tmp_buf[i + 1] = rx_buf[i + 0];
	}
	crc1 = CRC16_2(tmp_buf, len + 2);
	crc2 = (rx_buf[2 + len + 0] << 8) + (rx_buf[2 + len + 1] << 0);

	if (crc1 != crc2) {
		applog(LOG_WARNING, "%s crc error !", __FUNCTION__);
		return false;
	}

	memcpy(reg, rx_buf + 2, len);

	return true;
}


bool spi_cmd_read_write_reg0d(unsigned char chain_id, unsigned char chip_id, unsigned char *in, int len, unsigned char *out)
{
	int i;
	int max_len;
	unsigned short crc;
	unsigned short crc1, crc2;
	ZYNQ_SPI_T *spi = &s_spi[chain_id];
	unsigned char tx_buf[MCOMPAT_CONFIG_MAX_CMD_LENGTH];
	unsigned char rx_buf[MCOMPAT_CONFIG_MAX_CMD_LENGTH];
	unsigned char tmp_buf[MCOMPAT_CONFIG_MAX_CMD_LENGTH];

	applog(LOG_DEBUG, "%s,%d: %s ", __FILE__, __LINE__, __FUNCTION__);

	if ((in == NULL) || (out == NULL))
	{
		applog(LOG_ERR, "%s para error !", __FUNCTION__);
		return false;
	}

	memset(tx_buf, 0, sizeof(tx_buf));
	tx_buf[0] = CMD_WRITE_REG0d;
	tx_buf[1] = chip_id;
	memcpy(tx_buf + 2, in, len);
	for(i = 0; i < len + 2; i = i + 2)
	{
		tmp_buf[i + 0] = tx_buf[i + 1];
		tmp_buf[i + 1] = tx_buf[i + 0];
	}
	crc = CRC16_2(tmp_buf, len + 2);
	tx_buf[2 + len + 0] = (unsigned char)((crc >> 8) & 0xff);
	tx_buf[2 + len + 1] = (unsigned char)((crc >> 0) & 0xff);

	spi_send_data(spi, tx_buf, len + 4);

	max_len = g_chip_num * 4;
	memset(rx_buf, 0, sizeof(rx_buf));

	for(i = 0; i < max_len; i = i + 2)
	{
		spi_recv_data(spi, rx_buf, 2);
		if ((rx_buf[0] & 0x0f) == CMD_WRITE_REG0d)
		{
			break;
		}
	}

	if (i >= max_len)
	{
		applog(LOG_WARNING, "%s poll fail !", __FUNCTION__);
		return false;
	}

	spi_recv_data_in_word(spi, rx_buf + 2, len + 2);

	for(i = 0; i < len + 2; i = i + 2)
	{
		tmp_buf[i + 0] = rx_buf[i + 1];
		tmp_buf[i + 1] = rx_buf[i + 0];
	}
	crc1 = CRC16_2(tmp_buf, len + 2);
	crc2 = (rx_buf[2 + len + 0] << 8) + (rx_buf[2 + len + 1] << 0);

	if (crc1 != crc2) {
		applog(LOG_WARNING, "%s crc error !", __FUNCTION__);
		return false;
	}

	memcpy(out, rx_buf + 2, len);

	return true;
}


bool spi_cmd_read_result(unsigned char chain_id, unsigned char chip_id, unsigned char *res, int len)
{
	int i;
	int max_len;
	unsigned short crc1, crc2;
	ZYNQ_SPI_T *spi = &s_spi[chain_id];
	unsigned char tx_buf[MCOMPAT_CONFIG_MAX_CMD_LENGTH];
	unsigned char rx_buf[MCOMPAT_CONFIG_MAX_CMD_LENGTH];
	unsigned char tmp_buf[MCOMPAT_CONFIG_MAX_CMD_LENGTH];

	applog(LOG_DEBUG, "%s,%d: %s ", __FILE__, __LINE__, __FUNCTION__);

	if (res == NULL)
	{
		applog(LOG_ERR, "%s para error !", __FUNCTION__);
		return false;
	}

	memset(tx_buf, 0, sizeof(tx_buf));
	if (!spi_send_command(spi, CMD_READ_RESULT, chip_id, tx_buf, 2))
	{
		applog(LOG_WARNING, "%s send fail !", __FUNCTION__);
		return false;
	}

	max_len = g_chip_num * 4;
	memset(rx_buf, 0, sizeof(rx_buf));

	for(i = 0; i < max_len; i = i + 2)
	{
		spi_recv_data(spi, rx_buf, 2);
		if (((rx_buf[0] & 0x0f) == CMD_READ_RESULT) && (rx_buf[1] != 0))
		{
			break;
		}
	}

	if (i >= max_len)
	{
		return false;
	}

	spi_recv_data_in_word(spi, rx_buf + 2, len + 2);

	for(i = 0; i < len + 2; i = i + 2)
	{
		tmp_buf[i + 0] = rx_buf[i + 1];
		tmp_buf[i + 1] = rx_buf[i + 0];
	}
	crc1 = CRC16_2(tmp_buf, len + 2);
	crc2 = (rx_buf[2 + len + 0] << 8) + (rx_buf[2 + len + 1] << 0);

	if (crc1 != crc2) {
		applog(LOG_WARNING, "%s crc error !", __FUNCTION__);
		return false;
	}

	memcpy(res, rx_buf, len + 2);

	return true;
}


bool spi_cmd_write_job(unsigned char chain_id, unsigned char chip_id, unsigned char *job, int len)
{
	ZYNQ_SPI_T *spi = &s_spi[chain_id];

	applog(LOG_DEBUG, "%s,%d: %s(%d, %d, %p, %d)", __FILE__, __LINE__, __FUNCTION__, chain_id, chip_id, job, len);

	if (job == NULL)
	{
		applog(LOG_ERR, "%s para error !", __FUNCTION__);
		return false;
	}

	spi_send_data(spi, job, len);

	return true;
}

const unsigned short wCRCTalbeAbs[] =
{
	0x0000, 0xCC01, 0xD801, 0x1400,
	0xF001, 0x3C00, 0x2800, 0xE401,
	0xA001, 0x6C00, 0x7800, 0xB401,
	0x5000, 0x9C01, 0x8801, 0x4400,
};

unsigned short CRC16_2(unsigned char* pchMsg, unsigned short wDataLen)
{
	volatile unsigned short wCRC = 0xFFFF;
	unsigned short i;
	unsigned char chChar;

	for (i = 0; i < wDataLen; i++)
	{
		chChar = *pchMsg++;
		wCRC = wCRCTalbeAbs[(chChar ^ wCRC) & 15] ^ (wCRC >> 4);
		wCRC = wCRCTalbeAbs[((chChar >> 4) ^ wCRC) & 15] ^ (wCRC >> 4);
	}

	return wCRC;
}


void print_data_hex(char *arg, unsigned char *buff, int len)
{
	int i = 0;

	printf("%s ", arg);
	for(i = 0; i < len; i++)
	{
		printf("0x%02x ", buff[i]);
		if ((i % 16) == 15)
		{
			//printf("");
		}
	}

	if ((len % 16) != 0)
	{
		//printf("");
	}
}

unsigned int SUNXI_IO_BASE = 0;

/*******************************************************************************************
 * GPIO
 *******************************************************************************************/
int gpio_init(void)
{
	int fd;
	unsigned int addr_start, addr_offset, PageSize, PageMask;
	void *pc;

	fd = open("/dev/mem", O_RDWR);
	if (fd < 0)
		return -1;

	PageSize = sysconf(_SC_PAGESIZE);
	PageMask = (~(PageSize - 1));

	addr_start = SW_PORTC_IO_BASE & PageMask;
	addr_offset = SW_PORTC_IO_BASE & ~PageMask;

	pc = (void *)mmap(0, PageSize * 2, PROT_READ | PROT_WRITE, MAP_SHARED, fd, addr_start);

	if (pc == MAP_FAILED)
		return -1;

	SUNXI_IO_BASE = (unsigned int) pc;
	SUNXI_IO_BASE += addr_offset;

	close(fd);
	return 0;
}

int gpio_setcfg(unsigned int pin, unsigned int p1)
{
	unsigned int cfg;
	unsigned int bank = GPIO_BANK(pin);
	unsigned int index = GPIO_CFG_INDEX(pin);
	unsigned int offset = GPIO_CFG_OFFSET(pin);

	if (SUNXI_IO_BASE == 0)
		return -1;

	struct sunxi_gpio *pio = &((struct sunxi_gpio_reg *) SUNXI_IO_BASE)->gpio_bank[bank];

	cfg = *(&pio->cfg[0] + index);
	cfg &= ~(0xf << offset);
	cfg |= p1 << offset;

	*(&pio->cfg[0] + index) = cfg;

	return 0;
}

int gpio_getcfg(unsigned int pin)
{
	unsigned int cfg;
	unsigned int bank = GPIO_BANK(pin);
	unsigned int index = GPIO_CFG_INDEX(pin);
	unsigned int offset = GPIO_CFG_OFFSET(pin);

	if (SUNXI_IO_BASE == 0)
		return -0;

	struct sunxi_gpio *pio = &((struct sunxi_gpio_reg *) SUNXI_IO_BASE)->gpio_bank[bank];
	cfg = *(&pio->cfg[0] + index);
	cfg >>= offset;

	return (cfg & 0xf);
}

int gpio_output(unsigned int pin, unsigned int p1)
{
	unsigned int bank = GPIO_BANK(pin);
	unsigned int num = GPIO_NUM(pin);

	if (SUNXI_IO_BASE == 0)
		return -1;

	struct sunxi_gpio *pio = &((struct sunxi_gpio_reg *) SUNXI_IO_BASE)->gpio_bank[bank];

	if (p1)
		*(&pio->dat) |= 1 << num;
	else
		*(&pio->dat) &= ~(1 << num);

	return 0;
}

int gpio_pullup(unsigned int pin, unsigned int p1)
{
	unsigned int cfg;
	unsigned int bank = GPIO_BANK(pin);
	unsigned int index = GPIO_PUL_INDEX(pin);
	unsigned int offset = GPIO_PUL_OFFSET(pin);

	if (SUNXI_IO_BASE == 0)
		return -1;

	struct sunxi_gpio *pio = &((struct sunxi_gpio_reg *) SUNXI_IO_BASE)->gpio_bank[bank];

	cfg = *(&pio->pull[0] + index);
	cfg &= ~(0x3 << offset);
	cfg |= p1 << offset;

	*(&pio->pull[0] + index) = cfg;

	return 0;
}

int gpio_input(unsigned int pin)
{
	unsigned int dat;
	unsigned int bank = GPIO_BANK(pin);
	unsigned int num = GPIO_NUM(pin);

	if (SUNXI_IO_BASE == 0)
		return -1;

	struct sunxi_gpio *pio = &((struct sunxi_gpio_reg *) SUNXI_IO_BASE)->gpio_bank[bank];

	dat = *(&pio->dat);
	dat >>= num;

	return (dat & 0x1);
}

/*******************************************************************************************
 * I2C
 *******************************************************************************************/
int i2c_open(char *dev, uint8_t address)
{
	int fd;
	int ret;

	fd = open(dev, O_RDWR);
	if (fd < 0)
		return fd;

	ret = ioctl(fd, I2C_SLAVE_FORCE, address);
	if (ret < 0)
		return ret;

	return fd;
}

int i2c_close(int fd)
{
	return (close(fd));
}

int i2c_send(int fd, uint8_t *buf, uint8_t num_bytes)
{
	return (write(fd, buf, num_bytes));
}

int i2c_read(int fd, uint8_t *buf, uint8_t num_bytes)
{
	return (write(fd, buf, num_bytes));
}

/*******************************************************************************************
 * spi
 *******************************************************************************************/
int spi_open(char *dev, spi_config_t config)
{
	int fd;

	fd = open(dev, O_RDWR);
	if (fd < 0)
		return fd;

	/* Set SPI_POL and SPI_PHA */
	if (ioctl(fd, SPI_IOC_WR_MODE, &config.mode) < 0)
		return -1;

	if (ioctl(fd, SPI_IOC_RD_MODE, &config.mode) < 0)
		return -1;

	/* Set bits per word*/
	if (ioctl(fd, SPI_IOC_WR_BITS_PER_WORD, &config.bits) < 0)
		return -1;

	if (ioctl(fd, SPI_IOC_RD_BITS_PER_WORD, &config.bits) < 0)
		return -1;

	/* Set SPI speed*/
	if (ioctl(fd, SPI_IOC_WR_MAX_SPEED_HZ, &config.speed) < 0)
		return -1;

	if (ioctl(fd, SPI_IOC_RD_MAX_SPEED_HZ, &config.speed) < 0)
		return -1;

	return fd;
}

int spi_close(int fd)
{
	return (close(fd));
}

int spi_xfer(int fd, uint8_t *tx_buf, uint8_t tx_len, uint8_t *rx_buf, uint8_t rx_len)
{
	struct spi_ioc_transfer spi_message[2];

	memset(spi_message, 0, sizeof(spi_message));

	spi_message[0].rx_buf = (unsigned long)tx_buf;
	spi_message[0].len = tx_len;

	spi_message[1].tx_buf = (unsigned long)rx_buf;
	spi_message[1].len = rx_len;

	return ioctl(fd, SPI_IOC_MESSAGE(2), spi_message);
}

int spi_read(int fd, uint8_t *rx_buf, uint8_t rx_len)
{
	struct spi_ioc_transfer spi_message[1];
	memset(spi_message, 0, sizeof(spi_message));

	spi_message[0].rx_buf = (unsigned long)rx_buf;
	spi_message[0].len = rx_len;

	return ioctl(fd, SPI_IOC_MESSAGE(1), spi_message);
}

int spi_write(int fd, uint8_t *tx_buffer, uint8_t tx_len)
{
	struct spi_ioc_transfer spi_message[1];
	memset(spi_message, 0, sizeof(spi_message));

	spi_message[0].tx_buf = (unsigned long)tx_buffer;
	spi_message[0].len = tx_len;

	return ioctl(fd, SPI_IOC_MESSAGE(1), spi_message);
}

/*******************************************************************************************
 * time
 *******************************************************************************************/
void delay(unsigned int howLong)
{
	struct timespec sleeper, dummy ;

	sleeper.tv_sec  = (time_t)(howLong / 1000) ;
	sleeper.tv_nsec = (long)(howLong % 1000) * 1000000 ;

	nanosleep (&sleeper, &dummy) ;
}

static int opi_spi_fd = 0;
static pthread_mutex_t opi_spi_lock;
static struct spi_config opi_spi_config = {
	.bus		= DEFAULT_SPI_BUS,
	.cs_line	= DEFAULT_SPI_CS_LINE,
	.mode		= DEFAULT_SPI_MODE,
	.speed		= DEFAULT_SPI_SPEED,
	.bits		= DEFAULT_SPI_BITS_PER_WORD,
	.delay		= DEFAULT_SPI_DELAY_USECS,
};


void opi_spi_gpio_init(void)
{
	if (gpio_init() == -1) {
		printf("gpio initial fail");
	}

	gpio_setcfg(PIN_SPI_E1, OUTPUT);
	gpio_output(PIN_SPI_E1, HIGH);

	gpio_setcfg(PIN_SPI_A0, OUTPUT);
	gpio_output(PIN_SPI_A0, HIGH);
	gpio_setcfg(PIN_SPI_A1, OUTPUT);
	gpio_output(PIN_SPI_A1, HIGH);
	gpio_setcfg(PIN_SPI_A2, OUTPUT);
	gpio_output(PIN_SPI_A2, HIGH);
}

void opi_spi_cs_enable(int id)
{
	if (id & 01)
	{
		gpio_output(PIN_SPI_A0, HIGH);
	}
	else
	{
		gpio_output(PIN_SPI_A0, LOW);
	}

	if (id & 02)
	{
		gpio_output(PIN_SPI_A1, HIGH);
	}
	else
	{
		gpio_output(PIN_SPI_A1, LOW);
	}

	if (id & 04)
	{
		gpio_output(PIN_SPI_A2, HIGH);
	}
	else
	{
		gpio_output(PIN_SPI_A2, LOW);
	}

	gpio_output(PIN_SPI_E1, LOW);
}

void opi_spi_cs_disable(void)
{
	gpio_output(PIN_SPI_E1, HIGH);
}

void opi_spi_init(void)
{
	char dev_fname[PATH_MAX];
	struct spi_config *config = &opi_spi_config;

	pthread_mutex_init(&opi_spi_lock, NULL);
	opi_spi_gpio_init();

	sprintf(dev_fname, SPI_DEVICE_TEMPLATE, config->bus, config->cs_line);

	int fd = open(dev_fname, O_RDWR);
	if (fd < 0) {
		applog(LOG_ERR, "SPI: Can not open SPI device %s ", dev_fname);
	}

	if ((ioctl(fd, SPI_IOC_WR_MODE, &config->mode) < 0) ||
		(ioctl(fd, SPI_IOC_RD_MODE, &config->mode) < 0) ||
		(ioctl(fd, SPI_IOC_WR_BITS_PER_WORD, &config->bits) < 0) ||
		(ioctl(fd, SPI_IOC_RD_BITS_PER_WORD, &config->bits) < 0) ||
		(ioctl(fd, SPI_IOC_WR_MAX_SPEED_HZ, &config->speed) < 0) ||
		(ioctl(fd, SPI_IOC_RD_MAX_SPEED_HZ, &config->speed) < 0)) {
		close(fd);
	applog(LOG_ERR, "SPI: ioctl error on SPI device %s", dev_fname);
		}

		opi_spi_fd = fd;

		applog(LOG_INFO, "SPI '%s': mode=%hhu, bits=%hhu, speed=%u ", dev_fname, config->mode, config->bits, config->speed);
}

void opi_spi_exit(void)
{	
	close(opi_spi_fd);
}

/*
 * void opi_set_spi_speed(uint32_t speed)
 * {
 * pthread_mutex_lock(&opi_spi_lock);
 *
 * if ((ioctl(opi_spi_fd, SPI_IOC_WR_MAX_SPEED_HZ, speed) < 0) ||
 * (ioctl(opi_spi_fd, SPI_IOC_RD_MAX_SPEED_HZ, speed) < 0)) {
 * applog(LOG_ERR, "SPI: ioctl error on SPI device");
 * }
 *
 * opi_spi_config.speed = speed;
 *
 * pthread_mutex_unlock(&opi_spi_lock);
 * }
 */
bool opi_spi_transfer(uint8_t id, uint16_t *txbuf, uint16_t *rxbuf, int len)
{
	struct spi_ioc_transfer xfr;
	int ret;

	if (rxbuf != NULL) {
		memset(rxbuf, 0xff, len);
	}

	pthread_mutex_lock(&opi_spi_lock);

	opi_spi_cs_enable(id);
	cgsleep_ms(1); // usleep(1);

	ret = len;

	xfr.tx_buf = (unsigned long)txbuf;
	xfr.rx_buf = (unsigned long)rxbuf;
	xfr.len = len;
	xfr.speed_hz = opi_spi_config.speed;
	xfr.delay_usecs = opi_spi_config.delay;
	xfr.bits_per_word = opi_spi_config.bits;
	xfr.cs_change = 0;
	xfr.pad = 0;

	ret = ioctl(opi_spi_fd, SPI_IOC_MESSAGE(1), &xfr);
	if (ret < 1) {
		applog(LOG_ERR, "SPI: ioctl error on SPI device: %d", ret);
	}

	cgsleep_ms(1); // usleep(1);
	opi_spi_cs_disable();

	pthread_mutex_unlock(&opi_spi_lock);

	return ret > 0;
}

int g_platform;
int g_miner_type;
int g_chain_num;
int g_chip_num;


bool sys_platform_init(int platform, int miner_type, int chain_num, int chip_num)
{
	applog(LOG_NOTICE, "sys : platform[%d] miner_type[%d] chain_num[%d] chip_num[%d] ", platform, miner_type, chain_num, chip_num);

	switch(platform)
	{
		case PLATFORM_ZYNQ_SPI_G9:
		case PLATFORM_ZYNQ_SPI_G19:
		case PLATFORM_ZYNQ_HUB_G9:
		case PLATFORM_ZYNQ_HUB_G19:
		case PLATFORM_SOC:
			break;
		default:
			applog(LOG_ERR, "the platform is undefined !!! ");
			break;
	}

	if (chain_num > MCOMPAT_CONFIG_MAX_CHAIN_NUM)
	{
		applog(LOG_ERR, "the chain_num is error !!! ");
		return false;
	}

	if (chip_num > MCOMPAT_CONFIG_MAX_CHIP_NUM)
	{
		applog(LOG_ERR, "the chip_num is error !!! ");
		return false;
	}

	g_platform = platform;
	g_miner_type = miner_type;
	g_chain_num = chain_num;
	g_chip_num = chip_num;

	init_mcompat_cmd();

	init_mcompat_gpio();

	init_mcompat_pwm();

	init_mcompat_chain();

	return true;
}


bool sys_platform_exit(void)
{
	exit_mcompat_cmd();

	exit_mcompat_gpio();

	exit_mcompat_pwm();

	exit_mcompat_chain();

	return true;
}

