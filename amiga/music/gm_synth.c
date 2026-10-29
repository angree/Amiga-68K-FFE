/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 angree. See LICENSE at the repository root.
 *
 * gm_synth: sample-playback General MIDI behind midi_synth.h.
 *
 * The bank format and the controller list are in gm_synth.h. This file is
 * the engine, and every number in it is an integer: the target is a 68020
 * with no FPU, and the same source must give the same samples on the host.
 *
 * ---- time base -----------------------------------------------------------
 *
 * Everything that is not a sample -- envelopes, gains, controller changes --
 * runs at a CONTROL RATE of one step per BLOCK (32) output samples, on a grid
 * fixed to the absolute sample count. render() keeps one block of finished
 * output and hands it out in whatever slices the caller asks for, so how the
 * caller chunks its calls changes nothing: an event between two render()
 * calls takes effect at the next block boundary, at most 1.45 ms late at
 * 22050 Hz, and the output is bit-identical for any chunking.
 *
 * ---- pitch ---------------------------------------------------------------
 *
 * Positions are 16.16 fixed point, so the frame index is the high word -- one
 * SWAP on the 68020 -- and a zone holds fewer than 65472 frames (the margin
 * keeps pos + MAXSTEP inside 32 bits). A voice's pitch is kept in 1/64
 * semitone:
 *   p = (key - root) * 64 + fine * 64 / 100 + bend
 * and turned into a step by two table products (semitone, then 1/64 step)
 * on a 12-bit-fraction base step, then one shift for octave and format. All
 * intermediate products stay below 2^32 unsigned: the base step is at most
 * (32000 / 8000) << 12 = 16384, times at most 123715 in the semitone table.
 *
 * ---- loudness ------------------------------------------------------------
 *
 * Envelopes after the attack live in the log domain, as an attenuation in
 * 1/256 octave of amplitude (256 = 6.02 dB, 4096 = 96 dB = silent), so a
 * decay is a constant add per block and sounds like a real one. The 64-entry
 * table below turns attenuation into a Q15 gain. The attack is linear in
 * amplitude, as in SoundFont 2. Velocity, CC 7 and CC 11 follow the GM
 * 40*log10 curve (value squared); the bank's program volume is linear.
 *
 * Per voice the gain is Q14 (16384 = unity), kept in the HIGH word of a long
 * so the per-sample multiplier is again one SWAP away, and ramped linearly
 * across each block so control-rate steps never click; a block whose gain
 * does not move takes a loop without the ramp. Sample times gain is a 16x16
 * MULS.W. The mix accumulates in 32 bits and is scaled so ONE voice at full
 * velocity and unity gain peaks at -6 dBFS, then hard-clipped.
 *
 * ---- 68020 cost ------------------------------------------------------------
 *
 * The mixers are kept out of line so that gcc gives them every register;
 * build_gm_tests.sh keeps the generated assembly for counting. Counted from
 * that assembly (gcc 6.5 -O1), per voice and output sample, with MC68020
 * cache-case timings and no memory wait states:
 *   nearest, steady gain  12 instructions, ~70 cycles (MULS.W alone is 27)
 *   nearest, ramped gain  15 instructions, ~80 cycles
 *   interpolated          26 instructions, ~150 cycles (two MULS.W)
 * A 14 MHz 68020 at 22050 Hz output has ~640 cycles per output sample, so
 * live playback there fits about 6-7 nearest voices with nothing else
 * running; 11025 Hz doubles that. Offline rendering is the intended route on
 * the 68020; live is for faster machines.
 */

#include <stdlib.h>
#include <string.h>

#include "gm_synth.h"

#define MAXVOICES	64
#define BLOCK		32		/* control period, samples (a power of two: see ramp) */
#define BLOCK_SHIFT	5
#define FRAC		16		/* position fraction bits */
#define MAXSTEP		(64UL << FRAC)
#define MAXFRAMES	65472UL		/* (MAXFRAMES << 16) + MAXSTEP < 2^32 */

#ifdef __GNUC__
#define NOINLINE	__attribute__((noinline))
#else
#define NOINLINE
#endif

/* 16x16 -> 32 signed multiply of the LOW words of a and b. gcc 6.5 widens
 * both operands before it matches its own MULS.W pattern and so emits the
 * 32x32 MULS.L (~43 cycles against ~27 on the 68020); this says it directly.
 * The C fallback is the same arithmetic for every value the mixers pass. */
