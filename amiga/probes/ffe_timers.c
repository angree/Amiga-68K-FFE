/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 angree. See LICENSE at the repository root.
 *
 * C replacements for the LAST two translator-refused REACHABLE functions:
 * FUNC_000118 (ffe.asm:12772-13103, the AVI chunk pump) and FUNC_000120
 * (ffe.asm:13752-13952, the video player loop). Both are refused for one
 * reason only -- a handful of `fild/fcomp(p)/fnstsw/sahf` frame-timer
 * comparisons -- so everything else here is a line-by-line port of the
 * disassembly, with the ffe.asm line range cited on every block.
 *
 * Same legality argument as Func833_c in ffe_game.c: these are called from
 * TRANSLATED code under the t-ABI, where d2-d7/a2-a6 are scratch, and a C
 * callee saves strictly MORE than that, so a C body behind the bare symbol is
 * safe. massgen must exclude both names or the link sees them twice.
 *
 * The reverse direction is NOT safe, which is why every call back into
 * translated code below goes either through that function's `_`-prefixed
 * C-ABI wrapper or, where the target is reached through one of the game's own
 * function-pointer slots, through the FfeTabiCallN thunks at the top of this
 * file.
 */
#include <math.h>
#include <string.h>

/* ------------------------------------------------------------------------
 * t-ABI call thunks.
 *
 * FUNC_000118/FUNC_000120 call through four of the game's function-pointer
 * slots -- [DATA_008606] (the frame decoder it installs itself),
 * [DATA_007672] (GuiGetLastAction), [DATA_007673] (GuiSetLastAction) and
 * [DATA_007217] (FUNC_000922). The values in those slots are BARE t-ABI
 * entry points, written there by translated code, so they cannot be called
 * as C function pointers: the callee treats d2-d7/a2-a6 as scratch and this
 * file's locals live there. These thunks are the same wrapper emit.c_entry()
 * generates for a known callee, with the target taken from a register
 * instead of being a fixed symbol -- including its "same displacement for
 * every argument" trick (each push moves sp down 4 and the next argument to
 * fetch is 4 lower, so the two cancel).
 *
 * Two more callees need them for a different reason: FUNC_001637_FlipScreen
 * and FUNC_001529_SysExit do not open with `push ebp / mov ebp,esp`, so
 * has_standard_prologue() is false for both and massgen emits NO `_FUNC_...`
 * wrapper for them (verified: no xdef _FUNC_001637 / _FUNC_001529 anywhere
 * in build\xlat-full).
 *
 * Stack on entry: 0(sp)=return address, 4(sp)=fp, 8(sp)=arg1 ...
 * After the 11-register MOVEM (44 bytes) that becomes 48(sp)=fp, 52(sp)=arg1.
 */
__asm__ (
"	.text\n"
"	.even\n"
"	.globl	_FfeTabiCall0\n"
"_FfeTabiCall0:\n"
"	movem.l	d2-d7/a2-a6,-(sp)\n"
"	move.l	48(sp),a0\n"
"	jsr	(a0)\n"
"	movem.l	(sp)+,d2-d7/a2-a6\n"
"	rts\n"
"	.even\n"
"	.globl	_FfeTabiCall1\n"
"_FfeTabiCall1:\n"
"	movem.l	d2-d7/a2-a6,-(sp)\n"
"	move.l	48(sp),a0\n"
"	move.l	52(sp),-(sp)\n"
"	jsr	(a0)\n"
"	addq.l	#4,sp\n"
"	movem.l	(sp)+,d2-d7/a2-a6\n"
"	rts\n"
"	.even\n"
"	.globl	_FfeTabiCall10\n"
"_FfeTabiCall10:\n"
"	movem.l	d2-d7/a2-a6,-(sp)\n"
"	move.l	48(sp),a0\n"
"	move.l	88(sp),-(sp)\n"		/* arg 10 */
"	move.l	88(sp),-(sp)\n"		/* arg 9  */
"	move.l	88(sp),-(sp)\n"		/* arg 8  */
"	move.l	88(sp),-(sp)\n"		/* arg 7  */
"	move.l	88(sp),-(sp)\n"		/* arg 6  */
"	move.l	88(sp),-(sp)\n"		/* arg 5  */
"	move.l	88(sp),-(sp)\n"		/* arg 4  */
"	move.l	88(sp),-(sp)\n"		/* arg 3  */
"	move.l	88(sp),-(sp)\n"		/* arg 2  */
"	move.l	88(sp),-(sp)\n"		/* arg 1  */
"	jsr	(a0)\n"
"	lea	40(sp),sp\n"
"	movem.l	(sp)+,d2-d7/a2-a6\n"
"	rts\n"
);

