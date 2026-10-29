/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 angree. See LICENSE at the repository root.
 *
 * YM3812 (OPL2) FM synthesis, integer only. Written for this port from the
 * chip's public description (Yamaha YM3812 application manual, the AdLib
 * programming guide) -- no emulator's code is copied. Interface in opl2.h.
 *
 * ---- what the chip does, and how this file models it --------------------
 *
 * Nine channels of two operators (modulator, carrier). An operator is a
 * phase accumulator driving a sine looked up in the LOG domain: the chip
 * stores -log2(sin) of a quarter wave (256 entries, 1/256-octave units),
 * ADDS the attenuation (envelope + total level + key scaling + tremolo, in
 * 0.1875 dB steps = 8 log units) and turns the sum back into a linear value
 * through a 256-entry 2^-x table plus a shift. No multiply anywhere -- which
 * is exactly why the scheme is cheap on a 68020 too. opl_logsin/opl_exp below
 * are those two tables, computed on the host from the formulas next to them
 * (tools/music/opltest --check-tables recomputes them with libm and compares).
 *
 * Everything else is rebuilt once per chip into two RAM tables:
 *   logw[4][1024]   one whole cycle of each of the four OPL2 waveforms, the
 *                   log value with the SIGN in bit 13 (zero = a log value large
 *                   enough that the exp step shifts it to nothing),
 *   expsig[16384]   log value + attenuation (+ sign bit) -> signed output.
 * So one operator sample is: index the wave, add the attenuation, index the
 * exp table. Four instructions of real work. 40 KB per chip.
 *
 * Phase: a 32-bit accumulator whose top 10 bits are the wave index. The chip's
 * own 19-bit increment is ((fnum << block) >> 1) * multiple; it is scaled by
 * kphase = 8192 (exactly the chip, quality 1) or 8192 * 49716 / rate (quality
 * 0, computed at the output rate). Unsigned overflow is the wraparound.
 *
 * Modulation: the carrier's wave index is offset by the modulator's output
 * (+-4095 = +-8 pi); the modulator's own feedback is (last two outputs summed)
 * >> (9 - FB), which puts FB=1 at pi/16 and FB=7 at 4 pi -- the datasheet's
 * figures. CON=1 adds the two operators instead.
 *
 * Envelope: attenuation 0..511 (0.1875 dB steps), kept in Q16 here so that
 * the quality 0 path can add fractional per-block increments. Effective rate
 * = 4 * R + key scale offset (0..63). Rates 4..51 step every 2^(12 - rate/4)
 * chip samples with an 8-step pattern averaging 4/8, 5/8, 6/8, 7/8; rates
 * 52..59 step every sample by 1..2 / 2..4; 60..63 move 4 per sample and make
 * attack instantaneous. Decay with R=1 thus takes ~42 s to silence and R=15
 * 2.6 ms, against the datasheet's 39.3 s and 2.4 ms. Attack follows the chip's
 * exponential law (att -= (att + 1) * inc / 8). Sustain level SL is 3 dB per
 * step, SL=15 meaning 93 dB. EG-TYP=1 holds at the sustain level while the key
 * is down; EG-TYP=0 keeps falling at the release rate (percussive sounds).
 *
 * Key scale level: the chip's 16-entry table over the top four F-number bits,
 * minus 6 dB per octave below block 8, then shifted by 0/1/2/8 for the
 * register's 6 / 3 / 1.5 / 0 dB-per-octave settings (note the register's
 * odd order: KSL=1 is 3 dB, KSL=2 is 1.5 dB, KSL=3 is 6 dB).
 *
 * LFOs: tremolo is a triangle of 210 steps, one step per 64 chip samples
 * (3.7 Hz), depth 26 or 6 attenuation units (4.8 / 1 dB, register 0xBD bit 7);
 * vibrato an 8-step pattern, one step per 1024 chip samples (6.07 Hz), worth
 * (fnum >> 7) F-number units at the peak, halved when 0xBD bit 6 is clear.
 *
 * ---- the two render paths -------------------------------------------------
 *
 * quality 1: one chip sample at a time at 49716 Hz -- LFO step, envelope tick
 * for every operator, both operators of every live channel -- and the chip
 * stream is box-filtered (exact area average, fractional at both edges) down
 * to the output rate. That average is the anti-alias filter: from 49716 Hz to
 * 22050 Hz it removes most of what would otherwise fold back.
 *
 * quality 0: the operators run directly at the output rate. Once per
 * OPL2_BLOCK output samples the LFOs and envelopes jump by the number of chip
 * samples that block stands for (averaged rates, attack linearised over the
 * block), and every operator's attenuation and increment is frozen for the
 * block. The inner loop is then per channel, per sample: two phase adds, two
 * wave lookups, two exp lookups, the feedback shift. See the cost note at
 * chan_q0().
 *
 * Not implemented: rhythm mode, timers, CSM.
 */
