/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 angree. See LICENSE at the repository root.
 *
 * HMI "HMIMIDIP" song loader and sequencer for the game's .HMP files in MUSIC.
 *
 * Written from the file format, checked against the 17 shipped songs and
 * against JJFFE's HMP->MIDI converter (ref/jjffe/hmp.cpp, used as a test
 * oracle only -- tools/music/hmptest.c). None of its code is in here.
 *
 * ---- the file, as it is read ---------------------------------------------
 *
 *   0x00  "HMIMIDIP" + version; every shipped song says "HMIMIDIP013195"
 *   0x20  u32 LE  length of the song data (the files carry 7..17 bytes more)
 *   0x30  u32 LE  number of track chunks
 *   0x34  u32 LE  ticks per quarter note (96 in every song)
 *   0x38  u32 LE  ticks per SECOND (120 in every song)
 *   0x3C  u32 LE  song length in whole seconds, informational
 *   0x308 first track chunk, or 0x388 in the "013195" revision
 *
 * A track chunk is u32 chunk number, u32 chunk size (header included), u32
 * track id, then size-12 bytes of events. The first chunk of every shipped
 * song is empty (one end-of-track); the reference converter drops it and
 * writes its own tempo track instead, we keep it, so track numbers agree.
 *
 * Events are MIDI with two changes:
 *   - the delta time is a variable-length number with the groups in the
 *     OPPOSITE order and the opposite stop rule: least significant 7 bits
 *     first, and the byte with bit 7 SET is the last one (MIDI sets bit 7 on
 *     every byte EXCEPT the last);
 *   - no shipped song uses running status, sysex or any meta except
 *     end-of-track (measured over all 17). Running status, sysex and tempo
 *     metas are still accepted, the latter two with standard MIDI length
 *     numbers -- unverified against a real file, so a song that has them
 *     deserves a look.
 *
 * Data bytes are CLAMPED to 127, not masked: GGOFKIEV.HMP opens its drum
 * channel with "B9 07 80", volume 128 -- the only data byte >= 0x80 in all
 * 17 songs. Masking would make that volume 0 and mute the drums for the whole
 * song; the reference converter copies the 0x80 into its MIDI verbatim.
 *
 * ---- the clock -----------------------------------------------------------
 *
 * The tick is 1/rate seconds, rate from 0x38: 120 Hz, 8333 1/3 us. That is
 * what the header says and what ScummVM's HMP parser does (60 PPQN at
 * 60000000/bpm us per quarter = 1/bpm s per tick). The reference converter
 * instead writes division 192 with tempo 0x188000, 8362 2/3 us a tick, which
 * runs every song 0.35% slow (0.67 s by the end of INTRO.HMP);
 * hmp_set_tick_period() reproduces its time base so the oracle comparison can
 * still be done to the microsecond.
 *
 * The period is kept as a fraction pnum/pden microseconds. hmp_advance()
 * adds usec*pden to a remainder and takes whole ticks out of it by pnum, so
 * nothing is ever rounded and the clock cannot drift however the calls are
 * sliced. A tempo meta switches to tempo/division.
 *
 * Every read is bounded by the track's own end, which is bounded by the
 * buffer; a track whose bytes stop making sense simply ends there.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "hmp.h"

#define HMP_MAX_TRACKS	64
#define HMP_MAX_DEN	16384UL		/* keeps rem below 2^31: see hmp_advance */
#define HMP_MAX_NUM	0x40000000UL
#define HMP_CHUNK_USEC	65536L		/* largest slice added to rem at once */

typedef struct HmpTrack {
	const unsigned char *start, *pos, *end;
	long next;			/* absolute tick of the event at pos */
	int live;
	int rs;				/* running status, 0 = none yet */
} HmpTrack;

struct HmpSong {
	unsigned char *buf;
	long len;
	int ntr;
	HmpTrack tr[HMP_MAX_TRACKS];
	HmpTrack save[HMP_MAX_TRACKS];	/* hmp_length_usec's dry run */
	int division, rate;
	unsigned long onum, oden;	/* tick period override, 0 = header's */
	unsigned long pnum, pden;	/* current tick period, pnum/pden us */
	long cur_tick;
	unsigned long rem;		/* time past cur_tick's start, us*pden */
	int live;			/* tracks not yet ended */
	long length;			/* -1 = not computed */
};