extern long FfeTabiCall0 (long fp) __asm__("_FfeTabiCall0");
extern long FfeTabiCall1 (long fp, long a1) __asm__("_FfeTabiCall1");
extern long FfeTabiCall10 (long fp, long a1, long a2, long a3, long a4,
                           long a5, long a6, long a7, long a8, long a9,
                           long a10) __asm__("_FfeTabiCall10");

/* ---- the platform layer: already C, called by their own names ---------- */
extern unsigned long TimerGetTimeStamp (void) __asm__("_TimerGetTimeStamp");
extern long SoundStreamGetUsedBlocks (void) __asm__("_SoundStreamGetUsedBlocks");
extern char *DirMakeAVIName (char *pBuf, char *pStub) __asm__("_DirMakeAVIName");

/* ---- translated callees, through their C-ABI wrappers ------------------ */
extern long Func109 (void) __asm__("_FUNC_000109");
extern long Func111 (void) __asm__("_FUNC_000111");
extern long Func113 (long a, long b) __asm__("_FUNC_000113");
extern long Func114 (long a, long b) __asm__("_FUNC_000114");
extern long Func115 (long a, long b) __asm__("_FUNC_000115");
extern long Func116 (long a, long b) __asm__("_FUNC_000116");
extern long Func117 (void) __asm__("_FUNC_000117");
extern long Func123 (long a, long b, long c) __asm__("_FUNC_000123");
extern long Func124 (long a, long b) __asm__("_FUNC_000124");
extern long Func560 (void) __asm__("_FUNC_000560");
extern long Func1418_TimerAdvance (void) __asm__("_FUNC_001418");
extern long Func1582_TextWriteStringOnly (long s) __asm__("_FUNC_001582");
extern long Func1583_TextWriteDefaultColor (long s, long x, long y)
	__asm__("_FUNC_001583");
extern long FuncExtraVideo (long name, long size) __asm__("_FUNC_extravideo");

/* ---- translated callees reached as BARE t-ABI addresses ---------------- */
extern void Func119_body (void) __asm__("FUNC_000119");	/* CRAM decoder */
extern void Func121_body (void) __asm__("FUNC_000121");	/* RGB decoder  */
extern void Func1637_FlipScreen (void) __asm__("FUNC_001637");
extern void Func1529_SysExit (void) __asm__("FUNC_001529");

/* ---- game globals -------------------------------------------------------
 * Storage class from build\data-be\ffedat_be.storage.tsv. be32 labels are
 * read and written as native longs; `le` and `plain` labels keep the raw x86
 * byte image, and every access below to one of those is a BYTE access at the
 * same offset the x86 used, so no byte order is involved.
 */
extern long D_2320 __asm__("DATA_002320");	/* be32   AVI FILE*        */
extern long D_2321 __asm__("DATA_002321");	/* be32   name table ptr   */
extern long D_2322 __asm__("DATA_002322");	/* be32   name index arg   */
extern long D_2323[15] __asm__("DATA_002323");	/* be32   60 bytes         */
extern unsigned char D_2324[90] __asm__("DATA_002324");	/* plain  strings  */
extern long D_7217 __asm__("DATA_007217");	/* be32   -> FUNC_000922   */
extern long D_7672 __asm__("DATA_007672");	/* be32   GuiGetLastAction */
extern long D_7673[3] __asm__("DATA_007673");	/* be32   GuiSetLastAction */
extern long D_7706 __asm__("DATA_007706");	/* be32   pause counter    */
extern long D_7902 __asm__("DATA_007902");	/* be32   sound enabled    */
extern long D_8584 __asm__("DATA_008584");	/* be32   frame width      */
extern long D_8585 __asm__("DATA_008585");	/* be32   frame height     */
extern long D_8587 __asm__("DATA_008587");	/* be32   bits per pixel   */
extern long D_8588 __asm__("DATA_008588");	/* be32   codec FOURCC     */
extern unsigned char D_8595[56] __asm__("DATA_008595");	/* plain  avih hdr */
extern long D_8596[12] __asm__("DATA_008596");	/* be32   strh header      */
extern unsigned char D_8597[44] __asm__("DATA_008597");	/* plain  strf hdr */
extern long D_8598[655] __asm__("DATA_008598");	/* be32   2620 bytes       */
extern unsigned char D_8605[14400] __asm__("DATA_008605");	/* plain frame */
extern unsigned char D_8606[20000] __asm__("DATA_008606");	/* plain ctx   */
extern long D_8607 __asm__("DATA_008607");	/* be32   frame x          */
extern long D_8608 __asm__("DATA_008608");	/* be32   frame y          */
extern long D_8609 __asm__("DATA_008609");	/* be32   frame x + w      */
extern long D_8610 __asm__("DATA_008610");	/* be32   frame y + h      */
extern long D_8611 __asm__("DATA_008611");	/* be32   frame counter    */
extern unsigned char D_8612[4] __asm__("DATA_008612");	/* le, byte 0 only */
extern long D_8613 __asm__("DATA_008613");	/* be32   file size        */
extern long D_8616 __asm__("DATA_008616");	/* be32   bytes left       */
extern long D_8618 __asm__("DATA_008618");	/* be32   last time stamp  */
extern unsigned char D_9027[2] __asm__("DATA_009027");	/* le, byte 0 only */

