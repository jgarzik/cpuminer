#ifndef BF16_GPIODEVICE_H
#define BF16_GPIODEVICE_H

#include "bf16-device.h"

#define GPIO_BUFFER_SIZE 64

/* GPIO/CTRL function types */
#define GPIO0_INDEX         0
#define GPIO1_INDEX         1
#define GPIO2_INDEX         2
#define GPIO3_INDEX         3
#define MAX_GPIO_INDEX      4

#define GPIO_SIZE           0x2000
#define GPIO0_START_ADDR    0x44E07000
#define GPIO1_START_ADDR    0x4804C000
#define GPIO2_START_ADDR    0x481AC000
#define GPIO3_START_ADDR    0x481AE000

#define GPIO_OE             0x134
#define GPIO_SETDATAOUT     0x194
#define GPIO_CLEARDATAOUT   0x190
#define GPIO_DATAIN         0x138
#define GPIO_DATAOUT        0x13C

#define OE_REG_INDEX        0
#define DATAOUT_REG_INDEX   1
#define DATASET_REG_INDEX   2
#define DATACLR_REG_INDEX   3
#define DATAIN_REG_INDEX    4
#define MAX_REGISTER_INDEX  5

typedef struct {
	uint32_t    gpioIndex;
	uint32_t    regIndex;
	uint32_t    data;
} gpio_rq_t;

typedef struct {
	uint32_t    oe_reg0;
	uint32_t    out_reg0;
	uint32_t    in_reg0;

	uint32_t    oe_reg1;
	uint32_t    out_reg1;
	uint32_t    in_reg1;

	uint32_t    oe_reg2;
	uint32_t    out_reg2;
	uint32_t    in_reg2;

	uint32_t    oe_reg3;
	uint32_t    out_reg3;
	uint32_t    in_reg3;
} gpio_resp_t;

extern char *gpio_device_name;

int gpio_read_ctrl(gpio_rq_t* rq);
int8_t gpio_write_ctrl(gpio_rq_t* rq);

int8_t gpio_init(device_t* attr, char *device, uint16_t size);
void gpio_release(device_t *attr);

#endif /* BF16_GPIODEVICE_H */
