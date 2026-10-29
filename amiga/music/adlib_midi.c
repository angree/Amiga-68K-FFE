/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 angree. See LICENSE at the repository root.
 *
 * General MIDI on an OPL2, with the game's own MELODIC.BNK / DRUM.BNK.
 * Interface: adlib_midi.h / midi_synth.h. The chip itself: opl2.c.
 *
 * ---- the banks ------------------------------------------------------------
 *
 * Both files are AdLib "ADLIB-" banks (version 0.0, the HMI flavour), 5404
 * bytes: a 28-byte header (version, signature, numUsed, numInstruments,
 * offsetName, offsetData), 128 name records of 12 bytes (index into the data,
 * a flags byte, a 9-byte name) and 128 instruments of 30 bytes:
 *
 *   +0 percussive, +1 voice number          (0 in every record of both banks)
 *   +2  modulator: ksl mult fb ar sl eg dr rr tl am vib ksr con  (13 bytes)
 *   +15 carrier:   the same 13 fields (its fb and con are unused)
 *   +28 modulator waveform, +29 carrier waveform
 *
 * Every field is the register bit-field's value, unshifted, and that includes
 * CON: 1 means ADDITIVE (register 0xC0 bit 0 set). The modding wiki lists the
 * field as inverted "[verify this]"; the banks themselves say otherwise:
 * every instrument whose carrier is quiet (GLOCK car TL 0x1C, BANDNEON 0x0B,
 * FANTASIA 0x06) has a full-level modulator and con=1, which only makes sense
 * if the modulator is heard, i.e. additive; the organs (ORGAN1..3) are con=1,
 * the pianos and basses con=0 (FM).
 *
 * MELODIC.BNK is laid out in General MIDI order (PIANO1, PIANO2, ... GUNSHOT),
 * so program N is instrument N. DRUM.BNK is indexed by the GM KEY on channel
 * 10 (35 Kick2, 36 Kick, 38 Snare, 42 clsdht47, 46 Openhat, 49 Crash ...), and
 * in this bank the name record's flags byte is not 0/1 but the NOTE AT WHICH
 * THE DRUM SOUNDS: Kick 35, Snare 60, the six toms 41/44/48/51/54/57 rising.
 * Keys 0..27 and 88..127 are "Blank" (both TL 63) and are ignored.
 *
 * ---- voices: nine melodic, no rhythm mode ---------------------------------
 *
 * The drum instruments are ordinary two-operator patches (percussive = 0 in
 * all of them, hi-hats and cymbals on waveform 3 with high multiples) played
 * at a fixed pitch -- the bank was made for a driver that plays drums on
 * melodic voices. OPL2 rhythm mode would throw those patches away for the
 * chip's five fixed, shared-register drums, and cost three of the nine voices
 * besides. So all nine voices are melodic and drums take one like any note.
 *
 * Allocation: a new note takes (1) the voice already sounding the same
 * channel and key, else (2) a released voice -- one that already holds the
 * same instrument first (no reload), the longest released otherwise -- else
 * (3) the oldest held voice is stolen.
 *
 * Pitch: note + bend in 1/32 semitone steps, one octave table of F-numbers
 * (am_fnum, G4..F#5 at block 4, 517..976 -- the top of the 10-bit range, so
 * one step is ~3 cents) and the block from the octave. Bend range RPN 0
 * (default 2 semitones). Drums ignore bend.
 *
 * Loudness: velocity, CC7 and CC11 each through the GM curve 40 log10(v/127),
 * summed in TL units (0.75 dB) onto the carrier's TL -- and the modulator's
 * too when additive, since then it is heard. 0xBD is set to deep tremolo and
 * deep vibrato, as the AdLib-era GM drivers did.
 *
 * Not done: pan (mono out), modulation wheel, HMI's own SOS voice-priority
 * and per-instrument transposition rules (none are documented; exact HMI
 * behaviour is not a goal).
 */
#include <stdlib.h>
#include <string.h>

#include "adlib_midi.h"
#include "opl2.h"

/* round(440 * 2^((67 + k/32 - 69) / 12) * 65536 / 49716): F-numbers of
 * G4 + k/32 semitones at block 4, k = 0..383 */
