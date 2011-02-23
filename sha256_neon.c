/*
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option) 
 * any later version.  See COPYING for more details.
 *
 * tcatm's 4-way 128-bit SSE2 SHA-256 modified to use ARM NEON
 * Modified by: Mark Crichton <crichton@gmail.com> 0xAFEEFE80
 * performance of intrinsics is poor on gcc 4.4, generic C is better
 */

// Copyright (c) 2010 Satoshi Nakamoto
// Distributed under the MIT/X11 software license, see the accompanying
// file license.txt or http://www.opensource.org/licenses/mit-license.php.

// tcatm's 4-way 128-bit SSE2 SHA-256

#include "miner.h"

#ifdef WANT_NEON

#include <string.h>
#include <assert.h>

#include <stdint.h>
#include <stdio.h>

#include <arm_neon.h>

#define NPAR 32

static void DoubleBlockSHA256(const void *pin, void *pout,
			      const void *pinit, uint32_t hash[9][NPAR],
			      const void *init2);

static const unsigned int sha256_consts[] = {
	0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5,	/*  0 */
	0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
	0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,	/*  8 */
	0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
	0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc,	/* 16 */
	0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
	0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7,	/* 24 */
	0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
	0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,	/* 32 */
	0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
	0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3,	/* 40 */
	0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
	0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5,	/* 48 */
	0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
	0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,	/* 56 */
	0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
};


static inline uint32x2_t Ch(uint32x2_t b, uint32x2_t c, uint32x2_t d)
{
	/*return (b & c) ^ (~b & d);*/
	return vbsl_u32(b, c, d);
}

static inline uint32x2_t Maj(uint32x2_t b, uint32x2_t c, uint32x2_t d)
{
	return (b & c) ^ (b & d) ^ (c & d);
}

static __attribute__ ((always_inline))
uint32x2_t ROTR(uint32x2_t x, const int n)
{
	return vshr_n_u32(x, n) | vshl_n_u32(x, (32 - n));
}

static __attribute__ ((always_inline))
uint32x2_t SHR(uint32x2_t x, const int n)
{
	return vshr_n_u32(x, n);
}

/* SHA256 Functions */
#define BIGSIGMA0_256(x)    (ROTR((x),  2) ^ ROTR((x), 13) ^ ROTR((x), 22))
#define BIGSIGMA1_256(x)    (ROTR((x),  6) ^ ROTR((x), 11) ^ ROTR((x), 25))
#define SIGMA0_256(x)       (ROTR((x),  7) ^ ROTR((x), 18) ^  SHR((x),  3))
#define SIGMA1_256(x)       (ROTR((x), 17) ^ ROTR((x), 19) ^  SHR((x), 10))

#define add4(x0, x1, x2, x3) (vadd_u32(vadd_u32(x0, x1), vadd_u32(x2, x3)))
#define add5(x0, x1, x2, x3, x4) (vadd_u32(add4(x0, x1, x2, x3), x4))

#define SHA256ROUND(a, b, c, d, e, f, g, h, i, w)                       \
    T1 = add5(h, BIGSIGMA1_256(e), Ch(e, f, g), vdup_n_u32(sha256_consts[i]), w);   \
d = vadd_u32(d, T1);                                           \
h = vadd_u32(T1, vadd_u32(BIGSIGMA0_256(a), Maj(a, b, c)));

static const unsigned int pSHA256InitState[8] =
    { 0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f,
0x9b05688c, 0x1f83d9ab, 0x5be0cd19 };


