/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 angree. See LICENSE at the repository root.
 *
 * Host test bench for amiga/music/opl2.c + adlib_midi.c.
 *
 *   opltest MELODIC.BNK DRUM.BNK OUTDIR      render the listening tests
 *   opltest MELODIC.BNK DRUM.BNK --bench     render() speed, both qualities
 *   opltest --check-tables                   embedded tables against libm
 *
 * The listening tests, each written at quality 1 / 22050 Hz and quality 0 at
 * 22050 and 11025 Hz (OUTDIR/<test>_q<Q>_<rate>.wav, mono 16-bit):
 *   scales   every MELODIC.BNK program 0..127 in order: C4 E4 G4 C5, a rest
 *   drums    a GM rock pattern (kick, snare, hats, crash), then every
 *            non-blank DRUM.BNK key 27..87 one after another
 *   chords   piano chords with the sustain pedal, a string pad under a pitch
 *            bend sweep, organ, brass, a nine-voice cluster (voice stealing)
 *
 * The synth sources are #included, not linked, so that --check-tables can see
 * their static tables. This file is host-only: it uses stdio, clock() and
 * libm; nothing here goes into the Amiga build.
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "../../amiga/music/opl2.c"
#include "../../amiga/music/adlib_midi.c"

/* ---- table check ------------------------------------------------------- */

static int check(const char *name, const unsigned short *t, int n, double (*f)(int))
{
	int i, bad = 0;
	for (i = 0; i < n; i++) {
		long want = (long)floor(f(i) + 0.5);
		if (want != t[i]) {
			if (bad < 5)
				printf("  %s[%d] = %u, formula gives %ld\n", name, i, t[i], want);
			bad++;
		}
	}
	printf("%-12s %3d entries, %d mismatches\n", name, n, bad);
	return bad;
}
static double f_logsin(int i) { return -log2(sin((i + 0.5) * M_PI / 512)) * 256; }
static double f_exp(int i) { return 2048 * pow(2, (255 - i) / 256.0); }
static double f_fnum(int k) { return 440 * pow(2, (67 + k / 32.0 - 69) / 12) * 65536 / 49716; }

static int check_tables(void)
{
	unsigned short va[128];
	int i, bad = 0;
	bad += check("opl_logsin", opl_logsin, 256, f_logsin);
	bad += check("opl_exp", opl_exp, 256, f_exp);
	bad += check("am_fnum", am_fnum, 384, f_fnum);
	for (i = 0; i < 128; i++) {
		long w = i ? (long)floor(40 * log10(127.0 / i) / 0.75 + 0.5) : 63;
		va[i] = (unsigned short)am_volatt[i];
		if (w > 63)
			w = 63;
		if (w != va[i]) {
			printf("  am_volatt[%d] = %u, formula gives %ld\n", i, va[i], w);
			bad++;
		}
	}
	printf("am_volatt    128 entries checked\n");
	printf(bad ? "TABLES: %d MISMATCHES\n" : "TABLES: all identical\n", bad);
	return bad ? 1 : 0;
}

/* ---- files -------------------------------------------------------------- */

static unsigned char *slurp(const char *path, long *len)
{
	FILE *f = fopen(path, "rb");
	unsigned char *b;
	if (!f) {
		fprintf(stderr, "cannot open %s\n", path);
		exit(1);
	}
	fseek(f, 0, SEEK_END);
	*len = ftell(f);
	fseek(f, 0, SEEK_SET);
	b = (unsigned char *)malloc(*len);
	if (fread(b, 1, *len, f) != (size_t)*len)
		exit(1);
	fclose(f);
	return b;
}

static void put32le(FILE *f, unsigned long v)
{
	fputc(v & 255, f); fputc((v >> 8) & 255, f); fputc((v >> 16) & 255, f); fputc((v >> 24) & 255, f);
}
static void put16le(FILE *f, unsigned v) { fputc(v & 255, f); fputc((v >> 8) & 255, f); }

/* ---- a tiny recorder: events at millisecond times ----------------------- */

typedef struct {
	MidiSynth *s;
	int rate;
	short *buf;
	long len, cap;		/* samples */
	long peak, clips;
} Rec;

