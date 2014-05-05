/*
 * Copyright 2013-2014 Con Kolivas
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
#include "mcp2210.h"

#define BXF_CLOCK_OFF 0
#define BXF_CLOCK_MIN 32
#define BXF_CLOCK_MAX 63 // Not really used since we only get hw errors above default

/* In tenths of a degree */
#define BXF_TEMP_TARGET 820
#define BXF_TEMP_HYSTERESIS 30

extern int opt_bxf_temp_target;
extern int opt_nfu_bits;
extern int opt_bxm_bits;
extern int opt_bxf_bits;
extern int opt_bxf_debug;
extern int opt_osm_led_mode;

#define NFU_PIN_LED 0
#define NFU_PIN_SCK_OVR 5
#define NFU_PIN_PWR_EN 6
#define NFU_PIN_PWR_EN0 7

#define SPIBUF_SIZE 16384
#define BITFURY_REFRESH_DELAY 100

#define SIO_RESET_REQUEST 0
#define SIO_SET_LATENCY_TIMER_REQUEST 0x09
#define SIO_SET_EVENT_CHAR_REQUEST    0x06
#define SIO_SET_ERROR_CHAR_REQUEST    0x07
#define SIO_SET_BITMODE_REQUEST       0x0B
#define SIO_RESET_PURGE_RX 1
#define SIO_RESET_PURGE_TX 2

#define BITMODE_RESET 0x00
#define BITMODE_MPSSE 0x02
#define SIO_RESET_SIO 0

#define BXM_LATENCY_MS 2

struct bitfury_payload {
	unsigned char midstate[32];
	unsigned int junk[8];
	unsigned m7;
	unsigned ntime;
	unsigned nbits;
	unsigned nnonce;
};

struct bitfury_info {
	struct cgpu_info *base_cgpu;
	struct thr_info *thr;
	enum sub_ident ident;
	int nonces;
	int total_nonces;
	double saved_nonces;
	int cycles;
	bool valid; /* Set on first valid data being found */
	bool failing; /* Set when an attempted restart has been sent */

	int chips;
	char product[8];

	/* BF1 specific data */
	uint8_t version;
	uint32_t serial;
	struct timeval tv_start;

	/* BXF specific data */
	pthread_mutex_t lock;
	pthread_t read_thr;
	int last_decitemp;
	int max_decitemp;
	int temp_target;
	int work_id; // Current work->subid
	int no_matching_work;
	int maxroll; // Last maxroll sent to device
	int ver_major;
	int ver_minor;
	int hw_rev;
	uint8_t clocks; // There are two but we set them equal
	int *filtered_hw; // Hardware errors we're told about but are filtered
	int *job; // Completed jobs we're told about
	int *submits; // Submitted responses

	/* NFU specific data */
	struct mcp_settings mcp;
	char spibuf[SPIBUF_SIZE];
	unsigned int spibufsz;
	int osc6_bits;

	/* Chip sized arrays */
	struct bitfury_payload *payload;
	unsigned int *oldbuf; // 17 vals per chip
	bool *job_switched;
	bool *second_run;
	struct work **work;
	struct work **owork;

	bool (*spi_txrx)(struct cgpu_info *, struct bitfury_info *info);
};

#endif /* BITFURY_H */