#if defined(__GNUC__) && defined(__mc68000__)
static __inline__ long mul16(long a, long b)
{
	__asm__ ("muls.w %1,%0" : "+d" (a) : "d" (b));
	return a;
}
#else
#define mul16(a, b)	((long)(short)(a) * (long)(short)(b))
#endif

#define ATT_SILENT	4096
#define ATT_OFF		2560		/* -60 dB: a decaying voice is freed here */
#define CHOKE_MS	40		/* exclusive drum groups, CC 120-less cut */

#define ZF_LOOP		1
#define ZF_TAIL		2

enum { ENV_ATTACK, ENV_HOLD, ENV_DECAY, ENV_SUSTAIN, ENV_RELEASE };

#define VF_LOOP		0x01
#define VF_TAIL		0x02
#define VF_DRUM		0x04
#define VF_RELEASED	0x08		/* note-off seen */
#define VF_SUSTAINED	0x10		/* note-off seen while the pedal was down */

/* round(65536 * 2^(i/12)) */
static const unsigned long semi_tab[12] = {
	65536, 69433, 73562, 77936, 82570, 87480, 92682, 98193, 104032, 110218, 116772, 123715
};

/* round(65536 * 2^(i/768)): 1/64 semitone steps */
static const unsigned long fine_tab[64] = {
	65536, 65595, 65654, 65714, 65773, 65832, 65892, 65951, 66011, 66071, 66130, 66190,
	66250, 66309, 66369, 66429, 66489, 66549, 66609, 66670, 66730, 66790, 66850, 66911,
	66971, 67032, 67092, 67153, 67213, 67274, 67335, 67395, 67456, 67517, 67578, 67639,
	67700, 67761, 67823, 67884, 67945, 68007, 68068, 68129, 68191, 68252, 68314, 68376,
	68438, 68499, 68561, 68623, 68685, 68747, 68809, 68871, 68933, 68996, 69058, 69120,
	69183, 69245, 69308, 69370
};

/* round(32768 * 2^(-i/64)): attenuation (in 1/256 octave, low 8 bits >> 2) -> Q15 */
static const unsigned short att_tab[64] = {
	32768, 32415, 32066, 31720, 31379, 31041, 30706, 30376, 30048, 29725, 29405, 29088,
	28774, 28464, 28158, 27855, 27554, 27258, 26964, 26674, 26386, 26102, 25821, 25543,
	25268, 24995, 24726, 24460, 24196, 23936, 23678, 23423, 23170, 22921, 22674, 22430,
	22188, 21949, 21713, 21479, 21247, 21019, 20792, 20568, 20347, 20127, 19911, 19696,
	19484, 19274, 19066, 18861, 18658, 18457, 18258, 18061, 17867, 17674, 17484, 17296,
	17109, 16925, 16743, 16562
};

typedef struct {
	unsigned char inst;
	signed char transpose;
	long vol_q15;
	long attack_inc;		/* Q16 linear per block; 65536 = instant */
	long decay_rate;		/* attenuation << 8 per block; 0 = none */
	long sustain_att;		/* attenuation << 8 */
	long release_rate;
} Prog;

typedef struct {
	unsigned char inst;		/* 0xFF = silent */
	unsigned char key;
	unsigned char excl;
	long vol_q15;
	long decay_rate;
} Drum;

typedef struct {
	unsigned short first, count;
} Inst;

typedef struct {
	unsigned char lo, hi, root, flags;
	int fine64;			/* fine tune in 1/64 semitone */
	int gain;			/* 128 = unity */
	long decay_rate;
	unsigned long base_step;	/* (rate << 12) / out_rate: 12-bit fraction */
	const signed char *data;
	unsigned long length, loop_start, loop_end;
} Zone;

typedef struct {
	unsigned char prog;
	unsigned char volume, expression, sustain;
	int bend;			/* 1/64 semitone */
	int bend_raw;
	int range64;			/* bend range, 1/64 semitone */
	int rpn_msb, rpn_lsb;
	long q15;			/* volume^2 * expression^2 */
} Channel;