static void rec_until(Rec *r, long ms)
{
	long want = ms * (long)r->rate / 1000, n, i;
	if (want <= r->len)
		return;
	n = want - r->len;
	if (want > r->cap) {
		r->cap = want * 2;
		r->buf = (short *)realloc(r->buf, r->cap * sizeof(short));
	}
	r->s->render(r->s->ctx, r->buf + r->len, (int)n);
	for (i = r->len; i < want; i++) {
		long v = r->buf[i] < 0 ? -(long)r->buf[i] : r->buf[i];
		if (v > r->peak)
			r->peak = v;
		if (v >= 32767)
			r->clips++;
	}
	r->len = want;
}

#define ON(t, c, n, v)	do { rec_until(&r, t); s->note_on(s->ctx, c, n, v); } while (0)
#define OFF(t, c, n)	do { rec_until(&r, t); s->note_off(s->ctx, c, n); } while (0)

static long t_scales(Rec *rr)
{
	Rec r = *rr;
	MidiSynth *s = r.s;
	static const int nn[4] = { 60, 64, 67, 72 };
	long t = 0;
	int p, i;
	for (p = 0; p < 128; p++) {
		s->program(s->ctx, 0, p);
		for (i = 0; i < 4; i++) {
			ON(t, 0, nn[i], 100);
			OFF(t + 180, 0, nn[i]);
			t += 200;
		}
		t += 250;
	}
	rec_until(&r, t + 1000);
	*rr = r;
	return t;
}

static long t_drums(Rec *rr)
{
	Rec r = *rr;
	MidiSynth *s = r.s;
	long t = 0;
	int bar, e, k;
	for (bar = 0; bar < 4; bar++)
		for (e = 0; e < 8; e++) {		/* 120 bpm eighths: 250 ms */
			long tt = t + (bar * 8 + e) * 250;
			if (bar == 0 && e == 0)
				ON(tt, 9, 49, 110);
			if (e == 0 || e == 4 || e == 5)
				ON(tt, 9, 36, 120);
			if (e == 2 || e == 6)
				ON(tt, 9, 38, 110);
			ON(tt, 9, e == 7 ? 46 : 42, 80);
			OFF(tt + 120, 9, 36);
			OFF(tt + 120, 9, 38);
			OFF(tt + 120, 9, 42);
			OFF(tt + 120, 9, 46);
			OFF(tt + 120, 9, 49);
		}
	t += 32 * 250 + 1000;
	for (k = 27; k <= 87; k++) {
		ON(t, 9, k, 110);
		OFF(t + 150, 9, k);
		t += 400;
	}
	rec_until(&r, t + 1500);
	*rr = r;
	return t;
}

static long t_chords(Rec *rr)
{
	Rec r = *rr;
	MidiSynth *s = r.s;
	static const int ch3[4][3] = { { 60, 64, 67 }, { 60, 65, 69 }, { 59, 62, 67 }, { 60, 64, 67 } };
	static const int progs[3] = { 48, 16, 61 };
	long t = 0;
	int c, i, p, b;

	s->program(s->ctx, 0, 0);			/* piano, pedal */
	for (c = 0; c < 4; c++) {
		rec_until(&r, t);
		s->control(s->ctx, 0, 64, 127);
		for (i = 0; i < 3; i++)
			ON(t, 0, ch3[c][i], 90 + i * 10);
		ON(t, 0, ch3[c][0] - 24, 100);
		for (i = 0; i < 3; i++)
			OFF(t + 300, 0, ch3[c][i]);
		OFF(t + 300, 0, ch3[c][0] - 24);
		rec_until(&r, t + 950);
		s->control(s->ctx, 0, 64, 0);
		t += 1000;
	}
	t += 500;
	for (p = 0; p < 3; p++) {			/* strings / organ / brass */
		s->program(s->ctx, 1, progs[p]);
		for (c = 0; c < 4; c++) {
			for (i = 0; i < 3; i++)
				ON(t, 1, ch3[c][i], 100);
			if (p == 0 && c == 3)		/* bend sweep up 2 semitones and back */
				for (b = 0; b <= 64; b++) {
					rec_until(&r, t + b * 15);
					s->pitch_bend(s->ctx, 1, 8192 + (b <= 32 ? b : 64 - b) * 8191 / 32);
				}
			for (i = 0; i < 3; i++)
				OFF(t + 900, 1, ch3[c][i]);
			t += 1000;
		}
		t += 600;
	}
	s->program(s->ctx, 2, 89);			/* 12-note cluster on 9 voices */
	for (i = 0; i < 12; i++)
		ON(t + i * 120, 2, 48 + i * 3, 90);
	for (i = 0; i < 12; i++)
		OFF(t + 2500, 2, 48 + i * 3);
	t += 4000;
	rec_until(&r, t);
	*rr = r;
	return t;
}

