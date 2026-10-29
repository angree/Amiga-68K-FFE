/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 angree. See LICENSE at the repository root.
 *
 * Host test for amiga/music/hmp.c.
 *
 *   hmptest FILE.HMP...
 *       per song: tracks, header values, duration (hmp_length_usec), events
 *       per type, and a clock check: the song is played through hmp_advance
 *       in irregular slices and every dispatched event must fall inside the
 *       slice that contains its exact time, tick * 1000000 / rate.
 *
 *   hmptest_oracle [--write-ref DIR] FILE.HMP...     (built with -DHMP_ORACLE)
 *       additionally runs JJFFE's converter (ref/jjffe/hmp.cpp, linked in as
 *       load_hmp, a test oracle only) and diffs its MIDI against our parser:
 *         1. raw events, track by track: status, data and absolute tick;
 *         2. playback on the reference's time base (hmp_set_tick_period
 *            1605632/192): the dispatched list -- order, channel, data -- and
 *            the time error against the reference's own MIDI timing;
 *         3. the same on the header's time base, to show what the reference's
 *            time base costs.
 *       Exit status is non-zero if anything but 3 disagrees.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "hmp.h"

typedef long long i64;

/* ---- a recording synth -------------------------------------------------- */

typedef struct Rec {
	char type;			/* n on, f off, c control, p program, b bend */
	int ch, a, b;
	i64 t0, t1;			/* the slice it was dispatched in */
	long tick;
} Rec;

typedef struct Recorder {
	HmpSong *song;
	Rec *r;
	int n, cap;
	i64 t0, t1;
} Recorder;

static void rec_add(Recorder *rc, char type, int ch, int a, int b)
{
	Rec *e;

	if (rc->n == rc->cap) {
		rc->cap = rc->cap ? rc->cap * 2 : 4096;
		rc->r = (Rec *)realloc(rc->r, rc->cap * sizeof(Rec));
		if (!rc->r) { fprintf(stderr, "out of memory\n"); exit(2); }
	}
	e = &rc->r[rc->n++];
	e->type = type; e->ch = ch; e->a = a; e->b = b;
	e->t0 = rc->t0; e->t1 = rc->t1;
	e->tick = hmp_tick(rc->song);
}

static void r_on(void *c, int ch, int n, int v) { rec_add((Recorder *)c, 'n', ch, n, v); }
static void r_off(void *c, int ch, int n) { rec_add((Recorder *)c, 'f', ch, n, 0); }
static void r_prog(void *c, int ch, int p) { rec_add((Recorder *)c, 'p', ch, p, 0); }
static void r_ctl(void *c, int ch, int cc, int v) { rec_add((Recorder *)c, 'c', ch, cc, v); }
static void r_bend(void *c, int ch, int v) { rec_add((Recorder *)c, 'b', ch, v, 0); }

/* Play the whole song in slices taken round-robin from steps[]; returns the
 * total time the clock ran. */
static i64 play(HmpSong *s, Recorder *rc, const long *steps, int nsteps)
{
	MidiSynth m;
	int k = 0, more = 1;
	i64 now = 0;

	memset(&m, 0, sizeof(m));
	m.ctx = rc;
	m.note_on = r_on; m.note_off = r_off; m.program = r_prog;
	m.control = r_ctl; m.pitch_bend = r_bend;
	rc->song = s;
	rc->n = 0;
	hmp_rewind(s);
	while (more) {
		long st = steps[k++ % nsteps];
		rc->t0 = now;
		rc->t1 = now + st;
		more = hmp_advance(s, st, &m);
		now += st;
		if (now > (i64)3600 * 1000000) {
			fprintf(stderr, "  song never ends\n");
			break;
		}
	}
	return now;
}

