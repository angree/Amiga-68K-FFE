/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 angree. See LICENSE at the repository root.
 *
 * First Amiga artefact of the FFE port: prove the AGA LORES 320x200 8bpp path
 * and MEASURE what it costs, before any translated game code exists.
 *
 * Why this exact size: FFE renders into one 320x200 8bpp chunky buffer and hands
 * it over whole (ref\jjffe\sdl\sdlvideo.cpp: pBackBufBase[320*200+32]). The game
 * also keeps a second descriptor for a 320x158 "upper backbuffer" -- the 3D
 * viewport above the cockpit console (ffedat.asm:24820, DATA_007774). In flight
 * only those 158 lines change, so both are measured: full frame and viewport.
 * The difference decides whether the AGA path is worth having on a 68030.
 *
 * TIMING: amigagfx_millis() is DateStamp-based, i.e. 20 ms granular, and reports
 * a flat zero for a hundred c2p blits. This probe therefore opens timer.device
 * UNIT_ECLOCK itself -- ~709 kHz on PAL, so ~1.4 us per tick, which is the right
 * order for a 64000-pixel conversion. Do not "simplify" this back to millis().
 *
 * What a correct run looks like on screen:
 *   - a 1-pixel white frame flush against all four edges  -> really 320x200,
 *     nothing clipped or shifted;
 *   - a smooth diagonal gradient                          -> chunky stride
 *     matches the bitmap; a mismatch shows as a shear;
 *   - a 16x16 grid of colour blocks                       -> all 256 registers
 *     set and every bit landing in the right plane. The four ramps
 *     (red/green/blue/grey) make a wrong index a wrong FAMILY of colour, which
 *     the eye catches instantly, rather than a slightly wrong shade.
 *
 * The pattern is HELD on screen for HOLD_SEC regardless of input: the run script
 * detaches us with `Run >NIL: <NIL:`, and stray events left over from the boot
 * were ending the first version before any screenshot could be taken.
 *
 * Results go to PROGDIR:ffe_agatest.log -- no shell redirection survives Run.
 */

#include <stdio.h>
#include <stdarg.h>
#include <string.h>

#include <exec/types.h>
#include <devices/timer.h>
#include <proto/exec.h>
#include <proto/timer.h>
#include <proto/dos.h>

#include "amiga_gfx.h"

#define W        320
#define H        200
#define VPH      158            /* the 3D viewport, DATA_007774 */
#define FRAMES   50
#define ROUNDS   3              /* see the warm-up note on bench() */
#define HOLD_SEC 60

static FILE *g_log;

static void logf_(const char *fmt, ...)
{
	va_list ap;
	if (g_log == NULL) return;
	va_start(ap, fmt);
	vfprintf(g_log, fmt, ap);
	va_end(ap);
	fputc('\n', g_log);
	fflush(g_log);          /* a Guru must not cost us the last line */
}

/* ---- EClock timing ---------------------------------------------------- */

/* The NDK's proto/timer.h declares this as `struct Device *`, not the
 * `struct Library *` the other library bases use -- timer.device really is a
 * device. Matching it exactly is required or the definition conflicts. */
struct Device *TimerBase;
static struct timerequest g_tr;
static int   g_timer_open;
static ULONG g_eclock_hz;

static int timer_open(void)
{
	if (OpenDevice((CONST_STRPTR)"timer.device", UNIT_ECLOCK,
	               (struct IORequest *)&g_tr, 0) != 0)
		return 0;
	TimerBase = g_tr.tr_node.io_Device;
	g_timer_open = 1;
	return 1;
}

static void timer_close(void)
{
	if (g_timer_open) { CloseDevice((struct IORequest *)&g_tr); g_timer_open = 0; }
}

/* 32 low bits are plenty: at 709 kHz they wrap after ~100 minutes. */
static ULONG eclock_now(void)
{
	struct EClockVal ev;
	if (!g_timer_open) return 0;
	g_eclock_hz = ReadEClock(&ev);
	return ev.ev_lo;
}

/* ---- test pattern ------------------------------------------------------ */

/* Four ramps, so a wrong palette index shows as a wrong FAMILY of colour rather
 * than a slightly wrong shade. 6-bit values scaled to 8-bit exactly the way
 * FFE's own VideoSetPalValue does it (<< 2), to exercise the same range. */
static void build_palette(unsigned char *rgb)
{
	int i;
	for (i = 0; i < 256; i++) {
		int v = (i & 63) << 2;              /* 0..252 */
		unsigned char r = 0, g = 0, b = 0;
		switch (i >> 6) {
		case 0:  r = (unsigned char)v; break;
		case 1:  g = (unsigned char)v; break;
		case 2:  b = (unsigned char)v; break;
		default: r = g = b = (unsigned char)v; break;
		}
		rgb[i * 3 + 0] = r; rgb[i * 3 + 1] = g; rgb[i * 3 + 2] = b;
	}
}

