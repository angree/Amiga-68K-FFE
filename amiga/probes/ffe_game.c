/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 angree. See LICENSE at the repository root.
 *
 * THE GAME. First-light build: every function tools\translator emits
 * (build\xlat-full, 1,475 of 1,627) plus logging stubs for the 152 it still
 * refuses, linked against the big-endian data image (build\data-be) and the
 * platform layer. main() does nothing but call the game's own entry --
 * _asmmain brings the screen up itself through SystemInit and runs its own
 * splash and loop.
 *
 * The call goes through the C-ABI WRAPPER (__asmmain in _asmmain.s): the
 * body follows the t-ABI, where d2-d7 are scratch, and calling it directly
 * from C would corrupt this file's locals.
 *
 * When the running game reaches a function the translator refused, the stub
 * calls FfeStubHit(), which names it in PROGDIR:ffe_game.log and exits --
 * the run test's whole point is that the FIRST missing piece announces
 * itself instead of crashing anonymously.
 */
#include <math.h>
#include "../ffeapi/ffe_dev.h"
#include <stdio.h>
#include <stdlib.h>

extern void asmmain_entry (void) __asm__("__asmmain");

/* VIEWPORT DEBUG: slots for the render-chain call counter (see FfeCount).
 * Raised from 24 to 48 on 2026-08-05 to hold all 32 model-interpreter opcode
 * handlers at once. The histogram has to be COMPLETE to be evidence: with
 * fewer slots than handlers the ones that never got a slot look exactly like
 * the ones that never ran, which is the question being asked. */
#define FFE_CNT_MAX 48

static FILE *g_log;

static FILE *log_open (void)
{
	if (!g_log)
		g_log = ffe_logopen ("PROGDIR:ffe_game.log", "a");
	return g_log;
}

void FfeStubHit (int num)
{
	FILE *f = log_open ();
	if (f) {
		fprintf (f, "STUB HIT: FUNC_%06d -- translator-refused function "
		         "reached; see build/xlat-full/refused.txt\n", num);
		fflush (f);
		fclose (f);
	}
	exit (10);
}

/* ---- hand shims for the x86-bound helpers -------------------------------
 *
 * _SetJmp/_LongJmp save the x86 register image and cannot be translated.
 * They are now IMPLEMENTED, in 68k, in amiga\probes\ffe_setjmp.s -- see the
 * header there. They used to be stubs (SetJmp -> 0, LongJmp -> exit(11)),
 * and that stub was what stopped the first landed frame dead: the renderer
 * overflows its 3000-node pool, the game LongJmps back to FUNC_001876 to
 * abandon that one frame, and exit(11) turned a designed recovery into a
 * dead process. Do not reintroduce them here.
 */

/* NullKill is the game's deliberate crash (`db 0xff`, an invalid opcode) --
 * make it a loud stop instead. Bare label, so the asm name is explicit. */
void NullKill_c (void) __asm__("NullKill");
void NullKill_c (void)
{
	FILE *f = log_open ();
	if (f) {
		fprintf (f, "NullKill reached -- the game killed itself on "
		         "purpose (null pointer path)\n");
		fflush (f);
	}
	exit (12);
}

/* FUNC_CheckInputWrap: a 16-bit saturating add the translator cannot prove
 * safe (word reads of bare [esp+4] cdecl slots). Called from TRANSLATED
 * code under the t-ABI -- a C callee saves strictly more, so a C shim is
 * legal. Returns in the low word of d0, which is where the callers look. */
int CheckInputWrap_c (int a, int b) __asm__("FUNC_CheckInputWrap");
int CheckInputWrap_c (int a, int b)
{
	short sa = (short)a, sb = (short)b, sc = (short)(sa + sb);
	if (sa < 0 && sb < 0 && sc >= 0)
		return (short)0x8000;
	if (sa > 0 && sb > 0 && sc <= 0)
		return 0x7fff;
	return sc;
}

/* FUNC_000833: 3D point distance, the first of the game's three x87
 * functions to be reached at runtime (new-game start path). The original is
 * integer end to end except ONE fild/sqrt/fistp: abs(delta>>8) per axis,
 * halve all three until each fits 15 bits (tracking the halvings in
 * `shift`, which starts at 14), sum of squares, x87 sqrt, fistp back, then a
 * TRUNCATING 32-bit imul by 0xc8b4 and an arithmetic shift right by
 * `shift`. IEEE-double sqrt of an integer is bit-identical to the x87's,
 * and sqrt(k) is never exactly n+0.5 for integer k, so floor(x+0.5)
 * reproduces the fistp round-to-nearest exactly. The multiply wraps to 32
 * bits on x86 (values reach ~2.9e9) -- hence the unsigned wrap below. */
long Func833_c (long ax, long ay, long az,
                long bx, long by, long bz) __asm__("FUNC_000833");
long Func833_c (long ax, long ay, long az,
                long bx, long by, long bz)
{
	long dx = (bx - ax) >> 8, dy = (by - ay) >> 8, dz = (bz - az) >> 8;
	long shift = 14, r;
	unsigned long m;

	if (dx < 0) dx = -dx;
	if (dy < 0) dy = -dy;
	if (dz < 0) dz = -dz;
	while (dx > 0x7fff || dy > 0x7fff || dz > 0x7fff) {
		dx >>= 1; dy >>= 1; dz >>= 1;
		shift--;
	}
	/* TRUNCATE, never round: the x86 stores through FUNC_002095_fstore, which
	 * sets RC=11b (or byte [cw+1],0xc = round toward zero) before its fistp.
	 * The old floor(x+0.5) made the map distance Gateway->Essedze 9.97 ly
	 * where the PC shows 9.94 (sqrt 100989 = 317.8: 318 vs 317), and with it
	 * the hyperspace time and fuel (2026-10-15). */
	r = (long)sqrt ((double)(dx * dx + dy * dy + dz * dz));
	m = (unsigned long)r * 0xc8b4UL;
	return (long)m >> (shift & 31);
}

/* FIRST-LIGHT DEBUG: dispatch logger for the hand-written FUNC_000144 in
 * ffe_dispatch_dbg.s (enabled via `!FUNC_000144` in force_stub.txt). The
 * LAST line in PROGDIR:ffe_dispatch.log before a hang names the module and
 * event whose handler spins. */
void FfeDispatchLog (long a, long b, long c)
{
	static int n;
	FILE *f;

	if (n >= 4000) return;
	f = ffe_logopen ("PROGDIR:ffe_dispatch.log", "a");
	if (f == NULL) return;
	fprintf (f, "%d mod=%ld phase=%ld ev=%lx\n", n, a, b,
	         (unsigned long)c);
	fclose (f);
	n++;
}

/* VIEWPORT DEBUG (2026-08-01): a CALL COUNTER for the render chain. The entry
 * logger below writes a file per call, which is unusable for a rasteriser
 * primitive called thousands of times per frame; this one only bumps a slot,
 * and ffe_video.c's dump_clock prints and RESETS the table, so every line is
 * "calls since the previous dump". Injected by tools\translator\inject_count.py
 * with the same register-transparent MOVEM payload the trace rig uses.
 * Debug only -- comes out with the rest of the rig. */
long ffe_cnt_id[FFE_CNT_MAX];
long ffe_cnt_n[FFE_CNT_MAX];

/* VIEWPORT DEBUG: WHO ZEROES THE PLAYER'S MATRIX.
 * FUNC_001675_MatBuildYZT is proven correct (called from C on a private buffer
 * with the game's own angles it returns the expected near-identity) AND is
 * proven to run on the player object (YZTv3 reports the pool address). Yet the
 * matrix reads all-zero from the first dump on. So something clears it after
 * the build. Every instrumented entry samples m00 here; the first call that
 * sees it go non-zero -> zero names the function it happened INSIDE, which
 * bisects the search without a hardware watchpoint. */
long ffe_zero_at;
long ffe_zero_prev;		/* the FUNC number that first saw m00 == 0 */
long ffe_zero_seen;		/* m00 was non-zero at least once */
long ffe_zero_calls;		/* how many instrumented calls ran before it */

extern long ffe_mb_dst;		/* defined with the rest of the MB probe below */

static void zero_watch (long num)
{
	char *ob;
	long m0;

	/* WATCH THE FIXED ADDRESS the matrix was written to, not the current
	 * player pointer: the pointer at DATA_008804+0xc8 only takes its final
	 * value late in the init, so watching it reported "always zero" and hid
	 * the transition entirely. ffe_mb_dst is set by mat_build_c at the exact
	 * moment of the (verified) write. */
	/* Watch the SHIP object -- FUNC_000996 stores it at DATA_008861
	 * (ffe.asm:74010) and the renderer draws it. Its matrix is copied from
	 * the identity template DATA_007265, which is now proven correct in the
	 * running image (TMPL 7f000000 ...), so the loss is downstream. */
	{
		extern char *ship_obj __asm__("DATA_008861");

		if (ffe_zero_at || ship_obj == 0) return;
		ob = ship_obj;
	}
	if ((unsigned long)ob <= 0x1000 || ((unsigned long)ob & 1)) return;
	m0 = *(long *)ob;
	if (m0) { ffe_zero_seen = 1; ffe_zero_calls++; ffe_zero_prev = num; return; }
	/* ffe_zero_prev is the last instrumented entry that still saw the matrix
	 * intact, ffe_zero_at the first that saw it cleared: together they
	 * BRACKET the culprit, so each round of widening the instrumented set
	 * narrows the gap instead of just repeating the answer. */
	if (ffe_zero_seen) ffe_zero_at = num;
}

/* VIEWPORT DEBUG: how many object slots are live at chosen moments. The system
 * generation creates seven objects (FUNC_000927 ran 7 times, measured) yet only
 * ONE slot is flagged by the time the first frame is dumped, so the arena is
 * cleared somewhere between. Sampling the live count at the entry of a few
 * named functions brackets the clear. */
long ffe_live[8];
long ffe_live_base[8];		/* [0]=at 880, [1]=at 927, [2]=at 996, [3]=at 998, [4]=at 994 */

static void live_count (long num)
{
	extern char *obj_base __asm__("DATA_009133");
	char *b = obj_base;
	int i, n = 0, slot = -1;

	if ((unsigned long)b <= 0x1000) return;
	switch (num) {
	case 880: slot = 0; break;
	case 927: slot = 1; break;
	case 996: slot = 2; break;
	case 998: slot = 3; break;
	case 994: slot = 4; break;
	default:  return;
	}
	/* also remember WHICH arena each moment saw: DATA_009133 is a pointer,
	 * and if it is re-assigned between the system generation and the frame
	 * dump then the seven objects are simply in a different block. */
	ffe_live_base[slot] = (long)b;
	for (i = 0; i <= 0x72; i++) if (b[i]) n++;
	/* keep the LAST observation for 927 (it runs seven times) and the first
	 * for the others, which run once */
	if (slot == 1 || ffe_live[slot] == 0) ffe_live[slot] = n + 1;  /* +1: 0 means "never sampled" */
}

/* VIEWPORT DEBUG: FUNC_000927's allocation, caught at the flag store.
 * The function takes the allocating tail JUMP_003875 all seven times (counter
 * 9275=7) and its emitted 68k is exact, yet the flag array the frame dump
 * reads stays empty -- so the base it writes through must not be the one at
 * [DATA_009133]. This records the base (d2) and slot index (d3) at the store. */
long ffe_alloc_base[8], ffe_alloc_idx[8], ffe_alloc_n;

void FfeAllocLog (long base, long idx)
{
	if (ffe_alloc_n < 8) {
		ffe_alloc_base[ffe_alloc_n] = base;
		ffe_alloc_idx[ffe_alloc_n] = idx;
	}
	ffe_alloc_n++;
}

void FfeCount (long num)
{
	int i;

	zero_watch (num);
	live_count (num);
	for (i = 0; i < FFE_CNT_MAX; i++) {
		if (ffe_cnt_id[i] == num) { ffe_cnt_n[i]++; return; }
		if (ffe_cnt_id[i] == 0)   { ffe_cnt_id[i] = num; ffe_cnt_n[i] = 1; return; }
	}
}

/* HOW FAR INTO THE RENDER DO WE GET BEFORE THE MACHINE RESETS (2026-08-05).
 *
 * Since the model interpreter started walking the reference's models the box
 * HARD-RESETS a few tens of frames after the splash, and no ffe_crash.log is
 * written at all -- the catcher is linked and did write one before, so this is
 * not a caught exception but a write into low memory that eats the vectors.
 * ffe_dump.log says the clock runs (presents=129 / presents=33 on the two dead
 * boots) and LJ/POOL16/NODE72 are all zero, so the node pool is eliminated.
 *
 * These counters answer the one remaining question cheaply: WHICH of the render
 * entry points is still being reached when the last surviving dump is written.
 *
 * MONOTONIC ON PURPOSE -- dump_clock prints them and does NOT reset them, unlike
 * ffe_cnt_n above. The wanted quantity is cumulative depth ("did 1750 ever run
 * at all"), not a per-window delta, and a window that resets would read as zero
 * for anything that only runs during system setup.
 *
 * The id table is fixed and ordered, so the printed line is comparable between
 * runs line-position by line-position. Two of the nine already carry a DIFFERENT
 * injected payload (FfeCode1772, FfeScene1876) and those files are left alone --
 * their probes bump the counter themselves, at their first statement, ahead of
 * their own early returns. */
#define FFE_RP_N 9
long ffe_rp_id[FFE_RP_N] = {
	1682,		/* scene renderer entry */
	1683,
	1772,		/* the model interpreter loop (counted from FfeCode1772) */
	1773,
	1774,
	1745,		/* arena allocator (counted from FfeArena1745) */
	1750,		/* subobject cull */
	1687,		/* frustum test */
	1876		/* scene entry (counted from FfeScene1876) */
};
long ffe_rp_n[FFE_RP_N];

void FfeRenderPath (long num) __asm__("_FfeRenderPath");
void FfeRenderPath (long num)
{
	int i;

	/* No stack walk, no I/O: nine compares is all this may cost, because the
	 * interpreter loop runs in the thousands per frame. */
	for (i = 0; i < FFE_RP_N; i++)
		if (ffe_rp_id[i] == num) { ffe_rp_n[i]++; return; }
}

/* THE ARENA ALLOCATOR'S BOUND, AND WHETHER IT CAN GO NEGATIVE (2026-08-05).
 *
 * FUNC_001745 (ffe.asm:108409-108427) computes
 *     size  = 36 * (arg1 + arg2) + 8 * arg3 + 12
 *     total = [DATA_007823] + size
 * and takes the allocating branch when `cmp esi,0x8000 / jng' holds -- a SIGNED
 * compare. A negative total therefore PASSES, and everything after it writes
 * through [DATA_009251 + cursor] with no further check: exactly the shape of a
 * write into low memory that destroys the exception vectors.
 *
 * The watermark is read here the way the GAME reads it -- a plain native dword.
 * build\xlat-full\FUNC_001745.s:253 emits `movea.l d0,a0 / move.l (a0),d2' with
 * no swap, so a C dword read is the same access and the probe corrects nothing.
 *
 * max is the largest total seen and neg counts the times it came out negative;
 * calls is the positive control that makes a neg of 0 mean something. */
long ffe_arena_max;
long ffe_arena_neg;
long ffe_arena_calls;

/* THE SAME ALLOCATOR, CALL BY CALL rather than as three summary numbers.
 *
 * `max=3428 neg=0 calls=63' says the bound is never crossed, which eliminates
 * the overflow -- but it says nothing about WHAT is being asked for, and the
 * hard reset is still somewhere in the render. One line per call gives the
 * request itself (the three cdecl dwords), the watermark it is measured
 * against, and the verdict, so a request that is accepted with an absurd size
 * is visible instead of averaged away.
 *
 * The watermark is read as a NATIVE dword and that IS the game's own access:
 * build\xlat-full\FUNC_001745.s:253 emits `movea.l d0,a0 / move.l (a0),d2'
 * with no swap, so no probe-side correction applies (contrast the bytecode
 * word in FfeOpTrace below, which the game DOES swap at the site).
 *
 * Not a second injection: FUNC_001745.s already carries the FfeArena1745
 * payload at its bare label, so this is called from there -- the same
 * arrangement FfeCode1772 uses for the render-path counter. Two INJECT_
 * blocks at one label would be stripped and re-added as a pair, and the
 * second one would silently disappear the next time the first is re-injected.
 *
 * Capped: FUNC_001745 measured 63 calls over a whole dying run, so 256 lines
 * covers several renders with room to spare and cannot fill the disk if the
 * call count ever runs away. */
#define ALLOC1745_MAX 256
long ffe_alloc1745_n;

static void FfeAlloc1745 (long a1, long a2, long a3, long wm, int ok)
{
	FILE *f;

	if (ffe_alloc1745_n >= ALLOC1745_MAX) return;
	ffe_alloc1745_n++;
	f = ffe_logopen ("PROGDIR:ffe_enter.log", "a");
	if (f == NULL) return;
	fprintf (f, "ALLOC a1=%ld a2=%ld a3=%ld wm=%ld %s\n",
	         a1, a2, a3, wm, ok ? "accepted" : "refused");
	fflush (f);
	fclose (f);
}

void FfeArena1745 (long num) __asm__("_FfeArena1745");
void FfeArena1745 (long num)
{
	unsigned long *caller = (unsigned long *)((char *)&num + 4 + 60);
	long a1 = (long)caller[1], a2 = (long)caller[2], a3 = (long)caller[3];
	extern long arena_water __asm__("DATA_007823");
	long total = arena_water + 36 * (a1 + a2) + 8 * a3 + 12;

	FfeRenderPath (num);
	if (total < 0) ffe_arena_neg++;
	if (ffe_arena_calls == 0 || total > ffe_arena_max) ffe_arena_max = total;
	ffe_arena_calls++;
	/* `cmp esi,0x8000 / jng JUMP_006728' (ffe.asm:108425-108426) is a SIGNED
	 * compare and JNG is "not greater", so the allocating branch is taken for
	 * total <= 0x8000 and the refusal (`mov eax,1') for anything above. */
	FfeAlloc1745 (a1, a2, a3, arena_water, total <= 0x8000);
}

/* THE SHIFT COUNT THE CULL SCALES BY -- one number, and a standing question.
 *
 * FUNC_001750 computes it at ffe.asm:109011 as
 *     [ebp-0x4] = arg7 + [model+0x14] + [model+0x18] - [obj+0x130]
 * and uses it two instructions later as `shl eax,cl' (109014). The translator
 * emits LSL.L d1,d0 there and its own comment names the accepted risk: x86
 * masks the count to 5 bits, the 68k takes it modulo 64, so the two agree for
 * 0..31 and diverge for 32..63 -- and nothing in the function proves the
 * range (PLAN 3.6). This costs one push and one call on a path that ran 31
 * times in a whole run, so measuring it is cheaper than continuing to argue
 * about it.
 *
 * NOT a ring dumped later, deliberately: the machine hard-resets, so a buffer
 * that is printed at some consumer may never be printed at all. The lines are
 * flushed as they happen and bounded two ways instead -- the first
 * SHIFT_FIRST calls for the ordinary picture, plus every value OUTSIDE 0..31
 * up to SHIFT_ODD more, because a first-N window is exactly what would miss a
 * single anomalous count late in the run.
 *
 * Hand-injected into build\xlat-full\FUNC_001750.s immediately after the
 * `move.l d0,(-4,a5)' that emits ffe.asm:109011, where d0 still holds the
 * value; marker `; INJECT_SHIFT'. That file also carries the _FfeRenderPath
 * payload at its bare label and a frame.py displacement fix, so re-running
 * translate.py on it drops both. */
#define SHIFT_FIRST 16
#define SHIFT_ODD   16
long ffe_shift_n;
long ffe_shift_odd;

void FfeShift1750 (long v) __asm__("_FfeShift1750");
void FfeShift1750 (long v)
{
	int odd = (v < 0 || v > 31);
	FILE *f;

	ffe_shift_n++;
	if (odd) {
		if (ffe_shift_odd >= SHIFT_ODD) return;
		ffe_shift_odd++;
	} else if (ffe_shift_n > SHIFT_FIRST) {
		return;
	}
	f = ffe_logopen ("PROGDIR:ffe_enter.log", "a");
	if (f == NULL) return;
	fprintf (f, "SHIFT n=%ld v=%ld\n", ffe_shift_n, v);
	fflush (f);
	fclose (f);
}

/* VIEWPORT DEBUG (2026-08-01): an ARGUMENT sampler. Same injected payload as
 * FfeCount, but it also copies the traced function's first three cdecl dwords
 * out of the caller's frame -- the stack walk is FfeEnterLog's, which is
 * proven: the payload pushed d0-d7/a0-a6 (60 bytes) then #num, so the return
 * address sits 64 bytes behind &num and the arguments right after it.
 * Buffered in memory (no I/O in a path called 200x per frame); ffe_video.c's
 * dump_clock prints and resets it. Debug only. */
#define FFE_ARG_MAX 40
#define FFE_ARG_W   12		/* num, a, b, c, 4 words at a, 4 words at b */
long ffe_arg_buf[FFE_ARG_MAX][FFE_ARG_W];
long ffe_arg_n;

static void arg_peek (long *dst, unsigned long p)
{
	int k;

	if (p <= 0x1000 || (p & 1)) {
		for (k = 0; k < 4; k++) dst[k] = -99999;
		return;
	}
	for (k = 0; k < 4; k++) dst[k] = ((short *)p)[k];
}

/* VIEWPORT DEBUG: the object FUNC_001675_MatBuildYZT was last handed. The
 * builder is proven correct in isolation (called from C on a private buffer it
 * returns 7fffffea 0 0 0 7ffffff5 0 0 0 7ffffff5), yet the player object's 3x3
 * reads all-zero every frame -- so either that matrix is overwritten later or
 * the player pointer moves to a slot that never got one. Remembering the
 * address here lets dump_clock print BOTH and tell the two apart. */
/* VIEWPORT DEBUG, BISECTION SHIM: build the matrix in C, from the injected
 * payload, for the ONE call that targets a pooled object. Paired with a
 * hand-stubbed FUNC_001675 body (rts straight after the payload) this makes
 * the C code the only writer, so the question "do the stores land?" and the
 * question "does something clear them afterwards?" separate cleanly.
 * Reproduces ffe.asm:101537-101609 exactly: m01 = sin(y), m12 = sin(z),
 * m11 = cos(y), m22 = cos(z), m20 = 0, then the four products through
 * FUNC_001521, two of them negated. Both primitives are already proven on
 * the Amiga (SIN cy=7ffffff5 ... MUL=7fffffea). */
long ffe_mb_dst, ffe_mb_ay, ffe_mb_az, ffe_mb_cy, ffe_mb_cz, ffe_mb_after;

/* VIEWPORT DEBUG: the length FUNC_000880's name copy computes.
 * ffe.asm:64860-64871 is the classic strcpy idiom -- `or ecx,-1` / `repne
 * scasb` / `not ecx` measures the source, then `rep movsd` + `rep movsb`
 * copies that many bytes into object+0x124. If the source string lost its
 * terminator (a byte-order accident in the table it lives in), the length
 * runs away and the copy smears over neighbouring memory -- which is exactly
 * what clears the player object's matrix between the moment it is written
 * (proven: MB after=7fffffea) and the first frame dump (M all zero).
 * Injected by hand into build\xlat-full\FUNC_000880.s right after `not.l d1`. */
long ffe_len_last, ffe_len_max, ffe_len_n;

void FfeLenLog (long len)
{
	ffe_len_last = len;
	if (len > ffe_len_max) ffe_len_max = len;
	ffe_len_n++;
}

static void mat_build_c (char *d, long ay, long az)
{
	extern long ffe_sin32 (long) __asm__("_FUNC_001914");
	extern long ffe_fmul (long, long) __asm__("_FUNC_001521");
	long sy = ffe_sin32 (ay), sz = ffe_sin32 (az);
	long cy = ffe_sin32 (ay + 0x4000), cz = ffe_sin32 (az + 0x4000);

	*(long *)(d + 0x04) = sy;
	*(long *)(d + 0x18) = sz;
	*(long *)(d + 0x10) = cy;
	*(long *)(d + 0x20) = cz;
	*(long *)(d + 0x1c) = 0;
	*(long *)(d + 0x14) = ffe_fmul (sy, sz);
	*(long *)(d + 0x00) = ffe_fmul (cy, cz);
	*(long *)(d + 0x0c) = -ffe_fmul (sy, cz);
	*(long *)(d + 0x08) = -ffe_fmul (sz, cy);
	/* read straight back: separates "computed zero" from "the store was lost" */
	ffe_mb_dst = (long)d;
	ffe_mb_ay = ay;
	ffe_mb_az = az;
	ffe_mb_cy = cy;
	ffe_mb_cz = cz;
	ffe_mb_after = *(long *)d;
}

long ffe_yzt_obj;
long ffe_yzt_m0;		/* m00 read back at the next call after the build */
long ffe_yzt_ret;		/* return address of the pooled MatBuildYZT call */
long ffe_yzt_first, ffe_yzt_n, ffe_yzt_distinct;
long ffe_yzt_checked;

/* VIEWPORT DEBUG (2026-08-02): durable evidence that the game took its own
 * error escape, written by ffe_setjmp.s BEFORE the registers are restored.
 * dump_clock's LJ counter only prints every 128 presents, which is no use if
 * the machine dies before the next dump; this line survives. Capped so a
 * frame that bails every time cannot fill the disk. */
void FfeLongJmpLog (long val) __asm__("_FfeLongJmpLog");
void FfeLongJmpLog (long val)
{
	static int n;
	FILE *f;

	if (n >= 8) { n++; return; }
	n++;
	f = ffe_logopen ("PROGDIR:ffe_dump.log", "a");
	if (f) {
		fprintf (f, "LONGJMP(%ld) #%d -- the renderer abandoned a frame "
		         "(pool exhausted); this is the game's own design\n",
		         val, n);
		fflush (f);
		fclose (f);
	}
}

/* COLOUR PROBE (2026-08-02). FUNC_001791 reads a 16-bit colour with
 * `mov ax,[ebx+0x4]` (ffe.asm:111962) and hands it to DPal16toPal and then to
 * DrawSpans. The viewport is drawn almost entirely in two extreme palette
 * indices, which is what a byte-swapped colour word would produce, so the
 * value is worth seeing.
 *
 * WHY THIS AND NOT FfeArgLog: FfeArgLog peeks four words at BOTH arg1 and
 * arg2. FUNC_001791's arg2 is not a pointer, and `arg_peek`'s guard only
 * rejects <=0x1000 and odd addresses -- a value like 0x02000000 sails through
 * and reads unmapped memory. Injected on FUNC_001791 it hard-reset the
 * emulated machine a few hundred presents into the cockpit, in a build that
 * had otherwise run 25,089 presents clean. This probe dereferences ONE
 * pointer, arg1, and only after checking it lies inside the program image. */
#define FFE_COL_MAX 16
long ffe_col_v[FFE_COL_MAX];
long ffe_col_ptr[FFE_COL_MAX];
long ffe_col_n;
long ffe_col_calls;

void FfeColourLog (long num) __asm__("_FfeColourLog");
void FfeColourLog (long num)
{
	unsigned long *caller = (unsigned long *)((char *)&num + 4 + 60);
	unsigned long p = caller[1];
	extern char img_lo[] __asm__("FUNC_000286");

	(void)num;
	ffe_col_calls++;
	if (ffe_col_n >= FFE_COL_MAX) return;
	/* arg1 must be an even address inside the loaded program image. The
	 * renderer's records live there; anything else is not worth a bus
	 * error. 4 MB is a generous bound on our hunk. */
	if ((p & 1) || p < (unsigned long)img_lo
	    || p > (unsigned long)img_lo + 0x400000)
		return;
	ffe_col_ptr[ffe_col_n] = (long)p;
	ffe_col_v[ffe_col_n] = (long)*(const unsigned short *)(p + 4);
	ffe_col_n++;
}

/* GEOMETRY (2026-08-03): WHERE THE OBJECT POSITION COMES FROM.
 *
 * Diffing our projection log against the instrumented PC reference said the
 * fault in one number: the PC has ZERO of 3680 sampled vertices belonging to an
 * object within 100k of the camera, and we have 34 of 71. An object at the
 * camera origin paints a polygon across the whole viewport, which is the "huge
 * slabs" on screen.
 *
 * FUNC_001683 (ffe.asm:102221) builds the render object and fills that position
 * from the source object:
 *     mov edx,[eax+0x26] -> [esi+0x4]      x
 *     mov edx,[eax+0x2e] -> [esi+0x8]      y
 *     mov eax,[eax+0x36] -> [esi+0xc]      z
 * The three source offsets are 8 apart and sit at 2 mod 4, so each is the UPPER
 * dword of a 64-bit fixed-point coordinate whose lower dword is at 0x22 / 0x2a /
 * 0x32. This prints BOTH halves, which separates the only two possibilities
 * without a third run:
 *   - low half large, high half 0  -> the magnitude is in the wrong half, i.e.
 *     a 64-bit layout fault, and the position read is the victim not the cause;
 *   - both halves small            -> the object really is at the origin and
 *     the fault is further upstream, in whatever places the objects.
 * Arg1 is the source object (`mov eax,[ebp+0x8]`, ffe.asm:102230), so caller[1].
 */
/* THE GATE FIELD, our side. FUNC_000880 walks stellar-body records in ebx
 * (= edi = arg2, ffe.asm:64604-64607) and submits the real planet only when
 *     cmp word [ebx+0xa],byte +0x23 / jnz near JUMP_003674   (64715-64716)
 * passes. Measured: FUNC_000880 runs here (it has a counter slot) but
 * FUNC_000879 never does, so that compare fails for us. A byte-swapped 0x23
 * reads 0x2300, which is exactly what this prints if the hypothesis holds.
 */
void FfeGateField (long num) __asm__("_FfeGateField");
void FfeGateField (long num)
{
	unsigned long *caller = (unsigned long *)((char *)&num + 4 + 60);
	unsigned long rec = caller[2];
	static int n_done;
	FILE *f;

	if (n_done >= 2) return;
	if (!(rec > 0x1000 && !(rec & 1))) return;
	n_done++;
	f = ffe_logopen ("PROGDIR:ffe_enter.log", "a");
	if (f == NULL) return;
	/* WALK THE WHOLE LIST, exactly as FUNC_000880 does: records are 0x44
	 * bytes apart (`add ebx,byte +0x44`, ffe.asm:64875) and the list ends
	 * when the dword at [ebx] is zero (64877-64878). Printing every record's
	 * +0xa says which of three things is wrong -- too few bodies, the right
	 * count with wrong types, or the right set in a different order -- and
	 * each points at a different part of the generator. The first record
	 * alone could not tell them apart. */
	{
		unsigned long p = rec;
		int k;

		fprintf (f, "BODYLIST%ld base=%lx", num, rec);
		for (k = 0; k < 48 && *(const long *)p != 0; k++, p += 0x44)
			fprintf (f, " %04x",
			         (unsigned int)*(const unsigned short *)(p + 0xa));
		fprintf (f, "  n=%d\n", k);
	}
	fclose (f);
}

/* WHO BROADCASTS THE EVENT THAT STARTS A NEW GAME.
 *
 * FUNC_000144(module, event, param) is the module event dispatcher: it looks up
 * DATA_004892[module][event] and forwards `param' as the handler's only
 * argument (ffe.asm:19348-19360). FUNC_000829 is event 0 of module 2, and it
 * picks the starting-position record with `(param & 0xff00) >> 8'.
 *
 * Measured: the reference is handed param = 0, 1, 2, 3 and we are handed
 * 0, 0x101, 0x102, 0x103, 0x104 -- the low byte is right every time and bit 8
 * is set from the second call onward, which is why we build the game from
 * record 1 of DATA_006820 where the reference uses record 0.
 *
 * FUNC_000144 has 21 call sites, so its own RETURN ADDRESS is the question.
 * Filtered to event 0 because the dispatcher runs constantly and an unfiltered
 * cap of six would fill up with unrelated traffic long before this one.
 */
void FfeDispatch (long num) __asm__("_FfeDispatch");
void FfeDispatch (long num)
{
	unsigned long *caller = (unsigned long *)((char *)&num + 4 + 60);
	static int n_done;
	FILE *f;

	if (n_done >= 6) return;
	/* Event 0 with a NON-ZERO param. The first broadcast wave carries
	 * param 0 to every module and filled the cap before the interesting
	 * wave -- the one whose param arrives as 0x101 where the reference
	 * gets 1 -- ever reached the log. */
	if (caller[2] != 0 || caller[3] == 0) return;
	n_done++;
	f = ffe_logopen ("PROGDIR:ffe_enter.log", "a");
	if (f == NULL) return;
	{
	extern void FUNC_000144 (void) __asm__("FUNC_000144");
	fprintf (f, "DISPATCH%ld ret=%lx self=%lx module=%lx event=%lx param=%lx\n",
	         num, caller[0], (unsigned long)&FUNC_000144,
	         caller[1], caller[2], caller[3]);
	}
	fclose (f);
}

/* WHERE THE SYSTEM ID GETS BYTE-REVERSED.
 *
 * Measured twice, on two different values, so it is not a coincidence:
 *     [DATA_008804+0x27c] = 00064000  ->  record+0xc = 00400600
 *     [DATA_008804+0x27c] = 0006406a  ->  record+0xc = 6a400600
 * i.e. the id in the generator's record is the exact 32-bit byte reversal of
 * the commander field it comes from. The path is
 *     FUNC_000908  mov eax,[ebx+0x27c] (ffe.asm:67586)
 *       -> FUNC_000909 arg1
 *         -> FUNC_000860 arg2  (ffe.asm:67688)
 *           -> mov [ebx+0xc],eax   the record field (60292-60293)
 * and every step of that is a plain dword move, so printing the id at each end
 * says which half of the chain flips it. No le_read_sites.tsv entry covers any
 * line in this range, so it is not the swap-at-read machinery.
 */