typedef struct {
	unsigned char active, ch, note, flags, excl, stage;
	const signed char *data;
	unsigned long pos, step;	/* 16.16 */
	unsigned long end_fp;		/* loop end (looping) or length, << FRAC */
	unsigned long loop_len_fp, loop_start_fp;
	unsigned long base_step;
	long p64;			/* pitch without bend, 1/64 semitone */
	long env_lin;			/* Q16, attack only */
	long att;			/* attenuation << 8 */
	long attack_inc, decay_rate, sustain_att, release_rate;
	long static_q14;		/* velocity * volume * zone gain */
	long g;				/* current gain, Q14 << 16 */
	unsigned long age;
} Voice;

typedef struct {
	MidiSynth iface;		/* first: the MidiSynth handed out IS the ctx */
	int rate;
	int nvoices;
	int interp;
	int master;
	unsigned long clock;
	Prog prog[128];
	Drum drum[128];
	Inst *inst;
	int ninst;
	Zone *zone;
	int nzone;
	Channel chan[16];
	Voice voice[MAXVOICES];
	long acc[BLOCK];
	short out[BLOCK];
	int out_pos;			/* next unread sample of out[]; BLOCK = empty */
} Synth;

/* ---- bank parsing ------------------------------------------------------- */

static unsigned long rd16(const unsigned char *p)
{
	return ((unsigned long)p[0] << 8) | p[1];
}

static unsigned long rd32(const unsigned char *p)
{
	return ((unsigned long)p[0] << 24) | ((unsigned long)p[1] << 16) |
	       ((unsigned long)p[2] << 8) | p[3];
}

/* ms for a 96 dB fall -> attenuation << 8 per block */
static long fall_rate(Synth *s, unsigned long ms)
{
	unsigned long samples;
	if (ms == 0)
		return 0;
	samples = (ms * (unsigned long)(s->rate / 10)) / 100;
	if (samples < BLOCK)
		return (long)ATT_SILENT << 8;
	return (long)(((unsigned long)ATT_SILENT << 8) * BLOCK / samples);
}

static long rise_inc(Synth *s, unsigned long ms)
{
	unsigned long samples = (ms * (unsigned long)(s->rate / 10)) / 100;
	if (samples < BLOCK)
		return 65536;
	return (long)((65536UL * BLOCK) / samples);
}

/* 0..127 -> Q15 on the GM square-law curve */
static long sq_q15(int v)
{
	if (v < 0) v = 0;
	if (v > 127) v = 127;
	return ((long)v * v * 32767L) / 16129L;
}

/* 0..127 -> Q15, linear: the bank's own program and drum volumes, which are
 * balance factors set by the bank builder rather than MIDI controller values */
static long lin_q15(int v)
{
	if (v > 127) v = 127;
	return ((long)v * 32767L) / 127L;
}