unsigned int ScanHash_NEON(const unsigned char *pmidstate,
			   unsigned char *pdata, unsigned char *phash1,
			   unsigned char *phash,
			   const unsigned char *ptarget,
			   uint32_t max_nonce, unsigned long *nHashesDone)
{
	unsigned int *nNonce_p = (unsigned int *) (pdata + 12);
	unsigned int nonce = 0;

	for (;;) {
		uint32_t thash[9][NPAR] __attribute__ ((aligned(128)));
		int j, i;

		*nNonce_p = nonce;

		DoubleBlockSHA256(pdata, phash1, pmidstate, thash,
				  pSHA256InitState);

		for (j = 0; j < NPAR; j++) {
			if (unlikely(thash[7][j] == 0)) {
				int i;

				for (i = 0; i < 32 / 4; i++) {
					((unsigned int *) phash)[i] =
					    thash[i][j];
				}

				if (fulltest(phash, ptarget)) {
					*nHashesDone = nonce;
					*nNonce_p = nonce + j;
					return nonce + j;
				}
			}
		}

		if (unlikely(opt_validate)) {
			int i;
			for (i = 0; i < 32 / 4; i++) {
				((unsigned int *) phash)[i] = thash[i][0];
			}
			*nHashesDone = nonce;
			return 0;
		}

		nonce += NPAR;

		if (nonce >= max_nonce) {
			*nHashesDone = nonce;
			return -1;
		}
	}
}