void FfeSysId (long num) __asm__("_FfeSysId");
void FfeSysId (long num)
{
	unsigned long *caller = (unsigned long *)((char *)&num + 4 + 60);
	/* ONE LINE PER DISTINCT FUNCTION, not per call. A flat cap is useless
	 * here: the first function that runs every frame burns it in one frame
	 * and the rest of the chain never gets a line, which reads exactly like
	 * "the rest of the chain is dead". Deduping by `num` answers the actual
	 * question -- WHICH LINKS EXECUTE -- in one run. */
	static long seen[24];
	static int n_seen;
	int i;
	FILE *f;

	/* THE OBJECT ALLOCATOR IS THE EXCEPTION: log EVERY call, not the first.
	 * FUNC_000880's walk calls it once per stellar body and a zero return
	 * ends the WHOLE walk (ffe.asm:64629-64633), so the question is how many
	 * times it is reached -- 7 means the walk completed and the 0x23 record
	 * at index 4 was seen; fewer means the walk died before it. */
	if (num == 927) {
		static int n_927;
		if (n_927 >= 12) return;
		n_927++;
	} else {
		for (i = 0; i < n_seen; i++)
			if (seen[i] == num) return;
		if (n_seen >= 24) return;
		seen[n_seen++] = num;
	}

	f = ffe_logopen ("PROGDIR:ffe_enter.log", "a");
	if (f == NULL) return;
	fprintf (f, "SYSID%ld ret=%lx a1=%lx a2=%lx a3=%lx\n",
	         num, caller[0], caller[1], caller[2], caller[3]);
	fclose (f);
}

/* WHERE THE SYSTEM ID IS PACKED, our side.
 *
 * The body-list record carries the system id at +0xc (ffe.asm:60292-60293) and
 * the two machines disagree: reference 0x1aa51717, ours 0x00400600. Decoded
 * with FUNC_000852's own shifts (x = bits 0..12, y = bits 13..25, index = bits
 * 26..31) that is x=5911 y=5504 idx=6 against our suspiciously round
 * x=1536 y=512 idx=0.
 *
 * FUNC_000832 packs it into [DATA_008804+0x27c] (ffe.asm:56604-56637) with a
 * WORD store at +0x27c, a DWORD store at +0x27c and a BYTE store at +0x27f --
 * three sub-word accesses into one dword field, and neither le_read_sites.tsv
 * nor lo_byte_sites.tsv has an entry for any of them. Print the SOURCES: if x
 * and y agree with the reference then the packing is the fault and it is ours to
 * fix in the translator; if they already differ the fault is upstream.
 */
void FfePackSys (long num) __asm__("_FfePackSys");
void FfePackSys (long num)
{
	unsigned long *caller = (unsigned long *)((char *)&num + 4 + 60);
	extern unsigned char DATA_008804[] __asm__("DATA_008804");
	static int n_done;
	FILE *f;

	if (n_done >= 16) return;
	n_done++;
	f = ffe_logopen ("PROGDIR:ffe_enter.log", "a");
	if (f == NULL) return;
	{
	/* FUNC_000832 has four call sites (ffe.asm:56259, 57010, 57075, 57691).
	 * The return address names which one ran; resolve it by subtracting this
	 * function's own link address in build\amiga-out\ffe_game.symmap from the
	 * runtime `self` printed alongside it. */
	extern void FUNC_000832 (void) __asm__("FUNC_000832");
	fprintf (f, "PACKSYS%ld ret=%lx self=%lx a1=%lx a2=%lx a3=%lx"
	            "  x25c=%lx y260=%lx id27c=%lx\n",
	         num, caller[0], (unsigned long)&FUNC_000832,
	         caller[1], caller[2], caller[3],
	         *(const unsigned long *)(DATA_008804 + 0x25c),
	         *(const unsigned long *)(DATA_008804 + 0x260),
	         *(const unsigned long *)(DATA_008804 + 0x27c));
	/* 2026-10-15: positions and the PREVIOUS call's distance (+0x26c is
	 * written after the entry); twin of PC832 in build\pcprobe_proj.c */
	fprintf (f, "P832 n=%d cur=%lx,%lx,%lx tgt=%lx,%lx,%lx dist=%lx\n", n_done,
	         *(const unsigned long *)(DATA_008804 + 0x250),
	         *(const unsigned long *)(DATA_008804 + 0x254),
	         *(const unsigned long *)(DATA_008804 + 0x258),
	         *(const unsigned long *)(DATA_008804 + 0x25c),
	         *(const unsigned long *)(DATA_008804 + 0x260),
	         *(const unsigned long *)(DATA_008804 + 0x264),
	         *(const unsigned long *)(DATA_008804 + 0x26c));
	}
	fclose (f);
}

/* THE PRODUCER OF THE BODY LIST, our side.
 *
 * FUNC_000905 fills a STATIC list and hands it to the walker two lines later:
 *     push ebx / push dword DATA_008798 / call FUNC_000878    (ffe.asm:67418-20)
 *     ... push dword DATA_008798 / call FUNC_000880           (67430-67433)
 * so FUNC_000878(list, sysrec) is what decides 27 records or 7. This prints the
 * INCOMING system record, which separates the only two possibilities in one run:
 * either the record differs (we are generating a different star system, and the
 * fault is upstream of the generator) or it matches and the generator itself
 * diverges.
 *
 * The fields are the ones the generator reads at ffe.asm:63859-63905: +0x0/+0x4
 * are the seed pair (-> DATA_008843/8844), +0x8 is read BOTH as a byte (63898)
 * and as a DWORD (63901) whose value picks the generation shape via
 * `sub eax,1 / jc / jz`, +0xc goes to FUNC_000871 and +0x10 to DATA_008854.
 * Reference measured 2026-08-03: r0=8d17f528 r4=0a21fe80 r8b=00 r8d=0
 * rc=1aa51717 r10=e6, and 27 bodies out.
 */
void FfeSysGen (long num) __asm__("_FfeSysGen");
void FfeSysGen (long num)
{
	unsigned long *caller = (unsigned long *)((char *)&num + 4 + 60);
	unsigned long list = caller[1];
	unsigned long rec = caller[2];
	static int n_done;
	FILE *f;

	if (n_done >= 3) return;
	if (!(rec > 0x1000 && !(rec & 1))) return;
	n_done++;
	f = ffe_logopen ("PROGDIR:ffe_enter.log", "a");
	if (f == NULL) return;
	{
	extern unsigned char DATA_008804[] __asm__("DATA_008804");
	/* THE ID THE RECORD IS COPIED FROM. FUNC_000855 fills rec+0xc from the
	 * global DATA_008958 (ffe.asm:59903-59904), and ffedat.asm:28406 declares
	 * that label as `resb 3` with DATA_008959 (`resb 1`) right behind it -- so
	 * a dword access there spans TWO labels, and x86 puts the id's top byte
	 * (the index) in the second one. Printing the four RAW BYTES next to the
	 * dword read settles which end is wrong: bytes in x86 order means the
	 * WRITER laid it down little-endian, bytes in our order means the READ is
	 * being swapped. */
	extern unsigned char DATA_008958[] __asm__("DATA_008958");
	/* rec+0x10 is the ONE field that differs from the reference (ours 0x0c,
	 * theirs 0xe6) and it is what turns 27 bodies into 8. We copy it from
	 * DATA_008962 in FUNC_000855 (ffe.asm:59905-59906) while the reference
	 * recomputes it in FUNC_000860 from FUNC_000869's output. Print the
	 * global itself: if it already holds 0x0c the copy source is wrong, and
	 * whoever writes DATA_008962 is the next place to look. */
	{
		extern unsigned char DATA_008962[] __asm__("DATA_008962");
		fprintf (f, "D8962 %02x %02x %02x %02x  asdword=%lx\n",
		         DATA_008962[0], DATA_008962[1], DATA_008962[2],
		         DATA_008962[3], *(const unsigned long *)DATA_008962);
	}
	fprintf (f, "IDBYTES %02x %02x %02x %02x  asdword=%lx\n",
	         DATA_008958[0], DATA_008958[1], DATA_008958[2], DATA_008958[3],
	         *(const unsigned long *)DATA_008958);
	fprintf (f, "SYSGEN%ld list=%lx rec=%lx cmdr27c=%lx"
	            "  r0=%lx r4=%lx r8b=%02x r8d=%lx rc=%lx r10=%lx\n",
	         num, list, rec,
	         *(const unsigned long *)(DATA_008804 + 0x27c),
	         *(const unsigned long *)(rec + 0x0),
	         *(const unsigned long *)(rec + 0x4),
	         (unsigned int)*(const unsigned char *)(rec + 0x8),
	         *(const unsigned long *)(rec + 0x8),
	         *(const unsigned long *)(rec + 0xc),
	         *(const unsigned long *)(rec + 0x10));
	}
	fclose (f);
}

/* WHAT FILLS ARENA NODE 6 -- the mirror of PC1478in in pcprobe_proj.c.
 *
 * Both machines hand FUNC_001877 the SAME arena slot (idx=6, a6-arena = 0xdc)
 * with DIFFERENT contents, so the node SELECTION is right and the FILLER is not.
 * FUNC_001478 is the filler: three cdecl dwords (ctx, index, flag), the node it
 * writes is `index*36 + [DATA_009200]' (ffe.asm:87753-87757) and the vertex
 * DESCRIPTOR it dispatches on is `[ctx+0x150] + (index>>1)*6' (87759-87764).
 *
 * This is an ENTRY probe, so it prints the descriptor rather than the result --
 * which is the half that matters: identical descriptors on both machines mean
 * the handler's arithmetic is wrong, differing ones mean the model data feeding
 * it is. The node's own contents are already logged by NODE1876.
 *
 * Filtered to index 6. This function runs for every vertex of every object, so
 * an unfiltered probe would drown the log and slow the frame to a crawl -- the
 * same mistake an uncapped fopen in the render made once before.
 */
/* A RING, not the first N calls. The first cut of this probe logged the first
 * four calls with index 6 across ALL objects, and `vtab' differed on every line
 * -- the two machines sampled DIFFERENT models, so the descriptors could not be
 * compared at all. What matters is the call that LAST wrote node 6 before
 * FUNC_001876 read it, so the calls go into a ring here and the ring is printed
 * from FfeScene1876, at the consumer. A ring also removes the fopen per vertex
 * call, which on this machine would measure itself.
 */
#define V1478_RING 4
static struct {
	unsigned long ctx, vtab;
	long flag, seq;
	unsigned char desc[6];
} g_v1478[V1478_RING];
static long g_v1478_n;

/* THE MODEL NUMBER ITSELF -- the mirror of PC1538.
 *
 * FUNC_001538 is a one-line lookup -- `mov eax,[ebp+0x8] / mov eax,
 * [eax*4+DATA_004681]' (ffe.asm:91412-91417) -- so its ARGUMENT is the model
 * number an object asks for. The two machines interpret different models
 * (ref 176 and 360, ours 138, 130, 85, 270), and this says whether the
 * divergence is in the numbers asked for or in which objects do the asking.
 *
 * Ring dumped at the consumer, like every other pairing here.
 *
 * THE RING OF 8 IS NOT ENOUGH, measured 2026-08-05. At the first scene render
 * the totals nearly agree -- reference 455, ours 446, a deficit of exactly 9 --
 * but the last eight requests DIFFER (ours 34,24,34,24,34,24,31,139 against
 * 48,48,28,28,36,36,31,139), and at the second render the rings are identical
 * again. So the two request sequences part company somewhere inside the first
 * 455 calls, which a window of 8 can never locate. Hence the WHOLE sequence,
 * in order, up to a cap -- the reference only reaches 834 by n=8, so 4096 is
 * generous, and if it is ever hit the dump says `cap=1' instead of quietly
 * dropping the tail.
 */
#define M1538_RING 8
#define M1538_MAX 4096		/* entries kept; the counter itself is uncapped */
#define M1538_CHUNK 64		/* numbers per output line -- see the dump */
static long g_m1538[M1538_RING];
static long g_m1538_n;
static long g_m1538_seq[M1538_MAX];

/* WHICH CALL SITE ASKS FOR THE BAD NUMBERS -- the mirror of PC1538RET.
 *
 * Measured 2026-08-05 over the whole sequence: 214 of our 458 requests are
 * outside DATA_004681, where NONE of the reference's 389 is, and the bad values
 * are exactly two -- 0xB0000000 (x114) and 0xB1000000 (x100), i.e. 176 and 177
 * displaced into the TOP byte of the dword. Reading the call sites does not
 * settle which one produces them: FUNC_001749/FUNC_001750 pass `arg3 >> 5' and
 * a logical shift right can never yield 0xB0000000, so those two are excluded
 * by arithmetic, leaving the direct calls at ffe.asm:37442, 37446, 37557, 91005,
 * 91318 and the indirect `call near [DATA_007756]' at 109004. So let the caller
 * name ITSELF: histogram the return address, split by whether the number asked
 * for is in range.
 *
 * Capped at M1538_SITES distinct addresses because a histogram that silently
 * forgets is worse than no histogram: calls that do not fit are counted in
 * `ovf' and the count is printed on the line.
 */
#define M1538_ENTRIES 466	/* DATA_004681's entry count; outside == bad */
#define M1538_SITES 16
/* `first'/`last' added 2026-08-05: the census counted CALLS per site and the
 * ring recorded NUMBERS in order, but nothing tied a number to a site -- so
 * with two live sites (FUNC_001682+0x30 and FUNC_001750+0x3a) asking 108 and
 * 237 times, "we request model 440 where the reference requests 177" could not
 * be pinned on either. Keeping the first and the last number each site asked
 * for costs two longs per slot and answers it outright. */
static struct { unsigned long ret; long n, bad, first, last; } g_m1538_site[M1538_SITES];
static long g_m1538_sites, g_m1538_ovf;

/* Kept byte-for-byte the same shape as the reference's m1538_site_add so the
 * two logs diff directly. */
static void ffe_m1538_site_add (unsigned long ret, long index)
{
	int bad = (index < 0 || index >= M1538_ENTRIES);
	int s;

	for (s = 0; s < g_m1538_sites; s++)
		if (g_m1538_site[s].ret == ret) break;
	if (s == g_m1538_sites) {
		if (g_m1538_sites >= M1538_SITES) { g_m1538_ovf++; return; }
		g_m1538_site[s].ret = ret;
		g_m1538_site[s].first = index;
		g_m1538_sites++;
	}
	g_m1538_site[s].n++;
	g_m1538_site[s].last = index;
	if (bad) g_m1538_site[s].bad++;
}

void FfeModel1538 (long num) __asm__("_FfeModel1538");
void FfeModel1538 (long num)
{
	/* `caller' is sp as it stood at the BARE label FUNC_001538:, i.e. the
	 * frame the ORIGINAL caller left. inject_count.py's payload pushes
	 * MOVEM d0-d7/a0-a6 (15 x 4 = 60 bytes) and then the function number, so
	 * &num + 4 undoes the number and + 60 undoes the MOVEM. Hence
	 * caller[0] = the return address the original caller's JSR pushed --
	 * NOT the injected JSR's own, which is 4 bytes below &num -- and
	 * caller[1] = the first cdecl argument, the model number. Both come out
	 * of the SAME frame, so the histogram and the sequence can never
	 * disagree about which call they describe. */
	unsigned long *caller = (unsigned long *)((char *)&num + 4 + 60);
	long model = (long)caller[1];

	ffe_m1538_site_add (caller[0], model);
	g_m1538[g_m1538_n % M1538_RING] = model;
	if (g_m1538_n < M1538_MAX) g_m1538_seq[g_m1538_n] = model;
	g_m1538_n++;
}

/* Chunked into M1538_CHUNK numbers per line rather than one enormous one: a
 * 4096-entry sequence is ~20 KB on a single line, and this log is read back
 * through tools that were never asked to hold one. Wrapping is not truncating
 * -- every entry is printed, and `p=' numbers the continuation lines so the
 * order survives. Format is byte-for-byte the reference's PC1538SEQ apart from
 * the prefix, so the two logs diff directly. */
static void ffe_m1538_dump (FILE *f, long n)
{
	long kept = g_m1538_n < M1538_MAX ? g_m1538_n : M1538_MAX;
	long i;
	int j;

	fprintf (f, "MODEL1538 total=%ld models=", g_m1538_n);
	for (j = 0; j < M1538_RING; j++) {
		int k = (int)((g_m1538_n + j) % M1538_RING);
		if (g_m1538_n > j || g_m1538[k]) fprintf (f, "%ld,", g_m1538[k]);
	}
	fprintf (f, "\n");

	/* `i == 0 ||' so a dump with nothing recorded still emits its header line
	 * -- an empty sequence must be visible as one, not as a missing line. */
	for (i = 0; i == 0 || i < kept; i += M1538_CHUNK) {
		long e = i + M1538_CHUNK < kept ? i + M1538_CHUNK : kept;
		long q;

		fprintf (f, "MODEL1538SEQ n=%ld p=%ld total=%ld kept=%ld cap=%d ",
		         n, i / M1538_CHUNK, g_m1538_n, kept,
		         g_m1538_n > M1538_MAX ? 1 : 0);
		for (q = i; q < e; q++)
			fprintf (f, "%s%ld", q > i ? "," : "", g_m1538_seq[q]);
		fprintf (f, "\n");
	}

	/* The call-site census, one `retaddr:total:outofrange' per site. `slots'
	 * is how many distinct addresses were seen and `ovf' how many CALLS were
	 * dropped for want of a slot -- printed even when zero, because a silent
	 * cap would read as "that site does not exist". */
	fprintf (f, "MODEL1538RET n=%ld slots=%ld ovf=%ld",
	         n, g_m1538_sites, g_m1538_ovf);
	for (i = 0; i < g_m1538_sites; i++)
		fprintf (f, " %lx:%ld:%ld:m%ld..%ld", g_m1538_site[i].ret,
		         g_m1538_site[i].n, g_m1538_site[i].bad,
		         g_m1538_site[i].first, g_m1538_site[i].last);
	fprintf (f, "\n");
}

/* THE MODEL INTERPRETER'S OPCODE STREAM -- the mirror of PC1772.
 *
 * FUNC_001772's loop reads its opcode as a 16-bit WORD from the bytecode and
 * takes the low five bits as the handler index (ffe.asm:111196-111202). The
 * reference dispatches heavily to entries 5 and 10; we dispatch to 6. The
 * bytecode is reached through a RUN-TIME pointer, so no storage correction can
 * apply -- and not one label in the whole of ffebmp is typed be16 (3366 are
 * `le', raw x86 bytes).
 *
 * HYPOTHESIS: every opcode word is byte-reversed here, so `and 0x1f' takes the
 * low bits of the wrong byte.  WHAT WOULD KILL IT: these words coming out
 * IDENTICAL to the reference's. Both probes read at ENTRY, so no handler has to
 * run for the two logs to be comparable.
 *
 * FUNC_001772 (object, bytecode): arg2 is the stream.
 */
/* WHICH STRING THE TEXT OPCODE ASKS FOR, and how long it comes out (2026-08-06).
 *
 * The model-index sequence measured the reference entering FIVE glyph models
 * per text opcode where we enter ONE, so our expanded string is short. The
 * string is built by FUNC_001344_StringExpandFFCode, called from FUNC_001747 as
 *     lea eax,[ebp+0xfffffd64] / push eax          -> arg3
 *     movsx eax,word [eax+0x6] / push eax          -> arg2, the string ID
 *     lea eax,[ebp+0xfffffd78] / push eax          -> arg1, the output buffer
 * (ffe.asm:108671-108681), so arg2 is what to compare first. The ID's stream
 * word IS a registered swap site (le_read_sites.tsv:108676), so this is a
 * measurement of whether that registration is doing its job, not a guess that
 * it is not.
 *
 * arg1 is recorded too, because on this side inject_count can only reach a
 * function's ENTRY: the buffer address seen at call N lets call N+1's dump read
 * back what call N actually produced, which is the only way to see the OUTPUT
 * from an entry hook. */
#define STR1344_MAX 12
long ffe_s44_id[STR1344_MAX], ffe_s44_buf[STR1344_MAX];
long ffe_s44_len[STR1344_MAX], ffe_s44_n, ffe_s44_total;
unsigned char ffe_s44_txt[STR1344_MAX][12];

void FfeStr1344 (long num) __asm__("_FfeStr1344");
void FfeStr1344 (long num)
{
	unsigned long *caller = (unsigned long *)((char *)&num + 4 + 60);
	unsigned long out = caller[1];
	long id = (long)caller[2];

	/* Read back the PREVIOUS call's output before recording this one: at entry
	 * the current call has not written anything yet. */
	if (ffe_s44_n > 0 && ffe_s44_len[ffe_s44_n - 1] < 0) {
		unsigned long p = (unsigned long)ffe_s44_buf[ffe_s44_n - 1];

		if (p > 0x1000) {
			const unsigned char *s = (const unsigned char *)p;
			int k = 0;

			while (k < 11 && s[k]) { ffe_s44_txt[ffe_s44_n - 1][k] = s[k]; k++; }
			ffe_s44_len[ffe_s44_n - 1] = k;
		} else {
			ffe_s44_len[ffe_s44_n - 1] = -2;
		}
	}
	ffe_s44_total++;
	if (ffe_s44_n < STR1344_MAX) {
		ffe_s44_id[ffe_s44_n] = id;
		ffe_s44_buf[ffe_s44_n] = (long)out;
		ffe_s44_len[ffe_s44_n] = -1;		/* not read back yet */
		ffe_s44_n++;
	}
}

/* FUNC_001686 -- THE BRANCH THAT DECIDES WHETHER A GLYPH IS INTERPRETED.
 *
 * FUNC_001747's character loop does
 *     call FUNC_001686 / test eax,eax / jnz JUMP_006742
 * and only the ZERO branch recurses into FUNC_001772 on the glyph model
 * (ffe.asm:108781-108795). Measured 2026-08-06: the reference enters FIVE glyph
 * models per text opcode and we enter ONE, with the expanded string proven
 * IDENTICAL on both ("FRONTIER", id 16512, len 8) -- so the string is not the
 * fault and this branch is the next link.
 *
 * FUNC_001686 is nineteen lines (ffe.asm:102438-102456) and its first act is
 *     mov ecx,[eax+0x148] / mov edx,[edx+0x1c] / shl edx,cl
 * -- a VARIABLE-COUNT SHIFT whose count is an unbounded record field, the exact
 * class of the mask landed in lower.py this morning. Recording the count is
 * therefore the first thing to do: a count of 32 or more is the difference
 * between "shift by 0" on x86 and "shift everything out" on an unmasked 68k.
 *
 * THE TWO FIELDS ARE NOT READ THE SAME WAY, and getting that wrong cost a
 * retraction on 2026-08-06. I read both as plain native dwords, saw
 * `in=6a000000' against the reference's `in=6a', and reported a byte-order bug.
 * There is none: ffe.asm:102445 IS registered in le_read_sites.tsv and the
 * emitted code swaps it in full --
 *     move.l (28,a0),d2 / rol.w #8,d2 / swap d2 / rol.w #8,d2
 * -- so the GAME reads 0x6a exactly like the reference, and it was the PROBE
 * that read the raw x86 bytes. The project's own rule, broken by the person who
 * wrote it down: a probe must reproduce the GAME's access, not the natural C
 * one.
 *
 *   [a2+0x1c]   registered swap site -> byte-reversed HERE to match the game
 *   [a1+0x148]  NOT a swap site (ffe.asm:102443 is absent from the tsv, and the
 *               emitted `move.l (328,a0),d1' carries no swap) -> read natively.
 *               Confirmed by measurement rather than by reading alone: it came
 *               out 16 on both machines, and a mis-swapped 16 would read
 *               0x10000000. */
#define S86_MAX 12
long ffe_s86_cl[S86_MAX], ffe_s86_in[S86_MAX], ffe_s86_out[S86_MAX];
long ffe_s86_n, ffe_s86_total, ffe_s86_clmax;

void FfeShift1686 (long num) __asm__("_FfeShift1686");
void FfeShift1686 (long num)
{
	unsigned long *caller = (unsigned long *)((char *)&num + 4 + 60);
	unsigned long a1 = caller[1], a2 = caller[2];

	ffe_s86_total++;
	if (a1 <= 0x1000 || (a1 & 1) || a2 <= 0x1000 || (a2 & 1)) return;
	{
		long cl = *(const long *)(a1 + 0x148);	/* no swap site: read native */
		unsigned long raw = *(const unsigned long *)(a2 + 0x1c);
		/* The registered site's swap, reproduced: the game does
		 * rol.w #8 / swap / rol.w #8, which is a full 32-bit byte reversal. */
		long v = (long)((raw >> 24) | ((raw >> 8) & 0xff00UL)
		                | ((raw << 8) & 0xff0000UL) | (raw << 24));

		if (cl > ffe_s86_clmax) ffe_s86_clmax = cl;
		if (ffe_s86_n < S86_MAX) {
			ffe_s86_cl[ffe_s86_n] = cl;
			ffe_s86_in[ffe_s86_n] = v;
			/* What the GAME will compute, masked the way x86 masks it. Printed
			 * beside the raw count so a count over 31 is visible as such. */
			ffe_s86_out[ffe_s86_n] = (long)((unsigned long)v << (cl & 31));
			ffe_s86_n++;
		}
	}
}

/* FUNC_001470 -- THE PRODUCER OF THE POSITION TRIPLE (ffe.asm:86939-86997).
 *
 * CULL1687 measured the same size with a different position on the two
 * machines, and this is where that position is made. It is a SUM, which is what
 * makes the split worth having:
 *
 *     edi = DATA_009200, edx = [edi]            a BSS pointer to the node array
 *     eax = (s8)bl * 9, records are 36 bytes    a SIGNED BYTE index
 *     if [rec+0x14] == 0: call FUNC_001472      fill the local offset
 *     rec[+0x4] = rec[+0x18] + ctx[+0x4]        x
 *     rec[+0x8] = rec[+0x1c] + ctx[+0x8]        y
 *     rec[+0xc] = rec[+0x20] + ctx[+0xc]        z
 *     return &rec
 *
 * Three candidates, and one line of output separates them: the INDEX, the
 * LOCAL OFFSET that FUNC_001472 computes, and the CALLER'S CONTEXT position.
 * Whichever of the three already differs is the fault; the other two are
 * downstream.
 *
 * The read-back is deferred to the next call, as STR1344 does, because
 * inject_count reaches only a function's ENTRY and rec[+0x4..] is written
 * during the call. The index and the context position ARE available at entry
 * and are recorded there.
 *
 * Native reads: none of ffe.asm:86939-86997 appears in le_read_sites.tsv, and
 * the node array is reached through a BSS pointer (DATA_009200 is `resb 4'), so
 * there is no converted image involved and a native dword read is the game's
 * own access. Checked, not assumed -- the last two times I skipped this check
 * it produced a retraction. */
#define S70_MAX 16
#define OBR_MAX 24
long ffe_s70_idx[S70_MAX], ffe_s70_rec[S70_MAX], ffe_s70_ctx[S70_MAX];
long ffe_s70_cx[S70_MAX], ffe_s70_cy[S70_MAX], ffe_s70_cz[S70_MAX];
long ffe_s70_ox[S70_MAX], ffe_s70_oy[S70_MAX], ffe_s70_oz[S70_MAX];
long ffe_s70_done[S70_MAX], ffe_s70_n, ffe_s70_total;

void FfeNode1470 (long num) __asm__("_FfeNode1470");
void FfeNode1470 (long num)
{
	unsigned long *caller = (unsigned long *)((char *)&num + 4 + 60);
	unsigned long ctx = caller[1];
	long idx = (long)(signed char)(caller[2] & 0xff);
	extern char nodebase[] __asm__("DATA_009200");
	unsigned long base = *(const unsigned long *)nodebase;
	long slot;

	/* CIRCULAR since 2026-09-02. The first-N window only ever showed the
	 * text glyphs of the intro; the calls that matter are the LAST ones
	 * before the renderer's divide dies, and those are printed from
	 * FfeCrashDie. ffe_s70_total counts every call; the slot is total mod
	 * ring, so the ring always holds the most recent S70_MAX calls. */
	if (ffe_s70_total > 0) {
		long prev = (ffe_s70_total - 1) % S70_MAX;
		unsigned long r = (unsigned long)ffe_s70_rec[prev];

		if (!ffe_s70_done[prev] && r > 0x1000 && !(r & 1)) {
			ffe_s70_ox[prev] = *(const long *)(r + 0x18);
			ffe_s70_oy[prev] = *(const long *)(r + 0x1c);
			ffe_s70_oz[prev] = *(const long *)(r + 0x20);
		}
		ffe_s70_done[prev] = 1;
	}
	slot = ffe_s70_total % S70_MAX;
	ffe_s70_total++;
	if (ffe_s70_n < S70_MAX) ffe_s70_n++;
	ffe_s70_idx[slot] = idx;
	ffe_s70_ctx[slot] = (long)ctx;
	ffe_s70_done[slot] = 0;
	if (ctx <= 0x1000 || (ctx & 1) || base <= 0x1000) {
		ffe_s70_rec[slot] = 0;
		ffe_s70_cx[slot] = ffe_s70_cy[slot] = ffe_s70_cz[slot] = 0;
		return;
	}
	ffe_s70_rec[slot] = (long)(base + (unsigned long)(idx * 36));
	ffe_s70_cx[slot] = *(const long *)(ctx + 0x4);
	ffe_s70_cy[slot] = *(const long *)(ctx + 0x8);
	ffe_s70_cz[slot] = *(const long *)(ctx + 0xc);
}

/* FUNC_001687 -- WHERE THE CULL IS ACTUALLY DECIDED (ffe.asm:102457-102507).
 *
 * FUNC_001686 does nothing but shift and tail-call this, and THIS is what
 * FUNC_001747 branches on: non-zero means "do not interpret the glyph".
 *
 *     ecx = arg2                 the shifted size
 *     eax = arg1                 = context+4, so [eax+0/4/8] is the position
 *     edx = [eax+0x8] + ecx      z + size
 *     cmp edx,byte +0x40 / jnl   z + size < 64 -> return 1, CULL
 *     ... else |x| and |y| against edx, 1 to cull or 0 to interpret
 *
 * Native reads, and that is CHECKED rather than assumed this time: no line in
 * 102457-102507 appears in le_read_sites.tsv and the emitted FUNC_001687.s
 * carries no rol.w/swap on any of them. The lesson from an hour ago is written
 * into SHIFT1686 above.
 *
 * `near' counts the first branch specifically, because "z + size < 64" and "|x|
 * too big" are different faults and the aggregate cannot tell them apart. */
#define S87_MAX 12
long ffe_s87_sz[S87_MAX], ffe_s87_x[S87_MAX], ffe_s87_y[S87_MAX];
long ffe_s87_z[S87_MAX], ffe_s87_n, ffe_s87_total, ffe_s87_near;

void FfeCull1687 (long num) __asm__("_FfeCull1687");
void FfeCull1687 (long num)
{
	unsigned long *caller = (unsigned long *)((char *)&num + 4 + 60);
	unsigned long a1 = caller[1];
	long sz = (long)caller[2];

	ffe_s87_total++;
	if (a1 <= 0x1000 || (a1 & 1)) return;
	{
		long x = *(const long *)(a1 + 0x0);
		long y = *(const long *)(a1 + 0x4);
		long z = *(const long *)(a1 + 0x8);

		if (z + sz < 0x40) ffe_s87_near++;
		if (ffe_s87_n < S87_MAX) {
			ffe_s87_sz[ffe_s87_n] = sz;
			ffe_s87_x[ffe_s87_n] = x;
			ffe_s87_y[ffe_s87_n] = y;
			ffe_s87_z[ffe_s87_n] = z;
			ffe_s87_n++;
		}
	}
}

/* The model-index sequence, mirrored by PCMODSEQ on the reference side. */
#define MODSEQ_MAX 400
long ffe_modseq[MODSEQ_MAX];
long ffe_modseq_n, ffe_modseq_total;
/* For every SUB-MODEL (idx==-1) descent, its ffebmp-relative offset keyed off
 * model 176's pointer -- machine-independent, so it compares directly to the
 * reference's PC1772REL. Distinct set printed from dump_clock. */
long ffe_modrel[MODSEQ_MAX]; long ffe_modrel_n;