/* A dword field of the decoder context at DATA_008606. The label is `plain`
 * -- raw bytes, address-taken -- and it lives in BSS, so there is no image to
 * byte-swap; the fields are written here and read back by the translated
 * decoder with move.l, i.e. native, on both sides. */
#define CTXL(off)	(*(long *)(D_8606 + (off)))

/* The 68k stores IEEE numbers big-endian, so feeding the x86's little-endian
 * image REVERSED rebuilds the exact bit pattern with no decimal round-trip.
 * Same helper as ffe_x87.c's from_bits(). */
static double dbl_from_be (const unsigned char *be)
{
	union { double d; unsigned char b[8]; } u;
	int i;

	for (i = 0; i < 8; i++)
		u.b[i] = be[i];
	return u.d;
}

static double flt_from_be (const unsigned char *be)
{
	union { float f; unsigned char b[4]; } u;
	int i;

	for (i = 0; i < 4; i++)
		u.b[i] = be[i];
	return (double)u.f;
}

/* FUNC_002110 (ffe.asm:133015-133054), FUNC_000120's only callee that the
 * translator also refuses, and FUNC_000120 is its only caller -- so it is
 * inlined here as a static instead of being given its own symbol.
 *
 * It is Watcom's fmod(): `fld qword [ebp+0x10]` puts the SECOND argument on
 * the stack first, `fld qword [ebp+0x8]` the first argument on top, and the
 * fprem loop leaves ST(0) = ST(0) rem ST(1) with a round-to-zero quotient --
 * exactly C's fmod(x, m). The two exponent tests are done on the high word of
 * each double (`mov ax,[ebp+0x16]` / `[ebp+0xe]` are bytes 6-7 of the two
 * qwords), shifted left once to drop the sign:
 *   modulus  == 0 (exponent 0 and top mantissa nibble 0)  -> 0.0   (13023)
 *   modulus  >= 0xffe0 (exponent all ones: Inf/NaN)       -> x     (13025)
 *   value    == 0 or >= 0xffe0                            -> 0.0   (13029/31)
 * On the 68k the high word is w[0]; on the x86 it was the byte pair at +6.
 * Same word of the same value, so the tests carry over unchanged. */
static double Func2110_fmod (double x, double m)
{
	union { double d; unsigned short w[4]; } u;
	unsigned short hi;

	u.d = m;					/* 133020-133021 */
	hi = (unsigned short)(u.w[0] << 1);		/* 133022         */
	if (hi == 0)					/* 133023 -> 8987 */
		return 0.0;
	if (hi >= 0xffe0)				/* 133024 -> 8985 */
		return x;
	u.d = x;					/* 133026-133027 */
	hi = (unsigned short)(u.w[0] << 1);		/* 133028         */
	if (hi == 0 || hi >= 0xffe0)			/* 133029/133031  */
		return 0.0;
	return fmod (x, m);				/* 133032-133040  */
}

/* ========================================================================
 * FUNC_000118 (ffe.asm:12772-13088) -- read one AVI chunk and act on it.
 *
 * Signature: one dword argument at [ebp+8] (a member of the 52-function
 * module-event handler family, listed in tools\translator\frame.py; every
 * dispatcher pushes a whole dword). The argument is loaded into eax at 12779
 * and then immediately overwritten -- it is dead. The return value IS live:
 * FUNC_000913 (ffe.asm:67960) tail-forwards it to its own caller, so eax is
 * reproduced below at every exit, which for the call-terminated paths simply
 * means returning what the callee returned.
 * ======================================================================== */