static const unsigned short am_fnum[384] = {
	517, 518, 519, 520, 520, 521, 522, 523, 524, 525, 526, 527, 528, 529, 530, 531,
	532, 533, 534, 535, 536, 537, 538, 539, 540, 541, 542, 543, 544, 545, 545, 546,
	547, 548, 549, 550, 551, 552, 553, 554, 555, 556, 557, 558, 559, 560, 561, 562,
	563, 565, 566, 567, 568, 569, 570, 571, 572, 573, 574, 575, 576, 577, 578, 579,
	580, 581, 582, 583, 584, 585, 586, 587, 588, 590, 591, 592, 593, 594, 595, 596,
	597, 598, 599, 600, 601, 602, 604, 605, 606, 607, 608, 609, 610, 611, 612, 613,
	615, 616, 617, 618, 619, 620, 621, 622, 623, 625, 626, 627, 628, 629, 630, 631,
	633, 634, 635, 636, 637, 638, 639, 641, 642, 643, 644, 645, 646, 648, 649, 650,
	651, 652, 653, 655, 656, 657, 658, 659, 661, 662, 663, 664, 665, 666, 668, 669,
	670, 671, 673, 674, 675, 676, 677, 679, 680, 681, 682, 684, 685, 686, 687, 689,
	690, 691, 692, 693, 695, 696, 697, 699, 700, 701, 702, 704, 705, 706, 707, 709,
	710, 711, 713, 714, 715, 716, 718, 719, 720, 722, 723, 724, 726, 727, 728, 729,
	731, 732, 733, 735, 736, 737, 739, 740, 741, 743, 744, 745, 747, 748, 749, 751,
	752, 754, 755, 756, 758, 759, 760, 762, 763, 765, 766, 767, 769, 770, 771, 773,
	774, 776, 777, 778, 780, 781, 783, 784, 785, 787, 788, 790, 791, 793, 794, 795,
	797, 798, 800, 801, 803, 804, 806, 807, 808, 810, 811, 813, 814, 816, 817, 819,
	820, 822, 823, 825, 826, 828, 829, 831, 832, 834, 835, 837, 838, 840, 841, 843,
	844, 846, 847, 849, 850, 852, 853, 855, 857, 858, 860, 861, 863, 864, 866, 867,
	869, 871, 872, 874, 875, 877, 878, 880, 882, 883, 885, 886, 888, 890, 891, 893,
	894, 896, 898, 899, 901, 903, 904, 906, 908, 909, 911, 912, 914, 916, 917, 919,
	921, 922, 924, 926, 927, 929, 931, 932, 934, 936, 937, 939, 941, 943, 944, 946,
	948, 949, 951, 953, 955, 956, 958, 960, 961, 963, 965, 967, 968, 970, 972, 974,
	975, 977, 979, 981, 983, 984, 986, 988, 990, 991, 993, 995, 997, 999, 1000, 1002,
	1004, 1006, 1008, 1009, 1011, 1013, 1015, 1017, 1019, 1020, 1022, 1024, 1026, 1028, 1030, 1032,
};
/* GM loudness curve in TL units: round(40 log10(127 / v) / 0.75), max 63 */
static const unsigned char am_volatt[128] = {
	63, 63, 63, 63, 63, 63, 63, 63, 63, 61, 59, 57, 55, 53, 51, 49,
	48, 47, 45, 44, 43, 42, 41, 40, 39, 38, 37, 36, 35, 34, 33, 33,
	32, 31, 31, 30, 29, 29, 28, 27, 27, 26, 26, 25, 25, 24, 24, 23,
	23, 22, 22, 21, 21, 20, 20, 19, 19, 19, 18, 18, 17, 17, 17, 16,
	16, 16, 15, 15, 14, 14, 14, 13, 13, 13, 13, 12, 12, 12, 11, 11,
	11, 10, 10, 10, 10, 9, 9, 9, 8, 8, 8, 8, 7, 7, 7, 7,
	6, 6, 6, 6, 6, 5, 5, 5, 5, 4, 4, 4, 4, 4, 3, 3,
	3, 3, 3, 2, 2, 2, 2, 2, 1, 1, 1, 1, 1, 0, 0, 0,
};
static const unsigned char am_opoff[9] = { 0, 1, 2, 8, 9, 10, 16, 17, 18 };

#define NVOICE	9
#define DRUMCH	9
#define BNK_SIZE (28 + 128 * 12 + 128 * 30)

typedef struct {
	unsigned char r20[2], r40[2], r60[2], r80[2], re0[2], rc0;
	unsigned char fixnote;		/* drums: the key it sounds at */
	unsigned char blank;		/* both operators silent */
} Inst;