void FfeCode1772 (long num) __asm__("_FfeCode1772");
void FfeCode1772 (long num)
{
	unsigned long *caller = (unsigned long *)((char *)&num + 4 + 60);
	unsigned long code = caller[2];
	static int n_done;
	FILE *f;
	int i;

	/* RENDERPATH: this file already carries the CODE1772 payload, so the
	 * counter is bumped here instead of injecting a second one. Before every
	 * early return below -- the count must be of ENTRIES, not of dumps. */
	FfeRenderPath (num);

	/* LOOP BOUNDARIES, added 2026-08-05 after the opcode trace measured the
	 * stream pointer reaching 0x000008FC -- low memory, the 68k exception
	 * vectors. That single line explains the hard reset with no crash log, but
	 * NOT who produced the pointer, because opcode 14 recurses (FUNC_001749 ->
	 * FUNC_001750 -> FUNC_001773 -> a fresh FUNC_001772) and adjacent OPT lines
	 * can belong to different dispatcher invocations.
	 *
	 * This line is emitted BEFORE the two guards below on purpose. The
	 * `code > 0x1000' guard is exactly what hid the fault: it filters out the
	 * wild low-memory pointers this is hunting, so the old CODE1772M output
	 * could only ever show healthy streams. Unconditional, flushed per line,
	 * capped so a runaway cannot fill the disk. */
	/* WHICH MODEL, ENTRY BY ENTRY (2026-08-06) -- and NO I/O, unlike the
	 * OPENTER line this replaces, which did a fopen/fprintf/fclose on every one
	 * of 2000 entries and is exactly the shape the method rules forbid.
	 *
	 * The opcode-sequence diff put the first divergence at dispatch 22: the two
	 * machines agree on twenty-one dispatches and then enter glyph models 240
	 * bytes apart, because FUNC_001747 (opcode 10, "Text string") recurses into
	 * FUNC_001772 on `[ebx+edi*4-0x40]' for character edi. The MODEL INDEX names
	 * that glyph, and the glyph decides which of three things is at fault -- the
	 * character byte, FUNC_001344_StringExpandFFCode's expansion, or the font
	 * pointer table.
	 *
	 * The scan is the reference's, expression for expression: the LARGEST entry
	 * of DATA_004681 at or below `code'. Not `code - 0x40': an exact match on
	 * that came back -1 on both machines, which only proved the guess wrong.
	 * A native dword read IS the game's own access here -- the entries are
	 * `dd SYMBOL', staticscan types the label dword-only, and both machines load
	 * them natively -- so this probe reproduces the access rather than
	 * inventing one.
	 *
	 * Capped at MODSEQ_MAX because it is a 466-entry linear scan per entry on an
	 * emulated 68020, and an instrumentation cost that grows without bound is
	 * how a probe stops the run from reaching what it was built to watch. */
	{
		extern unsigned char DATA_004681[] __asm__("DATA_004681");
		extern long ffe_modseq[], ffe_modseq_n, ffe_modseq_total;

		if (ffe_modseq_n < MODSEQ_MAX) {
			const unsigned long *tab = (const unsigned long *)DATA_004681;
			unsigned long best = 0;
			long idx = -1, q;

			for (q = 0; q < M1538_ENTRIES; q++)
				if (tab[q] <= code && tab[q] > best) { best = tab[q]; idx = q; }
			ffe_modseq[ffe_modseq_n] = idx;
			ffe_modseq_n++;
			if (idx == -1 && tab[176] && ffe_modrel_n < MODSEQ_MAX) {
				long rel = (long)(code - tab[176]); int seen = 0, z;
				for (z = 0; z < ffe_modrel_n; z++)
					if (ffe_modrel[z] == rel) { seen = 1; break; }
				if (!seen) ffe_modrel[ffe_modrel_n++] = rel;
			}
		}
		ffe_modseq_total++;
	}

	if (n_done >= 6) return;
	if (!(code > 0x1000 && !(code & 1))) return;
	n_done++;
	f = ffe_logopen ("PROGDIR:ffe_enter.log", "a");
	if (f == NULL) return;
	{
		const unsigned short *w = (const unsigned short *)code;
		const unsigned char *b = (const unsigned char *)code;

		/* PRINT WHAT THE GAME COMPUTES, not the raw native word. The first
		 * cut of this probe read the stream with a plain C `unsigned short',
		 * which on this machine is a BIG-endian load, and reported opcode 0
		 * everywhere -- while the translated code has swapped this read since
		 * 2026-07-30 (le_read_sites.tsv:111195, emitted as `move.w (a0),d7 /
		 * rol.w #8,d7'). The probe was wrong, not the game. */
	/* WHICH MODEL, as an INDEX rather than an address -- the only form the two
	 * machines can be compared in. DATA_004681 (ffebmp.asm:41814, "Ship object
	 * table") is 1864 bytes = 466 dword pointers, one per model header, and the
	 * interpreter is handed header+0x40. So scanning the table for `code - 0x40`
	 * names the model.
	 *
	 * A plain C dword read of the table IS the game's own read here, unlike the
	 * word read that misled this probe an hour ago: the entries are `dd SYMBOL`,
	 * the manifest types the label be32, and both machines load them native.
	 */
	{
		extern unsigned char DATA_004681[] __asm__("DATA_004681");
		const unsigned long *tab = (const unsigned long *)DATA_004681;
		long idx = -1, q;

		unsigned long best = 0;

		/* The LARGEST table entry at or below `code'. The header size is not
		 * assumed -- an exact `code - 0x40' match came back -1 on both
		 * machines. The delta printed alongside IS the header size, measured. */
		for (q = 0; q < 466; q++)
			if (tab[q] <= code && tab[q] > best) { best = tab[q]; idx = q; }
		fprintf (f, "CODE1772M n=%d model=%ld delta=%ld code=%lx\n",
		         n_done, idx, (long)(code - best), code);
	}
		fprintf (f, "CODE1772 n=%d code=%lx words=", n_done, code);
		for (i = 0; i < 8; i++)
			fprintf (f, "%04x,", (unsigned)(b[i * 2] | (b[i * 2 + 1] << 8)));
		fprintf (f, "  op0=%d bytes=",
		         (int)((b[0] | (b[1] << 8)) & 0x1f));
		for (i = 0; i < 8; i++) fprintf (f, "%02x,", b[i]);
		fprintf (f, "\n");
	}
	fclose (f);
}

/* WHICH OPCODE KILLS THE MACHINE -- one line per iteration of the interpreter.
 *
 * The box hard-resets a few tens of frames after the game enters the world and
 * writes NO ffe_crash.log at all, although _FfeTrapCode is linked and has
 * written one before -- so this is a genuine reset, the signature of a wild
 * write or a wild JSR rather than a caught exception. Everything cheap is
 * eliminated BY MEASUREMENT and must not be re-hunted: the render-entry
 * counters at the last surviving dump are small and consistent
 * (1682=31 1683=31 1772=62 1773=62 1774=62 1745=63 1750=31 1687=70 1876=0),
 * ARENA reports max=3428 neg=0 calls=63 against a 32768 limit, the 3000-node
 * pool is untouched (POOL16=0 NODE72=0, LJ n=0), and static analysis has
 * cleared the dispatcher fetch, every model-stream word operand, the embedded
 * jump tables and FUNC_001480's write loop, each with the emitted 68k read.
 * Both boots die during the SIXTH entry to FUNC_001772, so the killer is
 * inside one opcode handler and only a per-opcode trace names it: the LAST
 * `OPT' line in the log is the opcode whose handler never came back.
 *
 * Injected at the LOOP HEAD (JUMP_006896, ffe.asm:111194), immediately before
 * the fetch at 111195 and therefore before `call eax' at 111202 -- the line is
 * on disk before the handler runs, which is the whole point.
 *
 * THE WORD IS ASSEMBLED BY HAND, b[0] | (b[1] << 8), and that is MANDATORY.
 * A plain C `unsigned short' read is a BIG-endian load on the 68k and does NOT
 * reproduce the game's access: the bytecode is raw x86 bytes ('le' in the
 * storage manifest) and the translated fetch swaps at the site --
 * tools\datamap\le_read_sites.tsv:111195, emitted as `move.w (a0),d7 /
 * rol.w #8,d7' at build\xlat-full\FUNC_001772.s:142-143. The first cut of the
 * CODE1772 probe above got this wrong and reported opcode 0 everywhere; three
 * findings on this project have been retracted for the same mistake.
 *
 * No pointer guard, on purpose. The game performs the identical two-byte read
 * through the identical pointer one instruction later, so a guard here could
 * only hide the very access being measured -- and on the 68k low memory is
 * mapped, so a small pointer reads rather than faults.
 *
 * fopen/fclose per line rather than a held handle: the other probes in this
 * file open PROGDIR:ffe_enter.log for append per line, and an exclusive write
 * lock held across the whole render would make every one of those fopens fail
 * -- CODE1772, SCENE1876 and the rest would quietly stop logging. fflush
 * before the fclose all the same, so the line is out even if the machine dies
 * between the two.
 *
 * Capped at OPT_MAX lines and it SAYS SO once when it stops: a trace that
 * silently ends reads exactly like a trace that reached the crash. Expected
 * volume is a few hundred to a couple of thousand lines. */
#define OPT_MAX 30000
long ffe_opt_n;

/* FUNC_001753 -- THE SKIP ARITHMETIC ITSELF (2026-08-05).
 *
 * All four conditional-skip opcodes delegate here, and this is what moves the
 * stream: ffe.asm:109288-109310 computes, for a non-zero flag,
 *     ret = ptr + 2 * ((s16)word >> 5)
 * and for `word >> 5 == 0' it instead clears DATA_007824, which makes the
 * DISPATCHER LOOP EXIT (ffe.asm:111205-111206). That third path matters for
 * reading any measurement here: when the loop exits, control returns to an
 * outer dispatcher and the next entry in the OPT ring is far away -- a LARGE
 * DELTA THAT IS ENTIRELY NORMAL and must not be read as a wild pointer. The
 * ring alone cannot tell the two apart, which is why the inputs are recorded
 * here instead of inferred from it.
 *
 * A histogram of the WORD, because the word decides everything: op = word&0x1f,
 * the skip distance is 2*((s16)word>>5), and word>>5 == 0 is the loop exit.
 * The reference's shim records the same, so the two diff directly. Bounded
 * slots with an overflow count, because a histogram that silently forgets
 * reads as "that word never occurred". */
#define SK1753_SLOTS 12
long ffe_sk_calls, ffe_sk_ovf, ffe_sk_zeroflag, ffe_sk_exit;
/* per CALLER: [0]=1752 op11, [1]=1755 op12, [2]=1756 op19, [3]=1757 op20,
 * [4]=none of the four (which must stay at zero -- a non-zero here means the
 * bucketing is wrong, not that a fifth caller exists). */
long ffe_sk_by[5], ffe_sk_by_zero[5];
long ffe_sk_w[SK1753_SLOTS], ffe_sk_n[SK1753_SLOTS], ffe_sk_slots;

void FfeSkip1753 (long num) __asm__("_FfeSkip1753");
void FfeSkip1753 (long num)
{
	/* Same frame arithmetic as FfeModel1538: the payload pushes MOVEM
	 * d0-d7/a0-a6 (60 bytes) and then the function number, so &num + 4 + 60
	 * lands on the ORIGINAL caller's frame -- caller[0] its return address,
	 * caller[1..3] the three cdecl arguments (flag, ptr, word). */
	unsigned long *caller = (unsigned long *)((char *)&num + 4 + 60);
	long flag = (long)caller[1];
	long word = (long)caller[3];
	long i;

	/* WHICH OF THE FOUR SKIP HANDLERS HANDS US THE FALSE FLAG (2026-08-06).
	 *
	 * `noflag' is 68% of our calls against the reference's 13%, and the
	 * decoder that feeds it -- FUNC_001473 -- is now measured IDENTICAL on
	 * both machines, so the fault is in one of the four handlers between them.
	 * An aggregate cannot say which. caller[0] is FUNC_001753's own return
	 * address, i.e. a point inside whichever handler called it, so bucketing by
	 * the largest handler address at or below it names the caller exactly.
	 *
	 * Anchors are taken as CODE symbols (`extern char x[] __asm__(...)'), never
	 * from an address some other probe printed: BSS is a separate AmigaDOS hunk
	 * and a data anchor resolves a code address into a plausible WRONG
	 * function -- that cost a diagnosis on 2026-08-05. */
	{
		extern char sk_h0[] __asm__("FUNC_001752");
		extern char sk_h1[] __asm__("FUNC_001755");
		extern char sk_h2[] __asm__("FUNC_001756");
		extern char sk_h3[] __asm__("FUNC_001757");
		unsigned long a[4];
		unsigned long pc = caller[0];
		int best = 4, k;

		a[0] = (unsigned long)sk_h0; a[1] = (unsigned long)sk_h1;
		a[2] = (unsigned long)sk_h2; a[3] = (unsigned long)sk_h3;
		for (k = 0; k < 4; k++)
			if (a[k] <= pc && (best == 4 || a[k] > a[best]))
				best = k;
		ffe_sk_by[best]++;
		if (flag == 0) ffe_sk_by_zero[best]++;
	}

	ffe_sk_calls++;
	if (flag == 0) { ffe_sk_zeroflag++; return; }
	if (((unsigned long)word >> 5) == 0) { ffe_sk_exit++; return; }
	for (i = 0; i < ffe_sk_slots; i++)
		if (ffe_sk_w[i] == word) { ffe_sk_n[i]++; return; }
	if (ffe_sk_slots >= SK1753_SLOTS) { ffe_sk_ovf++; return; }
	ffe_sk_w[ffe_sk_slots] = word;
	ffe_sk_n[ffe_sk_slots] = 1;
	ffe_sk_slots++;
}

/* FUNC_001473 -- THE PARAMETER DECODER THAT PRODUCES THE SKIP CONDITION.
 *
 * ffe.asm:87105-87147. It switches on the top two bits of its second argument:
 *   0x00  literal            eax = arg & 0x3f
 *   0x40  16-bit             and ax,0x3f / movsx eax,ax / shl eax,0xa
 *   0x80  word through ptr   movzx eax,word [[a1+0x14c] + (arg&0x3f)*2 + 0x9c]
 *   0xc0  dword through ptr  mov eax,[a1 + (arg&0x3f)*4 + 0x54]
 *
 * `noflag' at FUNC_001753 is 62% of our calls against 12% of the reference's,
 * so the condition this returns is what diverges. Splitting by MODE is what
 * separates the two candidates: matching modes with differing values means the
 * fault is in the DATA one of the two pointer reads lands on, and the mode says
 * which; differing modes mean the fault is upstream, in what the stream hands
 * this function.
 *
 * The value is recomputed here rather than taken from the return, because
 * inject_count can only reach a function's ENTRY. Both pointer reads are
 * unregistered in le_read_sites.tsv and the game performs them as plain 68k
 * reads, so the native C reads below reproduce the game's own access exactly --
 * which is the whole point; a probe that reads differently than the game
 * measures a different quantity and has produced three retracted findings here.
 */
long ffe_cond_mode[4], ffe_cond_calls;
long ffe_cond_arg[4], ffe_cond_val[4], ffe_cond_seen[4];

/* FUNC_001534 -- READ OUT OF THE DISASSEMBLY, ffe.asm:91108-91158:
 *
 *     mov esi,[DATA_008804] / shr esi,0xa   ...   mov eax,esi / ret
 *
 * Its return IS the game clock shifted right by ten, and nothing between the
 * load and the return touches esi (it is pushed at 91114 and popped at 91154,
 * and the intervening calls are cdecl, which preserves esi). FUNC_001536/1537
 * store its LOW WORD at record offset +0x9c, which is the field measuring 10
 * here against 1 on the reference.
 *
 * So the field is a CLOCK SAMPLE, and this probe confirms the identification
 * rather than assuming it: record [DATA_008804] at entry alongside the value
 * the function is about to return. If (clock >> 10) & 0xffff equals the +0x9c
 * field, the chain is closed and the x10 is a timing difference; if it does
 * not, something else writes that field and the reading is wrong. */
long ffe_f34_calls, ffe_f34_seen;
long ffe_f34_clock[4], ffe_f34_ret[4], ffe_f34_n;

void FfeFunc1534 (long num) __asm__("_FfeFunc1534");
void FfeFunc1534 (long num)
{
	extern char clock_arr[] __asm__("DATA_008804");
	unsigned long c = *(const unsigned long *)clock_arr;

	ffe_f34_calls++;
	if (ffe_f34_n < 4) {
		ffe_f34_clock[ffe_f34_n] = (long)c;
		/* THE MASK THAT MADE THIS PROBE LIE, removed 2026-08-06. It read
		 * `(c >> 10) & 0xffff' because FUNC_001536 stores the LOW WORD --
		 * but the reference's shim records FUNC_001534's REAL return, which
		 * is unmasked. Comparing the two produced "same clock input, wildly
		 * different output" and very nearly a reported bug in the shift.
		 * Unmasked they agree exactly: 2097156/2097163/2097169/2097175 here
		 * and there. RECORD WHAT THE FUNCTION RETURNS; mask only when the
		 * other side masks too. */
		ffe_f34_ret[ffe_f34_n] = (long)(c >> 10);
		ffe_f34_n++;
	}
}

void FfeCondIdx (long mode, long idx, long v);

/* FUNC_001473's four cases, replicated so both this probe and the FUNC_001758
 * one can decode an operand without calling the game's own function (which is
 * hooked, and calling it would double-count its histogram). Native C reads
 * because the game performs these as plain 68k reads -- neither is registered
 * in le_read_sites.tsv. */
long FfeDec1473 (unsigned long a1, long arg)
{
	long idx = arg & 0x3f;

	switch ((arg >> 6) & 3) {
	case 0:
		return idx;
	case 1:
		return ((long)(short)(arg & 0x3f)) << 10;
	case 2:
		if (a1 > 0x1000 && !(a1 & 1)) {
			unsigned long p = *(const unsigned long *)(a1 + 0x14c);
			if (p > 0x1000 && !(p & 1))
				return *(const unsigned short *)(p + idx * 2 + 0x9c);
		}
		return 0;
	default:
		if (a1 > 0x1000 && !(a1 & 1))
			return *(const long *)(a1 + idx * 4 + 0x54);
		return 0;
	}
}

/* FUNC_001758 -- the SOLE writer of [a1 + idx*4 + 0x54], the fields that come
 * out nine times too large. ffe.asm:109436-109614: it decodes two operands
 * through FUNC_001473, picks one of SIXTEEN operations from the embedded jump
 * table DATA_000050 by ([ebp+0x10] >> 4) & 0xf, and stores the 16-bit result
 * at ([ebp+0x10] >> 8) & 7. Its translation is already verified correct
 * instruction by instruction, so the x9 is propagated through its OPERANDS.
 *
 * First store per DESTINATION index, with the selector and both operands: the
 * first index whose value already differs names the operation, and the selector
 * says which of the sixteen ran. Recorded at ENTRY, so the stored value is not
 * visible here -- it is read back from the CONDC0 histogram, which samples the
 * same fields. */
long ffe_w58_sel[8], ffe_w58_o1[8], ffe_w58_o2[8], ffe_w58_b[8];
long ffe_w58_seen[8], ffe_w58_n[8], ffe_w58_calls;
long ffe_w58_a1, ffe_w58_p14c, ffe_w58_w9c, ffe_w58_ptrseen;
long ffe_w58_bytes[12];
long ffe_w58_w9c_first, ffe_w58_w9c_last, ffe_w58_w9c_min, ffe_w58_w9c_max;
long ffe_w58_clk_first, ffe_w58_clk_last;

void FfeWrite1758 (long num) __asm__("_FfeWrite1758");
void FfeWrite1758 (long num)
{
	unsigned long *caller = (unsigned long *)((char *)&num + 4 + 60);
	unsigned long a1 = caller[1];
	unsigned long a2 = caller[2];
	long a3 = (long)caller[3];
	long dest = (a3 >> 8) & 7;
	const unsigned char *s = (const unsigned char *)a2;

	ffe_w58_calls++;
	ffe_w58_n[dest]++;
	if (ffe_w58_seen[dest] || a2 <= 0x1000)
		return;
	/* WHAT [a1+0x14c] ACTUALLY POINTS AT, and its +0x9c word -- measured, not
	 * derived. The previous attempt reached this field by arithmetic on label
	 * sizes (DATA_009127 is 130 bytes, DATA_009128 is 26, 130+26 = 0x9c) and
	 * concluded it was DATA_009129. That was WRONG: FUNC_001683 stores
	 * [esi+0x14c] where esi is a LOCAL FRAME BUFFER (lea esi,[ebp+0xfffffea4],
	 * ffe.asm:102231), so the record is not the static view object at all.
	 * Print the pointer, the word, and the bytes around it, on both machines.
	 *
	 * o1 for destination 6 IS this word (mode 0x80, index 0) and it measures 1
	 * on the reference against 10 here, stable across a 4x clock change -- so
	 * whatever these bytes are, they are the x10 itself. */
	if (a1 > 0x1000 && !(a1 & 1)) {
		unsigned long p = *(const unsigned long *)(a1 + 0x14c);
		int q;

		ffe_w58_a1 = a1;
		ffe_w58_p14c = p;
		if (p > 0x1000 && !(p & 1)) {
			ffe_w58_w9c = *(const unsigned short *)(p + 0x9c);
			for (q = 0; q < 12; q++)
				ffe_w58_bytes[q] = ((const unsigned char *)(p + 0x9c))[q];
			/* FIRST and LAST, with the CLOCK at each -- not one-shot. A single
			 * sample cannot tell a wrong value from a value read at a
			 * different moment, and the whole question here is whether this
			 * field is a clock sample. If it SWEEPS across the reference's
			 * value while opcode 21 still never fires, timing is dead. */
			{
				extern char clock_arr3[] __asm__("DATA_008804");
				unsigned long c = *(const unsigned long *)clock_arr3;

				if (!ffe_w58_ptrseen) {
					ffe_w58_w9c_first = ffe_w58_w9c;
					ffe_w58_clk_first = (long)(c & 0x7fffffffL);
				}
				ffe_w58_w9c_last = ffe_w58_w9c;
				ffe_w58_clk_last = (long)(c & 0x7fffffffL);
				if (ffe_w58_w9c < ffe_w58_w9c_min || !ffe_w58_ptrseen)
					ffe_w58_w9c_min = ffe_w58_w9c;
				if (ffe_w58_w9c > ffe_w58_w9c_max || !ffe_w58_ptrseen)
					ffe_w58_w9c_max = ffe_w58_w9c;
			}
			ffe_w58_ptrseen = 1;
		}
	}
	ffe_w58_sel[dest] = (a3 >> 4) & 0xf;
	ffe_w58_b[dest] = (long)s[0] | ((long)s[1] << 8);
	ffe_w58_o1[dest] = FfeDec1473 (a1, s[0]);
	ffe_w58_o2[dest] = FfeDec1473 (a1, s[1]);
	ffe_w58_seen[dest] = 1;
}

void FfeCond1473 (long num) __asm__("_FfeCond1473");
void FfeCond1473 (long num)
{
	unsigned long *caller = (unsigned long *)((char *)&num + 4 + 60);
	unsigned long a1 = caller[1];
	long arg = (long)caller[2];
	int m = (int)((arg >> 6) & 3);
	long idx = arg & 0x3f;
	long v = FfeDec1473 (a1, arg);

	ffe_cond_calls++;
	ffe_cond_mode[m]++;
	FfeCondIdx (m, idx, v);
	if (!ffe_cond_seen[m]) {
		ffe_cond_arg[m] = arg;
		ffe_cond_val[m] = v;
		ffe_cond_seen[m] = 1;
	}
}

/* PER-INDEX, because one "first call" sample per mode cannot separate WRONG
 * DATA from DIFFERENT STATE -- the two runs reach their first call at different
 * moments. Recording the first value seen for each INDEX makes the comparison
 * field-by-field: if index 6 of mode 0xc0 reads 128 on the reference and 1408
 * here across the whole run, that is the field, not the moment. */
long ffe_cx_val[64], ffe_cx_seen[64], ffe_cx_n[64];	/* mode 0xc0 */
long ffe_cw_val[64], ffe_cw_seen[64], ffe_cw_n[64];	/* mode 0x80 */

void FfeCondIdx (long mode, long idx, long v)
{
	if (idx < 0 || idx > 63)
		return;
	if (mode == 3) {
		ffe_cx_n[idx]++;
		if (!ffe_cx_seen[idx]) { ffe_cx_val[idx] = v; ffe_cx_seen[idx] = 1; }
	} else if (mode == 2) {
		ffe_cw_n[idx]++;
		if (!ffe_cw_seen[idx]) { ffe_cw_val[idx] = v; ffe_cw_seen[idx] = 1; }
	}
}

void FfeOpTrace (long p) __asm__("_FfeOpTrace");
/* THE DISPATCH WALK, as a RING IN MEMORY (rewritten 2026-08-05).
 *
 * This used to fopen/fprintf/fflush/fclose on EVERY dispatch. The interpreter
 * loop runs ~5800 times per 32-present window, so that probe slowed the
 * emulated 68020 enough that the run never reached the state it was built to
 * observe, and the log ended on its own line cap. Same shape as the other
 * probes here now: no I/O in the hot path, a ring, printed from dump_clock.
 *
 * WHAT IT IS FOR. Exactly two opcodes are dispatched by the reference and never
 * by us -- 21 ("Nested execution block start/end") and 1 -- while the models,
 * the node and the stream bytes are all measured identical. The stream is
 * walked by the handler's RETURN VALUE: the dispatcher does
 *     movzx edx,word [eax] / add eax,2 / push / call [edx*4+DATA_007816]
 * and then loops back to `movzx edx,word [eax]' with eax = what the handler
 * RETURNED (ffe.asm:111195-111206). So consecutive ring entries are exactly
 * (in-pointer, out-pointer) for whatever handler ran between them, and a skip
 * opcode that returns the wrong pointer shows up as a jump to a position the
 * reference never visits.
 *
 * The word is read LITTLE-endian here because the game's own fetch is a
 * registered swap site -- a plain C `short' read would be big-endian and would
 * report a different opcode than the one actually dispatched. */
#define OPT_RING 96
unsigned long ffe_opt_p[OPT_RING];
unsigned short ffe_opt_w[OPT_RING];
long ffe_opt_pos;

/* THE HEAD OF THE WALK, kept SEPARATELY from the ring (2026-08-06).
 *
 * The ring above answers "what was happening when it died". It cannot answer
 * "where did the two machines FIRST disagree", which is the question the
 * debugging loop actually demands -- the earliest divergence, not the most
 * interesting one. A ring holds the LAST 96 entries and the first divergence is
 * thousands of dispatches earlier, so it is structurally the wrong instrument.
 *
 * This keeps the FIRST OPHEAD_MAX dispatches instead, and records the DELTA to
 * the previous stream position rather than the position itself: the two
 * machines load the bytecode at different addresses, so absolute pointers can
 * never be compared and deltas always can. `total' is printed beside it so a
 * truncated head cannot be mistaken for a short walk. */
/* The delta is a LONG and the WORD is kept whole, both learned the hard way an
 * hour after this was first written. Clamping the delta to a short hid three
 * model-to-model jumps behind +-32767 at exactly the indices where the walk
 * might already have drifted, and keeping only `word & 0x1f' threw away the
 * half of the word that turned out to differ: the first divergence is a skip
 * distance, i.e. `(s16)word >> 5', which lives entirely in the bits the opcode
 * mask discards. Record the whole quantity; mask when READING, not when
 * recording. */
#define OPHEAD_MAX 400
unsigned char ffe_ophead_op[OPHEAD_MAX];
unsigned short ffe_ophead_w[OPHEAD_MAX];
long ffe_ophead_d[OPHEAD_MAX];
long ffe_ophead_n;
static unsigned long ffe_ophead_prev;

void FfeOpTrace (long p)
{
	const unsigned char *b = (const unsigned char *)p;
	int s = (int)(ffe_opt_pos & (OPT_RING - 1));
	unsigned short w = (unsigned short)(b[0] | (b[1] << 8));

	ffe_opt_p[s] = (unsigned long)p;
	ffe_opt_w[s] = w;

	if (ffe_ophead_n < OPHEAD_MAX) {
		ffe_ophead_op[ffe_ophead_n] = (unsigned char)(w & 0x1f);
		ffe_ophead_w[ffe_ophead_n] = w;
		ffe_ophead_d[ffe_ophead_n] =
			ffe_opt_pos ? (long)((unsigned long)p - ffe_ophead_prev) : 0;
		ffe_ophead_n++;
	}
	ffe_ophead_prev = (unsigned long)p;
	ffe_opt_pos++;
}

/* Printed oldest-first, with the opcode and the delta to the PREVIOUS entry --
 * the delta IS the previous handler's advance, which is the quantity the skip
 * hypothesis is about. */
void FfeOptDump (FILE *f) __asm__("_FfeOptDump");
void FfeOptDump (FILE *f)
{
	long first = ffe_opt_pos > OPT_RING ? ffe_opt_pos - OPT_RING : 0;
	long k;

	/* The HEAD first: it is what gets diffed against the reference's PCOPSEQ,
	 * and it must be one line per array so a textual diff lines up. Opcodes and
	 * deltas separately rather than interleaved, because the opcode sequence
	 * usually diverges first and reading it alone is enough to find where. */
	{
		extern unsigned char ffe_ophead_op[];
		extern unsigned short ffe_ophead_w[];
		extern long ffe_ophead_d[];
		extern long ffe_ophead_n;
		long q;

		fprintf (f, "OPSEQ n=%ld total=%ld op=", ffe_ophead_n, ffe_opt_pos);
		for (q = 0; q < ffe_ophead_n; q++)
			fprintf (f, "%d,", (int)ffe_ophead_op[q]);
		fprintf (f, "\nOPSEQD d=");
		for (q = 0; q < ffe_ophead_n; q++)
			fprintf (f, "%ld,", ffe_ophead_d[q]);
		fprintf (f, "\nOPSEQW w=");
		for (q = 0; q < ffe_ophead_n; q++)
			fprintf (f, "%04x,", (unsigned)ffe_ophead_w[q]);
		fprintf (f, "\n");
	}
	{
		/* WHICH MODEL each FUNC_001772 entry interprets, in order -- the line
		 * that names the glyph the two machines disagree about at dispatch 22.
		 * `total' is printed even when the sequence is empty, because a zero
		 * there means the CODE1772 payload is not injected and must never be
		 * read as "the interpreter never ran". */
		extern long ffe_modseq[], ffe_modseq_n, ffe_modseq_total;
		long q;

		{
			extern long ffe_modrel[], ffe_modrel_n;
			long z;
			fprintf (f, "MODREL n=%ld set=", ffe_modrel_n);
			for (z = 0; z < ffe_modrel_n; z++) fprintf (f, "%ld,", ffe_modrel[z]);
			fprintf (f, "\n");
		}
		{
			extern long ffe_g45_a1[],ffe_g45_a2[],ffe_g45_a3[],ffe_g45_pool[],ffe_g45_esi[],ffe_g45_n,ffe_g45_total;
			long z;
			fprintf (f, "GATE1745 n=%ld total=%ld\n", ffe_g45_n, ffe_g45_total);
			for (z = 0; z < ffe_g45_n; z++)
				fprintf (f, "  a1=%ld a2=%ld a3=%ld pool=%ld esi=%ld cull=%d\n",
				         ffe_g45_a1[z], ffe_g45_a2[z], ffe_g45_a3[z], ffe_g45_pool[z],
				         ffe_g45_esi[z], (int)(ffe_g45_esi[z] > 0x8000));
		}
		fprintf (f, "MODSEQ n=%ld total=%ld m=", ffe_modseq_n,
		         ffe_modseq_total);
		for (q = 0; q < ffe_modseq_n; q++)
			fprintf (f, "%ld,", ffe_modseq[q]);
		fprintf (f, "\n");
	}
	{
		/* The string the text opcode asked for, and what came back. `len' of -1
		 * means the next call has not happened yet, so the output could not be
		 * read back -- distinct from 0, which means an EMPTY string and is the
		 * result this is hunting. */
		extern long ffe_s44_id[], ffe_s44_len[], ffe_s44_n, ffe_s44_total;
		extern unsigned char ffe_s44_txt[][12];
		long q;
		int k;

	{
		/* The glyph branch's shift count. `clmax' is over ALL calls, not just
		 * the recorded ones -- a count over 31 anywhere on this path is the
		 * whole question, and a 12-entry sample could miss it. */
		extern long ffe_s86_cl[], ffe_s86_in[], ffe_s86_out[];
		extern long ffe_s86_n, ffe_s86_total, ffe_s86_clmax;
		long q;

		{
			/* idx / ctx position / local offset, per FUNC_001470 call. The
			 * first of the three to differ from the reference's is the fault;
			 * `off=?' means the read-back has not happened yet (there was no
			 * next call), which is distinct from a genuine zero. */
			extern long ffe_s70_idx[], ffe_s70_cx[], ffe_s70_cy[], ffe_s70_cz[];
			extern long ffe_s70_ox[], ffe_s70_oy[], ffe_s70_oz[];
			extern long ffe_s70_done[], ffe_s70_n, ffe_s70_total;
			long k;

			fprintf (f, "NODE1470 n=%ld total=%ld", ffe_s70_n, ffe_s70_total);
			for (k = 0; k < ffe_s70_n; k++) {
				fprintf (f, "  [i=%ld ctx=(%ld,%ld,%ld) off=", ffe_s70_idx[k],
				         ffe_s70_cx[k], ffe_s70_cy[k], ffe_s70_cz[k]);
				if (ffe_s70_done[k])
					fprintf (f, "(%ld,%ld,%ld)]", ffe_s70_ox[k],
					         ffe_s70_oy[k], ffe_s70_oz[k]);
				else
					fprintf (f, "?]");
			}
			fprintf (f, "\n");
		}
		{
			extern long ffe_s87_sz[], ffe_s87_x[], ffe_s87_y[], ffe_s87_z[];
			extern long ffe_s87_n, ffe_s87_total, ffe_s87_near;
			long k;

			fprintf (f, "CULL1687 n=%ld total=%ld near=%ld", ffe_s87_n,
			         ffe_s87_total, ffe_s87_near);
			for (k = 0; k < ffe_s87_n; k++)
				fprintf (f, "  [sz=%ld pos=(%ld,%ld,%ld) z+sz=%ld]",
				         ffe_s87_sz[k], ffe_s87_x[k], ffe_s87_y[k],
				         ffe_s87_z[k], ffe_s87_z[k] + ffe_s87_sz[k]);
			fprintf (f, "\n");
		}
		fprintf (f, "SHIFT1686 n=%ld total=%ld clmax=%ld", ffe_s86_n,
		         ffe_s86_total, ffe_s86_clmax);
		for (q = 0; q < ffe_s86_n; q++)
			fprintf (f, "  [cl=%ld in=%lx out=%lx]", ffe_s86_cl[q],
			         (unsigned long)ffe_s86_in[q],
			         (unsigned long)ffe_s86_out[q]);
		fprintf (f, "\n");
	}
		fprintf (f, "STR1344 n=%ld total=%ld", ffe_s44_n, ffe_s44_total);
		for (q = 0; q < ffe_s44_n; q++) {
			fprintf (f, "  [id=%ld len=%ld \"", ffe_s44_id[q], ffe_s44_len[q]);
			for (k = 0; k < 11 && ffe_s44_txt[q][k]; k++)
				fprintf (f, "%c", ffe_s44_txt[q][k] >= 32
				         && ffe_s44_txt[q][k] < 127
				         ? ffe_s44_txt[q][k] : '.');
			fprintf (f, "\"]");
		}
		fprintf (f, "\n");
	}
	fprintf (f, "OPTRING total=%ld\n", ffe_opt_pos);
	for (k = first; k < ffe_opt_pos; k++) {
		int s = (int)(k & (OPT_RING - 1));
		long d = (k > first)
		    ? (long)(ffe_opt_p[s] - ffe_opt_p[(k - 1) & (OPT_RING - 1)]) : 0;

		fprintf (f, "OPT %ld p=%08lx w=%04x op=%d delta=%ld\n",
		         k, ffe_opt_p[s], (unsigned)ffe_opt_w[s],
		         (int)(ffe_opt_w[s] & 0x1f), d);
	}
}

