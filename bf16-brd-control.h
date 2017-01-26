#ifndef BF16_BRD_CONTROL_H
#define BF16_BRD_CONTROL_H

#include <stdint.h>

#include "bf16-gpiodevice.h"

#define BIT_STATE(data, pin) ( (data & BV(pin)) >> pin )
#define BIT_INV_STATE(data, pin) ( BIT_STATE(data, pin) ^ 1 )

#define BUZZER_PIN      2   //  GPIO2[2],  GPIO_66, X8.7
#define BRD_VER0_PIN    3   //  GPIO2[3],  GPIO_67, X8.8
#define BRD_VER1_PIN    5   //  GPIO2[5],  GPIO_69, X8.9
#define BRD_VER2_PIN    4   //  GPIO2[4],  GPIO_68, X8.10
#define BRD_VER3_PIN    13  //  GPIO1[13], GPIO_45, X8.11
#define BRD_DET2_PIN    12  //  GPIO1[12], GPIO_44, X8.12
#define BRD_DET1_PIN    23  //  GPIO0[23], GPIO_23, X8.13
#define LED_GREEN_PIN   26  //  GPIO0[26], GPIO_26, X8.14
#define LED_RED_PIN     15  //  GPIO1[15], GPIO_47, X8.15
#define CH2_MSP_RST_PIN 14  //  GPIO1[14], GPIO_46, X8.16
#define CH1_MSP_RST_PIN 27  //  GPIO0[27], GPIO_27, X8.17
#define BRD_BUT1_PIN    1   //  GPIO2[1],  GPIO_65, X8.18
#define BRD_BUT2_PIN    22  //  GPIO0[22], GPIO_22, X8.19
#define CH1_SPI_RES_PIN 22  //  GPIO2[22], GPIO_86, X8.27
#define CH2_SPI_RES_PIN 29  //  GPIO1[29], GPIO_61, X8.26


void brd_init(void);

int get_hw_ver(void);
int get_btn_fr(void);
int get_btn_discovery(void);
int get_ch1_det(void);
int get_ch2_det(void);
int8_t set_buzzer(uint8_t state);
int8_t set_led_green(uint8_t state);
int8_t set_led_red(uint8_t state);
int8_t set_ch1_rst(uint8_t state);
int8_t set_ch2_rst(uint8_t state);
int8_t set_ch1_spi(uint8_t state);
int8_t set_ch2_spi(uint8_t state);

#endif /* BF16_BRD_CONTROL_H */
