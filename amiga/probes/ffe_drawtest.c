/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 angree. See LICENSE at the repository root.
 *
 * The game's own span renderer, on a real Amiga screen.
 *
 * Every pixel below is drawn by FUNC_001601_DrawHLine -- the routine FFE fills
 * every polygon scanline with, translated from x86 by tools\translator and
 * running as 68020 code. There is NO C drawing anywhere in this file: no
 * memset, no loop over pixels, not even to clear the buffer. The background,
 * the horizon, the disc and the test swatches are all spans, and if the
 * translation were wrong the picture would say so before any number did.
 *
 * The wiring is the structural win CLAUDE.md describes: DATA_007773's pixel
 * pointer is aimed straight at amigagfx_chunky(), so the game writes into the
 * buffer the c2p already reads and a frame costs no 64 KB copy at all. Present
 * is therefore amigagfx_blit() rather than VideoBlit() -- VideoBlit would
 * memcpy the buffer onto itself, which is both pointless and undefined.
 *
 * WHAT A CORRECT RUN LOOKS LIKE:
 *   - a sky gradient from black-blue at the top to pale at the horizon, one
 *     320-pixel span per row, so a stride bug shows up as a slanted or
 *     repeating background rather than as a number in a log;
 *   - a white two-row horizon line and a brown ground ramp below it;
 *   - a filled disc, span by span, shaded top to bottom -- this is the shape a
 *     polygon filler actually produces, and it is round only if the x320 row
 *     stride is exactly right;
 *   - four columns of eighteen short runs near the bottom. The columns start at
 *     x congruent to 0, 1, 2 and 3 modulo 4 and the runs are 0 to 17 pixels
 *     long, so between them they cover every combination of "leading bytes to
 *     reach alignment", "whole longwords" and "trailing bytes" the routine can
 *     take, including the lengths where a phase is skipped entirely. A length
 *     of 0 draws nothing and leaves a gap, which is part of the test.
 *
 * The same 72 length/alignment combinations are checked numerically here (read
 * back out of the chunky buffer -- reading is not drawing) and, against the
 * original x86 code, by amiga\probes\ffe_xlattest.c plus
 * tools\translator\compare.py. This probe is the one that shows it.
 *
 * Evidence: PROGDIR:ffe_frame.raw, the exact 320x200 index buffer plus the
 * palette, turned into a PNG on the host by build\frame2png.py. Same format and
 * same reasoning as ffe_apitest.c -- no emulator window in the evidence chain.
 * The screen is then held for ~40 s so it can also be photographed, with a
 * heartbeat line every second because winuae\harness\run_probe.ps1 stops when
 * the log stops growing.
 */

#include <stdio.h>
#include <stdarg.h>

#include "ffeapi/ffeapi.h"
#include "amiga_gfx.h"		/* amigagfx_chunky / amigagfx_blit / amigagfx_millis */
#include "ffe_bufdesc.h"

/* The translated span renderer. C-ABI entry point (_FUNC_001601 in the .s):
 * the body follows the translator's t-ABI, in which d2 and d7 are scratch, and
 * calling it directly from C would corrupt this file's own locals.
 *
 *     void DrawHLine (int xpos, int ypos, int length, PALCOL col)
 */
extern void FUNC_001601 (int xpos, int ypos, int length, int col);

/* The game's back-buffer descriptor. Hand-written as a big-endian C structure
 * rather than copied from the 24 bytes at ffedat.asm:24815 -- ffe_bufdesc.h
 * carries the argument. `pixels` is filled in from amigagfx_chunky() once the
 * screen is open; the other five fields are what the original holds. */
FfeBufDesc DATA_007773 = { 0, 0, 0, FFE_SCREEN_W, FFE_SCREEN_H, 0 };

/* ---- palette ------------------------------------------------------------ */