/* WHERE INSIDE OPCODE 4 THE MACHINE DIES -- a bisect by progress marker.
 *
 * The opcode trace above localised the death to one line and reproducibly over
 * three runs: the third-level dispatcher fetches `w=0x0004', dispatches to
 * FUNC_001711, and nothing is ever written again. Everything cheap around that
 * is eliminated BY MEASUREMENT and must not be re-hunted -- the arena allocator
 * (ARENA max=3428 neg=0 against a 32768 limit), the node pool (POOL16=0),
 * runaway recursion (FUNC_001750 ran 31 times), the shift-count class on this
 * path (SHIFT v=15, never outside 0..31), both stream word operands of opcode 4
 * (ffe.asm:104883 and 104869, both in le_read_sites.tsv, both emitting
 * `rol.w #8'), and FUNC_001712's argument slots.
 *
 * FUNC_001711 (ffe.asm:104862-104892) is 27 instructions that unpack six fields
 * and call FUNC_001712, so the body is FUNC_001712 -- 583 lines of ffe.asm with
 * 21 calls out of it. A marker before and after every one of those calls turns
 * "somewhere in opcode 4" into a single name: the LAST `MARK1712' line in the
 * log is either an odd site, meaning the machine died INSIDE that call, or an
 * even one, meaning it died in the straight-line code after it.
 *
 * The numbering is fixed in tools\translator\inject_count.py's MARK1712_SITES
 * and is deliberately parity-coded: 1 and 2 are FUNC_001711's entry and its
 * single exit (so a death in the wrapper is distinguishable from one in the
 * body), 10 is FUNC_001712's entry, and from 11 up the ODD site is before call
 * k and the EVEN site right after it, k in source order.
 *
 * fopen/fclose and fflush per line, exactly as FfeOpTrace does and for the same
 * reason: a held handle would lock PROGDIR:ffe_enter.log for the whole render
 * and silently stop every other probe in this file from logging, and a buffered
 * line is no evidence at all when the box resets mid-frame.
 *
 * Capped, and it SAYS SO once when it stops -- a trace that silently ends reads
 * exactly like a trace that reached the crash. */
/* Raised from 4000 after the first bisection run: 4000 was reached BEFORE the
 * crash, so the log ended on the cap rather than on the death and the last
 * markers were s=37/38 -- the DPal32to16 colour-quantiser pair, a hot loop that
 * runs thousands of times correctly. A trace that stops on its own limit says
 * nothing about where the machine died. 60000 covers ~1400 entries to
 * FUNC_001712 at 43 markers each; the `capped' line still distinguishes "ran
 * out of budget" from "reached the crash", which is the whole point of having
 * it. */
/* THE MARKER RING. The first three cuts of this probe all wrote to the log per
 * line, and every one of them measured itself instead of the game: sites 37/38
 * sit in a loop that runs about 1040 times per entry to FUNC_001712, so an
 * fopen/fflush/fclose each time slowed the emulated 68020 until the run never
 * reached the crash at all. Sampling hid the tail; arming full logging after N
 * entries never triggered, because the instrumented run could not reach N.
 *
 * So: NO I/O in the hot path. Sites go into a ring in memory, and dump_clock()
 * prints it every 32 presents -- the shape FfeVtx1478, FfeTick1470 and
 * FfeModel1538 already use here. Zero perturbation, and because the machine
 * hard-resets rather than exiting, the last ring printed is at most 32 frames
 * before the death, which is what names the site. */
/* THE TRIANGLE-DETERMINANT RING (2026-08-05, after the DATA_007811 fix).
 *
 * The run now survives to presents=769 and dies on a clean TRAP 5 -- vector
 * offset 0x14, divide by zero -- at ffe.asm:130812 inside FUNC_001876:
 *
 *     cdq / idiv dword [ebp-0x74]   ->   divs.l (-116,a5),d2:d0
 *
 * [ebp-0x74] and [ebp-0x7c] are two components of a TRIANGLE DETERMINANT built
 * at ffe.asm:130750-130777 from the (x,y) of three points, and the game's own
 * guard against a zero divisor (130803-130806) is skipped whenever ebx >= 15.
 * The three points are read through [ebp-0x48]/[ebp-0x4c]/[ebp-0x50] and each
 * coordinate is shifted right by [ebp-0x6c] = FindMSB(OR of all nine |coords|)
 * - 13, so a single oversized coordinate normalises the others to zero and the
 * triangle collapses.
 *
 * The probe sits on JUMP_008027, the label immediately before the divide, so
 * every value it records is exactly what the divide is about to use. A RING,
 * printed by FfeCrashDie -- which means the LAST entry IS the death record and
 * the earlier ones are the positive control in the same run. If they are all
 * zero, the probe is wrong, not the game.
 *
 * The offsets are READ OFF THE EMITTED 68k in build\xlat-full\FUNC_001876.s,
 * not assumed: frame.py relocates locals, and here it happens to map them
 * identically. None of the vertex reads is registered in le_read_sites.tsv, so
 * the game reads those fields as plain 68k dwords and a native C read
 * reproduces its own access exactly. */
#define TRI_RING 8
long ffe_tri_ring[TRI_RING][17];
long ffe_tri_pos;               /* total records; & (RING-1) is the slot */
long ffe_tri_null;              /* vertex pointers that were NULL */

void FfeTri1876 (long *fp) __asm__("_FfeTri1876");
void FfeTri1876 (long *fp)
{
	char *b = (char *)fp;                   /* the game's a5 */
	long *r = ffe_tri_ring[ffe_tri_pos & (TRI_RING - 1)];
	long i;

	r[0] = *(long *)(b - 108);              /* [ebp-0x6c]  the shift */
	r[1] = *(long *)(b - 116);              /* [ebp-0x74]  THE DIVISOR */
	r[2] = *(long *)(b - 124);              /* [ebp-0x7c] */
	for (i = 0; i < 3; i++) {
		long *v = *(long **)(b - 72 - 4 * i);   /* -0x48/-0x4c/-0x50 */

		/* THE POINTERS THEMSELVES, not only what they point at. Three equal
		 * CONTENTS can mean two different faults and the numbers alone cannot
		 * tell them apart: equal POINTERS mean the selection above chose the
		 * same record three times, different pointers with equal contents mean
		 * the records themselves were filled wrong. ffe.asm:130393-130400
		 * fills the three from three different sources, so they should differ.
		 */
		r[12 + i] = (long)v;
		if (!v) {
			ffe_tri_null++;
			r[3 + 3 * i] = r[4 + 3 * i] = r[5 + 3 * i] = 0;
			continue;
		}
		r[3 + 3 * i] = v[1];            /* +0x4  x */
		r[4 + 3 * i] = v[2];            /* +0x8  y */
		r[5 + 3 * i] = v[3];            /* +0xc  z */
	}
	/* The two records the second and third pointers are DERIVED from, so a
	 * collapse can be traced one level up without another build. */
	r[15] = *(long *)(b - 84);              /* [ebp-0x54] */
	r[16] = *(long *)(b - 92);              /* [ebp-0x5c] */
	ffe_tri_pos++;
}

#define MARK1712_RING 64
long ffe_mark_ring[MARK1712_RING];
long ffe_mark_pos;              /* total pushes; & (RING-1) is the slot */
long ffe_mark_arg7, ffe_mark_base, ffe_mark_entries, ffe_mark_hot;
long ffe_mark_ret1778, ffe_mark_ret_low;

void FfeMark1712 (long site) __asm__("_FfeMark1712");
void FfeMark1712 (long site)
{
	/* THE HOT SITES ARE COUNTED, NOT RECORDED. Sites 37/38 and 47/48 are the
	 * DPal32to16 calls, inside a loop that runs ~1040 times per entry: pushing
	 * them wrapped the 64-slot ring every 32 pushes, so flush-on-wrap produced
	 * 10065 writes and the perturbation came straight back (1 boot, no reset).
	 * Counting them keeps the ring STRUCTURAL -- about 20 pushes per entry, so
	 * it wraps a handful of times in a whole run -- while `hot' still says how
	 * far the loop got. A death inside that loop then shows as a ring ending on
	 * site 36 with a large hot count, which is just as nameable. */
	if (site == 37 || site == 38 || site == 47 || site == 48) {
		ffe_mark_hot++;
		return;
	}

	/* SITE 36 IS THE MARKER RIGHT AFTER `call FUNC_001778' RETURNS
	 * (ffe.asm:105247), and the ring says it is the LAST thing that happens
	 * before the machine resets. The five instructions after it write through
	 * that return value with no null check -- including `mov dword
	 * [eax],FUNC_001805', a FUNCTION ADDRESS -- so a zero or wild pointer lands
	 * on the 68k exception vectors at 0/4/8/0xc/0x10 and the machine resets
	 * with no handler, which is exactly the symptom.
	 *
	 * The injector's payload does `movem.l d0-d7/a0-a6,-(sp)' and then pushes
	 * the site number, so the SAVED d0 -- which is eax, the return value -- sits
	 * immediately past this probe's own parameter. Reading it there is the
	 * game's own value, untouched. Recorded, not printed: printing here would
	 * put I/O back in the path this whole rig exists to keep quiet. */
	if (site == 36) {
		unsigned long *saved = (unsigned long *)((char *)&site + 4);
		ffe_mark_ret1778 = (long)saved[0];
		if (ffe_mark_ret1778 >= 0 && ffe_mark_ret1778 < 0x1000)
			ffe_mark_ret_low++;
	}

	ffe_mark_ring[ffe_mark_pos & (MARK1712_RING - 1)] = site;
	ffe_mark_pos++;

	/* FLUSH ON WRAP, not on the frame clock. dump_clock runs every 32 presents
	 * and the machine dies BETWEEN two of them, so the ring as it stood at the
	 * death was never printed: every MARKRING line dump_clock produced was
	 * balanced, ending on a clean exit, which says nothing. Writing the ring
	 * each time it wraps makes the last printed line at most 64 markers stale
	 * -- about one and a half entries to FUNC_001712 -- and costs one write per
	 * 64 pushes, roughly 130 for a whole run. That is the difference between a
	 * trace that ends where the game died and one that ends where the timer
	 * happened to tick. */
	/* Flush every 16 pushes, not every 64. The ring still holds 64, so each
	 * line shows the full recent context; the granularity is what bounds how
	 * STALE the last line can be when the machine resets between flushes. At
	 * 64 the last printed line was 63 pushes -- three whole entries -- behind
	 * the death. At 16 it is under one entry, and the cost is still only a
	 * handful of writes per run because the hot sites are counted, not
	 * pushed. */
	if (1) {		/* every push: hot sites are counted, not pushed, so this is ~600 writes */
		FILE *g = ffe_logopen ("PROGDIR:ffe_enter.log", "a");
		if (g != NULL) {
			long i;
			fprintf (g, "MRING p=%ld e=%ld hot=%ld ret1778=%08lx low=%ld",
			         ffe_mark_pos, ffe_mark_entries, ffe_mark_hot,
			         (unsigned long)ffe_mark_ret1778, ffe_mark_ret_low);
			for (i = ffe_mark_pos - MARK1712_RING; i < ffe_mark_pos; i++)
				fprintf (g, " %ld", ffe_mark_ring[i & (MARK1712_RING - 1)]);
			fprintf (g, "\n");
			fflush (g);
			fclose (g);
		}
	}

	/* Site 10 is FUNC_001712's bare label. Capture the two numbers that decide
	 * the entry window (ffe.asm:104907-104909, mov al,[eax+edx*8+0x3]) without
	 * printing them: arg7 off the ORIGINAL caller's frame, past this probe's
	 * own parameter (4) and the injector's movem (60), and the base pointer.
	 * Both reads are the game's own -- DATA_009201 is a dword the translated
	 * code also loads natively, and arg7 is already the caller's computed
	 * value, swap and sign extension included. */
	if (site == 10) {
		extern unsigned long DATA_009201 __asm__("DATA_009201");
		unsigned long *caller = (unsigned long *)((char *)&site + 4 + 60);
		ffe_mark_arg7 = (long)caller[7];
		ffe_mark_base = (long)DATA_009201;
		ffe_mark_entries++;
	}
}

/* WHO CALLS THE VERTEX RESOLVER, AND HOW OFTEN -- the mirror of PC1470T.
 *
 * The reference resolves node 6 189 times before the first scene render and we
 * resolve it once, by a different route. FUNC_001470 has TWELVE call sites in
 * FIVE functions -- FUNC_001696 (six), FUNC_001697, FUNC_001721, FUNC_001747
 * (three) and FUNC_001874 -- so a histogram of RETURN ADDRESSES names the loop
 * that runs there and not here, with one probe instead of five. The addresses
 * are raw: resolve them against buildmiga-outfe_game.symmap, whose link
 * addresses shift by a constant the ANCHOR line in ffe_dump.log pins down.
 *
 * Dumped from FfeScene1876, at the consumer, so the count is taken at the same
 * point in the frame as the reference's.
 */
#define V1470_SLOTS 8
static struct { unsigned long ret; long n; } g_v1470[V1470_SLOTS];
static long g_v1470_total;

void FfeTick1470 (long num) __asm__("_FfeTick1470");
void FfeTick1470 (long num)
{
	unsigned long *caller = (unsigned long *)((char *)&num + 4 + 60);
	unsigned long ret = caller[0];
	int i;

	g_v1470_total++;
	for (i = 0; i < V1470_SLOTS; i++) {
		if (g_v1470[i].ret == ret) { g_v1470[i].n++; return; }
		if (g_v1470[i].ret == 0) { g_v1470[i].ret = ret; g_v1470[i].n = 1; return; }
	}
}

static void ffe_v1470_dump (FILE *f)
{
	int i;

	fprintf (f, "TICK1470T total=%ld\n", g_v1470_total);
	for (i = 0; i < V1470_SLOTS && g_v1470[i].ret; i++)
		fprintf (f, "TICK1470R ret=%lx n=%ld\n", g_v1470[i].ret, g_v1470[i].n);
}

void FfeVtx1478 (long num) __asm__("_FfeVtx1478");
void FfeVtx1478 (long num)
{
	unsigned long *caller = (unsigned long *)((char *)&num + 4 + 60);
	unsigned long ctx = caller[1];
	long index = (long)caller[2], flag = (long)caller[3];

	if (index != 6) return;
	if (!(ctx > 0x1000 && !(ctx & 1))) return;
	{
		const unsigned char *vt =
			*(const unsigned char *const *)(ctx + 0x150);
		const unsigned char *d;
		int k = (int)(g_v1478_n % V1478_RING), q;

		if ((unsigned long)vt <= 0x1000) return;
		d = vt + (index >> 1) * 6;
		g_v1478[k].ctx = ctx;
		g_v1478[k].vtab = (unsigned long)vt;
		g_v1478[k].flag = flag;
		g_v1478[k].seq = ++g_v1478_n;
		for (q = 0; q < 6; q++) g_v1478[k].desc[q] = d[q];
	}
}

static void ffe_v1478_dump (FILE *f)
{
	int i;

	for (i = 0; i < V1478_RING; i++) {
		int k = (int)((g_v1478_n + i) % V1478_RING);

		if (g_v1478[k].seq == 0) continue;
		fprintf (f, "VTX1478R seq=%ld ctx=%lx vtab=%lx flag=%ld"
		            " desc=%02x,%02x,%02x,%02x,%02x,%02x\n",
		         g_v1478[k].seq, g_v1478[k].ctx, g_v1478[k].vtab,
		         g_v1478[k].flag,
		         g_v1478[k].desc[0], g_v1478[k].desc[1], g_v1478[k].desc[2],
		         g_v1478[k].desc[3], g_v1478[k].desc[4], g_v1478[k].desc[5]);
	}
}

/* THE BAND COUNT BEFORE THE SHIFT -- the mirror of PC1876B in pcprobe_proj.c.
 *
 * FUNC_001877 receives `bands' as FUNC_001876's own arg9, but FUNC_001876
 * rescales that argument slot in place first (ffe.asm:129433-129446):
 *
 *     eax = [arg4 + 0x148] - 8
 *     eax < 0 ? sar [ebp+0x28], -eax : shl [ebp+0x28], eax
 *
 * Measured on both machines, same scenario: object 2 arrives at FUNC_001877
 * with bands=6242304 on the reference and 12484608 for us -- exactly 2x, one
 * bit. arg10 (atmo) is IDENTICAL on both objects and is the control: whatever
 * is wrong reaches arg9 without touching arg10, and the two are pushed by
 * adjacent `movsx eax,word [ebx+0x8]' / `[ebx+0xa]' at the call site
 * (ffe.asm:129335-129339). Printing arg9 RAW beside [arg4+0x148] says whether
 * the shift field differs or the model word does.
 */
void FfeScene1876 (long num) __asm__("_FfeScene1876");
void FfeScene1876 (long num)
{
	unsigned long *caller = (unsigned long *)((char *)&num + 4 + 60);
	unsigned long a4 = caller[4];
	long a9 = (long)caller[9], a10 = (long)caller[10];
	static int n_done;
	FILE *f;

	/* RENDERPATH: this file already carries the SCENE1876 payload, so the
	 * counter is bumped here instead of injecting a second one. Before the
	 * anomaly filter below -- the count must be of ENTRIES, not of dumps. */
	FfeRenderPath (num);
	/* TRIGGER ON THE ANOMALY: two of the three call sites push literal zeros
	 * for a4/a9/a10, so a plain first-N window sees nothing from the one that
	 * matters. */
	if (a9 == 0) return;
	if (n_done >= 8) return;
	n_done++;
	f = ffe_logopen ("PROGDIR:ffe_enter.log", "a");
	if (f == NULL) return;
	/* The FUNC_001478 calls that wrote node 6 most recently, printed at the
	 * CONSUMER so the two machines' logs pair up. */
	ffe_v1478_dump (f);
	ffe_v1470_dump (f);
	ffe_m1538_dump (f, (long)n_done);
	fprintf (f, "SCENE1876 n=%d a9raw=%ld a10=%ld shiftfld=%ld a4=%lx\n",
	         n_done, a9, a10,
	         (a4 > 0x1000 && !(a4 & 1))
	             ? (long)*(const unsigned long *)(a4 + 0x148) : -1L,
	         a4);
	/* THE RAW STREAM BYTES -- to tell an OFFSET error from a BYTE-ORDER one.
	 *
	 * atmo and bands are word reads of the MODEL STREAM at [ebx+0x8] and
	 * [ebx+0xa] (ffe.asm:129335/129338), and arg5 is `ebx' AFTER the
	 * `add ebx,byte +0xc' at 129346 -- so the stream base is arg5 - 0xc.
	 * Both sites ARE registered in le_read_sites.tsv and the emitted 68k does
	 * `move.w (8,a0),d7 / rol.w #8,d7', i.e. it reads the LITTLE-endian word.
	 * A plain C `*(short *)' here would be a BIG-endian read and would report
	 * a different number than the game computes -- the mistake that produced
	 * three retracted findings on this project -- so `gw' below reproduces
	 * the game's own access, and the raw bytes are printed beside it as the
	 * ground truth neither reading can distort.
	 *
	 * READ IT THIS WAY, written down before the run:
	 *   the reference's atmo word 0x1999 appears in our bytes at offset 8
	 *      -> we read the right place and the fault is elsewhere;
	 *   it appears at some OTHER offset
	 *      -> OFFSET ERROR, and the log names the delta;
	 *   it does not appear at all
	 *      -> we are on a different stream or a different instance, and the
	 *         model number is not the whole story. */
	{
		const unsigned char *s = (const unsigned char *)(caller[5] - 0xc);
		int q;

		if ((unsigned long)s > 0x1000) {
			fprintf (f, "STREAM1876 n=%d base=%lx bytes=",
			         n_done, (unsigned long)s);
			for (q = 0; q < 16; q++)
				fprintf (f, "%02x,", s[q]);
			fprintf (f, "  gw@0=%04x gw@6=%04x gw@8=%04x gw@a=%04x\n",
			         (unsigned)(s[0] | (s[1] << 8)),
			         (unsigned)(s[6] | (s[7] << 8)),
			         (unsigned)(s[8] | (s[9] << 8)),
			         (unsigned)(s[10] | (s[11] << 8)));
		} else {
			fprintf (f, "STREAM1876 n=%d base=%lx -- NIEPRAWIDLOWY, nie czytam\n",
			         n_done, (unsigned long)s);
		}
		/* WHICH NODE WAS SELECTED, and BY WHICH ROUTE -- the mirror of the
		 * reference's PCNODE, and the discriminator the byte dump left open.
		 *
		 * FUNC_001874 picks the object with a SIGNED BYTE index out of the
		 * stream (ffe.asm:129208 `movsx eax,byte [ebx+0x2]'), looks up
		 * arena[idx*36 + 0x14] where arena = [DATA_009200], and compares it
		 * with [edi+0xf0] (129212). MATCH -> esi = arena + idx*36 straight
		 * away (129214-129218). MISMATCH -> esi = FUNC_001470(edi, idx)
		 * instead (129220-129226). arg6 is `esi+4', so:
		 *
		 *   a6 - arena == idx*36 + 4   -> the compare MATCHED, node taken
		 *                                 from the arena, exactly as the
		 *                                 reference does (its PCNODE gives
		 *                                 a6-arena = 0xDC = 6*36+4, idx=6);
		 *   anything else              -> the compare FAILED and we went
		 *                                 through FUNC_001470, which is a
		 *                                 DIFFERENT ROUTE from the reference
		 *                                 and names [edi+0xf0] or the arena
		 *                                 contents as the divergence.
		 *
		 * DATA_009200 is read HERE, inside the render. Reading it from
		 * dump_clock gives 0 because the cursor is reset between frames -- a
		 * value measured at the wrong moment, which is not a measurement. */
		{
			extern char *node_base __asm__("DATA_009200");
			unsigned long arena = (unsigned long)node_base;
			unsigned long a6 = caller[6];
			long idx = ((unsigned long)s > 0x1000) ? (long)(signed char)s[2] : -128;
			long idx2 = ((unsigned long)s > 0x1000) ? (long)(signed char)s[3] : -128;

			fprintf (f, "NODE1876 n=%d a6=%lx arena=%lx idx=%ld idx2=%ld "
			            "a6-arena=%ld oczek=%ld  %s\n",
			         n_done, a6, arena, idx, idx2,
			         (long)(a6 - arena), idx * 36 + 4,
			         (arena && (long)(a6 - arena) == idx * 36 + 4)
			             ? "Z ARENY, jak referencja"
			             : "NIE z areny -> poszlo przez FUNC_001470");
		}
		/* And the shiftfld dword as BYTES, for the same reason: the game reads
		 * it raw (ffe.asm:129435 is NOT registered), so if the underlying data
		 * were in x86 order this is where it would show as a byte pattern
		 * rather than as a plausible small number. */
		if (a4 > 0x1000 && !(a4 & 1)) {
			const unsigned char *p = (const unsigned char *)(a4 + 0x148);

			fprintf (f, "SHIFTFLD n=%d bytes=%02x,%02x,%02x,%02x\n",
			         n_done, p[0], p[1], p[2], p[3]);
		}
	}
	/* WHICH NODE, AND WHICH INDEX -- the mirror of PCNODE. FUNC_001874 never
	 * computes the position: it takes node [ebx+0x2] out of the arena at
	 * [DATA_009200] with a 36-byte stride (ffe.asm:129206-129216), or takes
	 * FUNC_001470's answer when the node is unresolved, and hands esi+4 on as
	 * arg6 -- which becomes FUNC_001877's `centre'. arg6 IS that node+4.
	 * Printing the derived index beside the node separates the last two
	 * possibilities: the same index with different contents means the vertex
	 * TRANSFORM that filled the arena is wrong, a different index means the
	 * model bytecode byte that selects the node is. */
	{
		extern unsigned char DATA_009200[] __asm__("DATA_009200");
		unsigned long a6 = caller[6];
		unsigned long arena = *(const unsigned long *)DATA_009200;
		const unsigned long *nd = (const unsigned long *)a6;
		long idx = (arena && a6 > arena) ? (long)((a6 - 4 - arena) / 36) : -1;

		fprintf (f, "NODE1876 n=%d a6=%lx arena=%lx idx=%ld"
		            " node=(%ld,%ld,%ld)\n",
		         n_done, a6, arena, idx,
		         (a6 > 0x1000 && !(a6 & 1)) ? (long)nd[0] : 0L,
		         (a6 > 0x1000 && !(a6 & 1)) ? (long)nd[1] : 0L,
		         (a6 > 0x1000 && !(a6 & 1)) ? (long)nd[2] : 0L);
	}
	fclose (f);
}

/* THE BODY GENERATOR, call by call -- the mirror of PC881 in pcprobe_proj.c.
 *
 * FUNC_000881 takes eleven cdecl dwords; arg2 is the pointer to the running
 * body COUNT. This is an ENTRY probe, so it can only print the count BEFORE the
 * call -- but that is enough to line the two logs up, because call n's "before"
 * is call n-1's "after". The return address names which of the six call sites
 * (63617, 63639, 63784, 63812, 63924, 66942) made it, which is the recursion
 * shape; the seeds come along because the whole generator is a seeded PRNG and
 * a diverging seed would explain body 0 agreeing and body 1 not.
 *
 * The return address is printed RAW: resolve it against
 * build\amiga-out\ffe_game.symmap rather than guessing.
 */
void FfeGen881 (long num) __asm__("_FfeGen881");
void FfeGen881 (long num)
{
	unsigned long *caller = (unsigned long *)((char *)&num + 4 + 60);
	unsigned long ret = caller[0];
	unsigned long a1 = caller[1], a2 = caller[2], a3 = caller[3];
	unsigned long a4 = caller[4], a5 = caller[5], a6 = caller[6];
	static int n_done;
	FILE *f;

	if (n_done >= 40) return;
	n_done++;
	f = ffe_logopen ("PROGDIR:ffe_enter.log", "a");
	if (f == NULL) return;
	{
		extern unsigned char DATA_008843[] __asm__("DATA_008843");
		extern unsigned char DATA_008844[] __asm__("DATA_008844");

		fprintf (f, "GEN881 n=%2d ret=%lx a1=%lx a3=%lx a4=%lx a5=%lx a6=%lx"
		            "  cnt=%ld  s0=%lx s1=%lx\n",
		         n_done, ret, a1, a3, a4, a5, a6,
		         (a2 > 0x1000 && !(a2 & 1))
		             ? (long)*(const unsigned long *)a2 : -1L,
		         *(const unsigned long *)DATA_008843,
		         *(const unsigned long *)DATA_008844);
	}
	fclose (f);
}

/* THE FINISHED BODY LIST, walked exactly as the game walks it.
 *
 * FUNC_000880 is the consumer: `push [DATA_009133] / push DATA_008798 / push
 * [DATA_008804] / push [DATA_008807] / call FUNC_000880' (ffe.asm:67426-67433),
 * so arg2 is the list FUNC_000878 has just filled. Its own loop is stride 0x44,
 * terminated by a zero dword (64875-64878), and the type it gates on lives at
 * +0xa: `cmp word [ebx+0xa],byte +0x23 / jnz' at 64715 is what decides whether
 * the real planet is submitted at all.
 *
 * The instrumented reference prints the same walk as PC878out. On 2026-08-03 it
 * gave 27 records with eight of type 0x23 against our 8 with none, which is the
 * whole difference between a planet on screen and a hardcoded slab at the
 * camera. Printing OUR list in the identical format is what makes the two
 * directly comparable, line against line.
 */
void FfeBodyList (long num) __asm__("_FfeBodyList");
void FfeBodyList (long num)
{
	unsigned long *caller = (unsigned long *)((char *)&num + 4 + 60);
	unsigned long list = caller[2];
	static int n_done;
	FILE *f;
	int k;

	if (n_done >= 4) return;
	if (!(list > 0x1000 && !(list & 1))) return;
	n_done++;
	f = ffe_logopen ("PROGDIR:ffe_enter.log", "a");
	if (f == NULL) return;
	fprintf (f, "BODYLIST%ld list=%lx", num, list);
	for (k = 0; k < 48 && *(const unsigned long *)(list + k * 0x44) != 0; k++)
		fprintf (f, " %04x",
		         (unsigned int)*(const unsigned short *)(list + k * 0x44 + 0xa));
	fprintf (f, "  n=%d\n", k);
	/* Field +0x24 is the body's size/radius word -- FUNC_000883 types the body
	 * by comparing it against a ladder of constants (0x139, 0x152, 0x212,
	 * 0xc8, 0xb4, 0xa0, 0x87 ...), so a wrong value here shows up as a wrong
	 * TYPE without anything else looking out of place. Every access to it in
	 * the whole generator is 16-bit, checked, so it is a genuine word field
	 * and not a sub-word read of something wider. */
	fprintf (f, "BODYR24%ld", num);
	for (k = 0; k < 48 && *(const unsigned long *)(list + k * 0x44) != 0; k++)
		fprintf (f, " %04x",
		         (unsigned int)*(const unsigned short *)(list + k * 0x44 + 0x24));
	fprintf (f, "  n=%d\n", k);
	/* 2026-10-15: one line per body -- parent, type, mass, orbit radius and
	 * the name string at +0x26 (raw bytes, no byte-order question); twin of
	 * PCBODY in build\pcprobe_proj.c */
	for (k = 0; k < 48 && *(const unsigned long *)(list + k * 0x44) != 0; k++) {
		const unsigned char *r = (const unsigned char *)(list + k * 0x44);
		fprintf (f, "BODY%ld k=%d par=%04x typ=%04x mass=%08lx orb=%08lx lat=%04x name=%.20s\n",
		         num, k, *(const unsigned short *)(r + 0x8),
		         *(const unsigned short *)(r + 0xa),
		         *(const unsigned long *)(r + 0x0),
		         *(const unsigned long *)(r + 0xe),
		         *(const unsigned short *)(r + 0x12), (const char *)(r + 0x26));
	}
	fclose (f);
}

/* THE RENDER OBJECT'S POSITION at a nested transform.
 *
 * FUNC_001741 is the model interpreter's 0x9c matrix-transform opcode. The
 * dispatcher calls every handler with THREE arguments (ffe.asm:111195-111202):
 *     push edx   ; the opcode word       -> arg3
 *     push eax   ; the bytecode pointer  -> arg2
 *     push ebx   ; the RENDER OBJECT     -> arg1
 * so arg1 is the object. (An earlier version of this probe read arg2 and
 * printed bytes out of the bytecode stream as if they were coordinates -- the
 * giveaway was an ODD pointer. `lea eax,[edi+0x4]` at 108163 is bytecode+4,
 * not the position triple.)
 * FUNC_001683 builds that object at (0,0,84000), which both machines agree on,
 * yet by the time FUNC_001478 uses it ours reads (0,0,-12) and the reference
 * reads planetary scale. So the change happens on this path, and printing the
 * triple at entry across several calls brackets it.
 */
void FfeRendPos (long num) __asm__("_FfeRendPos");
void FfeRendPos (long num)
{
	unsigned long *caller = (unsigned long *)((char *)&num + 4 + 60);
	unsigned long obj = caller[1];
	static int n_done;
	FILE *f;

	if (n_done >= 40) return;
	if (!(obj > 0x1000 && !(obj & 1))) return;
	n_done++;
	f = ffe_logopen ("PROGDIR:ffe_enter.log", "a");
	if (f == NULL) return;
	fprintf (f, "REND%ld obj=%lx pos=(%ld,%ld,%ld)\n", num, obj,
	         *(const long *)(obj + 0x4),
	         *(const long *)(obj + 0x8),
	         *(const long *)(obj + 0xc));
	fclose (f);
}

