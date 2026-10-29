/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 angree. See LICENSE at the repository root.
 *
 * gmtest: host test bench for amiga/music/gm_synth.c and the bank.
 *
 *   gmtest <bank.bnk> <outdir> [rate]
 *
 * Writes four WAV files for listening (mono, 16-bit, default 22050 Hz):
 *   gm_allprogs.wav  all 128 GM programs in order, each an arpeggio + chord
 *   gm_scales.wav    one program per GM family, two octaves of C major
 *   gm_drums.wav     a rock pattern with fills, then every mapped drum key
 *   gm_chords.wav    a small arrangement: piano with pedal, strings, bass,
 *                    drums, a bent sax line (polyphony and level check)
 * then checks determinism (the arrangement rendered in three different
 * chunkings must be bit-identical) and benchmarks the render loop at 8, 16
 * and 24 voices with interpolation on and off.
 *
 * This is a HOST tool: it may use double and stdio. gm_synth.c may not.
 */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "gm_synth.h"

typedef struct {
	long t;			/* sample time */
	int seq;		/* insertion order, keeps sorting stable */
	unsigned char type, ch, a, b;
	int v;
} Ev;

enum { E_ON, E_OFF, E_PROG, E_CC, E_BEND };

static Ev *evs;
static int nev, cap;
static int rate = 22050;

static void ev(long t, int type, int ch, int a, int b)
{
	if (nev == cap) {
		cap = cap ? cap * 2 : 1024;
		evs = realloc(evs, cap * sizeof(Ev));
		if (!evs) {
			fprintf(stderr, "out of memory\n");
			exit(1);
		}
	}
	evs[nev].t = t;
	evs[nev].seq = nev;
	evs[nev].type = (unsigned char)type;
	evs[nev].ch = (unsigned char)ch;
	evs[nev].a = (unsigned char)a;
	evs[nev].b = (unsigned char)b;
	evs[nev].v = a | (b << 7);
	nev++;
}

static long ms(long m)
{
	return m * rate / 1000;
}

static void note(long t, long dur, int ch, int key, int vel)
{
	ev(t, E_ON, ch, key, vel);
	ev(t + dur, E_OFF, ch, key, 0);
}

static int ev_cmp(const void *x, const void *y)
{
	const Ev *a = x, *b = y;
	if (a->t != b->t)
		return a->t < b->t ? -1 : 1;
	return a->seq - b->seq;
}

/* Play the event list through the synth into a buffer; chunk = max render
 * slice (0 = render exactly up to each event). */
static short *play(MidiSynth *m, long total, int chunk)
{
	short *buf = calloc(total, sizeof(short));
	long pos = 0;
	int i = 0;
	qsort(evs, nev, sizeof(Ev), ev_cmp);
	while (pos < total) {
		long until = i < nev ? evs[i].t : total;
		if (until > total)
			until = total;
		while (pos < until) {
			long n = until - pos;
			if (chunk && n > chunk)
				n = chunk;
			m->render(m->ctx, buf + pos, (int)n);
			pos += n;
		}
		while (i < nev && evs[i].t <= pos) {
			Ev *e = &evs[i++];
			switch (e->type) {
			case E_ON: m->note_on(m->ctx, e->ch, e->a, e->b); break;
			case E_OFF: m->note_off(m->ctx, e->ch, e->a); break;
			case E_PROG: m->program(m->ctx, e->ch, e->a); break;
			case E_CC: m->control(m->ctx, e->ch, e->a, e->b); break;
			case E_BEND: m->pitch_bend(m->ctx, e->ch, e->v); break;
			}
		}
	}
	return buf;
}

static void put16(FILE *f, int v) { fputc(v & 255, f); fputc((v >> 8) & 255, f); }
static void put32(FILE *f, long v) { put16(f, (int)(v & 0xFFFF)); put16(f, (int)((v >> 16) & 0xFFFF)); }

