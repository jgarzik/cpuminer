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

#include "miner.h"
#include "libbitfury.h"
#include "sha2.h"

#define BITFURY_REFRESH_DELAY 100

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

/* Test vectors to calculate (using address-translated loads) */
static unsigned int atrvec[] = {
	0xb0e72d8e, 0x1dc5b862, 0xe9e7c4a6, 0x3050f1f5, 0x8a1a6b7e, 0x7ec384e8, 0x42c1c3fc, 0x8ed158a1, /* MIDSTATE */
	0,0,0,0,0,0,0,0,
	0x8a0bb7b7, 0x33af304f, 0x0b290c1a, 0xf0c4e61f, /* WDATA: hashMerleRoot[7], nTime, nBits, nNonce */

	0x9c4dfdc0, 0xf055c9e1, 0xe60f079d, 0xeeada6da, 0xd459883d, 0xd8049a9d, 0xd49f9a96, 0x15972fed, /* MIDSTATE */
	0,0,0,0,0,0,0,0,
	0x048b2528, 0x7acb2d4f, 0x0b290c1a, 0xbe00084a, /* WDATA: hashMerleRoot[7], nTime, nBits, nNonce */

	0x0317b3ea, 0x1d227d06, 0x3cca281e, 0xa6d0b9da, 0x1a359fe2, 0xa7287e27, 0x8b79c296, 0xc4d88274, /* MIDSTATE */
	0,0,0,0,0,0,0,0,
	0x328bcd4f, 0x75462d4f, 0x0b290c1a, 0x002c6dbc, /* WDATA: hashMerleRoot[7], nTime, nBits, nNonce */

	0xac4e38b6, 0xba0e3b3b, 0x649ad6f8, 0xf72e4c02, 0x93be06fb, 0x366d1126, 0xf4aae554, 0x4ff19c5b, /* MIDSTATE */
	0,0,0,0,0,0,0,0,
	0x72698140, 0x3bd62b4f, 0x3fd40c1a, 0x801e43e9, /* WDATA: hashMerleRoot[7], nTime, nBits, nNonce */

	0x9dbf91c9, 0x12e5066c, 0xf4184b87, 0x8060bc4d, 0x18f9c115, 0xf589d551, 0x0f7f18ae, 0x885aca59, /* MIDSTATE */
	0,0,0,0,0,0,0,0,
	0x6f3806c3, 0x41f82a4f, 0x3fd40c1a, 0x00334b39, /* WDATA: hashMerleRoot[7], nTime, nBits, nNonce */
};

static int rehash(unsigned char *midstate, unsigned m7, unsigned ntime, unsigned nbits,
		  unsigned nnonce)
{
	unsigned char in[16];
	unsigned int *in32 = (unsigned int *)in;
	char *hex;
	unsigned int *mid32 = (unsigned int *)midstate;
	unsigned out32[8];
	unsigned char *out = (unsigned char *) out32;
	sha256_ctx ctx;

	memset( &ctx, 0, sizeof( sha256_ctx ) );
	memcpy(ctx.h, mid32, 8*4);
	ctx.tot_len = 64;

	nnonce = bswap_32(nnonce);
	in32[0] = bswap_32(m7);
	in32[1] = bswap_32(ntime);
	in32[2] = bswap_32(nbits);
	in32[3] = nnonce;

	sha256_update(&ctx, in, 16);
	sha256_final(&ctx, out);
	sha256(out, 32, out);

	if (out32[7] == 0) {
		hex = bin2hex(midstate, 32);
		hex = bin2hex(out, 32);
		applog(LOG_INFO, "! MS0: %08x, m7: %08x, ntime: %08x, nbits: %08x, nnonce: %08x\n\t\t\t out: %s\n", mid32[0], m7, ntime, nbits, nnonce, hex);
		return 1;
	}
	return 0;
}

void bitfury_work_to_payload(struct bitfury_payload *p, struct work *w)
{
	unsigned char flipped_data[80];

	memset(p, 0, sizeof(struct bitfury_payload));
	flip80(flipped_data, w->data);

	memcpy(p->midstate, w->midstate, 32);
	p->m7 = bswap_32(*(unsigned *)(flipped_data + 64));
	p->ntime = bswap_32(*(unsigned *)(flipped_data + 68));
	p->nbits = bswap_32(*(unsigned *)(flipped_data + 72));
	applog(LOG_INFO, "INFO nonc: %08x bitfury_scanHash MS0: %08x, ", p->nnonce, ((unsigned int *)w->midstate)[0]);
	applog(LOG_INFO, "INFO merkle[7]: %08x, ntime: %08x, nbits: %08x", p->m7, p->ntime, p->nbits);
}

void spi_clear_buf(struct bitfury_info *info)
{
	info->spibufsz = 0;
}

void spi_add_buf(struct bitfury_info *info, const void *buf, const int sz)
{
	if (unlikely(info->spibufsz + sz > NF1_SPIBUF_SIZE)) {
		applog(LOG_WARNING, "SPI bufsize overflow!");
		return;
	}
	memcpy(&info->spibuf[info->spibufsz], buf, sz);
	info->spibufsz += sz;
}

void spi_add_break(struct bitfury_info *info)
{
	spi_add_buf(info, "\x4", 1);
}

static void spi_add_buf_reverse(struct bitfury_info *info, const char *buf, const int sz)
{
	int i;

	for (i = 0; i < sz; i++) { // Reverse bit order in each byte!
		unsigned char p = buf[i];

		p = ((p & 0xaa) >> 1) | ((p & 0x55) << 1);
		p = ((p & 0xcc) >> 2) | ((p & 0x33) << 2);
		p = ((p & 0xf0) >> 4) | ((p & 0x0f) << 4);
		info->spibuf[info->spibufsz + i] = p;
	}
	info->spibufsz += sz;
}