void FfeObjPos (long num) __asm__("_FfeObjPos");
void FfeObjPos (long num)
{
	unsigned long *caller = (unsigned long *)((char *)&num + 4 + 60);
	unsigned long src = caller[1];
	static int n_done;
	FILE *f;
	int k;

	/* 400, and one line per object: the question is now the SET of positions,
	 * not one object's bytes. FUNC_001683 contributes a single object per
	 * frame and both sides already agree on it; FUNC_001684 contributes every
	 * other scene object, and the reference produces 325 distinct positions
	 * against our 31. */
	if (n_done >= 400) return;
	if (!(src > 0x1000 && !(src & 1))) return;
	n_done++;

	f = ffe_logopen ("PROGDIR:ffe_enter.log", "a");
	if (f == NULL) return;
	/* The object starts with the 3x3 matrix -- both builders copy nine dwords
	 * from its base (`rep movsd`, ffe.asm:102238 and 102318) -- so +0x00..0x23
	 * is the matrix and the three 64-bit positions begin at +0x24, 8 apart.
	 * The reads at +0x26/+0x2e/+0x36 therefore STRADDLE the two dwords of each
	 * 64-bit value, taking bytes 2..5 of it. On x86 that yields bits 16..47,
	 * i.e. the integer part of a 48.16 fixed-point coordinate. On the 68k the
	 * same address yields a mixture of the wrong halves, and no displacement
	 * can fix it -- the correct value has to be COMPUTED as
	 *     (lo >> 16) | (hi << 16)
	 * with lo at +0x24+8k and hi at +0x28+8k. Printing the raw dwords, what
	 * the code currently reads, and what the x86 would have produced settles
	 * that in one run instead of by argument. */
	(void)k;
	/* THE RETURN ADDRESS, plus one anchor to make it resolvable.
	 *
	 * FUNC_001682 has nine call sites, all indirect through DATA_007807, and
	 * counters said only FUNC_000960 and FUNC_001775 are live. Naming which of
	 * the two supplies each object needs the return address -- but a raw
	 * runtime address is useless without the load base, and an earlier attempt
	 * to guess the base by constraint-solving against the symmap found no
	 * consistent answer.
	 *
	 * So print the runtime address of a known symbol once. base = that minus
	 * its link address in build\amiga-out\ffe_game.symmap, and every return
	 * address then resolves directly. Taking the address of the BARE label is
	 * what makes this work: `FUNC_001682` is the t-ABI entry the translated
	 * callers jump to, and it is the symbol the map lists.
	 */
	{
		extern void FUNC_001682 (void) __asm__("FUNC_001682");
		static int anchored;

		if (!anchored) {
			/* THE GATE that lets the extra near-camera object in.
			 * ffe.asm:77448-77454 (FUNC_001052) calls FUNC_001775 --
			 * which hardcodes a render object at (0,0,512) with model
			 * 0x13b -- only when
			 *     DATA_009019 != 0   AND   DATA_008814 == 0
			 * The reference never calls it; we call it about half the
			 * frames. DATA_009019 is a genuine single byte (`resb 1`,
			 * ffedat.asm:28529) toggled by `not byte [DATA_009019]` in
			 * FUNC_000216_SysMenuProcessAction_Event4, i.e. a system-menu
			 * setting that starts at 0 in BSS. So printing it says
			 * whether something toggled it, or spilled into it, or
			 * whether our BSS simply is not zero. */
			extern unsigned char DATA_009019 __asm__("DATA_009019");
			extern unsigned char DATA_009020 __asm__("DATA_009020");
			extern long DATA_008814 __asm__("DATA_008814");
			/* SPLIT MEASURED, 2026-08-03: on the reference FUNC_001052
			 * IS reached, with 009019=ff but 008814=ffffffff, so the
			 * second half of the gate closes it. Ours reads 0 there.
			 * DATA_008814 is set to -1 only inside FUNC_001877, and two
			 * branches skip that write: DATA_009263 != 0 (ffe.asm:131714)
			 * and FUNC_001878 returning zero (131800-131806). Printing
			 * 009263 tests the first without another build. */
			extern long DATA_009263 __asm__("DATA_009263");
			/* THE SCREEN MODE. FUNC_001010 dispatches on
			 * `movsx eax,byte [DATA_008870]; sar eax,1;
			 *  mov eax,[eax*8+DATA_007256]; call eax`
			 * (ffe.asm:74833-74836), and FUNC_001025 -- the handler that
			 * walks the stellar-body script and eventually draws the real
			 * planet through FUNC_000581 -- is one of the entries. None of
			 * that chain runs for us (measured: 568/569/570/574/575/576,
			 * 579/580/581/582, 243/246/1025 all zero against a live
			 * 1876=224 control), so the mode byte is the thing to compare.
			 * It is a genuine resb 1 read byte-wise everywhere, so this is
			 * NOT an endianness question. */
			extern unsigned char DATA_008870 __asm__("DATA_008870");

			/* FUNC_001877 is NOT stubbed -- it is hand-ported to C in
			 * amiga\probes\ffe_planet.c and excluded from massgen, which
			 * is why inject_count reports it MISSING. These counters say
			 * which of its three exits suppresses the DATA_008814 = -1
			 * write: never called, atmo == 0, DATA_009263 != 0, or
			 * FUNC_001878 returning 0. */
			extern long ffe_p1877_calls, ffe_p1877_noatmo;
			extern long ffe_p1877_shell, ffe_p1877_shade0, ffe_p1877_set;

			anchored = 1;
			fprintf (f, "ANCHOR FUNC_001682=%lx  GATE 009019=%02x "
			         "009020=%02x 008814=%lx 009263=%lx\n",
			         (unsigned long)&FUNC_001682,
			         (unsigned int)DATA_009019,
			         (unsigned int)DATA_009020,
			         (unsigned long)DATA_008814,
			         (unsigned long)DATA_009263);
			fprintf (f, "MODE 008870=%02x\n", (unsigned int)DATA_008870);
			(void)0;
		}
	}
	/* PERIODICALLY, not once. The anchor above fires on the FIRST call, which
	 * is far too early for a per-frame counter to have moved -- reading
	 * `calls=0` there says nothing. Printing every 100th sample covers the
	 * whole run. Same mistake shape as sampling "the first N calls". */
	if ((n_done % 100) == 0) {
		extern long ffe_p1877_calls, ffe_p1877_noatmo;
		extern long ffe_p1877_shell, ffe_p1877_shade0, ffe_p1877_set;
		extern long ffe_p1877_singular, ffe_p1877_far;
		extern long ffe_p1877_near, ffe_p1877_inside;

		fprintf (f, "P1877 n=%d calls=%ld noatmo=%ld sing=%ld far=%ld "
		         "near=%ld inside=%ld shell=%ld shade0=%ld set=%ld\n",
		         n_done, ffe_p1877_calls, ffe_p1877_noatmo,
		         ffe_p1877_singular, ffe_p1877_far, ffe_p1877_near,
		         ffe_p1877_inside, ffe_p1877_shell, ffe_p1877_shade0,
		         ffe_p1877_set);
		{
			extern long ffe_p1877_E[3], ffe_p1877_msb, ffe_p1877_centre[3];
			extern long ffe_p1877_rec[5];
			fprintf (f, "P1877E E=(%ld,%ld,%ld) centre=(%ld,%ld,%ld) msb=%ld\n",
			         ffe_p1877_E[0], ffe_p1877_E[1], ffe_p1877_E[2],
			         ffe_p1877_centre[0], ffe_p1877_centre[1],
			         ffe_p1877_centre[2], ffe_p1877_msb);
			fprintf (f, "P1877R rec0=%ld stamp=%ld cam=(%ld,%ld,%ld)\n",
			         ffe_p1877_rec[0], ffe_p1877_rec[1], ffe_p1877_rec[2],
			         ffe_p1877_rec[3], ffe_p1877_rec[4]);
		}
	}
	fprintf (f, "OBJ%ld src=%lx pos=(%ld,%ld,%ld) ret=%lx\n", num, src,
	         *(const long *)(src + 0x26),
	         *(const long *)(src + 0x2e),
	         *(const long *)(src + 0x36),
	         caller[0]);
	fclose (f);
}

/* GEOMETRY (2026-08-03): WHICH VERTEX-TYPE HANDLER RUNS, named at the source.
 * FUNC_001478 dispatches on the vertex stream byte (ffe.asm:87806-87812):
 *     al = [edi]; if (al > 0x18) -> JUMP_005599
 *     al = DATA_000044[al]; jmp DATA_000045[al*4]
 * with edi = [obj+0x150] + (idx>>1)*6, i.e. SIX stream bytes per record PAIR.
 * Both tables are static and known, so the handler can be named here without
 * touching the translated code at all -- the probe only has to recompute edi
 * from the arguments, which are all on the stack at entry.
 * This is the question the record dump left open. R60 showed [rec+0x18] -- the
 * transform's x -- is the constant 1 in EVERY sample while +0x1c/+0x20 carry
 * plausible values, and [rec+0x14] reads 0 although every handler that calls
 * the matrix transform stores a non-zero stamp there. Those two facts cannot
 * both hold for JUMP_005540, so the type byte decides which code really fills
 * these records before more of ffe.asm is read on a guess. */
static const unsigned char ffe_vtype[25] = {
	0xc, 0xc, 0xc, 0xb, 0xb, 0xa, 0xa, 0x9,
	0x9, 0x8, 0x8, 0x7, 0x7, 0x6, 0x6, 0x5,
	0x5, 0x4, 0x4, 0x3, 0x3, 0x2, 0x2, 0x1,
	0x1
};
static const short ffe_vhandler[13] = {
	5599, 5600, 5599, 5598, 5593, 5586, 5577, 5569,
	5568, 5563, 5558, 5541, 5540
};

void FfeTypeLog (long num) __asm__("_FfeTypeLog");
void FfeTypeLog (long num)
{
	unsigned long *caller = (unsigned long *)((char *)&num + 4 + 60);
	unsigned long obj = caller[1];
	long idx = (long)caller[2];
	long arg3 = (long)caller[3];
	static int n_done;
	unsigned long stream, rec, arena;
	unsigned int t, h;
	FILE *f;

	/* Only the records already reported bad, and only a few of them. This is
	 * an event trigger, not a window: idx names the record directly. */
	if (n_done >= 24) return;
	if (idx < 0 || idx > 3) return;
	if (!(obj > 0x1000 && !(obj & 1))) return;
	stream = *(const unsigned long *)(obj + 0x150);
	if (!(stream > 0x1000)) return;
	n_done++;
	stream += (idx >> 1) * 6;
	t = *(const unsigned char *)stream;
	h = (t <= 0x18) ? ffe_vhandler[ffe_vtype[t]] : 5599;

	{
		extern char *node_base __asm__("DATA_009200");
		arena = (unsigned long)node_base;
	}
	rec = arena + idx * 36;

	f = ffe_logopen ("PROGDIR:ffe_enter.log", "a");
	if (f == NULL) return;
	fprintf (f, "TYPE idx=%ld arg3=%ld type=%u -> JUMP_%04u  bytes=%d,%d,%d "
	         "shift=%ld  pre18=(%ld,%ld,%ld) pre14=%ld\n",
	         idx, arg3, t, h,
	         (int)*(const signed char *)(stream + 2),
	         (int)*(const signed char *)(stream + 3),
	         (int)*(const signed char *)(stream + 4),
	         *(const long *)(obj + 0x148),
	         *(const long *)(rec + 0x18), *(const long *)(rec + 0x1c),
	         *(const long *)(rec + 0x20), *(const long *)(rec + 0x14));
	fclose (f);
}

/* GEOMETRY (2026-08-02): THE TRANSFORM'S INPUT -- scaled model vertex + matrix.
 * R57 pinned the fault to X: it comes out either exactly 0 or in the tens of
 * millions while Y and Z stay inside a 16-bit range, and 17104901 = 0x01050D05
 * is plainly a 16-bit value (low word 0x0D05 = 3333) with junk in its top half.
 * The decoder (ffe.asm:87840-87863) builds the three coordinates from SIGNED
 * BYTES scaled by [esi+0x148] and hands them to FUNC_001671_SpecialVecMatMul
 * as arg3, with the matrix as arg4. That function is oracle-carried and green,
 * so if its input is sane the fault is the MATRIX, and if the input is already
 * huge the fault is the byte scaling.
 * Triggered on the anomaly, never on the first N calls -- the rule this
 * session learned four times over. */
void FfeVecLog (long num) __asm__("_FfeVecLog");
void FfeVecLog (long num)
{
	unsigned long *caller = (unsigned long *)((char *)&num + 4 + 60);
	unsigned long sp = caller[3], mp = caller[4];
	static int n_done;
	long s[3], m[9];
	int k;
	FILE *f;

	/* ALL NINE ELEMENTS. R58 logged only the first row -- (0,0,0x7fffffea)
	 * -- and that alone does NOT prove the matrix is broken: a row of
	 * (0,0,1) is a legitimate 90-degree rotation. Only the full 3x3 says
	 * whether this is a rotation or a displaced copy. 0x7fffffea is this
	 * format's ONE (CLAUDE.md's measured healthy ship matrix reads
	 * `7fffffea 0 0  0 7ffffff5 0  0 0 7ffffff5`), so the test is simple:
	 * exactly one near-2^31 value per row and per column means a rotation,
	 * anything else means corruption. */
	if (n_done >= 12) return;
	if (!(sp > 0x1000 && !(sp & 3) && mp > 0x1000 && !(mp & 3))) return;
	n_done++;
	for (k = 0; k < 3; k++)
		s[k] = *(const long *)(sp + 4 * k);
	for (k = 0; k < 9; k++)
		m[k] = *(const long *)(mp + 4 * k);
	f = ffe_logopen ("PROGDIR:ffe_enter.log", "a");
	if (f == NULL) return;
	fprintf (f, "VEC@%ld src=(%ld,%ld,%ld)\n", num, s[0], s[1], s[2]);
	fprintf (f, "   M  %lx %lx %lx | %lx %lx %lx | %lx %lx %lx\n",
	         (unsigned long)m[0], (unsigned long)m[1], (unsigned long)m[2],
	         (unsigned long)m[3], (unsigned long)m[4], (unsigned long)m[5],
	         (unsigned long)m[6], (unsigned long)m[7], (unsigned long)m[8]);
	fclose (f);
}

/* GEOMETRY (2026-08-02): THE PROJECTION'S INPUT.
 * FUNC_001479 (ffe.asm:88742-88778) is the projection:
 *     sx = x / (z>>8) + 0xa0        (z > 0x10000)
 *     sx = (x<<8) / z + 0xa0        (otherwise)
 *     sy = 0x4f - y / (z>>8)
 * Its argument points at the camera-space vertex: x at +0, y at +4, z at +8,
 * all dwords. R55 measured the OUTPUT -- every polygon shares a first vertex
 * at (3744, 79), i.e. x/(z>>8) = 3584 and y/(z>>8) = 0 -- so this prints the
 * INPUT that produces it and says whether z is too small or x is too large.
 * Note the function's own reject guard at 88736: z <= 0x40 returns 1 without
 * projecting, so anything logged here already passed that. */
void FfeProjLog (long num) __asm__("_FfeProjLog");
void FfeProjLog (long num)
{
	unsigned long *caller = (unsigned long *)((char *)&num + 4 + 60);
	/* ARG **TWO**, not arg one. RETRACTION, 2026-08-03: this probe read
	 * caller[1] and every reading taken through it was one dword low.
	 * FUNC_001479 is called as
	 *     lea eax,[ebx+0x4] / push eax / push ebx / call   (ffe.asm:88705-88708)
	 * so arg1 = ebx is the DESTINATION the projected screen words are
	 * written back into (`mov [edx],cx` / `mov [edx+0x2],ax`, 88806-88809)
	 * and arg2 = ebx+4 is the camera-space vertex it reads. Reading arg1
	 * therefore reported [rec+0x00] -- the packed (sx,sy) word PAIR left
	 * over from the previous frame -- as if it were the x coordinate, which
	 * is exactly where "x = 17104901" came from: 0x01050D05 is two 16-bit
	 * screen coordinates, not one huge number. The same shift made
	 * [rec+0x14], the validity stamp JUMP_005569 sets to 1, read as the
	 * transform's x and produce the "x is always 1" pattern. Both readings
	 * were artefacts of this line. */
	unsigned long p = caller[2];
	static int n_bad, n_ok;
	long x, y, z, zs, sx, sy;
	FILE *f;

	if (!(p > 0x1000 && !(p & 1))) return;
	x = *(const long *)p;
	y = *(const long *)(p + 4);
	z = *(const long *)(p + 8);

	/* AN UNBIASED SAMPLE, for comparison against the PC reference's own
	 * pc_proj.log (build\pcprobe_proj.c logs the identical fields there).
	 *
	 * This is deliberately NOT the anomaly trigger used everywhere else in
	 * this file. The question it answers is statistical -- "are our
	 * camera-space magnitudes the same size as the reference's?" -- and an
	 * anomaly filter would answer it with the anomalies. So: every 37th call
	 * (a prime, so it cannot alias with a per-polygon or per-object period)
	 * across the whole run, capped, and with NO z filter, because the host
	 * applies the game's own z >= 0x40 guard to BOTH logs afterwards.
	 *
	 * The previous version filtered z < 0x40 and capped at 20 out-of-range
	 * plus 2 in-range samples, which made every magnitude drawn from it
	 * unrepresentative -- exactly the mistake this comment exists to stop
	 * being repeated.
	 */
	{
		static long every;
		if ((every++ % 37) != 0) return;
	}
	if (n_bad >= 200) return;
	n_bad++;
	(void)n_ok;

	/* Mirror the function's OWN two branches (ffe.asm:88743-88778) so the
	 * printed sx/sy are what the game computes. z below its 0x40 guard is
	 * still logged -- the raw x/y/z is the point -- but not divided by. */
	zs = z >> 8;
	if (z < 0x40) {
		sx = sy = -99999;
	} else if (z > 0x10000) {
		sx = (x / zs) + 0xa0;
		sy = 0x4f - (y / zs);
	} else {
		sx = ((x << 8) / z) + 0xa0;
		sy = 0x4f - ((y << 8) / z);
	}

	f = ffe_logopen ("PROGDIR:ffe_enter.log", "a");
	if (f == NULL) return;
	/* WHICH RECORD carries the bad X. The vertex records live in the
	 * DATA_009200 arena at a 36-byte stride (FUNC_001478 builds the pointer
	 * as arena + idx*36, ffe.asm:87749-87751), so printing the address and
	 * its offset in records names the slot -- and therefore points at whoever
	 * wrote it. Everything upstream is now measured healthy: the transform's
	 * input vector, the object matrix (a valid 90-degree rotation, R59), and
	 * FUNC_001671 itself is oracle-green. */
	{
		extern char *node_base __asm__("DATA_009200");
		long off = (long)(p - (unsigned long)node_base);
		/* SPLIT THE ADD. The projection's argument is record+4
		 * (ffe.asm:88705 `lea eax,[ebx+0x4]`), and JUMP_005601 fills
		 * those three dwords one line earlier as
		 *     [rec+0x4] = [rec+0x18] + [obj+0x4]
		 *     [rec+0x8] = [rec+0x1c] + [obj+0x8]
		 *     [rec+0xc] = [rec+0x20] + [obj+0xc]
		 * so the SAME record still carries the transform's own output at
		 * +0x18.., untouched by the translation. Printing both halves
		 * decides the open question in one run: a huge +0x18 blames the
		 * vertex-type handler behind DATA_000045, a sane +0x18 with a
		 * huge +0x4 blames the object position added on top. Nothing
		 * else in this record is guessed -- +0x3c.. is the ODD sibling's
		 * vector (offsets 0x24..0x47 are physically the next 36-byte
		 * record, ffe.asm:87874-87894) and is printed for contrast,
		 * because the even records are the ones reported bad. */
		unsigned long r = p - 4;
		long cx = *(const long *)(r + 0x18);
		long cy = *(const long *)(r + 0x1c);
		long cz = *(const long *)(r + 0x20);

		fprintf (f, "PROJSAMP@%ld x=%ld y=%ld z=%ld z>>8=%ld sx=%ld sy=%ld %s "
		         "at=%lx off=%ld rec=%ld\n",
		         num, x, y, z, zs, sx, sy,
		         (sx >= 0 && sx < 320 && sy >= 0 && sy < 158) ? "inrange" : "OUT",
		         p, off - 4, (off - 4) / 36);
		fprintf (f, "   raw18=(%ld,%ld,%ld) hex=(%lx,%lx,%lx) "
		         "objadd=(%ld,%ld,%ld) stamp=%lx sib3c=(%ld,%ld,%ld)\n",
		         cx, cy, cz,
		         (unsigned long)cx, (unsigned long)cy, (unsigned long)cz,
		         x - cx, y - cy, z - cz,
		         (unsigned long)*(const long *)(r + 0x14),
		         *(const long *)(r + 0x3c), *(const long *)(r + 0x40),
		         *(const long *)(r + 0x44));
		/* THE WHOLE 36-BYTE RECORD, as raw dwords.
		 * Two independent readings came out one dword low at the same
		 * time: [r+0x18] -- the transform's x -- is the constant 1 in
		 * every sample, and [r+0x14] reads 0 although JUMP_005540 stores
		 * a literal 1 there (ffe.asm:87899-87900). Either the whole
		 * vector sits at +0x1c.. instead of +0x18.., or the x really is
		 * being overwritten by that stamp. The dump separates the two
		 * without another hypothesis: if the plausible triple appears at
		 * +0x1c/+0x20/+0x24 the writes are displaced, and if +0x14 holds
		 * the stamp with +0x18 still 1 they are not. */
		{
			int k;
			fprintf (f, "   RECDUMP");
			for (k = 0; k <= 0x24; k += 4)
				fprintf (f, " %02x:%ld", k, *(const long *)(r + k));
			fprintf (f, "\n");
		}
	}
	fclose (f);
}

/* GEOMETRY (2026-08-02): THE PROJECTED VERTICES of a drawn polygon.
 * The colour half is fixed and visible (the dominant surface went white ->
 * green), and the spans are already proven sane (ymin/ymax inside the 158-row
 * viewport, 2-3 crossings per row). What is left is that the scene is HUGE
 * SLABS instead of a landing pad, and a slab is what you get when the vertices
 * are in range but in the wrong PLACES.
 * FUNC_001714 receives the polygon's vertex pointers as arguments 2..4; each
 * points at a {short x, short y} screen record (FUNC_001712 bounds-checks them
 * against 0x140 = 320 and 0x9e = 158 at ffe.asm:105359-105370). Printing them
 * says outright whether the projection produces a plausible shape or three
 * points at the viewport corners. Guarded and capped -- R22 proved an
 * uncapped probe inside the render measures itself. */
void FfeVtxLog (long num) __asm__("_FfeVtxLog");
void FfeVtxLog (long num)
{
	unsigned long *caller = (unsigned long *)((char *)&num + 4 + 60);
	static int n_done;
	FILE *f;
	int k;

	if (n_done >= 24) return;
	n_done++;
	f = ffe_logopen ("PROGDIR:ffe_enter.log", "a");
	if (f == NULL) return;
	fprintf (f, "VTX@%ld", num);
	for (k = 2; k <= 4; k++) {
		unsigned long p = caller[k];
		if (p > 0x1000 && !(p & 1))
			fprintf (f, "  v%d=(%d,%d)", k - 2,
			         (int)*(const short *)p,
			         (int)*(const short *)(p + 2));
		else
			fprintf (f, "  v%d=BAD:%lx", k - 2, p);
	}
	fprintf (f, "\n");
	fclose (f);
}

/* A SHARED SEQUENCE COUNTER for the two list probes.
 * The entry log is a CYCLE, so "1786, 1682, 1789" in the file is equally
 * consistent with reset->render->draw and with render->reset->draw. Stamping
 * both probes from one counter settles the interleaving without any more code
 * archaeology: if a non-zero ROOT sample carries a HIGHER sequence number than
 * the NODELIST sample that reports it empty, the render happens AFTER the
 * draw and the reset in between is what wipes the frame. */
long ffe_probe_seq;
/* Renders since the last draw. FfeRootNow bumps it on EVERY call (before any
 * cap or skip), FfeNodeList prints and clears it. Two capped probes never
 * managed to sample the same window -- R46 and R47 both produced two disjoint
 * blocks of sequence numbers -- so the ordering question is answered inside
 * ONE probe instead: a draw that reports renders_since_last_draw > 0 while
 * still seeing an empty list proves the list is cleared BETWEEN the render and
 * the draw; a draw reporting 0 proves it draws before the scene is built. */
long ffe_render_ticks;

/* BLANK VIEWPORT (2026-08-02): THE ROOT LIST *DURING* THE RENDER.
 * Established: the full render chain DOES run every cockpit frame
 * (1682 -> 1683 -> 1685 -> 1773 -> 1774 -> 1745 -> 1689 -> 1772, the model
 * interpreter, ten times), the frame order is correct (reset -> render ->
 * draw), and the allocator is busy -- yet the list FUNC_001788 walks is empty
 * at draw time. So either nothing is ever linked, or something clears the root
 * between the render and the draw.
 * Sampling DATA_009255 from inside the render (injected on FUNC_001772)
 * separates those two: non-zero here and zero at draw means a reset in
 * between; zero in both means the linking itself never takes. */
void FfeRootNow (long num) __asm__("_FfeRootNow");
void FfeRootNow (long num)
{
	extern void *list_root __asm__("DATA_009255");
	extern void **cur_head __asm__("DATA_007832");
	extern long list_depth __asm__("DATA_007831");
	extern long link_gate  __asm__("DATA_007833");
	static int n_done;
	FILE *f;

	(void)num;
	ffe_render_ticks++;
	{ static int n_seen; n_seen++; if (n_seen < 200) return; }
	if (n_done >= 40) return;
	n_done++;
	f = ffe_logopen ("PROGDIR:ffe_enter.log", "a");
	if (f == NULL) return;
	fprintf (f, "seq=%ld ROOT@%ld root=%lx cur=%lx curval=%lx depth=%ld gate=%ld\n",
	         ++ffe_probe_seq, num, (unsigned long)list_root, (unsigned long)cur_head,
	         (unsigned long)(cur_head ? *cur_head : 0), list_depth, link_gate);
	fclose (f);
}

/* BLANK VIEWPORT (2026-08-02): DOES FUNC_001780 REALLY NEVER RUN?
 * The whole contradiction rests on that single NEGATIVE result, and it came
 * from FfeEnterLog, which shares one static counter across every traced
 * function and has a cap. A negative from a shared, capped probe is exactly
 * the kind of evidence that deserves an independent check before anything is
 * concluded from it. This logger has its OWN counter, no gate, and records
 * the argument that would be stored into DATA_007833 plus the flag's value
 * before and after -- so if FUNC_001780 does run, this says so and names the
 * value it writes. */
void FfeGateCall (long num) __asm__("_FfeGateCall");
void FfeGateCall (long num)
{
	unsigned long *caller = (unsigned long *)((char *)&num + 4 + 60);
	extern long link_gate __asm__("DATA_007833");
	static int n_done;
	FILE *f;

	if (n_done >= 24) return;
	n_done++;
	f = ffe_logopen ("PROGDIR:ffe_enter.log", "a");
	if (f == NULL) return;
	fprintf (f, "GATECALL@%ld arg1=%lx arg2=%lx gate_before=%ld\n",
	         num, caller[1], caller[2], link_gate);
	fclose (f);
}

/* BLANK VIEWPORT (2026-08-02, evening): IS THE NODE LIST EMPTY?
 * Measured, not guessed: FUNC_001613_DrawSpans never runs in the landed
 * cockpit -- the probe injected on it produced no log at all -- so nothing
 * fills polygons, which is exactly why the developer's screenshot shows black
 * with thin vertical streaks (unfilled EDGES, not surfaces).
 * DrawSpans is only ever called from FUNC_001791 and FUNC_001799, which are
 * NODE CALLBACKS. FUNC_001788 (ffe.asm:111760) is the walker: it counts the
 * list, merge-sorts it, then for each node does
 *     mov eax,[ebp+8] / add eax,8 / call near [eax]   (ffe.asm:111899-111905)
 * repeatedly while DATA_007834 == 0. Two things could give zero draws:
 *   - FUNC_001788 is never called at all, or
 *   - it is called with an EMPTY list (*arg1 == 0 exits at 111896).
 * DATA_007834 itself is clean -- 22 accesses, all dword, zero-initialised, no
 * byte-order question -- so the flag protocol is not the suspect.
 * Logs the head pointer, what it dereferences to, and the chain length. */
void FfeNodeList (long num) __asm__("_FfeNodeList");
void FfeNodeList (long num)
{
	unsigned long *caller = (unsigned long *)((char *)&num + 4 + 60);
	unsigned long head = caller[1];
	unsigned long first = 0;
	long len = 0;
	/* SKIP THE INTRO. R46 showed this probe spending its whole budget on the
	 * first twelve draws -- which happen before the game builds any scene --
	 * while the ROOT probe sampled later, so the two never overlapped in time
	 * and their sequence numbers proved nothing about frame ordering. Sample
	 * the SAME window the render probe does. */
	/* TRIGGER ON THE INTERESTING DRAW, not on a fixed window. R48 sampled
	 * draws 200..240 and every one reported renders_since_draw=0 -- they are
	 * console frames, which have no 3D scene by design. The draw that matters
	 * is the one FOLLOWING a render, so log those and keep two ordinary ones
	 * for contrast. Fixed windows have now failed three times in a row. */
	static int n_done, n_zero;
	FILE *f;

	if (ffe_render_ticks == 0) {
		if (n_zero >= 2) return;
		n_zero++;
	} else if (n_done >= 40) {
		return;
	} else {
		n_done++;
	}
	if (head > 0x1000 && !(head & 1)) {
		unsigned long p;
		first = *(unsigned long *)head;
		p = first;
		while (p > 0x1000 && !(p & 1) && len < 5000) {
			len++;
			p = *(unsigned long *)(p + 4);
		}
	}
	/* IS IT EVEN THE ROOT? (2026-08-02 evening.) The list structure is now
	 * read: DATA_007832 is the CURRENT head pointer, DATA_009255 the ROOT,
	 * DATA_009254 the stack of saved heads and DATA_007831 the depth
	 * (FUNC_001783 pushes, FUNC_001784 pops, FUNC_001785 resets). Group nodes
	 * -- the ones FUNC_001780 allocates with arg3=1 and marks FUNC_001792,
	 * whose callback recurses into the sublist -- DO link, so the root should
	 * not be empty. Print the root's address and value, the current head and
	 * the depth next to arg1: that says outright whether FUNC_001788 is handed
	 * the root, a live sublist, or a head that has already been popped. */
	{
		extern void *list_root  __asm__("DATA_009255");
		extern void **cur_head  __asm__("DATA_007832");
		extern long list_depth  __asm__("DATA_007831");

		f = ffe_logopen ("PROGDIR:ffe_enter.log", "a");
		if (f == NULL) return;
		fprintf (f, "seq=%ld NODELIST@%ld renders_since_draw=%ld head=%lx first=%lx len=%ld "
		         "rootaddr=%lx rootval=%lx cur=%lx depth=%ld\n",
		         ++ffe_probe_seq, num, ffe_render_ticks, head, first, len,
		         (unsigned long)&list_root, (unsigned long)list_root,
		         (unsigned long)cur_head, list_depth);
		ffe_render_ticks = 0;
		fclose (f);
	}
}

/* F3 CRASH (2026-08-02): FUNC_001471's ARENA INDEX, triggered on the anomaly.
 * FUNC_001471 (ffe.asm:86998) is the only function left on the proven-guilty
 * quad path that WRITES into the 36-byte-stride DATA_009200 arena, and it
 * indexes with `movsx eax,bl` -- the SIGN-EXTENDED low byte of arg2 -- times
 * 9, times 4. A negative index writes BELOW the arena and a large one past
 * it, which is exactly a reset with no CPU exception. The first four calls in
 * the R25 trace carried 0x1e/0x20/0x1c/0, all sane, but those are the first.
 * Logs only indices outside 0..127, plus two in-range ones for contrast --
 * the R29 lesson: a first-N cap measures the wrong calls when the interesting
 * event is late. */
void FfeIdxNow (long num) __asm__("_FfeIdxNow");
void FfeIdxNow (long num)
{
	unsigned long *caller = (unsigned long *)((char *)&num + 4 + 60);
	signed char idx = (signed char)(caller[2] & 0xff);
	static int n_bad, n_ok;
	FILE *f;

	if (idx >= 0 && idx < 128) {
		if (n_ok >= 2) return;
		n_ok++;
	} else if (n_bad >= 20) {
		return;
	} else {
		n_bad++;
	}
	f = ffe_logopen ("PROGDIR:ffe_enter.log", "a");
	if (f == NULL) return;
	fprintf (f, "IDX@%ld arg2=%lx bl=%d rec=%d off=%d\n", num,
	         caller[2], (int)idx, (int)idx, (int)idx * 36);
	fclose (f);
}

/* F3 CRASH (2026-08-02): the NODE ALLOCATOR's cursor at its entry.
 * FUNC_001778 (ffe.asm:111547) is a bump allocator: it writes a header
 * through [DATA_007830], pushes onto a list through [DATA_007832], then bumps
 * the cursor by arg1*4. All three control variables are initialised
 * `dd DATA_009253` / `dd DATA_009255`, i.e. relocated pointers, and their
 * storage class is dword-only, which for a `dd SYMBOL` means a native dc.l --
 * so they SHOULD be right. This measures whether they actually are, because
 * the trace dies at this function's entry and a bad cursor makes the header
 * writes wild, which is a reset with no CPU exception.
 * Capped like FfeArenaNow: an uncapped probe inside the render moved the
 * crash somewhere else entirely in R22. */
void FfeAllocNow (long num) __asm__("_FfeAllocNow");
void FfeAllocNow (long num)
{
	unsigned long *caller = (unsigned long *)((char *)&num + 4 + 60);
	extern char *alloc_cur  __asm__("DATA_007830");
	extern char *alloc_base __asm__("DATA_007829");
	extern char *alloc_head __asm__("DATA_007832");
	extern char arena[] __asm__("DATA_009253");
	static int n_done;
	FILE *f;

	/* TRIGGER ON THE ANOMALY, not on the first N calls. R29 capped this at 24
	 * and therefore sampled the COCKPIT's allocations, all healthy (cur
	 * advancing, used 200..341 against a 50000 limit) -- while the fatal call
	 * seen in the trace carries val=0x7fa7, three orders of magnitude below
	 * the ~0x5000000 depth keys every healthy call passes. A first-N cap
	 * measures the wrong calls; this logs only the ones shaped like the fatal
	 * one, plus a couple of healthy ones for contrast. */
	if (0) {
		static int n_ctrl;
		if (n_ctrl >= 2) return;
		n_ctrl++;
	} else if (n_done >= 20) {
		return;
	}
	n_done++;
	f = ffe_logopen ("PROGDIR:ffe_enter.log", "a");
	if (f == NULL) return;
	/* THE LINK CONDITION (2026-08-02 evening). R35 proved the node list
	 * FUNC_001788 walks is EMPTY (head=10242774 first=0 len=0, twelve times)
	 * while this allocator is busy (used 139..677) -- so nodes are allocated
	 * and never LINKED. FUNC_001778 links only when
	 *     [ebp+0x10] != 0  ||  DATA_007833 == 0
	 * (ffe.asm:111553-111556). The trace shows arg3 = 0, so everything hangs
	 * on DATA_007833, which FUNC_001780 sets from its own argument. Log both,
	 * plus the head the linking would use, so this run says outright whether
	 * the link is being skipped and why. */
	{
		extern long link_gate __asm__("DATA_007833");

		fprintf (f, "ALLOC@%ld cur=%lx base=%lx head=%lx arena=%lx used=%ld "
		         "n=%lx val=%lx arg3=%lx gate7833=%lx linked=%d\n", num,
		         (unsigned long)alloc_cur, (unsigned long)alloc_base,
		         (unsigned long)alloc_head, (unsigned long)arena,
		         (long)(alloc_cur - alloc_base) / 4, caller[1], caller[2],
		         caller[3], (unsigned long)link_gate,
		         (caller[3] != 0 || link_gate == 0) ? 1 : 0);
	}
	fclose (f);
}

/* F3 CRASH (2026-08-02): the node-arena base AT THE FATAL MOMENT, flushed
 * immediately.
 * Why not the NODEARENA line in dump_clock: that prints every 32 presents,
 * and FUNC_001745 only sets DATA_009200 during the F3 render frame -- the
 * machine dies in that same frame, so every dumped value predates the write.
 * Reading base=0 there proved nothing except "not set yet", and this probe
 * exists because that misreading was caught before it was acted on.
 * Injected on FUNC_001690, which runs once per render, so a fopen per call
 * costs nothing. No dereference of the base -- it may well be garbage. */