static void write_wav(const char *dir, const char *name, const short *buf, long n)
{
	char path[1024];
	FILE *f;
	long i, peak = 0, clip = 0;
	snprintf(path, sizeof(path), "%s/%s", dir, name);
	f = fopen(path, "wb");
	if (!f) {
		perror(path);
		exit(1);
	}
	fwrite("RIFF", 1, 4, f); put32(f, 36 + n * 2); fwrite("WAVEfmt ", 1, 8, f);
	put32(f, 16); put16(f, 1); put16(f, 1); put32(f, rate); put32(f, rate * 2L);
	put16(f, 2); put16(f, 16); fwrite("data", 1, 4, f); put32(f, n * 2);
	for (i = 0; i < n; i++) {
		long a = buf[i] < 0 ? -(long)buf[i] : buf[i];
		if (a > peak) peak = a;
		if (a >= 32767) clip++;
		put16(f, buf[i]);
	}
	fclose(f);
	printf("  %-16s %6.1f s  peak %5ld (%5.1f dBFS)  clipped %ld\n", name, (double)n / rate,
	       peak, peak ? 20.0 * log10((double)peak / 32767.0) : -99.0, clip);
}

static void reset_events(void)
{
	nev = 0;
}

/* ---- the demos ---------------------------------------------------------- */

static long demo_allprogs(void)
{
	long t = ms(200);
	int p;
	for (p = 0; p < 128; p++) {
		int base = 60, k;
		static const int arp[4] = { 0, 4, 7, 12 };
		if ((p >= 32 && p < 40) || p == 43 || p == 58 || p == 70)
			base = 36;
		else if (p == 42 || p == 57 || p == 67 || p == 87)
			base = 48;
		else if (p >= 8 && p < 16 && p != 14)
			base = 72;
		ev(t, E_PROG, 0, p, 0);
		for (k = 0; k < 4; k++)
			note(t + ms(30 + k * 180), ms(170), 0, base + arp[k], 100);
		for (k = 0; k < 3; k++)
			note(t + ms(780), ms(600), 0, base + arp[k], 90);
		t += ms(1700);
	}
	return t + ms(500);
}

static long demo_scales(void)
{
	static const int fam_base[16] = { 48, 72, 48, 48, 36, 48, 48, 48, 60, 60, 60, 60, 48, 48, 48, 48 };
	static const int major[15] = { 0, 2, 4, 5, 7, 9, 11, 12, 14, 16, 17, 19, 21, 23, 24 };
	long t = ms(200);
	int f, k;
	for (f = 0; f < 16; f++) {
		int prog = f * 8 + (f == 9 ? 1 : 0);
		ev(t, E_PROG, 1, prog, 0);
		for (k = 0; k < 15; k++)
			note(t + ms(20 + k * 160), ms(150), 1, fam_base[f] + major[k], 100);
		note(t + ms(20 + 15 * 160), ms(700), 1, fam_base[f], 100);
		t += ms(20 + 15 * 160 + 1300);
	}
	return t;
}

static long demo_drums(void)
{
	long t = ms(200), beat = ms(500), e = beat / 2;
	int bar, i, k;
	ev(0, E_CC, 9, 7, 110);
	for (bar = 0; bar < 4; bar++) {
		long b0 = t + bar * 4 * beat;
		if (bar == 0)
			note(b0, e, 9, 49, 110);
		for (i = 0; i < 8; i++)
			note(b0 + i * e, e, 9, (bar == 3 && i == 7) ? 46 : 42, i & 1 ? 80 : 100);
		note(b0, e, 9, 36, 120);
		note(b0 + 2 * e + e, e, 9, 36, 100);
		note(b0 + 4 * e, e, 9, 36, 120);
		note(b0 + 2 * e, e, 9, 38, 115);
		note(b0 + 6 * e, e, 9, 38, 115);
	}
	t += 16 * beat;
	/* tom fill and crash */
	{
		static const int fill[8] = { 50, 50, 48, 48, 47, 45, 43, 41 };
		for (i = 0; i < 8; i++)		/* sixteenths over two beats */
			note(t + i * (e / 2), e / 2, 9, fill[i], 110);
		t += 4 * e;
		note(t, e, 9, 49, 120);
		note(t, e, 9, 36, 120);
		t += 4 * beat;
	}
	/* every mapped key, slowly */
	for (k = 27; k <= 87; k++) {
		note(t, ms(300), 9, k, 110);
		t += ms(450);
	}
	return t + ms(1500);
}

