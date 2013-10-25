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

#define WORK_HISTORY_LEN 2

struct drillbit_chip_info;

/* drillbit_info structure applies to entire device */
struct drillbit_info {
  struct cgpu_info *base_cgpu;
  uint8_t version;
  char product[8];
  uint32_t serial;
  uint8_t num_chips;
  struct drillbit_chip_info *chips;
};

struct drillbit_chip_info {
  uint16_t chip_id;
  struct work *current_work[WORK_HISTORY_LEN];
  bool has_work;
  struct timeval tv_start;
};

#endif /* BITFURY_H */