void FfeArenaNow (long num) __asm__("_FfeArenaNow");
void FfeArenaNow (long num)
{
	unsigned long *caller = (unsigned long *)((char *)&num + 4 + 60);
	extern char *node_base __asm__("DATA_009200");
	extern char node_arena[] __asm__("DATA_009251");
	static int n_done;
	FILE *f;

	/* CAPPED. R22 left this uncapped and logged 1660 times inside the
	 * render; the fopen storm moved the crash to an entirely different
	 * place (boot 1 died at present 481, right after the new game starts,
	 * before F3 was pressed at all). A probe that changes where the bug
	 * happens measures itself, not the bug. */
	if (n_done >= 24) return;
	n_done++;
	f = ffe_logopen ("PROGDIR:ffe_enter.log", "a");
	if (f == NULL) return;
	fprintf (f, "ARENA@%ld base=%lx arena=%lx off=%ld rec=%ld "
	         "count=%lx val=%lx\n", num,
	         (unsigned long)node_base, (unsigned long)node_arena,
	         (long)(node_base - node_arena),
	         (long)(node_base - node_arena) / 36,
	         caller[1], caller[2]);
	fclose (f);
}

/* VIEWPORT DEBUG (2026-08-02): arg1 recorded VERBATIM, with NO dereference.
 * Written to measure the FUNC_001761 colour fix at FUNC_001326_DPal32to16,
 * whose argument IS the packed RGB dword -- not a pointer. FfeColourLog above
 * dereferences its sample and would fault (or silently reject) on a colour;
 * FfeArgLog's arg_peek dereferences args 1 and 2 and already hard-reset the
 * machine once on FUNC_001791. This one only stores, so it cannot.
 * Reuses the COL slots so dump_clock's existing printer shows it: ffe_col_ptr
 * stays 0, which reads as "no pointer was sampled". */
void FfeColourArg (long num) __asm__("_FfeColourArg");
void FfeColourArg (long num)
{
	unsigned long *caller = (unsigned long *)((char *)&num + 4 + 60);

	(void)num;
	ffe_col_calls++;
	if (ffe_col_n >= FFE_COL_MAX) return;
	/* RECORD THE CALLER. DPal32to16 has two producers -- FUNC_001761 (no
	 * clamp) and FUNC_001712 (which clamps each channel to 0xf) -- and a
	 * measured R of 0x4a is impossible out of the clamped one. The return
	 * address says which path produced the out-of-range colour. */
	ffe_col_ptr[ffe_col_n] = (long)caller[0];
	ffe_col_v[ffe_col_n] = (long)caller[1];
	ffe_col_n++;
}

/* VIEWPORT DEBUG (2026-08-02): the first EIGHT cdecl arguments, not three.
 * Written for the recursive terrain subdividers FUNC_001868..1873, whose
 * subdivision LEVEL is argument 7 ([ebp+0x20]) and whose termination depends
 * on it -- FfeArgLog's three dwords cannot reach it. Records the FIRST call
 * of each traced function per dump window (the deepest recursion is not the
 * interesting one; the entry level is), plus the min/max level seen. */
#define FFE_A8_MAX 6
long ffe_a8_num[FFE_A8_MAX];
long ffe_a8_arg[FFE_A8_MAX][8];
long ffe_a8_node[FFE_A8_MAX][10];	/* 8 longs at arg1, then +0x40 and +0x44 */
long ffe_a8_n;
long ffe_a8_calls;

void FfeArg8Log (long num) __asm__("_FfeArg8Log");
void FfeArg8Log (long num)
{
	unsigned long *caller = (unsigned long *)((char *)&num + 4 + 60);
	long i;
	int k;

	ffe_a8_calls++;
	for (i = 0; i < ffe_a8_n; i++)
		if (ffe_a8_num[i] == num) return;	/* first call only */
	if (ffe_a8_n >= FFE_A8_MAX) return;
	ffe_a8_num[ffe_a8_n] = num;
	for (k = 0; k < 8; k++)
		ffe_a8_arg[ffe_a8_n][k] = (long)caller[1 + k];
	/* and the node argument 1 points at: for FUNC_001868 that is the
	 * 0x48-byte vertex record whose +4/+8/+0xc are the view-space x,y,z
	 * and whose +0x40/+0x44 feed the LOD test that must stop the
	 * recursion. Longwords, guarded against a wild pointer. */
	{
		unsigned long p = caller[1];

		if (p > 0x1000 && !(p & 1))
			for (k = 0; k < 8; k++)
				ffe_a8_node[ffe_a8_n][k] = ((long *)p)[k];
		if (p > 0x1000 && !(p & 1)) {
			ffe_a8_node[ffe_a8_n][8]  = ((long *)p)[0x40 / 4];
			ffe_a8_node[ffe_a8_n][9]  = ((long *)p)[0x44 / 4];
		}
	}
	ffe_a8_n++;
}

/* VIEWPORT DEBUG (2026-08-02): a CALLER census. The renderer exhausts its
 * 3000-node small pool (FUNC_001820) on the first landed frame and takes the
 * game's own escape; the question that follows is WHICH call site is eating
 * the pool. Injected with the same payload as FfeCount, this tallies the
 * traced function's RETURN ADDRESS -- caller[0], the stack walk FfeArgLog
 * already proves -- into a small table that dump_clock prints and resets.
 * No I/O: it runs 3000 times a frame. Resolve the addresses through the MAP
 * line and build\amiga-out\ffe_game.symmap. Debug only. */
#define FFE_RET_MAX 24
unsigned long ffe_ret_pc[FFE_RET_MAX];
long ffe_ret_n[FFE_RET_MAX];
long ffe_ret_total;

void FfeRetCensus (long num) __asm__("_FfeRetCensus");
void FfeRetCensus (long num)
{
	unsigned long *caller = (unsigned long *)((char *)&num + 4 + 60);
	unsigned long pc = caller[0];
	int i;

	ffe_ret_total++;
	for (i = 0; i < FFE_RET_MAX; i++) {
		if (ffe_ret_pc[i] == pc) { ffe_ret_n[i]++; return; }
		if (ffe_ret_pc[i] == 0) {
			ffe_ret_pc[i] = pc;
			ffe_ret_n[i] = 1;
			return;
		}
	}
}

void FfeArgLog (long num)
{
	unsigned long *caller = (unsigned long *)((char *)&num + 4 + 60);
	long i, seen = 0;

	zero_watch (num);
	/* Only calls that target the OBJECT POOL are interesting: the renderer
	 * also builds matrices into stack temporaries every frame, and those
	 * would hide the one call that writes a pooled object. DATA_009134 is
	 * the 38,992-byte pool the player object lives in (resolved from the
	 * runtime pointer at DATA_008804+0xc8). */
	if (num == 1675) {
		extern char obj_pool[] __asm__("DATA_009134");
		char *t = (char *)caller[1];

		/* SNAPSHOT FIRST: the very next MatBuildYZT call after the pooled
		 * one reads m00 back. The renderer calls this function every frame
		 * on stack temporaries, so the snapshot lands within a frame of the
		 * write -- close enough to separate "the write never landed" from
		 * "something cleared it later". */
		if (ffe_yzt_obj && !ffe_yzt_checked) {
			ffe_yzt_m0 = *(long *)ffe_yzt_obj;
			ffe_yzt_checked = 1;
		}
		if (t >= obj_pool && t < obj_pool + 38992) {
			/* The caller is FUNC_000880's system-population loop: create an
			 * object (FUNC_000927), fill it, build its matrix, copy its name
			 * at +0x124. If the allocator kept handing back the SAME slot,
			 * every object would overwrite the last and only one matrix
			 * would survive -- so count the DISTINCT targets. */
			if (!ffe_yzt_first) ffe_yzt_first = (long)t;
			if ((long)t != ffe_yzt_obj) ffe_yzt_distinct++;
			ffe_yzt_n++;
			ffe_yzt_obj = (long)t;
			mat_build_c (t, (long)caller[2], (long)caller[3]);
			/* the RETURN ADDRESS names the call site: resolve it against
			 * the ANCHOR line's FUNC_001772 runtime address and the link
			 * map. Whatever runs immediately after that call is the thing
			 * that clears the matrix -- the alignment and the builder
			 * itself are both already ruled out by measurement. */
			ffe_yzt_ret = (long)caller[0];
		}
	}

	if (ffe_arg_n >= FFE_ARG_MAX) return;
	/* FAIR SHARE: at most 8 samples per traced function, so a primitive
	 * called 200x per frame cannot crowd out the once-per-frame one. */
	for (i = 0; i < ffe_arg_n; i++)
		if (ffe_arg_buf[i][0] == num) seen++;
	if (seen >= 8) return;
	ffe_arg_buf[ffe_arg_n][0] = num;
	ffe_arg_buf[ffe_arg_n][1] = (long)caller[1];
	ffe_arg_buf[ffe_arg_n][2] = (long)caller[2];
	ffe_arg_buf[ffe_arg_n][3] = (long)caller[3];
	/* AT CALL TIME: arg0/arg1 are pointers into scratch vertices or into
	 * the model bytecode stream, both of which the caller REUSES -- decoding
	 * them in dump_clock 128 frames later reports the last write, not this
	 * call's. Peek four words at each now. -99999 marks "not a pointer". */
	arg_peek (&ffe_arg_buf[ffe_arg_n][4], caller[1]);
	arg_peek (&ffe_arg_buf[ffe_arg_n][8], caller[2]);
	ffe_arg_n++;
}

/* VIEWPORT DEBUG: the RENDER-STATE sampler. Every function of the model
 * pipeline takes the 0x15c-byte render state as arg0 -- it lives in
 * FUNC_001683's stack frame, so it cannot be reached by name. The offsets
 * are the ones the pipeline itself uses: +0x44 the position-normalisation
 * shift FUNC_001475 returned (ffe.asm:102394), +0x148 the view scale
 * exponent copied from the 1538 table entry (102408), +0x48/+0x4c/+0x50 the
 * eye-relative position after VecMatTMul (102416), +0xd4 the last distance
 * (103240) and +0xf0 the mode. A huge +0x44 with a tiny +0x48.. is exactly
 * what collapses every projected vertex onto the view centre. */
#define FFE_ST_MAX 8
long ffe_st_buf[FFE_ST_MAX][8];
long ffe_st_n;

void FfeStateLog (long num)
{
	unsigned long *caller = (unsigned long *)((char *)&num + 4 + 60);
	long *st = (long *)caller[1];

	if (ffe_st_n >= FFE_ST_MAX) return;
	if ((unsigned long)st <= 0x1000 || ((unsigned long)st & 3)) return;
	ffe_st_buf[ffe_st_n][0] = num;
	ffe_st_buf[ffe_st_n][1] = st[0x44 / 4];
	ffe_st_buf[ffe_st_n][2] = st[0x148 / 4];
	ffe_st_buf[ffe_st_n][3] = st[0x48 / 4];
	ffe_st_buf[ffe_st_n][4] = st[0x4c / 4];
	ffe_st_buf[ffe_st_n][5] = st[0x50 / 4];
	ffe_st_buf[ffe_st_n][6] = st[0xd4 / 4];
	/* the vertex-cache base, sampled INSIDE the render: DATA_009200 is a
	 * push/pop cursor into the DATA_009251 arena (ffe.asm:108451/108509),
	 * so reading it between frames only ever shows the popped value. */
	{
		extern long vtx_base __asm__("DATA_009200");
		ffe_st_buf[ffe_st_n][7] = vtx_base;
	}
	ffe_st_n++;
}

/* VIEWPORT DEBUG: the MODEL VERTEX TABLES. FUNC_001481 (ffe.asm:88865) builds
 * one transformed vertex: it reads the vertex record BYTE-wise from
 * state+0x154 (`add esi,[eax+0x154]` then `mov cl,[esi]` / `movsx byte
 * [esi+2..4]`, 88880-88899) and a second table at state+0x150. Both pointers
 * come out of the model descriptor. All-zero tables would give every vertex
 * x=y=0, which is exactly what FUNC_001479's projection turns into the
 * constant screen point (160,79) measured for all four quad corners. */
#define FFE_VT_MAX 6
#define FFE_VT_W   24		/* 8 below + 9 matrix + 3 position + 3 source */
long ffe_vt_buf[FFE_VT_MAX][FFE_VT_W];
long ffe_vt_n;

void FfeVtxTabLog (long num)
{
	unsigned long *caller = (unsigned long *)((char *)&num + 4 + 60);
	long *st = (long *)caller[1];
	unsigned char *p;

	if (ffe_vt_n >= FFE_VT_MAX) return;
	if ((unsigned long)st <= 0x1000 || ((unsigned long)st & 3)) return;
	ffe_vt_buf[ffe_vt_n][0] = num;
	ffe_vt_buf[ffe_vt_n][1] = (long)caller[2];	/* the vertex index */
	ffe_vt_buf[ffe_vt_n][2] = st[0x150 / 4];
	ffe_vt_buf[ffe_vt_n][3] = st[0x154 / 4];
	p = (unsigned char *)st[0x154 / 4];
	ffe_vt_buf[ffe_vt_n][4] = ((unsigned long)p > 0x1000)
		? (long)((p[0] << 24) | (p[1] << 16) | (p[2] << 8) | p[3]) : -1;
	ffe_vt_buf[ffe_vt_n][5] = ((unsigned long)p > 0x1000)
		? (long)((p[4] << 24) | (p[5] << 16) | (p[6] << 8) | p[7]) : -1;
	p = (unsigned char *)st[0x150 / 4];
	ffe_vt_buf[ffe_vt_n][6] = ((unsigned long)p > 0x1000)
		? (long)((p[0] << 24) | (p[1] << 16) | (p[2] << 8) | p[3]) : -1;
	ffe_vt_buf[ffe_vt_n][7] = st[0x148 / 4];
	/* the OBJECT's rotation matrix, 9 dwords copied into state+0x14 by
	 * FUNC_001683 (`lea edi,[esi+0x14]` / `rep movsd` with ecx=9,
	 * ffe.asm:102240-102243), and the eye-relative position at +0x48 that
	 * FUNC_001685 leaves behind. A zero matrix or a zero position makes
	 * every transformed x,y zero, which is the (160,79) collapse. */
	{
		int k;

		for (k = 0; k < 9; k++)
			ffe_vt_buf[ffe_vt_n][8 + k] = st[0x14 / 4 + k];
		for (k = 0; k < 3; k++)
			ffe_vt_buf[ffe_vt_n][17 + k] = st[0x48 / 4 + k];
		/* WHICH object this state was built from: FUNC_001683 stores the
		 * object pointer at state+0x14c (ffe.asm:102233) right before the
		 * `rep movsd` that copies its 9-dword matrix into state+0x14. If
		 * the matrix here is zero, reading the SOURCE tells whether the
		 * copy failed or the object itself is zeroed. */
		ffe_vt_buf[ffe_vt_n][2] = st[0x14c / 4];
		{
			long *ob = (long *)st[0x14c / 4];

			/* [20] the object's m00, [21] its MODEL ID at +0x82 and [22]
			 * its z position at +0x36 -- the model id names the call site
			 * that built this object: 0xa5 = FUNC_001066 (ffe.asm:79216),
			 * 0xa6 = the site at 57592, 0xb0 = the DATA_009127 view built
			 * by FUNC_000959. Both fields are UNALIGNED in the original
			 * struct, which the 68020 handles. */
			if ((unsigned long)ob > 0x1000 && !((unsigned long)ob & 3)) {
				char *b = (char *)ob;

				ffe_vt_buf[ffe_vt_n][20] = ob[0];
				ffe_vt_buf[ffe_vt_n][21] = *(long *)(b + 0x82);
				ffe_vt_buf[ffe_vt_n][22] = *(long *)(b + 0x36);
			}
		}
	}
	ffe_vt_n++;
}

/* VIEWPORT DEBUG: a GAME OBJECT dump. arg0 of FUNC_001054 is an object in the
 * pool DATA_009134 (resolved from the runtime pointer via the ANCHOR line).
 * The question it answers: was the object CREATED and only its matrix left
 * unwritten, or was the whole slot never touched? Class is byte +0x87 (0x0b
 * for the kind FUNC_000778 dispatches), type byte +0xff (the row index into
 * DATA_006258), model dword +0x82, position dwords +0x26/+0x2e/+0x36 (all
 * unaligned in the original layout, which the 68020 handles). */
#define FFE_OB_MAX 4
long ffe_ob_buf[FFE_OB_MAX][16];
long ffe_ob_n;

void FfeObjLog (long num)
{
	unsigned long *caller = (unsigned long *)((char *)&num + 4 + 60);
	char *ob = (char *)caller[1];
	int k;

	if (ffe_ob_n >= FFE_OB_MAX) return;
	if ((unsigned long)ob <= 0x1000 || ((unsigned long)ob & 3)) return;
	ffe_ob_buf[ffe_ob_n][0] = num;
	ffe_ob_buf[ffe_ob_n][1] = (long)ob;
	for (k = 0; k < 9; k++)
		ffe_ob_buf[ffe_ob_n][2 + k] = ((long *)ob)[k];
	ffe_ob_buf[ffe_ob_n][11] = (unsigned char)ob[0x87];
	ffe_ob_buf[ffe_ob_n][12] = (unsigned char)ob[0xff];
	ffe_ob_buf[ffe_ob_n][13] = *(long *)(ob + 0x82);
	ffe_ob_buf[ffe_ob_n][14] = *(long *)(ob + 0x26);
	ffe_ob_buf[ffe_ob_n][15] = *(long *)(ob + 0x36);
	ffe_ob_n++;
}

/* FIRST-LIGHT DEBUG: entry logger for massgen's trace_funcs.txt injection.
 * Also snapshots mission slot 0's state machine (DATA_008561, stride 0x24 --
 * the FUNC_000054 spin hunt). Register-transparent at the call site (the
 * injected MOVEM saves everything). */
void FfeEnterLog (long num)
{
	extern long mission_slots[] __asm__("DATA_008561");
	static int n;
	static int armed;
	FILE *f;

	/* ARMING: nothing logs until FUNC_000147 (new-game init) runs once, so
	 * per-frame render callbacks can be traced without the intro flooding
	 * the 3000-entry budget. Remove with the rest of the debug rig. */
	/* arm the VBlank PC sampler at the catch-up storm's known entry: the
	 * first fuel-tick (FUNC_000929) of the interactive loop -- the last
	 * traced entry before every observed silence */
	extern short ffe_samp_arm_flag __asm__("_ffe_samp_armed");

	if (num == 147) armed = 1;
	/* ARM AT THE NEW GAME (2026-08-02). The old trigger was FUNC_000929, the
	 * first fuel tick of the interactive loop, which was right for profiling
	 * the module-time catch-up. The question now is different: with the
	 * object allocator fixed the init itself stops producing frames after the
	 * '1' key, so the sampler has to be running THROUGH the init. FUNC_000147
	 * is the new-game entry and is already entry-traced. */
	if (num == 147) ffe_samp_arm_flag = 1;

	/* DEBUG SHORTCUT, NOT A FIX -- now OFF by default (2026-08-02).
	 * FUNC_000147 deliberately sets the game epoch [DATA_008804] = 0x80000000
	 * with [DATA_008808] = 0, so the first interactive frame runs FFE's
	 * module-time catch-up over (2^31)/0xc22e = 43,207 units. On DOS-class
	 * x86 that is a blink; on the emulated 68020 it is many minutes. Setting
	 * the "last processed" clock equal to the epoch makes the catch-up zero,
	 * which starts the universe UNSIMULATED -- wrong for play, and it was
	 * exactly right for the one question it was written for: does the 3D
	 * viewport draw the star system now that it exists. That question is
	 * ANSWERED (build\FIRST-3D-2026-08-02.png), so the shortcut goes off:
	 * while it is on, this build and the PC reference are at different
	 * simulated times and their frames cannot be compared pixel for pixel.
	 * Applied at FUNC_000152's ENTRY, not FUNC_000147's: 147 writes the epoch
	 * itself, so a write from 147's entry hook is immediately overwritten.
	 * 152 is the catch-up (edi = ([8804]-[8808])/0xc22e, ffe.asm:68615), so
	 * levelling the two clocks there makes its count zero every frame.
	 * Set FFE_LEVEL_CLOCKS to 1 to get it back for a quick visual run. */
#define FFE_LEVEL_CLOCKS 0
#if FFE_LEVEL_CLOCKS
	if (num == 152) {
		extern long clk_now  __asm__("DATA_008804");
		extern long clk_last __asm__("DATA_008808");

		clk_last = clk_now;
	}
#endif
	if (!armed) return;
	/* GATED ON THE F3 ACTION (2026-08-02). Chasing FAULT 3 -- pressing F3
	 * hard-resets the machine -- the useful trace is the SHORT one that starts
	 * at the keypress and is COMPLETE. Thinning (the old "first 200 in full,
	 * then every 1000th") blurs exactly the tail that names the crash, and
	 * logging everything from the new game on is far too slow: every entry
	 * costs an emulated fopen/fclose.
	 * So: write nothing until DATA_009158 (the GUI's last-action byte) reads
	 * the function-key action, then write EVERY entry. The reset arrives a
	 * few hundred calls later, so the file stays small and the last line is
	 * the real last line.
	 * Set FFE_TRACE_GATE to 0 for the old behaviour. */
/* GATE OFF (2026-08-02, evening): the developer's own screenshot of the LANDED
 * COCKPIT -- no F3 involved -- shows the viewport as black with long VERTICAL
 * white streaks. A vertical streak is a polygon whose X is sane and whose Y
 * spans far past the 158-row viewport, which is the exact signature the
 * FUNC_001560 bug produced this morning (SPANS ymin=-93 ymax=3744 against a
 * 158-row array). So the interesting frames are the ordinary cockpit ones and
 * the 0xf3 gate would hide every one of them. Trace FUNC_001613 only and let
 * the SPANS detail below print ymin/ymax. */
#define FFE_TRACE_GATE 0
#if FFE_TRACE_GATE
	{
		extern unsigned char gui_last_action __asm__("DATA_009158");
		static int gate_open;

		if (!gate_open) {
			if (gui_last_action != FFE_TRACE_GATE) return;
			gate_open = 1;
		}
	}
	if (n >= 4000) return;
#else
	/* HARD CAP, deliberately small. DrawSpans runs many times per frame and
	 * this logger does a fopen/fprintf/fclose per call; R22 proved an
	 * uncapped probe inside the render moves the crash somewhere else
	 * entirely, i.e. it measures itself. 60 entries is plenty to see whether
	 * ymin/ymax are sane. */
	if (n >= 60) return;
#endif
	if (n == 0) {
		/* one MAP line: runtime addresses of known symbols pin the
		 * load base exactly, so every ret= resolves through the link
		 * map with no ambiguity */
		extern char map_a[] __asm__("FUNC_000286");
		extern char map_b[] __asm__("FUNC_001682");
		extern char map_c[] __asm__("FUNC_000938");
		FILE *g = ffe_logopen ("PROGDIR:ffe_enter.log", "a");
		if (g != NULL) {
			fprintf (g, "MAP FUNC_000286=%lx FUNC_001682=%lx "
			         "FUNC_000938=%lx\n", (unsigned long)map_a,
			         (unsigned long)map_b, (unsigned long)map_c);
			fclose (g);
		}
	}
	f = ffe_logopen ("PROGDIR:ffe_enter.log", "a");
	if (f == NULL) return;
	{
		/* The injected MOVEM pushed d0-d7/a0-a6 (60 bytes) and then #num,
		 * so the traced function's own stack -- return address, then its
		 * cdecl arguments -- sits right behind them. The return address
		 * names the exact CALL SITE of every entry. */
		unsigned long *caller = (unsigned long *)((char *)&num + 4 + 60);
		fprintf (f, "%d FUNC_%06ld ret=%lx args=%lx,%lx,%lx,%lx "
		         "slot0=%lx,%lx\n", n, num, caller[0], caller[1],
		         caller[2], caller[3], caller[4],
		         mission_slots[0], mission_slots[1]);
		/* DRAWSPANS DETAIL (2026-08-02). The trace now ends INSIDE
		 * FUNC_001613_DrawSpans and the machine hard-resets there with no
		 * exception -- the signature of an unbounded write. DrawSpans reads a
		 * per-row crossing COUNT at ctx+4+44*y (ffe.asm:96694-96696) and
		 * bubble-sorts that many longs in place; a garbage count walks off the
		 * row record and scribbles over memory. It also picks the UNCLIPPED
		 * FUNC_001601_DrawHLine when ctx+0x1b28 is zero, so a bad span writes
		 * straight past the framebuffer. Log the two row bounds, the clip
		 * flag, and the counts of the rows it is about to walk. */
		if (num == 1613) {
			const char *ctx = (const char *)caller[1];
			long ymin, ymax, clip, k;

			if ((unsigned long)ctx > 0x1000 && !((unsigned long)ctx & 1)) {
				clip = *(const long *)(ctx + 0x1b28);
				ymin = *(const long *)(ctx + 0x1b2c);
				ymax = *(const long *)(ctx + 0x1b30);
				fprintf (f, "   SPANS clip=%ld ymin=%ld ymax=%ld counts:",
				         clip, ymin, ymax);
				/* the COUNT is at ctx+44*y; the crossings follow at +4
				 * (ffe.asm:96686-96696). 44*157+4 = 6912, just inside the
				 * 0x1b28 field, so a row index outside 0..157 is already
				 * proof of the fault -- print and stop rather than follow
				 * it into whatever lies past the record. */
				for (k = ymax - 1; k >= ymin && k >= ymax - 8; k--) {
					if (k < 0 || k > 157) { fprintf (f, " y%ld=OUT", k); break; }
					fprintf (f, " %ld", *(const long *)(ctx + 44 * k));
				}
				fprintf (f, "\n");
			} else {
				fprintf (f, "   SPANS ctx=%lx is not a usable pointer\n",
				         (unsigned long)ctx);
			}
		}
	}
	fclose (f);
	n++;
}

/* FIRST-LIGHT DEBUG: the crash reporter the trap catcher (ffe_trap.s) RTEs
 * into. Runs in USER mode in the task's own context, so file I/O is legal.
 * The globals were filled in supervisor mode at the moment of the fault. */
extern void FfeTrapCode (void) __asm__("_FfeTrapCode");
extern unsigned long ffe_crash_num __asm__("_ffe_crash_num");
extern unsigned long ffe_crash_pc  __asm__("_ffe_crash_pc");
extern unsigned short ffe_crash_sr  __asm__("_ffe_crash_sr");
extern unsigned short ffe_crash_fmt __asm__("_ffe_crash_fmt");

/* FIRST-LIGHT DEBUG: the VBlank PC sampler's reporter. The sampler diverts
 * the game task here after 3000 in-storm samples; the loop being profiled is
 * abandoned on purpose. Runs in user mode -- file I/O is legal. */
extern void FfeSampInstall (void) __asm__("_FfeSampInstall");
extern short ffe_samp_armed __asm__("_ffe_samp_armed");
extern long  ffe_samp_n     __asm__("_ffe_samp_n");
extern unsigned long ffe_samp_buf[3000] __asm__("_ffe_samp_buf");

void FfeSampDump (void) __asm__("_FfeSampDump");
void FfeSampDump (void)
{
	FILE *f = ffe_logopen ("PROGDIR:ffe_samples.log", "w");
	if (f) {
		extern char map_a[] __asm__("FUNC_000286");
		extern char map_b[] __asm__("FUNC_001682");
		extern char map_c[] __asm__("FUNC_000938");
		long i;
		fprintf (f, "MAP FUNC_000286=%lx FUNC_001682=%lx FUNC_000938=%lx\n",
		         (unsigned long)map_a, (unsigned long)map_b,
		         (unsigned long)map_c);
		for (i = 0; i < ffe_samp_n; i++)
			fprintf (f, "%lx\n", ffe_samp_buf[i]);
		fclose (f);
	}
	for (;;) ;	/* the diverted loop cannot be resumed -- park here */
}

void FfeCrashDie (void) __asm__("_FfeCrashDie");
void FfeCrashDie (void)
{
	extern unsigned long ffe_crash_frame[12] __asm__("_ffe_crash_frame");
	FILE *f = fopen ("PROGDIR:ffe_crash.log", "a");
	if (f) {
		int i;
		/* THE CODE-HUNK ANCHOR, and it belongs HERE rather than only on the
		 * sampler path. Without it a crash PC cannot be resolved at all, and
		 * the tempting substitute -- anchoring on a DATA_ symbol printed by
		 * some other probe -- is WRONG: BSS is a separate AmigaDOS hunk with
		 * its own load address, so a BSS anchor gives a code base that is off
		 * by the gap between the hunks. Done once on 2026-08-05, it resolved a
		 * divide-by-zero into FUNC_CalcKeyInput, a function whose only
		 * arithmetic is a MUL -- the wrong answer was caught only because that
		 * check was made. Three CODE anchors, exactly as resolve_samples.py
		 * demands, so a disagreement between them is visible instead of
		 * silently averaged away. */
		{
			extern char map_a[] __asm__("FUNC_000286");
			extern char map_b[] __asm__("FUNC_001682");
			extern char map_c[] __asm__("FUNC_000938");

			fprintf (f, "MAP FUNC_000286=%lx FUNC_001682=%lx FUNC_000938=%lx\n",
			         (unsigned long)map_a, (unsigned long)map_b,
			         (unsigned long)map_c);
		}
		fprintf (f, "TRAP %lu at PC=%08lx SR=%04x fmt/vec=%04x\nframe:",
		         ffe_crash_num, ffe_crash_pc,
		         (unsigned)ffe_crash_sr, (unsigned)ffe_crash_fmt);
		for (i = 0; i < 12; i++)
			fprintf (f, " %08lx", ffe_crash_frame[i]);
		{
			/* the USER-stack snapshot: print every long that could be a
			 * return address into our own code, which is what names the
			 * caller of the wild jump. The program loads around 0x1000_0000
			 * on this setup, so anything in 0x1000_0000..0x1080_0000 is
			 * worth resolving against ffe_game.symmap. */
			extern unsigned long ffe_crash_ustk[16] __asm__("_ffe_crash_ustk");
			int k;

			fprintf (f, "\nustk:");
			for (k = 0; k < 16; k++)
				fprintf (f, " %08lx", ffe_crash_ustk[k]);
		}
		fprintf (f, "\n");
		{
			extern long ffe_obr_num[], ffe_obr_ret[], ffe_obr_obj[], ffe_obr_a2[];
			extern long ffe_obr_x[], ffe_obr_y[], ffe_obr_z[], ffe_obr_total;
			long q, first = ffe_obr_total > OBR_MAX ? ffe_obr_total - OBR_MAX : 0;

			fprintf (f, "OBJRING@DIE total=%ld  (num ret obj a2 pos)\n", ffe_obr_total);
			for (q = first; q < ffe_obr_total; q++) {
				long k = q % OBR_MAX;

				fprintf (f, "  #%ld f=%ld ret=%08lx obj=%08lx a2=%08lx pos=(%ld,%ld,%ld)\n",
				         q, ffe_obr_num[k], (unsigned long)ffe_obr_ret[k],
				         (unsigned long)ffe_obr_obj[k], (unsigned long)ffe_obr_a2[k],
				         ffe_obr_x[k], ffe_obr_y[k], ffe_obr_z[k]);
			}
		}
		{
			extern long ffe_va_n, ffe_va_total, ffe_va_idx[], ffe_va_obj[], ffe_va_flag[];
			extern long ffe_va_out[][3]; extern unsigned char ffe_va_desc[][6];
			long q;

			fprintf (f, "VTXANOM n=%ld total=%ld\n", ffe_va_n, ffe_va_total);
			for (q = 0; q < ffe_va_n; q++)
				fprintf (f, "  [%ld] idx=%ld obj=%08lx flag=%ld desc=%02x %02x %02x %02x %02x %02x out=(%ld,%ld,%ld)\n",
				         q, ffe_va_idx[q], (unsigned long)ffe_va_obj[q], ffe_va_flag[q],
				         ffe_va_desc[q][0], ffe_va_desc[q][1], ffe_va_desc[q][2],
				         ffe_va_desc[q][3], ffe_va_desc[q][4], ffe_va_desc[q][5],
				         ffe_va_out[q][0], ffe_va_out[q][1], ffe_va_out[q][2]);
		}
		/* The last FUNC_001470 calls before the death, oldest first: the node
		 * index, the PARENT object pointer and its position (ctx), and the
		 * node's own transformed offset (off). The centre FUNC_001876 divides
		 * over is ctx+off, so whichever half carries the all-ones pattern is
		 * the half to chase. `off=?' means the read-back never happened. */
		{
			extern long ffe_s70_idx[], ffe_s70_ctx[], ffe_s70_rec[];
			extern long ffe_s70_cx[], ffe_s70_cy[], ffe_s70_cz[];
			extern long ffe_s70_ox[], ffe_s70_oy[], ffe_s70_oz[];
			extern long ffe_s70_done[], ffe_s70_n, ffe_s70_total;
			long q, first = ffe_s70_total > S70_MAX ? ffe_s70_total - S70_MAX : 0;

			fprintf (f, "NODE1470@DIE total=%ld\n", ffe_s70_total);
			for (q = first; q < ffe_s70_total; q++) {
				long k = q % S70_MAX;

				fprintf (f, "  #%ld idx=%ld parent=%08lx pos=(%ld,%ld,%ld) off=",
				         q, ffe_s70_idx[k], (unsigned long)ffe_s70_ctx[k],
				         ffe_s70_cx[k], ffe_s70_cy[k], ffe_s70_cz[k]);
				if (ffe_s70_done[k])
					fprintf (f, "(%ld,%ld,%ld)", ffe_s70_ox[k], ffe_s70_oy[k], ffe_s70_oz[k]);
				else if (ffe_s70_rec[k] > 0x1000)
					fprintf (f, "(%ld,%ld,%ld)*", *(long *)(ffe_s70_rec[k] + 0x18),
					         *(long *)(ffe_s70_rec[k] + 0x1c), *(long *)(ffe_s70_rec[k] + 0x20));
				else
					fprintf (f, "?");
				fprintf (f, " rec=%08lx\n", (unsigned long)ffe_s70_rec[k]);
			}
		}
		/* The triangle ring, oldest first. The LAST line is the record the
		 * divide died on; the ones above it are the positive control, and if
		 * they are zero too then the probe is wrong rather than the game. */
		if (ffe_tri_pos) {
			long k, first = ffe_tri_pos > TRI_RING ? ffe_tri_pos - TRI_RING : 0;

			fprintf (f, "TRI n=%ld null=%ld\n", ffe_tri_pos, ffe_tri_null);
			for (k = first; k < ffe_tri_pos; k++) {
				long *r = ffe_tri_ring[k & (TRI_RING - 1)];

				fprintf (f, "TRI[%ld] sh=%ld det74=%ld det7c=%ld "
				            "P0=(%ld,%ld,%ld) P1=(%ld,%ld,%ld) "
				            "P2=(%ld,%ld,%ld)%s\n",
				         k, r[0], r[1], r[2],
				         r[3], r[4], r[5], r[6], r[7], r[8],
				         r[9], r[10], r[11],
				         k == ffe_tri_pos - 1 ? "   <== SMIERC" : "");
				fprintf (f, "TRIP[%ld] p0=%08lx p1=%08lx p2=%08lx "
				            "src54=%08lx src5c=%08lx  %s\n",
				         k, (unsigned long)r[12], (unsigned long)r[13],
				         (unsigned long)r[14], (unsigned long)r[15],
				         (unsigned long)r[16],
				         (r[12] == r[13] && r[13] == r[14])
				             ? "WSKAZNIKI ROWNE -> wina jest w WYBORZE rekordu"
				             : "wskazniki rozne -> wina jest w TRESCI rekordow");
			}
		} else {
			fprintf (f, "TRI n=0 -- sonda nie wystrzelila ANI RAZU, wiec ten "
			            "krach nie jest tym dzieleniem albo wstrzykniecie "
			            "wypadlo z build\\xlat-full\\FUNC_001876.s\n");
		}
		fclose (f);
	}
	exit (13);
}

