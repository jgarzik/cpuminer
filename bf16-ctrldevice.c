#include "bf16-ctrldevice.h"
#include "miner.h"

#define MAX_TOKENS_ALLOWED  10

char *ctrl_device_name  = "ctrl";

static char *d_state_on  = "ON";
static char *d_state_off = "OFF";

static char* D_STATE(int state)
{
	return (state == 0) ? d_state_off : d_state_on;
}

char* get_ctrl_data(int channel, int state, int fn)
{
	char *request = malloc(CTRL_BUFFER_SIZE);
	memset(request, 0, CTRL_BUFFER_SIZE);
	int index = (state == 0) ? 0 : 1;

	switch (fn) {
		case F_BUZZER:
			sprintf(request, "%s=%s\n", BUZZER, D_STATE(index));
			break;
		case F_LED1:
			sprintf(request, "%s=%s\n", LED_GREEN, D_STATE(index));
			break;
		case F_LED2:
			sprintf(request, "%s=%s\n", LED_RED, D_STATE(index));
			break;
		case F_BRST:
			(channel == 1) ?
				sprintf(request, "%s=%s\n", CH1_MSP_RST, D_STATE(index)) :
				sprintf(request, "%s=%s\n", CH2_MSP_RST, D_STATE(index));
			break;
		case F_RST:
			(channel == 1) ?
				sprintf(request, "%s=%s\n", CH1_SPI_RES, D_STATE(index)) :
				sprintf(request, "%s=%s\n", CH2_SPI_RES, D_STATE(index));
			break;
		case F_BDET:
			(channel == 1) ?
				sprintf(request, "%s=%s\n", CH1_BRD_DET, D_STATE(get_ch1_det())) :
				sprintf(request, "%s=%s\n", CH2_BRD_DET, D_STATE(get_ch2_det()));
			break;
		case F_STAT:
		default:
			request[0] = '\n';
	}

	return request;
}

static int D_VALUE(char *value)
{
	if (strcasecmp(value, d_state_on) == 0)
		return 1;

	if (strcasecmp(value, d_state_off) == 0)
		return 0;

	return -1;
}

static int8_t D_FUNCTION(uint8_t value, char *cmd)
{
	if (strcmp(cmd, BUZZER) == 0)
		return set_buzzer(value);
	else if (strcmp(cmd, LED_GREEN) == 0)
		return set_led_green(value);
	else if (strcmp(cmd, LED_RED) == 0)
		return set_led_red(value);
	else if (strcmp(cmd, CH1_MSP_RST) == 0)
		return set_ch1_rst(value);
	else if (strcmp(cmd, CH2_MSP_RST) == 0)
		return set_ch2_rst(value);
	else if (strcmp(cmd, CH1_SPI_RES) == 0)
		return set_ch1_spi(value);
	else if (strcmp(cmd, CH2_SPI_RES) == 0)
		return set_ch2_spi(value);
	else
		applog(LOG_ERR, "CTRL DEVICE, D_FUNCTION: unknown command: [%s]", cmd);

	return -1;
}

static int split_tokens(char* cmd, char token, char** tokens)
{
	int len=0;
	char *src, *ptr;

	src = cmd;
	ptr = cmd;
	while ((*src != '\n') && (*src != '\r') && (*src != '\0') &&
	       (len < MAX_TOKENS_ALLOWED)) {
		if (*src == token) {
			*src = 0;
			tokens[len++] = ptr;
			ptr = src + 1;
		}
		src++;
	}

	*src = 0;
	if (src != ptr)
		tokens[len++] = ptr;

	return len;
}

int8_t ctrl_init(device_t* attr, char *device, uint16_t size)
{
	attr->device = device;
	attr->mode = 0;
	attr->speed = 0;
	attr->bits = 0;
	attr->size = size;
	attr->rx = malloc(size);
	attr->tx = malloc(size);

	brd_init();

	return 0;
}

int8_t ctrl_transfer(device_t *attr)
{
	char* cmd = (char*) attr->tx;
	char* tokens[MAX_TOKENS_ALLOWED];
	int8_t ret = 0;

	int len = split_tokens(cmd, ';', tokens);
	cmd = (char*) attr->rx;

	if (len == 0) {
		len += snprintf(cmd + len, attr->size, "%s=%d;", HW_VER,        get_hw_ver());
		len += snprintf(cmd + len, attr->size, "%s=%s;", BTN_FR,        D_STATE(get_btn_fr()));
		len += snprintf(cmd + len, attr->size, "%s=%s;", BTN_DISCOVERY, D_STATE(get_btn_discovery()));
		len += snprintf(cmd + len, attr->size, "%s=%s;", CH1_DET,       D_STATE(get_ch1_det()));
		len += snprintf(cmd + len, attr->size, "%s=%s\n",CH2_DET,       D_STATE(get_ch2_det()));
		attr->datalen = len;
	} else {
		char* values[2];
		int i;
		int datalen = 0;

		for (i = 0; i < len; i++) {
			char *tok = tokens[i];
			int n = split_tokens(tok, '=', values);

			if (n == 2) {
				int state = D_VALUE(values[1]);

				if (state != -1) {
					applog(LOG_DEBUG, "BF16: CTRL: token applying: %s>%s", values[0], values[1]);

					if (strcasecmp(values[0], CH1_BRD_DET) == 0)
						datalen += snprintf(cmd + datalen, attr->size, "%s:%s\n", CH1_DET, D_STATE(get_ch1_det()));
					else if (strcasecmp(values[0], CH2_BRD_DET) == 0)
						datalen += snprintf(cmd + datalen, attr->size, "%s:%s\n", CH2_DET, D_STATE(get_ch2_det()));
					else
						ret = D_FUNCTION(state, values[0]);
				}
			}
		}

		if (datalen == 0) {
			datalen = 1;
			cmd[0] = '\n';
		} else
			attr->datalen = datalen;
	}

	return ret;
}

void ctrl_release(device_t *attr)
{
	free(attr->rx);
	free(attr->tx);
}