static long demo_chords(void)
{
	static const int prog_chord[4][3] = { { 48, 52, 55 }, { 45, 48, 52 }, { 41, 45, 48 }, { 43, 47, 50 } };
	static const int roots[4] = { 36, 33, 29, 31 };
	static const int lead[16] = { 72, 74, 76, 79, 76, 74, 72, 69, 72, 74, 76, 77, 79, 77, 76, 74 };
	long t = ms(300), bar = ms(2000), q = bar / 4;
	int rep, c, i, k;
	ev(0, E_PROG, 0, 0, 0);		/* piano */
	ev(0, E_PROG, 1, 48, 0);	/* strings */
	ev(0, E_PROG, 2, 33, 0);	/* fingered bass */
	ev(0, E_PROG, 3, 66, 0);	/* tenor sax */
	ev(0, E_CC, 1, 7, 80);
	ev(0, E_CC, 3, 7, 100);
	ev(0, E_CC, 9, 7, 100);
	for (rep = 0; rep < 2; rep++) {
		for (c = 0; c < 4; c++) {
			long b0 = t + (rep * 4 + c) * bar;
			ev(b0, E_CC, 0, 64, 127);
			for (k = 0; k < 4; k++)
				for (i = 0; i < 3; i++)
					note(b0 + k * q, q / 2, 0, prog_chord[c][i] + 12, 90 - k * 5);
			ev(b0 + bar - ms(30), E_CC, 0, 64, 0);
			for (i = 0; i < 3; i++)
				note(b0, bar - ms(40), 1, prog_chord[c][i], 85);
			for (k = 0; k < 4; k++)
				note(b0 + k * q, q - ms(40), 2, roots[c] + (k == 2 ? 7 : 0), 110);
			for (k = 0; k < 8; k++) {
				note(b0 + k * q / 2, q / 4, 9, 42, 85);
				if (k == 0 || k == 3 || k == 4)
					note(b0 + k * q / 2, q / 4, 9, 36, 115);
				if (k == 2 || k == 6)
					note(b0 + k * q / 2, q / 4, 9, 38, 110);
			}
			if (rep == 1)
				for (k = 0; k < 4; k++)
					note(b0 + k * q, q - ms(20), 3, lead[c * 4 + k], 100);
		}
	}
	/* a bent sax note at the end, and a final chord */
	t += 8 * bar;
	note(t, bar * 2, 3, 72, 105);
	for (k = 0; k <= 32; k++)
		ev(t + ms(400) + k * ms(25), E_BEND, 3, (8192 + k * 128) & 127, (8192 + k * 128) >> 7);
	for (k = 0; k <= 32; k++)
		ev(t + ms(1600) + k * ms(25), E_BEND, 3, (12288 - k * 128) & 127, (12288 - k * 128) >> 7);
	for (i = 0; i < 3; i++) {
		note(t, bar * 2, 0, prog_chord[0][i] + 12, 100);
		note(t, bar * 2, 1, prog_chord[0][i], 90);
	}
	note(t, bar * 2, 2, 36, 110);
	note(t, ms(300), 9, 49, 115);
	note(t, ms(300), 9, 36, 115);
	return t + bar * 2 + ms(1500);
}

/* ---- benchmark ------------------------------------------------------------ */