long Func118_c (long ev) __asm__("FUNC_000118");
long Func118_c (long ev)
{
	/* x86 LE images of the two frame-rate constants, reversed to BE:
	 *   DATA_000013 (ffe.asm:13094) ab aa aa aa aa aa 50 40 -> 66.666...
	 *   DATA_000014 (ffe.asm:13097) ab aa aa aa aa aa 40 40 -> 33.333...
	 * They must NOT be read from their labels: support_data.s emits both as
	 * the raw little-endian byte image. */
	static const unsigned char c13[8] =
		{0x40, 0x50, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xab};
	static const unsigned char c14[8] =
		{0x40, 0x40, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xab};
	const double C13 = dbl_from_be (c13);
	const double C14 = dbl_from_be (c14);

	long esi, ebx = 0, eax = 0, t;
	long a4 = 0, a8 = 0, ac = 0, a10 = 0;	/* [ebp-4/-8/-0xc/-0x10] */

	(void)ev;			/* 12779: loaded into eax, then dead */

	/* 12780-12783 */
	if (!(D_8616 > 0) && !(D_8613 > 0))
		goto L1031;

	/* JUMP_001001, 12785-12788 */
	esi = Func111 ();
	ebx = Func109 ();

	/* 12789-12842: the compiler's binary search over the chunk FOURCC.
	 * Every arm is a signed compare or a running `sub esi,K / jz`; the
	 * accumulated constants are the values below, and each one decodes to
	 * a real RIFF/AVI chunk id, which is what confirms the arithmetic.
	 * The two-value arm at 12800-12802 is `add esi,0xffffdceb / sub esi,2
	 * / jc`, i.e. the UNSIGNED range [0x30306462, 0x30306463] = 00db/00dc. */
	switch (esi) {
	case 0x52494646L:			/* 'RIFF' 12789-12791 */
		goto L1006;
	case 0x4c495354L:			/* 'LIST' 12817-12818 */
		goto L1007;
	case 0x61766968L:			/* 'avih' 12824-12825 */
		goto L1008;
	case 0x73747268L:			/* 'strh' 12834-12835 */
		goto L1009;
	case 0x73747266L:			/* 'strf' 12821-12823 */
		goto L1010;
	case 0x30306478L:			/* '00dx' 12795-12797 */
	case 0x30306462L:			/* '00db' 12800-12802 */
	case 0x30306463L:			/* '00dc' 12800-12802 */
	case 0x30306976L:			/* '00iv' 12805-12806 */
	case 0x30307878L:			/* '00xx' 12807-12808 */
		goto L1012;
	case 0x30317762L:			/* '01wb' 12792-12794 */
		goto L1026;
	case 0x3030414dL:			/* '00AM' 12798-12799 */
	case 0x44495350L:			/* 'DISP' 12811-12812 */
	case 0x4953424aL:			/* 'ISBJ' 12813-12814 */
	case 0x4a554e4bL:			/* 'JUNK' 12815-12816 */
	case 0x6864726cL:			/* 'hdrl' 12826-12827 */
	case 0x69647831L:			/* 'idx1' 12828-12829 */
	case 0x73747264L:			/* 'strd' 12830-12831 */
	case 0x7374726cL:			/* 'strl' 12836-12837 */
	case 0x76656474L:			/* 'vedt' 12838-12839 */
	case 0x76696473L:			/* 'vids' 12840-12841 */
		goto L1028;
	default:				/* 12803/12809/12819/... */
		goto L1029;
	}

L1006:	/* 12843-12845 */
L1007:	/* 12846-12848 -- the same two instructions */
	eax = Func111 ();
	goto L1032;

L1008:	/* 12849-12856 */
	eax = Func113 (ebx, (long)D_8595);
	goto L1032;

L1009:	/* 12857-12864 */
	eax = Func114 (ebx, (long)D_8596);
	goto L1032;

L1010:	/* 12865-12877 */
	if (D_8596[0] == 0x61756473L)		/* 'auds' */
		goto L1011;
	eax = D_8596[0] - 0x76696473L;		/* 'vids'; the running `sub eax` */
	if (eax != 0)
		goto L1032;
	eax = Func115 (ebx, (long)D_8597);
	goto L1032;

L1011:	/* 12878-12886 */
	ebx = (ebx + 1) & ~1L;
	eax = Func124 (D_2320, ebx);
	goto L1032;

L1012:	/* 12887-12890 */
	if (++D_8611 != 0)
		Func560 ();
	/* JUMP_001013, 12891-12898: idiv by 5. x86 idiv truncates toward zero
	 * and so do C's / and %, so both halves carry over unchanged. eax is
	 * left holding the QUOTIENT, which is what two exits below return. */
	eax = D_8611 / 5;
	if (D_8611 % 5 == 0)
		D_8612[0] = 1;
	/* JUMP_001014, 12899-12906 */
	if (D_8612[0] != 1)
		goto L1025;
	ebx = (ebx + 1) & ~1L;
	if (ebx == 0)
		goto L1032;			/* eax = the quotient above */
	/* 12907-12917 */
	CTXL (0x04) = 0x1100;
	CTXL (0x20) = 0;
	CTXL (0x24) = ebx;
	Func123 (D_2320, (long)(D_8606 + 0x28), ebx);
	/* 12918-12934 */
	D_8609 = D_8607 + D_8584;
	D_8610 = D_8608 + D_8585;
	CTXL (0x0c) = 0;
	CTXL (0x08) = 0;
	CTXL (0x10) = D_8584;
	CTXL (0x14) = D_8585;
	CTXL (0x18) = 0;
	CTXL (0x1c) = 0;
	/* 12935-12949: pick the decoder by codec FOURCC. Both arms reach
	 * JUMP_001032 with eax == 0 (the `sub eax` that selected them left it
	 * zero); the default arm returns the running difference. */
	if (D_8588 == 0x4352414dL) {		/* 'CRAM', JUMP_001015 */
		eax = 0;
		if (D_8587 != 8)
			goto L1032;
		CTXL (0x00) = (long)Func119_body;
	} else if (D_8588 == 0x52474220L) {	/* 'RGB ', JUMP_001016 */
		eax = 0;
		if (D_8587 != 8)
			goto L1032;
		CTXL (0x00) = (long)Func121_body;
	} else {
		eax = D_8588 - 0x4352414dL - 0x0ef500d3L;
		goto L1032;
	}
	/* JUMP_001017, 12950-12954 */
	if (D_8598[0] != 0 && (unsigned long)D_8587 > 8UL)
		goto L1019;
	/* JUMP_001018, 12955-12979: ten cdecl arguments through the slot the
	 * two branches above just filled in. eax ends up holding [ebp-8]
	 * because of the two subtractions, but no path out of here reads it. */
	eax = FfeTabiCall10 (CTXL (0x00), (long)D_8605,
	                     (long)(D_8606 + 0x28), CTXL (0x24),
	                     D_8584, D_8585, 8,
	                     (long)&a4, (long)&a8, (long)&ac, (long)&a10);
	ac -= a4;
	a10 -= a8;
	eax = a8;

L1019:	/* 12980-12991 */
	if (D_7902 == 0)
		goto L1023;
	if (D_8611 != 0)
		goto L1020;
	eax = Func116 (3, 0);
	if (eax != 0)
		goto L1024;
	goto L1032;

L1020:	/* 12992-12994 */
	D_8618 = (long)TimerGetTimeStamp ();

	/* JUMP_001021, 12995-13007: hold the frame until the sound buffer has
	 * caught up, but no longer than one 15 Hz tick.
	 *
	 *   fild dword [ebp-0x14]      ST0 = (double)now
	 *   fild dword [DATA_008618]   ST0 = (double)start, ST1 = now
	 *   fadd qword [DATA_000013]   ST0 = start + 66.666...
	 *   fcompp / fnstsw / sahf     CF = C0 = (ST0 < ST1) = start+C13 < now
	 *   jnc JUMP_001021            loop while NOT(start+C13 < now)
	 *
	 * i.e. keep polling while start + 66.666ms >= now. */
	for (;;) {
		eax = SoundStreamGetUsedBlocks ();
		if (eax >= D_8611)		/* jnl: signed >= */
			break;
		t = (long)TimerGetTimeStamp ();
		if (!((double)D_8618 + C13 >= (double)t))
			break;
	}

	/* JUMP_001022, 13008-13013 */
	eax = SoundStreamGetUsedBlocks ();
	if (eax <= D_8611)			/* jng: signed <= */
		goto L1024;
	D_8612[0] = 0;
	goto L1024;

L1023:	/* 13014-13024: no sound -- plain 30 Hz frame limiter.
	 *
	 *   fild dword [ebp-0x14]      ST0 = (double)(now - start)
	 *   fcomp qword [DATA_000014]  CF = C0 = (ST0 < 33.333...)
	 *   jc JUMP_001023             loop while the delta is still under it */
	do {
		t = (long)TimerGetTimeStamp () - D_8618;
	} while ((double)t < C14);
	D_8618 = (long)TimerGetTimeStamp ();

L1024:	/* 13025-13029 */
	Func1418_TimerAdvance ();
	eax = FfeTabiCall0 ((long)Func1637_FlipScreen);
	goto L1032;

L1025:	/* JUMP_001025, 13030-13036 -- note: ebx is NOT rounded up here */
	eax = Func124 (D_2320, ebx);
	goto L1032;

L1026:	/* 13037-13046 */
	if (D_7902 == 0)
		goto L1027;
	eax = Func116 (2, ebx);
	goto L1032;

L1027:	/* 13047-13055 */
L1028:	/* 13056-13064 -- the same five instructions */
	ebx = (ebx + 1) & ~1L;
	eax = Func124 (D_2320, ebx);
	goto L1032;

L1029:	/* 13065-13069. eax still holds FUNC_000109's return here (12788
	 * copied it to ebx and nothing has written eax since). */
	eax = ebx;
	if (!(D_8616 > 0) && !(D_8613 > 0))
		goto L1032;

L1031:	/* JUMP_001030 13070-13076 and JUMP_001031 13077-13082 -- the same
	 * four calls, and 13069 falls into 13070, so one block covers both */
	Func117 ();
	Func560 ();
	Func1418_TimerAdvance ();
	eax = FfeTabiCall0 ((long)Func1637_FlipScreen);

L1032:	/* 13083-13088 */
	return eax;
}

