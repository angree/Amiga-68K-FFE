#include <string.h>
/* PC-side projection probe -- the reference half of the Amiga's FfeProjLog.
 *
 * Why this exists: every remaining question about the 3D viewport has come down
 * to "is this number the right size?", and reasoning about it from the picture
 * has already produced two retracted diagnoses. FUNC_001479 is the projection,
 * so logging its INPUT on both sides turns the question into a diff.
 *
 * How it is hooked without touching ref\jjffe: build\build-pc-probe.py writes a
 * COPY of ffe.asm into build\pcprobe\ with the label `FUNC_001479:` renamed to
 * `FUNC_001479_REAL:` and its `GLOBAL` line changed to match, plus an `EXTERN
 * FUNC_001479`. All seven `call FUNC_001479` sites then bind to the function
 * below, which logs and forwards. Same trick as pcdump_video.cpp uses for
 * VideoBlit, and the same reason: the stock sources stay untouched.
 *
 * The game's own symbols carry no leading underscore while MinGW decorates C
 * ones, hence the explicit __asm__ names on both sides.
 *
 * Argument order, checked at all seven call sites (ffe.asm:80622, 87058, 88708,
 * 102034, 114411, 129882, 132039) -- cdecl pushes right to left, and every site
 * pushes the destination LAST:
 *     FUNC_001479 (short *dest, const long *vertex)
 * `dest` receives the two projected screen words; `vertex` is the camera-space
 * (x, y, z) the projection divides. Getting this backwards is exactly the bug
 * that made the Amiga probe report a packed screen-coordinate pair as a
 * coordinate in the tens of millions, so it is stated here rather than assumed.
 */
#include <stdio.h>
#include <stdlib.h>

extern int FUNC_001479_REAL (short *dest, const long *vertex)
	__asm__("FUNC_001479_REAL");

int FUNC_001479 (short *dest, const long *vertex) __asm__("FUNC_001479");

/* A RING of the most recent projections, rewritten whole every time it fills.
 *
 * Not "the first N calls": those are the splash and the menu, and the frame
 * worth comparing is the cockpit one that happens to be on screen when the
 * harness stops the process. A ring plus a periodic whole-file rewrite means
 * the log always holds a recent window without an fopen per call -- an
 * uncapped fopen inside the render measured itself on the Amiga side (R22) and
 * moved the fault somewhere else entirely.
 *
 * The harness kills the process, so an atexit hook would never run. */
#define PCPROJ_MAX 4000

static long pcproj_x[PCPROJ_MAX], pcproj_y[PCPROJ_MAX], pcproj_z[PCPROJ_MAX];
/* The MODEL half of the same vertex, so the two scales can be separated.
 * JUMP_005601 fills the projected vector one line before the call as
 *     [rec+0x4] = [rec+0x18] + [obj+0x4]        (ffe.asm:88696-88704)
 * and arg1 points at [rec+0x0], so [rec+0x18] is still there to be read. The
 * difference is the object's camera-relative POSITION. Logging both says
 * outright whether a scale mismatch lives in the model vertices or in how far
 * away the objects are -- one number cannot distinguish them. */
static long pcproj_cx[PCPROJ_MAX], pcproj_cy[PCPROJ_MAX], pcproj_cz[PCPROJ_MAX];
static int pcproj_n;

static void pcproj_dump (void)
{
	FILE *f;
	int i;

	/* Write a TEMP file and swap it in, never rewrite the target in place.
	 * The first attempt did the latter and the harness read back ZERO bytes:
	 * the ring fills about 25 times a second, so the process is very likely
	 * to be stopped in the middle of a 4000-line write. Swapping means the
	 * reader either sees the previous complete dump or the new complete one. */
	f = fopen ("pc_proj.tmp", "w");
	if (f == NULL)
		return;
	for (i = 0; i < PCPROJ_MAX; i++) {
		long x = pcproj_x[i], y = pcproj_y[i], z = pcproj_z[i];
		long sx, sy;

		/* Mirror the function's own two branches (ffe.asm:88743-88778)
		 * so the printed sx/sy are what the game computes, not an
		 * approximation of it. */
		if (z > 0x10000) {
			long zs = z >> 8;
			if (zs == 0)
				continue;
			sx = (x / zs) + 0xa0;
			sy = 0x4f - (y / zs);
		} else if (z != 0) {
			sx = ((x << 8) / z) + 0xa0;
			sy = 0x4f - ((y << 8) / z);
		} else {
			continue;
		}
		fprintf (f, "PCPROJ x=%ld y=%ld z=%ld sx=%ld sy=%ld %s "
		         "cam=(%ld,%ld,%ld) objpos=(%ld,%ld,%ld)\n",
		         x, y, z, sx, sy,
		         (sx >= 0 && sx < 320 && sy >= 0 && sy < 158)
		             ? "inrange" : "OUT",
		         pcproj_cx[i], pcproj_cy[i], pcproj_cz[i],
		         x - pcproj_cx[i], y - pcproj_cy[i], z - pcproj_cz[i]);
	}
	fclose (f);
	remove ("pc_proj.log");
	rename ("pc_proj.tmp", "pc_proj.log");
}

int FUNC_001479 (short *dest, const long *vertex)
{
	if (vertex != NULL) {
		const long *rec = (const long *)dest;

		pcproj_x[pcproj_n] = vertex[0];
		pcproj_y[pcproj_n] = vertex[1];
		pcproj_z[pcproj_n] = vertex[2];
		pcproj_cx[pcproj_n] = rec[6];       /* [rec+0x18] */
		pcproj_cy[pcproj_n] = rec[7];       /* [rec+0x1c] */
		pcproj_cz[pcproj_n] = rec[8];       /* [rec+0x20] */
		if (++pcproj_n == PCPROJ_MAX) {
			pcproj_n = 0;
			pcproj_dump ();
		}
	}
	return FUNC_001479_REAL (dest, vertex);
}

/* THE OBJECT POSITION, on the reference side, in raw form.
 *
 * FUNC_001683 builds the render object and fills its camera-relative position
 * from the source object (ffe.asm:102241-102246):
 *     mov edx,[eax+0x26] -> [render+0x4]
 *     mov edx,[eax+0x2e] -> [render+0x8]
 *     mov eax,[eax+0x36] -> [render+0xc]
 * Both builders copy nine dwords from the object's base first (`rep movsd`), so
 * +0x00..0x23 is the 3x3 matrix and the positions begin at +0x24, eight apart.
 * Each read therefore takes bytes 2..5 of a 64-bit value -- on x86 that is
 * bits 16..47, the integer part of a 48.16 coordinate; at the same address on
 * a big-endian machine it is a mixture of the wrong halves, and no change of
 * displacement can fix it.
 *
 * Our side prints exactly these fields. Printing them HERE too means the two
 * can be compared for the same object instead of two layouts being argued
 * about: whichever pairing reproduces the reference's numbers is the one the
 * translator has to emit.
 *
 * Signature: FUNC_001683 (void *src, void *ctx) -- `mov ebx,[ebp+0xc]` /
 * `mov eax,[ebp+0x8]` at ffe.asm:102229-102230, so the source object is arg1.
 */
extern int FUNC_001683_REAL (void *src, void *ctx) __asm__("FUNC_001683_REAL");
int FUNC_001683 (void *src, void *ctx) __asm__("FUNC_001683");
extern int FUNC_001684_REAL (void *src, void *ctx) __asm__("FUNC_001684_REAL");
int FUNC_001684 (void *src, void *ctx) __asm__("FUNC_001684");

/* One line per object, so the two logs compare by SET of positions.
 *
 * The raw-halves form this replaced has done its job: it proved the straddling
 * read at +0x26/+0x2e/+0x36 is endian-SAFE, because bytes 2..5 are symmetric
 * about the centre of an 8-byte value and reversing every byte maps that window
 * onto itself. Both sides read (0, 0, 84000) for the top-level object.
 *
 * FUNC_001683 runs once per frame for that top-level object; FUNC_001684
 * (ffe.asm:102296) builds a render object the same way for every OTHER scene
 * object, which is where the reference's 325 distinct positions and our 31 must
 * diverge. Its source object is arg1 (`mov ebx,[ebp+0x8]`, ffe.asm:102304).
 */
static void pc_objpos_log (int which, void *src)
{
	const unsigned char *p = (const unsigned char *)src;
	FILE *f = fopen ("pc_objpos.log", "a");

	if (f == NULL)
		return;
	fprintf (f, "PCOBJ%d src=%p pos=(%ld,%ld,%ld)\n", which, src,
	         *(const long *)(p + 0x26),
	         *(const long *)(p + 0x2e),
	         *(const long *)(p + 0x36));
	fclose (f);
}

/* The GATE, on the reference side. ffe.asm:77448-77454 calls FUNC_001775 only
 * when DATA_009019 != 0 and DATA_008814 == 0. Ours reads 009019=ff 009020=ff
 * 008814=0, so the gate is open; both are system-menu toggles that start at 0
 * in BSS. Printing the same two here says WHICH half of the gate differs --
 * without it, "we toggle spuriously" and "we differ on 008814" are
 * indistinguishable, and only one of them is a bug worth chasing. */
extern unsigned char DATA_009019 __asm__("DATA_009019");
extern unsigned char DATA_009020 __asm__("DATA_009020");
extern long DATA_008814 __asm__("DATA_008814");
extern unsigned char DATA_008870 __asm__("DATA_008870");

int FUNC_001683 (void *src, void *ctx)
{
	static int n_done;
	static int gated;

	if (!gated) {
		FILE *g = fopen ("pc_objpos.log", "a");

		gated = 1;
		if (g != NULL) {
			fprintf (g, "PCGATE 009019=%02x 009020=%02x 008814=%lx\n",
			         (unsigned int)DATA_009019, (unsigned int)DATA_009020,
			         (unsigned long)DATA_008814);
			fclose (g);
		}
	}

	/* 400, matching our side. At a cap of 8 this reported a single object and
	 * looked like agreement, while our side -- sampled 400 times -- alternates
	 * between two. A cap that small cannot tell "one object" from "the first
	 * of several". */
	if (n_done < 400 && src != NULL) {
		n_done++;
		pc_objpos_log (1683, src);
	}
	return FUNC_001683_REAL (src, ctx);
}

int FUNC_001684 (void *src, void *ctx)
{
	static int n_done;

	if (n_done < 400 && src != NULL) {
		n_done++;
		pc_objpos_log (1684, src);
	}
	return FUNC_001684_REAL (src, ctx);
}

/* Does the reference draw the near-camera object at all?
 *
 * FUNC_001775 (ffe.asm:111293) builds a LOCAL render object, hardcodes its
 * position to (0, 0, 512) and its model to 0x13b, and hands it to FUNC_001682.
 * That is the game's own design, not a fault -- so the question is not "why is
 * this object at the camera" but "does the reference render it too". Our side
 * produced 181 of these against the reference's zero in a matched window, and
 * a plain call count settles whether the path runs there at all.
 */
extern int FUNC_001775_REAL (void *a) __asm__("FUNC_001775_REAL");
int FUNC_001775 (void *a) __asm__("FUNC_001775");

int FUNC_001775 (void *a)
{
	static long n;
	if ((++n % 64) == 1) {
		FILE *f = fopen ("pc_objpos.log", "a");
		if (f != NULL) { fprintf (f, "PC1775 calls=%ld\n", n); fclose (f); }
	}
	return FUNC_001775_REAL (a);
}

/* IS THE GATE EVEN REACHED on the reference?
 *
 * Both sides carry the same gate variables (009019=ff 009020=ff 008814=0) yet
 * only we call FUNC_001775. Two things can explain that and a bare call count
 * cannot tell them apart, so this records the gate state AT EACH CALL:
 *   - no lines at all      -> FUNC_001052 is never reached here;
 *   - lines with 008814!=0 -> it is reached but DATA_008814 is -1 at the
 *     decisive moment, which is the half of the gate that differs.
 * FUNC_001052 takes four cdecl dwords (ffe.asm:77396-77399, callers push four
 * and add esp,0x10).
 */
extern int FUNC_001052_REAL (long a, long b, long c, long d)
	__asm__("FUNC_001052_REAL");
int FUNC_001052 (long a, long b, long c, long d) __asm__("FUNC_001052");

int FUNC_001052 (long a, long b, long c, long d)
{
	static long n;

	if (++n <= 40) {
		FILE *f = fopen ("pc_objpos.log", "a");
		if (f != NULL) {
			fprintf (f, "PC1052 n=%ld 009019=%02x 008814=%lx\n",
			         n, (unsigned int)DATA_009019,
			         (unsigned long)DATA_008814);
			fclose (f);
		}
	}
	return FUNC_001052_REAL (a, b, c, d);
}

/* THE GATE FIELD. FUNC_000880 walks a list of stellar-body records in ebx
 * (= edi = arg2, ffe.asm:64604-64607) and submits the real planet only when
 *     cmp word [ebx+0xa],byte +0x23 / jnz near JUMP_003674   (64715-64716)
 * passes. Ours runs FUNC_000880 but never calls FUNC_000879, so that compare
 * fails for us. The field is a 16-bit value out of converted game data, and a
 * byte-swapped 0x23 reads 0x2300 -- so print it, plus its neighbours at +0x8
 * and +0xc which the same block uses, and compare the two machines.
 *
 * Two cdecl dwords are enough to reach arg2; the extra ones are harmless
 * because cdecl is caller-cleaned and the forward passes the same frame on.
 */
extern long FUNC_000880_REAL (long, long) __asm__("FUNC_000880_REAL");
long FUNC_000880 (long, long) __asm__("FUNC_000880");

__attribute__((force_align_arg_pointer))
long FUNC_000880 (long a1, long a2)
{
	static long n;

	if (++n <= 4 && a2 != 0) {
		FILE *f = fopen ("pc_objpos.log", "a");

		if (f != NULL) {
			/* The same walk the function itself does: stride 0x44
			 * (ffe.asm:64875), terminated by a zero dword at [ebx]
			 * (64877-64878). */
			const unsigned char *p = (const unsigned char *)a2;
			int k;

			fprintf (f, "PCBODYLIST base=%lx", (unsigned long)a2);
			for (k = 0; k < 48 && *(const long *)p != 0; k++, p += 0x44)
				fprintf (f, " %04x",
				         (unsigned int)*(const unsigned short *)(p + 0xa));
			fprintf (f, "  n=%d\n", k);
			/* 2026-10-15: twin of BODY in amiga\probes\ffe_game.c */
			fflush (f);
			p = (const unsigned char *)a2;
			for (k = 0; k < 48 && *(const long *)p != 0; k++, p += 0x44)
				fprintf (f, "BODY k=%d par=%04x typ=%04x mass=%08lx orb=%08lx lat=%04x name=%.20s\n",
				         k, *(const unsigned short *)(p + 0x8),
				         *(const unsigned short *)(p + 0xa),
				         (unsigned long)*(const long *)(p + 0x0),
				         (unsigned long)*(const long *)(p + 0xe),
				         *(const unsigned short *)(p + 0x12), (const char *)(p + 0x26));
			fclose (f);
		}
	}
	return FUNC_000880_REAL (a1, a2);
}

/* WHICH SITE RUNS THE ARRIVAL PATH ON THE REFERENCE.
 *
 * Our run reaches the generator only through the module REFRESH event
 * (FUNC_001000 -> FUNC_000898 -> FUNC_000904 -> FUNC_000905), measured with
 * probes; FUNC_000908 and FUNC_000909 never fire for us. The reference must run
 * FUNC_000909 as well, because its rec+0x10 is RECOMPUTED in FUNC_000860 (0xe6)
 * where ours is COPIED from a stale global (0x0c) -- and that one field is the
 * whole 8-bodies-against-27 difference.
 *
 * FUNC_000909 has exactly two call sites: ffe.asm:1996 and 67588 (inside
 * FUNC_000908). Printing its return address says which one, and hooking
 * FUNC_000908 alongside says whether that wrapper runs at all.
 *
 * Two cdecl dwords: the 67588 site pushes byte +0x1 and [ebx+0x27c], and the
 * body reads [ebp+0x8] and [ebp+0xc].
 */
extern long FUNC_000909_REAL (long, long) __asm__("FUNC_000909_REAL");
long FUNC_000909 (long, long) __asm__("FUNC_000909");
extern void FUNC_000908 (void) __asm__("FUNC_000908");

long FUNC_000909 (long a1, long a2)
{
	static long n;

	if (++n <= 4) {
		FILE *f = fopen ("pc_objpos.log", "a");

		if (f != NULL) {
			fprintf (f, "PC909 n=%ld ret=%p a1=%lx a2=%lx  908=%p\n",
			         n, __builtin_return_address (0),
			         (unsigned long)a1, (unsigned long)a2,
			         (void *)FUNC_000908);
			fclose (f);
		}
	}
	return FUNC_000909_REAL (a1, a2);
}

/* WHICH RECORD OF THE STARTING-POSITION TABLE A NEW GAME USES.
 *
 * FUNC_000829 picks it with `eax = (arg1 & 0xff00) >> 8` (ffe.asm:56226-56228)
 * after requiring the LOW byte to be 1 (`and edx,0xff / dec edx / jnz`). The
 * reference lands on record 0 and we land on record 1, which is the entire
 * remaining difference between the two star systems -- our packed id already
 * agrees with the reference on x (0x1717) and is one step off on y.
 *
 * FUNC_000829 is reached indirectly, as event 0 of the module event table
 * DATA_006798 (itself entry 2 of DATA_004892, "Module event functions
 * tables"), so the return address is worth as much as the argument here.
 */
extern long FUNC_000829_REAL (long, long, long) __asm__("FUNC_000829_REAL");
long FUNC_000829 (long, long, long) __asm__("FUNC_000829");

