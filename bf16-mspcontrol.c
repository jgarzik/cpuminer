#include "bf16-mspcontrol.h"

#include "bf16-communication.h"
#include "bf16-ctrldevice.h"

static bool check_crc(char* data)
{
	uint8_t crc = 0;
	char crcdata[8];

	char* mid = strchr(data, '#');
	char* end = strchr(data, '\n');

	if (mid == NULL)
		return false;

	if (end == NULL)
		return false;

	while (data != mid) {
		crc += *data;
		data++;
	}

	uint8_t value_len = end - mid;
	memset(crcdata, 0, sizeof(crcdata));
	cg_memcpy(crcdata, mid + 1, value_len - 1);

	int expected_crc = atoi(crcdata);

	if (expected_crc != crc)
		return false;

	return true;
}

static char* get_int_val(char* data, char* name, char delim, int* value)
{
	char rname[16];
	char rvalue[32];

	char* end = strchr(data, delim);
	if (end == NULL)
		return NULL;

	memset(rname, 0, sizeof(rname));
	memset(rvalue, 0, sizeof(rvalue));

	char* mid = strchr(data, ':');
	if (mid == NULL)
		return NULL;

	uint8_t name_len  = mid - data;
	uint8_t value_len = end - mid;

	cg_memcpy(rname, data, name_len);
	cg_memcpy(rvalue, mid + 1, value_len - 1);

	if (strcmp(rname, name) == 0)
		*value = atoi(rvalue);
	else {
		*value = 0;
		return data;
	}

	return end + 1;
}

static char* get_str_val(char* data, char* name, char delim, char* value)
{
	char rname[16];
	char rvalue[32];

	char* end = strchr(data, delim);
	if (end == NULL)
		return NULL;

	memset(rname, 0, sizeof(rname));
	memset(rvalue, 0, sizeof(rvalue));

	char* mid = strchr(data, ':');
	if (mid == NULL)
		return NULL;

	uint8_t name_len  = mid - data;
	uint8_t value_len = end - mid;

	cg_memcpy(rname, data, name_len);
	cg_memcpy(rvalue, mid + 1, value_len - 1);

	if (strcmp(rname, name) == 0)
		cg_memcpy(value, rvalue, strlen(rvalue));
	else {
		memset(value, '0', 16);
		return data;
	}

	return end + 1;
}

void parse_board_detect(struct cgpu_info *bitfury, uint8_t board_id, char* data)
{
	struct bitfury16_info *info = (struct bitfury16_info *)(bitfury->device_data);
	char* start = data;

	char val[4];
	memset(val, 0, sizeof(val));

	/* BRD_DET*/
	if (board_id == 0) {
		if (get_str_val(start, "CH1_DET", '\n', val) == NULL)
			quit(1, "%s: %s() failed to parse BOARD%d detect",
				bitfury->drv->name, __func__, board_id + 1);

		if (strcmp(val, "ON") == 0)
			info->chipboard[board_id].detected = true;
		else
			info->chipboard[board_id].detected = false;
	} else {
		if (get_str_val(start, "CH2_DET", '\n', val) == NULL)
			quit(1, "%s: %s() failed to parse BOARD%d detect",
				bitfury->drv->name, __func__, board_id + 1);

		if (strcmp(val, "ON") == 0)
			info->chipboard[board_id].detected = true;
		else
			info->chipboard[board_id].detected = false;
	}
}

static void parse_board_version(struct cgpu_info *bitfury, uint8_t board_id, char* data)
{
	struct bitfury16_info *info = (struct bitfury16_info *)(bitfury->device_data);

	if (check_crc(data) == false)
		quit(1, "%s: %s() invalid BOARD%d version data recieved: [%s]",
				bitfury->drv->name, __func__, board_id + 1, data);

	/* BRD */
	char* start = data;
	start = get_int_val(start, "BRD", ';', (int*)&info->chipboard[board_id].board_ver);
	if (start == NULL)
		quit(1, "%s: %s() failed to parse BOARD%d BRD",
			bitfury->drv->name, __func__, board_id + 1);

	/* FW */
	start = get_int_val(start, "FW", ';', (int*)&info->chipboard[board_id].board_fwver);
	if (start == NULL)
		quit(1, "%s: %s() failed to parse BOARD%d FW",
			bitfury->drv->name, __func__, board_id + 1);

	/* ID */
	if (get_str_val(start, "ID", '#', info->chipboard[board_id].board_hwid) == NULL)
		quit(1, "%s: %s() failed to parse BOARD%d ID",
			bitfury->drv->name, __func__, board_id + 1);
}

void get_board_info(struct cgpu_info *bitfury, uint8_t board_id)
{
	struct bitfury16_info *info = (struct bitfury16_info *)(bitfury->device_data);
	char buff[256];

	if (info->chipboard[board_id].detected == true) {
		memset(buff, 0, sizeof(buff));
		if (device_uart_txrx(board_id + 1, "V", buff) < 0)
			quit(1, "%s: %s() failed to get BOARD%d version",
				bitfury->drv->name, __func__, board_id + 1);

		parse_board_version(bitfury, board_id, buff);

		applog(LOG_INFO, "%s: BOARD%d version: [%d]", bitfury->drv->name,
				board_id + 1, info->chipboard[board_id].board_ver);
		applog(LOG_INFO, "%s: BOARD%d firmware version: [%d]", bitfury->drv->name,
				board_id + 1, info->chipboard[board_id].board_fwver);
		applog(LOG_INFO, "%s: BOARD%d hardware id: [%s]", bitfury->drv->name,
				board_id + 1, info->chipboard[board_id].board_hwid);
	}
}