/* ========================================================================
 * FUNC_000120 (ffe.asm:13752-13932) -- play one AVI.
 *
 * Signature: one dword argument, pushed by its only caller (FUNC_000913,
 * `push ebx / call FUNC_000120 / pop ecx` at ffe.asm:67968-67970), and never
 * read by the body. The return value IS read (`test eax,eax` at 67971) and
 * is ZERO on every path -- every exit runs `xor eax,eax` first.
 * ======================================================================== */
/* jjffe's own patch at ffe.asm:13881-13882: the "video took too long" branch
 * was rerouted from JUMP_1124 to JUMP_1132. 1 reproduces the shipping jjffe
 * behaviour; 0 restores the original executable's.
 *
 * VOLATILE on purpose. As a plain constant the compiler folds the branch and
 * deletes JUMP_1124..JUMP_1127 -- correct, but it would also delete the only
 * two x87 comparisons in this function and the FUNC_002110 port with them,
 * leaving nothing in the object to check against the disassembly. A
 * translated FUNC_000120 would carry that block; so does this one. */
static const volatile int JJFFE_VIDEO_TIMEOUT_PATCH = 1;

long Func120_c (long arg) __asm__("FUNC_000120");
long Func120_c (long arg)
{
	/* The two float constants at ffe.asm:13934/13939, which ffedis
	 * mis-disassembled as instructions; support_data.s carries their raw
	 * little-endian bytes (00 60 6a 47 / 00 00 fa 43). Reversed to BE they
	 * are 60000.0f and 500.0f. */
	static const unsigned char vd1[4] = {0x47, 0x6a, 0x60, 0x00};
	static const unsigned char vd2[4] = {0x43, 0xfa, 0x00, 0x00};
	const double VD1 = flt_from_be (vd1);
	const double VD2 = flt_from_be (vd2);

	long ebx = 0, eax = 0;
	char name[256];				/* [ebp-0x104] */
	unsigned char msg1[260];		/* [ebp-0x208] */
	unsigned char msg2[260];		/* [ebp-0x30c] */
	struct { short w0, w1; } sz;		/* [ebp-0x4], [ebp-0x2] */

	(void)arg;

	/* 13762: FIX (jjffe) -- pause the game clock for the duration */
	D_7706 = 0;

	/* 13767-13775. esi = DATA_007672 and edi = DATA_002324 in the original;
	 * both are used only as bases, so they are spelled out below. */
	if (D_2321 == 0 || D_9027[0] == 0) {	/* JUMP_1121 */
		eax = 0;
		goto L1133;
	}

	/* JUMP_1122, 13777-13784 */
	Func117 ();
	FfeTabiCall0 ((long)Func1637_FlipScreen);
	if (D_7902 != 0)
		Func116 (1, 0);

	/* JUMP_1123, 13786-13809 */
	D_8611 = -1;
	sz.w0 = 1;
	sz.w1 = 0x11;
	eax = FfeTabiCall1 (D_7217, D_2322);	/* [DATA_007217] = FUNC_000922 */
	DirMakeAVIName (name, (char *)((long *)D_2321)[eax]);
	D_2321 = 0;
	D_8612[0] = 1;
	FfeTabiCall0 (D_7672);			/* GuiGetLastAction */
	D_8618 = (long)TimerGetTimeStamp ();
	goto L1128;

	/* JUMP_1124, 13811-13839. UNREACHABLE in the jjffe build: the only
	 * branch here, `jz near JUMP_1124` at 13881, is commented out and
	 * replaced by `jz near JUMP_1132`. Ported anyway so that clearing
	 * JJFFE_VIDEO_TIMEOUT_PATCH below restores the original behaviour, and
	 * because both of this function's x87 comparisons live in it. */
L1124:
	ebx = (long)TimerGetTimeStamp () - D_8618;
	/* 13815-13819: fild dword; fcomp dword [VideoData1]; jna JUMP_1126
	 *   CF = C0 = (elapsed < 60000), ZF = C3 = (elapsed == 60000)
	 *   jna = CF|ZF -> branch when elapsed <= 60000.0 */
	if ((double)ebx <= VD1)
		goto L1126;
	/* 13820-13834: "Press a key to exit" at x=0x14, y=0x64 */
	msg1[0] = 0x14;
	msg1[1] = 0x64;
	memcpy (msg1 + 2, D_2324 + 0x39, 20);	/* rep movsd, ecx = 5 */
	Func1582_TextWriteStringOnly ((long)msg1);
	/* JUMP_1125, 13836-13839 */
	while ((FfeTabiCall0 (D_7672) & 0xff) == 0)
		;
	FfeTabiCall0 ((long)Func1529_SysExit);	/* does not return */

L1126:	/* 13841-13870 */
	Func117 ();
	Func560 ();
	msg2[0] = 0x14;
	msg2[1] = 0x64;
	memcpy (msg2 + 2, D_2324 + 0x4d, 13);	/* rep movsd ecx=3 + movsb */
	/* 13855-13866: FUNC_002110(elapsed, 1000.0) -- the 0x408f4000 pushed
	 * over a zero dword is the double 1000.0 -- then
	 *   fcomp dword [VideoData2]; jnc JUMP_1127
	 *   CF = C0 = (remainder < 500.0); jnc skips the text
	 * so "Insert CDROM" blinks on for the first half of every second. */
	if (Func2110_fmod ((double)ebx, 1000.0) < VD2)
		Func1582_TextWriteStringOnly ((long)msg2);

	/* JUMP_1127, 13872 */
	FfeTabiCall0 ((long)Func1637_FlipScreen);

L1128:	/* 13874-13888 */
	eax = FuncExtraVideo ((long)name, (long)&sz);
	if (eax == 0) {
		/* jjffe replaced 13881 `jz near JUMP_1124` with 13882
		 * `jz near JUMP_1132`; both spellings are kept so the
		 * disassembly still reads one-to-one. */
		if (JJFFE_VIDEO_TIMEOUT_PATCH)
			goto L1132;	/* 13882 */
		goto L1124;		/* 13881, commented out in ffe.asm */
	}
	D_2323[0] = 1;
	FfeTabiCall0 (D_7672);
	FfeTabiCall0 (D_7672);
	D_8618 = (long)TimerGetTimeStamp ();
	goto L1131;

L1129:	/* 13891-13904 */
	ebx = FfeTabiCall0 (D_7672) & 0xff;
	if (ebx == 0)
		goto L1130;
	FfeTabiCall1 (D_7673[0], ebx);		/* GuiSetLastAction */
	Func117 ();
	Func560 ();
	Func1418_TimerAdvance ();
	FfeTabiCall0 ((long)Func1637_FlipScreen);
	eax = 0;
	goto L1133;

L1130:	/* 13906-13908 */
	Func118_c (0);

L1131:	/* 13910-13913 */
	if (D_2320 != 0)
		goto L1129;
	eax = 0;
	goto L1133;

L1132:	/* 13915-13921 */
	Func1583_TextWriteDefaultColor ((long)(D_2324 + 0x4d), 0x14, 0x64);
	eax = 0;

L1133:	/* 13925-13932: FIX (jjffe) -- restore the game clock */
	D_7706 = 0x3e2;
	return eax;
}

