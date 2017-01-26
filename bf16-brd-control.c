#include "bf16-brd-control.h"
#include "miner.h"

#define BV(x)  (1 << x)

static uint32_t ctrl_read(uint8_t gpio, uint8_t reg)
{
	gpio_rq_t rq;

	rq.gpioIndex = gpio;
	rq.regIndex = reg;
	gpio_read_ctrl(&rq);

	return rq.data;
}

static int8_t ctrl_write(uint8_t gpio, uint8_t reg, uint32_t data)
{
	gpio_rq_t rq;

	rq.gpioIndex = gpio;
	rq.regIndex = reg;
	rq.data = data;
	return gpio_write_ctrl(&rq);
}

void brd_init(void)
{
	uint32_t data = ctrl_read(GPIO0_INDEX, OE_REG_INDEX);
	data &= ~(BV(CH1_MSP_RST_PIN) | BV(LED_GREEN_PIN));
	ctrl_write(GPIO0_INDEX, OE_REG_INDEX, data);

	data = ctrl_read(GPIO1_INDEX, OE_REG_INDEX);
	data &= ~(BV(LED_RED_PIN) | BV(CH2_MSP_RST_PIN) | BV(CH1_SPI_RES_PIN));
	ctrl_write(GPIO1_INDEX, OE_REG_INDEX, data);

	data = ctrl_read(GPIO2_INDEX, OE_REG_INDEX);
	data &= ~(BV(BUZZER_PIN) | BV(CH2_SPI_RES_PIN));
	ctrl_write(GPIO2_INDEX, OE_REG_INDEX, data);
}

int get_hw_ver(void)
{
	uint32_t data1 = ctrl_read(GPIO2_INDEX, DATAIN_REG_INDEX);
	uint32_t data2 = ctrl_read(GPIO1_INDEX, DATAIN_REG_INDEX);

	uint8_t result = BIT_STATE(data1, BRD_VER0_PIN);
	result |= BIT_STATE(data1, BRD_VER1_PIN) ? 2 : 0;
	result |= BIT_STATE(data1, BRD_VER2_PIN) ? 4 : 0;
	result |= BIT_STATE(data2, BRD_VER3_PIN) ? 8 : 0;

	return result;
}

int get_btn_fr(void)
{
	uint32_t data = ctrl_read(GPIO2_INDEX, DATAIN_REG_INDEX);
	return BIT_INV_STATE(data, BRD_BUT1_PIN);
}

int get_btn_discovery(void)
{
	uint32_t data = ctrl_read(GPIO0_INDEX, DATAIN_REG_INDEX);
	return BIT_INV_STATE(data, BRD_BUT2_PIN);
}

int get_ch1_det(void)
{
	uint32_t data = ctrl_read(GPIO0_INDEX, DATAIN_REG_INDEX);
	return BIT_INV_STATE(data, BRD_DET1_PIN);
}

int get_ch2_det(void)
{
	uint32_t data = ctrl_read(GPIO1_INDEX, DATAIN_REG_INDEX);
	return BIT_INV_STATE(data, BRD_DET2_PIN);
}

static uint8_t _direct_state_reg_index(uint8_t state)
{
	return (state != 0) ? DATASET_REG_INDEX : DATACLR_REG_INDEX;
}

static uint8_t _inverse_state_reg_index(uint8_t state)
{
	return (state != 0) ? DATACLR_REG_INDEX : DATASET_REG_INDEX;
}

int8_t set_buzzer(uint8_t state)
{
	return ctrl_write(GPIO2_INDEX, _direct_state_reg_index(state), BV(BUZZER_PIN));
}

int8_t set_led_green(uint8_t state)
{
	return ctrl_write(GPIO0_INDEX, _direct_state_reg_index(state), BV(LED_GREEN_PIN));
}

int8_t set_led_red(uint8_t state)
{
	return ctrl_write(GPIO1_INDEX, _direct_state_reg_index(state), BV(LED_RED_PIN));
}

int8_t set_ch1_rst(uint8_t state)
{
	return ctrl_write(GPIO0_INDEX, _inverse_state_reg_index(state), BV(CH1_MSP_RST_PIN));
}

int8_t set_ch2_rst(uint8_t state)
{
	return ctrl_write(GPIO1_INDEX, _inverse_state_reg_index(state), BV(CH2_MSP_RST_PIN));
}

int8_t set_ch1_spi(uint8_t state)
{
	return ctrl_write(GPIO2_INDEX, _direct_state_reg_index(state), BV(CH1_SPI_RES_PIN));
}

int8_t set_ch2_spi(uint8_t state)
{
	return ctrl_write(GPIO1_INDEX, _direct_state_reg_index(state), BV(CH2_SPI_RES_PIN));
}
