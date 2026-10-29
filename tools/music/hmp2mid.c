/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 angree. See LICENSE at the repository root.
 *
 * hmp2mid IN.HMP OUT.MID -- write a song as a standard MIDI file (format 1),
 * through the same parser the Amiga build plays it with (amiga/music/hmp.c),
 * for listening and for inspection in any MIDI tool.
 *
 * Every track chunk of the HMP becomes one MIDI track, the empty first one
 * included, so track numbers match hmptest's. Track 0 also gets the tempo:
 * the header's tick is 1/rate s, written as division = ticks per quarter
 * (0x34) and tempo = 1000000 * division / rate us per quarter -- 96 and
 * 800000 for every shipped song, i.e. 120 ticks a second exactly. (If that
 * does not divide, division = rate and tempo = 1 s.) Events are written with
 * full status bytes; nothing is dropped, aftertouch and metas included.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "hmp.h"

typedef struct Buf {
	unsigned char *p;
	long n, cap;
	long last;		/* tick of the last event written */
	int ended;
} Buf;

static void put(Buf *b, int c)
{
	if (b->n == b->cap) {
		b->cap = b->cap ? b->cap * 2 : 4096;
		b->p = (unsigned char *)realloc(b->p, b->cap);
		if (!b->p) { fprintf(stderr, "out of memory\n"); exit(2); }
	}
	b->p[b->n++] = (unsigned char)c;
}

static void put_vlq(Buf *b, unsigned long v)
{
	unsigned char t[5];
	int n = 0;

	t[n++] = v & 0x7f;
	while ((v >>= 7) && n < 5)
		t[n++] = (v & 0x7f) | 0x80;
	while (n)
		put(b, t[--n]);
}

static void put_delta(Buf *b, long tick)
{
	put_vlq(b, (unsigned long)(tick - b->last));
	b->last = tick;
}

static void put32(FILE *f, unsigned long v)
{
	fputc((int)(v >> 24) & 0xff, f); fputc((int)(v >> 16) & 0xff, f);
	fputc((int)(v >> 8) & 0xff, f); fputc((int)v & 0xff, f);
}

int main(int argc, char **argv)
{
	HmpSong *s;
	HmpEvent ev;
	Buf *tr;
	FILE *f;
	int i, nt, div, rate;
	unsigned long tempo;

	if (argc != 3) {
		fprintf(stderr, "usage: hmp2mid IN.HMP OUT.MID\n");
		return 1;
	}
	s = hmp_load_file(argv[1]);
	if (!s) {
		fprintf(stderr, "%s: not an HMP song\n", argv[1]);
		return 1;
	}
	nt = hmp_tracks(s);
	div = hmp_division(s);
	rate = hmp_tick_rate(s);
	if ((1000000UL * (unsigned long)div) % (unsigned long)rate == 0 &&
	    1000000UL * (unsigned long)div / (unsigned long)rate <= 0xffffffUL) {
		tempo = 1000000UL * (unsigned long)div / (unsigned long)rate;
	} else {
		div = rate;
		tempo = 1000000UL;
	}
	tr = (Buf *)calloc((size_t)nt, sizeof(Buf));

	put(&tr[0], 0);
	put(&tr[0], 0xff); put(&tr[0], 0x51); put(&tr[0], 3);
	put(&tr[0], (int)(tempo >> 16) & 0xff);
	put(&tr[0], (int)(tempo >> 8) & 0xff);
	put(&tr[0], (int)tempo & 0xff);

	hmp_rewind(s);
	while (hmp_next_event(s, &ev)) {
		Buf *b = &tr[ev.track];
		if (b->ended)
			continue;
		put_delta(b, ev.tick);
		put(b, ev.status);
		if (ev.status < 0xf0) {
			put(b, ev.d1);
			if (ev.len == 2)
				put(b, ev.d2);
		} else {
			if (ev.status == 0xff)
				put(b, ev.d1);
			put_vlq(b, (unsigned long)ev.len);
			for (i = 0; i < ev.len; i++)
				put(b, ev.data[i]);
			if (ev.status == 0xff && ev.d1 == 0x2f)
				b->ended = 1;
		}
	}
	for (i = 0; i < nt; i++)
		if (!tr[i].ended) {
			put_delta(&tr[i], tr[i].last);
			put(&tr[i], 0xff); put(&tr[i], 0x2f); put(&tr[i], 0);
		}

	f = fopen(argv[2], "wb");
	if (!f) {
		fprintf(stderr, "%s: cannot write\n", argv[2]);
		return 1;
	}
	fwrite("MThd", 1, 4, f);
	put32(f, 6);
	fputc(0, f); fputc(1, f);
	fputc((nt >> 8) & 0xff, f); fputc(nt & 0xff, f);
	fputc((div >> 8) & 0xff, f); fputc(div & 0xff, f);
	for (i = 0; i < nt; i++) {
		fwrite("MTrk", 1, 4, f);
		put32(f, (unsigned long)tr[i].n);
		fwrite(tr[i].p, 1, (size_t)tr[i].n, f);
		free(tr[i].p);
	}
	fclose(f);
	printf("%s -> %s: %d tracks, division %d, tempo %lu us/quarter, %ld.%03ld s\n",
	       argv[1], argv[2], nt, div, tempo, hmp_length_usec(s) / 1000000,
	       (hmp_length_usec(s) / 1000) % 1000);
	free(tr);
	hmp_free(s);
	return 0;
}
