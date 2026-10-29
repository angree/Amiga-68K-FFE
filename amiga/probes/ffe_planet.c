/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 angree. See LICENSE at the repository root.
 *
 * FUNC_001877 -- the planet limb / atmosphere-halo renderer, hand-ported to C
 * because the translator refuses it (ffe.asm:131620, "no flag model for
 * fdivp": 97 x87 instructions plus a SECTION .data island inside the function
 * body). Source: ref\jjffe\ffe.asm lines 130987-132199, 1169 instructions.
 * The binding specification is build\datamap\func1877_analysis.md; every
 * block below carries the ffe.asm line range it reproduces.
 *
 * What it does: solve for the observer's position in the planet's own corner
 * basis (a 3x3 Gauss-Jordan in single precision), build the horizon tangent
 * circle, walk it in 32 segments emitting 32 Gouraud-shaded quads into the
 * deferred-draw list, and leave DATA_009262/9263/9265/9267 behind as the
 * atmospheric depth-fade parameters the deferred span handler FUNC_001840
 * reads later.
 *
 * CALLING CONVENTION. The symbol is the BARE name FUNC_001877: the only
 * caller, translated FUNC_001876, reaches it with `jsr FUNC_001877` under the
 * t-ABI, where d2-d7/a2-a6 are scratch. A C callee saves strictly more than
 * that, so a C shim is legal -- the same rule that licenses Func833_c and
 * FUNC_CheckInputWrap in ffe_game.c. Arguments are the six cdecl dwords
 * FUNC_001876 pushes at ffe.asm:129462-129474; the return is void (the caller
 * overwrites eax at 129477).
 *
 * Calls OUT go through the C-ABI `_`-prefixed wrappers the translator emits,
 * never the bare bodies: a bare body would clobber d2-d7/a2-a6 that this
 * file's compiler expects preserved across a call.
 *
 * FLOATING POINT (analysis section 6). The Gauss-Jordan block runs in double
 * with an explicit (float) round at exactly the fstp points the original has
 * -- x87 computes each group at 80 bits and rounds once to 24, C rounds to 53
 * then to 24, and the two disagree only on a 24-bit tie (order 1e-7 per call).
 * The final `fmul DATA_000051` (= 32768.0f) is NOT rounded to single before
 * FUNC_002095_fstore truncates it, hence double -> long with no intermediate
 * float. FUNC_002095_fstore sets RC=11b, round-toward-zero, which is what a C
 * cast does. The band-count rescale is done in long long, not double: the
 * product can exceed 2^53 and the exact truncated quotient is what x87's
 * 64-bit mantissa produces anyway.
 */

#include "../ffeapi/ffe_dev.h"
#include <stdio.h>	/* the HALO timeline probe */
extern int ffe_is_quiet (void);

/* ---- game data -----------------------------------------------------------
 *
 * Storage classes from build\data-be\ffedat_be.storage.tsv. `be32` means the
 * conversion swapped the label, so a native 68k dword access yields the value
 * the x86 dword access yielded; `plain`/`le` means the bytes are untouched and
 * only byte-wide access is safe unless the readers agree otherwise.
 */
extern long DATA_009262 __asm__("DATA_009262");	/* be32: atmosphere present */
/* Instrumentation counters, read from ffe_game.c. See the comment at the
 * top of Func1877_c for what each one distinguishes. */
long ffe_p1877_calls;
long ffe_p1877_noatmo;
long ffe_p1877_shell;      /* DATA_009263 != 0 -> the whole block skipped */
long ffe_p1877_shade0;     /* FUNC_001878 returned 0 -> no -1 written */
long ffe_p1877_set;        /* DATA_008814 = -1 actually written */
long ffe_p1877_singular;   /* M[0] == 0 -> no usable pivot */
long ffe_p1877_far;        /* |E| >= 0x800000 -> too far to draw */
long ffe_p1877_near;       /* Msb1825(E) <= 13 -> too close */
long ffe_p1877_inside;     /* tan2 < 0 -> eye inside the horizon sphere */
/* EVERY call past the atmosphere check leaves through the `near` exit, so
 * Msb1825(E) is coming back <= 13, i.e. |E| < 8192 for a vector that should
 * be a planet radius. Sample the vector and the answer to say whether the
 * INPUT is small or FUNC_001825 is wrong -- the two need different fixes and
 * FUNC_001825 is NOT one of the 95 oracle-carried functions. */
long ffe_p1877_E[3];
/* The FIRST call that leaves through the `far' exit, recorded whole so it can
 * be set against the reference's PC1877 line. */
