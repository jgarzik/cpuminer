/*
 * Copyright 2014 Con Kolivas
 * Copyright 2013 Andrew Smith
 * Copyright 2013 bitfury
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 3 of the License, or (at your option)
 * any later version.  See COPYING for more details.
 */

#include "sha2.h"
void ms3steps(uint32_t *p)
{
	uint32_t a, b, c, d, e, f, g, h, new_e, new_a;
	int i;

	a = p[0];
	b = p[1];
	c = p[2];
	d = p[3];
	e = p[4];
	f = p[5];
	g = p[6];
	h = p[7];
	for (i = 0; i < 3; i++) {
		new_e = p[i+16] + sha256_k[i] + h + CH(e,f,g) + SHA256_F2(e) + d;
		new_a = p[i+16] + sha256_k[i] + h + CH(e,f,g) + SHA256_F2(e) +
			SHA256_F1(a) + MAJ(a,b,c);
		d = c;
		c = b;
		b = a;
		a = new_a;
		h = g;
		g = f;
		f = e;
		e = new_e;
	}
	p[15] = a;
	p[14] = b;
	p[13] = c;
	p[12] = d;
	p[11] = e;
	p[10] = f;
	p[9] = g;
	p[8] = h;
}

uint32_t decnonce(uint32_t in)
{
	uint32_t out;

	/* First part load */
	out = (in & 0xFF) << 24;
	in >>= 8;

	/* Byte reversal */
	in = (((in & 0xaaaaaaaa) >> 1) | ((in & 0x55555555) << 1));
	in = (((in & 0xcccccccc) >> 2) | ((in & 0x33333333) << 2));
	in = (((in & 0xf0f0f0f0) >> 4) | ((in & 0x0f0f0f0f) << 4));

	out |= (in >> 2) & 0x3FFFFF;

	/* Extraction */
	if (in & 1)
		out |= (1 << 23);
	if (in & 2)
		out |= (1 << 22);

	out -= 0x800004;
	return out;
}
