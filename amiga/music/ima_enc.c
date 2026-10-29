/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 angree. See LICENSE at the repository root.
 *
 * IMA ADPCM WAV writer -- see ima_enc.h. Each 1024-byte block starts with the
 * predictor and step index of its first sample (4 bytes), then 2040 more
 * samples as nibbles, low nibble first, exactly what the decoder expects.
 * The encoder picks each nibble by the standard IMA rule (no look-ahead):
 * decoding then reproduces the encoder's own predictor bit for bit.
 */
#include <stdlib.h>
#include <string.h>

#include "ima_enc.h"

#define BLOCK_BYTES   1024
#define BLOCK_SAMPLES 2041

static const int idx_tab[16] = { -1, -1, -1, -1, 2, 4, 6, 8, -1, -1, -1, -1, 2, 4, 6, 8 };
static const int step_tab[89] = {
	7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37,
	41, 45, 50, 55, 60, 66, 73, 80, 88, 97, 107, 118, 130, 143, 157, 173,
	190, 209, 230, 253, 279, 307, 337, 371, 408, 449, 494, 544, 598, 658,
	724, 796, 876, 963, 1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066,
	2272, 2499, 2749, 3024, 3327, 3660, 4026, 4428, 4871, 5358, 5894, 6484,
	7132, 7845, 8630, 9493, 10442, 11487, 12635, 13899, 15289, 16818,
	18500, 20350, 22385, 24623, 27086, 29794, 32767
};

struct ImaWriter {
	FILE *f;
	int   rate;
	long  pred, index;			/* encoder state, carried across blocks */
	short buf[BLOCK_SAMPLES];
	int   nbuf;
	unsigned long data_bytes;
};

static void put32 (unsigned char *p, unsigned long v) { p[0] = v; p[1] = v >> 8; p[2] = v >> 16; p[3] = v >> 24; }
static void put16 (unsigned char *p, unsigned v) { p[0] = v; p[1] = v >> 8; }

static void header (ImaWriter *w)
{
	unsigned char h[60];
	unsigned long blocks = w->data_bytes / BLOCK_BYTES;

	memcpy (h, "RIFF", 4); put32 (h + 4, 52 + w->data_bytes);
	memcpy (h + 8, "WAVEfmt ", 8); put32 (h + 16, 20);
	put16 (h + 20, 0x11); put16 (h + 22, 1); put32 (h + 24, w->rate);
	put32 (h + 28, (unsigned long)w->rate * BLOCK_BYTES / BLOCK_SAMPLES);
	put16 (h + 32, BLOCK_BYTES); put16 (h + 34, 4); put16 (h + 36, 2); put16 (h + 38, BLOCK_SAMPLES);
	memcpy (h + 40, "fact", 4); put32 (h + 44, 4); put32 (h + 48, blocks * BLOCK_SAMPLES);
	memcpy (h + 52, "data", 4); put32 (h + 56, w->data_bytes);
	fseek (w->f, 0, SEEK_SET);
	fwrite (h, 1, sizeof h, w->f);
	fseek (w->f, 0, SEEK_END);
}

static int nibble (ImaWriter *w, int s)
{
	long step = step_tab[w->index], diff = s - w->pred, dq = step >> 3;
	int code = 0;

	if (diff < 0) { code = 8; diff = -diff; }
	if (diff >= step) { code |= 4; diff -= step; dq += step; }
	step >>= 1;
	if (diff >= step) { code |= 2; diff -= step; dq += step; }
	step >>= 1;
	if (diff >= step) { code |= 1; dq += step; }
	w->pred += (code & 8) ? -dq : dq;
	if (w->pred > 32767) w->pred = 32767;
	if (w->pred < -32768) w->pred = -32768;
	w->index += idx_tab[code];
	if (w->index < 0) w->index = 0;
	if (w->index > 88) w->index = 88;
	return code;
}

static void flush_block (ImaWriter *w)
{
	unsigned char b[BLOCK_BYTES];
	int i;

	while (w->nbuf < BLOCK_SAMPLES) w->buf[w->nbuf++] = 0;	/* pad the last block with silence */
	w->pred = w->buf[0];
	put16 (b, (unsigned)(w->pred & 0xffff)); b[2] = (unsigned char)w->index; b[3] = 0;
	for (i = 0; i < BLOCK_BYTES - 4; i++) {
		int lo = nibble (w, w->buf[1 + 2 * i]);
		int hi = nibble (w, w->buf[2 + 2 * i]);
		b[4 + i] = (unsigned char)(lo | (hi << 4));
	}
	fwrite (b, 1, BLOCK_BYTES, w->f);
	w->data_bytes += BLOCK_BYTES;
	w->nbuf = 0;
}

ImaWriter *ima_open (const char *path, int rate)
{
	ImaWriter *w = (ImaWriter *)calloc (1, sizeof *w);

	if (w == NULL) return NULL;
	w->f = fopen (path, "wb");
	if (w->f == NULL) { free (w); return NULL; }
	w->rate = rate;
	header (w);
	return w;
}

void ima_write (ImaWriter *w, const short *pcm, long n)
{
	while (n-- > 0) {
		w->buf[w->nbuf++] = *pcm++;
		if (w->nbuf == BLOCK_SAMPLES) flush_block (w);
	}
}

int ima_close (ImaWriter *w)
{
	int bad;

	if (w->nbuf) flush_block (w);
	header (w);
	bad = ferror (w->f);
	fclose (w->f);
	free (w);
	return bad;
}