static int write_wav(const char *path, const Rec *r)
{
	FILE *f = fopen(path, "wb");
	long i;
	if (!f)
		return 0;
	fwrite("RIFF", 1, 4, f);
	put32le(f, 36 + r->len * 2);
	fwrite("WAVEfmt ", 1, 8, f);
	put32le(f, 16);
	put16le(f, 1);
	put16le(f, 1);
	put32le(f, r->rate);
	put32le(f, r->rate * 2);
	put16le(f, 2);
	put16le(f, 16);
	fwrite("data", 1, 4, f);
	put32le(f, r->len * 2);
	for (i = 0; i < r->len; i++)
		put16le(f, (unsigned short)r->buf[i]);
	fclose(f);
	return 1;
}

/* ---- main --------------------------------------------------------------- */

static unsigned char *mel, *drm;
static long mlen, dlen;

static MidiSynth *mk(int rate, int q)
{
	MidiSynth *s = adlib_midi_create(rate, mel, mlen, drm, dlen, q);
	if (!s) {
		fprintf(stderr, "adlib_midi_create(%d, q%d) failed\n", rate, q);
		exit(1);
	}
	return s;
}

static double now(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec + ts.tv_nsec / 1e9;
}

/* nvoices notes of prog, re-struck every 0.5 s of audio so none decays to
 * silence (a silent channel costs nothing and would flatter the number);
 * renders until at least 2 s of wall time have passed */
static void bench(int q, int rate, int nvoices, int prog)
{
	MidiSynth *s = mk(rate, q);
	short buf[512];
	long done = 0, next = 0;
	double t0 = now(), sec = 0;
	int i;

	s->program(s->ctx, 0, prog);
	do {
		if (done >= next) {
			for (i = 0; i < nvoices; i++) {
				s->note_off(s->ctx, 0, 48 + i * 4);
				s->note_on(s->ctx, 0, 48 + i * 4, 100);
			}
			next += rate / 2;
		}
		s->render(s->ctx, buf, 512);
		done += 512;
	} while ((done & 0xffff) || (sec = now() - t0) < 2.0);
	printf("q%d %5d Hz  %d voices prog %3d: %10.0f samples/s  = %7.1f x realtime\n",
	       q, rate, nvoices, prog, done / sec, done / sec / rate);
	s->destroy(s->ctx);
}

int main(int argc, char **argv)
{
	static const struct { int q, rate; } cfg[3] = { { 1, 22050 }, { 0, 22050 }, { 0, 11025 } };
	static const char *names[3] = { "scales", "drums", "chords" };
	long (*tests[3])(Rec *) = { t_scales, t_drums, t_chords };
	int i, j;

	if (argc >= 2 && strcmp(argv[1], "--check-tables") == 0)
		return check_tables();
	if (argc < 4) {
		fprintf(stderr, "usage: opltest MELODIC.BNK DRUM.BNK OUTDIR|--bench\n"
				"       opltest --check-tables\n");
		return 2;
	}
	mel = slurp(argv[1], &mlen);
	drm = slurp(argv[2], &dlen);

	if (strcmp(argv[3], "--bench") == 0) {
		for (i = 0; i < 2; i++) {
			bench(i, 22050, 9, 48);	/* strings: FM + feedback */
			bench(i, 22050, 9, 0);	/* piano: FM, no feedback */
			bench(i, 22050, 9, 16);	/* organ: additive + feedback */
			bench(i, 11025, 9, 48);
			bench(i, 22050, 1, 48);	/* one voice: the per-voice figure */
		}
		return 0;
	}

	for (i = 0; i < 3; i++)
		for (j = 0; j < 3; j++) {
			char path[1024];
			Rec r;
			memset(&r, 0, sizeof(r));
			r.rate = cfg[j].rate;
			r.s = mk(cfg[j].rate, cfg[j].q);
			tests[i](&r);
			snprintf(path, sizeof(path), "%s/%s_q%d_%d.wav", argv[3], names[i], cfg[j].q, cfg[j].rate);
			if (!write_wav(path, &r)) {
				fprintf(stderr, "cannot write %s\n", path);
				return 1;
			}
			printf("%-40s %6.1f s  peak %5ld  clipped %ld\n", path,
			       (double)r.len / r.rate, r.peak, r.clips);
			r.s->destroy(r.s->ctx);
			free(r.buf);
		}
	return 0;
}
