/*
 * Transport layer interface for KnCminer devices
 *
 * Copyright 2014 KnCminer
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 3 of the License, or (at your option)
 * any later version.  See COPYING for more details.
 */

#define	MAX_ASICS		6
#define	NUM_DIES_IN_ASIC	4
#define	CORES_IN_DIE		48
#define	CORES_PER_ASIC		(NUM_DIES_IN_ASIC * CORES_IN_DIE)

#define	MAX_BYTES_IN_SPI_XSFER	4096

void *knc_trnsp_new(int dev_idx);
void knc_trnsp_free(void *opaque_ctx);
int knc_trnsp_transfer(void *opaque_ctx, uint8_t *txbuf, uint8_t *rxbuf, int len);
bool knc_trnsp_asic_detect(void *opaque_ctx, int chip_id);
void knc_trnsp_periodic_check(void *opaque_ctx);
