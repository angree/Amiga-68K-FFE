/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 angree. See LICENSE at the repository root.
 *
 * C replacements for the small x87 functions the translator refuses on
 * purpose (no FP model). Same legality argument as Func833_c in ffe_game.c:
 * these are called from TRANSLATED code under the t-ABI, and a C callee
 * saves strictly more than the t-ABI assumes, so a C shim is safe. Bare
 * symbols -- massgen excludes these names so no stub collides.
 *
 * FUNC_002095_fstore (the Watcom __ftol, round-to-zero) becomes DEAD once
 * its only callers (112 here, 833 and 1877 as C) are shims: C truncation
 * `(long)d` IS round-to-zero, so no replacement is needed for it.
 */

/* FUNC_000112 (ffe.asm:12295): fills the four 256-entry sound pitch tables
 * DATA_008599..DATA_008602. Per entry i (a SIGNED byte index):
 *     x  = (float)i;  if (i & 0x80) x -= 256.0f;     -- sign-fold to -128..127
 *     x *= 16384.0f;                                  -- single precision
 *     t[i] = ftol(Cn * x)                             -- double, then truncate
 * The four doubles live as raw LE bytes at DATA_000008..11; they are rebuilt
 * here from the SAME bit patterns (written big-endian for the 68k), so the
 * products are bit-identical to the x86's double math under soft-float IEEE.
 * The x86 ran them at extended precision on the FP stack -- but every factor
 * and product here is representable well inside double range, and the store
 * was through fistp qword after a double-precision fmul, so double math
 * reproduces the truncated integer exactly for these magnitudes (|result|
 * < 2^22). */
extern long ffe_pitch_a[256] __asm__("DATA_008599");
extern long ffe_pitch_b[256] __asm__("DATA_008600");
extern long ffe_pitch_c[256] __asm__("DATA_008601");
extern long ffe_pitch_d[256] __asm__("DATA_008602");

static double from_bits (const unsigned char *be)
{
	/* the 68k stores IEEE doubles big-endian; feeding the bytes in BE
	 * order rebuilds the exact bit pattern with no decimal round-trip */
	union { double d; unsigned char b[8]; } u;
	int i;
	for (i = 0; i < 8; i++)
		u.b[i] = be[i];
	return u.d;
}

void Func112_c (void) __asm__("FUNC_000112");
void Func112_c (void)
{
	/* x86 LE images at ffe.asm:17c4c.. reversed to BE:
	 * DATA_000008 27 31 08 ac 1c 5a fc 3f -> 3f fc 5a 1c ac 08 31 27
	 * DATA_000009 3b df 4f 8d 97 6e f6 3f -> 3f f6 6e 97 8d 4f df 3b
	 * DATA_000010 4a b5 4f c7 63 06 d6 bf -> bf d6 06 63 c7 4f b5 4a
	 * DATA_000011 7c 7e 18 21 3c da e6 bf -> bf e6 da 3c 21 18 7e 7c */
	static const unsigned char c8[8]  =
		{0x3f,0xfc,0x5a,0x1c,0xac,0x08,0x31,0x27};
	static const unsigned char c9[8]  =
		{0x3f,0xf6,0x6e,0x97,0x8d,0x4f,0xdf,0x3b};
	static const unsigned char c10[8] =
		{0xbf,0xd6,0x06,0x63,0xc7,0x4f,0xb5,0x4a};
	static const unsigned char c11[8] =
		{0xbf,0xe6,0xda,0x3c,0x21,0x18,0x7e,0x7c};
	double C8 = from_bits (c8), C9 = from_bits (c9);
	double C10 = from_bits (c10), C11 = from_bits (c11);
	long i;

	for (i = 0; i < 256; i++) {
		float x = (float)i;
		if (i & 0x80)
			x -= 256.0f;
		x *= 16384.0f;
		ffe_pitch_a[i] = (long)(C8  * (double)x);
		ffe_pitch_b[i] = (long)(C9  * (double)x);
		ffe_pitch_c[i] = (long)(C10 * (double)x);
		ffe_pitch_d[i] = (long)(C11 * (double)x);
	}
}