#define C_BLACK    0
#define C_WHITE    1
#define C_SKY     16		/* 16..47  sky ramp, dark at the top          */
#define C_SKY_N   32
#define C_GROUND  48		/* 48..63  ground ramp                        */
#define C_GROUND_N 16
#define C_DISC    64		/* 64..79  the disc, shaded top to bottom     */
#define C_DISC_N  16
#define C_RUN     80		/* 80..95  the swatches, cycled               */
#define C_RUN_N   16

#define HORIZON_Y 128		/* first row of the white horizon line        */
#define HORIZON_H 2

#define DISC_CX  160
#define DISC_CY   64
#define DISC_R    44

/* The swatch grid: four columns, one per starting alignment, eighteen rows, one
 * per length. The column origins 8, 84, 160, 236 are all multiples of 4, so
 * adding the alignment gives 8, 85, 162, 239 -- x & 3 == 0, 1, 2, 3, which is
 * what the routine actually branches on. */
#define RUN_LEN_MAX 17
#define RUN_Y0     158
#define RUN_DY       2
#define RUN_X(a)   (8 + (a) * 76 + (a))
#define RUN_XMAX   (RUN_X (3) + RUN_LEN_MAX)	/* 256: nothing runs off the row */

static FILE *g_log;
static long  g_spans;		/* every DrawHLine call this file makes */
static int   g_pass, g_fail;

static void logf_ (const char *fmt, ...)
{
	va_list ap;
	if (g_log == NULL) return;
	va_start (ap, fmt);
	vfprintf (g_log, fmt, ap);
	va_end (ap);
	fputc ('\n', g_log);
	fflush (g_log);			/* a Guru must not cost us the last line */
}

static void check (int ok, const char *fmt, ...)
{
	va_list ap;
	if (ok) { g_pass++; return; }
	g_fail++;
	if (g_log == NULL) return;
	fputs ("FAIL ", g_log);
	va_start (ap, fmt);
	vfprintf (g_log, fmt, ap);
	va_end (ap);
	fputc ('\n', g_log);
	fflush (g_log);
}

static void set_pal (long index, int r, int g, int b)
{
	UCHAR v[3];
	v[0] = (UCHAR)r; v[1] = (UCHAR)g; v[2] = (UCHAR)b;	/* 6-bit VGA */
	VideoSetPalValue (index, v);
}

/* Chosen so every element of the picture is unmistakable in a photograph: the
 * sky ramp is monotone blue, the ground monotone brown, the disc orange, and
 * the sixteen swatch colours are all distinct from each other and from
 * everything else on screen.
 *
 * Every component stays inside 0..63. That is not decoration: VideoSetPalValue
 * scales by << 2 into a UCHAR, so a 64 would come back as 0 and a ramp would
 * suddenly go dark at its bright end -- which reads exactly like a drawing bug
 * and is not one. */
static void build_palette (void)
{
	int i;

	set_pal (C_BLACK, 0, 0, 0);
	set_pal (C_WHITE, 63, 63, 63);

	for (i = 0; i < C_SKY_N; i++)			/* night at the top, dusk at the horizon */
		set_pal (C_SKY + i, i / 4, 4 + i / 2, 16 + i + i / 3);

	for (i = 0; i < C_GROUND_N; i++)		/* near ground bright, far ground dark */
		set_pal (C_GROUND + i, 40 - i * 2, 24 - i, 8);

	for (i = 0; i < C_DISC_N; i++)			/* hot at the top, deep at the rim */
		set_pal (C_DISC + i, 63 - i, 52 - i * 3, 8);

	for (i = 0; i < C_RUN_N; i++)			/* 4 reds x 2 greens x 2 blues = 16 */
		set_pal (C_RUN + i,
		         20 + (i % 4) * 14,
		         20 + ((i / 4) % 2) * 43,
		         20 + ((i / 8) % 2) * 43);
}

/* ---- the scene, drawn with nothing but DrawHLine ------------------------ */

static void hline (int x, int y, int len, int col)
{
	FUNC_001601 (x, y, len, col);
	g_spans++;
}

/* Integer square root, for the disc's half-widths. Geometry, not drawing: the
 * pixels still all come from DrawHLine. */
