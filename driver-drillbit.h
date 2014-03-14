/*
 * Copyright 2013 Con Kolivas
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 3 of the License, or (at your option)
 * any later version.  See COPYING for more details.
 */

#ifndef BITFURY_H
#define BITFURY_H

#include "miner.h"
#include "usbutils.h"

#define WORK_HISTORY_LEN 4

struct drillbit_chip_info;

/* drillbit_info structure applies to entire device */
struct drillbit_info {
  struct cgpu_info *base_cgpu;
  uint8_t protocol_version;
  uint8_t num_chips;
  uint16_t capabilities;
  char product[8];
  uint32_t serial;
  struct drillbit_chip_info *chips;
  struct timeval tv_lastchipinfo;
  struct timeval tv_lasttemp;
  uint16_t temp;
  uint16_t max_temp;
};

enum drillbit_chip_state {
  IDLE,            /* Has no work */
  WORKING_NOQUEUED, /* Has current work but nothing queued as "next work" */
  WORKING_QUEUED   /* Has current work and a piece of work queued for after that */
};

struct drillbit_chip_info {
  uint16_t chip_id;
  struct work *current_work[WORK_HISTORY_LEN];
  enum drillbit_chip_state state;
  struct timeval tv_start;
  uint32_t success_count;
  uint32_t error_count;
  uint32_t timeout_count;
  uint32_t work_sent_count;
  uint32_t success_auto;
  uint32_t error_auto;
  int auto_delta;
  int auto_max;
};

#endif /* BITFURY_H */