static int parse_bank(Synth *s, const unsigned char *b, long len)
{
	unsigned long prog_off, drum_off, inst_off, zone_off, data_off, data_len;
	const signed char *data;
	int i;

	if (len < 48 || memcmp(b, GM_BANK_MAGIC, 4) != 0)
		return 0;
	if (rd16(b + 4) != GM_BANK_VERSION || rd16(b + 6) != 48)
		return 0;
	s->ninst = (int)rd16(b + 8);
	s->nzone = (int)rd16(b + 10);
	prog_off = rd32(b + 12);
	drum_off = rd32(b + 16);
	inst_off = rd32(b + 20);
	zone_off = rd32(b + 24);
	data_off = rd32(b + 28);
	data_len = rd32(b + 32);
	if (s->ninst < 1 || s->ninst > 255 || s->nzone < 1)
		return 0;
	if (prog_off + 128 * 12 > (unsigned long)len || drum_off + 128 * 8 > (unsigned long)len ||
	    inst_off + (unsigned long)s->ninst * 20 > (unsigned long)len ||
	    zone_off + (unsigned long)s->nzone * 28 > (unsigned long)len ||
	    data_off > (unsigned long)len || data_len > (unsigned long)len - data_off)
		return 0;
	data = (const signed char *)(b + data_off);

	s->inst = (Inst *)malloc(sizeof(Inst) * s->ninst);
	s->zone = (Zone *)malloc(sizeof(Zone) * s->nzone);
	if (!s->inst || !s->zone)
		return 0;

	for (i = 0; i < s->nzone; i++) {
		const unsigned char *p = b + zone_off + i * 28;
		Zone *z = &s->zone[i];
		unsigned long rate = rd16(p + 4), off = rd32(p + 12);
		z->lo = p[0];
		z->hi = p[1];
		z->root = p[2];
		z->fine64 = ((signed char)p[3]) * 64 / 100;
		z->gain = p[6];
		z->flags = p[7];
		z->decay_rate = fall_rate(s, rd16(p + 8));
		z->length = rd32(p + 16);
		z->loop_start = rd32(p + 20);
		z->loop_end = rd32(p + 24);
		if (rate < 4000 || rate > 32000 || z->lo > z->hi || z->hi > 127 || z->root > 127)
			return 0;
		if (z->length < 1 || z->length >= MAXFRAMES || off > data_len ||
		    z->length + 1 > data_len - off)
			return 0;
		if ((z->flags & ZF_LOOP) &&
		    (z->loop_start >= z->loop_end || z->loop_end > z->length))
			return 0;
		z->data = data + off;
		z->base_step = (rate << 12) / (unsigned long)s->rate;
	}
	for (i = 0; i < s->ninst; i++) {
		const unsigned char *p = b + inst_off + i * 20;
		s->inst[i].first = (unsigned short)rd16(p + 16);
		s->inst[i].count = (unsigned short)rd16(p + 18);
		if (s->inst[i].count < 1 || (long)s->inst[i].first + s->inst[i].count > s->nzone)
			return 0;
	}
	for (i = 0; i < 128; i++) {
		const unsigned char *p = b + prog_off + i * 12;
		Prog *g = &s->prog[i];
		g->inst = p[0];
		g->transpose = (signed char)p[1];
		g->vol_q15 = lin_q15(p[2]);
		g->attack_inc = rise_inc(s, rd16(p + 4));
		g->decay_rate = fall_rate(s, rd16(p + 6));
		g->sustain_att = (long)rd16(p + 8) << 8;
		g->release_rate = fall_rate(s, rd16(p + 10));
		if (g->release_rate == 0)
			g->release_rate = fall_rate(s, 10);
		if (g->inst >= s->ninst)
			return 0;
	}
	for (i = 0; i < 128; i++) {
		const unsigned char *p = b + drum_off + i * 8;
		Drum *d = &s->drum[i];
		d->inst = p[0];
		d->key = p[1];
		d->vol_q15 = lin_q15(p[2]);
		d->excl = p[3];
		d->decay_rate = fall_rate(s, rd16(p + 4));
		if (d->inst != 0xFF && d->inst >= s->ninst)
			return 0;
		if (d->key > 127)
			return 0;
	}
	return 1;
}

/* ---- pitch and gain ------------------------------------------------------- */

/* base: 12-bit fraction; result: 16.16 */
static unsigned long calc_step(unsigned long base, long p64)
{
	long oct = p64 / 768, r = p64 % 768;
	unsigned long st;
	if (r < 0) {
		r += 768;
		oct--;
	}
	st = (base * semi_tab[r >> 6]) >> 16;
	st = (st * fine_tab[r & 63]) >> 16;
	oct += FRAC - 12;
	if (oct > 0) {
		if (oct > 12)
			return MAXSTEP;
		st <<= oct;
	} else if (oct < 0) {
		st = oct < -24 ? 0 : st >> -oct;
	}
	if (st < 1)
		st = 1;
	if (st > MAXSTEP)
		st = MAXSTEP;
	return st;
}

/* attenuation (1/256 octave) -> Q15 */
static long att_gain(long att)
{
	if (att >= ATT_SILENT)
		return 0;
	return (long)att_tab[(att >> 2) & 63] >> (att >> 8);
}

/* Q15 -> attenuation (1/256 octave): only on a note-off during the attack */
static long gain_att(long q15)
{
	long oct = 0;
	int i;
	if (q15 <= 0)
		return ATT_SILENT;
	while (q15 < 16384) {
		q15 <<= 1;
		oct++;
	}
	for (i = 0; i < 63 && att_tab[i + 1] >= q15; i++)
		;
	return oct * 256 + i * 4;
}

static void chan_update(Channel *c)
{
	long v = (long)c->volume * c->expression;	/* <= 16129 */
	long q = (v * v) / 7939L;			/* 16129^2 / 32767 */
	c->q15 = q > 32767 ? 32767 : q;
}

static void chan_reset(Channel *c)
{
	c->expression = 127;
	c->sustain = 0;
	c->bend = 0;
	c->bend_raw = 8192;
	c->rpn_msb = c->rpn_lsb = 127;
	chan_update(c);
}