long ffe_p1877_farmag, ffe_p1877_faratmo, ffe_p1877_farbands, ffe_p1877_farcol;
long ffe_p1877_farE[3], ffe_p1877_farc[3], ffe_p1877_farM[3];

/* THE FIRST SIX CALLS, recorded at ENTRY so the sequence lines up with the
 * reference's PC1877 n=1..6 whatever exit each one takes. The reference
 * alternates two objects per frame -- a planet at 442M with atmo=429457408
 * bands=1024, and a second at -185M with atmo=193265664 bands=6242304 -- so a
 * single sample cannot be matched to the right one by hand. Column 7 is the
 * exit: 1 noatmo, 2 far, 3 near, 4 singular, 5 inside, 6 shell, 7 shade0,
 * 8 SET (the only one that publishes DATA_008814). */
/* Columns 8..11 carry the record around `centre': rec+0x0 and the transform's
 * OWN output at rec+0x18..0x20. The reference prints them as PC1877R and reads
 * cam=(0,0,0) there, which is what makes `centre' equal the object POSITION on
 * its side -- so the same three words have to be read here before the position
 * can be blamed. centre points at rec+4, hence centre[-1] and centre[5..7]. */
long ffe_p1877_seq[6][12];
long ffe_p1877_n;
long ffe_p1877_msb;
long ffe_p1877_centre[3];
long ffe_p1877_rec[5];

extern long DATA_009263 __asm__("DATA_009263");	/* be32: shell distance */
extern long DATA_009267 __asm__("DATA_009267");	/* be32: band count, >= 1 */
extern long DATA_008814 __asm__("DATA_008814");	/* be32: colour-valid flag */

/* DATA_009265 (le, 2 bytes) + DATA_009266 (le, 2 bytes) are ONE uint32 in
 * disguise: the original writes them word+byte (131003, 131005) and every
 * reader -- FUNC_001838:114437, FUNC_001839:114511, FUNC_001840:114760/114788
 * -- dword-reads them. FUNC_001840 combines that dword arithmetically with the
 * be32 palette table DATA_007846 under `and eax,0xf0f0f`, i.e. the channels
 * must sit at bits 0..3 / 8..11 / 16..19 exactly as on x86. So this is written
 * as ONE NATIVE dword holding the x86 number 0x00bbggrr, never byte-wise. */
extern unsigned long DATA_009265 __asm__("DATA_009265");

/* DATA_008815/16/17 are a 3-byte r,g,b array (DATA_008814 is its dword flag).
 * The original's `mov ax,[DATA_008815]` is a 2-byte COPY, not a 16-bit
 * integer, and FUNC_001700:103816/103840 reads the same bytes individually --
 * so byte-wide access here is both correct and endian-neutral. */
extern unsigned char DATA_008815 __asm__("DATA_008815");
extern unsigned char DATA_008816 __asm__("DATA_008816");
extern unsigned char DATA_008817 __asm__("DATA_008817");

/* ---- a screen vertex: two int16 written by FUNC_001479 ------------------- */
typedef struct { short x, y; } Scr;

/* ---- the translated callees, through their C-ABI wrappers ---------------- */
extern long Mul1521    (long a, long b)            __asm__("_FUNC_001521");
extern long Msb1825    (long *v3)                  __asm__("_FUNC_001825");
extern long Sqrt1467   (long v)                    __asm__("_FUNC_001467");
extern long Sin1913    (long theta)                __asm__("_FUNC_001913");
extern long Project1479(Scr *dst, long *p3)        __asm__("_FUNC_001479");
extern long Shade1878  (void *ctx, long *n3)       __asm__("_FUNC_001878");
extern void Defer1780  (long open, long key)       __asm__("_FUNC_001780");
extern void Quad1879   (void *ctx, Scr *a, Scr *b, Scr *c,
                        Scr *d, Scr *e, Scr *f,
                        unsigned char *ramp, long *n3) __asm__("_FUNC_001879");

/* x86 masks a variable shift count to 5 bits, and this function does use
 * counts that go negative (`15 - shift` at 131675) -- the mask is load
 * bearing, not decoration. The left shifts go through unsigned to keep the
 * 32-bit wrap the original relies on out of signed-overflow territory. */
#define SAR(v,c)	((long)(v) >> (int)((c) & 31))
#define SHL(v,c)	((long)((unsigned long)(v) << (int)((c) & 31)))

/* one-operand `imul`: the low 32 bits of the product, wrapping */
static long mul32 (long a, long b)
{
	return (long)((unsigned long)a * (unsigned long)b);
}

/* the game's `_abs`; abs(INT_MIN) wraps to itself on both machines */
static long ffe_abs (long v)
{
	return v < 0 ? -v : v;
}