static void bench(const unsigned char *bank, long len, int voices, int interp)
{
	MidiSynth *m = gm_synth_create(rate, bank, len, voices);
	static short buf[1024];
	long total = (long)rate * 20, done = 0;
	struct timespec c0, c1;
	double secs, sps;
	int i, act;
	gm_synth_set_interp(m, interp);
	/* looping instruments only, so every voice sounds the whole time */
	for (i = 0; i < voices; i++) {
		int ch = i % 4;
		m->program(m->ctx, ch, ch == 0 ? 48 : ch == 1 ? 19 : ch == 2 ? 81 : 52);
		m->note_on(m->ctx, ch, 40 + (i * 7) % 48, 100);
	}
	m->render(m->ctx, buf, 64);
	act = gm_synth_active_voices(m);
	/* at least 20 s of audio and at least 1 s of wall time: the voices loop
	 * forever, so the polyphony stays where it was set */
	clock_gettime(CLOCK_MONOTONIC, &c0);
	for (;;) {
		m->render(m->ctx, buf, 1024);
		done += 1024;
		if (done >= total) {
			clock_gettime(CLOCK_MONOTONIC, &c1);
			secs = (c1.tv_sec - c0.tv_sec) + (c1.tv_nsec - c0.tv_nsec) * 1e-9;
			if (secs >= 1.0)
				break;
		}
	}
	if (gm_synth_active_voices(m) != act)
		printf("  (polyphony changed during the run: %d -> %d)\n", act, gm_synth_active_voices(m));
	sps = done / secs;
	printf("  voices %2d (active %2d) interp %d: %10.0f samples/s = %6.1fx realtime, "
	       "%6.1f ns per voice-sample\n", voices, act, interp, sps, sps / rate,
	       secs * 1e9 / ((double)done * act));
	m->destroy(m->ctx);
}

int main(int argc, char **argv)
{
	FILE *f;
	unsigned char *bank;
	long len, n;
	MidiSynth *m;
	short *a, *b, *c;
	int ok;

	if (argc < 3) {
		fprintf(stderr, "usage: gmtest <bank.bnk> <outdir> [rate]\n");
		return 2;
	}
	if (argc > 3)
		rate = atoi(argv[3]);
	f = fopen(argv[1], "rb");
	if (!f) {
		perror(argv[1]);
		return 1;
	}
	fseek(f, 0, SEEK_END);
	len = ftell(f);
	fseek(f, 0, SEEK_SET);
	bank = malloc(len);
	if (fread(bank, 1, len, f) != (size_t)len) {
		fprintf(stderr, "short read\n");
		return 1;
	}
	fclose(f);
	printf("bank %s: %ld bytes, rate %d\n", argv[1], len, rate);

#define RENDER(fn, name, voices) do { \
		reset_events(); n = fn(); \
		m = gm_synth_create(rate, bank, len, voices); \
		if (!m) { fprintf(stderr, "bad bank\n"); return 1; } \
		a = play(m, n, 0); write_wav(argv[2], name, a, n); free(a); \
		m->destroy(m->ctx); } while (0)

	RENDER(demo_allprogs, "gm_allprogs.wav", 24);
	RENDER(demo_scales, "gm_scales.wav", 24);
	RENDER(demo_drums, "gm_drums.wav", 24);
	RENDER(demo_chords, "gm_chords.wav", 24);

	/* determinism: three chunkings of the same events */
	reset_events();
	n = demo_chords();
	m = gm_synth_create(rate, bank, len, 24); a = play(m, n, 0); m->destroy(m->ctx);
	m = gm_synth_create(rate, bank, len, 24); b = play(m, n, 1); m->destroy(m->ctx);
	m = gm_synth_create(rate, bank, len, 24); c = play(m, n, 777); m->destroy(m->ctx);
	ok = !memcmp(a, b, n * sizeof(short)) && !memcmp(a, c, n * sizeof(short));
	printf("determinism (chunks: per-event, 1, 777): %s\n", ok ? "IDENTICAL" : "DIFFERENT");
	free(a); free(b); free(c);

	printf("benchmark (host, %d Hz, >= 20 s of audio and >= 1 s wall time per line):\n", rate);
	bench(bank, len, 8, 1);
	bench(bank, len, 16, 1);
	bench(bank, len, 24, 1);
	bench(bank, len, 8, 0);
	bench(bank, len, 16, 0);
	bench(bank, len, 24, 0);
	free(bank);
	return ok ? 0 : 1;
}