static char *ffe_trap_task;
static void *ffe_trap_old;
static void ffe_trap_restore (void)
{
	if (ffe_trap_task) *(void **)(ffe_trap_task + 50) = ffe_trap_old;
}

/* OWN STACK (2026-10-23, release): the game used to need "Stack 1000000" in
 * the Shell; started with the default 4 KB, or from an icon, it would run off
 * its stack. This runtime is newlib, which has no __stack/swapstack, so main()
 * switches to a 1 MB stack itself with exec's StackSwap (LVO -732). The stack
 * is BSS, so it is freed with the program. exit() from inside the game skips
 * the swap back (crt0 restores its saved SP directly), so an atexit handler
 * puts the task's tc_SPLower/tc_SPUpper (Task offsets 58/62) back first -
 * otherwise the Shell process would keep bounds pointing into freed memory. */
#define FFE_STACK_BYTES 1000000
static unsigned long ffe_stack[FFE_STACK_BYTES / 4] __attribute__ ((aligned (4)));
static struct { void *lower; unsigned long upper; void *pointer; } ffe_sss;
static char *ffe_stack_task;
static unsigned long ffe_stack_bounds[2];
extern int ffe_call_on_stack (void *sss, int (*fn) (void));
__asm__ (
	"	.text\n"
	"	.even\n"
	"	.globl	_ffe_call_on_stack\n"
	"_ffe_call_on_stack:\n"
	"	movem.l	d2-d7/a2-a6,-(sp)\n"
	"	move.l	48(sp),a2\n"		/* sss: 11 registers + return address */
	"	move.l	52(sp),a3\n"		/* fn */
	"	move.l	a2,a0\n"
	"	move.l	4.w,a6\n"
	"	jsr	-732(a6)\n"		/* StackSwap: now on ffe_stack */
	"	jsr	(a3)\n"
	"	move.l	d0,d2\n"
	"	move.l	a2,a0\n"
	"	move.l	4.w,a6\n"
	"	jsr	-732(a6)\n"		/* and back */
	"	move.l	d2,d0\n"
	"	movem.l	(sp)+,d2-d7/a2-a6\n"
	"	rts\n");
static void ffe_stack_bounds_restore (void)
{
	*(unsigned long *)(ffe_stack_task + 58) = ffe_stack_bounds[0];
	*(unsigned long *)(ffe_stack_task + 62) = ffe_stack_bounds[1];
}

static int ffe_main (void);

int main (void)
{
	char *task;
	__asm__ volatile ("move.l 4.w,a6\n\tsuba.l a1,a1\n\t"
	                  "jsr (-294,a6)\n\tmove.l d0,%0"
	                  : "=g"(task) :: "a6", "a1", "d0", "d1", "a0", "cc",
	                    "memory");
	ffe_stack_task = task;
	ffe_stack_bounds[0] = *(unsigned long *)(task + 58);
	ffe_stack_bounds[1] = *(unsigned long *)(task + 62);
	atexit (ffe_stack_bounds_restore);
	ffe_sss.lower = ffe_stack;
	ffe_sss.upper = (unsigned long)ffe_stack + sizeof ffe_stack;
	ffe_sss.pointer = (void *)ffe_sss.upper;
	return ffe_call_on_stack (&ffe_sss, ffe_main);
}

static int ffe_main (void)
{
	{
		/* soft-run protocol (ffe_watchdog.c): wait while Work:ffe_hold exists */
		extern void ffe_hold_wait (void);
		ffe_hold_wait ();
		{ extern void ffe_boot_mark (const char *); ffe_boot_mark ("main (after hold)"); }
	}
	/* install the crash catcher: tc_TrapCode sits at offset 50 of struct
	 * Task (exec/tasks.h: 14-byte Node, 4 flag bytes, 4 signal longs, 2
	 * trap words, ExceptData/ExceptCode/TrapData = 50). Written directly
	 * to keep exec headers out of the probe; FindTask(NULL) is LVO -294. */
	{
		char *task;
		__asm__ volatile ("move.l 4.w,a6\n\tsuba.l a1,a1\n\t"
		                  "jsr (-294,a6)\n\tmove.l d0,%0"
		                  : "=g"(task) :: "a6", "a1", "d0", "d1", "a0", "cc",
		                    "memory");
		ffe_trap_task = task;
		ffe_trap_old = *(void **)(task + 50);
		*(void **)(task + 50) = (void *)FfeTrapCode;
		atexit (ffe_trap_restore);	/* the shell's task must not keep our handler */
	}
	/* OPEN THE IEEE MATHS LIBRARIES (2026-08-02). This is the root cause of
	 * the crash that ended every render once the star system was populated.
	 *
	 * `__divdf3` in libm020/libc.a is not soft-float code at all -- it is a
	 * 36-byte stub that loads MathIeeeDoubBasBase into a6 and does
	 * `jsr (-84,a6)`, i.e. it calls mathieeedoubbas.library/IEEEDPDiv. The
	 * same holds for __adddf3 and friends, and the link needs three bases:
	 * MathIeeeDoubBasBase, MathIeeeDoubTransBase and MathIeeeSingBasBase.
	 * NOTHING opened them, so a6 held whatever was in the uninitialised
	 * global and the first double DIVISION jumped into the void -- recorded
	 * by the crash catcher as `TRAP 11 at PC=00fca0c0` (line-F, inside
	 * Kickstart ROM) with the user stack showing FUNC_001877+0x142 ->
	 * __divdf3+0x1c.
	 *
	 * Why it only bit today: the only double arithmetic in the whole program
	 * is in the hand-written x87 shims, and the biggest of them --
	 * FUNC_001877, the planet limb -- never ran until the object allocator
	 * was fixed and the star system finally had planets in it.
	 *
	 * OpenLibrary is LVO -552; the bases are libc's own globals, so filling
	 * them is all the stubs need. Kickstart 3.1 has all three in ROM, so
	 * this cannot fail on the target machine, and a NULL base is left NULL
	 * (the crash catcher then still names the caller).
	 */
	{
		extern void *MathIeeeDoubBasBase;
		extern void *MathIeeeDoubTransBase;
		extern void *MathIeeeSingBasBase;
		static const char *const names[3] = {
			"mathieeedoubbas.library",
			"mathieeedoubtrans.library",
			"mathieeesingbas.library"
		};
		void **bases[3] = { &MathIeeeDoubBasBase, &MathIeeeDoubTransBase,
		                    &MathIeeeSingBasBase };
		int i;

		for (i = 0; i < 3; i++) {
			void *lib;
			const char *nm = names[i];

			__asm__ volatile ("move.l 4.w,a6\n\tmove.l %1,a1\n\t"
			                  "moveq #0,d0\n\tjsr (-552,a6)\n\tmove.l d0,%0"
			                  : "=g"(lib) : "g"(nm)
			                  : "a6", "a1", "d0", "d1", "a0", "cc", "memory");
			if (lib) *bases[i] = lib;
			{
				FILE *lf = ffe_logopen ("PROGDIR:ffe_game.log", "a");

				if (lf) {
					fprintf (lf, "MATHLIB %s -> %lx (base now %lx)\n", nm,
					         (unsigned long)lib, (unsigned long)*bases[i]);
					fclose (lf);
				}
			}
		}
	}
	FfeSampInstall ();	/* FIRST-LIGHT DEBUG: VBlank PC sampler */
	{
		/* soft restart: the vector must not outlive the program */
		extern void FfeSampRemove (void) __asm__("_FfeSampRemove");
		atexit (FfeSampRemove);
	}
	{
		/* HANG WATCHDOG (2026-09-25) -- amigafeapife_watchdog.c */
		extern void ffe_watchdog_start (void);
		ffe_watchdog_start ();
	}
	/* 2026-09-25: arm it from the START when PROGDIR:ffe_sample exists, for a
	 * silent hang in the intro (the new-game trigger in FfeEnterLog never
	 * fires there). 3000 VBlanks = 60 s, then the task is diverted to
	 * FfeSampDump and PROGDIR:ffe_samples.log is written. */
	{
		extern short ffe_samp_start_flag __asm__("_ffe_samp_armed");
		FILE *sf = fopen ("PROGDIR:ffe_sample", "r");

		extern long ffe_samp_limit_v __asm__("_ffe_samp_limit");

		ffe_samp_limit_v = 3000;
		if (sf != NULL) {
			/* the file holds the ARMING PRESENT (0 = from the start); the
			 * limit drops to 500 samples (10 s) so a hang is caught while it
			 * lasts. Armed from VideoBlit via ffe_samp_arm_at. */
			extern long ffe_samp_arm_at;
			if (fscanf (sf, "%ld", &ffe_samp_arm_at) != 1) ffe_samp_arm_at = 0;
			fclose (sf);
			ffe_samp_limit_v = 500;
			if (ffe_samp_arm_at == 0) ffe_samp_start_flag = 1;
		}
	}

	/* F3 CRASH, BISECT BY OPCODE (2026-08-02). Thirteen runs of call tracing
	 * (R17..R30) walked the chain down twelve levels and every component
	 * measured clean, so this partitions the problem instead of narrowing it
	 * further. Model opcode 4 is the 'non-reflected quad' handler and the F3
	 * trace runs through it; pointing DATA_007816[4] at index 0's handler
	 * FUNC_001754 makes the interpreter skip quads entirely.
	 *   F3 SURVIVES  -> the fault is inside the quad path, and only
	 *                   FUNC_001711/001712/001692/001695/001777/001778 remain.
	 *   F3 STILL DIES -> the quad is a red herring and the whole trace was
	 *                   following the wrong branch.
	 * Patched here at runtime rather than in build\data-be\ffedat_be.s, which
	 * convert_static regenerates. DEBUG ONLY -- comes out with the rest of the
	 * rig; set to 0 to restore the real handler. */
#define FFE_BISECT_QUAD 0
#if FFE_BISECT_QUAD
	{
		extern void *opcode_tab[] __asm__("DATA_007816");
		extern char quad_h __asm__("FUNC_001711");
		extern char noop_h __asm__("FUNC_001754");
		FILE *bf = ffe_logopen ("PROGDIR:ffe_game.log", "a");

		if (bf) {
			fprintf (bf, "BISECT: opcode_tab[4] %lx (FUNC_001711=%lx) -> "
			         "FUNC_001754=%lx\n", (unsigned long)opcode_tab[4],
			         (unsigned long)&quad_h, (unsigned long)&noop_h);
			fflush (bf);
			fclose (bf);
		}
		opcode_tab[4] = (void *)&noop_h;
	}
#endif
	/* APPEND, never truncate: a Guru reboots the machine, the startup script
	 * relaunches ffe_game, and a "w" here would destroy the crashed run's
	 * evidence. The harness deletes the logs before each launch instead. */
	FILE *f = ffe_logopen ("PROGDIR:ffe_game.log", "a");
	if (f) {
		fprintf (f, "ffe_game: first light REC-R60 -- calling _asmmain\n");
		fflush (f);
		fclose (f);
	}
	{ extern void ffe_boot_mark (const char *); ffe_boot_mark ("calling _asmmain"); }
	asmmain_entry ();
	f = log_open ();
	if (f) {
		fprintf (f, "ffe_game: _asmmain returned cleanly\n");
		fflush (f);
	}
	return 0;
}









/* ---------------------------------------------------------------------------
 * COLOUR BYTE-ORDER CENSUS (2026-09-01), for the green-background fault.
 *
 * FUNC_001326_DPal32to16 reads its argument as three BYTES off the stack:
 *   mov al,[ebp+0x8] / mov dl,[ebp+0x9] / mov dl,[ebp+0xa]   (ffe.asm:82765-73)
 * On x86 those are the dword's bits 0..7 / 8..15 / 16..23, so a producer builds
 * 0x00BBGGRR and the TOP byte is the unused one. On the 68k the same three
 * addresses are bits 31..24 / 23..16 / 15..8, so a correct producer must build
 * 0xRRGGBB00 and the BOTTOM byte is the unused one.
 *
 * So the two conventions are distinguishable from the VALUE ALONE, with no
 * reference run needed: count how many arguments have a zero top byte against
 * how many have a zero bottom byte. A producer still in x86 order makes R read
 * as 0x00 -- which is exactly the symptom, a background whose red and blue
 * collapse while green survives.
 *
 * Deliberately NOT a first-N window: the first sixteen calls are the startup
 * palette build and say nothing about the background fill (CLAUDE.md, "trigger
 * on the anomaly"). Counters only, no I/O in the hot path -- this runs ~12,000
 * times per window and a printing probe once stopped the game reaching its own
 * crash. Callers are bucketed by return address so a bad producer can be named.
 */
#define FFE_CC_CALLERS 8
long ffe_cc_calls;          /* every call */
long ffe_cc_hi0;            /* (D >> 24) == 0  -- looks x86-ordered */
long ffe_cc_lo0;            /* (D & 0xff) == 0 -- looks 68k-ordered */
long ffe_cc_both0;          /* both ends zero: says nothing, kept honest */
long ffe_cc_neither;        /* neither zero: not a packed RGB at all */
long ffe_cc_ret[FFE_CC_CALLERS];    /* distinct caller return addresses */
long ffe_cc_cnt[FFE_CC_CALLERS];
long ffe_cc_hi0_by[FFE_CC_CALLERS]; /* how many of THAT caller's look x86 */
long ffe_cc_sample[FFE_CC_CALLERS]; /* one verbatim argument per caller */
long ffe_cc_other;                  /* overflow bucket, so the split is checked */

void FfeColCensus (long num) __asm__("_FfeColCensus");
void FfeColCensus (long num)
{
	unsigned long *caller = (unsigned long *)((char *)&num + 4 + 60);
	unsigned long d = caller[1];
	long ret = (long)caller[0];
	int hi0 = ((d >> 24) & 0xffUL) == 0;
	int lo0 = (d & 0xffUL) == 0;
	int i;

	(void)num;
	ffe_cc_calls++;
	if (hi0 && lo0)      ffe_cc_both0++;
	else if (hi0)        ffe_cc_hi0++;
	else if (lo0)        ffe_cc_lo0++;
	else                 ffe_cc_neither++;

	for (i = 0; i < FFE_CC_CALLERS; i++) {
		if (ffe_cc_cnt[i] == 0) {
			ffe_cc_ret[i] = ret;
			ffe_cc_sample[i] = (long)d;
		}
		if (ffe_cc_ret[i] == ret) {
			ffe_cc_cnt[i]++;
			if (hi0 && !lo0) ffe_cc_hi0_by[i]++;
			return;
		}
	}
	ffe_cc_other++;
}

/* OBJECT-POSITION RING (2026-09-02). Entry-only, generic: for any function
 * whose ARG1 is an object record with the position at +4/+8/+0xc, record the
 * function number, the caller's return address, the object pointer and the
 * three longs. Circular; the last entries before the renderer's divide are
 * printed from FfeCrashDie. Built to name WHO hands FUNC_001874 an object
 * whose position is the (0x1FFFFF,0x5FFFFF,..) all-ones pattern: the ring at
 * FUNC_001470 already proved it is the PARENT object's position, not the
 * node offset, and the 64-bit producers (879/819/817/826/581/823) count ZERO
 * in the intro, so the writer is somewhere on the 1683 -> 1772 -> 1874 path. */
long ffe_obr_num[OBR_MAX], ffe_obr_ret[OBR_MAX], ffe_obr_obj[OBR_MAX];
long ffe_obr_x[OBR_MAX], ffe_obr_y[OBR_MAX], ffe_obr_z[OBR_MAX];
long ffe_obr_a2[OBR_MAX], ffe_obr_total;

void FfeObjRing (long num) __asm__("_FfeObjRing");
void FfeObjRing (long num)
{
	unsigned long *caller = (unsigned long *)((char *)&num + 4 + 60);
	unsigned long obj = caller[1];
	long k = ffe_obr_total % OBR_MAX;

	ffe_obr_total++;
	ffe_obr_num[k] = num;
	ffe_obr_ret[k] = (long)caller[0];
	ffe_obr_obj[k] = (long)obj;
	ffe_obr_a2[k]  = (long)caller[2];
	if (obj > 0x1000 && !(obj & 1)) {
		ffe_obr_x[k] = *(const long *)(obj + 0x4);
		ffe_obr_y[k] = *(const long *)(obj + 0x8);
		ffe_obr_z[k] = *(const long *)(obj + 0xc);
	} else
		ffe_obr_x[k] = ffe_obr_y[k] = ffe_obr_z[k] = 0;
}

/* VERTEX-EVALUATOR ANOMALY TRAP (2026-09-02). FUNC_001478(obj, idx, flag)
 * fills node idx's own offset at arena[idx]+0x18/+0x1c/+0x20. The crash's
 * parent position decomposes EXACTLY as parent(0,0,85023) + node offset
 * (0x1FFFFF, 0x5FFFFF, 0x177FFFF) = (N*2^21 - 1) for N = 1, 3, 188 -- a
 * 64-bit result one short of N*2^32, then >> 11. This trap catches the call
 * that produces it: on every entry the PREVIOUS call's output is read back
 * (the arena node is only written once 1478 returns), and if any component
 * has its low 21 bits all set it is kept with the node index, the object and
 * the 6-byte vertex descriptor at [obj+0x150] + (idx>>1)*6, which names the
 * vertex TYPE and so the handler inside 1478 that computed it. Anomaly-
 * triggered, not first-N: the producing call happens long before the death. */
#define VA_MAX 8
long ffe_va_n, ffe_va_total;
long ffe_va_idx[VA_MAX], ffe_va_obj[VA_MAX], ffe_va_flag[VA_MAX];
long ffe_va_out[VA_MAX][3];
unsigned char ffe_va_desc[VA_MAX][6];
static long va_prev_rec, va_prev_idx, va_prev_obj, va_prev_flag;
static unsigned char va_prev_desc[6];

void FfeVtxAnom (long num) __asm__("_FfeVtxAnom");
void FfeVtxAnom (long num)
{
	unsigned long *caller = (unsigned long *)((char *)&num + 4 + 60);
	unsigned long obj = caller[1];
	long idx = (long)(signed char)(caller[2] & 0xff);
	long flag = (long)caller[3];
	extern char nodebase[] __asm__("DATA_009200");
	unsigned long base = *(const unsigned long *)nodebase;
	int i;

	(void)num;
	ffe_va_total++;
	if (va_prev_rec > 0x1000 && ffe_va_n < VA_MAX) {
		long o[3];
		int hit = 0;

		for (i = 0; i < 3; i++) {
			o[i] = *(const long *)(va_prev_rec + 0x18 + 4 * i);
			if ((o[i] & 0x1FFFFFL) == 0x1FFFFFL) hit = 1;
		}
		if (hit) {
			int k = ffe_va_n++;

			ffe_va_idx[k] = va_prev_idx; ffe_va_obj[k] = va_prev_obj;
			ffe_va_flag[k] = va_prev_flag;
			for (i = 0; i < 3; i++) ffe_va_out[k][i] = o[i];
			for (i = 0; i < 6; i++) ffe_va_desc[k][i] = va_prev_desc[i];
		}
	}
	va_prev_rec = 0;
	if (obj > 0x1000 && !(obj & 1) && base > 0x1000) {
		unsigned long tab = *(const unsigned long *)(obj + 0x150);
		unsigned long d = tab + (unsigned long)((idx >> 1) * 6);

		va_prev_rec = (long)(base + (unsigned long)(idx * 36));
		va_prev_idx = idx; va_prev_obj = (long)obj; va_prev_flag = flag;
		for (i = 0; i < 6; i++)
			va_prev_desc[i] = (tab > 0x1000) ? *(const unsigned char *)(d + i) : 0;
	}
}

/* GATE1745 INPUTS (2026-09-02). FUNC_001745 culls a sub-model descent when
 * esi = (arg1+arg2)*36 + arg3*8 + 0xc + [DATA_007823] > 0x8000. MODREL proved
 * we descend into 3 sub-models the reference culls, and the emitted gate
 * arithmetic is correct, so the divergence is in these inputs. Record the
 * DISTINCT input tuples (entry only; the gate's own esi recomputed here from
 * the same formula) so the set diffs directly against the reference's PC1745. */
#define G45_MAX 64
long ffe_g45_a1[G45_MAX], ffe_g45_a2[G45_MAX], ffe_g45_a3[G45_MAX];
long ffe_g45_pool[G45_MAX], ffe_g45_esi[G45_MAX], ffe_g45_n, ffe_g45_total;

void FfeGate1745 (long num) __asm__("_FfeGate1745");
void FfeGate1745 (long num)
{
	unsigned long *caller = (unsigned long *)((char *)&num + 4 + 60);
	long a1 = (long)caller[1], a2 = (long)caller[2], a3 = (long)caller[3];
	extern long DATA_007823[] __asm__("DATA_007823");
	long pool = DATA_007823[0];
	long esi = (a1 + a2) * 36 + a3 * 8 + 0xc + pool;
	int i;

	(void)num;
	ffe_g45_total++;
	for (i = 0; i < ffe_g45_n; i++)
		if (ffe_g45_a1[i] == a1 && ffe_g45_a3[i] == a3 && ffe_g45_pool[i] == pool)
			return;
	if (ffe_g45_n >= G45_MAX) return;
	ffe_g45_a1[ffe_g45_n] = a1; ffe_g45_a2[ffe_g45_n] = a2; ffe_g45_a3[ffe_g45_n] = a3;
	ffe_g45_pool[ffe_g45_n] = pool; ffe_g45_esi[ffe_g45_n] = esi;
	ffe_g45_n++;
}

/* PALMAP (2026-09-25). Entry of FUNC_001327_DPal16toPal: the 12-bit colour
 * code (low 16 bits of arg 1, exactly what its `movzx edx,ax' takes) split by
 * the caller's return address, first 48 distinct (ret, code) pairs counted,
 * the rest in `other'. Built because the intro draws the logo, the ships and
 * the asteroids in index 0 where the reference uses the grey ramp 1..15, and
 * DATA_007770[0] = 1 is precisely "code 0 -> index 0". The reference's twin
 * is PCPALMAP in build\pcprobe_proj.c; compare CODES, not return addresses. */
#define PM_MAX 48
long ffe_pm_calls, ffe_pm_other;
long ffe_pm_ret[PM_MAX], ffe_pm_code[PM_MAX], ffe_pm_n[PM_MAX];
void FfePalMap (long num) __asm__("_FfePalMap");
void FfePalMap (long num)
{
	unsigned long *caller = (unsigned long *)((char *)&num + 4 + 60);
	long ret = (long)caller[0];
	long code = (long)(caller[1] & 0xffffUL);
	int i;

	(void)num;
	ffe_pm_calls++;
	for (i = 0; i < PM_MAX; i++) {
		if (ffe_pm_n[i] == 0) {
			ffe_pm_ret[i] = ret;
			ffe_pm_code[i] = code;
		}
		if (ffe_pm_ret[i] == ret && ffe_pm_code[i] == code) {
			ffe_pm_n[i]++;
			return;
		}
	}
	ffe_pm_other++;
}

/* RGBMAP (2026-09-25). Entry of FUNC_001326_DPal32to16: the low 24 bits of
 * its by-value RGB dword (r = bits 7..0, g = 15..8, b = 23..16 on x86, which
 * is exactly what its three byte reads take), split by return address, in a
 * 512-slot open-addressing hash (a linear table of 48 filled in the first
 * second). PALMAP shows WHICH consumer gets a bad code; this names the
 * PRODUCER. Twin: PCRGBMAP in build\pcprobe_proj.c. */
#define RM_MAX 512
long ffe_rm_calls, ffe_rm_other;
long ffe_rm_ret[RM_MAX], ffe_rm_val[RM_MAX], ffe_rm_n[RM_MAX];
void FfeRgbMap (long num) __asm__("_FfeRgbMap");
void FfeRgbMap (long num)
{
	unsigned long *caller = (unsigned long *)((char *)&num + 4 + 60);
	long ret = (long)caller[0];
	long v = (long)(caller[1] & 0xffffffUL);
	unsigned h = ((unsigned)ret * 7u + (unsigned)v * 131u) & (RM_MAX - 1);
	int k;

	(void)num;
	ffe_rm_calls++;
	for (k = 0; k < 16; k++, h = (h + 1) & (RM_MAX - 1)) {
		if (ffe_rm_n[h] == 0) {
			ffe_rm_ret[h] = ret;
			ffe_rm_val[h] = v;
		}
		if (ffe_rm_ret[h] == ret && ffe_rm_val[h] == v) {
			ffe_rm_n[h]++;
			return;
		}
	}
	ffe_rm_other++;
}

/* BLITMAP (2026-09-25). Entry of FUNC_001623_BlitIndex(buf, index, x, y):
 * distinct (index, x, y) with the bitmap header read the way the game reads
 * it (bytes at +0/+1, LE words at +2/+4/+6 -- ffebmp is raw x86 order), plus
 * the buffer descriptor's first dwords. Built because the intro's FIRST
 * ENCOUNTERS banner never appears although BlitIndex runs every frame and
 * BlitInternal never does. Twin: PCBLITMAP. */
#define BM_MAX 32
long ffe_bm_calls, ffe_bm_other;
long ffe_bm_idx[BM_MAX], ffe_bm_x[BM_MAX], ffe_bm_y[BM_MAX], ffe_bm_n[BM_MAX];
unsigned long ffe_bm_hdr[BM_MAX][2], ffe_bm_buf[BM_MAX][6];
extern unsigned long DATA_008240[] __asm__("DATA_008240");
void FfeBlitMap (long num) __asm__("_FfeBlitMap");
void FfeBlitMap (long num)
{
	unsigned long *caller = (unsigned long *)((char *)&num + 4 + 60);
	unsigned long *buf = (unsigned long *)caller[1];
	long idx = (long)caller[2], x = (long)caller[3], y = (long)caller[4];
	int i, k;

	(void)num;
	ffe_bm_calls++;
	for (i = 0; i < BM_MAX; i++) {
		if (ffe_bm_n[i] == 0) {
			const unsigned char *h = (const unsigned char *)DATA_008240[idx];
			ffe_bm_idx[i] = idx; ffe_bm_x[i] = x; ffe_bm_y[i] = y;
			ffe_bm_hdr[i][0] = ((unsigned long)h[0] << 24) | ((unsigned long)h[1] << 16)
			                 | (unsigned long)(h[2] | (h[3] << 8));
			ffe_bm_hdr[i][1] = ((unsigned long)(h[4] | (h[5] << 8)) << 16)
			                 | (unsigned long)(h[6] | (h[7] << 8));
			for (k = 0; k < 6; k++) ffe_bm_buf[i][k] = buf ? buf[k] : 0;
		}
		if (ffe_bm_idx[i] == idx && ffe_bm_x[i] == x && ffe_bm_y[i] == y) {
			ffe_bm_n[i]++;
			return;
		}
	}
	ffe_bm_other++;
}

/* TRI1808 (2026-09-25). Entry of FUNC_001808 (the textured-triangle draw
 * item): bucket the three screen points at item+4/+8/+0xc ({short x, short y}
 * in address order, as FUNC_001645 reads them) while the game clock is in the
 * Mars close-up window, where our frames show vertical "stalactites" and the
 * reference a smooth textured limb. Twin: PCTRI1808. Buckets per coordinate:
 * <-1000, -1000..-1, on screen, just past the edge (<1000), >=1000. */
long ffe_t8_n, ffe_t8_x[5], ffe_t8_y[5], ffe_t8_s[8][6];
extern long ffe_clock_for_probe __asm__("DATA_008804");
static int t8_bucket (long v, long lim)
{
	if (v < -1000) return 0;
	if (v < 0) return 1;
	if (v < lim) return 2;
	if (v < 1000) return 3;
	return 4;
}
void FfeTri1808 (long num) __asm__("_FfeTri1808");
void FfeTri1808 (long num)
{
	unsigned long *caller = (unsigned long *)((char *)&num + 4 + 60);
	const unsigned char *item = (const unsigned char *)caller[1];
	long clk = ffe_clock_for_probe;
	int k;

	(void)num;
	{
		extern void FfeFrame1808 (const unsigned char *);
		FfeFrame1808 (item);
	}
	if (clk < 1700000 || clk > 1950000) return;
	for (k = 0; k < 3; k++) {
		long x = *(const short *)(item + 4 + 4 * k);
		long y = *(const short *)(item + 6 + 4 * k);
		ffe_t8_x[t8_bucket (x, 320)]++;
		ffe_t8_y[t8_bucket (y, 158)]++;
	}
	if (ffe_t8_n < 8)
		for (k = 0; k < 3; k++) {
			ffe_t8_s[ffe_t8_n][2 * k] = *(const short *)(item + 4 + 4 * k);
			ffe_t8_s[ffe_t8_n][2 * k + 1] = *(const short *)(item + 6 + 4 * k);
		}
	ffe_t8_n++;
}

/* WINCNT (2026-09-25): entry counts inside the Mars close-up clock window
 * [1.70M, 1.95M], indexed by function number -- twin of PCWINCNT. */
long ffe_wc_cnt[2200];
long ffe_fc_cnt[2200];
#define IT_MAX 2000
short ffe_it[IT_MAX][11];
long ffe_it_n;
void FfeWinCount (long num) __asm__("_FfeWinCount");
void FfeWinCount (long num)
{
	long clk = ffe_clock_for_probe;

	extern long ffe_f8_state;

	if (clk >= 600000 && clk <= 800000 && num >= 0 && num < 2200)
		ffe_wc_cnt[num]++;
	/* FRAMECNT (2026-09-26): the same counts over exactly the FRAME1808
	 * capture frame (state 2). Twin: PCFRAMECNT. */
	if (ffe_f8_state == 2 && num >= 0 && num < 2200)
		ffe_fc_cnt[num]++;
	/* ITEMLOG (2026-09-26): the draw items of the capture frame, function
	 * number + 8 shorts from item+4. Twin: pc_item_log (build\pcprobe_proj.c).
	 * caller[1] is arg1, the item, exactly as FfeTri1808 reads it. */
	if (ffe_f8_state == 2 && ffe_it_n < IT_MAX &&
	    (num == 1791 || num == 1792 || (num >= 1799 && num <= 1811) || num == 1814)) {
		unsigned long *caller = (unsigned long *)((char *)&num + 4 + 60);
		const short *item = (const short *)caller[1];
		int k;
		ffe_it[ffe_it_n][0] = (short)num;
		for (k = 0; k < 10; k++) ffe_it[ffe_it_n][1 + k] = item[2 + k];
		ffe_it_n++;
	}
}

/* FRAME1808 (2026-09-25): every FUNC_001808 item of ONE whole frame -- the
 * first frame that starts with the clock >= 1.80M -- as 3 screen points and
 * the three depth words at +0x12/+0x16/+0x1a. Twin: PCFRAME1808. The state
 * machine is advanced by ffe_frame1808_present() from VideoBlit. */
#define F8_MAX 400
short ffe_f8[F8_MAX][9];
long ffe_f8_n, ffe_f8_state;	/* 0 wait, 1 armed, 2 recording, 3 done */
/* OBJTAB (2026-10-09): the whole game-object table at the capture frame --
 * slot, flag byte, type [+0x82] and the six position dwords +0x26..+0x3a
 * (three 64-bit coords, low dword first). Twin of the PC side's objtab_dump.
 * Layout from FUNC_000927/928: flags at base+1..0x72, images at
 * base+0x74+idx*0x152. */
