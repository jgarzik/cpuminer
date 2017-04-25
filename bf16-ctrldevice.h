#ifndef BF16_CTRLDEVICE_H
#define BF16_CTRLDEVICE_H

#include "bf16-brd-control.h"
#include "bf16-device.h"

#define CTRL_BUFFER_SIZE    96

/* CTRL functions */
#define F_BUZZER        0
#define F_LED1          1
#define F_LED2          2
#define F_BRST          3
#define F_RST           4
#define F_BDET          5
#define F_STAT          6

/* read in signals */
#define HW_VER          "HW_VER"
#define BTN_FR          "BTN_FR"
#define BTN_DISCOVERY   "BTN_DISCOVERY"
#define CH1_DET         "CH1_DET"
#define CH2_DET         "CH2_DET"

/* write out signals */
#define BUZZER          "BUZZER"
#define LED_GREEN       "LED_GREEN"
#define LED_RED         "LED_RED"
#define CH1_MSP_RST     "CH1_MSP_RST"
#define CH2_MSP_RST     "CH2_MSP_RST"
#define CH1_SPI_RES     "CH1_SPI_RES"
#define CH2_SPI_RES     "CH2_SPI_RES"
#define CH1_BRD_DET     "CH1_BRD_DET"
#define CH2_BRD_DET     "CH2_BRD_DET"

extern char *ctrl_device_name;

int8_t ctrl_init(device_t* attr, char *device, uint16_t size);
int8_t ctrl_transfer(device_t *attr);
void ctrl_release(device_t *attr);
char* get_ctrl_data(int channel, int state, int fn);

#endif /* BF16_CTRLDEVICE_H */