typedef struct {
	int ch, note;			/* MIDI channel and key; ch -1 = never used */
	int on;				/* key held (or held by the pedal) */
	int pedal;			/* released while the sustain pedal was down */
	int vel;
	unsigned long age;		/* serial of the last on/off */
	const Inst *inst;		/* loaded into the chip */
	int fnum, block;
} Voice;

typedef struct {
	int prog, vol, expr, sustain, bend, bendcents, rpn;
} MChan;

typedef struct {
	MidiSynth ms;
	Opl2 *opl;
	unsigned long serial;
	Inst mel[128], drum[128];
	Voice v[NVOICE];
	MChan c[16];
} Adl;

/* ---- banks ----------------------------------------------------------------- */

static unsigned rd16(const unsigned char *p) { return p[0] | (p[1] << 8); }
static unsigned long rd32(const unsigned char *p)
{
	return p[0] | (p[1] << 8) | ((unsigned long)p[2] << 16) | ((unsigned long)p[3] << 24);
}

static void op_regs(Inst *in, int k, const unsigned char *f, int wave)
{
	/* f: ksl mult fb ar sl eg dr rr tl am vib ksr con */
	in->r20[k] = (unsigned char)(((f[9] & 1) << 7) | ((f[10] & 1) << 6) | ((f[5] ? 1 : 0) << 5)
				     | ((f[11] & 1) << 4) | (f[1] & 15));
	in->r40[k] = (unsigned char)(((f[0] & 3) << 6) | (f[8] & 63));
	in->r60[k] = (unsigned char)(((f[3] & 15) << 4) | (f[6] & 15));
	in->r80[k] = (unsigned char)(((f[4] & 15) << 4) | (f[7] & 15));
	in->re0[k] = (unsigned char)(wave & 3);
}

static int load_bank(Inst *dst, const unsigned char *b, long len)
{
	unsigned ninst, i;
	unsigned long on, od;

	if (!b || len < 28 || memcmp(b + 2, "ADLIB-", 6) != 0)
		return 0;
	ninst = rd16(b + 10);
	on = rd32(b + 12);
	od = rd32(b + 16);
	if (ninst > 128)
		ninst = 128;
	memset(dst, 0, sizeof(Inst) * 128);
	for (i = 0; i < 128; i++)
		dst[i].blank = 1;
	for (i = 0; i < ninst; i++) {
		const unsigned char *nr = b + on + i * 12, *d;
		unsigned idx;
		Inst *in = &dst[i];
		if (on + i * 12 + 12 > (unsigned long)len)
			return 0;
		idx = rd16(nr);
		if (od + idx * 30UL + 30 > (unsigned long)len)
			return 0;
		d = b + od + idx * 30UL;
		op_regs(in, 0, d + 2, d[28]);
		op_regs(in, 1, d + 15, d[29]);
		in->rc0 = (unsigned char)(((d[4] & 7) << 1) | (d[14] ? 1 : 0));
		in->fixnote = nr[2];
		in->blank = (d[10] & 63) == 63 && (d[23] & 63) == 63;
	}
	return 1;
}

/* ---- chip access ----------------------------------------------------------- */

static void wr(Adl *a, int reg, int val) { opl2_write(a->opl, reg, val); }

static int vol_att(Adl *a, Voice *v)
{
	const MChan *c = &a->c[v->ch];
	int t = am_volatt[v->vel] + am_volatt[c->vol] + am_volatt[c->expr];
	return t > 63 ? 63 : t;
}

static int tl_with(int r40, int att)
{
	int tl = (r40 & 63) + att;
	return (r40 & 0xc0) | (tl > 63 ? 63 : tl);
}

static void set_levels(Adl *a, int vi)
{
	Voice *v = &a->v[vi];
	const Inst *in = v->inst;
	int att = vol_att(a, v), mo = am_opoff[vi];

	wr(a, 0x40 + mo, (in->rc0 & 1) ? tl_with(in->r40[0], att) : in->r40[0]);
	wr(a, 0x43 + mo, tl_with(in->r40[1], att));
}

static void load_inst(Adl *a, int vi, const Inst *in)
{
	int mo = am_opoff[vi], k;
	for (k = 0; k < 2; k++) {
		int r = mo + 3 * k;
		wr(a, 0x20 + r, in->r20[k]);
		wr(a, 0x60 + r, in->r60[k]);
		wr(a, 0x80 + r, in->r80[k]);
		wr(a, 0xE0 + r, in->re0[k]);
	}
	wr(a, 0xC0 + vi, in->rc0);
	a->v[vi].inst = in;
}