static int isqrt_ (int v)
{
	int r = 0;
	while ((r + 1) * (r + 1) <= v) r++;
	return r;
}

static void draw_background (void)
{
	int y;

	for (y = 0; y < HORIZON_Y; y++)
		hline (0, y, FFE_SCREEN_W, C_SKY + y * C_SKY_N / HORIZON_Y);

	for (y = HORIZON_Y; y < HORIZON_Y + HORIZON_H; y++)
		hline (0, y, FFE_SCREEN_W, C_WHITE);

	for (y = HORIZON_Y + HORIZON_H; y < FFE_SCREEN_H; y++)
		hline (0, y, FFE_SCREEN_W,
		       C_GROUND + (y - HORIZON_Y - HORIZON_H) * C_GROUND_N
		                  / (FFE_SCREEN_H - HORIZON_Y - HORIZON_H));
}

/* Span by span, exactly as the polygon filler does it. Every row starts at a
 * different x and has a different length, so this is also the widest sweep
 * through the routine's three phases that the picture itself can show. */
static void draw_disc (void)
{
	int dy;

	for (dy = -DISC_R; dy <= DISC_R; dy++) {
		int half = isqrt_ (DISC_R * DISC_R - dy * dy);
		hline (DISC_CX - half, DISC_CY + dy, half * 2 + 1,
		       C_DISC + (dy + DISC_R) * C_DISC_N / (DISC_R * 2 + 1));
	}
}

static int run_colour (int a, int len)
{
	return C_RUN + (a * (RUN_LEN_MAX + 1) + len) % C_RUN_N;
}

static void draw_runs (void)
{
	int a, len;

	for (a = 0; a < 4; a++)
		for (len = 0; len <= RUN_LEN_MAX; len++) {
			int y = RUN_Y0 + len * RUN_DY;
			int x = RUN_X (a);
			hline (x, y,     len, run_colour (a, len));
			hline (x, y + 1, len, run_colour (a, len));
		}
}

/* ---- self-checks, read back out of the chunky buffer -------------------- */

/* Reading the framebuffer is not drawing it. Each swatch must be exactly `len`
 * pixels of its own colour at exactly the right place, with the ground colour
 * still intact on both sides -- which is what catches an off-by-one in the
 * leading-byte phase, an over-long longword phase, or a stride that is not 320.
 */
static void check_runs (const UCHAR *buf)
{
	int a, len, i;

	for (a = 0; a < 4; a++)
		for (len = 0; len <= RUN_LEN_MAX; len++) {
			int y   = RUN_Y0 + len * RUN_DY;
			int x   = RUN_X (a);
			int col = run_colour (a, len);
			const UCHAR *row = buf + (long)y * FFE_SCREEN_W;
			int bad = 0;

			for (i = 0; i < len; i++)
				if (row[x + i] != (UCHAR)col) bad = 1;
			if (row[x - 1] == (UCHAR)col) bad = 1;
			if (row[x + len] == (UCHAR)col) bad = 1;

			check (!bad, "run a=%d (x=%d, x&3=%d) len=%2d col=%d: "
			             "got [%d %d %d ... %d %d]",
			       a, x, x & 3, len, col,
			       row[x - 1], row[x], row[x + 1],
			       row[x + len - 1], row[x + len]);
		}
}