/* Every event inside the slice containing its exact time? Returns misses. */
static int check_slices(const Recorder *rc, i64 num, i64 den)
{
	int i, bad = 0;

	for (i = 0; i < rc->n; i++) {
		const Rec *e = &rc->r[i];
		i64 x = (i64)e->tick * num;	/* exact time * den */
		if (!(x <= e->t1 * den && (x > e->t0 * den || (e->tick == 0 && e->t0 == 0)))) {
			if (bad < 5)
				printf("  slice miss: tick %ld in [%lld,%lld] us\n",
				       e->tick, e->t0, e->t1);
			bad++;
		}
	}
	return bad;
}

/* ---- plain report ------------------------------------------------------- */

static const char *type_name(int st)
{
	switch (st & 0xf0) {
	case 0x80: return "noteoff";
	case 0x90: return "noteon";
	case 0xa0: return "polyAT";
	case 0xb0: return "control";
	case 0xc0: return "program";
	case 0xd0: return "chanAT";
	case 0xe0: return "bend";
	}
	return st == 0xff ? "meta" : "sysex";
}

static int report(const char *path, HmpSong *s)
{
	static const long steps[] = { 20000, 19999, 1, 33333, 7, 65536, 100000, 3 };
	int cnt[9], i, bad, vel0 = 0;
	HmpEvent ev;
	Recorder rc;
	i64 ran;
	long len;
	const int keys[9] = { 0x80, 0x90, 0xa0, 0xb0, 0xc0, 0xd0, 0xe0, 0xff, 0xf0 };

	memset(cnt, 0, sizeof(cnt));
	hmp_rewind(s);
	while (hmp_next_event(s, &ev)) {
		int st = ev.status >= 0xf0 ? (ev.status == 0xff ? 0xff : 0xf0) : (ev.status & 0xf0);
		for (i = 0; i < 9; i++)
			if (keys[i] == st) cnt[i]++;
		if ((ev.status & 0xf0) == 0x90 && !ev.d2) vel0++;
	}
	len = hmp_length_usec(s);
	printf("%s: tracks %d, division %d, rate %d Hz, end tick %ld, length %ld.%03ld s\n",
	       path, hmp_tracks(s), hmp_division(s), hmp_tick_rate(s), hmp_tick(s),
	       len / 1000000, (len / 1000) % 1000);
	printf(" ");
	for (i = 0; i < 9; i++)
		if (cnt[i]) printf(" %s %d", type_name(keys[i]), cnt[i]);
	printf("  (noteon vel 0: %d)\n", vel0);

	memset(&rc, 0, sizeof(rc));
	ran = play(s, &rc, steps, 8);
	bad = check_slices(&rc, 1000000, hmp_tick_rate(s));
	printf("  played: %d events dispatched, clock ran %lld us (length %ld), slice misses %d\n",
	       rc.n, ran, len, bad);
	if (ran < len || ran - len > 100000) {
		printf("  END MISMATCH\n");
		bad++;
	}
	free(rc.r);
	return bad;
}

#ifdef HMP_ORACLE
/* ---- the oracle --------------------------------------------------------- */

extern void *load_hmp(unsigned char *buf, unsigned int br, unsigned int *out_size);

typedef struct MEv {
	long tick;
	int track, seq;
	int status, d1, d2;
} MEv;

typedef struct Midi {
	int division, ntracks;
	MEv *e;
	int n, cap;
	long tempo;			/* the one tempo it may contain, at tick 0 */
	int tempos;
	int clamped;			/* data bytes > 127 in the reference */
} Midi;

static unsigned long be32(const unsigned char *p)
{
	return ((unsigned long)p[0] << 24) | ((unsigned long)p[1] << 16) |
	       ((unsigned long)p[2] << 8) | p[3];
}

static long vlq(const unsigned char **pp, const unsigned char *end)
{
	long v = 0;
	int i;
	for (i = 0; i < 4 && *pp < end; i++) {
		int b = *(*pp)++;
		v = (v << 7) | (b & 0x7f);
		if (!(b & 0x80)) return v;
	}
	return -1;
}