/* ---- voices ------------------------------------------------------------- */

static void voice_release(Voice *v)
{
	if (v->stage == ENV_ATTACK)
		v->att = gain_att(v->env_lin >> 1) << 8;
	v->stage = ENV_RELEASE;
	v->flags |= VF_RELEASED;
	v->flags &= ~VF_SUSTAINED;
}

static Voice *voice_alloc(Synth *s)
{
	Voice *best = 0;
	int i;
	for (i = 0; i < s->nvoices; i++)
		if (!s->voice[i].active)
			return &s->voice[i];
	/* the quietest voice already in its release */
	for (i = 0; i < s->nvoices; i++) {
		Voice *v = &s->voice[i];
		if (v->stage == ENV_RELEASE && (!best || v->g < best->g))
			best = v;
	}
	if (best)
		return best;
	/* else the oldest */
	best = &s->voice[0];
	for (i = 1; i < s->nvoices; i++)
		if (s->voice[i].age < best->age)
			best = &s->voice[i];
	return best;
}

static const Zone *find_zone(Synth *s, int inst, int key)
{
	const Inst *in = &s->inst[inst];
	int i;
	for (i = 0; i < in->count; i++) {
		const Zone *z = &s->zone[in->first + i];
		if (key >= z->lo && key <= z->hi)
			return z;
	}
	return 0;
}

static void voice_start(Synth *s, Voice *v, const Zone *z, int ch, int note, int key,
                        long vol_q15, int vel)
{
	long vel_q15 = sq_q15(vel);
	v->active = 1;
	v->ch = (unsigned char)ch;
	v->note = (unsigned char)note;
	v->excl = 0;
	v->flags = 0;
	if (z->flags & ZF_LOOP)
		v->flags |= VF_LOOP;
	if (z->flags & ZF_TAIL)
		v->flags |= VF_TAIL;
	v->data = z->data;
	v->pos = 0;
	if (v->flags & VF_LOOP) {
		v->end_fp = z->loop_end << FRAC;
		v->loop_start_fp = z->loop_start << FRAC;
		v->loop_len_fp = (z->loop_end - z->loop_start) << FRAC;
	} else {
		v->end_fp = z->length << FRAC;
		v->loop_start_fp = v->end_fp;
		v->loop_len_fp = 0;
	}
	v->base_step = z->base_step;
	v->p64 = (long)(key - z->root) * 64 + z->fine64;
	v->step = calc_step(v->base_step, v->p64 + s->chan[ch].bend);
	v->static_q14 = (((vel_q15 * vol_q15) >> 15) * z->gain) >> 8;
	v->g = 0;
	v->att = 0;
	v->env_lin = 0;
	v->age = s->clock++;
}

static void note_on_drum(Synth *s, int note, int vel)
{
	const Drum *d = &s->drum[note];
	const Zone *z;
	Voice *v;
	int i;
	if (d->inst == 0xFF)
		return;
	z = find_zone(s, d->inst, d->key);
	if (!z)
		return;
	if (d->excl) {
		for (i = 0; i < s->nvoices; i++) {
			Voice *o = &s->voice[i];
			if (o->active && (o->flags & VF_DRUM) && o->excl == d->excl &&
			    o->stage != ENV_RELEASE) {
				voice_release(o);
				o->release_rate = fall_rate(s, CHOKE_MS);
			}
		}
	}
	v = voice_alloc(s);
	voice_start(s, v, z, 9, note, d->key, d->vol_q15, vel);
	v->flags |= VF_DRUM;
	v->excl = d->excl;
	v->attack_inc = 65536;
	v->env_lin = 65536;
	v->decay_rate = d->decay_rate;
	v->sustain_att = d->decay_rate ? (long)ATT_SILENT << 8 : 0;
	v->release_rate = fall_rate(s, CHOKE_MS);
	v->stage = d->decay_rate ? ENV_DECAY : ENV_SUSTAIN;
}

static void syn_note_off(void *ctx, int ch, int note);

