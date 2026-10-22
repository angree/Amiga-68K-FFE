/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 angree. See LICENSE at the repository root.
 *
 * Linked by BOTH ffe_game and ffe_xlattest: the game needs the correct
 * FUNC_001486 and the oracle probe verifies it against the packed-float
 * vectors (pf_cases). Keep this file free of game-data externs so the
 * probe can link it without the full data image. */

/* FUNC_001486 (ffe.asm:89108): packed-float ADD. Not an x87 victim -- the
 * translator handles every instruction -- but the function builds its result
 * through `lea ebx,[ebp-4]` and POINTER word-writes the mantissa/exponent
 * halves, then dword-reads them back. frame.py cannot see through the alias,
 * so the translated composition came out (mant<<16)|exp instead of the x86's
 * (exp<<16)|mant -- caught by the packed-float oracle vectors (139 of 490).
 * The C spells the composition explicitly. Semantics line-for-line from the
 * disassembly, including the word-wise normalisation shifts. */
long Func1486_c (long a, long b) __asm__("FUNC_001486");
long Func1486_c (long a, long b)
{
	short ea = (short)((unsigned long)a >> 16);
	short eb = (short)((unsigned long)b >> 16);
	short ma = (short)a, mb = (short)b;
	long sum, res_exp;
	short mant;

	if (ea > eb) {
		if ((long)ea > (long)eb + 31)
			return a;
		res_exp = ea;
		sum = ((long)mb >> (ea - eb)) + (long)ma;
	} else {
		if ((long)eb > (long)ea + 31)
			return b;
		res_exp = eb;
		sum = ((long)ma >> (eb - ea)) + (long)mb;
	}
	if ((sum < 0 ? -sum : sum) > 0x7fff) {
		sum >>= 1;
		res_exp += 1;
		return ((res_exp & 0xffffl) << 16) | ((unsigned short)sum);
	}
	mant = (short)sum;
	if (mant == 0)
		return ((res_exp & 0xffffl) << 16) | 0;
	while (((long)mant < 0 ? -(long)mant : (long)mant) < 0x3fff) {
		mant = (short)(mant << 1);       /* word shift, exactly as SHL */
		res_exp -= 1;
	}
	return ((res_exp & 0xffffl) << 16) | ((unsigned short)mant);
}