static void objtab_dump2 (const char *path, const char *mode, long clk)
{
	extern unsigned char *ffe_objbase __asm__("DATA_009133");
	const unsigned char *b = ffe_objbase;
	FILE *f;
	int i, k;

	if (b == 0) return;
	f = ffe_logopen (path, mode);
	if (f == 0) return;
	{
		/* which slot is the PLAYER (DATA_008861 -> object), and the raw pointer
		 * offset in case it is not a table slot at all */
		extern unsigned char *ffe_plr __asm__("DATA_008861");
		long d = ffe_plr ? (long)(ffe_plr - b) : -1;
		fprintf (f, "T clk=%ld player_off=%ld slot=%ld%c", clk, d, d >= 0x74 ? (d - 0x74) / 0x152 : -1, 10);
		{
			/* RNG state (2026-10-16): DATA_008818 is le -> assemble as x86 does; DATA_008820 be32 */
			extern unsigned char rng_a[] __asm__("DATA_008818"); extern unsigned long rng_b __asm__("DATA_008820");
			fprintf (f, "N rng=%08lx %08lx%c", (unsigned long)rng_a[0] | ((unsigned long)rng_a[1] << 8) | ((unsigned long)rng_a[2] << 16) | ((unsigned long)rng_a[3] << 24), rng_b, 10);
		}
		{
			extern unsigned short g_ax __asm__("DATA_009188"), g_ay __asm__("DATA_009189");
			extern unsigned char g_rmb __asm__("DATA_007711"), g_glob[] __asm__("DATA_008804");
			fprintf (f, "G acc=%04x,%04x prev=%04x,%04x state=%02x ea=%02x tgt=%02x lock=%02x rmb=%02x%c", g_ax, g_ay,
			         *(const unsigned short *)(g_glob + 0xd0), *(const unsigned short *)(g_glob + 0xd2),
			         g_glob[0xe8], g_glob[0xea], g_glob[0xec], g_glob[0xee], g_rmb, 10);
		}
	}
	for (i = 1; i <= 0x72; i++) {
		const unsigned char *o;
		if (b[i] == 0) continue;
		o = b + 0x74 + i * 0x152;
		fprintf (f, "O %d f=%d t=%ld", i, b[i], *(const long *)(o + 0x82));
		for (k = 0; k < 6; k++) fprintf (f, " %ld", *(const long *)(o + 0x26 + 4 * k));
		fprintf (f, "%c", 10);
		/* R: parent-relative position +0x3e (6 dwords), parent +0x56, flag +0x57;
		 * M: the 3x3 matrix at +0; C: bytes +0xc8..+0xcf raw; A: angle +0xba/+0xbe,
		 * rate +0xc2, radius +0xb0 */
		fprintf (f, " R p=%d f=%d", o[0x56], o[0x57]);
		for (k = 0; k < 6; k++) fprintf (f, " %ld", *(const long *)(o + 0x3e + 4 * k));
		fprintf (f, "%c M", 10);
		for (k = 0; k < 9; k++) fprintf (f, " %ld", *(const long *)(o + 4 * k));
		fprintf (f, "%c C", 10);
		for (k = 0; k < 8; k++) fprintf (f, " %02x", o[0xc8 + k]);
		fprintf (f, " A %ld %ld rate=%ld rad=%08lx%c", *(const long *)(o + 0xba), *(const long *)(o + 0xbe),
		         *(const long *)(o + 0xc2), *(const unsigned long *)(o + 0xb0), 10);
		fprintf (f, " Y");
		for (k = 0; k < 6; k++) fprintf (f, " %04x", *(const unsigned short *)(o + 0xb0 + 2 * k));
		fprintf (f, " |");
		for (k = 0; k < 6; k++) fprintf (f, " %04x", *(const unsigned short *)(o + 0x10c + 2 * k));
		fprintf (f, " | %04x %04x %04x v=%ld,%ld,%ld%c", *(const unsigned short *)(o + 0xec), *(const unsigned short *)(o + 0xee),
		         *(const unsigned short *)(o + 0xf0), *(const long *)(o + 0x8c), *(const long *)(o + 0x90), *(const long *)(o + 0x94), 10);
		{
			/* X: the model header FUNC_001538 returns for this type, its +0x38
			 * "extra data" pointer and the first 8 bytes there (ship stats),
			 * plus the object's own words +0xbc..+0xc6 (2026-10-09) */
			extern unsigned char *FUNC_001538 (long type);
			const unsigned char *hdr = FUNC_001538 (*(const long *)(o + 0x82));
			const unsigned char *ex = hdr ? *(const unsigned char *const *)(hdr + 0x38) : 0;
			fprintf (f, " X hdr=%lx ex=%lx", (unsigned long)hdr, (unsigned long)ex);
			if (ex) for (k = 0; k < 8; k++) fprintf (f, " %02x", ex[k]);
			fprintf (f, " W");
			for (k = 0; k < 6; k++) fprintf (f, " %04x", *(const unsigned short *)(o + 0xbc + 2 * k));
			fprintf (f, "%c", 10);
		}
	}
	fclose (f);
}

/* P879 (2026-10-09): twin of the PC shims on FUNC_000879 / FUNC_000574 --
 * entry arguments only (slot, a, b / slot + the 64-bit input vector). The
 * outputs are read from the object table (OBJTS, "R" lines). */
long ffe_p9[64][10];
long ffe_p9_n;
void FfeP879 (long num) __asm__("_FfeP879");
void FfeP879 (long num)
{
	unsigned long *caller = (unsigned long *)((char *)&num + 4 + 60);
	long *r;
	int k;

	if (ffe_p9_n >= 64) return;
	r = ffe_p9[ffe_p9_n++];
	r[0] = num;
	{
		/* the height result and failure flag LEFT BY THE PREVIOUS FUNC_000574
		 * call (574 resets them only after this entry payload) */
		extern long ffe_h8703 __asm__("DATA_008703");
		extern long ffe_h8704 __asm__("DATA_008704");
		r[8] = ffe_h8703; r[9] = ffe_h8704;
	}
	if (num == 879) {
		r[1] = ((const unsigned char *)caller[1])[0x86];
		r[2] = (long)caller[2];
		r[3] = (long)caller[3];
	} else {
		const long *v = (const long *)caller[2];
		r[1] = ((const unsigned char *)caller[3])[0x86];
		for (k = 0; k < 6; k++) r[2 + k] = v[k];
	}
}
static void p879_dump (void)
{
	FILE *f = ffe_logopen ("PROGDIR:ffe_p879.log", "w");
	long i;
	if (f == 0) return;
	for (i = 0; i < ffe_p9_n; i++) {
		const long *r = ffe_p9[i];
		if (r[0] == 879) fprintf (f, "P879 slot=%ld a=%ld b=%ld prev8703=%ld prev8704=%ld%c", r[1], r[2], r[3], r[8], r[9], 10);
		else fprintf (f, "P574 slot=%ld v %ld %ld %ld %ld %ld %ld prev8703=%ld prev8704=%ld%c", r[1], r[2], r[3], r[4], r[5], r[6], r[7], r[8], r[9], 10);
	}
	fclose (f);
}
/* P581 (2026-10-09): the first 64 FUNC_000581 calls made once the GAME clock
 * runs (bit 31 of DATA_008804 set) -- i.e. the terrain-height queries of the
 * new-game starport placement. Inputs at entry: rec[0..5] (arg2), obj+0x138..
 * (arg3, 4 dwords), thr (arg4), ctx[0..6] (arg5); "po" is *out of the PREVIOUS
 * call (read through the saved pointer). Twin: FUNC_000581 shim on the PC. */
long ffe_p5[64][20];
long ffe_p5_n;
static long *ffe_p5_prev_out;
void FfeP581 (long num) __asm__("_FfeP581");
void FfeP581 (long num)
{
	unsigned long *caller = (unsigned long *)((char *)&num + 4 + 60);
	long *r;
	int k;
	const unsigned char *rec, *obj;
	const long *ctx;

	(void)num;
	if ((ffe_clock_for_probe & 0x80000000UL) == 0) return;
	if (ffe_p5_n > 0 && ffe_p5_n <= 64 && ffe_p5_prev_out) ffe_p5[ffe_p5_n - 1][19] = *ffe_p5_prev_out;
	if (ffe_p5_n >= 64) { ffe_p5_prev_out = 0; return; }
	r = ffe_p5[ffe_p5_n++];
	ffe_p5_prev_out = (long *)caller[1];
	rec = (const unsigned char *)caller[2];
	obj = (const unsigned char *)caller[3];
	ctx = (const long *)caller[5];
	for (k = 0; k < 6; k++) r[k] = rec[k];
	for (k = 0; k < 4; k++) r[6 + k] = *(const long *)(obj + 0x138 + 4 * k);
	r[10] = (long)caller[4];
	for (k = 0; k < 7; k++) r[11 + k] = ctx[k];
	r[18] = obj[0x86];
	r[19] = 0x7ffffffe;	/* "not read back" */
}
static void p581_dump (void)
{
	FILE *f = ffe_logopen ("PROGDIR:ffe_p581.log", "w");
	long i;
	int k;
	if (f == 0) return;
	for (i = 0; i < ffe_p5_n; i++) {
		const long *r = ffe_p5[i];
		fprintf (f, "P581 slot=%ld rec %02lx%02lx%02lx%02lx%02lx%02lx R", r[18], r[0], r[1], r[2], r[3], r[4], r[5]);
		for (k = 0; k < 4; k++) fprintf (f, " %ld", r[6 + k]);
		fprintf (f, " thr=%ld ctx", r[10]);
		for (k = 0; k < 7; k++) fprintf (f, " %ld", r[11 + k]);
		fprintf (f, " out=%ld%c", r[19], 10);
	}
	fclose (f);
}
/* P36 (2026-10-09): FUNC_000036(obj, type) -- which model header the type
 * resolves to and what its +0x38 "extra data" pointer and first bytes are.
 * Logged for the first 48 calls made with the game clock running. */
long ffe_p36[48][8];
unsigned char ffe_p36_b[48][12];
long ffe_p36_n;
void FfeP36 (long num) __asm__("_FfeP36");
void FfeP36 (long num)
{
	unsigned long *caller = (unsigned long *)((char *)&num + 4 + 60);
	extern unsigned char *FUNC_001538 (long type);	/* C-ABI wrapper of the translated function */
	const unsigned char *obj = (const unsigned char *)caller[1];
	long type = (long)caller[2];
	const unsigned char *hdr, *ex;
	long *r;
	int k;

	(void)num;
	/* a RING of the last 48 calls: objects are built before the game clock
	 * gets its bit 31, so a clock filter recorded nothing (2026-10-09) */
	hdr = FUNC_001538 (type);
	r = ffe_p36[ffe_p36_n % 48];
	r[0] = obj[0x86]; r[1] = type; r[2] = (long)hdr;
	r[3] = hdr ? *(const long *)(hdr + 0x38) : -1;
	r[4] = hdr ? *(const long *)(hdr + 0x34) : -1;
	ex = hdr ? *(const unsigned char *const *)(hdr + 0x38) : 0;
	for (k = 0; k < 12; k++) ffe_p36_b[ffe_p36_n % 48][k] = ex ? ex[k] : 0;
	ffe_p36_n++;
}
static void p36_dump (void)
{
	FILE *f = ffe_logopen ("PROGDIR:ffe_p36.log", "w");
	long i;
	int k;
	if (f == 0) return;
	fprintf (f, "P36 total calls=%ld (ring of 48, not in call order)%c", ffe_p36_n, 10);
	for (i = 0; i < ffe_p36_n && i < 48; i++) {
		fprintf (f, "P36 slot=%ld type=%ld hdr=%lx ex38=%lx p34=%lx bytes", ffe_p36[i][0], ffe_p36[i][1],
		         ffe_p36[i][2], ffe_p36[i][3], ffe_p36[i][4]);
		for (k = 0; k < 12; k++) fprintf (f, " %02x", ffe_p36_b[i][k]);
		fprintf (f, "%c", 10);
	}
	fclose (f);
}
static void objtab_dump (const char *path) { objtab_dump2 (path, "w", 0); }
/* OBJTS (2026-10-09): the object table at the first game frame (clock with
 * bit 31 set) and then every 128 presents, six snapshots, appended. */
static void objts_tick (long clk)
{
	static int n, cnt;
	if (((unsigned long)clk & 0x80000000UL) == 0 || n >= 90) return;
	/* 6 snapshots every 128 presents (game start), then every 256 up to 40
	 * (2026-10-09: to follow the player through launch) */
	if (n == 0 || (n < 6 && (++cnt & 127) == 0) || (n >= 6 && (++cnt & 255) == 0)) { objtab_dump2 ("PROGDIR:ffe_objts.log", n ? "a" : "w", clk); n++; }
}
/* RNGWIN (2026-10-16): PROGDIR:ffe_rngwin "lo hi" -> every present with the
 * unsigned clock in [lo,hi]: clock, RNG state (as the game reads it), object
 * count and the model of every live object. Twin: FFE_RNGWIN on the PC. */
static void rngwin_tick (long clk)
{
	static int rd; static unsigned long lo, hi;
	extern unsigned char rw_a[] __asm__("DATA_008818"); extern unsigned long rw_b __asm__("DATA_008820");
	extern unsigned char *rw_base __asm__("DATA_009133");
	FILE *f; int i;
	if (!rd) { FILE *w = fopen ("PROGDIR:ffe_rngwin", "r"); rd = 1; if (w) { if (fscanf (w, "%lu %lu", &lo, &hi) != 2) lo = hi = 0; fclose (w); } }
	if (!hi || (unsigned long)clk < lo || (unsigned long)clk > hi || !rw_base) return;
	f = ffe_logopen ("PROGDIR:ffe_rngwin.log", "a"); if (!f) return;
	fprintf (f, "W %lu %08lx %08lx", (unsigned long)clk, (unsigned long)rw_a[0] | ((unsigned long)rw_a[1] << 8) | ((unsigned long)rw_a[2] << 16) | ((unsigned long)rw_a[3] << 24), rw_b);
	for (i = 1; i <= 0x72; i++) if (rw_base[i]) fprintf (f, " %d:%ld", i, *(const long *)(rw_base + 0x74 + i * 0x152 + 0x82));
	fprintf (f, "%c", 10); fclose (f);
}
#define DOCKTS_PATH "PROGDIR:ffe_dockts.log"
/* DOCKTS (2026-10-16): while the PLAYER's parent object is a station (model
 * 72), every 8 presents: player position relative to the parent (+0x3e..,
 * three 64-bit), the player's and the station's 3x3 matrices. Twin on both
 * machines, same format, up to 400 lines. */
static void dockts_tick (long clk)
{
	extern unsigned char *dk_objbase __asm__("DATA_009133");
	extern unsigned char *dk_plr __asm__("DATA_008861");
	static int n, cnt;
	const unsigned char *b = dk_objbase, *p = dk_plr, *s;
	FILE *f;
	int k;

	if (b == 0 || p == 0 || n >= 3000 || (++cnt & 7) != 0) return;
	if (p[0x56] == 0 || p[0x56] > 0x72) return;
	s = b + 0x74 + p[0x56] * 0x152;
	if (*(const long *)(s + 0x82) != 72) return;
	f = ffe_logopen (DOCKTS_PATH, n ? "a" : "w");
	if (f == 0) return;
	n++;
	fprintf (f, "D %lu par=%d", (unsigned long)clk, p[0x56]);
	for (k = 0; k < 3; k++)
		fprintf (f, " %ld %ld", *(const long *)(p + 0x3e + 8 * k), *(const long *)(p + 0x42 + 8 * k));
	fprintf (f, " P");
	for (k = 0; k < 9; k++) fprintf (f, " %ld", *(const long *)(p + 4 * k));
	fprintf (f, " S");
	for (k = 0; k < 9; k++) fprintf (f, " %ld", *(const long *)(s + 4 * k));
	fprintf (f, "%c", 10);
	fclose (f);
}
void ffe_frame1808_present (long clk)
{
	objts_tick (clk);
	dockts_tick (clk);
	rngwin_tick (clk);
	/* F8_ARM: 700000 = the asteroid close-up (2026-09-26); was 1800000
	 * (Mars). Twin: F8_ARM in build\pcprobe_proj.c. */
	/* the arm clock is configurable (2026-10-03): PROGDIR:ffe_f8arm holds an
	 * UNSIGNED decimal clock; the game clock in the cockpit starts at
	 * 0x80000000, so the compare is unsigned. Default 700000 (the intro). */
	static unsigned long arm;
	if (arm == 0) {
		FILE *af = fopen ("PROGDIR:ffe_f8arm", "r");
		arm = 700000;
		if (af != NULL) { if (fscanf (af, "%lu", &arm) != 1) arm = 700000; fclose (af); }
	}
	if (ffe_f8_state == 0 && (unsigned long)clk >= arm) ffe_f8_state = 1;
	else if (ffe_f8_state == 1) ffe_f8_state = 2;
	else if (ffe_f8_state == 2) { ffe_f8_state = 3; objtab_dump ("PROGDIR:ffe_objtab.log"); p879_dump (); p581_dump (); p36_dump (); }
}
void FfeFrame1808 (const unsigned char *item)
{
	int k;

	if (ffe_f8_state != 2 || ffe_f8_n >= F8_MAX) return;
	for (k = 0; k < 3; k++) {
		ffe_f8[ffe_f8_n][2 * k] = *(const short *)(item + 4 + 4 * k);
		ffe_f8[ffe_f8_n][2 * k + 1] = *(const short *)(item + 6 + 4 * k);
	}
	ffe_f8[ffe_f8_n][6] = *(const short *)(item + 0x12);
	ffe_f8[ffe_f8_n][7] = *(const short *)(item + 0x16);
	ffe_f8[ffe_f8_n][8] = *(const short *)(item + 0x1a);
	ffe_f8_n++;
}

/* CLIP1837 (2026-09-25): inputs of FUNC_001837 (near-plane clip + project)
 * during the FRAME1808 capture frame: recA +4/+8/+0xc/+0x3c, recB +4/+8/+0xc/
 * +0x28/+0x2c/+0x30/+0x3c. The output (dest x,y) is read back by the NEXT
 * call or at the frame end (FfeClip1837Out). Twin: PCCLIP1837. */
#define C7_MAX 64
long ffe_c7[C7_MAX][13];
unsigned long ffe_c7_dest[C7_MAX];
long ffe_c7_n;
void FfeClip1837 (long num) __asm__("_FfeClip1837");
void FfeClip1837 (long num)
{
	unsigned long *caller = (unsigned long *)((char *)&num + 4 + 60);
	const long *a = (const long *)caller[2], *b = (const long *)caller[3];
	extern long ffe_f8_state;

	(void)num;
	if (ffe_f8_state != 2 || ffe_c7_n >= C7_MAX) return;
	ffe_c7_dest[ffe_c7_n] = caller[1];
	ffe_c7[ffe_c7_n][0] = a[1]; ffe_c7[ffe_c7_n][1] = a[2];
	ffe_c7[ffe_c7_n][2] = a[3]; ffe_c7[ffe_c7_n][3] = a[15];
	ffe_c7[ffe_c7_n][4] = b[1]; ffe_c7[ffe_c7_n][5] = b[2];
	ffe_c7[ffe_c7_n][6] = b[3]; ffe_c7[ffe_c7_n][7] = b[10];
	ffe_c7[ffe_c7_n][8] = b[11]; ffe_c7[ffe_c7_n][9] = b[12];
	ffe_c7[ffe_c7_n][10] = b[15];
	ffe_c7_n++;
}

/* FRAMEHALO (2026-09-25): the halo items of the FRAME1808 capture frame --
 * FUNC_001805 (7-colour gradient quad) and FUNC_001810 (flat quad): the
 * first 0x1c bytes as 14 words, plus which handler. Twin: PCFRAMEHALO. */
short ffe_fh[200][15];
long ffe_fh_n;
void FfeFrameHalo (long num) __asm__("_FfeFrameHalo");
void FfeFrameHalo (long num)
{
	unsigned long *caller = (unsigned long *)((char *)&num + 4 + 60);
	const short *it = (const short *)caller[1];
	extern long ffe_f8_state;
	int k;

	if (ffe_f8_state != 2 || ffe_fh_n >= 200) return;
	ffe_fh[ffe_fh_n][0] = (short)num;
	for (k = 0; k < 14; k++) ffe_fh[ffe_fh_n][k + 1] = it[k];
	ffe_fh_n++;
}

/* FRAME1478 (2026-09-25): every FUNC_001478 call of the FRAME1808 capture
 * frame -- the 6-byte vertex descriptor and, read back at the NEXT call (the
 * record is [DATA_009200] + idx*36), the screen point and position it
 * produced. Twin: PCFRAME1478 (reads the record right after the call). */
#define V8_MAX 1500
unsigned char ffe_v8_d[V8_MAX][6];
long ffe_v8_r[V8_MAX][5];
long ffe_v8_o[V8_MAX][12];	/* pos x,y,z + the 3x3 matrix at ctx+0x14 */
long ffe_v8_n;
long ffe_v8_k[V8_MAX];
static const unsigned char *v8_pend;
/* model index owning a vertex table: the largest DATA_004681 entry <= vt */
static long v8_model_of (const unsigned char *vt)
{
	extern unsigned char v8_modtab[] __asm__("DATA_004681");
	const unsigned long *tab = (const unsigned long *)v8_modtab;
	unsigned long best = 0; long bi = -1; int i;
	for (i = 0; i < 466; i++)
		if (tab[i] <= (unsigned long)vt && tab[i] > best) { best = tab[i]; bi = i; }
	return bi;
}
static void v8_close (void)
{
	if (v8_pend != NULL && ffe_v8_n > 0) {
		const short *s = (const short *)v8_pend;
		const long *l = (const long *)v8_pend;
		long *r = ffe_v8_r[ffe_v8_n - 1];
		r[0] = s[0]; r[1] = s[1]; r[2] = l[1]; r[3] = l[2]; r[4] = l[3];
	}
	v8_pend = NULL;
}
void FfeFrame1478 (long num) __asm__("_FfeFrame1478");
void FfeFrame1478 (long num)
{
	unsigned long *caller = (unsigned long *)((char *)&num + 4 + 60);
	const unsigned char *ctx = (const unsigned char *)caller[1];
	long idx = (long)(signed char)(caller[2] & 0xff);
	extern long ffe_f8_state;
	extern unsigned char *DATA_009200_p __asm__("DATA_009200");
	const unsigned char *vt, *d;
	int k;

	(void)num;
	v8_close ();
	if (ffe_f8_state != 2 || ffe_v8_n >= V8_MAX || idx < 0) return;
	vt = *(const unsigned char *const *)(ctx + 0x150);
	d = vt + (idx >> 1) * 6;
	for (k = 0; k < 6; k++) ffe_v8_d[ffe_v8_n][k] = d[k];
	ffe_v8_o[ffe_v8_n][0] = *(const long *)(ctx + 4);
	ffe_v8_o[ffe_v8_n][1] = *(const long *)(ctx + 8);
	ffe_v8_o[ffe_v8_n][2] = *(const long *)(ctx + 12);
	for (k = 0; k < 9; k++)
		ffe_v8_o[ffe_v8_n][3 + k] = *(const long *)(ctx + 0x14 + 4 * k);	/* matrix m[0..8] */
	ffe_v8_k[ffe_v8_n] = v8_model_of (vt);
	v8_pend = DATA_009200_p + idx * 36;
	ffe_v8_n++;
}
void ffe_frame1478_flush (void) { v8_close (); }

long ffe_samp_arm_at = -1;	/* present at which VideoBlit arms the sampler */

/* 2026-10-15: SYSMAP, twin of FUNC_000981's shim in build\pcprobe_proj.c.
 * Dwords at +0x2a4/+0x2e2 are be32 (native); the word pairs at +0x3f6/+0x3fc
 * are be16 (native words); the flag byte +0x3fa is a single byte. */
void FfeSysMap (long num) __asm__("_FfeSysMap");
void FfeSysMap (long num)
{
	extern unsigned char DATA_008804[] __asm__("DATA_008804");
	static int n_done;
	const unsigned char *b = DATA_008804;
	FILE *f;
	int i;

	if (n_done >= 3) return;
	n_done++;
	f = ffe_logopen ("PROGDIR:ffe_enter.log", "a");
	if (f == NULL) return;
	fprintf (f, "SYSMAP n=%d m=", n_done);
	for (i = 0; i < 9; i++)
		fprintf (f, "%lx,", *(const unsigned long *)(b + 0x2a4 + 4 * i));
	fprintf (f, " s=");
	for (i = 0; i < 6; i++)
		fprintf (f, "%lx,", *(const unsigned long *)(b + 0x2e2 + 4 * i));
	fprintf (f, " abs=");
	for (i = 0; i < 6; i++)
		fprintf (f, "%lx,", (unsigned long)*(const long *)(b + 0x2ca + 4 * i));
	fprintf (f, " v56=%02x v57=%02x", b[0x2fa], b[0x2fb]);
	fprintf (f, " w80=%04x w81=%04x w84=%04x w85=%04x f82=%02x\n",
	         *(const unsigned short *)(b + 0x3f6), *(const unsigned short *)(b + 0x3f8),
	         *(const unsigned short *)(b + 0x3fc), *(const unsigned short *)(b + 0x3fe),
	         b[0x3fa]);
	fclose (f);
}
/* 2026-10-15: ORB986, twin of FUNC_000986's shim in build\pcprobe_proj.c.
 * Object fields read as the game reads them: dwords/words native. */
void FfeOrb986 (long num) __asm__("_FfeOrb986");
void FfeOrb986 (long num)
{
	unsigned long *caller = (unsigned long *)((char *)&num + 4 + 60);
	const unsigned char *o = (const unsigned char *)caller[1];
	static int n_done;
	FILE *f;

	if (n_done >= 80 || o == 0) return;
	n_done++;
	f = ffe_logopen ("PROGDIR:ffe_enter.log", "a");
	if (f == NULL) return;
	fprintf (f, "ORB n=%d t82=%lx b87=%02x b56=%02x b14c=%02x w9e=%04x wc6=%04x dbe=%lx db0=%lx p3e=%lx,%lx,%lx,%lx,%lx,%lx a2=%lx\n",
	         n_done, *(const unsigned long *)(o + 0x82), o[0x87], o[0x56], o[0x14c],
	         *(const unsigned short *)(o + 0x9e), *(const unsigned short *)(o + 0xc6),
	         *(const unsigned long *)(o + 0xbe), *(const unsigned long *)(o + 0xb0),
	         *(const unsigned long *)(o + 0x3e), *(const unsigned long *)(o + 0x42),
	         *(const unsigned long *)(o + 0x46), *(const unsigned long *)(o + 0x4a),
	         *(const unsigned long *)(o + 0x4e), *(const unsigned long *)(o + 0x52),
	         caller[2]);
	fclose (f);
}
/* 2026-10-15: OBJ1680, twin of FUNC_001680's shim in build\pcprobe_proj.c --
 * logged in the FRAME1808 capture frame only (ffe_f8_state == 2). */
void FfeObj1680 (long num) __asm__("_FfeObj1680");
void FfeObj1680 (long num)
{
	extern long ffe_f8_state;
	unsigned long *caller = (unsigned long *)((char *)&num + 4 + 60);
	const unsigned char *o = (const unsigned char *)caller[1], *p = (const unsigned char *)caller[2];
	static int n_done;
	FILE *f;

	if (ffe_f8_state != 2 || n_done >= 120) return;
	n_done++;
	f = ffe_logopen ("PROGDIR:ffe_enter.log", "a");
	if (f == NULL) return;
	fprintf (f, "OBJ1680 n=%d mdl=%lx s7e=%lx pos=%lx,%lx,%lx,%lx,%lx,%lx f56=%02x f57=%02x f86=%02x p56=%02x p57=%02x\n",
	         n_done, *(const unsigned long *)(o + 0x82), *(const unsigned long *)(o + 0x7e),
	         *(const unsigned long *)(o + 0x3e), *(const unsigned long *)(o + 0x42),
	         *(const unsigned long *)(o + 0x46), *(const unsigned long *)(o + 0x4a),
	         *(const unsigned long *)(o + 0x4e), *(const unsigned long *)(o + 0x52),
	         o[0x56], o[0x57], o[0x86], p ? p[0x56] : 0, p ? p[0x57] : 0);
	fclose (f);
}
/* 2026-10-15: MSB1657, twin of the PC shim -- the vector at entry, in the
 * FRAME1808 capture frame. The 64-bit halves are native dwords here. */
void FfeMsb1657 (long num) __asm__("_FfeMsb1657");
void FfeMsb1657 (long num)
{
	extern long ffe_f8_state;
	unsigned long *caller = (unsigned long *)((char *)&num + 4 + 60);
	const unsigned long *v = (const unsigned long *)caller[1];
	static int n_done;
	FILE *f;

	if (ffe_f8_state != 2 || n_done >= 120) return;
	n_done++;
	f = ffe_logopen ("PROGDIR:ffe_enter.log", "a");
	if (f == NULL) return;
	fprintf (f, "MSB1657 n=%d v=%lx,%lx,%lx,%lx,%lx,%lx\n", n_done, v[0], v[1], v[2], v[3], v[4], v[5]);
	fclose (f);
}
/* 2026-10-15: TXT1747, twin of the FUNC_001747 shim in build\pcprobe_proj.c:
 * in the FRAME1808 capture frame, the 8 stream bytes (raw model bytecode, x86
 * byte order on both machines) and the 8 bytes of the anchor node. */
void FfeTxt1747 (long num) __asm__("_FfeTxt1747");
void FfeTxt1747 (long num)
{
	extern long ffe_f8_state;
	extern unsigned char *DATA_009201_p __asm__("DATA_009201");
	unsigned long *caller = (unsigned long *)((char *)&num + 4 + 60);
	const unsigned char *st = (const unsigned char *)caller[2], *nd;
	FILE *f;
	int i;

	if (ffe_f8_state != 2) return;
	nd = DATA_009201_p + st[2] * 8;
	f = ffe_logopen ("PROGDIR:ffe_enter.log", "a");
	if (f == NULL) return;
	fprintf (f, "TXT1747 st=");
	for (i = 0; i < 8; i++) fprintf (f, "%02x", st[i]);
	fprintf (f, " node=");
	for (i = 0; i < 8; i++) fprintf (f, "%02x", nd[i]);
	{ const unsigned char *cx = (const unsigned char *)caller[1], *nrm, *vtx; int q;
	  fprintf (f, " ctx=");
	  for (q = 0x38; q <= 0x50; q += 4) fprintf (f, "%lx,", (unsigned long)*(const long *)(cx + q));
	  fprintf (f, " s148=%lx", (unsigned long)*(const long *)(cx + 0x148)); fprintf (f, " mat="); for (q = 0x14; q <= 0x34; q += 4) fprintf (f, "%lx,", (unsigned long)*(const long *)(cx + q)); fprintf (f, " pos=%lx,%lx,%lx", (unsigned long)*(const long *)(cx + 4), (unsigned long)*(const long *)(cx + 8), (unsigned long)*(const long *)(cx + 12));
	  nrm = *(const unsigned char *const *)(cx + 0x154) + (st[2] >> 1) * 6 - 6;
	  vtx = *(const unsigned char *const *)(cx + 0x150) + (nrm[0] >> 1) * 6;
	  fprintf (f, " nrm=%02x%02x%02x%02x%02x vtx=%02x%02x%02x", nrm[0], nrm[1], nrm[2], nrm[3], nrm[4], vtx[2], vtx[3], vtx[4]); }
	fprintf (f, "\n");
	fclose (f);
}
/* 2026-10-16: LIN1703, twin of the PC shim (raw bytes on both machines). */
void FfeLin1703 (long num) __asm__("_FfeLin1703");
void FfeLin1703 (long num)
{
	extern long ffe_f8_state;
	extern unsigned char DATA_007826[] __asm__("DATA_007826");
	extern unsigned char DATA_007827[] __asm__("DATA_007827");
	unsigned long *caller = (unsigned long *)((char *)&num + 4 + 60);
	const unsigned char *cx = (const unsigned char *)caller[1];
	FILE *f;

	if (ffe_f8_state != 2) return;
	f = ffe_logopen ("PROGDIR:ffe_enter.log", "a");
	if (f == NULL) return;
	fprintf (f, "LIN1703 a2=%lx a3=%lx a4=%lx def=%02x%02x%02x f4=%02x%02x%02x 84=%02x%02x%02x\n",
	         caller[2], caller[3], caller[4], DATA_007826[0], DATA_007826[1], DATA_007827[0],
	         cx[0xf4], cx[0xf5], cx[0xf6], cx[0x84], cx[0x85], cx[0x86]);
	fclose (f);
}
/* 2026-10-16: SH1691, twin of the PC shim: the by-value RGB argument as the
 * four bytes in ADDRESS order (as FUNC_001691 reads them) and the index. */
void FfeSh1691 (long num) __asm__("_FfeSh1691");
void FfeSh1691 (long num)
{
	extern long ffe_f8_state;
	unsigned long *caller = (unsigned long *)((char *)&num + 4 + 60);
	const unsigned char *b = (const unsigned char *)&caller[2];
	static int n;
	FILE *f;

	if (ffe_f8_state != 2 || n >= 40) return;
	n++;
	f = ffe_logopen ("PROGDIR:ffe_enter.log", "a");
	if (f == NULL) return;
	fprintf (f, "SH1691 rgb=%02x%02x%02x%02x idx=%ld\n", b[0], b[1], b[2], b[3], (long)caller[3]);
	fclose (f);
}
/* 2026-10-16: SRF1840, twin of the PC shim. */
void FfeSrf1840 (long num) __asm__("_FfeSrf1840");
void FfeSrf1840 (long num)
{
	extern long ffe_f8_state;
	unsigned long *caller = (unsigned long *)((char *)&num + 4 + 60);
	static int n;
	FILE *f;
	int k;

	if (ffe_f8_state != 2 || n >= 60) return;
	n++;
	f = ffe_logopen ("PROGDIR:ffe_enter.log", "a");
	if (f == NULL) return;
	fprintf (f, "SRF1840");
	for (k = 0; k < 3; k++) {
		const long *v = (const long *)caller[1 + k];
		fprintf (f, " [%lx %lx %lx h=%lx]", (unsigned long)v[1], (unsigned long)v[2],
		         (unsigned long)v[3], (unsigned long)v[15]);
	}
	{ extern long DATA_009262 __asm__("DATA_009262"), DATA_009263 __asm__("DATA_009263"), DATA_009267 __asm__("DATA_009267");
	  fprintf (f, " g62=%lx g63=%lx g67=%lx", (unsigned long)DATA_009262, (unsigned long)DATA_009263, (unsigned long)DATA_009267); }
	fprintf (f, " a5=%lx\n", caller[5]);
	fclose (f);
}
/* 2026-10-16: CR1838, twin of the PC shim (table entries are be32 = numeric). */
void FfeCr1838 (long num) __asm__("_FfeCr1838");
void FfeCr1838 (long num)
{
	extern long ffe_f8_state;
	extern unsigned char DATA_007849[] __asm__("DATA_007849");
	unsigned long *caller = (unsigned long *)((char *)&num + 4 + 60);
	static int n;
	FILE *f;

	if (ffe_f8_state != 2 || n >= 80) return;
	n++;
	f = ffe_logopen ("PROGDIR:ffe_enter.log", "a");
	if (f == NULL) return;
	fprintf (f, "CR1838 s=%ld light=%lx off=%ld e0=%lx\n", (long)caller[2], caller[3],
	         (long)((const unsigned char *)caller[4] - DATA_007849), *(const unsigned long *)caller[4]);
	fclose (f);
}