static void syn_note_on(void *ctx, int ch, int note, int vel)
{
	Synth *s = (Synth *)ctx;
	const Prog *g;
	const Zone *z;
	Voice *v;
	int key, i;

	ch &= 15;
	note &= 127;
	if (vel <= 0) {
		syn_note_off(ctx, ch, note);
		return;
	}
	if (vel > 127)
		vel = 127;
	if (ch == 9) {
		note_on_drum(s, note, vel);
		return;
	}
	g = &s->prog[s->chan[ch].prog];
	key = note + g->transpose;
	if (key < 0) key = 0;
	if (key > 127) key = 127;
	z = find_zone(s, g->inst, key);
	if (!z)
		return;
	/* the same key again on the same channel: let the old one go */
	for (i = 0; i < s->nvoices; i++) {
		Voice *o = &s->voice[i];
		if (o->active && o->ch == ch && o->note == note && o->stage != ENV_RELEASE)
			voice_release(o);
	}
	v = voice_alloc(s);
	voice_start(s, v, z, ch, note, key, g->vol_q15, vel);
	v->attack_inc = g->attack_inc;
	v->release_rate = g->release_rate;
	if (v->flags & VF_TAIL) {
		v->decay_rate = g->decay_rate ? g->decay_rate : z->decay_rate;
		if (v->decay_rate == 0)
			v->decay_rate = fall_rate(s, 2000);
		v->sustain_att = (long)ATT_SILENT << 8;
	} else {
		v->decay_rate = g->decay_rate;
		v->sustain_att = g->sustain_att;
	}
	if (v->attack_inc >= 65536) {
		v->env_lin = 65536;
		v->stage = (v->flags & VF_TAIL) ? ENV_HOLD : ENV_DECAY;
	} else {
		v->stage = ENV_ATTACK;
	}
}

static void syn_note_off(void *ctx, int ch, int note)
{
	Synth *s = (Synth *)ctx;
	int i;
	ch &= 15;
	note &= 127;
	if (ch == 9)
		return;
	for (i = 0; i < s->nvoices; i++) {
		Voice *v = &s->voice[i];
		if (!v->active || v->ch != ch || v->note != note || (v->flags & VF_RELEASED))
			continue;
		if (s->chan[ch].sustain)
			v->flags |= VF_SUSTAINED | VF_RELEASED;
		else
			voice_release(v);
	}
}

static void syn_program(void *ctx, int ch, int prog)
{
	Synth *s = (Synth *)ctx;
	ch &= 15;
	if (ch != 9)
		s->chan[ch].prog = (unsigned char)(prog & 127);
}

static void bend_update(Synth *s, int ch)
{
	Channel *c = &s->chan[ch];
	int i;
	c->bend = (int)(((long)(c->bend_raw - 8192) * c->range64) / 8192);
	for (i = 0; i < s->nvoices; i++) {
		Voice *v = &s->voice[i];
		if (v->active && v->ch == ch && !(v->flags & VF_DRUM))
			v->step = calc_step(v->base_step, v->p64 + c->bend);
	}
}

static void syn_pitch_bend(void *ctx, int ch, int value)
{
	Synth *s = (Synth *)ctx;
	ch &= 15;
	if (value < 0) value = 0;
	if (value > 16383) value = 16383;
	s->chan[ch].bend_raw = value;
	bend_update(s, ch);
}

static void syn_control(void *ctx, int ch, int cc, int val)
{
	Synth *s = (Synth *)ctx;
	Channel *c;
	int i;
	ch &= 15;
	c = &s->chan[ch];
	val &= 127;
	switch (cc) {
	case 6:		/* data entry MSB: RPN 0 = bend range semitones */
		if (c->rpn_msb == 0 && c->rpn_lsb == 0) {
			c->range64 = val * 64 + (c->range64 % 64);
			bend_update(s, ch);
		}
		break;
	case 38:	/* data entry LSB: bend range cents */
		if (c->rpn_msb == 0 && c->rpn_lsb == 0) {
			c->range64 = (c->range64 / 64) * 64 + val * 64 / 100;
			bend_update(s, ch);
		}
		break;
	case 7:
		c->volume = (unsigned char)val;
		chan_update(c);
		break;
	case 11:
		c->expression = (unsigned char)val;
		chan_update(c);
		break;
	case 64:
		c->sustain = val >= 64;
		if (!c->sustain)
			for (i = 0; i < s->nvoices; i++) {
				Voice *v = &s->voice[i];
				if (v->active && v->ch == ch && (v->flags & VF_SUSTAINED))
					voice_release(v);
			}
		break;
	case 100:
		c->rpn_lsb = val;
		break;
	case 101:
		c->rpn_msb = val;
		break;
	case 120:	/* all sound off */
		for (i = 0; i < s->nvoices; i++)
			if (s->voice[i].ch == ch)
				s->voice[i].active = 0;
		break;
	case 121:	/* reset all controllers */
		chan_reset(c);
		for (i = 0; i < s->nvoices; i++) {
			Voice *v = &s->voice[i];
			if (v->active && v->ch == ch && (v->flags & VF_SUSTAINED))
				voice_release(v);
		}
		bend_update(s, ch);
		break;
	case 123:	/* all notes off */
		for (i = 0; i < s->nvoices; i++) {
			Voice *v = &s->voice[i];
			if (v->active && v->ch == ch && !(v->flags & VF_DRUM) && v->stage != ENV_RELEASE)
				voice_release(v);
		}
		break;
	default:	/* 10 pan and everything else: ignored */
		break;
	}
}