static void draw_pattern(unsigned char *buf, int pitch)
{
	int x, y;

	for (y = 0; y < H; y++) {
		unsigned char *row = buf + (long)y * pitch;
		for (x = 0; x < W; x++)
			row[x] = (unsigned char)((x + y) & 0xff);
	}

	for (y = 0; y < 128; y++) {          /* 16x16 palette grid, centred */
		unsigned char *row = buf + (long)(y + 36) * pitch + 32;
		for (x = 0; x < 256; x++)
			row[x] = (unsigned char)((y / 8) * 16 + (x / 16));
	}

	for (x = 0; x < W; x++) {            /* white frame, flush to the edges */
		buf[x] = 255;
		buf[(long)(H - 1) * pitch + x] = 255;
	}
	for (y = 0; y < H; y++) {
		buf[(long)y * pitch] = 255;
		buf[(long)y * pitch + W - 1] = 255;
	}
}

static ULONG bench(int h)
{
	ULONG t0, t1;
	int i;
	amigagfx_blit(0, 0, W, h);           /* warm caches, excluded from timing */
	t0 = eclock_now();
	for (i = 0; i < FRAMES; i++)
		amigagfx_blit(0, 0, W, h);
	t1 = eclock_now();
	return t1 - t0;
}

static void report(const char *what, int h, ULONG ticks)
{
	ULONG us_total, us_frame;
	if (g_eclock_hz == 0) { logf_("%s: no eclock", what); return; }
	/* ticks * 1e6 / hz would overflow 32 bits, so scale in two steps */
	us_total = (ULONG)(((unsigned long long)ticks * 1000000ULL) / g_eclock_hz);
	us_frame = us_total / FRAMES;
	logf_("%s 320x%3d : %lu ticks = %lu us total, %lu.%03lu ms/frame -> %lu fps ceiling",
	      what, h, ticks, us_total,
	      us_frame / 1000, us_frame % 1000,
	      us_frame ? (1000000UL / us_frame) : 0);
}

int main(void)
{
	unsigned char pal[256 * 3];
	unsigned char *chunky;
	int pitch, backend, gh;
	ULONG t_full, t_vp;
	ULONG hold_start;

	g_log = fopen("PROGDIR:ffe_agatest.log", "w");
	logf_("FFE AGA lores probe -- 320x200x8 chunky, Kalms c2p_rect");

	if (!timer_open())
		logf_("WARNING: timer.device UNIT_ECLOCK would not open - no timings");

	if (amigagfx_open(W, H, 0, AMIGAGFX_BACKEND_AGA) != 0) {
		logf_("FAIL: amigagfx_open(%d,%d) refused", W, H);
		timer_close();
		if (g_log) fclose(g_log);
		return 20;
	}

	backend = amigagfx_backend();
	gh      = amigagfx_game_height();
	chunky  = amigagfx_chunky();
	pitch   = amigagfx_pitch();

	logf_("backend=%s game_height=%d pitch=%d chunky=%p",
	      backend == AMIGAGFX_BACKEND_AGA ? "AGA" :
	      backend == AMIGAGFX_BACKEND_RTG ? "RTG" : "EHB", gh, pitch, chunky);
	logf_("chip free=%lu KB, fast free=%lu KB",
	      (unsigned long)(AvailMem(MEMF_CHIP) >> 10),
	      (unsigned long)(AvailMem(MEMF_FAST) >> 10));

	if (backend != AMIGAGFX_BACKEND_AGA)
		logf_("WARNING: asked for AGA, got something else - timings are NOT the AGA path");
	if (gh != H)    logf_("WARNING: game height %d != %d", gh, H);
	if (pitch != W) logf_("WARNING: pitch %d != %d", pitch, W);

	build_palette(pal);
	amigagfx_set_palette(pal, 0, 256);
	draw_pattern(chunky, pitch);

	/* Three rounds each, alternating, and every round logged. The first
	 * measurement of a run is not trustworthy: on the first attempt the full
	 * frame came out at 0.284 ms and the 158-line viewport at 0.051 ms -- an
	 * 18% ratio where the geometry says 79%. That is a warm-up artefact, not a
	 * property of the c2p, and reporting one number would have hidden it.
	 * Compare like-for-like rounds, and distrust round 1. */
	logf_("eclock = %lu Hz, %d frames per round, %d rounds", g_eclock_hz, FRAMES, ROUNDS);
	{
		int r;
		for (r = 0; r < ROUNDS; r++) {
			t_full = bench(H);
			t_vp   = bench(VPH);
			logf_("--- round %d ---", r + 1);
			report("c2p full ", H,   t_full);
			report("c2p vport", VPH, t_vp);
			if (t_full)
				logf_("  viewport = %lu%% of a full frame (geometry says %d%%)",
				      100UL * t_vp / t_full, 100 * VPH / H);
		}
	}

	amigagfx_blit(0, 0, W, H);
	logf_("pattern on screen, holding %d s", HOLD_SEC);

	/* Hold unconditionally. Input is drained but never ends the run: events left
	 * over from the boot were killing the first version instantly. */
	hold_start = eclock_now();
	for (;;) {
		AmigaGfxEvent ev;
		while (amigagfx_poll(&ev)) { /* drain */ }
		Delay(10);                                        /* 0.2 s, no busy spin */
		if (g_eclock_hz == 0) break;
		if ((eclock_now() - hold_start) / g_eclock_hz >= (ULONG)HOLD_SEC) break;
	}

	amigagfx_close();
	timer_close();
	logf_("closed cleanly");
	if (g_log) fclose(g_log);
	return 0;
}