static int midi_parse(const unsigned char *m, unsigned long len, Midi *mi)
{
	const unsigned char *p = m, *end = m + len;
	int tr;

	memset(mi, 0, sizeof(*mi));
	if (len < 14 || memcmp(m, "MThd", 4)) return 0;
	mi->ntracks = (m[10] << 8) | m[11];
	mi->division = (m[12] << 8) | m[13];
	p = m + 8 + be32(m + 4);
	for (tr = 0; tr < mi->ntracks; tr++) {
		const unsigned char *q, *te;
		long tick = 0;
		int rs = 0, seq = 0;
		if (end - p < 8 || memcmp(p, "MTrk", 4)) return 0;
		q = p + 8;
		te = q + be32(p + 4);
		if (te > end) return 0;
		while (q < te) {
			long d = vlq(&q, te);
			int st;
			MEv *e;
			if (d < 0 || q >= te) return 0;
			tick += d;
			st = *q;
			if (st < 0x80) st = rs; else q++;
			if (mi->n == mi->cap) {
				mi->cap = mi->cap ? mi->cap * 2 : 4096;
				mi->e = (MEv *)realloc(mi->e, mi->cap * sizeof(MEv));
			}
			e = &mi->e[mi->n];
			e->tick = tick; e->track = tr; e->seq = seq++;
			e->status = st; e->d1 = e->d2 = 0;
			if (st == 0xff || st == 0xf0 || st == 0xf7) {
				long l;
				if (st == 0xff) e->d1 = *q++;
				l = vlq(&q, te);
				if (l < 0 || l > te - q) return 0;
				if (st == 0xff && e->d1 == 0x51 && l == 3) {
					mi->tempo = (q[0] << 16) | (q[1] << 8) | q[2];
					mi->tempos++;
				}
				e->d2 = (int)l;
				q += l;
				rs = 0;
			} else if (st >= 0x80) {
				/* the reference copies data bytes verbatim; hmp.c
				 * clamps them to 127, so compare clamped, and count */
				int n = ((st & 0xe0) == 0xc0) ? 1 : 2;
				e->d1 = q[0];
				if (n == 2) e->d2 = q[1];
				if (e->d1 > 127) { e->d1 = 127; mi->clamped++; }
				if (e->d2 > 127) { e->d2 = 127; mi->clamped++; }
				q += n;
				rs = st;
			} else {
				return 0;
			}
			mi->n++;
		}
		p = te;
	}
	return 1;
}

static int cmp_mev(const void *a, const void *b)
{
	const MEv *x = (const MEv *)a, *y = (const MEv *)b;
	if (x->tick != y->tick) return x->tick < y->tick ? -1 : 1;
	if (x->track != y->track) return x->track - y->track;
	return x->seq - y->seq;
}

/* the reference event as our synth would receive it; 0 = not dispatched */
static int ref_as_rec(const MEv *e, Rec *r)
{
	r->ch = e->status & 0x0f;
	r->a = e->d1; r->b = 0;
	switch (e->status & 0xf0) {
	case 0x80: r->type = 'f'; return 1;
	case 0x90: if (e->d2) { r->type = 'n'; r->b = e->d2; } else r->type = 'f'; return 1;
	case 0xb0: r->type = 'c'; r->b = e->d2; return 1;
	case 0xc0: r->type = 'p'; return 1;
	case 0xe0: r->type = 'b'; r->a = e->d1 | (e->d2 << 7); return 1;
	}
	return 0;
}

/* Compare a playback against the reference's dispatched list and timing.
 * Returns the number of order/data mismatches; *maxerr = worst |t1 - ref|. */