static unsigned long rd32(const unsigned char *p)
{
	return (unsigned long)p[0] | ((unsigned long)p[1] << 8) |
	       ((unsigned long)p[2] << 16) | ((unsigned long)p[3] << 24);
}

/* HMP delta: LS group first, the byte with bit 7 set ends it. -1 = bad. */
static long read_delta(HmpTrack *t)
{
	long v = 0;
	int i;

	for (i = 0; i < 4; i++) {
		int b;
		if (t->pos >= t->end)
			return -1;
		b = *t->pos++;
		v |= (long)(b & 0x7f) << (7 * i);
		if (b & 0x80)
			return v;
	}
	return -1;
}

/* Standard MIDI number (meta and sysex lengths). -1 = bad. */
static long read_vlq(HmpTrack *t)
{
	long v = 0;
	int i;

	for (i = 0; i < 4; i++) {
		int b;
		if (t->pos >= t->end)
			return -1;
		b = *t->pos++;
		v = (v << 7) | (b & 0x7f);
		if (!(b & 0x80))
			return v;
	}
	return -1;
}

static void set_period(HmpSong *s, unsigned long num, unsigned long den)
{
	unsigned long old = s->pden;

	if (den < 1) den = 1;
	if (den > HMP_MAX_DEN) den = HMP_MAX_DEN;
	if (num < 1) num = 1;
	if (num > HMP_MAX_NUM) num = HMP_MAX_NUM;
	/* rem is time in units of 1/pden us: rescale it to the new unit */
	if (old && old != den && s->rem) {
		unsigned long us = s->rem / old, fr = s->rem % old;
		if (us > 0x7fffffffUL / den - 1)
			us = 0x7fffffffUL / den - 1;
		s->rem = us * den + fr * den / old;
	}
	s->pnum = num;
	s->pden = den;
}

static void base_period(HmpSong *s)
{
	if (s->onum && s->oden)
		set_period(s, s->onum, s->oden);
	else
		set_period(s, 1000000UL, (unsigned long)s->rate);
}

static void track_rewind(HmpTrack *t)
{
	long d;

	t->pos = t->start;
	t->rs = 0;
	d = read_delta(t);
	t->live = d >= 0;
	t->next = d >= 0 ? d : 0;
}

void hmp_rewind(HmpSong *s)
{
	int i;

	if (!s) return;
	s->live = 0;
	for (i = 0; i < s->ntr; i++) {
		track_rewind(&s->tr[i]);
		s->live += s->tr[i].live;
	}
	s->cur_tick = 0;
	s->rem = 0;
	s->pden = 0;
	base_period(s);
}

/* Track holding the earliest pending event, lowest index on a tie; -1 when
 * every track has ended. */
static int next_track(const HmpSong *s)
{
	int i, best = -1;
	long bt = 0;

	for (i = 0; i < s->ntr; i++) {
		const HmpTrack *t = &s->tr[i];
		if (t->live && (best < 0 || t->next < bt)) {
			best = i;
			bt = t->next;
		}
	}
	return best;
}

static void track_end(HmpSong *s, HmpTrack *t)
{
	if (t->live) {
		t->live = 0;
		s->live--;
	}
}

/* Parse the event at track i's position (its tick must be cur_tick), apply
 * a tempo meta, step to the following event. 0 = nothing usable there. */