long FUNC_000829 (long a1, long a2, long a3)
{
	static long n;

	if (++n <= 4) {
		FILE *f = fopen ("pc_objpos.log", "a");

		if (f != NULL) {
			fprintf (f, "PC829 n=%ld ret=%p a1=%lx a2=%lx a3=%lx"
			            "  rec=%ld\n",
			         n, __builtin_return_address (0),
			         (unsigned long)a1, (unsigned long)a2,
			         (unsigned long)a3, (long)((a1 & 0xff00) >> 8));
			fclose (f);
		}
	}
	/* FORCE THE SAME STARTING SCENARIO AS THE AMIGA RUN.
	 *
	 * The two harnesses do not reach the same menu state: our WinUAE run
	 * registers the '1' keypress as GUI action 0x31, so FUNC_000909's caller
	 * takes `mov edi,[eax*4+DATA_007191]' -> DATA_007199+4 = scenario 1
	 * (ffe.asm:68453), while the reference falls through to the default
	 * `mov edi,[DATA_007199]' = scenario 0 (68457) -- none of the five keyed
	 * entries is 0, so it cannot be reached by a key at all. Different
	 * scenario means a different record of DATA_006820, a different system id
	 * and legitimately a different star system, which would make any body-count
	 * comparison meaningless.
	 *
	 * FUNC_000147 packs the scenario into the high byte (`shl eax,8 / add
	 * eax,esi', ffe.asm:19450-19452), so setting bit 8 here selects record 1 on
	 * the reference too. Set FFE_SCENARIO in the environment to compare a
	 * different one; unset leaves the reference exactly as it ships.
	 */
	{
		const char *s = getenv ("FFE_SCENARIO");

		if (s != NULL && (a1 & 0xff) != 0)
			a1 = (a1 & 0xff) | ((atoi (s) & 0xff) << 8);
	}
	return FUNC_000829_REAL (a1, a2, a3);
}

/* WHERE THE SYSTEM ID IS PACKED.
 *
 * FUNC_000878's record carries the system id at +0xc (FUNC_000860 fills it,
 * ffe.asm:60292-60293), and the two machines disagree there: reference
 * 0x1aa51717, ours 0x00400600. The id is a packed triple (FUNC_000852,
 * ffe.asm:59681-59700): x = bits 0..12, y = bits 13..25, index = bits 26..31.
 *
 * FUNC_000832 is what packs it, into [DATA_008804+0x27c], from
 *     x     = [ebx+0x25c] sar 16, & 0x1fff     -> a WORD store at +0x27c
 *     y     = [ebx+0x260] sar 16, & 0x1fff     -> a DWORD store at +0x27c
 *     index = arg1 & 0x3f, << 2                -> a BYTE store at +0x27f
 * -- three sub-word accesses into one dword field, which is the endianness
 * question in its purest form. Printing the SOURCES here separates the only two
 * possibilities: if x/y agree across the machines the packing is at fault (ours,
 * fixable in the translator), and if they already differ the fault is upstream
 * of this function entirely.
 */
extern long FUNC_000832_REAL (long, long, long) __asm__("FUNC_000832_REAL");
long FUNC_000832 (long, long, long) __asm__("FUNC_000832");
extern unsigned char DATA_008804[] __asm__("DATA_008804");
extern unsigned char DATA_009127[] __asm__("DATA_009127");
extern unsigned char DATA_009200[] __asm__("DATA_009200");
extern unsigned char DATA_004681[] __asm__("DATA_004681");

long FUNC_000832 (long a1, long a2, long a3)
{
	static long n;
	long mine = ++n;
	long r;

	if (mine <= 3) {
		FILE *f = fopen ("pc_objpos.log", "a");

		if (f != NULL)
			fprintf (f, "PC832in n=%ld a1=%lx a2=%lx a3=%lx"
			            "  x25c=%lx y260=%lx id27c=%lx\n",
			         mine, (unsigned long)a1, (unsigned long)a2,
			         (unsigned long)a3,
			         (unsigned long)*(const long *)(DATA_008804 + 0x25c),
			         (unsigned long)*(const long *)(DATA_008804 + 0x260),
			         (unsigned long)*(const long *)(DATA_008804 + 0x27c)),
			fclose (f);
	}

	if (mine <= 16) {
		/* 2026-10-15: twin of P832 in amiga\probes\ffe_game.c -- positions
		 * and the PREVIOUS call's distance (+0x26c is written after entry) */
		FILE *f = fopen ("pc_objpos.log", "a");

		if (f != NULL)
			fprintf (f, "P832 n=%ld cur=%lx,%lx,%lx tgt=%lx,%lx,%lx dist=%lx\n",
			         mine,
			         (unsigned long)*(const long *)(DATA_008804 + 0x250),
			         (unsigned long)*(const long *)(DATA_008804 + 0x254),
			         (unsigned long)*(const long *)(DATA_008804 + 0x258),
			         (unsigned long)*(const long *)(DATA_008804 + 0x25c),
			         (unsigned long)*(const long *)(DATA_008804 + 0x260),
			         (unsigned long)*(const long *)(DATA_008804 + 0x264),
			         (unsigned long)*(const long *)(DATA_008804 + 0x26c)),
			fclose (f);
	}

	r = FUNC_000832_REAL (a1, a2, a3);

	if (mine <= 3) {
		FILE *f = fopen ("pc_objpos.log", "a");

		if (f != NULL)
			fprintf (f, "PC832out n=%ld id27c=%lx\n", mine,
			         (unsigned long)*(const long *)(DATA_008804 + 0x27c)),
			fclose (f);
	}
	return r;
}

/* THE PRODUCER OF THE BODY LIST.
 *
 * FUNC_000905 fills the list and then hands it to the walker, two lines apart:
 *     push ebx / push dword DATA_008798 / call FUNC_000878    (ffe.asm:67418-20)
 *     ... push dword DATA_008798 / call FUNC_000880           (67430-67433)
 * so the list is a STATIC buffer and FUNC_000878(list, sysrec) is what fills it.
 * The reference puts 27 records there and we put 7, so the divergence is either
 * in the system record `sysrec` that comes in, or inside the generator.
 *
 * This shim prints the incoming record and then walks the finished list exactly
 * as the walker does, which separates those two cases in a single run.
 *
 * The record fields the generator itself reads (ffe.asm:63859-63905): +0x0 and
 * +0x4 (the seed pair, -> DATA_008843/8844), +0x8 -- read BOTH as a byte (63898,
 * -> DATA_008850/8851) and as a DWORD (63901) whose value picks the generation
 * shape via `sub eax,1 / jc / jz` -- +0xc (-> DATA_008853, the argument to
 * FUNC_000871) and +0x10. Print +0x8 both ways: if the field is really a byte
 * and the neighbouring bytes are not zero, the dword read is exactly the kind of
 * thing that takes a different branch on a big-endian machine.
 */
extern void FUNC_000878_REAL (long, long) __asm__("FUNC_000878_REAL");
void FUNC_000878 (long, long) __asm__("FUNC_000878");

void FUNC_000878 (long a1, long a2)
{
	static long n;
	long mine = ++n;

	if (mine <= 3 && a2 != 0) {
		FILE *f = fopen ("pc_objpos.log", "a");

		if (f != NULL) {
			const unsigned char *r = (const unsigned char *)a2;

			fprintf (f, "PC878in n=%ld list=%lx rec=%lx cmdr27c=%lx"
			            "  r0=%lx r4=%lx r8b=%02x r8d=%lx rc=%lx r10=%lx\n",
			         mine, (unsigned long)a1, (unsigned long)a2,
			         (unsigned long)*(const long *)(DATA_008804 + 0x27c),
			         (unsigned long)*(const long *)(r + 0x0),
			         (unsigned long)*(const long *)(r + 0x4),
			         (unsigned int)r[0x8],
			         (unsigned long)*(const long *)(r + 0x8),
			         (unsigned long)*(const long *)(r + 0xc),
			         (unsigned long)*(const long *)(r + 0x10));
			fclose (f);
		}
	}

	FUNC_000878_REAL (a1, a2);

	if (mine <= 3 && a1 != 0) {
		FILE *f = fopen ("pc_objpos.log", "a");

		if (f != NULL) {
			const unsigned char *p = (const unsigned char *)a1;
			int k;

			fprintf (f, "PC878out n=%ld", mine);
			for (k = 0; k < 48 && *(const long *)p != 0; k++, p += 0x44)
				fprintf (f, " %04x",
				         (unsigned int)*(const unsigned short *)(p + 0xa));
			fprintf (f, "  n=%d\n", k);
			/* +0x24 is the size/radius word FUNC_000883 types each body from,
			 * against a ladder of constants (0x139, 0x152, 0x212, 0xc8 ...).
			 * A wrong value there shows up as a wrong TYPE with nothing else
			 * obviously out of place, which is exactly our symptom from body
			 * 6 on, so the two lists have to be compared at this field too. */
			p = (const unsigned char *)a1;
			fprintf (f, "PCR24out n=%ld", mine);
			for (k = 0; k < 48 && *(const long *)p != 0; k++, p += 0x44)
				fprintf (f, " %04x",
				         (unsigned int)*(const unsigned short *)(p + 0x24));
			fprintf (f, "  n=%d\n", k);
			fclose (f);
		}
	}
}

/* WHERE rec+0x10 COMES FROM -- and it is NOT a byte-order question.
 *
 * rec+0x10 is copied straight out of [DATA_008962] (FUNC_000855, ffe.asm:59905),
 * and DATA_008962 = DATA_008804+0x28c has exactly ONE writer in the whole
 * disassembly: FUNC_000869's arg4, called from FUNC_000832 at ffe.asm:56704.
 * The other five call sites of FUNC_000869 (19566, 42680, 45842, 58546, 60287)
 * all write into stack locals, so nothing else can touch it.
 *
 * FUNC_000869 is a pure lookup: FUNC_000868(id) returns an index, and every
 * output is a field of the 54-byte record at DATA_007110 + 54*index. Decoded
 * from ffedat.asm:15200-15300, field +5 (which becomes arg4, hence rec+0x10) is
 *     index  -8 -7 -6 -5 -4 -3 -2 -1
 *     +5     00 02 0c 33 6e 8c cc e6
 * so OUR 0x0c is index -6 and the REFERENCE's 0xe6 is index -1. Index -1 is
 * returned from exactly one place: `or eax,byte -0x1` after the id matches one
 * of the four dwords in DATA_007116 (ffe.asm:60804-60807).
 *
 * Hand-running FUNC_000868 on the id both machines were measured to hold,
 * 0x1aa51717, gives -6: no handcoded syscode matches (they are all 0x??a49718),
 * the masked id 0x02a51717 matches none of DATA_007116's four, and the distance
 * term comes to 149, which lands in the [0x64,0x121) bucket = -6. That is OUR
 * measured answer, exactly. So the translation of FUNC_000868/869 is right for
 * that input, and the reference CANNOT be passing the same id here.
 *
 * This shim settles it in one run: the id FUNC_000869 actually receives, the
 * call site it came from, and the byte it writes back through arg4.
 */
extern void FUNC_000869_REAL (long, long *, long *, long *, long *, long *,
                              long *, long *, long *) __asm__("FUNC_000869_REAL");
void FUNC_000869 (long, long *, long *, long *, long *, long *, long *,
                  long *, long *) __asm__("FUNC_000869");
extern long FUNC_000868 (long) __asm__("FUNC_000868");

void FUNC_000869 (long id, long *o2, long *o3, long *o4, long *o5, long *o6,
                  long *o7, long *o8, long *o9)
{
	static long n;
	long mine = ++n;

	FUNC_000869_REAL (id, o2, o3, o4, o5, o6, o7, o8, o9);

	if (mine <= 12) {
		FILE *f = fopen ("pc_objpos.log", "a");

		if (f != NULL) {
			fprintf (f, "PC869 n=%ld ret=%p id=%lx idx=%ld"
			            "  o2=%lx o3=%lx o4=%lx o5=%lx o9=%lx"
			            "  cmdr27c=%lx d8962=%lx\n",
			         mine, __builtin_return_address (0),
			         (unsigned long)id, (long)FUNC_000868 (id),
			         (unsigned long)*o2, (unsigned long)*o3,
			         (unsigned long)*o4, (unsigned long)*o5,
			         (unsigned long)*o9,
			         (unsigned long)*(const long *)(DATA_008804 + 0x27c),
			         (unsigned long)*(const long *)(DATA_008804 + 0x28c));
			fclose (f);
		}
	}
}

/* THE STAR ITSELF -- the first thing the generator computes.
 *
 * With rec+0x10 fixed on 2026-08-05 the record FUNC_000878 receives is now
 * IDENTICAL to the reference field for field (r0=8d17f528 r4=a21fe80 r8d=0
 * rc=1aa51717 r10=e6 on both), yet the body list is still 8 records against 27,
 * and the FIRST divergence is the second body: reference 1a 0d 0e 02 ...,
 * ours 1a 0e 02 ... -- one body of type 0x0d simply missing.
 *
 * FUNC_000878's very first call is FUNC_000871 (ffe.asm:63878-63888), seven
 * cdecl dwords: the system id, five out-pointers and the table DATA_007128.
 * Everything after it -- the star's class, its mass, the orbital radii the
 * recursive generator in FUNC_000881 lays planets into -- descends from those
 * five values, so if they already differ nothing downstream can agree, and if
 * they agree the fault is inside FUNC_000881.
 */
extern void FUNC_000871_REAL (long, long *, long *, long *, long,
                              long *, long *) __asm__("FUNC_000871_REAL");
void FUNC_000871 (long, long *, long *, long *, long, long *, long *)
	__asm__("FUNC_000871");

void FUNC_000871 (long id, long *o2, long *o3, long *o4, long tab,
                  long *o6, long *o7)
{
	static long n;
	long mine = ++n;

	FUNC_000871_REAL (id, o2, o3, o4, tab, o6, o7);

	if (mine <= 4) {
		FILE *f = fopen ("pc_objpos.log", "a");

		if (f != NULL) {
			fprintf (f, "PC871 n=%ld id=%lx tab=%lx -> o2=%lx o3=%lx"
			            " o4=%lx o6=%lx o7=%lx\n",
			         mine, (unsigned long)id, (unsigned long)tab,
			         (unsigned long)*o2, (unsigned long)*o3,
			         (unsigned long)*o4, (unsigned long)*o6,
			         (unsigned long)*o7);
			fclose (f);
		}
	}
}

/* THE BODY GENERATOR ITSELF.
 *
 * Everything upstream now agrees: the record FUNC_000878 receives is identical
 * field for field, and FUNC_000871 -- the star -- is oracle-green on the 68020.
 * The body list is still 8 records against 27, and the first divergence is the
 * SECOND body (reference 1a 0d 0e 02 ..., ours 1a 0e 02 ...), so the fault is
 * inside FUNC_000881, which FUNC_000878 calls once at ffe.asm:63924 and which
 * then recurses through five more call sites (63617, 63639, 63784, 63812,
 * 66942) to hang moons off planets.
 *
 * Eleven cdecl dwords (`add esp,byte +0x2c'). arg1 is the list, arg2 the
 * pointer to the running body COUNT -- printing *arg2 at entry and again at
 * exit says exactly how many bodies each call contributed, and the return
 * address says which of the six call sites made it. That pair is what our own
 * probe prints too, so the two logs line up call by call and the first row that
 * differs names the branch.
 */
extern long FUNC_000881_REAL (long, long *, long, long, long, long, long,
                              long, long, long, long) __asm__("FUNC_000881_REAL");
long FUNC_000881 (long, long *, long, long, long, long, long, long, long,
                  long, long) __asm__("FUNC_000881");
extern unsigned char DATA_008843[] __asm__("DATA_008843");
extern unsigned char DATA_008844[] __asm__("DATA_008844");

long FUNC_000881 (long a1, long *a2, long a3, long a4, long a5, long a6,
                  long a7, long a8, long a9, long a10, long a11)
{
	static long n;
	long mine = ++n;
	long before = (a2 != NULL) ? *a2 : -1;
	long r;
	FILE *f;

	r = FUNC_000881_REAL (a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11);

	if (mine <= 40) {
		f = fopen ("pc_objpos.log", "a");
		if (f != NULL) {
			fprintf (f, "PC881 n=%2ld ret=%p a3=%lx a4=%lx a5=%lx a6=%lx"
			            "  cnt %ld->%ld  r=%lx  s0=%lx s1=%lx\n",
			         mine, __builtin_return_address (0),
			         (unsigned long)a3, (unsigned long)a4,
			         (unsigned long)a5, (unsigned long)a6,
			         before, (a2 != NULL) ? *a2 : -1, (unsigned long)r,
			         (unsigned long)*(const long *)DATA_008843,
			         (unsigned long)*(const long *)DATA_008844);
			fclose (f);
		}
	}
	return r;
}

/* WHAT FILLS ARENA NODE 6 -- the vertex resolver.
 *
 * Measured 2026-08-05: both machines hand FUNC_001877 the SAME arena slot
 * (idx=6, a6-arena = 0xdc on both) with DIFFERENT contents, so the model
 * bytecode byte that selects the node is right and whatever fills it is not.
 * FUNC_001478 is that filler: it computes the node itself as
 * `index*36 + [DATA_009200]' (ffe.asm:87753-87757) and the vertex DESCRIPTOR as
 * `[ctx+0x150] + (index>>1)*6' (87759-87764), then dispatches on the type byte.
 *
 * Three cdecl dwords (`add esp,byte +0xc' at both call sites, ffe.asm:87016 and
 * 87098): (ctx, index, flag). Printing the six descriptor bytes at ENTRY on
 * both machines splits the last question in two: identical descriptors mean the
 * handler's arithmetic is wrong, differing ones mean the model data feeding it
 * is. Filtered to index 6 -- the slot that actually reaches the planet -- because
 * this function runs for every vertex of every object.
 */
/* THE MODEL NUMBER ITSELF.
 *
 * FUNC_001538 is a one-line lookup -- `mov eax,[ebp+0x8] / mov eax,
 * [eax*4+DATA_004681]' (ffe.asm:91412-91417) -- so its ARGUMENT is the model
 * number an object asks for. Measured 2026-08-05: the two machines interpret
 * different models (ref 176 and 360, ours 138, 130, 85, 270), and this says
 * whether the divergence is in the numbers being asked for or in which objects
 * do the asking.
 *
 * Ring dumped at the consumer, like every other pairing in this file: a count
 * or a sample taken at an arbitrary moment cannot be set against the other
 * machine's.
 *
 * THE RING OF 8 IS NOT ENOUGH, measured 2026-08-05. At the first scene render
 * the totals nearly agree -- 455 here against 446 on the Amiga, a deficit of
 * exactly 9 -- but the last eight requests DIFFER (48,48,28,28,36,36,31,139
 * against their 34,24,34,24,34,24,31,139), and at the second render the rings
 * are identical again. So the two request sequences part company somewhere
 * inside the first 455 calls, which a window of 8 can never locate. Hence the
 * WHOLE sequence, in order, up to a cap -- this side only reaches 834 by n=8,
 * so 4096 is generous, and if it is ever hit the dump says `cap=1' instead of
 * quietly dropping the tail.
 */
#define M1538_RING 8
#define M1538_MAX 4096		/* entries kept; the counter itself is uncapped */
#define M1538_CHUNK 64		/* numbers per output line -- see the dump */
static long m1538[M1538_RING];
static long m1538_n;
static long m1538_seq[M1538_MAX];

