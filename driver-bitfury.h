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

#define BXF_DEFAULT_CLOCK 54
#define BXF_MIN_CLOCK 0
#define BXF_MAX_CLOCK 63

struct bitfury_info {
	struct cgpu_info *base_cgpu;
	struct thr_info *thr;
	enum sub_ident ident;
	int nonces;
	int total_nonces;
	double saved_nonces;
	int cycles;
	bool valid; /* Set on first valid data being found */

	/* BF1 specific data */
	uint8_t version;
	char product[8];
	uint32_t serial;
	struct timeval tv_start;

	/* BXF specific data */
	pthread_mutex_t lock;
	pthread_t read_thr;
	double temperature;
	int max_decitemp;
	int work_id; // Current work->subid
	int no_matching_work;
	int maxroll; // Last maxroll sent to device
	int ver_major;
	int ver_minor;
	int hw_rev;
	int chips;
	uint8_t clocks; // There are two but we set them equal
	int filtered_hw[2]; // Hardware errors we're told about but are filtered
	int job[2]; // Completed jobs we're told about
};

#endif /* BITFURY_H */