#include <stdlib.h>
#include <string.h>

#include "opl2.h"

/* round(-log2(sin((i + 0.5) * pi / 512)) * 256), i = 0..255 */
static const unsigned short opl_logsin[256] = {
	2137, 1731, 1543, 1419, 1326, 1252, 1190, 1137, 1091, 1050, 1013, 979,
	949, 920, 894, 869, 846, 825, 804, 785, 767, 749, 732, 717,
	701, 687, 672, 659, 646, 633, 621, 609, 598, 587, 576, 566,
	556, 546, 536, 527, 518, 509, 501, 492, 484, 476, 468, 461,
	453, 446, 439, 432, 425, 418, 411, 405, 399, 392, 386, 380,
	375, 369, 363, 358, 352, 347, 341, 336, 331, 326, 321, 316,
	311, 307, 302, 297, 293, 289, 284, 280, 276, 271, 267, 263,
	259, 255, 251, 248, 244, 240, 236, 233, 229, 226, 222, 219,
	215, 212, 209, 205, 202, 199, 196, 193, 190, 187, 184, 181,
	178, 175, 172, 169, 167, 164, 161, 159, 156, 153, 151, 148,
	146, 143, 141, 138, 136, 134, 131, 129, 127, 125, 122, 120,
	118, 116, 114, 112, 110, 108, 106, 104, 102, 100, 98, 96,
	94, 92, 91, 89, 87, 85, 83, 82, 80, 78, 77, 75,
	74, 72, 70, 69, 67, 66, 64, 63, 62, 60, 59, 57,
	56, 55, 53, 52, 51, 49, 48, 47, 46, 45, 43, 42,
	41, 40, 39, 38, 37, 36, 35, 34, 33, 32, 31, 30,
	29, 28, 27, 26, 25, 24, 23, 23, 22, 21, 20, 20,
	19, 18, 17, 17, 16, 15, 15, 14, 13, 13, 12, 12,
	11, 10, 10, 9, 9, 8, 8, 7, 7, 7, 6, 6,
	5, 5, 5, 4, 4, 4, 3, 3, 3, 2, 2, 2,
	2, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0,
	0, 0, 0, 0,
};
/* round(2048 * 2^((255 - i) / 256)), i = 0..255 */
static const unsigned short opl_exp[256] = {
	4085, 4074, 4063, 4052, 4041, 4030, 4019, 4008, 3997, 3987, 3976, 3965,
	3954, 3944, 3933, 3922, 3912, 3901, 3891, 3880, 3870, 3859, 3849, 3838,
	3828, 3818, 3807, 3797, 3787, 3776, 3766, 3756, 3746, 3736, 3726, 3716,
	3706, 3696, 3686, 3676, 3666, 3656, 3646, 3636, 3626, 3616, 3607, 3597,
	3587, 3577, 3568, 3558, 3548, 3539, 3529, 3520, 3510, 3501, 3491, 3482,
	3472, 3463, 3454, 3444, 3435, 3426, 3416, 3407, 3398, 3389, 3380, 3371,
	3361, 3352, 3343, 3334, 3325, 3316, 3307, 3298, 3289, 3280, 3272, 3263,
	3254, 3245, 3236, 3228, 3219, 3210, 3201, 3193, 3184, 3176, 3167, 3158,
	3150, 3141, 3133, 3124, 3116, 3108, 3099, 3091, 3082, 3074, 3066, 3057,
	3049, 3041, 3033, 3025, 3016, 3008, 3000, 2992, 2984, 2976, 2968, 2960,
	2952, 2944, 2936, 2928, 2920, 2912, 2904, 2896, 2888, 2881, 2873, 2865,
	2857, 2850, 2842, 2834, 2827, 2819, 2811, 2804, 2796, 2789, 2781, 2774,
	2766, 2759, 2751, 2744, 2736, 2729, 2721, 2714, 2707, 2699, 2692, 2685,
	2678, 2670, 2663, 2656, 2649, 2642, 2634, 2627, 2620, 2613, 2606, 2599,
	2592, 2585, 2578, 2571, 2564, 2557, 2550, 2543, 2536, 2530, 2523, 2516,
	2509, 2502, 2496, 2489, 2482, 2475, 2469, 2462, 2455, 2449, 2442, 2435,
	2429, 2422, 2416, 2409, 2403, 2396, 2390, 2383, 2377, 2370, 2364, 2358,
	2351, 2345, 2339, 2332, 2326, 2320, 2313, 2307, 2301, 2295, 2288, 2282,
	2276, 2270, 2264, 2258, 2252, 2245, 2239, 2233, 2227, 2221, 2215, 2209,
	2203, 2197, 2191, 2186, 2180, 2174, 2168, 2162, 2156, 2150, 2144, 2139,
	2133, 2127, 2121, 2116, 2110, 2104, 2099, 2093, 2087, 2082, 2076, 2070,
	2065, 2059, 2054, 2048,
};
/* the chip's key scale level table over fnum >> 6, in 0.75 dB units */
static const unsigned char opl_kslrom[16] = {
	0, 32, 40, 45, 48, 51, 53, 55, 56, 58, 59, 60, 61, 62, 63, 64
};
/* multiple register 0..15 -> 2 * multiplier (0 means x0.5) */
static const unsigned char opl_mt2[16] = {
	1, 2, 4, 6, 8, 10, 12, 14, 16, 18, 20, 20, 24, 24, 30, 30
};
/* KSL register 0..3 -> shift of the 6 dB/oct value: off, 3, 1.5, 6 dB/oct */
static const unsigned char opl_kslshift[4] = { 31, 1, 2, 0 };
/* envelope step patterns: rates 4..51 (0 or 1 per step) and 52..55 (1 or 2) */
static const unsigned char opl_egpat[4][8] = {
	{ 0, 1, 0, 1, 0, 1, 0, 1 }, { 0, 1, 0, 1, 1, 1, 0, 1 },
	{ 0, 1, 1, 1, 0, 1, 1, 1 }, { 0, 1, 1, 1, 1, 1, 1, 1 }
};
static const unsigned char opl_egpat2[4][8] = {
	{ 1, 1, 1, 1, 1, 1, 1, 1 }, { 1, 1, 1, 2, 1, 1, 1, 2 },
	{ 1, 2, 1, 2, 1, 2, 1, 2 }, { 1, 2, 2, 2, 1, 2, 2, 2 }
};