static int cmp_play(const Recorder *rc, const MEv *re, int rn, i64 tnum, i64 tden,
		    i64 *maxerr, int *nref)
{
	int i, j = 0, bad = 0;

	*maxerr = 0;
	for (i = 0; i < rn; i++) {
		Rec r;
		i64 rt, err;
		if (!ref_as_rec(&re[i], &r)) continue;
		if (j >= rc->n) { bad++; continue; }
		if (r.type != rc->r[j].type || r.ch != rc->r[j].ch ||
		    r.a != rc->r[j].a || r.b != rc->r[j].b) {
			if (bad < 5)
				printf("  play mismatch #%d: ref %c ch%d %d %d, ours %c ch%d %d %d\n",
				       j, r.type, r.ch, r.a, r.b, rc->r[j].type,
				       rc->r[j].ch, rc->r[j].a, rc->r[j].b);
			bad++;
		}
		rt = (i64)re[i].tick * tnum / tden;
		err = rc->r[j].t1 - rt;
		if (err < 0) err = -err;
		if (err > *maxerr) *maxerr = err;
		j++;
	}
	*nref = j;
	if (j != rc->n) {
		printf("  play count: ref %d, ours %d\n", j, rc->n);
		bad++;
	}
	return bad;
}

static int oracle(const char *path, const unsigned char *raw, long rawlen, HmpSong *s,
		  const char *wdir)
{
	static const long step[] = { 100 };
	unsigned int mlen = 0;
	unsigned char *mid = (unsigned char *)load_hmp((unsigned char *)raw,
						       (unsigned int)rawlen, &mlen);
	Midi mi;
	HmpEvent ev;
	Recorder rc;
	MEv *sorted;
	int i, j, tr, bad = 0, raw_n = 0, nref = 0, ours_tr = hmp_tracks(s);
	i64 err_ref, err_hdr, tnum, tden;

	if (!mid) { printf("  ORACLE: reference converter refused the file\n"); return 1; }
	if (wdir) {
		char name[1024];
		const char *b = strrchr(path, '/');
		FILE *f;
		snprintf(name, sizeof(name), "%s/%s.ref.mid", wdir, b ? b + 1 : path);
		if ((f = fopen(name, "wb"))) { fwrite(mid, 1, mlen, f); fclose(f); }
	}
	if (!midi_parse(mid, mlen, &mi)) {
		printf("  ORACLE: cannot parse the reference MIDI\n");
		free(mid);
		return 1;
	}
	printf("  ref: %d tracks, division %d, tempo %ld (%d tempo events)%s\n",
	       mi.ntracks, mi.division, mi.tempo, mi.tempos,
	       mi.clamped ? " -- has data bytes > 127, compared clamped" : "");
	if (mi.ntracks != ours_tr) { printf("  TRACK COUNT ref %d ours %d\n", mi.ntracks, ours_tr); bad++; }

	/* 1. raw events, per track. Reference track 0 is its own tempo track;
	 * ours is the file's first chunk -- both must be metas only. */
	for (tr = 0; tr < ours_tr && tr < mi.ntracks; tr++) {
		int rn = 0, on = 0, mism = 0;
		hmp_rewind(s);
		j = 0;
		while (hmp_next_event(s, &ev)) {
			if (ev.track != tr) continue;
			on++;
			if (tr == 0) { if (ev.status != 0xff) mism++; continue; }
			while (j < mi.n && mi.e[j].track != tr) j++;
			if (j >= mi.n) { mism++; continue; }
			if (mi.e[j].tick != ev.tick || mi.e[j].status != ev.status ||
			    (ev.status != 0xff && (mi.e[j].d1 != ev.d1 || mi.e[j].d2 != ev.d2)) ||
			    (ev.status == 0xff && mi.e[j].d1 != ev.d1)) {
				if (mism < 3)
					printf("  raw mismatch track %d: ref t%ld %02x %d %d, ours t%ld %02x %d %d\n",
					       tr, mi.e[j].tick, mi.e[j].status, mi.e[j].d1, mi.e[j].d2,
					       ev.tick, ev.status, ev.d1, ev.d2);
				mism++;
			}
			j++;
		}
		for (i = 0; i < mi.n; i++)
			if (mi.e[i].track == tr) rn++;
		if (tr == 0) {
			for (i = 0; i < mi.n; i++)
				if (mi.e[i].track == 0 && mi.e[i].status != 0xff) mism++;
		} else if (rn != on) {
			printf("  raw count track %d: ref %d ours %d\n", tr, rn, on);
			mism++;
		}
		if (tr) raw_n += on;
		bad += mism;
	}

	/* 2./3. playback */
	sorted = (MEv *)malloc((mi.n ? mi.n : 1) * sizeof(MEv));
	memcpy(sorted, mi.e, mi.n * sizeof(MEv));
	qsort(sorted, mi.n, sizeof(MEv), cmp_mev);
	tnum = mi.tempo ? mi.tempo : 500000;
	tden = mi.division;
	memset(&rc, 0, sizeof(rc));

	hmp_set_tick_period(s, (unsigned long)tnum, (unsigned long)tden);
	play(s, &rc, step, 1);
	bad += check_slices(&rc, tnum, tden);
	bad += cmp_play(&rc, sorted, mi.n, tnum, tden, &err_ref, &nref);
	printf("  oracle: raw events %d identical per track; %d dispatched, order+data %s, "
	       "max time error on the reference clock %lld us (100 us slices)\n",
	       raw_n, nref, bad ? "DIFFER" : "identical", err_ref);

	hmp_set_tick_period(s, 0, 0);
	play(s, &rc, step, 1);
	j = cmp_play(&rc, sorted, mi.n, tnum, tden, &err_hdr, &nref);
	printf("  header clock (1/%d s) vs reference clock: max difference %lld.%03lld s%s\n",
	       hmp_tick_rate(s), err_hdr / 1000000, (err_hdr / 1000) % 1000,
	       j ? " (ORDER DIFFERS)" : "");
	bad += j;

	free(rc.r);
	free(sorted);
	free(mi.e);
	free(mid);
	return bad;
}
#endif