/* note (+ bend in 1/32 semitones) -> fnum/block */
static void pitch(int note, int bend32, int *fnum, int *block)
{
	int pos = (note - 67) * 32 + bend32, oct, idx, f, b;

	oct = pos >= 0 ? pos / 384 : -((-pos + 383) / 384);
	idx = pos - oct * 384;
	f = am_fnum[idx];
	b = 4 + oct;
	if (b < 0) {
		f >>= -b;
		b = 0;
	} else if (b > 7) {
		f = 1023;
		b = 7;
	}
	*fnum = f;
	*block = b;
}

static int bend32(const MChan *c)
{
	return (int)(((long)(c->bend - 8192) * c->bendcents) / 25600);
}

static void set_freq(Adl *a, int vi, int key)
{
	Voice *v = &a->v[vi];
	int note = v->note, b32 = 0;

	if (v->ch == DRUMCH)
		note = v->inst->fixnote;
	else
		b32 = bend32(&a->c[v->ch]);
	pitch(note, b32, &v->fnum, &v->block);
	wr(a, 0xA0 + vi, v->fnum & 0xff);
	wr(a, 0xB0 + vi, (key ? 0x20 : 0) | (v->block << 2) | (v->fnum >> 8));
}

static void voice_off(Adl *a, int vi)
{
	Voice *v = &a->v[vi];
	v->on = 0;
	v->pedal = 0;
	v->age = ++a->serial;
	wr(a, 0xB0 + vi, (v->block << 2) | (v->fnum >> 8));
}

static void chip_init(Adl *a)
{
	opl2_reset(a->opl);
	wr(a, 0x01, 0x20);	/* waveform select enable */
	wr(a, 0x08, 0x00);
	wr(a, 0xBD, 0xC0);	/* deep tremolo, deep vibrato, no rhythm */
}

/* ---- MidiSynth methods ----------------------------------------------------- */

static int pick_voice(Adl *a, int ch, int note, const Inst *in)
{
	int i, best = -1;
	unsigned long bage = 0;

	for (i = 0; i < NVOICE; i++)
		if (a->v[i].ch == ch && a->v[i].note == note && a->v[i].on)
			return i;
	for (i = 0; i < NVOICE; i++) {	/* released, same instrument */
		Voice *v = &a->v[i];
		if (!v->on && v->inst == in && (best < 0 || v->age < bage)) {
			best = i;
			bage = v->age;
		}
	}
	if (best >= 0)
		return best;
	for (i = 0; i < NVOICE; i++) {	/* released, any */
		Voice *v = &a->v[i];
		if (!v->on && (best < 0 || v->age < bage)) {
			best = i;
			bage = v->age;
		}
	}
	if (best >= 0)
		return best;
	for (i = 0; i < NVOICE; i++) {	/* steal the oldest */
		Voice *v = &a->v[i];
		if (best < 0 || v->age < bage) {
			best = i;
			bage = v->age;
		}
	}
	return best;
}

static void m_note_off(void *ctx, int ch, int note)
{
	Adl *a = (Adl *)ctx;
	int i;
	ch &= 15;
	for (i = 0; i < NVOICE; i++) {
		Voice *v = &a->v[i];
		if (v->on && !v->pedal && v->ch == ch && v->note == note) {
			if (a->c[ch].sustain && ch != DRUMCH)
				v->pedal = 1;
			else
				voice_off(a, i);
		}
	}
}

static void m_note_on(void *ctx, int ch, int note, int vel)
{
	Adl *a = (Adl *)ctx;
	const Inst *in;
	Voice *v;
	int vi;

	ch &= 15;
	note &= 127;
	if (vel <= 0) {
		m_note_off(ctx, ch, note);
		return;
	}
	in = ch == DRUMCH ? &a->drum[note] : &a->mel[a->c[ch].prog];
	if (in->blank)
		return;
	vi = pick_voice(a, ch, note, in);
	v = &a->v[vi];
	if (v->on)	/* key off first: the chip starts a note on the 0 -> 1 edge */
		wr(a, 0xB0 + vi, (v->block << 2) | (v->fnum >> 8));
	if (v->inst != in)
		load_inst(a, vi, in);
	v->ch = ch;
	v->note = note;
	v->vel = vel > 127 ? 127 : vel;
	v->on = 1;
	v->pedal = 0;
	v->age = ++a->serial;
	set_levels(a, vi);
	set_freq(a, vi, 1);
}