/* the phase arithmetic wraps at 2^32: int is 32 bits on both compilers */
typedef unsigned int u32;

#define LOG_ZERO	3400	/* >= 13 * 256: the exp shift leaves nothing */
#define LOG_SIGN	0x2000
#define ATT_MAX		511
#define Q16		16

enum { EG_OFF, EG_ATT, EG_DEC, EG_SUS, EG_REL };

typedef struct {
	/* registers */
	unsigned char am, vib, egt, ksr, mult;	/* 0x20 */
	unsigned char ksl, tl;			/* 0x40 */
	unsigned char ar, dr, sl, rr;		/* 0x60, 0x80 */
	unsigned char ws;			/* 0xE0 */
	/* derived from registers + channel frequency */
	unsigned char r_att, r_dec, r_rel;	/* effective rates 0..63 */
	int base_att;				/* TL * 4 + KSL, attenuation units */
	int sl_att;				/* sustain level, attenuation units */
	u32 inc19;				/* chip phase increment, no vibrato */
	/* state */
	int stage;
	long att;				/* Q16, 0 .. 511 << 16 */
	u32 phase;				/* top 10 bits = wave index */
	u32 inc;				/* per output (q0) / chip (q1) sample */
	int tot8;				/* frozen total attenuation << 3 */
} Op;