static void DoubleBlockSHA256(const void *pin, void *pad, const void *pre,
			      uint32_t thash[9][NPAR], const void *init)
{
	unsigned int *In = (unsigned int *) pin;
	unsigned int *Pad = (unsigned int *) pad;
	unsigned int *hPre = (unsigned int *) pre;
	unsigned int *hInit = (unsigned int *) init;
	unsigned int /* i, j, */ k;

	/* vectors used in calculation */
	uint32x2_t w0, w1, w2, w3, w4, w5, w6, w7;
	uint32x2_t w8, w9, w10, w11, w12, w13, w14, w15;
	uint32x2_t T1;
	uint32x2_t a, b, c, d, e, f, g, h;
	uint32x2_t nonce, preNonce;

	/* nonce offset for vector */
	uint32x2_t offset = { 0x0, 0x1 };


	preNonce = vadd_u32(vdup_n_u32(In[3]), offset);

	for (k = 0; k < NPAR; k += 2) {
		w0 = vdup_n_u32(In[0]);
		w1 = vdup_n_u32(In[1]);
		w2 = vdup_n_u32(In[2]);
		//w3 - nonce will be later hacked into the hash
		w4 = vdup_n_u32(In[4]);
		w5 = vdup_n_u32(In[5]);
		w6 = vdup_n_u32(In[6]);
		w7 = vdup_n_u32(In[7]);
		w8 = vdup_n_u32(In[8]);
		w9 = vdup_n_u32(In[9]);
		w10 = vdup_n_u32(In[10]);
		w11 = vdup_n_u32(In[11]);
		w12 = vdup_n_u32(In[12]);
		w13 = vdup_n_u32(In[13]);
		w14 = vdup_n_u32(In[14]);
		w15 = vdup_n_u32(In[15]);

		/* hack nonce into lowest byte of w3 */
		nonce = vadd_u32(preNonce, vdup_n_u32(k));
		w3 = nonce;

		a = vdup_n_u32(hPre[0]);
		b = vdup_n_u32(hPre[1]);
		c = vdup_n_u32(hPre[2]);
		d = vdup_n_u32(hPre[3]);
		e = vdup_n_u32(hPre[4]);
		f = vdup_n_u32(hPre[5]);
		g = vdup_n_u32(hPre[6]);
		h = vdup_n_u32(hPre[7]);

		/* The fun begins here */
		SHA256ROUND(a, b, c, d, e, f, g, h, 0, w0);
		SHA256ROUND(h, a, b, c, d, e, f, g, 1, w1);
		SHA256ROUND(g, h, a, b, c, d, e, f, 2, w2);
		SHA256ROUND(f, g, h, a, b, c, d, e, 3, w3);
		SHA256ROUND(e, f, g, h, a, b, c, d, 4, w4);
		SHA256ROUND(d, e, f, g, h, a, b, c, 5, w5);
		SHA256ROUND(c, d, e, f, g, h, a, b, 6, w6);
		SHA256ROUND(b, c, d, e, f, g, h, a, 7, w7);
		SHA256ROUND(a, b, c, d, e, f, g, h, 8, w8);
		SHA256ROUND(h, a, b, c, d, e, f, g, 9, w9);
		SHA256ROUND(g, h, a, b, c, d, e, f, 10, w10);
		SHA256ROUND(f, g, h, a, b, c, d, e, 11, w11);
		SHA256ROUND(e, f, g, h, a, b, c, d, 12, w12);
		SHA256ROUND(d, e, f, g, h, a, b, c, 13, w13);
		SHA256ROUND(c, d, e, f, g, h, a, b, 14, w14);
		SHA256ROUND(b, c, d, e, f, g, h, a, 15, w15);

		w0 = add4(SIGMA1_256(w14), w9, SIGMA0_256(w1), w0);
		SHA256ROUND(a, b, c, d, e, f, g, h, 16, w0);
		w1 = add4(SIGMA1_256(w15), w10, SIGMA0_256(w2), w1);
		SHA256ROUND(h, a, b, c, d, e, f, g, 17, w1);
		w2 = add4(SIGMA1_256(w0), w11, SIGMA0_256(w3), w2);
		SHA256ROUND(g, h, a, b, c, d, e, f, 18, w2);
		w3 = add4(SIGMA1_256(w1), w12, SIGMA0_256(w4), w3);
		SHA256ROUND(f, g, h, a, b, c, d, e, 19, w3);
		w4 = add4(SIGMA1_256(w2), w13, SIGMA0_256(w5), w4);
		SHA256ROUND(e, f, g, h, a, b, c, d, 20, w4);
		w5 = add4(SIGMA1_256(w3), w14, SIGMA0_256(w6), w5);
		SHA256ROUND(d, e, f, g, h, a, b, c, 21, w5);
		w6 = add4(SIGMA1_256(w4), w15, SIGMA0_256(w7), w6);
		SHA256ROUND(c, d, e, f, g, h, a, b, 22, w6);
		w7 = add4(SIGMA1_256(w5), w0, SIGMA0_256(w8), w7);
		SHA256ROUND(b, c, d, e, f, g, h, a, 23, w7);
		w8 = add4(SIGMA1_256(w6), w1, SIGMA0_256(w9), w8);
		SHA256ROUND(a, b, c, d, e, f, g, h, 24, w8);
		w9 = add4(SIGMA1_256(w7), w2, SIGMA0_256(w10), w9);
		SHA256ROUND(h, a, b, c, d, e, f, g, 25, w9);
		w10 = add4(SIGMA1_256(w8), w3, SIGMA0_256(w11), w10);
		SHA256ROUND(g, h, a, b, c, d, e, f, 26, w10);
		w11 = add4(SIGMA1_256(w9), w4, SIGMA0_256(w12), w11);
		SHA256ROUND(f, g, h, a, b, c, d, e, 27, w11);
		w12 = add4(SIGMA1_256(w10), w5, SIGMA0_256(w13), w12);
		SHA256ROUND(e, f, g, h, a, b, c, d, 28, w12);
		w13 = add4(SIGMA1_256(w11), w6, SIGMA0_256(w14), w13);
		SHA256ROUND(d, e, f, g, h, a, b, c, 29, w13);
		w14 = add4(SIGMA1_256(w12), w7, SIGMA0_256(w15), w14);
		SHA256ROUND(c, d, e, f, g, h, a, b, 30, w14);
		w15 = add4(SIGMA1_256(w13), w8, SIGMA0_256(w0), w15);
		SHA256ROUND(b, c, d, e, f, g, h, a, 31, w15);

		w0 = add4(SIGMA1_256(w14), w9, SIGMA0_256(w1), w0);
		SHA256ROUND(a, b, c, d, e, f, g, h, 32, w0);
		w1 = add4(SIGMA1_256(w15), w10, SIGMA0_256(w2), w1);
		SHA256ROUND(h, a, b, c, d, e, f, g, 33, w1);
		w2 = add4(SIGMA1_256(w0), w11, SIGMA0_256(w3), w2);
		SHA256ROUND(g, h, a, b, c, d, e, f, 34, w2);
		w3 = add4(SIGMA1_256(w1), w12, SIGMA0_256(w4), w3);
		SHA256ROUND(f, g, h, a, b, c, d, e, 35, w3);
		w4 = add4(SIGMA1_256(w2), w13, SIGMA0_256(w5), w4);
		SHA256ROUND(e, f, g, h, a, b, c, d, 36, w4);
		w5 = add4(SIGMA1_256(w3), w14, SIGMA0_256(w6), w5);
		SHA256ROUND(d, e, f, g, h, a, b, c, 37, w5);
		w6 = add4(SIGMA1_256(w4), w15, SIGMA0_256(w7), w6);
		SHA256ROUND(c, d, e, f, g, h, a, b, 38, w6);
		w7 = add4(SIGMA1_256(w5), w0, SIGMA0_256(w8), w7);
		SHA256ROUND(b, c, d, e, f, g, h, a, 39, w7);
		w8 = add4(SIGMA1_256(w6), w1, SIGMA0_256(w9), w8);
		SHA256ROUND(a, b, c, d, e, f, g, h, 40, w8);
		w9 = add4(SIGMA1_256(w7), w2, SIGMA0_256(w10), w9);
		SHA256ROUND(h, a, b, c, d, e, f, g, 41, w9);
		w10 = add4(SIGMA1_256(w8), w3, SIGMA0_256(w11), w10);
		SHA256ROUND(g, h, a, b, c, d, e, f, 42, w10);
		w11 = add4(SIGMA1_256(w9), w4, SIGMA0_256(w12), w11);
		SHA256ROUND(f, g, h, a, b, c, d, e, 43, w11);
		w12 = add4(SIGMA1_256(w10), w5, SIGMA0_256(w13), w12);
		SHA256ROUND(e, f, g, h, a, b, c, d, 44, w12);
		w13 = add4(SIGMA1_256(w11), w6, SIGMA0_256(w14), w13);
		SHA256ROUND(d, e, f, g, h, a, b, c, 45, w13);
		w14 = add4(SIGMA1_256(w12), w7, SIGMA0_256(w15), w14);
		SHA256ROUND(c, d, e, f, g, h, a, b, 46, w14);
		w15 = add4(SIGMA1_256(w13), w8, SIGMA0_256(w0), w15);
		SHA256ROUND(b, c, d, e, f, g, h, a, 47, w15);

		w0 = add4(SIGMA1_256(w14), w9, SIGMA0_256(w1), w0);
		SHA256ROUND(a, b, c, d, e, f, g, h, 48, w0);
		w1 = add4(SIGMA1_256(w15), w10, SIGMA0_256(w2), w1);
		SHA256ROUND(h, a, b, c, d, e, f, g, 49, w1);
		w2 = add4(SIGMA1_256(w0), w11, SIGMA0_256(w3), w2);
		SHA256ROUND(g, h, a, b, c, d, e, f, 50, w2);
		w3 = add4(SIGMA1_256(w1), w12, SIGMA0_256(w4), w3);
		SHA256ROUND(f, g, h, a, b, c, d, e, 51, w3);
		w4 = add4(SIGMA1_256(w2), w13, SIGMA0_256(w5), w4);
		SHA256ROUND(e, f, g, h, a, b, c, d, 52, w4);
		w5 = add4(SIGMA1_256(w3), w14, SIGMA0_256(w6), w5);
		SHA256ROUND(d, e, f, g, h, a, b, c, 53, w5);
		w6 = add4(SIGMA1_256(w4), w15, SIGMA0_256(w7), w6);
		SHA256ROUND(c, d, e, f, g, h, a, b, 54, w6);
		w7 = add4(SIGMA1_256(w5), w0, SIGMA0_256(w8), w7);
		SHA256ROUND(b, c, d, e, f, g, h, a, 55, w7);
		w8 = add4(SIGMA1_256(w6), w1, SIGMA0_256(w9), w8);
		SHA256ROUND(a, b, c, d, e, f, g, h, 56, w8);
		w9 = add4(SIGMA1_256(w7), w2, SIGMA0_256(w10), w9);
		SHA256ROUND(h, a, b, c, d, e, f, g, 57, w9);
		w10 = add4(SIGMA1_256(w8), w3, SIGMA0_256(w11), w10);
		SHA256ROUND(g, h, a, b, c, d, e, f, 58, w10);
		w11 = add4(SIGMA1_256(w9), w4, SIGMA0_256(w12), w11);
		SHA256ROUND(f, g, h, a, b, c, d, e, 59, w11);
		w12 = add4(SIGMA1_256(w10), w5, SIGMA0_256(w13), w12);
		SHA256ROUND(e, f, g, h, a, b, c, d, 60, w12);
		w13 = add4(SIGMA1_256(w11), w6, SIGMA0_256(w14), w13);
		SHA256ROUND(d, e, f, g, h, a, b, c, 61, w13);
		w14 = add4(SIGMA1_256(w12), w7, SIGMA0_256(w15), w14);
		SHA256ROUND(c, d, e, f, g, h, a, b, 62, w14);
		w15 = add4(SIGMA1_256(w13), w8, SIGMA0_256(w0), w15);
		SHA256ROUND(b, c, d, e, f, g, h, a, 63, w15);

#define store_load(x, i, dest) \
        T1 = vdup_n_u32((hPre)[i]); \
        dest = vadd_u32(T1, x);

		store_load(a, 0, w0);
		store_load(b, 1, w1);
		store_load(c, 2, w2);
		store_load(d, 3, w3);
		store_load(e, 4, w4);
		store_load(f, 5, w5);
		store_load(g, 6, w6);
		store_load(h, 7, w7);

		w8 = vdup_n_u32(Pad[8]);
		w9 = vdup_n_u32(Pad[9]);
		w10 = vdup_n_u32(Pad[10]);
		w11 = vdup_n_u32(Pad[11]);
		w12 = vdup_n_u32(Pad[12]);
		w13 = vdup_n_u32(Pad[13]);
		w14 = vdup_n_u32(Pad[14]);
		w15 = vdup_n_u32(Pad[15]);

		a = vdup_n_u32(hInit[0]);
		b = vdup_n_u32(hInit[1]);
		c = vdup_n_u32(hInit[2]);
		d = vdup_n_u32(hInit[3]);
		e = vdup_n_u32(hInit[4]);
		f = vdup_n_u32(hInit[5]);
		g = vdup_n_u32(hInit[6]);
		h = vdup_n_u32(hInit[7]);

		SHA256ROUND(a, b, c, d, e, f, g, h, 0, w0);
		SHA256ROUND(h, a, b, c, d, e, f, g, 1, w1);
		SHA256ROUND(g, h, a, b, c, d, e, f, 2, w2);
		SHA256ROUND(f, g, h, a, b, c, d, e, 3, w3);
		SHA256ROUND(e, f, g, h, a, b, c, d, 4, w4);
		SHA256ROUND(d, e, f, g, h, a, b, c, 5, w5);
		SHA256ROUND(c, d, e, f, g, h, a, b, 6, w6);
		SHA256ROUND(b, c, d, e, f, g, h, a, 7, w7);
		SHA256ROUND(a, b, c, d, e, f, g, h, 8, w8);
		SHA256ROUND(h, a, b, c, d, e, f, g, 9, w9);
		SHA256ROUND(g, h, a, b, c, d, e, f, 10, w10);
		SHA256ROUND(f, g, h, a, b, c, d, e, 11, w11);
		SHA256ROUND(e, f, g, h, a, b, c, d, 12, w12);
		SHA256ROUND(d, e, f, g, h, a, b, c, 13, w13);
		SHA256ROUND(c, d, e, f, g, h, a, b, 14, w14);
		SHA256ROUND(b, c, d, e, f, g, h, a, 15, w15);

		w0 = add4(SIGMA1_256(w14), w9, SIGMA0_256(w1), w0);
		SHA256ROUND(a, b, c, d, e, f, g, h, 16, w0);
		w1 = add4(SIGMA1_256(w15), w10, SIGMA0_256(w2), w1);
		SHA256ROUND(h, a, b, c, d, e, f, g, 17, w1);
		w2 = add4(SIGMA1_256(w0), w11, SIGMA0_256(w3), w2);
		SHA256ROUND(g, h, a, b, c, d, e, f, 18, w2);
		w3 = add4(SIGMA1_256(w1), w12, SIGMA0_256(w4), w3);
		SHA256ROUND(f, g, h, a, b, c, d, e, 19, w3);
		w4 = add4(SIGMA1_256(w2), w13, SIGMA0_256(w5), w4);
		SHA256ROUND(e, f, g, h, a, b, c, d, 20, w4);
		w5 = add4(SIGMA1_256(w3), w14, SIGMA0_256(w6), w5);
		SHA256ROUND(d, e, f, g, h, a, b, c, 21, w5);
		w6 = add4(SIGMA1_256(w4), w15, SIGMA0_256(w7), w6);
		SHA256ROUND(c, d, e, f, g, h, a, b, 22, w6);
		w7 = add4(SIGMA1_256(w5), w0, SIGMA0_256(w8), w7);
		SHA256ROUND(b, c, d, e, f, g, h, a, 23, w7);
		w8 = add4(SIGMA1_256(w6), w1, SIGMA0_256(w9), w8);
		SHA256ROUND(a, b, c, d, e, f, g, h, 24, w8);
		w9 = add4(SIGMA1_256(w7), w2, SIGMA0_256(w10), w9);
		SHA256ROUND(h, a, b, c, d, e, f, g, 25, w9);
		w10 = add4(SIGMA1_256(w8), w3, SIGMA0_256(w11), w10);
		SHA256ROUND(g, h, a, b, c, d, e, f, 26, w10);
		w11 = add4(SIGMA1_256(w9), w4, SIGMA0_256(w12), w11);
		SHA256ROUND(f, g, h, a, b, c, d, e, 27, w11);
		w12 = add4(SIGMA1_256(w10), w5, SIGMA0_256(w13), w12);
		SHA256ROUND(e, f, g, h, a, b, c, d, 28, w12);
		w13 = add4(SIGMA1_256(w11), w6, SIGMA0_256(w14), w13);
		SHA256ROUND(d, e, f, g, h, a, b, c, 29, w13);
		w14 = add4(SIGMA1_256(w12), w7, SIGMA0_256(w15), w14);
		SHA256ROUND(c, d, e, f, g, h, a, b, 30, w14);
		w15 = add4(SIGMA1_256(w13), w8, SIGMA0_256(w0), w15);
		SHA256ROUND(b, c, d, e, f, g, h, a, 31, w15);

		w0 = add4(SIGMA1_256(w14), w9, SIGMA0_256(w1), w0);
		SHA256ROUND(a, b, c, d, e, f, g, h, 32, w0);
		w1 = add4(SIGMA1_256(w15), w10, SIGMA0_256(w2), w1);
		SHA256ROUND(h, a, b, c, d, e, f, g, 33, w1);
		w2 = add4(SIGMA1_256(w0), w11, SIGMA0_256(w3), w2);
		SHA256ROUND(g, h, a, b, c, d, e, f, 34, w2);
		w3 = add4(SIGMA1_256(w1), w12, SIGMA0_256(w4), w3);
		SHA256ROUND(f, g, h, a, b, c, d, e, 35, w3);
		w4 = add4(SIGMA1_256(w2), w13, SIGMA0_256(w5), w4);
		SHA256ROUND(e, f, g, h, a, b, c, d, 36, w4);
		w5 = add4(SIGMA1_256(w3), w14, SIGMA0_256(w6), w5);
		SHA256ROUND(d, e, f, g, h, a, b, c, 37, w5);
		w6 = add4(SIGMA1_256(w4), w15, SIGMA0_256(w7), w6);
		SHA256ROUND(c, d, e, f, g, h, a, b, 38, w6);
		w7 = add4(SIGMA1_256(w5), w0, SIGMA0_256(w8), w7);
		SHA256ROUND(b, c, d, e, f, g, h, a, 39, w7);
		w8 = add4(SIGMA1_256(w6), w1, SIGMA0_256(w9), w8);
		SHA256ROUND(a, b, c, d, e, f, g, h, 40, w8);
		w9 = add4(SIGMA1_256(w7), w2, SIGMA0_256(w10), w9);
		SHA256ROUND(h, a, b, c, d, e, f, g, 41, w9);
		w10 = add4(SIGMA1_256(w8), w3, SIGMA0_256(w11), w10);
		SHA256ROUND(g, h, a, b, c, d, e, f, 42, w10);
		w11 = add4(SIGMA1_256(w9), w4, SIGMA0_256(w12), w11);
		SHA256ROUND(f, g, h, a, b, c, d, e, 43, w11);
		w12 = add4(SIGMA1_256(w10), w5, SIGMA0_256(w13), w12);
		SHA256ROUND(e, f, g, h, a, b, c, d, 44, w12);
		w13 = add4(SIGMA1_256(w11), w6, SIGMA0_256(w14), w13);
		SHA256ROUND(d, e, f, g, h, a, b, c, 45, w13);
		w14 = add4(SIGMA1_256(w12), w7, SIGMA0_256(w15), w14);
		SHA256ROUND(c, d, e, f, g, h, a, b, 46, w14);
		w15 = add4(SIGMA1_256(w13), w8, SIGMA0_256(w0), w15);
		SHA256ROUND(b, c, d, e, f, g, h, a, 47, w15);

		w0 = add4(SIGMA1_256(w14), w9, SIGMA0_256(w1), w0);
		SHA256ROUND(a, b, c, d, e, f, g, h, 48, w0);
		w1 = add4(SIGMA1_256(w15), w10, SIGMA0_256(w2), w1);
		SHA256ROUND(h, a, b, c, d, e, f, g, 49, w1);
		w2 = add4(SIGMA1_256(w0), w11, SIGMA0_256(w3), w2);
		SHA256ROUND(g, h, a, b, c, d, e, f, 50, w2);
		w3 = add4(SIGMA1_256(w1), w12, SIGMA0_256(w4), w3);
		SHA256ROUND(f, g, h, a, b, c, d, e, 51, w3);
		w4 = add4(SIGMA1_256(w2), w13, SIGMA0_256(w5), w4);
		SHA256ROUND(e, f, g, h, a, b, c, d, 52, w4);
		w5 = add4(SIGMA1_256(w3), w14, SIGMA0_256(w6), w5);
		SHA256ROUND(d, e, f, g, h, a, b, c, 53, w5);
		w6 = add4(SIGMA1_256(w4), w15, SIGMA0_256(w7), w6);
		SHA256ROUND(c, d, e, f, g, h, a, b, 54, w6);
		w7 = add4(SIGMA1_256(w5), w0, SIGMA0_256(w8), w7);
		SHA256ROUND(b, c, d, e, f, g, h, a, 55, w7);
		w8 = add4(SIGMA1_256(w6), w1, SIGMA0_256(w9), w8);
		SHA256ROUND(a, b, c, d, e, f, g, h, 56, w8);
		w9 = add4(SIGMA1_256(w7), w2, SIGMA0_256(w10), w9);
		SHA256ROUND(h, a, b, c, d, e, f, g, 57, w9);
		w10 = add4(SIGMA1_256(w8), w3, SIGMA0_256(w11), w10);
		SHA256ROUND(g, h, a, b, c, d, e, f, 58, w10);
		w11 = add4(SIGMA1_256(w9), w4, SIGMA0_256(w12), w11);
		SHA256ROUND(f, g, h, a, b, c, d, e, 59, w11);
		w12 = add4(SIGMA1_256(w10), w5, SIGMA0_256(w13), w12);
		SHA256ROUND(e, f, g, h, a, b, c, d, 60, w12);
		w13 = add4(SIGMA1_256(w11), w6, SIGMA0_256(w14), w13);
		SHA256ROUND(d, e, f, g, h, a, b, c, 61, w13);
		w14 = add4(SIGMA1_256(w12), w7, SIGMA0_256(w15), w14);
		SHA256ROUND(c, d, e, f, g, h, a, b, 62, w14);
		w15 = add4(SIGMA1_256(w13), w8, SIGMA0_256(w0), w15);
		SHA256ROUND(b, c, d, e, f, g, h, a, 63, w15);

		/* store resulsts directly in thash */
#define store_2(x,i)  \
        w0 = vdup_n_u32(hInit[i]); \
        vst1_u32(&thash[i][0+k], vadd_u32(w0, x));

		store_2(a, 0);
		store_2(b, 1);
		store_2(c, 2);
		store_2(d, 3);
		store_2(e, 4);
		store_2(f, 5);
		store_2(g, 6);
		store_2(h, 7);
		vst1_u32(&thash[8][0 + k], nonce);
	}
}

#endif				/* WANT_NEON */