static int take_event(HmpSong *s, int i, HmpEvent *ev)
{
	HmpTrack *t = &s->tr[i];
	int st, ended = 0;
	long d;

	ev->tick = t->next;
	ev->track = i;
	ev->d1 = ev->d2 = 0;
	ev->len = 0;
	ev->data = NULL;

	if (t->pos >= t->end) {
		track_end(s, t);
		return 0;
	}
	st = *t->pos;
	if (st < 0x80) {
		if (!t->rs) {
			track_end(s, t);
			return 0;
		}
		st = t->rs;
	} else {
		t->pos++;
	}
	ev->status = st;

	if (st < 0xf0) {
		int n = ((st & 0xe0) == 0xc0) ? 1 : 2;	/* Cx, Dx: one byte */
		if (t->end - t->pos < n) {
			track_end(s, t);
			return 0;
		}
		t->rs = st;
		ev->d1 = t->pos[0] > 0x7f ? 0x7f : t->pos[0];
		if (n == 2)
			ev->d2 = t->pos[1] > 0x7f ? 0x7f : t->pos[1];
		ev->len = n;
		t->pos += n;
	} else if (st == 0xff || st == 0xf0 || st == 0xf7) {
		long n;
		t->rs = 0;
		if (st == 0xff) {
			if (t->pos >= t->end) {
				track_end(s, t);
				return 0;
			}
			ev->d1 = *t->pos++;
		}
		n = read_vlq(t);
		if (n < 0 || n > t->end - t->pos) {
			track_end(s, t);
			return 0;
		}
		ev->len = (int)n;
		ev->data = t->pos;
		t->pos += n;
		if (st == 0xff && ev->d1 == 0x2f)
			ended = 1;
		if (st == 0xff && ev->d1 == 0x51 && n == 3) {
			unsigned long tempo = ((unsigned long)ev->data[0] << 16) |
					      ((unsigned long)ev->data[1] << 8) | ev->data[2];
			if (tempo)
				set_period(s, tempo, (unsigned long)s->division);
		}
	} else {
		/* F1..FE carry no length we could trust: the track stops here */
		track_end(s, t);
		return 0;
	}

	if (ended) {
		track_end(s, t);
	} else {
		d = read_delta(t);
		if (d < 0)
			track_end(s, t);
		else
			t->next += d;
	}
	return 1;
}

static void dispatch(const MidiSynth *m, const HmpEvent *ev)
{
	int ch = ev->status & 0x0f;

	if (!m)
		return;
	switch (ev->status & 0xf0) {
	case 0x80:
		if (m->note_off) m->note_off(m->ctx, ch, ev->d1);
		break;
	case 0x90:
		if (ev->d2) {
			if (m->note_on) m->note_on(m->ctx, ch, ev->d1, ev->d2);
		} else {
			if (m->note_off) m->note_off(m->ctx, ch, ev->d1);
		}
		break;
	case 0xb0:
		if (m->control) m->control(m->ctx, ch, ev->d1, ev->d2);
		break;
	case 0xc0:
		if (m->program) m->program(m->ctx, ch, ev->d1);
		break;
	case 0xe0:
		if (m->pitch_bend) m->pitch_bend(m->ctx, ch, ev->d1 | (ev->d2 << 7));
		break;
	default:
		/* Ax poly and Dx channel pressure: MidiSynth has no entry for
		 * them. Metas and sysex: nothing to play. */
		break;
	}
}

int hmp_advance(HmpSong *s, long usec, const MidiSynth *synth)
{
	HmpEvent ev;

	if (!s || !s->live)
		return 0;
	if (usec < 0)
		usec = 0;
	do {
		/* rem stays below HMP_CHUNK_USEC*HMP_MAX_DEN + HMP_MAX_NUM = 2^31 */
		long step = usec > HMP_CHUNK_USEC ? HMP_CHUNK_USEC : usec;
		usec -= step;
		s->rem += (unsigned long)step * s->pden;
		for (;;) {
			int i = next_track(s);
			unsigned long k, have;
			if (i < 0)
				return 0;
			k = (unsigned long)(s->tr[i].next - s->cur_tick);
			have = s->rem / s->pnum;
			if (have >= k) {
				s->rem -= k * s->pnum;
				s->cur_tick = s->tr[i].next;
				if (take_event(s, i, &ev))
					dispatch(synth, &ev);
			} else {
				s->cur_tick += (long)have;
				s->rem -= have * s->pnum;
				break;
			}
		}
	} while (usec > 0);
	return s->live ? 1 : 0;
}

int hmp_next_event(HmpSong *s, HmpEvent *ev)
{
	int i;

	if (!s)
		return 0;
	while ((i = next_track(s)) >= 0) {
		s->cur_tick = s->tr[i].next;
		if (take_event(s, i, ev))
			return 1;
	}
	return 0;
}