typedef struct {
	int fnum, block, key, fb, con;
	int out0, out1;				/* modulator's last two outputs */
	Op op[2];
} Chan;

struct Opl2 {
	int rate, quality, gain;
	int wse, nts, dam, dvb;
	u32 kphase;				/* chip inc19 -> our phase inc */
	unsigned long tick;			/* chip samples since reset */
	unsigned long tick_frac;		/* q0: Q16 fraction of a chip sample */
	unsigned long tpb_q16;			/* q0: chip samples per block, Q16 */
	unsigned long step_q16;			/* q1: chip samples per output sample, Q16 */
	unsigned long eg_blk[64];		/* q0: attenuation per block per rate, Q16 */
	int trem;				/* current tremolo, attenuation units */
	int vibpos;				/* 0..7 */
	int blkleft;				/* q0: samples left in the current block */
	long cur;				/* q1: current chip sample */
	unsigned long cur_left;			/* q1: Q16 of it not yet consumed */
	Chan ch[9];
	short logw[4][1024];
	short expsig[16384];
};

/* ---- tables ------------------------------------------------------------- */

static void build_tables(Opl2 *o)
{
	int i, w;

	for (i = 0; i < 1024; i++) {
		int q = i & 255, half = i & 512, quarter = (i >> 8) & 1;
		int l = opl_logsin[quarter ? 255 - q : q];
		for (w = 0; w < 4; w++) {
			int v;
			switch (w) {
			case 0: v = half ? (l | LOG_SIGN) : l; break;
			case 1: v = half ? LOG_ZERO : l; break;
			case 2: v = l; break;
			default: v = quarter ? LOG_ZERO : l; break;
			}
			o->logw[w][i] = (short)v;
		}
	}
	for (i = 0; i < 16384; i++) {
		int v = i & 0x1fff, s;
		s = v >= 13 * 256 ? 0 : (opl_exp[v & 255] >> (v >> 8));
		o->expsig[i] = (short)((i & LOG_SIGN) ? -s : s);
	}
}

/* average attenuation step per chip sample, Q16, for effective rate r */
static unsigned long eg_avg_q16(int r)
{
	int r4 = r >> 2, lo = r & 3;
	if (r4 == 0)
		return 0;
	if (r4 == 15)
		return 4UL << 16;
	return (unsigned long)(4 + lo) << (r4 + 1);
}

/* the chip's per-sample attenuation step for effective rate r at tick cnt */
static int eg_inc(unsigned long cnt, int r)
{
	int r4 = r >> 2, lo = r & 3, sh;
	if (r4 == 0)
		return 0;
	if (r4 <= 12) {
		sh = 12 - r4;
		if (cnt & ((1UL << sh) - 1))
			return 0;
		return opl_egpat[lo][(cnt >> sh) & 7];
	}
	if (r4 == 15)
		return 4;
	return opl_egpat2[lo][cnt & 7] << (r4 - 13);
}

/* ---- derived operator state -------------------------------------------- */

static int eff_rate(int reg, int ksroff)
{
	int r;
	if (reg == 0)
		return 0;
	r = reg * 4 + ksroff;
	return r > 63 ? 63 : r;
}

static void op_update(Opl2 *o, Chan *c, Op *op)
{
	int keycode, ksroff, ksl;

	keycode = (c->block << 1) | ((c->fnum >> (o->nts ? 8 : 9)) & 1);
	ksroff = op->ksr ? keycode : keycode >> 2;
	op->r_att = (unsigned char)eff_rate(op->ar, ksroff);
	op->r_dec = (unsigned char)eff_rate(op->dr, ksroff);
	op->r_rel = (unsigned char)eff_rate(op->rr, ksroff);

	ksl = (opl_kslrom[c->fnum >> 6] << 2) - ((8 - c->block) << 5);
	if (ksl < 0 || op->ksl == 0)
		ksl = 0;
	else
		ksl >>= opl_kslshift[op->ksl];
	op->base_att = op->tl * 4 + ksl;
	op->sl_att = (op->sl == 15 ? 31 : op->sl) << 4;

	op->inc19 = ((((u32)c->fnum << c->block) >> 1) * opl_mt2[op->mult]) >> 1;
	op->inc = op->inc19 * o->kphase;
}

