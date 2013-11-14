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

struct bitfury_info {
	struct cgpu_info *base_cgpu;
	struct thr_info *thr;
	enum sub_ident ident;
	int nonces;
	int total_nonces;
	double saved_nonces;
	int cycles;

	/* BF1 specific data */
	uint8_t version;
	char product[8];
	uint32_t serial;
	struct timeval tv_start;

	/* BXF specific data */
	pthread_mutex_t lock;
	pthread_t read_thr;
	double temperature;
	int work_id; // Current work->subid
	int no_matching_work;
	bool can_roll;
};

#endif /* BITFURY_H */
