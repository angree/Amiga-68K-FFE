/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 angree. See LICENSE at the repository root.
 *
 * fp_soft.c -- soft-float DOUBLE arithmetic for m68k-amigaos, so the program
 * never calls mathieee*.library.
 *
 * WHY THIS FILE EXISTS (diagnosed 2026-08-02, the crash that ended every 3D
 * render once the star system was populated).
 *
 * bebbo's m68k libgcc does NOT implement double arithmetic: `libm020/libgcc.a`
 * only REFERENCES __divdf3. The definition comes from
 * `libm020/libc.a(lib_a-__divdf3.o)`, which is 36 bytes long:
 *
 *     moveml d2-d3/a6,sp@-
 *     moveal _MathIeeeDoubBasBase,a6
 *     movel  sp@(16),d0 / sp@(20),d1 / sp@(24),d2 / sp@(28),d3
 *     jsr    a6@(-84)              ; mathieeedoubbas.library/IEEEDPDiv
 *     moveml sp@+,d2-d3/a6 / rts
 *
 * So every double operation is a library call, and the link needs three bases
 * (MathIeeeDoubBasBase, MathIeeeDoubTransBase, MathIeeeSingBasBase) that
 * nothing opened. main() now opens them, and the crash SURVIVED that: the
 * fault stayed at PC=00fca0c0, inside Kickstart, where the bytes are an F-line
 * word followed by a table of negative offsets. Depending on a ROM library for
 * arithmetic is fragile in any case -- the project's stated hard floor is a
 * 68020 with NO FPU, and what those libraries contain varies by machine and
 * Kickstart. Doing the arithmetic ourselves removes the dependency entirely.
 *
 * The same reasoning, and the same shape of fault, is already written up in
 * amiga\fp_conv.c for the int->double conversions (inherited from the OpenTTD
 * port). This file is that treatment applied to the arithmetic.
 *
 * SCOPE. Only the hand-written x87 shims use `double` at all -- ffe_planet.c
 * (FUNC_001877's Gauss-Jordan), ffe_x87.c, Func833_c's sqrt and ffe_timers.c's
 * fmod -- so the set of routines is small and is read off the link map:
 * add, sub, mul, div and the comparisons.
 *
 * IMPLEMENTATION. Pure integer code on the IEEE-754 bit pattern, using
 * unsigned long long for the 53-bit significands. Round-to-nearest-even, which
 * is what the x87 does with its default control word and what the original
 * therefore produced. Infinities and NaNs are handled structurally; subnormals
 * are flushed to zero, which is what the game's value ranges make irrelevant
 * (the shims work on coordinates and distances, never on 1e-310).
 *
 * A NOTE ON RETURN CONVENTION, which is what broke fp_conv.c's predecessors:
 * these are ordinary C functions returning `double` under -msoft-float, so the
 * compiler emits exactly the convention every call site expects (d0/d1). They
 * call no other float routine, so they cannot recurse into the stubs.
 */

typedef unsigned long long u64;
typedef unsigned long u32;

union dbits {
	double d;
	u64    u;
};

#define EXP_MASK  0x7ffULL
#define MANT_BITS 52
#define MANT_MASK 0x000fffffffffffffULL
#define HIDDEN    0x0010000000000000ULL

static double pack (int sign, int exp, u64 mant)
{
	union dbits v;

	/* mant carries the hidden bit at position 52 and has already been
	 * normalised and rounded by the caller. */
	if (exp >= 2047) {			/* overflow -> infinity */
		v.u = ((u64)(sign & 1) << 63) | (2047ULL << MANT_BITS);
		return v.d;
	}
	if (exp <= 0) {				/* underflow -> zero (see header) */
		v.u = (u64)(sign & 1) << 63;
		return v.d;
	}
	v.u = ((u64)(sign & 1) << 63) | ((u64)exp << MANT_BITS)
	    | (mant & MANT_MASK);
	return v.d;
}

static void unpack (double d, int *sign, int *exp, u64 *mant)
{
	union dbits v;

	v.d = d;
	*sign = (int)(v.u >> 63);
	*exp  = (int)((v.u >> MANT_BITS) & EXP_MASK);
	*mant = v.u & MANT_MASK;
	if (*exp != 0 && *exp != 2047)
		*mant |= HIDDEN;		/* normal: restore the hidden bit */
	else if (*exp == 0)
		*mant = 0;			/* zero or subnormal -> zero */
}

/* round a significand held with `guard` extra low bits, to nearest even */
static u64 round_ne (u64 m, int guard)
{
	u64 half, lowmask, rest;

	if (guard <= 0) return m;
	half = 1ULL << (guard - 1);
	lowmask = (half << 1) - 1;
	rest = m & lowmask;
	m >>= guard;
	if (rest > half || (rest == half && (m & 1)))
		m++;
	return m;
}

static double addsub (double a, double b, int subtract)
{
	int sa, sb, ea, eb, sign, exp;
	u64 ma, mb, m;
	int shift;

	unpack (a, &sa, &ea, &ma);
	unpack (b, &sb, &eb, &mb);
	if (subtract) sb ^= 1;

	if (ea == 2047 || eb == 2047) {		/* inf/nan propagate */
		union dbits v;
		v.d = (ea == 2047) ? a : b;
		return v.d;
	}
	if (ma == 0) { union dbits v; v.d = b; if (subtract) v.u ^= (1ULL << 63); return v.d; }
	if (mb == 0) return a;

	/* work with three guard bits below the significand */
	ma <<= 3; mb <<= 3;
	if (ea < eb) {
		int t = ea; ea = eb; eb = t;
		{ u64 tm = ma; ma = mb; mb = tm; }
		{ int ts = sa; sa = sb; sb = ts; }
	}
	shift = ea - eb;
	if (shift > 63) mb = 0;
	else if (shift) {
		u64 sticky = (mb & ((1ULL << shift) - 1)) ? 1ULL : 0ULL;
		mb = (mb >> shift) | sticky;
	}
	exp = ea;

	if (sa == sb) {
		m = ma + mb;
		sign = sa;
	} else if (ma >= mb) {
		m = ma - mb;
		sign = sa;
	} else {
		m = mb - ma;
		sign = sb;
	}
	if (m == 0) return pack (0, 0, 0);

	/* normalise so the hidden bit sits at 52+3 */
	while (m >= (HIDDEN << 4)) { m >>= 1; exp++; }
	while (m < (HIDDEN << 3)) { m <<= 1; exp--; }
	m = round_ne (m, 3);
	if (m >= (HIDDEN << 1)) { m >>= 1; exp++; }
	return pack (sign, exp, m);
}

double __adddf3 (double a, double b) { return addsub (a, b, 0); }
double __subdf3 (double a, double b) { return addsub (a, b, 1); }

double __muldf3 (double a, double b)
{
	int sa, sb, ea, eb, exp;
	u64 ma, mb, hi, lo, m;
	u32 a0, a1, b0, b1;
	u64 p0, p1, p2, p3;

	unpack (a, &sa, &ea, &ma);
	unpack (b, &sb, &eb, &mb);
	if (ea == 2047 || eb == 2047) { union dbits v; v.d = (ea == 2047) ? a : b; return v.d; }
	if (ma == 0 || mb == 0) return pack (sa ^ sb, 0, 0);

	/* 53x53 -> 106 bits through four 32-bit partial products */
	a0 = (u32)(ma & 0xffffffffULL); a1 = (u32)(ma >> 32);
	b0 = (u32)(mb & 0xffffffffULL); b1 = (u32)(mb >> 32);
	p0 = (u64)a0 * b0;
	p1 = (u64)a0 * b1;
	p2 = (u64)a1 * b0;
	p3 = (u64)a1 * b1;
	lo = p0 + ((p1 & 0xffffffffULL) << 32) + ((p2 & 0xffffffffULL) << 32);
	hi = p3 + (p1 >> 32) + (p2 >> 32);
	/* carries out of lo */
	if (lo < p0) hi++;
	exp = ea + eb - 1023;

	/* the product's hidden bit is at 104 or 105; bring it to 52 with a
	 * sticky bit folded into the low end */
	{
		u64 sticky = 0;
		int sh = 52;			/* drop 52 low bits */
		if (hi & (1ULL << (104 - 64))) { sh = 53; exp++; }
		if (lo & ((1ULL << sh) - 1)) sticky = 1;
		m = (hi << (64 - sh)) | (lo >> sh);
		m |= sticky;
	}
	m = round_ne (m << 3, 3);
	while (m >= (HIDDEN << 1)) { m >>= 1; exp++; }
	while (m && m < HIDDEN) { m <<= 1; exp--; }
	return pack (sa ^ sb, exp, m);
}

double __divdf3 (double a, double b)
{
	int sa, sb, ea, eb, exp, i;
	u64 ma, mb, q, r;

	unpack (a, &sa, &ea, &ma);
	unpack (b, &sb, &eb, &mb);
	if (ea == 2047 || eb == 2047) { union dbits v; v.d = (ea == 2047) ? a : b; return v.d; }
	if (mb == 0) {				/* x/0 -> signed infinity */
		union dbits v;
		v.u = ((u64)(sa ^ sb) << 63) | (2047ULL << MANT_BITS);
		return v.d;
	}
	if (ma == 0) return pack (sa ^ sb, 0, 0);

	exp = ea - eb + 1023;
	/* restoring division, 56 quotient bits (53 + 3 guard) */
	q = 0; r = ma;
	for (i = 0; i < 56; i++) {
		q <<= 1;
		if (r >= mb) { r -= mb; q |= 1; }
		r <<= 1;
	}
	if (r) q |= 1;				/* sticky */
	/* q's leading bit is at 55 or 54 */
	while (q >= (HIDDEN << 4)) { q >>= 1; exp++; }
	while (q < (HIDDEN << 3)) { q <<= 1; exp--; }
	q = round_ne (q, 3);
	if (q >= (HIDDEN << 1)) { q >>= 1; exp++; }
	return pack (sa ^ sb, exp, q);
}

/* Comparisons. GCC's soft-float protocol: __cmpdf2 returns <0, 0, >0 and the
 * *df2 aliases below are what the compiler actually emits for each operator;
 * the unordered case never arises here (no NaNs in the shims' value ranges). */
static int cmp2 (double a, double b)
{
	union dbits x, y;
	int sx, sy;

	x.d = a; y.d = b;
	sx = (int)(x.u >> 63);
	sy = (int)(y.u >> 63);
	if ((x.u << 1) == 0 && (y.u << 1) == 0) return 0;	/* +0 == -0 */
	if (sx != sy) return sx ? -1 : 1;
	if (x.u == y.u) return 0;
	if (sx)					/* both negative: reversed */
		return (x.u > y.u) ? -1 : 1;
	return (x.u > y.u) ? 1 : -1;
}

int __cmpdf2 (double a, double b) { return cmp2 (a, b); }
int __eqdf2  (double a, double b) { return cmp2 (a, b) != 0; }
int __nedf2  (double a, double b) { return cmp2 (a, b) != 0; }
int __ltdf2  (double a, double b) { return cmp2 (a, b); }
int __ledf2  (double a, double b) { return cmp2 (a, b); }
int __gtdf2  (double a, double b) { return cmp2 (a, b); }
int __gedf2  (double a, double b) { return cmp2 (a, b); }
int __unorddf2 (double a, double b) { (void)a; (void)b; return 0; }

/* CONVERSIONS. The crash survived replacing the arithmetic because
 * FUNC_001877 also converts: its header says the Gauss-Jordan runs in double
 * with an explicit (float) round at each fstp point, so every step crosses
 * float<->double, and those routines are mathieee stubs too. Same treatment,
 * same reasoning -- pure integer code, no call into any float routine. */

union fbits {
	float f;
	u32   u;
};

double __extendsfdf2 (float f)
{
	union fbits x;
	int sign, exp;
	u64 mant;

	x.f = f;
	sign = (int)(x.u >> 31);
	exp  = (int)((x.u >> 23) & 0xff);
	mant = (u64)(x.u & 0x7fffffUL);
	if (exp == 0) return pack (sign, 0, 0);		/* zero/subnormal -> 0 */
	if (exp == 255) {				/* inf/nan */
		union dbits v;
		v.u = ((u64)sign << 63) | (2047ULL << MANT_BITS)
		    | (mant << (MANT_BITS - 23));
		return v.d;
	}
	return pack (sign, exp - 127 + 1023,
	             HIDDEN | (mant << (MANT_BITS - 23)));
}

float __truncdfsf2 (double d)
{
	union fbits r;
	int sign, exp;
	u64 mant;
	u32 m;

	unpack (d, &sign, &exp, &mant);
	if (exp == 0 || mant == 0) { r.u = (u32)sign << 31; return r.f; }
	if (exp == 2047) { r.u = ((u32)sign << 31) | (255UL << 23); return r.f; }
	exp = exp - 1023 + 127;
	/* round the 53-bit significand to 24 bits, nearest-even */
	mant = round_ne (mant, MANT_BITS - 23);
	if (mant >= (1ULL << 24)) { mant >>= 1; exp++; }
	if (exp >= 255) { r.u = ((u32)sign << 31) | (255UL << 23); return r.f; }
	if (exp <= 0)   { r.u = (u32)sign << 31; return r.f; }
	m = (u32)(mant & 0x7fffffULL);
	r.u = ((u32)sign << 31) | ((u32)exp << 23) | m;
	return r.f;
}

double __floatsidf (long i)
{
	int sign = 0;
	unsigned long v;
	int exp;
	u64 mant;

	if (i == 0) return pack (0, 0, 0);
	if (i < 0) { sign = 1; v = (unsigned long)(-i); } else v = (unsigned long)i;
	exp = 1023 + 52;
	mant = (u64)v;
	while (mant < HIDDEN) { mant <<= 1; exp--; }
	while (mant >= (HIDDEN << 1)) { mant >>= 1; exp++; }
	return pack (sign, exp, mant);
}

double __floatunsidf (unsigned long v)
{
	int exp;
	u64 mant;

	if (v == 0) return pack (0, 0, 0);
	exp = 1023 + 52;
	mant = (u64)v;
	while (mant < HIDDEN) { mant <<= 1; exp--; }
	while (mant >= (HIDDEN << 1)) { mant >>= 1; exp++; }
	return pack (0, exp, mant);
}

long __fixdfsi (double d)
{
	int sign, exp;
	u64 mant;
	int sh;

	unpack (d, &sign, &exp, &mant);
	if (exp == 0 || mant == 0) return 0;
	exp -= 1023;
	if (exp < 0) return 0;
	if (exp > 62) return sign ? (-2147483647L - 1) : 2147483647L;
	sh = MANT_BITS - exp;
	if (sh > 0) mant >>= sh; else mant <<= -sh;
	if (mant > 2147483647ULL)
		return sign ? (-2147483647L - 1) : 2147483647L;
	return sign ? -(long)mant : (long)mant;
}

unsigned long __fixunsdfsi (double d)
{
	long v = __fixdfsi (d);

	return (v < 0) ? 0UL : (unsigned long)v;
}

double __negdf2 (double a)
{
	union dbits v;

	v.d = a;
	v.u ^= 1ULL << 63;
	return v.d;
}

/* ---- SINGLE PRECISION -----------------------------------------------------
 *
 * Same problem, same cause: __divsf3, __mulsf3, __floatsisf and the float
 * comparisons come from libc as mathieeesingbas.library stubs, and
 * ffe_planet.c rounds to (float) at every x87 fstp point, so the shim crosses
 * single precision constantly. Replacing only the double routines therefore
 * changed nothing -- measured.
 *
 * These are implemented THROUGH the double routines above rather than written
 * again with a 24-bit significand. That is exact, not a shortcut: binary64
 * carries more than 2p+2 bits relative to binary32, so computing a float
 * +,-,*,/ in double and rounding once to float yields the correctly-rounded
 * float result -- the classic no-double-rounding property. Less code, and the
 * arithmetic is the code already exercised by the double path.
 */
float __addsf3 (float a, float b)
{ return __truncdfsf2 (addsub (__extendsfdf2 (a), __extendsfdf2 (b), 0)); }

float __subsf3 (float a, float b)
{ return __truncdfsf2 (addsub (__extendsfdf2 (a), __extendsfdf2 (b), 1)); }

float __mulsf3 (float a, float b)
{ return __truncdfsf2 (__muldf3 (__extendsfdf2 (a), __extendsfdf2 (b))); }

float __divsf3 (float a, float b)
{ return __truncdfsf2 (__divdf3 (__extendsfdf2 (a), __extendsfdf2 (b))); }

float __negsf2 (float a)
{
	union fbits v;

	v.f = a;
	v.u ^= 1UL << 31;
	return v.f;
}

static int cmp2f (float a, float b)
{
	union fbits x, y;
	int sx, sy;

	x.f = a; y.f = b;
	sx = (int)(x.u >> 31);
	sy = (int)(y.u >> 31);
	if ((x.u << 1) == 0 && (y.u << 1) == 0) return 0;	/* +0 == -0 */
	if (sx != sy) return sx ? -1 : 1;
	if (x.u == y.u) return 0;
	if (sx) return (x.u > y.u) ? -1 : 1;
	return (x.u > y.u) ? 1 : -1;
}

int __cmpsf2 (float a, float b) { return cmp2f (a, b); }
int __eqsf2  (float a, float b) { return cmp2f (a, b) != 0; }
int __nesf2  (float a, float b) { return cmp2f (a, b) != 0; }
int __ltsf2  (float a, float b) { return cmp2f (a, b); }
int __lesf2  (float a, float b) { return cmp2f (a, b); }
int __gtsf2  (float a, float b) { return cmp2f (a, b); }
int __gesf2  (float a, float b) { return cmp2f (a, b); }
int __unordsf2 (float a, float b) { (void)a; (void)b; return 0; }

float __floatsisf (long i)      { return __truncdfsf2 (__floatsidf (i)); }
float __floatunsisf (unsigned long v) { return __truncdfsf2 (__floatunsidf (v)); }
long  __fixsfsi (float f)       { return __fixdfsi (__extendsfdf2 (f)); }
unsigned long __fixunssfsi (float f)
{
	long v = __fixsfsi (f);

	return (v < 0) ? 0UL : (unsigned long)v;
}