static void chan_update(Opl2 *o, Chan *c)
{
	op_update(o, c, &c->op[0]);
	op_update(o, c, &c->op[1]);
}

static void key_on(Op *op)
{
	op->phase = 0;
	if (op->r_att >= 60) {
		op->att = 0;
		op->stage = EG_DEC;
	} else
		op->stage = EG_ATT;
}

static void key_off(Op *op)
{
	if (op->stage != EG_OFF)
		op->stage = EG_REL;
}

/* ---- register file ------------------------------------------------------ */

void opl2_write(Opl2 *o, int reg, int val)
{
	reg &= 0xff;
	val &= 0xff;

	if (reg == 0x01) {
		o->wse = (val >> 5) & 1;
		return;
	}
	if (reg == 0x08) {
		int i;
		o->nts = (val >> 6) & 1;
		for (i = 0; i < 9; i++)
			chan_update(o, &o->ch[i]);
		return;
	}
	if (reg == 0xBD) {
		o->dam = (val >> 7) & 1;
		o->dvb = (val >> 6) & 1;
		return;
	}
	if ((reg >= 0x20 && reg <= 0x95) || reg >= 0xE0) {
		int s = reg & 0x1f, k = s & 7;
		Chan *c;
		Op *op;
		if (k >= 6 || s >= 0x16)
			return;
		c = &o->ch[(s >> 3) * 3 + k % 3];
		op = &c->op[k / 3];
		switch (reg & 0xe0) {
		case 0x20:
			op->am = (val >> 7) & 1;
			op->vib = (val >> 6) & 1;
			op->egt = (val >> 5) & 1;
			op->ksr = (val >> 4) & 1;
			op->mult = val & 15;
			break;
		case 0x40:
			op->ksl = (val >> 6) & 3;
			op->tl = val & 63;
			break;
		case 0x60:
			op->ar = (val >> 4) & 15;
			op->dr = val & 15;
			break;
		case 0x80:
			op->sl = (val >> 4) & 15;
			op->rr = val & 15;
			break;
		default:	/* 0xE0 */
			op->ws = val & 3;
			break;
		}
		op_update(o, c, op);
		return;
	}
	if (reg >= 0xA0 && reg <= 0xA8) {
		Chan *c = &o->ch[reg - 0xA0];
		c->fnum = (c->fnum & 0x300) | val;
		chan_update(o, c);
		return;
	}
	if (reg >= 0xB0 && reg <= 0xB8) {
		Chan *c = &o->ch[reg - 0xB0];
		int key = (val >> 5) & 1;
		c->fnum = (c->fnum & 0xff) | ((val & 3) << 8);
		c->block = (val >> 2) & 7;
		chan_update(o, c);
		if (key && !c->key) {
			key_on(&c->op[0]);
			key_on(&c->op[1]);
		} else if (!key && c->key) {
			key_off(&c->op[0]);
			key_off(&c->op[1]);
		}
		c->key = key;
		return;
	}
	if (reg >= 0xC0 && reg <= 0xC8) {
		Chan *c = &o->ch[reg - 0xC0];
		c->fb = (val >> 1) & 7;
		c->con = val & 1;
		return;
	}
}

/* ---- envelope ----------------------------------------------------------- */

/* q1: one chip sample */
static void eg_tick(Op *op, unsigned long cnt)
{
	long a = op->att >> Q16;
	int inc;

	switch (op->stage) {
	case EG_ATT:
		inc = eg_inc(cnt, op->r_att);
		if (inc) {
			a -= ((a + 1) * inc + 7) >> 3;
			if (a <= 0) {
				a = 0;
				op->stage = EG_DEC;
			}
		}
		break;
	case EG_DEC:
		a += eg_inc(cnt, op->r_dec);
		if (a >= op->sl_att) {
			a = op->sl_att;
			op->stage = EG_SUS;
		}
		break;
	case EG_SUS:
		if (op->egt)
			break;
		/* EG-TYP=0 keeps decaying at the release rate */
		/* fall through */
	case EG_REL:
		a += eg_inc(cnt, op->r_rel);
		if (a >= ATT_MAX) {
			a = ATT_MAX;
			if (op->stage == EG_REL)
				op->stage = EG_OFF;
		}
		break;
	default:
		a = ATT_MAX;
		break;
	}
	op->att = a << Q16;
}