int8_t parse_hwstats(struct bitfury16_info *info, uint8_t board_id, char* data)
{
	uint8_t i;
	int32_t value;
	char    strvalue[17];

	if (check_crc(data) == false) {
		applog(LOG_ERR, "BF16: invalid BOARD%d hwstats recieved: [%s]",
				board_id + 1, data);

		return -1;
	}

	/* T */
	char* start = data;
	start = get_int_val(start, "T", ';', (int *)&value);
	if (start != NULL)
		info->chipboard[board_id].temp = value / 10.0;
	else {
		info->chipboard[board_id].temp = 0.0;
		return -1;
	}

	/* UB */
	start = get_int_val(start, "UB", ';', (int *)&value);
	if (start != NULL)
		info->chipboard[board_id].u_board = value / 10.0;
	else {
		info->chipboard[board_id].u_board = 0.0;
		return -1;
	}

	/* P1 */
	start = get_int_val(start, "P1", ';', (int *)&value);
	if (start != NULL)
		info->chipboard[board_id].p_chain1_enabled = value;
	else {
		info->chipboard[board_id].p_chain1_enabled = 0;
		return -1;
	}

	/* P2 */
	start = get_int_val(start, "P2", ';', (int *)&value);
	if (start != NULL)
		info->chipboard[board_id].p_chain2_enabled = value;
	else {
		info->chipboard[board_id].p_chain2_enabled = 0;
		return -1;
	}

	/* U1 */
	start = get_int_val(start, "U1", ';', (int *)&value);
	if (start != NULL)
		info->chipboard[board_id].u_chain1 = value / 10.0;
	else {
		info->chipboard[board_id].u_chain1 = 0.0;
		return -1;
	}

	/* U2 */
	start = get_int_val(start, "U2", ';', (int *)&value);
	if (start != NULL)
		info->chipboard[board_id].u_chain2 = value / 10.0;
	else {
		info->chipboard[board_id].u_chain2 = 0.0;
		return -1;
	}

	/* I1 */
	start = get_int_val(start, "I1", ';', (int *)&value);
	if (start != NULL)
		info->chipboard[board_id].i_chain1 = value / 10.0;
	else {
		info->chipboard[board_id].i_chain1 = 0.0;
		return -1;
	}

	/* I2 */
	start = get_int_val(start, "I2", ';', (int *)&value);
	if (start != NULL)
		info->chipboard[board_id].i_chain2 = value / 10.0;
	else {
		info->chipboard[board_id].i_chain2 = 0.0;
		return -1;
	}

	/* RPM */
	start = get_int_val(start, "RPM", ';', (int *)&info->chipboard[board_id].rpm);
	if (start == NULL) {
		info->chipboard[board_id].rpm = 0;
		return -1;
	}

	/* A - alarms - may be absent */
	bool alarm = false;
	for (i = 0; i < 3; i++) {
		memset(strvalue, 0, sizeof(strvalue));
		start = get_str_val(start, "A", ';', strvalue);
		if (start == NULL)
			return -1;

		if (strcmp(strvalue, "T") == 0) {
			info->chipboard[board_id].a_temp = 1;
			alarm = true;
		} else if (strcmp(strvalue, "I1") == 0) {
			info->chipboard[board_id].a_ichain1 = 1;
			alarm = true;
		} else if (strcmp(strvalue, "I2") == 0) {
			info->chipboard[board_id].a_ichain2 = 1;
			alarm = true;
		}
	}

	if (alarm == false) {
		info->chipboard[board_id].a_temp    = 0;
		info->chipboard[board_id].a_ichain1 = 0;
		info->chipboard[board_id].a_ichain2 = 0;
	}

	/* F */
	start = get_int_val(start, "F", ';', (int *)&value);
	if (start != NULL)
		info->chipboard[board_id].fan_speed = value;
	else {
		info->chipboard[board_id].fan_speed = 0;
		return -1;
	}

	/* AI */
	start = get_int_val(start, "AI", ';', (int *)&value);
	if (start != NULL)
		info->chipboard[board_id].i_alarm = value / 10.0;
	else {
		info->chipboard[board_id].i_alarm = 0.0;
		return -1;
	}

	/* AT */
	start = get_int_val(start, "AT", ';', (int *)&value);
	if (start != NULL)
		info->chipboard[board_id].t_alarm = value / 10.0;
	else {
		info->chipboard[board_id].t_alarm = 0.0;
		return -1;
	}

	/* AG */
	start = get_int_val(start, "AG", ';', (int *)&value);
	if (start != NULL)
		info->chipboard[board_id].t_gisteresis = value / 10.0;
	else {
		info->chipboard[board_id].t_gisteresis = 0.0;
		return -1;
	}

	/* FM */
	memset(strvalue, 0, sizeof(strvalue));
	start = get_str_val(start, "FM", ';', strvalue);
	if (start != NULL)
		info->chipboard[board_id].fan_mode = strvalue[0];
	else
		return -1;

	/* TT */
	start = get_int_val(start, "TT", '#', (int *)&value);
	if (start != NULL)
		info->chipboard[board_id].target_temp = value / 10.0;
	else {
		info->chipboard[board_id].target_temp = 0.0;
		return -1;
	}

	return 0;
}