int main(int argc, char **argv)
{
	int i, fails = 0;
	const char *wdir = NULL;

	for (i = 1; i < argc; i++) {
		unsigned char *raw;
		long n;
		FILE *f;
		HmpSong *s;
		int bad;

		if (!strcmp(argv[i], "--write-ref") && i + 1 < argc) {
			wdir = argv[++i];
			continue;
		}
		f = fopen(argv[i], "rb");
		if (!f) { printf("%s: cannot open\n", argv[i]); fails++; continue; }
		fseek(f, 0, SEEK_END);
		n = ftell(f);
		fseek(f, 0, SEEK_SET);
		raw = (unsigned char *)malloc(n);
		if (fread(raw, 1, n, f) != (size_t)n) n = 0;
		fclose(f);

		/* robustness: every truncation of the file must load or refuse
		 * without reading past the end (run under valgrind/asan to see) */
		{
			long cut;
			for (cut = 0; cut < n; cut += (cut < 0x400 ? 1 : 97)) {
				unsigned char *c = (unsigned char *)malloc(cut ? cut : 1);
				HmpSong *t;
				memcpy(c, raw, cut);
				if ((t = hmp_load_mem(c, cut))) {
					hmp_length_usec(t);
					while (hmp_advance(t, 1000000, NULL)) ;
					hmp_free(t);
				}
				free(c);
			}
		}

		s = hmp_load_mem(raw, n);
		if (!s) { printf("%s: not loaded\n", argv[i]); fails++; free(raw); continue; }
		bad = report(argv[i], s);
#ifdef HMP_ORACLE
		bad += oracle(argv[i], raw, n, s, wdir);
#else
		(void)wdir;
#endif
		if (bad) { printf("  FAIL (%d)\n", bad); fails++; }
		hmp_free(s);
		free(raw);
	}
	printf("%s: %d file(s) failed\n", fails ? "FAIL" : "OK", fails);
	return fails ? 1 : 0;
}