/* q0: one block of chip samples at once, averaged */
static void eg_block(Opl2 *o, Op *op)
{
	unsigned long step;
	long a = op->att;

	switch (op->stage) {
	case EG_ATT:
		step = o->eg_blk[op->r_att];
		if (step >= (8UL << Q16))
			a = 0;
		else
			a -= ((a >> Q16) + 1) * (long)(step >> 3);
		if (a <= 0) {
			a = 0;
			op->stage = EG_DEC;
		}
		break;
	case EG_DEC:
		a += (long)o->eg_blk[op->r_dec];
		if (a >= ((long)op->sl_att << Q16)) {
			a = (long)op->sl_att << Q16;
			op->stage = EG_SUS;
		}
		break;
	case EG_SUS:
		if (op->egt)
			break;
		/* fall through */
	case EG_REL:
		a += (long)o->eg_blk[op->r_rel];
		if (a >= ((long)ATT_MAX << Q16)) {
			a = (long)ATT_MAX << Q16;
			if (op->stage == EG_REL)
				op->stage = EG_OFF;
		}
		break;
	default:
		a = (long)ATT_MAX << Q16;
		break;
	}
	op->att = a;
}

/* total attenuation << 3 for the log domain, and the vibrato'd increment */
static void op_freeze(Opl2 *o, Chan *c, Op *op)
{
	int t = (int)(op->att >> Q16) + op->base_att;
	if (op->am)
		t += o->trem;
	if (t > ATT_MAX)
		t = ATT_MAX;
	op->tot8 = t << 3;

	if (op->vib) {
		static const signed char vpat[8] = { 0, 1, 2, 1, 0, -1, -2, -1 };
		int r = (c->fnum >> 7) & 7, d;
		if (!o->dvb)
			r >>= 1;
		d = vpat[o->vibpos];
		d = d == 2 ? r : d == -2 ? -r : d == 1 ? (r >> 1) : d == -1 ? -(r >> 1) : 0;
		op->inc = (((((u32)(c->fnum + d) << c->block) >> 1)
			    * opl_mt2[op->mult]) >> 1) * o->kphase;
	}
}

static void lfo_at(Opl2 *o, unsigned long tick)
{
	int p = (int)((tick >> 6) % 210);
	int t = p < 105 ? p : 209 - p;
	o->trem = o->dam ? t >> 2 : t >> 4;
	o->vibpos = (int)((tick >> 10) & 7);
}

static int chan_live(const Chan *c)
{
	if (c->con)
		return c->op[0].stage != EG_OFF || c->op[1].stage != EG_OFF;
	return c->op[1].stage != EG_OFF;
}

/* ---- quality 1: the chip, sample by sample ----------------------------- */

static int chip_sample(Opl2 *o)
{
	unsigned long cnt = ++o->tick;
	int i, mix = 0;

	if ((cnt & 63) == 0)
		lfo_at(o, cnt);
	for (i = 0; i < 9; i++) {
		Chan *c = &o->ch[i];
		Op *m = &c->op[0], *k = &c->op[1];
		const short *wm, *wk;
		int fbm, om, ok;

		eg_tick(m, cnt);
		eg_tick(k, cnt);
		if (!chan_live(c))
			continue;
		op_freeze(o, c, m);
		op_freeze(o, c, k);
		wm = o->logw[o->wse ? m->ws : 0];
		wk = o->logw[o->wse ? k->ws : 0];

		fbm = c->fb ? (c->out0 + c->out1) >> (9 - c->fb) : 0;
		om = o->expsig[wm[((m->phase >> 22) + fbm) & 1023] + m->tot8];
		c->out1 = c->out0;
		c->out0 = om;
		if (c->con)
			ok = om + o->expsig[wk[(k->phase >> 22) & 1023] + k->tot8];
		else
			ok = o->expsig[wk[((k->phase >> 22) + om) & 1023] + k->tot8];
		mix += ok;
		m->phase += m->inc;
		k->phase += k->inc;
	}
	return mix;
}