/* WHICH CALL SITE ASKS FOR THE BAD NUMBERS -- the mirror of MODEL1538RET.
 *
 * Measured 2026-08-05 over the whole sequence: NONE of this side's 389 requests
 * is outside DATA_004681, while 214 of the Amiga's 458 are, and the bad values
 * are exactly two -- 0xB0000000 (x114) and 0xB1000000 (x100), i.e. 176 and 177
 * displaced into the TOP byte of the dword. Reading the call sites does not
 * settle which one produces them: FUNC_001749/FUNC_001750 pass `arg3 >> 5' and
 * a logical shift right can never yield 0xB0000000, so those two are excluded
 * by arithmetic, leaving the direct calls at ffe.asm:37442, 37446, 37557, 91005,
 * 91318 and the indirect `call near [DATA_007756]' at 109004. So let the caller
 * name ITSELF: histogram the return address, split by whether the number asked
 * for is in range. This side is the control -- every count must land in the
 * in-range column, and the addresses say which sites are live at all.
 *
 * Capped at M1538_SITES distinct addresses because a histogram that silently
 * forgets is worse than no histogram: calls that do not fit are counted in
 * `ovf' and the count is printed on the line.
 */
#define M1538_ENTRIES 466	/* DATA_004681's entry count; outside == bad */
#define M1538_SITES 16
static struct { unsigned long ret; long n, bad; } m1538_site[M1538_SITES];
static long m1538_sites, m1538_ovf;

/* Kept byte-for-byte the same shape as the Amiga's ffe_m1538_site_add so the
 * two logs diff directly. */
static void m1538_site_add (unsigned long ret, long index)
{
	int bad = (index < 0 || index >= M1538_ENTRIES);
	int s;

	for (s = 0; s < m1538_sites; s++)
		if (m1538_site[s].ret == ret) break;
	if (s == m1538_sites) {
		if (m1538_sites >= M1538_SITES) { m1538_ovf++; return; }
		m1538_site[s].ret = ret;
		m1538_sites++;
	}
	m1538_site[s].n++;
	if (bad) m1538_site[s].bad++;
}

/* Chunked into M1538_CHUNK numbers per line rather than one enormous one: a
 * 4096-entry sequence is ~20 KB on a single line, and this log is read back
 * through tools that were never asked to hold one. Wrapping is not truncating
 * -- every entry is printed, and `p=' numbers the continuation lines so the
 * order survives. Format is byte-for-byte the Amiga's MODEL1538SEQ apart from
 * the prefix, so the two logs diff directly. */
static void m1538_dump (FILE *f, long n)
{
	long kept = m1538_n < M1538_MAX ? m1538_n : M1538_MAX;
	long i;
	int j;

	fprintf (f, "PC1538 total=%ld models=", m1538_n);
	for (j = 0; j < M1538_RING; j++) {
		int k = (int)((m1538_n + j) % M1538_RING);
		if (m1538_n > j || m1538[k]) fprintf (f, "%ld,", m1538[k]);
	}
	fprintf (f, "\n");

	/* `i == 0 ||' so a dump with nothing recorded still emits its header line
	 * -- an empty sequence must be visible as one, not as a missing line. */
	for (i = 0; i == 0 || i < kept; i += M1538_CHUNK) {
		long e = i + M1538_CHUNK < kept ? i + M1538_CHUNK : kept;
		long q;

		fprintf (f, "PC1538SEQ n=%ld p=%ld total=%ld kept=%ld cap=%d ",
		         n, i / M1538_CHUNK, m1538_n, kept,
		         m1538_n > M1538_MAX ? 1 : 0);
		for (q = i; q < e; q++)
			fprintf (f, "%s%ld", q > i ? "," : "", m1538_seq[q]);
		fprintf (f, "\n");
	}

	/* The call-site census, one `retaddr:total:outofrange' per site. `slots'
	 * is how many distinct addresses were seen and `ovf' how many CALLS were
	 * dropped for want of a slot -- printed even when zero, because a silent
	 * cap would read as "that site does not exist". */
	fprintf (f, "PC1538RET n=%ld slots=%ld ovf=%ld", n, m1538_sites, m1538_ovf);
	for (i = 0; i < m1538_sites; i++)
		fprintf (f, " %lx:%ld:%ld", m1538_site[i].ret,
		         m1538_site[i].n, m1538_site[i].bad);
	fprintf (f, "\n");
}

extern long FUNC_001538_REAL (long index) __asm__("FUNC_001538_REAL");
long FUNC_001538 (long index) __asm__("FUNC_001538");

long FUNC_001538 (long index)
{
	/* THE ORIGINAL CALLER'S RETURN ADDRESS. This shim IS FUNC_001538 -- the
	 * generated ffe.asm renames the real routine to FUNC_001538_REAL, so every
	 * `call FUNC_001538' in the game lands here directly and level 0 is the
	 * game's own call site, not an injected wrapper's. Taken before the
	 * forward, so the sibling call below cannot matter. */
	m1538_site_add ((unsigned long)__builtin_return_address (0), index);
	m1538[m1538_n % M1538_RING] = index;
	if (m1538_n < M1538_MAX) m1538_seq[m1538_n] = index;
	m1538_n++;
	return FUNC_001538_REAL (index);
}

/* PC1745 -- the mirror of the Amiga FfeGate1745 (2026-09-02). FUNC_001745 is the
 * sub-model descent cull: esi = (a1+a2)*36 + a3*8 + 0xc + [DATA_007823], skip
 * when esi > 0x8000. MODREL showed we descend into 3 sub-models the reference
 * culls; recording the DISTINCT input tuples here lets the set diff directly. */
extern long FUNC_001745_REAL (long, long, long) __asm__("FUNC_001745_REAL");
long FUNC_001745 (long, long, long) __asm__("FUNC_001745");
extern long DATA_007823[] __asm__("DATA_007823");
long FUNC_001745 (long a1, long a2, long a3)
{
	static struct { long a1, a3, pool; } seen[128];
	static int nseen;
	long pool = DATA_007823[0];
	long esi = (a1 + a2) * 36 + a3 * 8 + 0xc + pool;
	int i, dup = 0;

	for (i = 0; i < nseen; i++)
		if (seen[i].a1 == a1 && seen[i].a3 == a3 && seen[i].pool == pool) { dup = 1; break; }
	if (!dup && nseen < 128) {
		FILE *f = fopen ("pc_objpos.log", "a");
		seen[nseen].a1 = a1; seen[nseen].a3 = a3; seen[nseen].pool = pool; nseen++;
		if (f) {
			fprintf (f, "PC1745 a1=%ld a2=%ld a3=%ld pool=%ld esi=%ld cull=%d\n",
			         a1, a2, a3, pool, esi, (int)(esi > 0x8000));
			fclose (f);
		}
	}
	return FUNC_001745_REAL (a1, a2, a3);
}

extern long FUNC_001478_REAL (void *ctx, long index, long flag)
	__asm__("FUNC_001478_REAL");
long FUNC_001478 (void *ctx, long index, long flag) __asm__("FUNC_001478");

/* A RING, not the first N calls, and this is the whole point of the rewrite.
 * The first cut logged the first four calls with index 6 across ALL objects and
 * the two machines sampled DIFFERENT models -- `vtab' differed on every line, so
 * the descriptors could not be compared at all. What matters is the call that
 * LAST wrote node 6 before FUNC_001876 read it, so the calls go into a ring and
 * the ring is printed from the FUNC_001876 shim. A ring also removes the fopen
 * per vertex call, which would otherwise measure itself. */
#define V1478_RING 4
static struct {
	unsigned long ctx, vtab;
	long flag, seq;
	unsigned char desc[6];
} v1478[V1478_RING];
static long v1478_n;

static void v1478_dump (FILE *f)
{
	int i;

	for (i = 0; i < V1478_RING; i++) {
		int k = (int)((v1478_n + i) % V1478_RING);

		if (v1478[k].seq == 0)
			continue;
		fprintf (f, "PC1478R seq=%ld ctx=%lx vtab=%lx flag=%ld"
		            " desc=%02x,%02x,%02x,%02x,%02x,%02x\n",
		         v1478[k].seq, v1478[k].ctx, v1478[k].vtab, v1478[k].flag,
		         v1478[k].desc[0], v1478[k].desc[1], v1478[k].desc[2],
		         v1478[k].desc[3], v1478[k].desc[4], v1478[k].desc[5]);
	}
}

/* WHO CALLS THE VERTEX RESOLVER, AND HOW OFTEN.
 *
 * Measured 2026-08-05: the reference resolves node 6 189 times before the first
 * scene render and we resolve it once, by a different route (flag=1 there,
 * flag=0 here). FUNC_001470 has TWELVE call sites in FIVE functions --
 * FUNC_001696 (six of them), FUNC_001697, FUNC_001721, FUNC_001747 (three) and
 * FUNC_001874 -- so a histogram of RETURN ADDRESSES names the loop that runs
 * there and not here, with one hook instead of five.
 *
 * Printed from the FUNC_001876 shim, at the consumer, for the same reason the
 * descriptor ring is: a count sampled at an arbitrary moment cannot be set
 * against the other machine's.
 */
#define V1470_SLOTS 8
static struct { unsigned long ret; long n; } v1470[V1470_SLOTS];
static long v1470_total;

static void v1470_note (unsigned long ret)
{
	int i;

	v1470_total++;
	for (i = 0; i < V1470_SLOTS; i++) {
		if (v1470[i].ret == ret) { v1470[i].n++; return; }
		if (v1470[i].ret == 0) { v1470[i].ret = ret; v1470[i].n = 1; return; }
	}
}

/* ALL FIVE ARE NOW HOOKED, so in every case the symbol `FUNC_00nnnn' is the
 * SHIM and the real body is `FUNC_00nnnn_REAL'. The return addresses in PC1470R
 * point into the REAL bodies, so those are the addresses these lines must print
 * -- printing a shim's would silently shift the caller mapping by the size of
 * the shim and mis-attribute every call from it. 1697 was already in this shape
 * on 2026-08-05; 1696, 1721, 1747 and 1874 joined it on 2026-08-06 when the
 * whole dispatch table was hooked for the opcode sequence, and the compiler
 * caught it as a type clash rather than letting it become a wrong answer. */
extern void FUNC_001696r (void) __asm__("FUNC_001696_REAL");
extern void FUNC_001697r (void) __asm__("FUNC_001697_REAL");
extern void FUNC_001721r (void) __asm__("FUNC_001721_REAL");
extern void FUNC_001747r (void) __asm__("FUNC_001747_REAL");
extern void FUNC_001874v (void) __asm__("FUNC_001874_REAL");

static void v1470_dump (FILE *f)
{
	int i;

	fprintf (f, "PC1470T total=%ld  1696=%p 1697=%p 1721=%p 1747=%p 1874=%p\n",
	         v1470_total, (void *)FUNC_001696r, (void *)FUNC_001697r,
	         (void *)FUNC_001721r, (void *)FUNC_001747r, (void *)FUNC_001874v);
	for (i = 0; i < V1470_SLOTS && v1470[i].ret; i++)
		fprintf (f, "PC1470R ret=%lx n=%ld\n", v1470[i].ret, v1470[i].n);
}

extern long FUNC_001470_REAL (void *ctx, long index) __asm__("FUNC_001470_REAL");
long FUNC_001470 (void *ctx, long index) __asm__("FUNC_001470");

/* The mirror of the Amiga side's NODE1470. FUNC_001470 makes the position as a
 * SUM -- rec[+0x4/8/c] = rec[+0x18/1c/20] + ctx[+0x4/8/c], indexed by a SIGNED
 * BYTE with a 36-byte stride -- and CULL1687 measured that position differing
 * between the machines with the size agreeing. Recording the INDEX, the
 * CONTEXT position and the LOCAL OFFSET separately is what says which of the
 * three is already wrong; the other two are then downstream.
 *
 * Read AFTER the call here, where the offset is filled; the Amiga side defers
 * its read-back to the next call because inject_count reaches only the entry.
 * Different mechanics, same three quantities. */
#define PCS70_MAX 10
long pcs70_idx[PCS70_MAX], pcs70_cx[PCS70_MAX], pcs70_cy[PCS70_MAX];
long pcs70_cz[PCS70_MAX], pcs70_ox[PCS70_MAX], pcs70_oy[PCS70_MAX];
long pcs70_oz[PCS70_MAX], pcs70_n, pcs70_total;

long FUNC_001470 (void *ctx, long index)
{
	long r;

	v1470_note ((unsigned long)__builtin_return_address (0));
	r = FUNC_001470_REAL (ctx, index);
	pcs70_total++;
	if (pcs70_n < PCS70_MAX && ctx != NULL && r) {
		const char *c = (const char *)ctx;

		pcs70_idx[pcs70_n] = (long)(signed char)(index & 0xff);
		pcs70_cx[pcs70_n] = *(const long *)(c + 0x4);
		pcs70_cy[pcs70_n] = *(const long *)(c + 0x8);
		pcs70_cz[pcs70_n] = *(const long *)(c + 0xc);
		pcs70_ox[pcs70_n] = *(const long *)(r + 0x18);
		pcs70_oy[pcs70_n] = *(const long *)(r + 0x1c);
		pcs70_oz[pcs70_n] = *(const long *)(r + 0x20);
		pcs70_n++;
	}
	return r;
}

/* model index owning a vertex table: the largest DATA_004681 entry <= vt */
static long v8_model_of (const unsigned char *vt)
{
	const unsigned long *tab = (const unsigned long *)DATA_004681;
	unsigned long best = 0; long bi = -1; int i;
	for (i = 0; i < 466; i++)
		if (tab[i] <= (unsigned long)vt && tab[i] > best) { best = tab[i]; bi = i; }
	return bi;
}
static void pcv8_rec (void *ctx, long index, long rec)
{
	static int n;
	extern long pcf8_state_pub (void);
	if (pcf8_state_pub () != 2 || n >= 1500 || (signed char)index < 0 || !ctx) return;
	{
		const unsigned char *vt = *(const unsigned char *const *)((const char *)ctx + 0x150);
		const unsigned char *d = vt + (((signed char)index) >> 1) * 6;
		const short *s = (const short *)rec;
		const long *l = (const long *)rec;
		FILE *f = fopen ("pc_frame1478.log", n ? "a" : "w");
		n++;
		if (f) {
			const long *c = (const long *)ctx;
			fprintf (f, "V %02x%02x%02x%02x%02x%02x s=%d,%d p=%ld,%ld,%ld o=%ld,%ld,%ld m=%ld,%ld,%ld,%ld,%ld,%ld,%ld,%ld,%ld k=%ld\n",
			         d[0], d[1], d[2], d[3], d[4], d[5], s[0], s[1], l[1], l[2], l[3],
			         c[1], c[2], c[3], c[5], c[6], c[7], c[8], c[9], c[10], c[11], c[12], c[13], v8_model_of (vt));
			fclose (f);
		}
	}
}
long FUNC_001478 (void *ctx, long index, long flag)
{
	if (index == 6 && ctx != NULL) {
		const unsigned char *vt =
			*(const unsigned char *const *)((const char *)ctx + 0x150);
		const unsigned char *d = vt + (index >> 1) * 6;
		int k = (int)(v1478_n % V1478_RING);
		int q;

		v1478[k].ctx = (unsigned long)ctx;
		v1478[k].vtab = (unsigned long)vt;
		v1478[k].flag = flag;
		v1478[k].seq = ++v1478_n;
		for (q = 0; q < 6; q++) v1478[k].desc[q] = d[q];
	}
	{
		/* PC1478D -- the mirror of the Amiga side's VTXANOM (2026-09-02).
		 * Every DISTINCT (index, 6-byte descriptor) pair seen is printed
		 * ONCE with the node offset FUNC_001478 produced for it (arena
		 * [DATA_009200] + index*36 + 0x18), so the Amiga anomaly -- an
		 * offset of (N*2^21 - 1) -- can be matched by key against what the
		 * original computes for the same vertex. A few hundred lines. */
		static struct { long idx; unsigned char d[6]; } seen[768];
		static int nseen;
		long r = FUNC_001478_REAL (ctx, index, flag);
		long idx = (long)(signed char)(index & 0xff);
		const unsigned char *vt = ctx ? *(const unsigned char *const *)((const char *)ctx + 0x150) : 0;
		const unsigned char *d = vt ? vt + (idx >> 1) * 6 : 0;
		int i, dup = 0;

		if (d && nseen < 768) {
			for (i = 0; i < nseen; i++)
				if (seen[i].idx == idx && memcmp (seen[i].d, d, 6) == 0) { dup = 1; break; }
			if (!dup) {
				const char *arena = *(const char *const *)DATA_009200;
				const long *o = (const long *)(arena + idx * 36 + 0x18);
				FILE *f = fopen ("pc_objpos.log", "a");

				seen[nseen].idx = idx; memcpy (seen[nseen].d, d, 6); nseen++;
				if (f) {
					fprintf (f, "PC1478D idx=%ld desc=%02x %02x %02x %02x %02x %02x flag=%ld out=(%ld,%ld,%ld) ctxpos=(%ld,%ld,%ld)\n",
					         idx, d[0], d[1], d[2], d[3], d[4], d[5], flag, o[0], o[1], o[2],
					         *(const long *)((const char *)ctx + 4), *(const long *)((const char *)ctx + 8),
					         *(const long *)((const char *)ctx + 12));
					fclose (f);
				}
			}
		}
		pcv8_rec (ctx, index, r);
		return r;
	}
}

/* THE MODEL INTERPRETER'S OPCODE STREAM.
 *
 * FUNC_001772's loop reads its opcode as a 16-bit WORD from the bytecode and
 * takes the low five bits as the handler index (ffe.asm:111196-111202):
 *
 *     movzx edx,word [eax] / push edx / add eax,2 / push eax / push ebx
 *     and edx,byte +0x1f / mov eax,[edx*4+DATA_007816] / call eax
 *
 * Measured 2026-08-05: the reference dispatches heavily to entries 5 (complex
 * polygon, FUNC_001721) and 10 (text string, FUNC_001747) and we dispatch to 6
 * (FUNC_001696) instead. The bytecode is reached through a RUN-TIME pointer, so
 * it carries no symbol and no storage correction applies -- and not one label in
 * the whole of ffebmp is typed be16 (3366 are `le', i.e. raw x86 bytes).
 *
 * HYPOTHESIS: every opcode word is byte-reversed on our side, so `and 0x1f'
 * takes the low bits of the wrong byte.
 * WHAT WOULD KILL IT: the first words of the bytecode being IDENTICAL on the two
 * machines. This prints them. Both sides read the pointer at ENTRY, so no
 * handler needs to run for the comparison to be possible.
 *
 * FUNC_001772 (object, bytecode) -- `mov eax,[ebp+0xc]' is the stream,
 * `mov ebx,[ebp+0x8]' the object (ffe.asm:111191-111192).
 */