long hmp_length_usec(HmpSong *s)
{
	long cur_tick, us = 0;
	unsigned long rem, pnum, pden, fr = 0;
	int live, i;
	HmpEvent ev;

	if (!s)
		return 0;
	if (s->length >= 0)
		return s->length;

	memcpy(s->save, s->tr, sizeof(s->tr));
	cur_tick = s->cur_tick; rem = s->rem; pnum = s->pnum; pden = s->pden;
	live = s->live;

	hmp_rewind(s);
	while ((i = next_track(s)) >= 0) {
		unsigned long k = (unsigned long)(s->tr[i].next - s->cur_tick);
		unsigned long od = s->pden;
		while (k) {
			unsigned long n = HMP_MAX_NUM / s->pnum, tot;
			if (n > k) n = k;
			tot = n * s->pnum + fr;
			us += (long)(tot / s->pden);
			fr = tot % s->pden;
			k -= n;
		}
		s->cur_tick = s->tr[i].next;
		take_event(s, i, &ev);
		if (s->pden != od)
			fr = fr * s->pden / od;
	}
	s->length = us;

	memcpy(s->tr, s->save, sizeof(s->tr));
	s->cur_tick = cur_tick; s->rem = rem; s->pnum = pnum; s->pden = pden;
	s->live = live;
	return us;
}

int hmp_tracks(HmpSong *s)
{
	return s ? s->ntr : 0;
}

long hmp_tick(const HmpSong *s)
{
	return s ? s->cur_tick : 0;
}

int hmp_division(const HmpSong *s)
{
	return s ? s->division : 0;
}

int hmp_tick_rate(const HmpSong *s)
{
	return s ? s->rate : 0;
}

void hmp_set_tick_period(HmpSong *s, unsigned long num, unsigned long den)
{
	if (!s)
		return;
	s->onum = num;
	s->oden = den;
	s->length = -1;
	base_period(s);
}

HmpSong *hmp_load_mem(const unsigned char *data, long len)
{
	HmpSong *s;
	unsigned long ntr, v;
	long p;
	int i;

	if (!data || len < 0x40 || memcmp(data, "HMIMIDIP", 8))
		return NULL;
	s = (HmpSong *)calloc(1, sizeof(*s));
	if (!s)
		return NULL;
	s->buf = (unsigned char *)malloc((size_t)len);
	if (!s->buf) {
		free(s);
		return NULL;
	}
	memcpy(s->buf, data, (size_t)len);
	s->len = len;
	s->length = -1;

	v = rd32(s->buf + 0x34);
	s->division = (v >= 1 && v <= HMP_MAX_DEN) ? (int)v : 96;
	v = rd32(s->buf + 0x38);
	s->rate = (v >= 1 && v <= HMP_MAX_DEN) ? (int)v : 120;
	ntr = rd32(s->buf + 0x30);
	if (ntr > HMP_MAX_TRACKS)
		ntr = HMP_MAX_TRACKS;

	p = memcmp(s->buf + 8, "013195", 6) ? 0x308 : 0x388;
	for (i = 0; i < (int)ntr; i++) {
		unsigned long csz;
		if (p > len - 12)
			break;
		csz = rd32(s->buf + p + 4);
		if (csz < 12)
			break;
		s->tr[i].start = s->buf + p + 12;
		s->tr[i].end = (csz > (unsigned long)(len - p)) ? s->buf + len
							      : s->buf + p + csz;
		p += (csz > (unsigned long)(len - p)) ? len - p : (long)csz;
	}
	s->ntr = i;
	if (!s->ntr) {
		hmp_free(s);
		return NULL;
	}
	hmp_rewind(s);
	return s;
}

HmpSong *hmp_load_file(const char *path)
{
	FILE *f;
	long n;
	unsigned char *b;
	HmpSong *s = NULL;

	f = fopen(path, "rb");
	if (!f)
		return NULL;
	if (fseek(f, 0, SEEK_END) == 0 && (n = ftell(f)) > 0 &&
	    fseek(f, 0, SEEK_SET) == 0) {
		b = (unsigned char *)malloc((size_t)n);
		if (b) {
			if (fread(b, 1, (size_t)n, f) == (size_t)n)
				s = hmp_load_mem(b, n);
			free(b);
		}
	}
	fclose(f);
	return s;
}

void hmp_free(HmpSong *s)
{
	if (!s)
		return;
	free(s->buf);
	free(s);
}