static void render_q1(Opl2 *o, short *out, int n)
{
	while (n-- > 0) {
		unsigned long need = o->step_q16;
		long acc = 0, wsum = 0, v;

		while (need) {
			unsigned long take;
			if (o->cur_left == 0) {
				o->cur = chip_sample(o);
				o->cur_left = 1UL << Q16;
			}
			take = need < o->cur_left ? need : o->cur_left;
			acc += o->cur * (long)(take >> 8);
			wsum += (long)(take >> 8);
			need -= take;
			o->cur_left -= take;
		}
		v = wsum ? acc / wsum : o->cur;
		v = (v * o->gain) >> 8;
		*out++ = (short)(v > 32767 ? 32767 : v < -32768 ? -32768 : v);
	}
}

/* ---- quality 0: at the output rate, per block -------------------------- */

/*
 * One channel for n samples, added into mix[]. Three loops, one per channel
 * shape, each its own function so that bebbo gcc allocates registers for
 * that loop alone (inlined into one body, -O1 spilled half of them to the
 * stack). The frozen attenuations are folded into the exp table POINTERS
 * (em = expsig + tot8), so one operator sample is: shift the phase, (add the
 * modulation, mask), index the wave, index the exp table.
 *
 * Cost, FM with feedback (the common case), per sample per channel: 2 phase
 * adds, 2 lsr #22, the feedback add + variable asr, 2 adds + 2 masks for the
 * modulation, 2 wave loads, 2 exp loads (indexed, scaled), the o1/o0 shuffle,
 * the mix add and the loop branch: about 18 instructions, ~75 cycles on a
 * 68020 with its cache warm (indexed loads ~7-9 cycles each, ALU 2-4, barrel
 * shifts 4-8). Without feedback ~14 instructions, ~55 cycles.
 */
#if defined(__GNUC__)
#define NOINLINE __attribute__((noinline))
#else
#define NOINLINE
#endif

static NOINLINE void loop_fm(long *mix, int n, const short *wm, const short *wk,
			     const short *em, const short *ek, Op *m, Op *k)
{
	u32 pm = m->phase, pk = k->phase, im = m->inc, ik = k->inc;

	while (n-- > 0) {
		int om = em[wm[pm >> 22]];
		*mix++ += ek[wk[((pk >> 22) + om) & 1023]];
		pm += im;
		pk += ik;
	}
	m->phase = pm;
	k->phase = pk;
}

static NOINLINE void loop_fmfb(long *mix, int n, const short *wm, const short *wk,
			       const short *em, const short *ek, Op *m, Op *k, Chan *c)
{
	u32 pm = m->phase, pk = k->phase, im = m->inc, ik = k->inc;
	int o0 = c->out0, o1 = c->out1, fsh = 9 - c->fb;

	while (n-- > 0) {
		int om = em[wm[((pm >> 22) + ((o0 + o1) >> fsh)) & 1023]];
		o1 = o0;
		o0 = om;
		*mix++ += ek[wk[((pk >> 22) + om) & 1023]];
		pm += im;
		pk += ik;
	}
	m->phase = pm;
	k->phase = pk;
	c->out0 = o0;
	c->out1 = o1;
}

static NOINLINE void loop_add(long *mix, int n, const short *wm, const short *wk,
			      const short *em, const short *ek, Op *m, Op *k, Chan *c)
{
	u32 pm = m->phase, pk = k->phase, im = m->inc, ik = k->inc;
	int o0 = c->out0, o1 = c->out1, fsh = 9 - c->fb, fbon = c->fb ? -1 : 0;

	while (n-- > 0) {
		int om = em[wm[((pm >> 22) + (((o0 + o1) >> fsh) & fbon)) & 1023]];
		o1 = o0;
		o0 = om;
		*mix++ += om + ek[wk[pk >> 22]];
		pm += im;
		pk += ik;
	}
	m->phase = pm;
	k->phase = pk;
	c->out0 = o0;
	c->out1 = o1;
}