void Func1877_c (void *ctx, long *centre, long *corners,
                 unsigned long colour, long atmo, long bands)
	__asm__("FUNC_001877");

void Func1877_c (void *ctx, long *centre, long *corners,
                 unsigned long colour, long atmo, long bands)
{
	/* The ebp-relative frame of ffe.asm:130991 (292 bytes), named as in
	 * the analysis. Only the arrays whose address is taken need to stay
	 * contiguous, and each of those is one C array here. */
	Scr   s04, s08, s0c, s10, s14, s18, s1c, s20, s24;
	long  M[9], Ms[9];			/* pivoted corner matrix, and its copy */
	float Mf[9], Vf[3];			/* the single-precision working set */
	long  E[3], U[3], W[3], T[3], P[3], R[3];
	union { long l[7]; unsigned char b[28]; } ramp;
	unsigned char cur[3];		/* current r,g,b */
	long  cr, cg, cb;			/* the COLOUR argument, unpacked */
	long  shift, r2, len, mag, theta, k, i;
	long  ebx_f;				/* the x86 `ebx` that is live across JUMP_008059 */
	extern void FfeWinCount (long) __asm__("_FfeWinCount");
	FfeWinCount (1877);	/* 2026-10-15: twin of the PC entry counter (FRAMECNT) */

	/* DATA_007895 (ffedat.asm:26693) is the six permutations of {0,1,2} as
	 * 18 dwords. Its storage class is `plain` -- raw x86 bytes -- so a
	 * native dword read of the image would be wrong. It is a compile-time
	 * constant; hard-coding it is what the analysis recommends and costs
	 * the data pipeline nothing. */
	static const unsigned char perm[6][3] = {
		{ 0, 1, 2 }, { 0, 2, 1 }, { 1, 0, 2 },
		{ 1, 2, 0 }, { 2, 0, 1 }, { 2, 1, 0 }
	};

	/* --- 130995-131005 ------------------------------------------------ */
	/* WHERE DOES THIS FUNCTION GO? Measured 2026-08-03: the reference reaches
	 * FUNC_001052's gate with DATA_008814 = 0xffffffff and skips the extra
	 * near-camera object; we reach it with 0 and draw the object, which is
	 * what paints the slabs. This function is the ONLY writer of that -1, at
	 * the bottom of the `DATA_009263 == 0` block. Three exits can suppress it
	 * and one counter each tells them apart without another guess. */
	ffe_p1877_calls++;
	/* HALO TIMELINE (2026-09-25): every 16th call, the game clock and the
	 * planet centre relative to the camera, to PROGDIR:ffe_halo.log. The
	 * reference writes the same line (PC shim, pc_halo.log), so the intro's
	 * camera path can be compared at EQUAL CLOCK -- the scenes had stopped
	 * lining up (we sit inside Mars where the reference frames it). */
	if ((ffe_p1877_calls & 15) == 1 && ffe_p1877_calls < 16 * 400 && !ffe_is_quiet ()) {
		extern long DATA_008804_clk __asm__("DATA_008804");
		FILE *hf = fopen (ffe_pd ("ffe_halo.log"), "a");

		if (hf != NULL) {
			fprintf (hf, "HALO clk=%ld c=(%ld,%ld,%ld) atmo=%ld bands=%ld\n",
			         DATA_008804_clk, centre[0], centre[1], centre[2],
			         atmo, bands);
			fclose (hf);
		}
	}
	if (ffe_p1877_n < 6) {
		long *r = ffe_p1877_seq[ffe_p1877_n++];

		r[0] = centre[0]; r[1] = centre[1]; r[2] = centre[2];
		r[3] = atmo; r[4] = bands; r[5] = (long)colour; r[6] = 0; r[7] = 0;
		r[8] = centre[-1];
		r[9] = centre[5]; r[10] = centre[6]; r[11] = centre[7];
	}
#define P1877_EXIT(code) do { if (ffe_p1877_n > 0 && ffe_p1877_n <= 6) \
		ffe_p1877_seq[ffe_p1877_n - 1][7] = (code); } while (0)
	DATA_009262 = atmo;
	DATA_009267 = bands;
	if (atmo == 0) {
		ffe_p1877_noatmo++;
		P1877_EXIT (1);
		return;
	}
	/* THE COLOUR ARRIVES BYTE-REVERSED, and the comment that used to stand
	 * here was WRONG. Measured 2026-08-05, same scenario, both machines:
	 *
	 *     reference  col=0x0002070f          ours  col=0x0f070200
	 *
	 * The MEMORY IS IDENTICAL -- bytes 0f 07 02 00 at DATA_009259+0..3 on
	 * both -- because FUNC_001691 fills it byte-wise (`mov [eax],dx' +
	 * `mov [eax+2],dl', ffe.asm:102603-102606) and writes r,g,b in that
	 * order. Only the DWORD READ differs: `mov eax,[DATA_009259]'
	 * (ffe.asm:129466) is a big-endian load here and a little-endian one
	 * there, so the same three bytes come back as 0xrrggbb00 instead of
	 * 0x00bbggrr. Left as it was, red was read as 0 and blue as 7 -- which
	 * is precisely the flat GREEN our sky was painted.
	 *
	 * WHY THE FIX IS HERE AND NOT IN THE MANIFEST. Two other sites read the
	 * same dword (ffe.asm:115610, 115651) and push it straight into
	 * FUNC_001326_DPal32to16, which takes r,g,b from bytes 0,1,2 of the
	 * PUSHED dword -- those two are already correct precisely because the
	 * load is big-endian, and a label-wide `le' class would break them. A
	 * per-site entry cannot do it either: lower.py's _site_swap returns None
	 * for any operand carrying a symbol, by design. So the one consumer that
	 * wants the NUMERIC x86 value converts it, which is this function.
	 *
	 * DATA_009265 must hold the x86 numeric value too: its four readers use
	 * it arithmetically (`imul ebx,[ebp+0xc]' at ffe.asm:114438 and
	 * `and eax,0xf0f0f' at 114766) on 4-bit-per-channel packed RGB. */
	colour = (unsigned long)(((colour >> 24) & 0xffUL)
	                         | (((colour >> 16) & 0xffUL) << 8)
	                         | (((colour >> 8) & 0xffUL) << 16));

	DATA_009265 = colour & 0x00ffffffUL;
	cr = (long)(colour & 0xffUL);
	cg = (long)((colour >> 8) & 0xffUL);
	cb = (long)((colour >> 16) & 0xffUL);

	/* --- LOOP 1, 131007-131094: greedy full-pivot selection over all six
	 * row permutations, keeping the diagonal away from zero so the
	 * Gaussian solve below has usable pivots. */
	for (i = 0; i < 9; i++)
		M[i] = 0;
	for (k = 0; k < 6; k++) {
		const unsigned char *p = perm[k];
		if (ffe_abs (corners[p[0] * 3 + 0]) <= ffe_abs (M[0])) continue;
		if (ffe_abs (corners[p[1] * 3 + 1]) <= ffe_abs (M[4])) continue;
		if (ffe_abs (corners[p[2] * 3 + 2]) <= ffe_abs (M[8])) continue;
		for (i = 0; i < 3; i++) {
			M[0 + i] = corners[p[0] * 3 + i];
			M[3 + i] = corners[p[1] * 3 + i];
			M[6 + i] = corners[p[2] * 3 + i];
		}
	}

	/* --- 131095-131111 ------------------------------------------------ */
	E[0] = -centre[0];
	E[1] = -centre[1];
	E[2] = -centre[2];
	if (M[0] == 0) {
		ffe_p1877_singular++;
		P1877_EXIT (4);
		return;
	}
	for (i = 0; i < 9; i++)
		Ms[i] = M[i];

	/* --- X87 BLOCK 1, 131112-131209: solve Vf[0]*A + Vf[1]*B + Vf[2]*C =
	 * -CENTRE for the observer's position in the corner basis. Every group
	 * below is one `fld / fmul / fsubr / fstp dword` or `fld / fdiv /
	 * fstp dword`, so the arithmetic is at extended precision and only the
	 * destination is rounded to single -- hence the (double) operands and
	 * the single (float) cast at the store. The int->float widening at
	 * 131112-131135 LOSES low bits above 2^24 and that loss is part of the
	 * algorithm, so it must be a genuine conversion. */
	for (i = 0; i < 9; i++)
		Mf[i] = (float)M[i];
	for (i = 0; i < 3; i++)
		Vf[i] = (float)E[i];

	Mf[2] = (float)((double)Mf[2] / (double)Mf[8]);
	Mf[5] = (float)((double)Mf[5] / (double)Mf[8]);
	Vf[2] = (float)((double)Vf[2] / (double)Mf[8]);
	Mf[1] = (float)((double)Mf[1] - (double)Mf[2] * (double)Mf[7]);
	Mf[0] = (float)((double)Mf[0] - (double)Mf[2] * (double)Mf[6]);
	Mf[4] = (float)((double)Mf[4] - (double)Mf[5] * (double)Mf[7]);
	Mf[3] = (float)((double)Mf[3] - (double)Mf[5] * (double)Mf[6]);
	Vf[1] = (float)((double)Vf[1] - (double)Vf[2] * (double)Mf[7]);
	Vf[0] = (float)((double)Vf[0] - (double)Vf[2] * (double)Mf[6]);

	Mf[1] = (float)((double)Mf[1] / (double)Mf[4]);
	Vf[1] = (float)((double)Vf[1] / (double)Mf[4]);
	Mf[0] = (float)((double)Mf[0] - (double)Mf[1] * (double)Mf[3]);
	Vf[0] = (float)((double)Vf[0] - (double)Vf[1] * (double)Mf[3]);

	Vf[0] = (float)((double)Vf[0] / (double)Mf[0]);
	Vf[1] = (float)((double)Vf[1] - (double)Vf[0] * (double)Mf[1]);
	Vf[2] = (float)((double)Vf[2] - (double)Vf[0] * (double)Mf[2]);
	Vf[2] = (float)((double)Vf[2] - (double)Vf[1] * (double)Mf[5]);

	/* 131198-131209: the multiply stays at extended precision and goes
	 * straight into the truncating store -- no (float) round here. */
	E[0] = (long)((double)Vf[0] * 32768.0);
	E[1] = (long)((double)Vf[1] * 32768.0);
	E[2] = (long)((double)Vf[2] * 32768.0);

	/* --- 131210-131249: reject a degenerate solve, then normalise E into
	 * the 13-bit window the fixed-point work below assumes. */
	mag = ffe_abs (E[0]) + ffe_abs (E[1]) + ffe_abs (E[2]);
	if (mag >= 0x800000L) {
		ffe_p1877_far++;
		P1877_EXIT (2);
		/* 2026-08-05: with the star system fixed, HALF the calls leave
		 * here and the other half reach the shell test with a non-zero
		 * DATA_009263, so nothing ever publishes DATA_008814 = -1. The
		 * old "everything exits near" reading is dead. Record the FIRST
		 * call's inputs and its solved E, so the reference's PC1877 line
		 * can be put beside it: matching arguments would put the fault
		 * in this hand-port's Gauss-Jordan, differing ones upstream in
		 * FUNC_001876. */
		if (ffe_p1877_farmag == 0) {
			ffe_p1877_farmag = mag;
			ffe_p1877_farE[0] = E[0];
			ffe_p1877_farE[1] = E[1];
			ffe_p1877_farE[2] = E[2];
			ffe_p1877_farc[0] = centre[0];
			ffe_p1877_farc[1] = centre[1];
			ffe_p1877_farc[2] = centre[2];
			ffe_p1877_faratmo = atmo;
			ffe_p1877_farbands = bands;
			ffe_p1877_farcol = (long)colour;
			ffe_p1877_farM[0] = M[0];
			ffe_p1877_farM[1] = M[4];
			ffe_p1877_farM[2] = M[8];
		}
		DATA_009262 = 0;
		return;
	}
	shift = Msb1825 (E) - 13;
	if (ffe_p1877_msb == 0) {
		/* centre points at rec+4, so centre[-1] is rec+0 and centre[5..7]
		 * are rec+0x18..0x20 -- the transform's own output before the
		 * object position was added. Recording both halves says whether
		 * the transform produced zero or the object position did. */
		ffe_p1877_rec[0] = centre[-1];
		ffe_p1877_rec[1] = centre[4];
		ffe_p1877_rec[2] = centre[5];
		ffe_p1877_rec[3] = centre[6];
		ffe_p1877_rec[4] = centre[7];
		ffe_p1877_E[0] = E[0];
		ffe_p1877_E[1] = E[1];
		ffe_p1877_E[2] = E[2];
		ffe_p1877_centre[0] = centre[0];
		ffe_p1877_centre[1] = centre[1];
		ffe_p1877_centre[2] = centre[2];
		ffe_p1877_msb = shift + 13;
	}
	if (shift <= 0) {
		ffe_p1877_near++;
		P1877_EXIT (3);
		DATA_009262 = 0;
		return;
	}
	E[0] = SAR (E[0], shift);
	E[1] = SAR (E[1], shift);
	E[2] = SAR (E[2], shift);

	/* --- 131250-131303: eye distance, clamped so the eye can never be
	 * closer than 0x8800 planet-radius units. */
	r2 = mul32 (E[0], E[0]) + mul32 (E[1], E[1]) + mul32 (E[2], E[2]);
	len = Sqrt1467 (r2);
	if (SHL (len, shift) < 0x8800L) {
		long sc = SHL (len, shift);
		/* ((E<<4)+E)<<11 == E*0x8800, wrapping to 32 bits */
		E[0] = mul32 (E[0], 0x8800L) / sc;
		E[1] = mul32 (E[1], 0x8800L) / sc;
		E[2] = mul32 (E[2], 0x8800L) / sc;
		r2  = SAR (0x48400000L, shift * 2);
		len = SAR (0x8800L, shift);
	}

	/* --- 131305-131345: U, the first basis vector of the horizon plane.
	 * Near the +z axis there is no well-defined perpendicular, so fall
	 * back on the x axis. */
	if (ffe_abs (E[0]) + ffe_abs (E[1]) > 0x1f4L) {
		long h = Sqrt1467 (mul32 (E[1], E[1]) + mul32 (E[0], E[0]));
		U[0] = SHL (E[1], 15) / h;
		U[1] = SHL (0 - E[0], 15) / h;
		U[2] = 0;
	} else {
		U[0] = 0x8000L;
		U[1] = 0;
		U[2] = 0;
	}

	/* --- 131347-131367: W = U x E ------------------------------------- */
	W[0] = SAR (mul32 (U[1], E[2]) - mul32 (U[2], E[1]), 15);
	W[1] = SAR (mul32 (U[2], E[0]) - mul32 (U[0], E[2]), 15);
	W[2] = SAR (mul32 (U[0], E[1]) - mul32 (U[1], E[0]), 15);

	/* --- 131368-131422: scale U and W out to the horizon circle's true
	 * screen radius. A negative tangent square means the eye is below the
	 * surface -- nothing to draw. */
	{
		long tan2 = r2 - SAR (0x40000000L, shift * 2);
		long tan, div;

		if (tan2 < 0) {
			ffe_p1877_inside++;
		P1877_EXIT (5);
			return;
		}
		tan = Sqrt1467 (tan2);
		div = len + 1;
		U[0] = mul32 (U[0], tan) / div;
		U[1] = mul32 (U[1], tan) / div;
		U[2] = mul32 (U[2], tan) / div;
		div = SAR (r2, 15) + 1;
		W[0] = mul32 (W[0], tan) / div;
		W[1] = mul32 (W[1], tan) / div;
		W[2] = mul32 (W[2], tan) / div;
	}

	/* --- 131423-131430: the two atmosphere cases are mutually exclusive
	 * and DATA_009263 is how the code below tells them apart. */
	if (SHL (len, shift) > SAR (atmo, 16) + 0x8000L) {
		/* ABOVE THE ATMOSPHERE, 131431-131630 --------------------------
		 * Push the eye direction out to the atmosphere shell, measure
		 * the shell and the centre, and rescale the band count by the
		 * perspective foreshortening between them. */
		long atmohalf = SAR (atmo, 1) + 0x40000000L;
		long sh, d, den;

		T[0] = SHL (Mul1521 (SHL (E[0], 15) / len, atmohalf), 16);
		T[1] = SHL (Mul1521 (SHL (E[1], 15) / len, atmohalf), 16);
		T[2] = SHL (Mul1521 (SHL (E[2], 15) / len, atmohalf), 16);

		for (i = 0; i < 3; i++) {
			long a = Mul1521 (Ms[0 + i], T[0]);
			a += Mul1521 (Ms[3 + i], T[1]);
			a += Mul1521 (Ms[6 + i], T[2]);
			P[i] = a + a + centre[i];
		}

		sh = Msb1825 (P) - 13;
		if (sh <= 0) {
			sh = 0;
		} else {
			P[0] = SAR (P[0], sh);
			P[1] = SAR (P[1], sh);
			P[2] = SAR (P[2], sh);
		}
		DATA_009263 = SHL (Sqrt1467 (mul32 (P[0], P[0]) +
		                             mul32 (P[1], P[1]) +
		                             mul32 (P[2], P[2])), sh);

		P[0] = centre[0];
		P[1] = centre[1];
		P[2] = centre[2];
		sh = Msb1825 (P) - 13;
		if (sh <= 0) {
			sh = 0;
		} else {
			P[0] = SAR (P[0], sh);
			P[1] = SAR (P[1], sh);
			P[2] = SAR (P[2], sh);
		}
		d = SHL (Sqrt1467 (mul32 (P[0], P[0]) +
		                   mul32 (P[1], P[1]) +
		                   mul32 (P[2], P[2])), sh);
		ebx_f = d;

		/* X87 BLOCK 2, 131612-131625. No memory float is involved, so
		 * the exact truncated quotient is what x87 produces; long long
		 * gives it without the 2^53 loss a double would suffer. A zero
		 * denominator would make x87 store the integer indefinite whose
		 * low dword is 0, which the clamp below turns into 1 -- so the
		 * explicit zero test reproduces the original rather than
		 * trapping the 68k divide unit. */
		den = d - DATA_009263;
		if (den == 0)
			DATA_009267 = 0;
		else
			DATA_009267 = (long)(((long long)DATA_009267 *
			                      (long long)d) / (long long)den);
		if (DATA_009267 < 1)
			DATA_009267 = 1;

		/* 131627-131630 */
		cur[0] = DATA_008815;
		cur[1] = DATA_008816;
		cur[2] = DATA_008817;
	} else {
		/* JUMP_008058, INSIDE THE ATMOSPHERE, 131632-131672 -- landed or
		 * cockpit view. Fade the base colour towards white by how deep
		 * into the shell the eye sits. No clamp on this branch. */
		long f, t;

		DATA_009263 = 0;
		f = SHL (SHL (len, shift) - 0x8000L, 15) / SAR (atmo, 16);
		ebx_f = f;

		t = mul32 (mul32 (0x8000L - f, cr), 3);
		cur[0] = (unsigned char)SAR (t, 15);
		t = mul32 (mul32 (0x8000L - f, cg), 3);
		cur[1] = (unsigned char)SAR (t, 15);
		t = mul32 (mul32 (0x8000L - f, cb), 3) + SHL (f, 2);
		cur[2] = (unsigned char)SAR (t, 15);
	}

	/* --- JUMP_008059, 131673-131712: project the eye onto the tangent
	 * plane. The shift count 15-shift goes negative for shift > 15 and the
	 * 5-bit mask in SAR is what makes that match x86. */
	{
		long dv = SAR (r2, 15 - shift) + 1;
		E[0] = SHL (E[0], 15) / dv;
		E[1] = SHL (E[1], 15) / dv;
		E[2] = SHL (E[2], 15) / dv;
	}

	/* --- 131713-131857: inside the atmosphere only, ask FUNC_001878 for
	 * the surface shade at the sub-eye point and fold it into the colour,
	 * publishing the result in DATA_008814..17 for FUNC_001700. */
	/* MISSING BRACES, found 2026-08-05: P1877_EXIT(6) sat OUTSIDE the `if'
	 * and ran on every call, so the ring reported exit=6 -- "shell" -- for
	 * every call whatever path it actually took. The counters were guarded
	 * and stayed honest, which is the only reason the two agreed and the lie
	 * was harmless here; it would not have been if any other exit had run.
	 * A probe that labels every call the same way is not a measurement. */
	if (DATA_009263 != 0) {
		ffe_p1877_shell++;
		P1877_EXIT (6);
	}
	if (DATA_009263 == 0) {
		long c;

		T[0] = SHL (E[0], 15);
		T[1] = SHL (E[1], 15);
		T[2] = SHL (E[2], 15);
		for (i = 0; i < 3; i++) {
			long a = Mul1521 (Ms[0 + i], T[0]);
			a += Mul1521 (Ms[3 + i], T[1]);
			a += Mul1521 (Ms[6 + i], T[2]);
			R[i] = a + a;
		}
		c = Shade1878 (ctx, R);
		if (c == 0) {
			ffe_p1877_shade0++;
			P1877_EXIT (7);
			cur[0] = DATA_008815;
			cur[1] = DATA_008816;
			cur[2] = DATA_008817;
		} else {
			/* BYTE ORDER. The original reads the three channels as
			 * bytes 0,1,2 of the dword it just stored (131809/20/31),
			 * i.e. bits 0..7 / 8..15 / 16..23 on x86. Translated
			 * FUNC_001878 builds its return the same way -- a word
			 * store, a byte store, one dword read (ffe.asm:132264-
			 * 132273) -- so on the 68k those channels come back at
			 * bits 31..24 / 23..16 / 15..8 instead. Translated
			 * FUNC_001326_DPal32to16 agrees: it takes red from the
			 * FIRST byte of the pushed dword (ffe.asm:82766). */
			/* 2026-09-25: CHANGED CONVENTION. FUNC_001878's final
			 * `mov eax,[ebp-0x4]' is now byte-swapped at the site
			 * (le_read_sites.tsv 132273) and its courier bytes pass
			 * through (frame.py PROVEN_SUBWORD), so the return is the
			 * x86 value r | g<<8 | b<<16 -- the same form translated
			 * DPal32to16 has taken since the 2026-09-02 courier fix,
			 * which reads r from the LOW byte. The old reading below
			 * (bits 31..24/23..16/15..8) matched the pre-09-02 build. */
			long ch[3];
			ch[0] = c & 0xffL;
			ch[1] = (c >> 8) & 0xffL;
			ch[2] = (c >> 16) & 0xffL;
			for (i = 0; i < 3; i++) {
				long v = mul32 (ch[i], 0x8000L - ebx_f);
				if (v < 0)
					v += 0x3fffL;
				cur[i] = (unsigned char)(cur[i] +
				                         (unsigned char)SAR (v, 14));
				if (cur[i] > 0xf)
					cur[i] = 0xf;
			}
			ffe_p1877_set++;
	P1877_EXIT (8);
			DATA_008814 = -1L;
			DATA_008815 = cur[0];
			DATA_008816 = cur[1];
			DATA_008817 = cur[2];
		}
	}

	/* --- JUMP_008067, 131858-131863: open a deferred-draw group at depth
	 * key -1, the farthest. */
	Defer1780 (1, -1L);

	/* --- LOOP 2, 131866-131909: a linear r,g,b ramp from the base colour
	 * to the shaded colour, seven entries of four bytes.
	 *
	 * BYTE ORDER. FUNC_001879 reads entry 0 -- and only entry 0 -- as one
	 * dword (`mov eax,[ebx]`, ffe.asm:132317), adds FUNC_001878's packed
	 * return to it and hands the sum to FUNC_001326_DPal32to16. Since
	 * 2026-09-25 that read is byte-swapped at the site, so a plain
	 * byte-wise r,g,b write here arrives as the x86 value r | g<<8 | b<<16,
	 * the form FUNC_001878 now returns and DPal32to16 reads. Entries 1..6 are read
	 * byte-wise at 132411/132421/132429 and are written the same way. The
	 * fourth byte is the one thing added to the original, which leaves it
	 * uninitialised: it lands outside the three channels DPal32to16
	 * reads on both machines now, and is zeroed anyway. */
	for (k = 0; k < 7; k++) {
		long w = 6 - k;
		ramp.b[k * 4 + 0] = (unsigned char)
			((mul32 (cr, w) + mul32 ((long)cur[0], k)) / 6);
		ramp.b[k * 4 + 1] = (unsigned char)
			((mul32 (cg, w) + mul32 ((long)cur[1], k)) / 6);
		ramp.b[k * 4 + 2] = (unsigned char)
			((mul32 (cb, w) + mul32 ((long)cur[2], k)) / 6);
		ramp.b[k * 4 + 3] = 0;
	}

	/* --- LOOP 3, 131911-132155: walk the horizon circle in 32 steps,
	 * projecting three ring radii per step (inner 7/8, the ring itself,
	 * and the outer atmosphere shell) and emitting the banded quad that
	 * closes the gap back to the previous step. */
	for (theta = 0; theta < 0x10000L; theta += 0x800L) {
		long a, b;

		a = mul32 (Sin1913 (theta + 0x4000L), U[0]);
		b = mul32 (Sin1913 (theta), W[0]);
		T[0] = a + b + SHL (E[0], 15);
		a = mul32 (Sin1913 (theta + 0x4000L), U[1]);
		b = mul32 (Sin1913 (theta), W[1]);
		T[1] = a + b + SHL (E[1], 15);
		a = mul32 (Sin1913 (theta + 0x4000L), U[2]);
		b = mul32 (Sin1913 (theta), W[2]);
		T[2] = a + b + SHL (E[2], 15);

		for (i = 0; i < 3; i++) {
			long t = Mul1521 (Ms[0 + i], T[0]);
			t += Mul1521 (Ms[3 + i], T[1]);
			t += Mul1521 (Ms[6 + i], T[2]);
			R[i] = t + t;
		}

		for (i = 0; i < 3; i++)
			P[i] = R[i] + centre[i];
		if (Project1479 (&s20, P) != 0) {
			s20.x = (short)0x8002;
			s20.y = (short)0x8002;
		}

		for (i = 0; i < 3; i++)
			P[i] = Mul1521 (R[i], atmo) + R[i] + centre[i];
		if (Project1479 (&s24, P) != 0) {
			s24.x = (short)0x8002;
			s24.y = (short)0x8002;
		}

		for (i = 0; i < 3; i++)
			P[i] = R[i] - SAR (R[i], 3) + centre[i];
		if (Project1479 (&s1c, P) != 0) {
			s1c.x = (short)0x8002;
			s1c.y = (short)0x8002;
		}

		if (theta == 0) {
			/* seed the wrap-around vertices */
			s04 = s1c;
			s08 = s20;
			s0c = s24;
		} else {
			Quad1879 (ctx, &s10, &s14, &s18, &s1c, &s20, &s24,
			          ramp.b, R);
		}
		s10 = s1c;
		s14 = s20;
		s18 = s24;
	}

	/* --- 132156-132179: the 32nd quad, wrapping back to the theta == 0
	 * vertices, then close the deferred-draw group. */
	Quad1879 (ctx, &s04, &s08, &s0c, &s10, &s14, &s18, ramp.b, R);
	Defer1780 (0, 0);
}