void spi_add_data(struct bitfury_info *info, uint16_t addr, const void *buf, int len)
{
	unsigned char otmp[3];

	if (len < 4 || len > 128) {
		applog(LOG_WARNING, "Can't add SPI data size %d", len);
		return;
	}
	len /= 4; /* Strip */
	otmp[0] = (len - 1) | 0xE0;
	otmp[1] = (addr >> 8) & 0xFF;
	otmp[2] = addr & 0xFF;
	spi_add_buf(info, otmp, 3);
	len *= 4;
	spi_add_buf_reverse(info, buf, len);
}

// Bit-banging reset... Each 3 reset cycles reset first chip in chain
bool spi_reset(struct cgpu_info *bitfury, struct bitfury_info *info)
{
	struct mcp_settings *mcp = &info->mcp;
	int r;

	// SCK_OVRRIDE
	mcp->value.pin[NF1_PIN_SCK_OVR] = MCP2210_GPIO_PIN_HIGH;
	mcp->direction.pin[NF1_PIN_SCK_OVR] = MCP2210_GPIO_OUTPUT;
	mcp->designation.pin[NF1_PIN_SCK_OVR] = MCP2210_PIN_GPIO;
	if (!mcp2210_set_gpio_settings(bitfury, mcp))
		return false;

	for (r = 0; r < 16; ++r) {
		char buf[1] = {0x81}; // will send this waveform: - _ _ _ _ _ _ -
		unsigned int length = 1;

		if (!mcp2210_spi_transfer(bitfury, buf, &length))
			return false;
	}

	// Deactivate override
	mcp->direction.pin[NF1_PIN_SCK_OVR] = MCP2210_GPIO_INPUT;
	if (!mcp2210_set_gpio_settings(bitfury, mcp))
		return false;

	return true;
}

bool spi_txrx(struct cgpu_info *bitfury, struct bitfury_info *info)
{
	unsigned int length, sendrcv;
	int offset = 0;

	if (!spi_reset(bitfury, info))
		return false;
	length = info->spibufsz;
	applog(LOG_DEBUG, "%s %d: SPI sending %u bytes total", bitfury->drv->name,
	       bitfury->device_id, length);
	while (length > MCP2210_TRANSFER_MAX) {
		sendrcv = MCP2210_TRANSFER_MAX;
		if (!mcp2210_spi_transfer(bitfury, info->spibuf + offset, &sendrcv))
			return false;
		if (sendrcv != MCP2210_TRANSFER_MAX) {
			applog(LOG_DEBUG, "%s %d: Send/Receive size mismatch sent %d received %d",
			       bitfury->drv->name, bitfury->device_id, MCP2210_TRANSFER_MAX, sendrcv);
		}
		length -= MCP2210_TRANSFER_MAX;
		offset += MCP2210_TRANSFER_MAX;
	}
	sendrcv = length;
	if (!mcp2210_spi_transfer(bitfury, info->spibuf + offset, &sendrcv))
		return false;
	if (sendrcv != length) {
		applog(LOG_WARNING, "%s %d: Send/Receive size mismatch sent %d received %d",
		       bitfury->drv->name, bitfury->device_id, length, sendrcv);
		return false;
	}
	return true;
}

void libbitfury_sendHashData(struct cgpu_info *bf)
{
	struct bitfury_info *info = bf->device_data;
	static unsigned second_run;
	unsigned *newbuf = info->newbuf;
	unsigned *oldbuf = info->oldbuf;
	struct bitfury_payload *p = &(info->payload);
	struct bitfury_payload *op = &(info->opayload);

	/* Programming next value */
	memcpy(atrvec, p, 20 * 4);
	ms3steps(atrvec);

	spi_clear_buf(info);
	spi_add_break(info);
	spi_add_data(info, 0x3000, (void*)&atrvec[0], 19 * 4);
	spi_txrx(bf, info);

	memcpy(newbuf, info->spibuf + 4, 17 * 4);

	info->job_switched = newbuf[16] != oldbuf[16];

	if (second_run && info->job_switched) {
		int i;
		int results_num = 0;
		unsigned int *results = info->results;

		for (i = 0; i < 16; i++) {
			if (oldbuf[i] != newbuf[i]) {
				unsigned pn; //possible nonce
				unsigned int s = 0; //TODO zero may be solution

				pn = decnonce(newbuf[i]);
				s |= rehash(op->midstate, op->m7, op->ntime, op->nbits, pn) ? pn : 0;
				s |= rehash(op->midstate, op->m7, op->ntime, op->nbits, pn-0x400000) ? pn - 0x400000 : 0;
				s |= rehash(op->midstate, op->m7, op->ntime, op->nbits, pn-0x800000) ? pn - 0x800000 : 0;
				s |= rehash(op->midstate, op->m7, op->ntime, op->nbits, pn+0x2800000)? pn + 0x2800000 : 0;
				s |= rehash(op->midstate, op->m7, op->ntime, op->nbits, pn+0x2C00000)? pn + 0x2C00000 : 0;
				s |= rehash(op->midstate, op->m7, op->ntime, op->nbits, pn+0x400000) ? pn + 0x400000 : 0;
				if (s)
					results[results_num++] = bswap_32(s);
			}
		}
		info->results_n = results_num;

		memcpy(op, p, sizeof(struct bitfury_payload));
		memcpy(oldbuf, newbuf, 17 * 4);
	}

	cgsleep_ms(BITFURY_REFRESH_DELAY);
	second_run = 1;
}