static void chan_q0(Opl2 *o, Chan *c, long *mix, int n)
{
	Op *m = &c->op[0], *k = &c->op[1];
	const short *wm = o->logw[o->wse ? m->ws : 0];
	const short *wk = o->logw[o->wse ? k->ws : 0];
	const short *em = o->expsig + m->tot8, *ek = o->expsig + k->tot8;

	if (c->con)
		loop_add(mix, n, wm, wk, em, ek, m, k, c);
	else if (c->fb)
		loop_fmfb(mix, n, wm, wk, em, ek, m, k, c);
	else
		loop_fm(mix, n, wm, wk, em, ek, m, k);
}

static void block_q0(Opl2 *o)
{
	int i;
	unsigned long t = o->tick_frac + o->tpb_q16;

	o->tick += t >> Q16;
	o->tick_frac = t & 0xffff;
	lfo_at(o, o->tick);
	for (i = 0; i < 9; i++) {
		Chan *c = &o->ch[i];
		eg_block(o, &c->op[0]);
		eg_block(o, &c->op[1]);
		op_freeze(o, c, &c->op[0]);
		op_freeze(o, c, &c->op[1]);
	}
}

static void render_q0(Opl2 *o, short *out, int n)
{
	long mix[OPL2_BLOCK];

	while (n > 0) {
		int len, i, j;
		if (o->blkleft == 0) {
			block_q0(o);
			o->blkleft = OPL2_BLOCK;
		}
		len = n < o->blkleft ? n : o->blkleft;
		memset(mix, 0, sizeof(long) * len);
		for (i = 0; i < 9; i++) {
			Chan *c = &o->ch[i];
			if (chan_live(c))
				chan_q0(o, c, mix, len);
		}
		for (j = 0; j < len; j++) {
			long v = (mix[j] * o->gain) >> 8;
			*out++ = (short)(v > 32767 ? 32767 : v < -32768 ? -32768 : v);
		}
		o->blkleft -= len;
		n -= len;
	}
}

/* ---- public ------------------------------------------------------------- */

void opl2_render(Opl2 *o, short *out, int n)
{
	if (o->quality)
		render_q1(o, out, n);
	else
		render_q0(o, out, n);
}

void opl2_reset(Opl2 *o)
{
	int i, j;

	o->wse = o->nts = o->dam = o->dvb = 0;
	o->tick = o->tick_frac = 0;
	o->trem = o->vibpos = o->blkleft = 0;
	o->cur = 0;
	o->cur_left = 0;
	memset(o->ch, 0, sizeof(o->ch));
	for (i = 0; i < 9; i++)
		for (j = 0; j < 2; j++) {
			o->ch[i].op[j].stage = EG_OFF;
			o->ch[i].op[j].att = (long)ATT_MAX << Q16;
		}
	for (i = 0; i < 9; i++)
		chan_update(o, &o->ch[i]);
}

Opl2 *opl2_create(int rate, int quality)
{
	Opl2 *o;
	unsigned long tpb_q8;
	int r;

	if (rate < 4000 || rate > 192000)
		return NULL;
	o = (Opl2 *)malloc(sizeof(Opl2));
	if (!o)
		return NULL;
	memset(o, 0, sizeof(*o));
	o->rate = rate;
	o->quality = quality ? 1 : 0;
	o->gain = 512;
	if (o->quality) {
		o->kphase = 8192;
	} else {
		o->kphase = (u32)((8192UL * OPL2_CHIP_RATE + rate / 2) / rate);
	}
	o->step_q16 = ((unsigned long)OPL2_CHIP_RATE << 16) / rate;
	o->tpb_q16 = o->step_q16 * OPL2_BLOCK;
	/* (avg >> 2) * tpb_q8 >> 6 = avg * tpb >> 16; avg has two zero low bits */
	tpb_q8 = o->tpb_q16 >> 8;
	for (r = 0; r < 64; r++)
		o->eg_blk[r] = ((eg_avg_q16(r) >> 2) * tpb_q8) >> 6;
	build_tables(o);
	opl2_reset(o);
	return o;
}

void opl2_destroy(Opl2 *o)
{
	free(o);
}

void opl2_set_gain(Opl2 *o, int gain_q8)
{
	o->gain = gain_q8;
}

int opl2_silent(const Opl2 *o)
{
	int i;
	for (i = 0; i < 9; i++)
		if (o->ch[i].op[0].stage != EG_OFF || o->ch[i].op[1].stage != EG_OFF)
			return 0;
	return 1;
}