extern void FUNC_001772_REAL (void *obj, void *code) __asm__("FUNC_001772_REAL");
void FUNC_001772 (void *obj, void *code) __asm__("FUNC_001772");

/* The model-index SEQUENCE, mirrored by MODSEQ on the Amiga side. The scan
 * below is the same expression on both machines -- the LARGEST DATA_004681
 * entry at or below `code' -- which is the whole point: two probes that compute
 * different quantities have produced three retracted findings on this project.
 * Capped so the cost stays bounded, and `total' printed so a short sequence
 * cannot be mistaken for a short run. */
#define PCMOD_MAX 400
long pcmod_seq[PCMOD_MAX], pcmod_n, pcmod_total;

/* FUNC_001344_StringExpandFFCode -- the mirror of the Amiga side's STR1344.
 *
 * Called from FUNC_001747 (ffe.asm:108671-108681) as (out, id, aux), where the
 * ID comes from `movsx eax,word [eax+0x6]' on the bytecode stream. The model
 * sequence measured the reference entering FIVE glyph models per text opcode
 * against our ONE, so the expanded string is the first thing to compare.
 *
 * Here the output can be read AFTER the call, which is the natural place; on
 * the Amiga side inject_count reaches only the entry, so it reads the previous
 * call's buffer at the next call. Different mechanics, SAME QUANTITY -- id and
 * the produced characters -- which is the only thing that makes the two lines
 * comparable at all. */
#define PCS44_MAX 12
long pcs44_id[PCS44_MAX], pcs44_len[PCS44_MAX], pcs44_n, pcs44_total;
unsigned char pcs44_txt[PCS44_MAX][12];

/* FUNC_001686 -- the branch that decides whether a glyph is INTERPRETED.
 *
 * The mirror of the Amiga side's SHIFT1686, and the expressions are written to
 * be identical: `[a1+0x148]' is the shift count, `[a2+0x1c]' the value, and
 * `out' is the value shifted by the count MASKED THE WAY x86 MASKS IT, which is
 * what the real instruction does here. The RETURN is recorded too, because on
 * this side it is free and it is the quantity FUNC_001747 actually branches on:
 * non-zero takes the cached path and does NOT recurse into FUNC_001772. */
#define PCS86_MAX 12
long pcs86_cl[PCS86_MAX], pcs86_in[PCS86_MAX], pcs86_out[PCS86_MAX];
long pcs86_ret[PCS86_MAX], pcs86_n, pcs86_total, pcs86_clmax, pcs86_nz;

/* FUNC_001687 -- where the cull is actually decided; the mirror of the Amiga
 * side's CULL1687. Same fields, same expressions, plus the real RETURN, which
 * is the quantity FUNC_001747 branches on: non-zero means the glyph is NOT
 * interpreted. `near' counts the first branch (z + size < 0x40) on its own,
 * because that and "|x| too big" are different faults. */
#define PCS87_MAX 12
long pcs87_sz[PCS87_MAX], pcs87_x[PCS87_MAX], pcs87_y[PCS87_MAX];
long pcs87_z[PCS87_MAX], pcs87_ret[PCS87_MAX];
long pcs87_n, pcs87_total, pcs87_near, pcs87_nz;

extern long FUNC_001687_REAL (long a1, long a2) __asm__("FUNC_001687_REAL");
long FUNC_001687 (long a1, long a2) __asm__("FUNC_001687");

long FUNC_001687 (long a1, long a2)
{
	long r = FUNC_001687_REAL (a1, a2);
	long x = *(const long *)(a1 + 0x0);
	long y = *(const long *)(a1 + 0x4);
	long z = *(const long *)(a1 + 0x8);

	pcs87_total++;
	if (r) pcs87_nz++;
	if (z + a2 < 0x40) pcs87_near++;
	if (pcs87_n < PCS87_MAX) {
		pcs87_sz[pcs87_n] = a2;
		pcs87_x[pcs87_n] = x;
		pcs87_y[pcs87_n] = y;
		pcs87_z[pcs87_n] = z;
		pcs87_ret[pcs87_n] = r;
		pcs87_n++;
	}
	return r;
}

extern long FUNC_001686_REAL (long a1, long a2) __asm__("FUNC_001686_REAL");
long FUNC_001686 (long a1, long a2) __asm__("FUNC_001686");

long FUNC_001686 (long a1, long a2)
{
	long r = FUNC_001686_REAL (a1, a2);
	long cl = *(const long *)(a1 + 0x148);
	long v = *(const long *)(a2 + 0x1c);

	pcs86_total++;
	if (r) pcs86_nz++;
	if (cl > pcs86_clmax) pcs86_clmax = cl;
	if (pcs86_n < PCS86_MAX) {
		pcs86_cl[pcs86_n] = cl;
		pcs86_in[pcs86_n] = v;
		pcs86_out[pcs86_n] = (long)((unsigned long)v << (cl & 31));
		pcs86_ret[pcs86_n] = r;
		pcs86_n++;
	}
	return r;
}

extern void FUNC_001344_REAL (void *out, long id, void *aux)
	__asm__("FUNC_001344_StringExpandFFCode_REAL");
void FUNC_001344 (void *out, long id, void *aux)
	__asm__("FUNC_001344_StringExpandFFCode");

void FUNC_001344 (void *out, long id, void *aux)
{
	FUNC_001344_REAL (out, id, aux);
	pcs44_total++;
	if (pcs44_n < PCS44_MAX) {
		const unsigned char *s = (const unsigned char *)out;
		int k = 0;

		while (k < 11 && s[k]) { pcs44_txt[pcs44_n][k] = s[k]; k++; }
		pcs44_id[pcs44_n] = id;
		pcs44_len[pcs44_n] = k;
		pcs44_n++;
	}
}

void FUNC_001772 (void *obj, void *code)
{
	static long n;

	if (pcmod_n < PCMOD_MAX) {
		const unsigned long *tab = (const unsigned long *)DATA_004681;
		unsigned long best = 0;
		long idx = -1, q;

		for (q = 0; q < 466; q++)
			if (tab[q] <= (unsigned long)code && tab[q] > best) {
				best = tab[q]; idx = q;
			}
		pcmod_seq[pcmod_n++] = idx;
	}
	pcmod_total++;

	if (++n <= 6 && code != NULL) {
		FILE *f = fopen ("pc_objpos.log", "a");

		if (f != NULL) {
			const unsigned short *w = (const unsigned short *)code;
			const unsigned char *b = (const unsigned char *)code;
			int i;

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
				const unsigned long *tab =
					(const unsigned long *)DATA_004681;
				long idx = -1, q;

				unsigned long best = 0;

				/* The LARGEST table entry at or below `code'. The header size
				 * is not assumed: an exact `code - 0x40' match came back -1 on
				 * both machines, which only proved the guess wrong. The delta
				 * printed alongside IS the header size, measured. */
				for (q = 0; q < 466; q++)
					if (tab[q] <= (unsigned long)code && tab[q] > best) {
						best = tab[q]; idx = q;
					}
				fprintf (f, "PC1772M n=%ld model=%ld delta=%ld code=%p\n",
				         n, idx, (long)((unsigned long)code - best), code);
				if (idx == -1 && tab[176]) {
					/* Sub-model descents keyed off model 176's pointer --
					 * machine-independent (both share ffebmp layout), so the
					 * distinct set compares directly to the Amiga MODREL. */
					static long rel[512]; static int nrel;
					long r = (long)((unsigned long)code - tab[176]);
					int seen = 0, z;
					for (z = 0; z < nrel; z++) if (rel[z] == r) { seen = 1; break; }
					if (!seen && nrel < 512) {
						rel[nrel++] = r;
						fprintf (f, "PC1772REL rel=%ld n=%d code=%p\n", r, nrel, code);
					}
				}
			}
			fprintf (f, "PC1772 n=%ld code=%p words=", n, code);
			for (i = 0; i < 8; i++) fprintf (f, "%04x,", w[i]);
			fprintf (f, "  op0=%d bytes=", (int)(w[0] & 0x1f));
			for (i = 0; i < 8; i++) fprintf (f, "%02x,", b[i]);
			fprintf (f, "\n");
			fclose (f);
		}
	}
	FUNC_001772_REAL (obj, code);
}

/* THE LAST FORK: does the reference enter through FUNC_000570 or FUNC_000574?
 *
 * Established so far, both sides measured: the reference reaches FUNC_000581
 * (and so the real planet) through FUNC_000582 <- FUNC_000579 <- FUNC_000576
 * <- FUNC_000575, and it does NOT call FUNC_000568. FUNC_000575 has exactly
 * three callers -- FUNC_000570 twice (ffe.asm:36020, 36138) and FUNC_000574
 * (36353) -- and FUNC_000570 is itself reachable only from FUNC_000568/569.
 * So the return address here separates "through 569" from "through 574", and
 * that is the branch that goes the other way on our side, where none of this
 * chain runs at all.
 *
 * Three cdecl dwords: the call site at 36353 ends `add esp,byte +0xc` and the
 * body reads [ebp+0x8], [ebp+0xc], [ebp+0x10].
 */
extern long FUNC_000575_REAL (long, long, long) __asm__("FUNC_000575_REAL");
long FUNC_000575 (long, long, long) __asm__("FUNC_000575");
extern void FUNC_000570 (void) __asm__("FUNC_000570");
extern void FUNC_000574 (void) __asm__("FUNC_000574");
extern void FUNC_000576 (void) __asm__("FUNC_000576");

long FUNC_000575 (long a1, long a2, long a3)
{
	static long n;

	if (++n <= 6) {
		FILE *f = fopen ("pc_objpos.log", "a");

		if (f != NULL) {
			fprintf (f, "PC575 n=%ld ret=%p  570=%p 574=%p 576=%p\n",
			         n, __builtin_return_address (0),
			         (void *)FUNC_000570, (void *)FUNC_000574,
			         (void *)FUNC_000576);
			fclose (f);
		}
	}
	return FUNC_000575_REAL (a1, a2, a3);
}

/* WHICH BRANCH REACHES THE BODY-SCRIPT INTERPRETER.
 *
 * The chain that ends at the real planet is
 *   243/246/1025 -> [DATA_006140] -> 568/569 -> 570 -> 575 -> 576 -> 579
 *                -> 582 -> 581 -> 1876 -> 1877
 * and on our side EVERY link is dead (counters, each with FUNC_001876 as a live
 * positive control). Naming which of the three entry points the reference uses
 * names the branch that goes the other way for us.
 *
 * Hooking FUNC_000568 -- their common target -- rather than all three avoids
 * having to get three separate arities right; its own return address plus the
 * three candidates' addresses identify the caller directly. Seven cdecl dwords:
 * both call sites end `add esp,byte +0x1c` (ffe.asm:19298, 75928) and the body
 * reads up to [ebp+0x20].
 */
extern long FUNC_000568_REAL (long,long,long,long,long,long,long)
	__asm__("FUNC_000568_REAL");
long FUNC_000568 (long,long,long,long,long,long,long) __asm__("FUNC_000568");
extern void FUNC_000246 (void) __asm__("FUNC_000246");
extern void FUNC_001025 (void) __asm__("FUNC_001025");
extern void FUNC_000243 (void) __asm__("FUNC_000243");

long FUNC_000568 (long a1, long a2, long a3, long a4, long a5, long a6, long a7)
{
	static long n;

	if (++n <= 10) {
		FILE *f = fopen ("pc_objpos.log", "a");

		if (f != NULL) {
			fprintf (f, "PC568 n=%ld ret=%p  243=%p 246=%p 1025=%p\n",
			         n, __builtin_return_address (0),
			         (void *)FUNC_000243, (void *)FUNC_000246,
			         (void *)FUNC_001025);
			fclose (f);
		}
	}
	return FUNC_000568_REAL (a1, a2, a3, a4, a5, a6, a7);
}

/* THE SEVEN OPCODE HANDLERS THAT NEVER EXECUTE ON OUR SIDE (2026-08-05).
 *
 * A complete 31-handler histogram on the Amiga build says these seven never run
 * at all, while 1766/1755/1754/1749/1752/1711 do -- the positive control in the
 * same run. "Seven handlers never run for us" is not evidence until it reads
 * "seven handlers the reference DOES run", so here are the same counters.
 *
 * FUNC_001697 is the one that matters most: it has NO direct call anywhere in
 * ffe.asm, its only route in is the handler table DATA_007816 (ffedat.asm:25550,
 * "Nested execution block start/end"), and the reference calls FUNC_001470 from
 * it while we never do.
 *
 * Arity is uniform: the dispatcher pushes three dwords and clears them with
 * `add esp,byte +0xc' (ffe.asm:111196-111203), so every handler is (long,long,
 * long). The counts are printed from the FUNC_001876 shim rather than from each
 * handler, because a handler that never runs prints nothing -- exactly the null
 * that has to be paired with something live to mean anything.
 */
long pc_h_n[7];			/* 1698, 1759, 1815, 1760, 1763, 1697, 1765 */

/* Forward: the opcode-sequence recorder is defined further down beside the
 * PC_OPCODE shims, but the handler and skipper macros below are EXPANDED above
 * it and every one of them has to feed the same sequence -- a handler missing
 * from it would leave a hole that reads exactly like an opcode that never ran,
 * which is the very thing being measured. */
static void pcop_add (int op, long a2);

