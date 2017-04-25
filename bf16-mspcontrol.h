#ifndef BF16_MSPCONTROL_H
#define BF16_MSPCONTROL_H

#include "driver-bitfury16.h"

void parse_board_detect(struct cgpu_info *bitfury, uint8_t board_id, char* data);
void get_board_info(struct cgpu_info *bitfury, uint8_t board_id);
int8_t parse_hwstats(struct bitfury16_info *info, uint8_t board_id, char* data);
int8_t enable_power_chain(struct cgpu_info *bitfury, uint8_t board_id, uint8_t chain);
int8_t disable_power_chain(struct cgpu_info *bitfury, uint8_t board_id, uint8_t chain);

void led_red_enable(struct bitfury16_info *info);
void led_red_disable(struct bitfury16_info *info);
void led_green_enable(struct bitfury16_info *info);
void led_green_disable(struct bitfury16_info *info);
void buzzer_enable(struct bitfury16_info *info);
void buzzer_disable(struct bitfury16_info *info);

#endif /* BF16_MSPCONTROL_H */