/* MODEL VARIABLES, FUNC_001473 / FUNC_001474 (ffe.asm, "Pos = 515f1/5164c"),
 * carried in C since 2026-10-09 (legal for the same reason as the shims
 * above: a t-ABI caller assumes less than the C ABI provides).
 *
 * Mode 0x80 hands a model the OBJECT's fields as an array of 16-bit words:
 * `movzx eax,word [obj + idx*2 + 0x9c]'. Words 2..9 are the two halves of
 * four DWORD fields -- +0xa0 (the object's unique id / seed), +0xa4, +0xa8,
 * +0xac -- which the game writes whole. On x86 word 2 is the LOW half of the
 * seed; in our big-endian object it is the HIGH half, so every model that
 * picks a variant from the seed picked a different one: at Old Blackelk the
 * reference draws tower models 303 and 400 where we drew 401 twice, and
 * models 397 and 403 were missing altogether (FRAME1478 `k=' column, both
 * machines). A displacement delta cannot express "swap neighbours under a
 * runtime index", hence C. Words 0/1 (+0x9c/+0x9e) and 10.. are real words
 * and stay as they are (word 0 was measured identical on both machines,
 * 2026-08-06). build\xlat-full\FUNC_001473.s / FUNC_001474.s must NOT exist
 * (massgen.py excludes both), or the link sees each symbol twice. */