#define PC_HANDLER(SLOT, OP, NUM)                                             \
	extern long FUNC_##NUM##_REAL (long, long, long)                          \
		__asm__("FUNC_" #NUM "_REAL");                                        \
	long FUNC_##NUM (long, long, long) __asm__("FUNC_" #NUM);                 \
	long FUNC_##NUM (long a1, long a2, long a3)                               \
	{                                                                         \
		pc_h_n[SLOT]++;                                                       \
		pcop_add (OP, a2);                                                    \
		return FUNC_##NUM##_REAL (a1, a2, a3);                                \
	}

/* THE FOUR CONDITIONAL-SKIP HANDLERS, and what they DECIDE (2026-08-05).
 *
 * The interpreter advances the stream by the handler's RETURN VALUE: the
 * dispatcher does `movzx edx,word [eax] / add eax,2 / call [edx*4+DATA_007816]'
 * and loops back to `movzx edx,word [eax]' with eax = the return
 * (ffe.asm:111195-111206). a2 is the stream pointer the handler was handed
 * (already +2), so `ret - a2' IS the decision: measured on the Amiga side, an
 * op-12 that does NOT skip advances the walk by 4 (delta 2 here) and one that
 * DOES skip advances it by 12 (delta 10).
 *
 * A histogram of those deltas per handler is directly comparable with the OPT
 * ring on our side, and it is what tests the hypothesis that a skip takes the
 * wrong branch and steps over the block holding opcode 21. Bounded slots,
 * with an overflow count printed, because a histogram that silently forgets
 * would read as "that delta never happened".
 */
#define PC_SKIP_SLOTS 8
struct pc_skip { long calls; long d[PC_SKIP_SLOTS]; long n[PC_SKIP_SLOTS];
                 long slots, ovf; };
struct pc_skip pc_skip[4];		/* 1752, 1755, 1756, 1757 */

static void pc_skip_add (int which, long delta)
{
	struct pc_skip *s = &pc_skip[which];
	int i;

	s->calls++;
	for (i = 0; i < s->slots; i++)
		if (s->d[i] == delta) { s->n[i]++; return; }
	if (s->slots >= PC_SKIP_SLOTS) { s->ovf++; return; }
	s->d[s->slots] = delta;
	s->n[s->slots] = 1;
	s->slots++;
}

#define PC_SKIPPER(SLOT, OP, NUM)                                             \
	extern long FUNC_##NUM##_REAL (long, long, long)                          \
		__asm__("FUNC_" #NUM "_REAL");                                        \
	long FUNC_##NUM (long, long, long) __asm__("FUNC_" #NUM);                 \
	long FUNC_##NUM (long a1, long a2, long a3)                               \
	{                                                                         \
		long r;                                                               \
		extern int pcsk_who;                                                  \
		int save = pcsk_who;                                                  \
		pcop_add (OP, a2);                                                    \
		pcsk_who = SLOT;                                                      \
		r = FUNC_##NUM##_REAL (a1, a2, a3);                                   \
		pcsk_who = save;                                                      \
		pc_skip_add (SLOT, r - a2);                                           \
		return r;                                                             \
	}

/* FUNC_001753 -- the skip arithmetic all four skip opcodes delegate to, and the
 * mirror of our SKIP1753. It computes ret = ptr + 2*((s16)word >> 5), except
 * when `word >> 5 == 0', where it instead clears DATA_007824 and thereby makes
 * the DISPATCHER LOOP EXIT (ffe.asm:109288-109310, 111205-111206). That third
 * path is counted separately on purpose: when the loop exits, the next entry in
 * the Amiga side's OPT ring is far away, and that LARGE DELTA IS NORMAL rather
 * than a wild pointer. Counting the exits is what lets the two be told apart. */
#define PCSK_SLOTS 12
long pcsk_calls, pcsk_ovf, pcsk_zeroflag, pcsk_exit;
long pcsk_w[PCSK_SLOTS], pcsk_n[PCSK_SLOTS], pcsk_slots;

/* FUNC_001473 -- the parameter decoder that produces the skip condition, and
 * the mirror of our COND1473. Split by the top two bits of arg2, because that
 * is what separates "the modes agree and the DATA differs" from "the modes
 * themselves differ, so the fault is upstream". Here the RETURN is the real
 * one; on the Amiga side inject_count can only reach a function's entry, so it
 * recomputes the same four cases with the same native reads. */
long pccond_mode[4], pccond_calls;
long pccond_arg[4], pccond_val[4], pccond_seen[4];

/* FUNC_001534 -- the mirror of our F1534. ffe.asm:91108-91158 is
 * `mov esi,[DATA_008804] / shr esi,0xa ... mov eax,esi / ret', so its return IS
 * the game clock >> 10, and FUNC_001536/1537 store its low word at record
 * offset +0x9c -- the field that reads 1 here and 10 on the Amiga side.
 * Recording the clock at entry beside the REAL return closes the chain by
 * measurement instead of by reading alone. */
long pcf34_calls, pcf34_clock[4], pcf34_ret[4], pcf34_n;

extern long FUNC_001534_REAL (long, long) __asm__("FUNC_001534_REAL");
long FUNC_001534 (long, long) __asm__("FUNC_001534");
long FUNC_001534 (long a1, long a2)
{
	extern char clock_arr2[] __asm__("DATA_008804");
	unsigned long c = *(const unsigned long *)clock_arr2;
	long r = FUNC_001534_REAL (a1, a2);

	pcf34_calls++;
	if (pcf34_n < 4) {
		pcf34_clock[pcf34_n] = (long)c;
		pcf34_ret[pcf34_n] = r;
		pcf34_n++;
	}
	return r;
}

extern long FUNC_001473_REAL (long, long) __asm__("FUNC_001473_REAL");
long FUNC_001473 (long, long) __asm__("FUNC_001473");
/* PER-INDEX too, because one "first call" sample per mode cannot separate
 * WRONG DATA from DIFFERENT STATE -- the two runs reach their first call at
 * different moments. First value seen per INDEX makes it field-by-field. */
long pccx_val[64], pccx_seen[64], pccx_n[64];	/* mode 0xc0 */
long pccw_val[64], pccw_seen[64], pccw_n[64];	/* mode 0x80 */

/* 2026-10-15: MV1473 -- every call in the FRAME1808 capture frame (twin of
 * MV1473 in amiga\probes\ffe_timers.c): code, result, object model +0x82. */
int pc1473_live;
long pc1473_n, pc1473_code[256], pc1473_r[256], pc1473_mdl[256];
long FUNC_001473 (long a1, long a2)
{
	long r = FUNC_001473_REAL (a1, a2);
	if (pc1473_live && pc1473_n < 256) {
		const unsigned char *obj = *(const unsigned char *const *)((const unsigned char *)a1 + 0x14c);
		pc1473_code[pc1473_n] = a2; pc1473_r[pc1473_n] = r;
		pc1473_mdl[pc1473_n] = obj ? *(const long *)(obj + 0x82) : -1;
		pc1473_n++;
	}
	int m = (int)((a2 >> 6) & 3);
	long idx = a2 & 0x3f;

	pccond_calls++;
	pccond_mode[m]++;
	if (m == 3) {
		pccx_n[idx]++;
		if (!pccx_seen[idx]) { pccx_val[idx] = r; pccx_seen[idx] = 1; }
	} else if (m == 2) {
		pccw_n[idx]++;
		if (!pccw_seen[idx]) { pccw_val[idx] = r; pccw_seen[idx] = 1; }
	}
	if (!pccond_seen[m]) {
		pccond_arg[m] = a2;
		pccond_val[m] = r;
		pccond_seen[m] = 1;
	}
	return r;
}

extern long FUNC_001753_REAL (long, long, long) __asm__("FUNC_001753_REAL");
long FUNC_001753 (long, long, long) __asm__("FUNC_001753");
/* Per CALLER, the mirror of the Amiga side's SKIPWHO. Which of the four skip
 * handlers is on the stack decides nothing here -- the reference is right by
 * definition -- but it is the baseline the Amiga split is compared against, and
 * without it "1755 gives us a false flag 80% of the time" has no reference
 * value to be 80% OF. The four PC_SKIPPER shims already count their own calls,
 * so the caller is known without a stack walk: each sets pcsk_who before
 * delegating and clears it after. */
long pcsk_by[5], pcsk_by_zero[5];
int pcsk_who = 4;

long FUNC_001753 (long flag, long ptr, long word)
{
	long i;

	pcsk_by[pcsk_who]++;
	if (flag == 0) pcsk_by_zero[pcsk_who]++;
	pcsk_calls++;
	if (flag == 0)
		pcsk_zeroflag++;
	else if (((unsigned long)word >> 5) == 0)
		pcsk_exit++;
	else {
		for (i = 0; i < pcsk_slots; i++)
			if (pcsk_w[i] == word) { pcsk_n[i]++; goto done; }
		if (pcsk_slots >= PCSK_SLOTS) pcsk_ovf++;
		else { pcsk_w[pcsk_slots] = word; pcsk_n[pcsk_slots] = 1;
		       pcsk_slots++; }
	}
done:
	return FUNC_001753_REAL (flag, ptr, word);
}

/* FUNC_001758 -- the SOLE writer of [a1 + idx*4 + 0x54], the fields that read
 * nine times too large on the Amiga side. First store per DESTINATION index,
 * with the operation selector and both operands, so the first index whose value
 * already differs names the operation and the selector says which of the
 * sixteen ran. FUNC_001473_REAL is called directly rather than FUNC_001473,
 * because the latter is hooked and would double-count its own histogram. */
long pcw58_sel[8], pcw58_o1[8], pcw58_o2[8], pcw58_b[8], pcw58_val[8];
long pcw58_seen[8], pcw58_n[8], pcw58_calls;
long pcw58_a1, pcw58_p14c, pcw58_w9c, pcw58_ptrseen, pcw58_bytes[12];

extern long FUNC_001758_REAL (long, long, long) __asm__("FUNC_001758_REAL");
long FUNC_001758 (long, long, long) __asm__("FUNC_001758");
long FUNC_001758 (long a1, long a2, long a3)
{
	long dest = (a3 >> 8) & 7;
	const unsigned char *s = (const unsigned char *)a2;
	long o1 = 0, o2 = 0, r;

	/* Table index 13. FUNC_001758 also sits at index 29 (ffedat.asm:25542 and
	 * 25558), and nothing inside the function can tell the two apart, so the
	 * sequence records 13 for both and the Amiga side -- which reads the actual
	 * opcode word at the loop head -- will show 29 where it really was 29. A
	 * 13-against-29 mismatch at one position is therefore EXPECTED and is not a
	 * divergence; anything else is. */
	pcop_add (13, a2);
	pcw58_calls++;
	pcw58_n[dest]++;
	if (!pcw58_seen[dest] && a2) {
		o1 = FUNC_001473_REAL (a1, s[0]);
		o2 = FUNC_001473_REAL (a1, s[1]);
	}
	r = FUNC_001758_REAL (a1, a2, a3);
	/* WHAT [a1+0x14c] POINTS AT and its +0x9c word -- the mirror of PTR14C.
	 * o1 for destination 6 IS this word (FUNC_001473 mode 0x80, index 0) and
	 * it reads 1 here against 10 on the Amiga side, stable across a 4x clock
	 * change, so these bytes ARE the x10. Measured rather than derived: the
	 * earlier guess that this is DATA_009129 was wrong -- FUNC_001683 stores a
	 * LOCAL frame buffer's address here (ffe.asm:102231), not the static view
	 * record. */
	if (!pcw58_ptrseen && a1) {
		unsigned long p = *(const unsigned long *)(a1 + 0x14c);
		int q;

		pcw58_a1 = a1;
		pcw58_p14c = (long)p;
		if (p) {
			pcw58_w9c = *(const unsigned short *)(p + 0x9c);
			for (q = 0; q < 12; q++)
				pcw58_bytes[q] = ((const unsigned char *)(p + 0x9c))[q];
			pcw58_ptrseen = 1;
		}
	}
	if (!pcw58_seen[dest] && a2) {
		pcw58_sel[dest] = (a3 >> 4) & 0xf;
		pcw58_b[dest] = (long)s[0] | ((long)s[1] << 8);
		pcw58_o1[dest] = o1;
		pcw58_o2[dest] = o2;
		pcw58_val[dest] = *(const long *)(a1 + dest * 4 + 0x54);
		pcw58_seen[dest] = 1;
	}
	return r;
}

PC_SKIPPER (0, 11, 001752)
PC_SKIPPER (1, 12, 001755)
PC_SKIPPER (2, 19, 001756)
PC_SKIPPER (3, 20, 001757)

PC_HANDLER (0,  1, 001698)
PC_HANDLER (1, 15, 001759)
PC_HANDLER (2, 16, 001815)
PC_HANDLER (3, 17, 001760)
PC_HANDLER (4, 18, 001763)
PC_HANDLER (5, 21, 001697)
PC_HANDLER (6, 23, 001765)

/* EVERY WRITER OF RECORD OFFSET +0x9c, counted on both machines (2026-08-06).
 *
 * WHY THIS EXISTS. The +0x9c word is the clock sample the skip conditions are
 * seeded from: 1 here, 10 on the Amiga side. The previous session traced it to
 * FUNC_001536 (ffe.asm:91273) and FUNC_001537 (91392) and stopped there. That
 * was INCOMPLETE -- grepping the whole disassembly for `+0x9c],' returns ELEVEN
 * write sites in nine functions, and forcing the value inside 1536/1537 on the
 * Amiga side (inject_count.py --w9cforce 1, verified present in both .o files
 * as `move.l #1,d0' immediately ahead of `move.l d0,(-4,a5)') left the field
 * reading 10. So either those two never run, or another writer wins.
 *
 * A counter per writer on BOTH machines separates the two, and any writer that
 * runs here and not there is itself the divergence -- upstream of every opcode
 * question. FUNC_000829 and FUNC_001741 are already hooked with their own shims
 * and are not repeated here.
 *
 * Arity: cdecl, caller-cleans, so a uniform three-dword shim is safe for a
 * callee of any arity -- the extra dwords are read (never written) above the
 * real arguments and the shim cleans exactly what it pushed. */
/* THE OPCODE SEQUENCE, the mirror of the Amiga side's OPSEQ (2026-08-06).
 *
 * The question is the earliest divergence in WHICH OPCODES RUN, and neither
 * call counts nor ratios can answer it -- our run ends on the renderer's TRAP 5
 * after a fraction of the reference's dispatches, so only the SEQUENCE is
 * comparable. On the Amiga side the recorder sits at the interpreter's loop
 * head, where inject_count can reach it. Here it cannot: the FUNC_001772 hook
 * wraps the whole call, not each iteration. So every handler records its own
 * arrival instead, which produces the identical sequence by a different route.
 *
 * DELTAS, not positions: the two machines load the bytecode at different
 * addresses. a2 is the stream pointer the dispatcher already advanced past the
 * opcode word (`add eax,2' before the call, ffe.asm:111198), so a2 - 2 is where
 * the word was read and consecutive differences are the walk itself. Clamped to
 * a short exactly as the Amiga side clamps, so a jump to another model reads as
 * one on both. */
#define PCOP_MAX 400
int pcop_op[PCOP_MAX];
unsigned pcop_w[PCOP_MAX];
long pcop_d[PCOP_MAX];
long pcop_n, pcop_total;
static unsigned long pcop_prev;

static void pcop_add (int op, long a2)
{
	unsigned long p = (unsigned long)a2 - 2;

	if (pcop_n < PCOP_MAX) {
		/* The WHOLE word and a FULL-WIDTH delta. Clamping the delta to a short
		 * hid the model-to-model jumps behind +-32767 at exactly the indices
		 * where the walk might already have drifted, and the low five bits
		 * alone throw away the half of the word the first divergence lives in:
		 * the skip distance is `(s16)word >> 5', entirely above the opcode
		 * mask. Mask when READING, not when recording. */
		pcop_op[pcop_n] = op;
		pcop_w[pcop_n] = *(const unsigned short *)p;
		pcop_d[pcop_n] = pcop_total ? (long)(p - pcop_prev) : 0;
		pcop_n++;
	}
	pcop_prev = p;
	pcop_total++;
}

/* One shim per DATA_007816 entry (ffedat.asm:25528-25560, 32 dwords). The
 * opcode number IS the table index, so it is written out here rather than
 * derived -- and FUNC_001758 appears at both 13 and 29, which is why the two
 * cannot be told apart from inside it and the table below is the authority. */
#define PC_OPCODE(OP, NUM)                                                    \
	extern long FUNC_##NUM##_REAL (long, long, long)                          \
		__asm__("FUNC_" #NUM "_REAL");                                        \
	long FUNC_##NUM (long, long, long) __asm__("FUNC_" #NUM);                 \
	long FUNC_##NUM (long a1, long a2, long a3)                               \
	{                                                                         \
		pcop_add (OP, a2);                                                    \
		return FUNC_##NUM##_REAL (a1, a2, a3);                                \
	}

PC_OPCODE ( 0, 001754)
PC_OPCODE ( 2, 001702)
PC_OPCODE ( 3, 001707)
PC_OPCODE ( 4, 001711)
PC_OPCODE ( 5, 001721)
PC_OPCODE ( 6, 001696)
PC_OPCODE ( 7, 001706)
PC_OPCODE ( 8, 001710)
PC_OPCODE ( 9, 001738)
/* 2026-10-15: TXT1747 -- the text opcode in the capture frame: stream bytes
 * +0..+7 and the 8 bytes of its anchor node DATA_009201[b@2]. Twin of
 * FfeTxt1747 (amiga\probes\ffe_game.c). */
extern long FUNC_001747_REAL (long, long, long) __asm__("FUNC_001747_REAL");
long FUNC_001747 (long, long, long) __asm__("FUNC_001747");
extern unsigned char *DATA_009201 __asm__("DATA_009201");
__attribute__((force_align_arg_pointer))
long FUNC_001747 (long a1, long a2, long a3)
{
	pcop_add (10, a2);
	if (pc1473_live) {
		FILE *f = fopen ("pc_objpos.log", "a");
		if (f) {
			const unsigned char *st = (const unsigned char *)a2, *nd = DATA_009201 + st[2] * 8;
			int i;
			fprintf (f, "TXT1747 st=");
			for (i = 0; i < 8; i++) fprintf (f, "%02x", st[i]);
			fprintf (f, " node=");
			for (i = 0; i < 8; i++) fprintf (f, "%02x", nd[i]);
			{ const unsigned char *cx = (const unsigned char *)a1, *nrm, *vtx; int q;
			  fprintf (f, " ctx=");
			  for (q = 0x38; q <= 0x50; q += 4) fprintf (f, "%lx,", (unsigned long)*(const long *)(cx + q));
			  fprintf (f, " s148=%lx", (unsigned long)*(const long *)(cx + 0x148)); fprintf (f, " mat="); for (q = 0x14; q <= 0x34; q += 4) fprintf (f, "%lx,", (unsigned long)*(const long *)(cx + q)); fprintf (f, " pos=%lx,%lx,%lx", (unsigned long)*(const long *)(cx + 4), (unsigned long)*(const long *)(cx + 8), (unsigned long)*(const long *)(cx + 12));
			  nrm = *(const unsigned char *const *)(cx + 0x154) + (st[2] >> 1) * 6 - 6;
			  vtx = *(const unsigned char *const *)(cx + 0x150) + (nrm[0] >> 1) * 6;
			  fprintf (f, " nrm=%02x%02x%02x%02x%02x vtx=%02x%02x%02x", nrm[0], nrm[1], nrm[2], nrm[3], nrm[4], vtx[2], vtx[3], vtx[4]); }
			fprintf (f, "\n");
			fclose (f);
		}
	}
	return FUNC_001747_REAL (a1, a2, a3);
}
PC_OPCODE (14, 001749)
PC_OPCODE (22, 001737)
PC_OPCODE (24, 001766)
PC_OPCODE (25, 001771)
PC_OPCODE (26, 001762)
PC_OPCODE (27, 001748)
PC_OPCODE (30, 001767)
PC_OPCODE (31, 001874)

long pc_w9cw_n[8];		/* 288, 562, 959, 1066, 1070, 1536, 1537, 1770 */

#define PC_W9CW(SLOT, NUM)                                                    \
	extern long FUNC_##NUM##_REAL (long, long, long)                          \
		__asm__("FUNC_" #NUM "_REAL");                                        \
	long FUNC_##NUM (long, long, long) __asm__("FUNC_" #NUM);                 \
	long FUNC_##NUM (long a1, long a2, long a3)                               \
	{                                                                         \
		pc_w9cw_n[SLOT]++;                                                    \
		return FUNC_##NUM##_REAL (a1, a2, a3);                                \
	}

PC_W9CW (0, 000288)
PC_W9CW (1, 000562)
PC_W9CW (2, 000959)
PC_W9CW (3, 001066)
PC_W9CW (4, 001070)
PC_W9CW (5, 001536)
PC_W9CW (6, 001537)
PC_W9CW (7, 001770)

/* WHICH PATH BRINGS THE REAL PLANET IN.
 *
 * FUNC_001876 has three callers -- FUNC_000581 (ffe.asm:38469), FUNC_000823
 * (55510) and FUNC_001874 (129353). Ours only ever reaches it through the
 * model interpreter with the fake near-camera object; the reference gets a real
 * planet centre, so it must use a different one. Rather than guess each
 * caller's arity to shim it, this shim prints its own RETURN ADDRESS next to
 * the three candidates' addresses and the host picks the match.
 *
 * Twelve cdecl dwords -- every call site ends `add esp,byte +0x30`.
 */
extern void FUNC_001876_REAL (long,long,long,long,long,long,
                              long,long,long,long,long,long)
	__asm__("FUNC_001876_REAL");
void FUNC_001876 (long,long,long,long,long,long,
                  long,long,long,long,long,long) __asm__("FUNC_001876");
extern void FUNC_000581 (void) __asm__("FUNC_000581");
extern void FUNC_000823 (void) __asm__("FUNC_000823");
/* _REAL, not the bare name: FUNC_001874 is hooked for the opcode sequence
 * (table index 31), so the bare symbol is the shim and the return addresses
 * this is compared against fall in the real body. Same trap as 1696/1697/
 * 1721/1747 above. */
extern void FUNC_001874s (void) __asm__("FUNC_001874_REAL");

void FUNC_001876 (long a1, long a2, long a3, long a4, long a5, long a6,
                  long a7, long a8, long a9, long a10, long a11, long a12)
{
	static long n;

	/* TRIGGER ON THE ANOMALY, not on the first N calls. Two of the three call
	 * sites (ffe.asm:38469, 55510) push literal zeros for a4/a9/a10, so a
	 * plain "first 16" window logged sixteen useless rows and nothing from
	 * the site that matters (129353). */
	if (a9 != 0 && ++n <= 8) {
		FILE *f = fopen ("pc_objpos.log", "a");
		const long *c = (const long *)a6;

		if (f != NULL) {
			fprintf (f, "PCMODE 008870=%02x\n", (unsigned int)DATA_008870);
			fprintf (f, "PC1876 n=%ld ret=%p  581=%p 823=%p 1874=%p"
			         "  centre=(%ld,%ld,%ld)\n",
			         n, __builtin_return_address (0),
			         (void *)FUNC_000581, (void *)FUNC_000823,
			         (void *)FUNC_001874s,
			         c ? c[0] : 0, c ? c[1] : 0, c ? c[2] : 0);
			/* THE BAND COUNT, BEFORE THE SHIFT (2026-08-05). FUNC_001877
			 * receives `bands' = [ebp+0x28] = a9, but FUNC_001876 rescales
			 * that slot in place first (ffe.asm:129433-129446):
			 *     eax = [a4 + 0x148] - 8
			 *     eax < 0 ? sar [ebp+0x28],-eax : shl [ebp+0x28],eax
			 * Measured, same scenario: object 2 reaches FUNC_001877 with
			 * bands=6242304 on the reference and 12484608 for us -- exactly
			 * 2x, i.e. one bit. Printing a9 RAW next to [a4+0x148] separates
			 * the two possibilities in one run: a differing shift field, or
			 * a differing word out of the model record. a10 is atmo, which
			 * already matches on both objects and is the control. */
			/* THE VIEW DESCRIPTOR (2026-08-05). FUNC_001877's `centre' is
			 * the object position in CAMERA space -- the vertex transform's
			 * own output is (0,0,0) on both machines, measured -- and ours
			 * is swung far off axis while z stays the right size, which is
			 * the shape of a rotation difference. DATA_009127 is the view
			 * descriptor FUNC_000959 builds (ffe.asm:71524): nine dwords of
			 * 3x3 at +0, then the 64-bit eye position whose low halves sit
			 * at +0x26 / +0x2e / +0x36. If the matrix agrees the fault is
			 * further down the vertex pipeline; if it does not, it is here. */
			{
				const long *m = (const long *)DATA_009127;
				int q;

				fprintf (f, "PCVIEW m=");
				for (q = 0; q < 9; q++) fprintf (f, "%ld,", m[q]);
				fprintf (f, "  eye=(%ld,%ld,%ld)\n",
				         *(const long *)(DATA_009127 + 0x26),
				         *(const long *)(DATA_009127 + 0x2e),
				         *(const long *)(DATA_009127 + 0x36));
			}
			/* WHICH NODE, AND WHICH INDEX. FUNC_001874 never computes the
			 * position: it takes node [ebx+0x2] out of the arena at
			 * [DATA_009200] with a 36-byte stride (ffe.asm:129206-129216) --
			 * or FUNC_001470's answer when the node is unresolved -- and
			 * hands esi+4 on as arg6, which becomes FUNC_001877's `centre'.
			 * a6 IS that node+4. Printing the derived index beside the node
			 * separates the two remaining possibilities in one run: the same
			 * index with different contents means the vertex TRANSFORM that
			 * filled the arena is wrong; a different index means the model
			 * bytecode byte that selects the node is. */
			{
				const long *nd = (const long *)a6;
				unsigned long arena = *(const unsigned long *)DATA_009200;
				long idx = (arena && (unsigned long)a6 > arena)
				         ? (long)(((unsigned long)a6 - 4 - arena) / 36) : -1;

				fprintf (f, "PCNODE n=%ld a6=%lx arena=%lx idx=%ld"
				            " node=(%ld,%ld,%ld)\n",
				         n, (unsigned long)a6, arena, idx,
				         nd ? nd[0] : 0, nd ? nd[1] : 0, nd ? nd[2] : 0);
			}
			/* The FUNC_001478 calls that wrote node 6 most recently --
			 * printed HERE, at the consumer, so the two machines' logs are
			 * pairable. Sampling them at the producer gave four rows from
			 * four different models and nothing comparable. */
			v1478_dump (f);
			v1470_dump (f);
			m1538_dump (f, n);
			fprintf (f, "PC1876B n=%ld a9raw=%ld a10=%ld shiftfld=%ld a4=%lx\n",
			         n, a9, a10,
			         a4 ? *(const long *)(a4 + 0x148) : -1,
			         (unsigned long)a4);
			/* THE RAW STREAM BYTES -- the mirror of our STREAM1876, added
			 * 2026-08-05 to tell an OFFSET error from a BYTE-ORDER one.
			 *
			 * atmo and bands are word reads of the model stream at [ebx+0x8]
			 * and [ebx+0xa] (ffe.asm:129335/129338), and a5 is `ebx' AFTER the
			 * `add ebx,byte +0xc' at 129346, so the base is a5 - 0xc. On x86 a
			 * word read IS the little-endian one, so `gw' here is a plain
			 * `s[0] | s[1]<<8' exactly as on the 68k side after its rol.w --
			 * the two logs then carry the same quantity and diff directly.
			 *
			 * Measured 2026-08-05 on our side: atmo word 0x051E where this
			 * prints 0x1999, and shiftfld 19 where this prints 18. If 0x1999
			 * turns up in these bytes at an offset other than 8, it is an
			 * offset error; if it is absent, the two machines are on different
			 * streams and the matching top-level model numbers (177, 361) are
			 * not the whole story. */
			/* The seven-handler histogram, printed HERE because a handler that
			 * never runs prints nothing on its own. Ours, same order:
			 * 1698, 1759, 1815, 1760, 1763, 1697, 1765 are ALL ZERO. */
			fprintf (f, "PCHANDLERS n=%ld 1698=%ld 1759=%ld 1815=%ld 1760=%ld "
			            "1763=%ld 1697=%ld 1765=%ld\n",
			         n, pc_h_n[0], pc_h_n[1], pc_h_n[2], pc_h_n[3],
			         pc_h_n[4], pc_h_n[5], pc_h_n[6]);
			{
				int q;

				{
					/* THE SEED OF THE x10 -- the mirror of our TIMESEED.
					 * [DATA_008804] -> DATA_009132 -> >>10 -> DATA_009129[0]
					 * -> FUNC_001473 mode 0x80 -> FUNC_001758's operand o1,
					 * measured 1 here and 10 on the Amiga side. */
					extern long DATA_009132 __asm__("DATA_009132");
					extern char clock_arr[] __asm__("DATA_008804");

					fprintf (f, "PCTIMESEED 009132=%ld (>>10 = %ld) "
					            "008804=%ld\n",
					         DATA_009132,
					         (long)((unsigned long)DATA_009132 >> 10),
					         *(const long *)clock_arr);
				}
				fprintf (f, "PCFUNC1534 calls=%ld", pcf34_calls);
				for (q = 0; q < pcf34_n; q++)
					fprintf (f, "  [clock=%ld ret=%ld]", pcf34_clock[q],
					         pcf34_ret[q]);
				fprintf (f, "\n");
				if (pcw58_ptrseen) {
					fprintf (f, "PCPTR14C a1=%08lx p=%08lx w9c=%ld bytes=",
					         (unsigned long)pcw58_a1,
					         (unsigned long)pcw58_p14c, pcw58_w9c);
					for (q = 0; q < 12; q++)
						fprintf (f, "%02lx,", pcw58_bytes[q]);
					fprintf (f, "\n");
				}
				{
					/* One line per WRITER of +0x9c, to be read against the
					 * Amiga side's CNT slots for the same numbers. A writer
					 * live here and zero there is the divergence itself. */
					static const int w9cw_num[8] =
						{288, 562, 959, 1066, 1070, 1536, 1537, 1770};

					fprintf (f, "PCW9CWRITERS");
					for (q = 0; q < 8; q++)
						fprintf (f, " %d=%ld", w9cw_num[q], pc_w9cw_n[q]);
					fprintf (f, "\n");
				}
				fprintf (f, "PCWRITE1758 calls=%ld", pcw58_calls);
				for (q = 0; q < 8; q++)
					if (pcw58_seen[q])
						fprintf (f, "  d%d[sel=%ld b=%04lx o1=%ld o2=%ld "
						            "val=%ld n=%ld]", q, pcw58_sel[q],
						         pcw58_b[q], pcw58_o1[q], pcw58_o2[q],
						         pcw58_val[q], pcw58_n[q]);
				fprintf (f, "\n");
				fprintf (f, "PCCOND1473 calls=%ld m00=%ld m40=%ld m80=%ld "
				            "mc0=%ld", pccond_calls, pccond_mode[0],
				         pccond_mode[1], pccond_mode[2], pccond_mode[3]);
				for (q = 0; q < 4; q++)
					if (pccond_seen[q])
						fprintf (f, "  [%d arg=%02lx val=%ld]", q * 0x40,
						         pccond_arg[q], pccond_val[q]);
				fprintf (f, "\n");
				fprintf (f, "PCCONDC0");
				for (q = 0; q < 64; q++)
					if (pccx_seen[q])
						fprintf (f, " i%d=%ld(n%ld)", q, pccx_val[q],
						         pccx_n[q]);
				fprintf (f, "\nPCCOND80");
				for (q = 0; q < 64; q++)
					if (pccw_seen[q])
						fprintf (f, " i%d=%ld(n%ld)", q, pccw_val[q],
						         pccw_n[q]);
				fprintf (f, "\n");
				{
					/* Mirror of the Amiga side's OPSEQ/OPSEQD, same shape and
					 * one line per array so a textual diff lines up. */
					fprintf (f, "PCOPSEQ n=%ld total=%ld op=", pcop_n,
					         pcop_total);
					for (q = 0; q < pcop_n; q++)
						fprintf (f, "%d,", pcop_op[q]);
					fprintf (f, "\nPCOPSEQD d=");
					for (q = 0; q < pcop_n; q++)
						fprintf (f, "%ld,", pcop_d[q]);
					fprintf (f, "\nPCOPSEQW w=");
					for (q = 0; q < pcop_n; q++)
						fprintf (f, "%04x,", pcop_w[q]);
					fprintf (f, "\nPCMODSEQ n=%ld total=%ld m=",
					         pcmod_n, pcmod_total);
					for (q = 0; q < pcmod_n; q++)
						fprintf (f, "%ld,", pcmod_seq[q]);
					fprintf (f, "\nPCNODE1470 n=%ld total=%ld", pcs70_n,
					         pcs70_total);
					for (q = 0; q < pcs70_n; q++)
						fprintf (f, "  [i=%ld ctx=(%ld,%ld,%ld) off=(%ld,%ld,%ld)]",
						         pcs70_idx[q], pcs70_cx[q], pcs70_cy[q], pcs70_cz[q],
						         pcs70_ox[q], pcs70_oy[q], pcs70_oz[q]);
					fprintf (f, "\nPCCULL1687 n=%ld total=%ld near=%ld nz=%ld",
					         pcs87_n, pcs87_total, pcs87_near, pcs87_nz);
					for (q = 0; q < pcs87_n; q++)
						fprintf (f, "  [sz=%ld pos=(%ld,%ld,%ld) z+sz=%ld "
						            "ret=%ld]", pcs87_sz[q], pcs87_x[q],
						         pcs87_y[q], pcs87_z[q],
						         pcs87_z[q] + pcs87_sz[q], pcs87_ret[q]);
					fprintf (f, "\nPCSHIFT1686 n=%ld total=%ld nz=%ld "
					            "clmax=%ld", pcs86_n, pcs86_total, pcs86_nz,
					         pcs86_clmax);
					for (q = 0; q < pcs86_n; q++)
						fprintf (f, "  [cl=%ld in=%lx out=%lx ret=%ld]",
						         pcs86_cl[q], (unsigned long)pcs86_in[q],
						         (unsigned long)pcs86_out[q], pcs86_ret[q]);
					fprintf (f, "\nPCSTR1344 n=%ld total=%ld",
					         pcs44_n, pcs44_total);
					for (q = 0; q < pcs44_n; q++) {
						int k;

						fprintf (f, "  [id=%ld len=%ld \"", pcs44_id[q],
						         pcs44_len[q]);
						for (k = 0; k < 11 && pcs44_txt[q][k]; k++)
							fprintf (f, "%c",
							         pcs44_txt[q][k] >= 32
							         && pcs44_txt[q][k] < 127
							         ? pcs44_txt[q][k] : '.');
						fprintf (f, "\"]");
					}
					fprintf (f, "\n");
				}
				fprintf (f, "PCSKIP1753 calls=%ld noflag=%ld exit=%ld ovf=%ld",
				         pcsk_calls, pcsk_zeroflag, pcsk_exit, pcsk_ovf);
				for (q = 0; q < pcsk_slots; q++)
					fprintf (f, " w%04lx=%ld(d%ld)", pcsk_w[q], pcsk_n[q],
					         2 * (((long)(short)pcsk_w[q]) >> 5));
				fprintf (f, "\n");
				{
					/* Mirror of the Amiga side's SKIPWHO: same four handlers,
					 * same order, same `zeroflag/total' per handler, so the two
					 * lines can be read one above the other. */
					static const int who4[4] = { 1752, 1755, 1756, 1757 };

					fprintf (f, "PCSKIPWHO");
					for (q = 0; q < 4; q++)
						fprintf (f, " %d=%ld/%ld", who4[q], pcsk_by_zero[q],
						         pcsk_by[q]);
					fprintf (f, " other=%ld/%ld\n", pcsk_by_zero[4],
					         pcsk_by[4]);
				}
			}
			{
				static const int sk_id[4] = { 1752, 1755, 1756, 1757 };
				int w, q;

				for (w = 0; w < 4; w++) {
					fprintf (f, "PCSKIP n=%ld %d calls=%ld ovf=%ld",
					         n, sk_id[w], pc_skip[w].calls, pc_skip[w].ovf);
					for (q = 0; q < pc_skip[w].slots; q++)
						fprintf (f, " d%ld=%ld",
						         pc_skip[w].d[q], pc_skip[w].n[q]);
					fprintf (f, "\n");
				}
			}
			{
				const unsigned char *s = (const unsigned char *)(a5 - 0xc);
				int q;

				fprintf (f, "PCSTREAM n=%ld base=%lx bytes=",
				         n, (unsigned long)s);
				for (q = 0; q < 16; q++)
					fprintf (f, "%02x,", s[q]);
				fprintf (f, "  gw@0=%04x gw@6=%04x gw@8=%04x gw@a=%04x\n",
				         (unsigned)(s[0] | (s[1] << 8)),
				         (unsigned)(s[6] | (s[7] << 8)),
				         (unsigned)(s[8] | (s[9] << 8)),
				         (unsigned)(s[10] | (s[11] << 8)));
				if (a4) {
					const unsigned char *p =
						(const unsigned char *)(a4 + 0x148);

					fprintf (f, "PCSHIFTFLD n=%ld bytes=%02x,%02x,%02x,%02x\n",
					         n, p[0], p[1], p[2], p[3]);
				}
			}
			fclose (f);
		}
	}
	FUNC_001876_REAL (a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12);
}

/* THE RENDER OBJECT'S POSITION at a nested transform, reference side.
 *
 * FUNC_001741 is the interpreter's 0x9c matrix-transform opcode; arg2 is the
 * render object (ffe.asm:107829) whose position triple it hands to FUNC_001739
 * at 108163-108177. FUNC_001683 builds that object at (0,0,84000) on both
 * machines, yet FUNC_001478 later sees planetary scale here and (0,0,-12) for
 * us, so the divergence happens on this path.
 */
extern void FUNC_001741_REAL (void *a, void *obj) __asm__("FUNC_001741_REAL");
void FUNC_001741 (void *a, void *obj) __asm__("FUNC_001741");

void FUNC_001741 (void *a, void *obj)
{
	static long n;

	/* Table index 28. `obj' is the dispatcher's SECOND argument, i.e. the
	 * stream pointer already advanced past the opcode word -- the same a2 every
	 * other handler here records. */
	pcop_add (28, (long)obj);
	if (++n <= 20 && obj != NULL) {
		FILE *f = fopen ("pc_objpos.log", "a");
		const long *p = (const long *)obj;

		if (f != NULL) {
			fprintf (f, "PC1741 n=%ld obj=%p pos=(%ld,%ld,%ld)\n",
			         n, obj, p[1], p[2], p[3]);
			fclose (f);
		}
	}
	FUNC_001741_REAL (a, obj);
}

/* THE PLANET CENTRE, on the reference side.
 *
 * Ours reaches FUNC_001877 with centre = (0, 0, -12) -- the planet sitting at
 * the camera -- so Msb1825(E) comes back <= 13 and EVERY call leaves through
 * the "too near" exit (measured: calls=1561, noatmo=223, near=1338, every
 * other exit 0). That is why DATA_008814 is never set to -1 and why the extra
 * near-camera object gets drawn. The reference must reach it with a real
 * planetary distance, since its DATA_008814 IS -1; this prints the number so
 * the two can be compared instead of assumed.
 *
 * Args are the six cdecl dwords FUNC_001876 pushes at ffe.asm:129462-129474:
 * (ctx, centre, corners, colour, atmo, bands).
 */
extern void FUNC_001877_REAL (void *ctx, long *centre, long *corners,
                              unsigned long colour, long atmo, long bands)
	__asm__("FUNC_001877_REAL");
void FUNC_001877 (void *ctx, long *centre, long *corners,
                  unsigned long colour, long atmo, long bands)
	__asm__("FUNC_001877");

void FUNC_001877 (void *ctx, long *centre, long *corners,
                  unsigned long colour, long atmo, long bands)
{
	static long n;

	/* HALO TIMELINE (2026-09-25) -- twin of the Amiga ffe_halo.log line. */
	if (((n + 1) & 15) == 1 && n < 16 * 400 && centre != NULL) {
		FILE *hf = fopen ("pc_halo.log", "a");
		if (hf != NULL) {
			fprintf (hf, "HALO clk=%ld c=(%ld,%ld,%ld) atmo=%ld bands=%ld\n",
			         *(const long *)DATA_008804, centre[0], centre[1], centre[2],
			         atmo, bands);
			fclose (hf);
		}
	}
	if (++n <= 12 && centre != NULL) {
		FILE *f = fopen ("pc_objpos.log", "a");
		if (f != NULL) {
			/* centre points at rec+4, so centre[-1] is rec+0 and
			 * centre[4..7] are rec+0x14 (the stamp) and rec+0x18..0x20
			 * (the transform's own output, before the object position
			 * was added). Printing both halves says whether ours zeroes
			 * in the transform or in the object placement. */
			fprintf (f, "PC1877 n=%ld centre=(%ld,%ld,%ld) atmo=%ld bands=%ld"
			            " col=%lx\n",
			         n, centre[0], centre[1], centre[2], atmo, bands,
			         (unsigned long)colour);
			fprintf (f, "PC1877R rec0=%ld stamp=%ld cam=(%ld,%ld,%ld)\n",
			         centre[-1], centre[4], centre[5], centre[6],
			         centre[7]);
			/* 2026-08-05: the four CORNER vectors are the matrix the
			 * hand-ported Gauss-Jordan solves for E, and E is what our
			 * side rejects as "too far" on half its calls. corners[0..2]
			 * is the first row and the three that follow are at +3, +6,
			 * +9 -- FUNC_001877 reads them as [ebp+0x10]+0..0x20
			 * (ffe.asm:131015-131100). If these agree across the two
			 * machines the fault is in the solve; if they do not it is
			 * upstream in FUNC_001876 and this function is innocent. */
			fprintf (f, "PC1877C c0=(%ld,%ld,%ld) c1=(%ld,%ld,%ld)"
			            " c2=(%ld,%ld,%ld)\n",
			         corners[0], corners[1], corners[2],
			         corners[3], corners[4], corners[5],
			         corners[6], corners[7], corners[8]);
			fclose (f);
		}
	}
	FUNC_001877_REAL (ctx, centre, corners, colour, atmo, bands);
}

/* WHICH BRANCH REACHES FUNC_000581, asked at its direct parent.
 *
 * The reference does NOT call FUNC_000568 at all (this shim is hooked and the
 * log carries every other marker, so the null is real), which kills the
 * 243/246/1025 -> 568 chain as the route. FUNC_000581's only caller is
 * FUNC_000582, whose own callers are FUNC_000579 (ffe.asm:37741) and
 * FUNC_000580 (37928, mutual recursion). Printing FUNC_000582's return address
 * against both names the entry without another guess.
 *
 * Five cdecl dwords: the call site at 37741 ends `add esp,byte +0x14`.
 */
extern long FUNC_000582_REAL (long,long,long,long,long)
	__asm__("FUNC_000582_REAL");
long FUNC_000582 (long,long,long,long,long) __asm__("FUNC_000582");
extern void FUNC_000579 (void) __asm__("FUNC_000579");
extern void FUNC_000580 (void) __asm__("FUNC_000580");

long FUNC_000582 (long a1, long a2, long a3, long a4, long a5)
{
	static long n;

	if (++n <= 6) {
		FILE *f = fopen ("pc_objpos.log", "a");

		if (f != NULL) {
			fprintf (f, "PC582 n=%ld ret=%p  579=%p 580=%p\n",
			         n, __builtin_return_address (0),
			         (void *)FUNC_000579, (void *)FUNC_000580);
			fclose (f);
		}
	}
	return FUNC_000582_REAL (a1, a2, a3, a4, a5);
}

/* PCPALMAP -- the mirror of the Amiga FfePalMap (2026-09-25): the 12-bit
 * colour code (low 16 bits of arg 1) handed to FUNC_001327_DPal16toPal, as
 * distinct (return address, code) pairs. The table is re-printed every 5000
 * calls to pc_objpos.log, so the LAST block is the whole run. Compare CODES:
 * the return addresses are PC addresses and only name the caller. */
extern long FUNC_001327_REAL (long) __asm__("FUNC_001327_DPal16toPal_REAL");
long FUNC_001327_shim (long) __asm__("FUNC_001327_DPal16toPal");
long FUNC_001327_shim (long v)
{
	static struct { unsigned long ret; long code, n; } t[48];
	static long calls, other;
	unsigned long ret = (unsigned long)__builtin_return_address (0);
	long code = v & 0xffff;
	int i;

	calls++;
	for (i = 0; i < 48; i++) {
		if (t[i].n == 0) { t[i].ret = ret; t[i].code = code; }
		if (t[i].ret == ret && t[i].code == code) { t[i].n++; break; }
	}
	if (i == 48) other++;
	if (calls % 5000 == 0) {
		FILE *f = fopen ("pc_objpos.log", "a");
		if (f) {
			fprintf (f, "PCPALMAP calls=%ld other=%ld\n", calls, other);
			for (i = 0; i < 48; i++)
				if (t[i].n)
					fprintf (f, "PCPALMAP1 ret=%08lx code=%03lx n=%ld\n",
					         t[i].ret, t[i].code, t[i].n);
			fclose (f);
		}
	}
	return FUNC_001327_REAL (v);
}

/* PCRGBMAP -- mirror of the Amiga FfeRgbMap (2026-09-25): low 24 bits of the
 * by-value RGB dword FUNC_001326_DPal32to16 receives, per return address, in
 * the same 512-slot hash. Printed every 20000 calls; the LAST block counts. */
extern long FUNC_001326_REAL (long) __asm__("FUNC_001326_DPal32to16_REAL");
long FUNC_001326_shim (long) __asm__("FUNC_001326_DPal32to16");
long FUNC_001326_shim (long v)
{
	static struct { unsigned long ret; long val, n; } t[512];
	static long calls, other;
	unsigned long ret = (unsigned long)__builtin_return_address (0);
	long val = v & 0xffffff;
	unsigned h = ((unsigned)ret * 7u + (unsigned)val * 131u) & 511;
	int k, i;

	calls++;
	for (k = 0; k < 16; k++, h = (h + 1) & 511) {
		if (t[h].n == 0) { t[h].ret = ret; t[h].val = val; }
		if (t[h].ret == ret && t[h].val == val) { t[h].n++; break; }
	}
	if (k == 16) other++;
	if (calls % 20000 == 0) {
		FILE *f = fopen ("pc_objpos.log", "a");
		if (f) {
			fprintf (f, "PCRGBMAP calls=%ld other=%ld\n", calls, other);
			for (i = 0; i < 512; i++)
				if (t[i].n)
					fprintf (f, "PCRGBMAP1 ret=%08lx val=%06lx n=%ld\n",
					         t[i].ret, t[i].val, t[i].n);
			fclose (f);
		}
	}
	return FUNC_001326_REAL (v);
}

/* PCBLITMAP -- mirror of the Amiga FfeBlitMap (2026-09-25): distinct
 * (index, x, y) at FUNC_001623_BlitIndex with the bitmap header and the first
 * six dwords of the buffer descriptor. */
extern long FUNC_001623_REAL (void *, long, long, long) __asm__("FUNC_001623_BlitIndex_REAL");
long FUNC_001623_shim (void *, long, long, long) __asm__("FUNC_001623_BlitIndex");
extern unsigned long DATA_008240[] __asm__("DATA_008240");
long FUNC_001623_shim (void *buf, long idx, long x, long y)
{
	static struct { long idx, x, y, n; unsigned long h0, h1, b[6]; } t[32];
	static long calls, other;
	int i, k;

	calls++;
	for (i = 0; i < 32; i++) {
		if (t[i].n == 0) {
			const unsigned char *h = (const unsigned char *)DATA_008240[idx];
			const unsigned long *bb = (const unsigned long *)buf;
			t[i].idx = idx; t[i].x = x; t[i].y = y;
			t[i].h0 = ((unsigned long)h[0] << 24) | ((unsigned long)h[1] << 16)
			        | (unsigned long)(h[2] | (h[3] << 8));
			t[i].h1 = ((unsigned long)(h[4] | (h[5] << 8)) << 16)
			        | (unsigned long)(h[6] | (h[7] << 8));
			for (k = 0; k < 6; k++) t[i].b[k] = bb ? bb[k] : 0;
		}
		if (t[i].idx == idx && t[i].x == x && t[i].y == y) { t[i].n++; break; }
	}
	if (i == 32) other++;
	if (calls % 500 == 0) {
		FILE *f = fopen ("pc_objpos.log", "a");
		if (f) {
			fprintf (f, "PCBLITMAP calls=%ld other=%ld\n", calls, other);
			for (i = 0; i < 32; i++)
				if (t[i].n)
					fprintf (f, "PCBLITMAP1 idx=%ld x=%ld y=%ld n=%ld hdr=%08lx %08lx buf=%08lx %08lx %08lx %08lx %08lx %08lx\n",
					         t[i].idx, t[i].x, t[i].y, t[i].n, t[i].h0, t[i].h1,
					         t[i].b[0], t[i].b[1], t[i].b[2], t[i].b[3], t[i].b[4], t[i].b[5]);
			fclose (f);
		}
	}
	return FUNC_001623_REAL (buf, idx, x, y);
}

/* PCTRI1808 -- twin of the Amiga FfeTri1808 (2026-09-25). */
extern long FUNC_001808_REAL (void *) __asm__("FUNC_001808_REAL");
long FUNC_001808_shim (void *) __asm__("FUNC_001808");
static int t8_bucket (long v, long lim)
{
	if (v < -1000) return 0;
	if (v < 0) return 1;
	if (v < lim) return 2;
	if (v < 1000) return 3;
	return 4;
}
/* the FRAME1808/FRAMECNT capture frame: first frame at clock >= F8_ARM.
 * 700000 = the asteroid close-up (2026-09-26); was 1800000 (Mars). Twin:
 * F8_ARM in amiga\probes\ffe_game.c. */
#define F8_ARM 700000
static short pcf8[400][9];
static long pcf8_n, pcf8_state;
/* OBJTAB (2026-10-09): the whole game-object table at the capture frame --
 * slot, flag byte, type [+0x82] and the six position dwords +0x26..+0x3a
 * (three 64-bit coords, low dword first). Twin of the PC side's objtab_dump.
 * Layout from FUNC_000927/928: flags at base+1..0x72, images at
 * base+0x74+idx*0x152. */
static void objtab_dump2 (const char *path, const char *mode, long clk)
{
	extern unsigned char *pc_objbase __asm__("DATA_009133");
	const unsigned char *b = pc_objbase;
	FILE *f;
	int i, k;

	if (b == 0) return;
	f = fopen (path, mode);
	if (f == 0) return;
	{
		/* which slot is the PLAYER (DATA_008861 -> object), and the raw pointer
		 * offset in case it is not a table slot at all */
		extern unsigned char *pc_plr __asm__("DATA_008861");
		long d = pc_plr ? (long)(pc_plr - b) : -1;
		fprintf (f, "T clk=%ld player_off=%ld slot=%ld%c", clk, d, d >= 0x74 ? (d - 0x74) / 0x152 : -1, 10);
		{
			extern unsigned long rng_a __asm__("DATA_008818"); extern unsigned long rng_b __asm__("DATA_008820");
			fprintf (f, "N rng=%08lx %08lx%c", rng_a, rng_b, 10);
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
	if (n == 0 || (n < 6 && (++cnt & 127) == 0) || (n >= 6 && (++cnt & 255) == 0)) { objtab_dump2 ("pc_objts.log", n ? "a" : "w", clk); n++; }
}
static void rngwin_tick (long clk)
{
	static int rd; static unsigned long lo, hi;
	extern unsigned long rw_a __asm__("DATA_008818"); extern unsigned long rw_b __asm__("DATA_008820");
	extern unsigned char *rw_base __asm__("DATA_009133");
	FILE *f; int i;
	if (!rd) { const char *w = getenv ("FFE_RNGWIN"); rd = 1; if (w) sscanf (w, "%lu %lu", &lo, &hi); }
	if (!hi || (unsigned long)clk < lo || (unsigned long)clk > hi || !rw_base) return;
	f = fopen ("pc_rngwin.log", "a"); if (!f) return;
	fprintf (f, "W %lu %08lx %08lx", (unsigned long)clk, rw_a, rw_b);
	for (i = 1; i <= 0x72; i++) if (rw_base[i]) fprintf (f, " %d:%ld", i, *(const long *)(rw_base + 0x74 + i * 0x152 + 0x82));
	fprintf (f, "%c", 10); fclose (f);
}
#define DOCKTS_PATH "pc_dockts.log"
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
	f = fopen (DOCKTS_PATH, n ? "a" : "w");
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
void pcf8_present (long clk)
{
	objts_tick (clk);
	dockts_tick (clk);
	rngwin_tick (clk);
	static unsigned long arm;	/* env FFE_F8ARM, unsigned (twin of PROGDIR:ffe_f8arm) */
	if (arm == 0) { const char *e = getenv ("FFE_F8ARM"); arm = e ? strtoul (e, 0, 10) : F8_ARM; }
	if (pcf8_state == 0 && (unsigned long)clk >= arm) pcf8_state = 1;
	else if (pcf8_state == 1) { pcf8_state = 2; pc1473_live = 1; }
	else if (pcf8_state == 2) {
		FILE *f = fopen ("pc_frame1808.log", "w");
		int q;
		pc1473_live = 0;
		{ FILE *g = fopen ("pc_objpos.log", "a"); if (g) { for (q = 0; q < pc1473_n; q++) fprintf (g, "MV1473 q=%d code=%02lx r=%lx mdl=%lx\n", q, pc1473_code[q] & 0xff, (unsigned long)pc1473_r[q], (unsigned long)pc1473_mdl[q]); fclose (g); } }

		pcf8_state = 3;
		objtab_dump ("pc_objtab.log");
		if (f) {
			fprintf (f, "FRAME1808 n=%ld clk=%ld\n", pcf8_n, clk);
			for (q = 0; q < pcf8_n; q++)
				fprintf (f, "T %d %d %d %d %d %d z %d %d %d\n",
				         pcf8[q][0], pcf8[q][1], pcf8[q][2], pcf8[q][3], pcf8[q][4],
				         pcf8[q][5], pcf8[q][6], pcf8[q][7], pcf8[q][8]);
			fclose (f);
		}
	}
}
long FUNC_001808_shim (void *itemv)
{
	if (pcf8_state == 2 && pcf8_n < 400) {
		const unsigned char *it = (const unsigned char *)itemv;
		int k;
		for (k = 0; k < 3; k++) {
			pcf8[pcf8_n][2 * k] = *(const short *)(it + 4 + 4 * k);
			pcf8[pcf8_n][2 * k + 1] = *(const short *)(it + 6 + 4 * k);
		}
		pcf8[pcf8_n][6] = *(const short *)(it + 0x12);
		pcf8[pcf8_n][7] = *(const short *)(it + 0x16);
		pcf8[pcf8_n][8] = *(const short *)(it + 0x1a);
		pcf8_n++;
	}
	static long n, bx[5], by[5], s[8][6], printed;
	const unsigned char *item = (const unsigned char *)itemv;
	long clk = *(const long *)DATA_008804;
	int k;

	if (clk >= 1700000 && clk <= 1950000) {
		for (k = 0; k < 3; k++) {
			long x = *(const short *)(item + 4 + 4 * k);
			long y = *(const short *)(item + 6 + 4 * k);
			bx[t8_bucket (x, 320)]++;
			by[t8_bucket (y, 158)]++;
			if (n < 8) { s[n][2 * k] = x; s[n][2 * k + 1] = y; }
		}
		n++;
	} else if (clk > 1950000 && n && !printed) {
		FILE *f = fopen ("pc_objpos.log", "a");
		printed = 1;
		if (f) {
			fprintf (f, "PCTRI1808 n=%ld x=%ld,%ld,%ld,%ld,%ld y=%ld,%ld,%ld,%ld,%ld\n", n,
			         bx[0], bx[1], bx[2], bx[3], bx[4], by[0], by[1], by[2], by[3], by[4]);
			for (k = 0; k < 8 && k < n; k++)
				fprintf (f, "PCTRI1808S (%ld,%ld) (%ld,%ld) (%ld,%ld)\n",
				         s[k][0], s[k][1], s[k][2], s[k][3], s[k][4], s[k][5]);
			fclose (f);
		}
	}
	return FUNC_001808_REAL (itemv);
}

/* PCWINCNT (2026-09-25): entry counts inside a clock window [WC_LO, WC_HI],
 * snapshotted at the window's edges by pcwc_tick (called per present).
 * Twin: WINCNT (Amiga, FfeWinCount) -- same window constants. */
#define WC_LO 600000
#define WC_HI 800000
extern long PCCNT_N __asm__("PCCNT_N");
extern long PCCNT_NUMS[] __asm__("PCCNT_NUMS");
extern long PCCNT_TAB[] __asm__("PCCNT_TAB");
#define WC_N ((int)PCCNT_N)
#define WC_MAX 128
/* ITEMLOG (2026-09-26): every draw item FUNC_001788 dispatches in the
 * capture frame, as its function number and the 8 shorts from item+4.
 * Called from the asm entry stub build-pc-probe.py writes. Twin: ITEMLOG in
 * amiga\probes\ffe_game.c. */
#define IT_MAX 2000
static short pcit[IT_MAX][11];   /* 10 shorts from item+4 (2026-10-16: +0x14 colour too) */
static long pcit_n;
void pc_item_log (long num, const short *item)
{
	int k;
	if (pcf8_state != 2 || pcit_n >= IT_MAX || !item) return;
	pcit[pcit_n][0] = (short)num;
	for (k = 0; k < 10; k++) pcit[pcit_n][1 + k] = item[2 + k];
	pcit_n++;
}
static void pcit_print (void)
{
	FILE *f = fopen ("pc_items.log", "w");
	long q;
	int k;
	if (!f) return;
	fprintf (f, "ITEMS n=%ld%c", pcit_n, 10);
	for (q = 0; q < pcit_n; q++) {
		fprintf (f, "I %d", pcit[q][0]);
		for (k = 1; k < 11; k++) fprintf (f, " %d", pcit[q][k]);
		fprintf (f, "%c", 10);
	}
	fclose (f);
}
void pcwc_tick (long clk)
{
	static long start[WC_MAX], fstart[WC_MAX];
	static int state, fprinted;
	int i;

	/* FRAMECNT (2026-09-26): the same counters over exactly the FRAME1808
	 * capture frame. pcwc_tick runs BEFORE pcf8_present in VideoBlit, so
	 * state 1 here is the present that opens the frame and state 2 the one
	 * that closes it. Twin: FRAMECNT (Amiga, FfeWinCount). */
	if (pcf8_state == 1) {
		for (i = 0; i < WC_N && i < WC_MAX; i++) fstart[i] = PCCNT_TAB[i];
	} else if (pcf8_state == 2 && !fprinted) {
		FILE *f = fopen ("pc_objpos.log", "a");
		fprinted = 1;
		pcit_print ();
		if (f) {
			fprintf (f, "PCFRAMECNT clk=%ld", clk);
			for (i = 0; i < WC_N && i < WC_MAX; i++)
				fprintf (f, " %ld=%ld", PCCNT_NUMS[i], PCCNT_TAB[i] - fstart[i]);
			fprintf (f, "%c", 10);
			fclose (f);
		}
	}

	if (state == 0 && clk >= WC_LO) {
		for (i = 0; i < WC_N && i < WC_MAX; i++) start[i] = PCCNT_TAB[i];
		state = 1;
	} else if (state == 1 && clk > WC_HI) {
		FILE *f = fopen ("pc_objpos.log", "a");
		state = 2;
		if (f) {
			extern long DATA_008812_v __asm__("DATA_008812");
			fprintf (f, "PCWINCNT d8812=%ld", DATA_008812_v);
			for (i = 0; i < WC_N && i < WC_MAX; i++)
				fprintf (f, " %ld=%ld", PCCNT_NUMS[i], PCCNT_TAB[i] - start[i]);
			fprintf (f, "%c", 10);
			fclose (f);
		}
	}
}


/* PCCLIP1837 -- twin of the Amiga FfeClip1837 (2026-09-25). */
extern void FUNC_001837_REAL (void *, long *, long *) __asm__("FUNC_001837_REAL");
void FUNC_001837_shim (void *, long *, long *) __asm__("FUNC_001837");
void FUNC_001837_shim (void *dest, long *a, long *b)
{
	static int n;
	FUNC_001837_REAL (dest, a, b);
	if (pcf8_state == 2 && n < 64) {
		FILE *f = fopen ("pc_clip1837.log", n ? "a" : "w");
		const short *d = (const short *)dest;
		n++;
		if (f) {
			fprintf (f, "C A=%ld,%ld,%ld z=%ld B=%ld,%ld,%ld d=%ld,%ld,%ld z=%ld out=%d,%d\n",
			         a[1], a[2], a[3], a[15], b[1], b[2], b[3], b[10], b[11], b[12],
			         b[15], d[0], d[1]);
			fclose (f);
		}
	}
}

/* PCFRAMEHALO -- twin of FfeFrameHalo (2026-09-25). */
static void pcfh_rec (int num, const short *it)
{
	static int n;
	if (pcf8_state != 2 || n >= 200) return;
	{
		FILE *f = fopen ("pc_framehalo.log", n ? "a" : "w");
		int k;
		n++;
		if (f) {
			fprintf (f, "H %d", num);
			for (k = 0; k < 14; k++) fprintf (f, " %d", it[k]);
			fprintf (f, "\n");
			fclose (f);
		}
	}
}
extern long FUNC_001805_REAL (void *) __asm__("FUNC_001805_REAL");
long FUNC_001805_shim (void *) __asm__("FUNC_001805");
long FUNC_001805_shim (void *it) { pcfh_rec (1805, (const short *)it); return FUNC_001805_REAL (it); }
extern long FUNC_001810_REAL (void *) __asm__("FUNC_001810_REAL");
long FUNC_001810_shim (void *) __asm__("FUNC_001810");
long FUNC_001810_shim (void *it) { pcfh_rec (1810, (const short *)it); return FUNC_001810_REAL (it); }

long pcf8_state_pub (void) { return pcf8_state; }

/* PC1709 (2026-09-26): the flat-colour triangle path FUNC_001708 falls into
 * (DATA_008812<0, a vertex outside [0xf830..0x910]x[0xf830..0x86e], or shade
 * index 0). The reference takes it ~20x more often than we do in the
 * asteroid scene; log its three screen points in the clock window. */
extern long FUNC_001709_REAL (void *, short *, short *, short *, long) __asm__("FUNC_001709_REAL");
long FUNC_001709_shim (void *, short *, short *, short *, long) __asm__("FUNC_001709");
long FUNC_001709_shim (void *ctx, short *a, short *b, short *c, long col)
{
	static int n;
	long clk = *(const long *)DATA_008804;
	if (clk >= 600000 && clk <= 800000 && n < 40) {
		FILE *f = fopen ("pc_objpos.log", "a");
		n++;
		if (f) {
			fprintf (f, "PC1709 (%d,%d) (%d,%d) (%d,%d) col=%lx\n",
			         a[0], a[1], b[0], b[1], c[0], c[1], (unsigned long)col);
			fclose (f);
		}
	}
	return FUNC_001709_REAL (ctx, a, b, c, col);
}


/* P879 (2026-10-09): inputs of the starport placement. FUNC_000879(obj, a, b,
 * time) builds the starport's parent-relative offset obj+0x3e; FUNC_000574(out,
 * vec64[3], obj, parent) is called from it with matrix-row x radius. Logged:
 * slot [obj+0x86] and the arguments. Twin: FfeP879 (amiga\probesfe_game.c). */
static void p879_log (const char *line)
{
	static int n;
	FILE *f;
	if (n >= 64) return;
	f = fopen ("pc_p879.log", n ? "a" : "w");
	n++;
	if (f) { fputs (line, f); fclose (f); }
}
extern long FUNC_000879_REAL (unsigned char *, long, long, long) __asm__("FUNC_000879_REAL");
long FUNC_000879_shim (unsigned char *, long, long, long) __asm__("FUNC_000879");
long FUNC_000879_shim (unsigned char *obj, long a, long b, long t)
{
	char l[200];
	long r;
	snprintf (l, sizeof l, "P879 slot=%d a=%ld b=%ld\n", obj[0x86], a, b);
	p879_log (l);
	r = FUNC_000879_REAL (obj, a, b, t);
	snprintf (l, sizeof l, "P879OUT slot=%d R %ld %ld %ld %ld %ld %ld row %ld %ld %ld\n", obj[0x86],
	          *(long *)(obj + 0x3e), *(long *)(obj + 0x42), *(long *)(obj + 0x46),
	          *(long *)(obj + 0x4a), *(long *)(obj + 0x4e), *(long *)(obj + 0x52),
	          *(long *)(obj + 0xc), *(long *)(obj + 0x10), *(long *)(obj + 0x14));
	p879_log (l);
	{
		extern long pc_h8703 __asm__("DATA_008703");
		extern long pc_h8704 __asm__("DATA_008704");
		snprintf (l, sizeof l, "P879H slot=%d 8703=%ld 8704=%ld%c", obj[0x86], pc_h8703, pc_h8704, 10);
		p879_log (l);
	}
	return r;
}
#if 0	/* hooking 574 left the reference on the splash (2026-10-09): it has other callers */
extern long FUNC_000574_REAL (long *, long *, unsigned char *, unsigned char *) __asm__("FUNC_000574_REAL");
long FUNC_000574_shim (long *, long *, unsigned char *, unsigned char *) __asm__("FUNC_000574");
long FUNC_000574_shim (long *out, long *v, unsigned char *obj, unsigned char *par)
{
	char l[200];
	long r = FUNC_000574_REAL (out, v, obj, par);
	snprintf (l, sizeof l, "P574 slot=%d v %ld %ld %ld %ld %ld %ld out=%ld\n", obj[0x86],
	          v[0], v[1], v[2], v[3], v[4], v[5], out[0]);
	p879_log (l);
	return r;
}
#endif

/* P581 -- twin of FfeP581 (amiga/probes/ffe_game.c): the first 64 FUNC_000581
 * calls with the game clock running, inputs + *out after the call. */
extern long FUNC_000581_REAL (long *, unsigned char *, unsigned char *, long, long *) __asm__("FUNC_000581_REAL");
long FUNC_000581_shim (long *, unsigned char *, unsigned char *, long, long *) __asm__("FUNC_000581");
long FUNC_000581_shim (long *out, unsigned char *rec, unsigned char *obj, long thr, long *ctx)
{
	static int n;
	extern unsigned long pc_clk_581 __asm__("DATA_008804");
	long in[18], r;
	int k, take = (pc_clk_581 & 0x80000000UL) != 0 && n < 64;

	if (take) {
		for (k = 0; k < 6; k++) in[k] = rec[k];
		for (k = 0; k < 4; k++) in[6 + k] = *(long *)(obj + 0x138 + 4 * k);
		in[10] = thr;
		for (k = 0; k < 7; k++) in[11 + k] = ctx[k];
	}
	r = FUNC_000581_REAL (out, rec, obj, thr, ctx);
	if (take) {
		FILE *f = fopen ("pc_p581.log", n ? "a" : "w");
		n++;
		if (f) {
			fprintf (f, "P581 slot=%d rec %02lx%02lx%02lx%02lx%02lx%02lx R", obj[0x86], in[0], in[1], in[2], in[3], in[4], in[5]);
			for (k = 0; k < 4; k++) fprintf (f, " %ld", in[6 + k]);
			fprintf (f, " thr=%ld ctx", in[10]);
			for (k = 0; k < 7; k++) fprintf (f, " %ld", in[11 + k]);
			fprintf (f, " out=%ld%c", out[0], 10);
			fclose (f);
		}
	}
	return r;
}

/* 2026-10-15: SYSTEM ORBIT MAP (F6). FUNC_000982 builds the view matrix
 * DATA_008968 (9 dwords, D8804+0x2a4) with FUNC_001676_MatBuildYZ from the
 * word pair DATA_008980/8981 (+0x3f6), scales it by DATA_008984/8985
 * (+0x3fc) into DATA_008973..8978 (+0x2e2), then calls FUNC_000981 to draw
 * the orbits. Twin of FfeSysMap in amiga\probes\ffe_game.c. */
extern long FUNC_000981_REAL (long) __asm__("FUNC_000981_REAL");
long FUNC_000981 (long) __asm__("FUNC_000981");
__attribute__((force_align_arg_pointer))
long FUNC_000981 (long a1)
{
	static long n;
	if (++n <= 3) {
		FILE *f = fopen ("pc_objpos.log", "a");
		if (f != NULL) {
			const unsigned char *b = DATA_008804;
			int i;
			fprintf (f, "SYSMAP n=%ld m=", n);
			for (i = 0; i < 9; i++)
				fprintf (f, "%lx,", (unsigned long)*(const long *)(b + 0x2a4 + 4 * i));
			fprintf (f, " s=");
			for (i = 0; i < 6; i++)
				fprintf (f, "%lx,", (unsigned long)*(const long *)(b + 0x2e2 + 4 * i));
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
	}
	return FUNC_000981_REAL (a1);
}
/* 2026-10-15: ORB986 -- the per-body fields FUNC_000986 reads to draw one
 * orbit (ffe.asm:73266..). Twin of FfeOrb986 in amiga\probes\ffe_game.c. */
extern long FUNC_000986_REAL (long, long) __asm__("FUNC_000986_REAL");
long FUNC_000986 (long, long) __asm__("FUNC_000986");
__attribute__((force_align_arg_pointer))
long FUNC_000986 (long a1, long a2)
{
	static long n;
	if (++n <= 80 && a1 != 0) {
		FILE *f = fopen ("pc_objpos.log", "a");
		if (f != NULL) {
			const unsigned char *o = (const unsigned char *)a1;
			fprintf (f, "ORB n=%ld t82=%lx b87=%02x b56=%02x b14c=%02x w9e=%04x wc6=%04x dbe=%lx db0=%lx p3e=%lx,%lx,%lx,%lx,%lx,%lx a2=%lx\n",
			         n, (unsigned long)*(const long *)(o + 0x82), o[0x87], o[0x56], o[0x14c],
			         *(const unsigned short *)(o + 0x9e), *(const unsigned short *)(o + 0xc6),
			         (unsigned long)*(const long *)(o + 0xbe), (unsigned long)*(const long *)(o + 0xb0),
			         (unsigned long)*(const long *)(o + 0x3e), (unsigned long)*(const long *)(o + 0x42),
			         (unsigned long)*(const long *)(o + 0x46), (unsigned long)*(const long *)(o + 0x4a),
			         (unsigned long)*(const long *)(o + 0x4e), (unsigned long)*(const long *)(o + 0x52),
			         (unsigned long)a2);
			fclose (f);
		}
	}
	return FUNC_000986_REAL (a1, a2);
}
/* 2026-10-15: OBJ1680 -- FUNC_001680's object in the FRAME1808 capture frame
 * (model +0x82, scale +0x7e, 64-bit position +0x3e.., flags +0x56/+0x57/+0x86,
 * the parent arg's +0x56/+0x57). Twin of FfeObj1680 (amiga\probes\ffe_game.c). */
extern int pc1473_live;
extern long FUNC_001680_REAL (long, long) __asm__("FUNC_001680_REAL");
long FUNC_001680 (long, long) __asm__("FUNC_001680");
__attribute__((force_align_arg_pointer))
long FUNC_001680 (long a1, long a2)
{
	static long n;
	if (pc1473_live && n < 120) {
		FILE *f = fopen ("pc_objpos.log", "a");
		n++;
		if (f != NULL) {
			const unsigned char *o = (const unsigned char *)a1, *p = (const unsigned char *)a2;
			fprintf (f, "OBJ1680 n=%ld mdl=%lx s7e=%lx pos=%lx,%lx,%lx,%lx,%lx,%lx f56=%02x f57=%02x f86=%02x p56=%02x p57=%02x\n",
			         n, (unsigned long)*(const long *)(o + 0x82), (unsigned long)*(const long *)(o + 0x7e),
			         (unsigned long)*(const long *)(o + 0x3e), (unsigned long)*(const long *)(o + 0x42),
			         (unsigned long)*(const long *)(o + 0x46), (unsigned long)*(const long *)(o + 0x4a),
			         (unsigned long)*(const long *)(o + 0x4e), (unsigned long)*(const long *)(o + 0x52),
			         o[0x56], o[0x57], o[0x86], p ? p[0x56] : 0, p ? p[0x57] : 0);
			fclose (f);
		}
	}
	return FUNC_001680_REAL (a1, a2);
}
/* 2026-10-15: MSB1657 -- the 64-bit vector Vec64FindMSB gets in the capture
 * frame (FUNC_001680's object-to-view vector), with the result. Twin of
 * FfeMsb1657 (amiga\probes\ffe_game.c). */
extern long FUNC_001657_Vec64FindMSB_REAL (long) __asm__("FUNC_001657_Vec64FindMSB_REAL");
long FUNC_001657_Vec64FindMSB (long) __asm__("FUNC_001657_Vec64FindMSB");
__attribute__((force_align_arg_pointer))
long FUNC_001657_Vec64FindMSB (long a1)
{
	static long n;
	long r = FUNC_001657_Vec64FindMSB_REAL (a1);
	if (pc1473_live && n < 120) {
		FILE *f = fopen ("pc_objpos.log", "a");
		const long *v = (const long *)a1;
		n++;
		if (f != NULL) {
			fprintf (f, "MSB1657 n=%ld v=%lx,%lx,%lx,%lx,%lx,%lx\n", n,
			         (unsigned long)v[0], (unsigned long)v[1], (unsigned long)v[2],
			         (unsigned long)v[3], (unsigned long)v[4], (unsigned long)v[5]);
			fclose (f);
		}
	}
	return r;
}
/* 2026-10-16: LIN1703 -- model opcode 2 (line) in the capture frame: its
 * stream, the default colour DATA_007826/7827 as raw bytes, the context tint
 * bytes +0xf4..+0xf6 and +0x84..+0x86. Twin of FfeLin1703. */
extern long FUNC_001703_REAL (long, long, long, long) __asm__("FUNC_001703_REAL");
long FUNC_001703 (long, long, long, long) __asm__("FUNC_001703");
extern unsigned char DATA_007826[] __asm__("DATA_007826");
extern unsigned char DATA_007827[] __asm__("DATA_007827");
__attribute__((force_align_arg_pointer))
long FUNC_001703 (long a1, long a2, long a3, long a4)
{
	if (pc1473_live) {
		FILE *f = fopen ("pc_objpos.log", "a");
		if (f) {
			const unsigned char *cx = (const unsigned char *)a1;
			fprintf (f, "LIN1703 a2=%lx a3=%lx a4=%lx def=%02x%02x%02x f4=%02x%02x%02x 84=%02x%02x%02x\n",
			         (unsigned long)a2, (unsigned long)a3, (unsigned long)a4,
			         DATA_007826[0], DATA_007826[1], DATA_007827[0],
			         cx[0xf4], cx[0xf5], cx[0xf6], cx[0x84], cx[0x85], cx[0x86]);
			fclose (f);
		}
	}
	return FUNC_001703_REAL (a1, a2, a3, a4);
}
/* 2026-10-16: SH1691 -- FUNC_001691's by-value RGB bytes and shade index in
 * the capture frame. Twin of FfeSh1691. */
extern long FUNC_001691_REAL (long, long, long) __asm__("FUNC_001691_REAL");
long FUNC_001691 (long, long, long) __asm__("FUNC_001691");
__attribute__((force_align_arg_pointer))
long FUNC_001691 (long a1, long a2, long a3)
{
	static int n;
	if (pc1473_live && n < 40) {
		FILE *f = fopen ("pc_objpos.log", "a");
		const unsigned char *b = (const unsigned char *)&a2;
		n++;
		if (f) { fprintf (f, "SH1691 rgb=%02x%02x%02x%02x idx=%ld\n", b[0], b[1], b[2], b[3], a3); fclose (f); }
	}
	return FUNC_001691_REAL (a1, a2, a3);
}
/* 2026-10-16: SRF1840 -- the three planet-surface vertices FUNC_001840 gets,
 * their +0x3c (height used for the colour band) and +0x4..+0xc, in the capture
 * frame. Twin of FfeSrf1840. */
extern long FUNC_001840_REAL (long, long, long, long, long) __asm__("FUNC_001840_REAL");
long FUNC_001840 (long, long, long, long, long) __asm__("FUNC_001840");
__attribute__((force_align_arg_pointer))
long FUNC_001840 (long a1, long a2, long a3, long a4, long a5)
{
	static int n;
	if (pc1473_live && n < 60) {
		FILE *f = fopen ("pc_objpos.log", "a");
		n++;
		if (f) {
			const long *v[3] = { (const long *)a1, (const long *)a2, (const long *)a3 };
			int k;
			fprintf (f, "SRF1840");
			for (k = 0; k < 3; k++)
				fprintf (f, " [%lx %lx %lx h=%lx]", (unsigned long)v[k][1], (unsigned long)v[k][2],
				         (unsigned long)v[k][3], (unsigned long)v[k][15]);
			{ extern long DATA_009262 __asm__("DATA_009262"), DATA_009263 __asm__("DATA_009263"), DATA_009267 __asm__("DATA_009267");
			  fprintf (f, " g62=%lx g63=%lx g67=%lx", (unsigned long)DATA_009262, (unsigned long)DATA_009263, (unsigned long)DATA_009267); }
			fprintf (f, " a5=%lx\n", (unsigned long)a5);
			fclose (f);
		}
	}
	return FUNC_001840_REAL (a1, a2, a3, a4, a5);
}
/* 2026-10-16: CR1838 -- FUNC_001838's arguments in the capture frame: shade,
 * light (numeric), table offset from DATA_007849 and its first entry. Twin of
 * FfeCr1838. */
extern long FUNC_001838_REAL (long, long, long, long) __asm__("FUNC_001838_REAL");
long FUNC_001838 (long, long, long, long) __asm__("FUNC_001838");
extern unsigned char DATA_007849[] __asm__("DATA_007849");
__attribute__((force_align_arg_pointer))
long FUNC_001838 (long a1, long a2, long a3, long a4)
{
	static int n;
	if (pc1473_live && n < 80) {
		FILE *f = fopen ("pc_objpos.log", "a");
		n++;
		if (f) { fprintf (f, "CR1838 s=%ld light=%lx off=%ld e0=%lx\n", a2, (unsigned long)a3,
		                  (long)((const unsigned char *)a4 - DATA_007849), (unsigned long)*(const long *)a4); fclose (f); }
	}
	return FUNC_001838_REAL (a1, a2, a3, a4);
}