static void syn_all_off(void *ctx)
{
	Synth *s = (Synth *)ctx;
	int i;
	for (i = 0; i < MAXVOICES; i++)
		s->voice[i].active = 0;
	for (i = 0; i < 16; i++)
		s->chan[i].sustain = 0;
}

/* ---- rendering ------------------------------------------------------------ */

/* One control step: advance the envelope, return the Q14 gain the block
 * should END on, or -1 when the voice has finished. */
static long envelope(Synth *s, Voice *v)
{
	long lin;
	switch (v->stage) {
	case ENV_ATTACK:
		v->env_lin += v->attack_inc;
		if (v->env_lin >= 65536) {
			v->env_lin = 65536;
			v->att = 0;
			v->stage = (v->flags & VF_TAIL) ? ENV_HOLD : ENV_DECAY;
		}
		lin = v->env_lin >> 1;
		break;
	case ENV_HOLD:
		if (v->pos >= v->loop_start_fp)
			v->stage = ENV_DECAY;
		lin = att_gain(v->att >> 8);
		break;
	case ENV_DECAY:
		if (v->decay_rate == 0)
			v->att = v->sustain_att;
		else
			v->att += v->decay_rate;
		if (v->att >= v->sustain_att) {
			v->att = v->sustain_att;
			v->stage = ENV_SUSTAIN;
		}
		lin = att_gain(v->att >> 8);
		break;
	case ENV_SUSTAIN:
		lin = att_gain(v->att >> 8);
		break;
	default:	/* ENV_RELEASE */
		v->att += v->release_rate;
		lin = att_gain(v->att >> 8);
		break;
	}
	if ((v->att >> 8) >= ATT_OFF && v->stage != ENV_ATTACK)
		return -1;
	return (((v->static_q14 * s->chan[v->ch].q15) >> 15) * lin) >> 15;
}

/* Mix n (>= 1) samples of v into acc, the gain ramping from v->g by dg per
 * sample. n is chosen so that pos never passes end_fp inside the loop.
 * Written for the 68020: pos >> 16 and g >> 16 are each a SWAP, every
 * product is (short) x (short) so gcc emits MULS.W, and the loops count down
 * to zero. Out of line on purpose -- inlined into the block loop they lose
 * their registers to it. */
static NOINLINE void mix_nearest(Voice *v, long *acc, int n, long dg)
{
	const signed char *d = v->data;
	unsigned long pos = v->pos, step = v->step;
	long g = v->g;
	short k = (short)(n - 1);
	if (dg == 0) {
		long gs = (long)((unsigned long)g >> 16);
		do {
			*acc++ += mul16(d[pos >> 16], gs);
			pos += step;
		} while (--k >= 0);
	} else {
		do {
			*acc++ += mul16(d[pos >> 16], (long)((unsigned long)g >> 16));
			pos += step;
			g += dg;
		} while (--k >= 0);
	}
	v->pos = pos;
	v->g = g;
}

static NOINLINE void mix_interp(Voice *v, long *acc, int n, long dg)
{
	const signed char *d = v->data;
	unsigned long pos = v->pos, step = v->step;
	long g = v->g;
	short k = (short)(n - 1);
	do {
		const signed char *p = d + (pos >> 16);
		long s0 = p[0];
		long smp = s0 * 256 + mul16(p[1] - s0, (pos >> 8) & 255);
		*acc++ += mul16(smp, (long)((unsigned long)g >> 16)) >> 8;
		pos += step;
		g += dg;
	} while (--k >= 0);
	v->pos = pos;
	v->g = g;
}