int8_t enable_power_chain(struct cgpu_info *bitfury, uint8_t board_id, uint8_t chain)
{
	struct bitfury16_info *info = (struct bitfury16_info *)(bitfury->device_data);

	if (info->chipboard[board_id].detected == true) {
		char uart_cmd[8];
		memset(uart_cmd, 0, sizeof(uart_cmd));

		switch (chain) {
			case 0:
				sprintf(uart_cmd, "P");
				break;
			case 1:
			case 2:
				sprintf(uart_cmd, "P:%d", chain);
				break;
		}

		if (device_uart_transfer(board_id + 1, uart_cmd) < 0) {
			switch (chain) {
				case 0:
					applog(LOG_ERR, "%s: failed to enable BOARD%d power chain",
							bitfury->drv->name, board_id + 1);
					break;
				case 1:
				case 2:
					applog(LOG_ERR, "%s: failed to enable BOARD%d power chain %d",
							bitfury->drv->name, board_id + 1, chain);
					break;
			}

		} else {
			switch (chain) {
				case 0:
					applog(LOG_NOTICE, "%s: enabled BOARD%d power chain",
							bitfury->drv->name, board_id + 1);
					break;
				case 1:
				case 2:
					applog(LOG_NOTICE, "%s: enabled BOARD%d power chain %d",
							bitfury->drv->name, board_id + 1, chain);
					break;
			}

			return 0;
		}
	}

	return -1;
}

int8_t disable_power_chain(struct cgpu_info *bitfury, uint8_t board_id, uint8_t chain)
{
	struct bitfury16_info *info = (struct bitfury16_info *)(bitfury->device_data);

	if (info->chipboard[board_id].detected == true) {
		char uart_cmd[8];
		memset(uart_cmd, 0, sizeof(uart_cmd));

		switch (chain) {
			case 0:
				sprintf(uart_cmd, "O");
				break;
			case 1:
			case 2:
				sprintf(uart_cmd, "O:%d", chain);
				break;
		}

		if (device_uart_transfer(board_id + 1, uart_cmd) < 0) {
			switch (chain) {
				case 0:
					applog(LOG_ERR, "%s: failed to disable BOARD%d power chain",
							bitfury->drv->name, board_id + 1);
					break;
				case 1:
				case 2:
					applog(LOG_ERR, "%s: failed to disable BOARD%d power chain %d",
							bitfury->drv->name, board_id + 1, chain);
					break;
			}

		} else {
			switch (chain) {
				case 0:
					applog(LOG_NOTICE, "%s: disabled BOARD%d power chain",
							bitfury->drv->name, board_id + 1);
					break;
				case 1:
				case 2:
					applog(LOG_NOTICE, "%s: disabled BOARD%d power chain %d",
							bitfury->drv->name, board_id + 1, chain);
					break;
			}
			return 0;
		}
	}

	return -1;
}

void led_red_enable(struct bitfury16_info *info)
{
	if (device_ctrl_transfer(SPI_CHANNEL1, 1, F_LED2) == 0) {
		info->led_red_enabled = true;
		gettimeofday(&info->led_red_switch, NULL);
	}
}

void led_red_disable(struct bitfury16_info *info)
{
	if (device_ctrl_transfer(SPI_CHANNEL1, 0, F_LED2) == 0) {
		info->led_red_enabled = false;
		gettimeofday(&info->led_red_switch, NULL);
	}
}

void led_green_enable(struct bitfury16_info *info)
{
	if (device_ctrl_transfer(SPI_CHANNEL1, 1, F_LED1) == 0) {
		info->led_green_enabled = true;
		gettimeofday(&info->led_green_switch, NULL);
	}
}

void led_green_disable(struct bitfury16_info *info)
{
	if (device_ctrl_transfer(SPI_CHANNEL1, 0, F_LED1) == 0) {
		info->led_green_enabled = false;
		gettimeofday(&info->led_green_switch, NULL);
	}
}

void buzzer_enable(struct bitfury16_info *info)
{
	if (device_ctrl_transfer(SPI_CHANNEL1, 1, F_BUZZER) == 0) {
		info->buzzer_enabled = true;
		gettimeofday(&info->buzzer_switch, NULL);
	}
}

void buzzer_disable(struct bitfury16_info *info)
{
	if (device_ctrl_transfer(SPI_CHANNEL1, 0, F_BUZZER) == 0) {
		info->buzzer_enabled = false;
		gettimeofday(&info->buzzer_switch, NULL);
	}
}