static void check_scene (const UCHAR *buf)
{
	int x, y, bad;

	/* Full-width spans: a whole row of one colour, and the right one. This is
	 * the stride check -- at 319 or 321 bytes per row the background would walk
	 * sideways and rows would stop being uniform. The disc is drawn over the
	 * sky, so the columns it can reach are skipped rather than the rows: that
	 * keeps every sampled row in the test. */
	for (y = 0; y < HORIZON_Y; y += 8) {
		int want = C_SKY + y * C_SKY_N / HORIZON_Y;
		const UCHAR *row = buf + (long)y * FFE_SCREEN_W;
		bad = 0;
		for (x = 0; x < FFE_SCREEN_W; x++) {
			if (x >= DISC_CX - DISC_R - 1 && x <= DISC_CX + DISC_R + 1)
				continue;
			if (row[x] != (UCHAR)want) bad = 1;
		}
		check (!bad, "sky row %d is not a uniform %d outside the disc "
		             "(x=0 gives %d, x=319 gives %d)",
		       y, want, row[0], row[FFE_SCREEN_W - 1]);
	}

	/* The same for the ground, on the rows above the swatch grid, where nothing
	 * else is drawn at all. */
	for (y = HORIZON_Y + HORIZON_H; y < RUN_Y0 - 1; y += 8) {
		int want = C_GROUND + (y - HORIZON_Y - HORIZON_H) * C_GROUND_N
		                      / (FFE_SCREEN_H - HORIZON_Y - HORIZON_H);
		const UCHAR *row = buf + (long)y * FFE_SCREEN_W;
		bad = 0;
		for (x = 0; x < FFE_SCREEN_W; x++)
			if (row[x] != (UCHAR)want) bad = 1;
		check (!bad, "ground row %d is not a uniform %d (x=0 gives %d, "
		             "x=319 gives %d)", y, want, row[0], row[FFE_SCREEN_W - 1]);
	}

	check (buf[(long)HORIZON_Y * FFE_SCREEN_W + 160] == C_WHITE,
	       "horizon row %d is %d, not white", HORIZON_Y,
	       buf[(long)HORIZON_Y * FFE_SCREEN_W + 160]);

	/* The disc: its widest row is the centre one, and it must be symmetric
	 * about DISC_CX. An asymmetric disc means the spans are not landing where
	 * their x says they are. */
	{
		const UCHAR *row = buf + (long)DISC_CY * FFE_SCREEN_W;
		check (row[DISC_CX - DISC_R] != C_BLACK
		       && row[DISC_CX - DISC_R] == row[DISC_CX + DISC_R],
		       "disc centre row is not symmetric: left edge %d, right edge %d",
		       row[DISC_CX - DISC_R], row[DISC_CX + DISC_R]);
		check (row[DISC_CX - DISC_R - 2] < C_DISC,
		       "disc centre row bleeds left: pixel at x=%d is %d",
		       DISC_CX - DISC_R - 2, row[DISC_CX - DISC_R - 2]);
		check (row[DISC_CX + DISC_R + 2] < C_DISC,
		       "disc centre row bleeds right: pixel at x=%d is %d",
		       DISC_CX + DISC_R + 2, row[DISC_CX + DISC_R + 2]);
	}

	/* A zero-length span must draw nothing at all, and a negative one must
	 * return before it touches memory. Both are drawn into the ground, well
	 * away from anything else, and the ground colour has to survive. */
	{
		int ty = FFE_SCREEN_H - 3;
		const UCHAR *row = buf + (long)ty * FFE_SCREEN_W;
		UCHAR before = row[300];
		hline (300, ty, 0, C_WHITE);
		hline (300, ty, -1, C_WHITE);
		hline (301, ty, -1000, C_WHITE);
		check (row[300] == before && row[301] == before,
		       "a zero or negative length wrote something: %d %d, was %d",
		       row[300], row[301], before);
	}
}

/* ---- framebuffer dump --------------------------------------------------- */

/* Byte for byte the format ffe_apitest.c writes, so build\frame2png.py reads it
 * unchanged: "FFEFRM01", 768 palette bytes (256 x r,g,b, 6-bit VGA), then
 * 320*200 colour indices. */
static void dump_frame (const UCHAR *buf)
{
	FILE *f = fopen (ffe_pd ("ffe_frame.raw"), "wb");
	UCHAR pal[3];
	int i;

	if (f == NULL) { logf_ ("could not open ffe_frame.raw"); return; }
	fwrite ("FFEFRM01", 1, 8, f);
	for (i = 0; i < 256; i++) {
		VideoGetPalValue (i, pal);
		fwrite (pal, 1, 3, f);
	}
	fwrite (buf, 1, (size_t)FFE_SCREEN_W * FFE_SCREEN_H, f);
	fclose (f);
	logf_ ("framebuffer dumped to ffe_frame.raw");
}