static void voice_block(Synth *s, Voice *v)
{
	long target = envelope(s, v), dg;
	long *acc = s->acc;
	int left = BLOCK, dead = 0;

	if (target < 0) {
		target = 0;
		dead = 1;
	}
	/* arithmetic shift: both compilers this builds with shift signed right arithmetically */
	dg = ((target << 16) - v->g) >> BLOCK_SHIFT;
	while (left > 0) {
		unsigned long cnt;
		while (v->pos >= v->end_fp) {
			if (!(v->flags & VF_LOOP)) {
				v->active = 0;
				return;
			}
			v->pos -= v->loop_len_fp;
		}
		cnt = (v->end_fp - v->pos + v->step - 1) / v->step;
		if (cnt > (unsigned long)left)
			cnt = left;
		if (s->interp)
			mix_interp(v, acc, (int)cnt, dg);
		else
			mix_nearest(v, acc, (int)cnt, dg);
		acc += cnt;
		left -= (int)cnt;
	}
	v->g = target << 16;
	if (dead)
		v->active = 0;
}

static void render_block(Synth *s)
{
	int i;
	memset(s->acc, 0, sizeof(s->acc));
	for (i = 0; i < s->nvoices; i++)
		if (s->voice[i].active)
			voice_block(s, &s->voice[i]);
	for (i = 0; i < BLOCK; i++) {
		long x = ((s->acc[i] >> 7) * s->master) >> 8;
		if (x > 32767) x = 32767;
		if (x < -32768) x = -32768;
		s->out[i] = (short)x;
	}
	s->out_pos = 0;
}

static void syn_render(void *ctx, short *out, int n)
{
	Synth *s = (Synth *)ctx;
	while (n > 0) {
		int k;
		if (s->out_pos >= BLOCK)
			render_block(s);
		k = BLOCK - s->out_pos;
		if (k > n)
			k = n;
		memcpy(out, s->out + s->out_pos, k * sizeof(short));
		s->out_pos += k;
		out += k;
		n -= k;
	}
}

static void syn_destroy(void *ctx)
{
	Synth *s = (Synth *)ctx;
	if (!s)
		return;
	free(s->inst);
	free(s->zone);
	free(s);
}

/* ---- public ------------------------------------------------------------- */

MidiSynth *gm_synth_create(int rate, const unsigned char *bank, long len, int voices)
{
	Synth *s;
	int i;
	if (!bank || rate < 8000 || rate > 48000)
		return 0;
	s = (Synth *)malloc(sizeof(Synth));
	if (!s)
		return 0;
	memset(s, 0, sizeof(*s));
	s->rate = rate;
	if (!parse_bank(s, bank, len)) {
		syn_destroy(s);
		return 0;
	}
	if (voices < 1) voices = 1;
	if (voices > MAXVOICES) voices = MAXVOICES;
	s->nvoices = voices;
	s->interp = 1;
	s->master = 256;
	s->out_pos = BLOCK;
	for (i = 0; i < 16; i++) {
		s->chan[i].volume = 100;
		s->chan[i].range64 = 2 * 64;
		chan_reset(&s->chan[i]);
	}
	s->iface.ctx = s;
	s->iface.note_on = syn_note_on;
	s->iface.note_off = syn_note_off;
	s->iface.program = syn_program;
	s->iface.control = syn_control;
	s->iface.pitch_bend = syn_pitch_bend;
	s->iface.all_off = syn_all_off;
	s->iface.render = syn_render;
	s->iface.destroy = syn_destroy;
	return &s->iface;
}

void gm_synth_set_interp(MidiSynth *m, int on)
{
	if (m)
		((Synth *)m->ctx)->interp = on ? 1 : 0;
}

void gm_synth_set_master(MidiSynth *m, int gain256)
{
	if (!m)
		return;
	if (gain256 < 0) gain256 = 0;
	if (gain256 > 4096) gain256 = 4096;
	((Synth *)m->ctx)->master = gain256;
}

int gm_synth_active_voices(MidiSynth *m)
{
	Synth *s;
	int i, n = 0;
	if (!m)
		return 0;
	s = (Synth *)m->ctx;
	for (i = 0; i < s->nvoices; i++)
		n += s->voice[i].active;
	return n;
}