static long modvar_word (const unsigned char *ctx, long idx)
{
	const unsigned char *obj = *(const unsigned char *const *)(ctx + 0x14c);

	if (idx >= 2 && idx <= 9)
		idx ^= 1;
	/* Words 10/11 (+0xb0/+0xb2): a WORD pair in a ship but ONE DWORD in a
	 * system body -- the orbit radius (FUNC_000880/986; field atlas: 11 dword
	 * accesses vs the ship layout's words). FUNC_000986 copies it whole into
	 * the orbit-ellipse object (model 0x96, ffe.asm:73292) whose model reads
	 * var 0x8a = the LOW half: the PC gets 0x4f10 from 0x00254f10, we got the
	 * high half 0x000c and drew no orbit (MV1473, both machines, 2026-10-15).
	 * +0x87 == 1 marks a system body; the orbit object is a stack copy whose
	 * +0x87 is never set, hence the model test. */
	else if ((idx == 10 || idx == 11) &&
	         (obj[0x87] == 1 || *(const long *)(obj + 0x82) == 0x96))
		idx ^= 1;
	return (long)*(const unsigned short *)(obj + 0x9c + idx * 2);
}

long Func1473_c (const unsigned char *ctx, long code) __asm__("FUNC_001473");
/* 2026-10-15: MV1473 -- every call in the FRAME1808 capture frame, twin of
 * MV1473 in build\pcprobe_proj.c; printed by ffe_video.c with FRAMECNT. */