/* ---- main --------------------------------------------------------------- */

#define HOLD_MS    40000UL	/* long enough to photograph the screen */
#define SC_ESC     0x01
#define ESC_GRACE  3000UL	/* ms before Esc may cut the hold short */

int main (void)
{
	UCHAR *chunky;
	unsigned long t0, t, last = 0;
	const char *why = "held the full time";

	g_log = fopen (ffe_pd ("ffe_drawtest.log"), "w");
	logf_ ("FFE span renderer probe -- every pixel drawn by the TRANSLATED "
	       "FUNC_001601_DrawHLine");

	SystemInit ();
	if (!ffe_system_ready ()) {
		logf_ ("FAIL: SystemInit could not bring the layer up (screen refused?)");
		if (g_log) fclose (g_log);
		return 20;
	}
	logf_ ("SystemInit ok: %dx%dx8", FFE_SCREEN_W, FFE_SCREEN_H);

	/* The whole point of the exercise: the game's descriptor points at the
	 * buffer the c2p already reads, so DrawHLine writes into the screen with
	 * nothing in between. */
	chunky = amigagfx_chunky ();
	if (chunky == NULL) {
		logf_ ("FAIL: amigagfx_chunky() is NULL");
		SystemCleanup ();
		if (g_log) fclose (g_log);
		return 20;
	}
	DATA_007773.pixels = chunky;
	logf_ ("DATA_007773 = { %p, %ld, %ld, %ld, %ld, %ld }  (pitch %d)",
	       (void *)DATA_007773.pixels, DATA_007773.x, DATA_007773.y,
	       DATA_007773.w, DATA_007773.h, DATA_007773.jump, amigagfx_pitch ());

	build_palette ();

	t0 = amigagfx_millis ();
	draw_background ();
	draw_disc ();
	draw_runs ();
	t = amigagfx_millis ();
	logf_ ("%ld spans drawn in %lu ms", g_spans, t - t0);

	amigagfx_blit (0, 0, FFE_SCREEN_W, FFE_SCREEN_H);
	logf_ ("blitted (c2p) in %lu ms", amigagfx_millis () - t);

	check_runs (chunky);
	check_scene (chunky);
	logf_ ("swatch grid: 4 alignments x %d lengths, x %d..%d, rows %d..%d",
	       RUN_LEN_MAX + 1, RUN_X (0), RUN_XMAX, RUN_Y0,
	       RUN_Y0 + RUN_LEN_MAX * RUN_DY + 1);
	logf_ ("self-checks: %d passed, %d failed  (%ld spans total)",
	       g_pass, g_fail, g_spans);

	dump_frame (chunky);

	/* Hold the picture. One line a second, because run_probe.ps1 decides the
	 * probe is finished when the log stops growing -- a silent 40 s wait would
	 * have the emulator stopped out from under the screen we are holding. */
	t0 = amigagfx_millis ();
	for (;;) {
		UCHAR keys[256];
		unsigned long dt = amigagfx_millis () - t0;

		if (dt >= HOLD_MS) break;
		if (dt / 1000 != last) {
			last = dt / 1000;
			logf_ ("holding %lu / %lu s", last, HOLD_MS / 1000);
		}
		InputKeybReadStates (keys);
		if (dt >= ESC_GRACE && keys[SC_ESC] == 0x00) {
			why = "Esc held";
			break;
		}
		TimerSleep ();
	}

	logf_ ("---- summary ----");
	logf_ ("hold ended: %s", why);
	logf_ ("spans drawn : %ld", g_spans);
	logf_ ("self-checks : %d passed, %d failed", g_pass, g_fail);

	SystemCleanup ();
	logf_ ("SystemCleanup done, closed cleanly");
	if (g_log) fclose (g_log);
	return g_fail ? 1 : 0;
}