static void m_program(void *ctx, int ch, int prog)
{
	Adl *a = (Adl *)ctx;
	a->c[ch & 15].prog = prog & 127;
}

static void m_pitch_bend(void *ctx, int ch, int value)
{
	Adl *a = (Adl *)ctx;
	int i;
	ch &= 15;
	a->c[ch].bend = value < 0 ? 0 : value > 16383 ? 16383 : value;
	if (ch == DRUMCH)
		return;
	for (i = 0; i < NVOICE; i++)
		if (a->v[i].ch == ch && a->v[i].inst)
			set_freq(a, i, a->v[i].on);
}

static void chan_reset(MChan *c)
{
	c->bend = 8192;
	c->expr = 127;
	c->sustain = 0;
	c->rpn = 0x3fff;
}

static void m_control(void *ctx, int ch, int cc, int val)
{
	Adl *a = (Adl *)ctx;
	MChan *c;
	int i;

	ch &= 15;
	c = &a->c[ch];
	val &= 127;
	switch (cc) {
	case 7:
	case 11:
		if (cc == 7)
			c->vol = val;
		else
			c->expr = val;
		for (i = 0; i < NVOICE; i++)
			if (a->v[i].ch == ch && a->v[i].inst)
				set_levels(a, i);
		break;
	case 64:
		c->sustain = val >= 64;
		if (!c->sustain)
			for (i = 0; i < NVOICE; i++)
				if (a->v[i].ch == ch && a->v[i].pedal)
					voice_off(a, i);
		break;
	case 101:
		c->rpn = (c->rpn & 0x7f) | (val << 7);
		break;
	case 100:
		c->rpn = (c->rpn & 0x3f80) | val;
		break;
	case 6:
		if (c->rpn == 0)
			c->bendcents = val * 100 + c->bendcents % 100;
		break;
	case 38:
		if (c->rpn == 0)
			c->bendcents = (c->bendcents / 100) * 100 + (val < 100 ? val : 99);
		break;
	case 121:
		chan_reset(c);
		for (i = 0; i < NVOICE; i++)
			if (a->v[i].ch == ch && a->v[i].pedal)
				voice_off(a, i);
		m_pitch_bend(ctx, ch, 8192);
		break;
	case 120:
	case 123:
		for (i = 0; i < NVOICE; i++)
			if (a->v[i].ch == ch && a->v[i].on)
				voice_off(a, i);
		break;
	default:
		break;
	}
}

static void m_all_off(void *ctx)
{
	Adl *a = (Adl *)ctx;
	int i;
	chip_init(a);
	for (i = 0; i < NVOICE; i++) {
		memset(&a->v[i], 0, sizeof(Voice));
		a->v[i].ch = -1;
	}
}

static void m_render(void *ctx, short *out, int n)
{
	opl2_render(((Adl *)ctx)->opl, out, n);
}

static void m_destroy(void *ctx)
{
	Adl *a = (Adl *)ctx;
	opl2_destroy(a->opl);
	free(a);
}

MidiSynth *adlib_midi_create(int rate, const unsigned char *melodic_bnk, long mlen,
			     const unsigned char *drum_bnk, long dlen, int quality)
{
	Adl *a = (Adl *)malloc(sizeof(Adl));
	int i;

	if (!a)
		return NULL;
	memset(a, 0, sizeof(*a));
	if (!load_bank(a->mel, melodic_bnk, mlen) || !load_bank(a->drum, drum_bnk, dlen)) {
		free(a);
		return NULL;
	}
	a->opl = opl2_create(rate, quality);
	if (!a->opl) {
		free(a);
		return NULL;
	}
	opl2_set_gain(a->opl, 512);	/* x2 (was x3: 13 of the 17 game songs clipped at 22050 Hz, measured 2026-10-24) */
	for (i = 0; i < 16; i++) {
		a->c[i].vol = 100;
		a->c[i].bendcents = 200;
		chan_reset(&a->c[i]);
	}
	m_all_off(a);

	a->ms.ctx = a;
	a->ms.note_on = m_note_on;
	a->ms.note_off = m_note_off;
	a->ms.program = m_program;
	a->ms.control = m_control;
	a->ms.pitch_bend = m_pitch_bend;
	a->ms.all_off = m_all_off;
	a->ms.render = m_render;
	a->ms.destroy = m_destroy;
	return &a->ms;
}