long ffe_mv_n, ffe_mv_code[256], ffe_mv_r[256], ffe_mv_mdl[256];
static long func1473_body (const unsigned char *ctx, long code)
{
	switch (code & 0xc0) {
	case 0x00: return code & 0x3f;
	case 0x40: return (long)(short)(code & 0x3f) << 10;
	case 0x80: return modvar_word (ctx, code & 0x3f);
	default:   return *(const long *)(ctx + 0x54 + (code & 0x3f) * 4);
	}
}
long Func1473_c (const unsigned char *ctx, long code)
{
	extern long ffe_f8_state;
	long r = func1473_body (ctx, code);
	if (ffe_f8_state == 2 && ffe_mv_n < 256) {
		const unsigned char *obj = *(const unsigned char *const *)(ctx + 0x14c);
		ffe_mv_code[ffe_mv_n] = code; ffe_mv_r[ffe_mv_n] = r;
		ffe_mv_mdl[ffe_mv_n] = obj ? *(const long *)(obj + 0x82) : -1;
		ffe_mv_n++;
	}
	return r;
}

long Func1474_c (const unsigned char *ctx, long code) __asm__("FUNC_001474");
long Func1474_c (const unsigned char *ctx, long code)
{
	switch (code & 0xc0) {
	case 0x00:
	case 0x40:
		return (long)(short)(((unsigned long)(code & 0x7f) << 9) & 0xffffUL)
		       | ((code >> 7) & 0x1ff);
	case 0x80: return modvar_word (ctx, code & 0x3f);
	default:   return *(const long *)(ctx + 0x54 + (code & 0x3f) * 4);
	}
}
