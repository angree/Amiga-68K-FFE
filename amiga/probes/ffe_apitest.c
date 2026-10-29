/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 angree. See LICENSE at the repository root.
 *
 * Drives the FFE Amiga platform layer (amiga/ffeapi/) exactly the way the game
 * will, with no game code present: SystemInit, then a frame loop of read input
 * -> draw -> full-width VideoBlit (which is the flip) -> TimerFrameUpdate.
 *
 * WHAT A CORRECT RUN LOOKS LIKE ON SCREEN -- one screenshot has to prove the
 * whole layer, so everything is a filled rectangle and no font is needed:
 *   - the background is a gradient of horizontal bands scrolling upwards, and
 *     a yellow box bounces across it   -> the frame loop runs and flips;
 *   - top strip: a white bar whose LENGTH is the mouse X (full width = 319),
 *     left edge: the same for mouse Y  -> InputMouseReadPos, in game coords;
 *   - two squares top right, red and blue, lit while the left / right mouse
 *     button is held                   -> InputMouseReadButtons;
 *   - two short bars above the bottom edge growing left or right from the
 *     centre with pointer movement     -> InputMouseReadMickeys (relative, and
 *     they fall back to nothing the moment the pointer stops, because the call
 *     zeroes the accumulator);
 *   - six boxes across the lower half, lit green while held, identified by the
 *     dots underneath: 1 = KP8, 2 = KP2, 3 = KP4, 4 = KP6, 5 = Space,
 *     6 = Esc                          -> InputKeybReadStates, and with it the
 *     DOS-scancode -> Amiga-rawkey table in ffe_keymap.c;
 *   - eight cells top right below the buttons: the last key code in binary,
 *     MSB left                         -> InputKeybGetLastKey / SetLastKey;
 *   - a bar creeping along the bottom edge -> the frame budget.
 *
 * Before the loop the three blits and the palette calls are checked against
 * computed expectations and the result is logged, because the transparency
 * rule of VideoMaskedBlit and the jump + w stride of VideoBlit /
 * VideoReverseBlit are not things a screenshot can prove.
 *
 * Results go to PROGDIR:ffe_apitest.log, one fflush per line: the run script
 * detaches us with `Run >NIL: <NIL:` and no shell redirection survives that.
 * Same reason ffe_agatest.c logs the way it does.
 */

#include <stdio.h>
#include <stdarg.h>
#include <string.h>

#include "ffeapi/ffeapi.h"
#include "amiga_gfx.h"		/* only for amigagfx_millis(): wall clock vs game clock */

/* ~60 s at the ~3 ms/frame this layer costs on the reference machine. It has to
 * outlive the emulator's boot plus the time the harness needs to focus the
 * window and inject keys; at 1200 frames the run was over in four seconds and
 * every screenshot caught Workbench instead. Esc still ends it early. */
#define FRAMES     20000
#define LOG_EVERY  100
#define ESC_GRACE  100		/* frames before Esc may end the run -- see below */

/* The keys FFE flies with, in DOS/PC set-1 scancodes (what InputKeybReadStates
 * indexes by). The numeric keypad is pitch and roll, Space fires. */
#define SC_ESC   0x01
#define SC_SPACE 0x39
#define SC_KP8   0x48
#define SC_KP2   0x50
#define SC_KP4   0x4b
#define SC_KP6   0x4d

/* Palette indices. 0..8 are flat colours, 32..95 the background ramp. */
#define C_BLACK   0
#define C_WHITE   1
#define C_DARK    2
#define C_RED     3
#define C_GREEN   4
#define C_BLUE    5
#define C_YELLOW  6
#define C_RAMP    32

static FILE *g_log;

/* 64000 bytes, and static on purpose: the Amiga default stack is 4 KB and a
 * back buffer on it is an instant crash. The game's own back buffer is a BSS
 * block for the same reason (ffedat.asm). */
static UCHAR g_buf[FFE_SCREEN_W * FFE_SCREEN_H];

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

/* ---- drawing ------------------------------------------------------------ */

static void fill_rect (UCHAR *buf, int x, int y, int w, int h, UCHAR c)
{
	int j;

	if (x < 0) { w += x; x = 0; }
	if (y < 0) { h += y; y = 0; }
	if (x + w > FFE_SCREEN_W) w = FFE_SCREEN_W - x;
	if (y + h > FFE_SCREEN_H) h = FFE_SCREEN_H - y;
	if (w <= 0 || h <= 0) return;

	for (j = 0; j < h; j++)
		memset (buf + (long)(y + j) * FFE_SCREEN_W + x, c, (size_t)w);
}

static void set_pal (long index, int r, int g, int b)
{
	UCHAR v[3];
	v[0] = (UCHAR)r; v[1] = (UCHAR)g; v[2] = (UCHAR)b;	/* 6-bit VGA, 0..63 */
	VideoSetPalValue (index, v);
}

static void build_palette (void)
{
	int i;

	set_pal (C_BLACK,  0,  0,  0);
	set_pal (C_WHITE, 63, 63, 63);
	set_pal (C_DARK,  10, 10, 14);
	set_pal (C_RED,   63,  8,  8);
	set_pal (C_GREEN,  8, 63,  8);
	set_pal (C_BLUE,  16, 16, 63);
	set_pal (C_YELLOW,63, 63,  8);

	for (i = 0; i < 64; i++)			/* background ramp, blue-ish */
		set_pal (C_RAMP + i, i / 4, i / 3, i);
}

/* Everything that reflects live API state. Values are passed in rather than
 * read here, so each API function is called exactly once per frame -- which
 * matters for InputMouseReadMickeys, whose read is destructive. */
static void draw_frame (int frame, long mx, long my, long mkx, long mky,
                        long buttons, long shownkey, const UCHAR *keys)
{
	static const int sc[6] = { SC_KP8, SC_KP2, SC_KP4, SC_KP6, SC_SPACE, SC_ESC };
	int i, y, bx;

	/* Scrolling gradient bands. One memset per row is cheap enough to leave
	 * the frame time dominated by the c2p, which is what we want to look at. */
	for (y = 0; y < FFE_SCREEN_H; y++)
		memset (g_buf + (long)y * FFE_SCREEN_W,
		        (UCHAR)(C_RAMP + ((y + frame * 2) & 63)), FFE_SCREEN_W);

	/* Bouncing box: obvious motion even if input is dead. */
	{
		int px = frame * 3 % 600; if (px > 300) px = 600 - px;
		int py = frame * 2 % 300; if (py > 150) py = 300 - py;
		fill_rect (g_buf, px, 24 + py / 4, 24, 24, C_YELLOW);
	}

	/* Mouse X as a bar length along the top, mouse Y down the left edge. */
	fill_rect (g_buf, 0, 4, FFE_SCREEN_W, 6, C_DARK);
	fill_rect (g_buf, 0, 4, (int)mx + 1, 6, C_WHITE);
	fill_rect (g_buf, 0, 12, 6, FFE_SCREEN_H - 12, C_DARK);
	fill_rect (g_buf, 0, 12, 6, (int)my + 1, C_WHITE);

	/* Mouse buttons. */
	fill_rect (g_buf, 268, 4, 20, 20, (buttons & 1) ? C_RED  : C_DARK);
	fill_rect (g_buf, 292, 4, 20, 20, (buttons & 2) ? C_BLUE : C_DARK);

	/* Last key, in binary, MSB left. */
	for (i = 0; i < 8; i++)
		fill_rect (g_buf, 232 + i * 10, 28, 8, 8,
		           (shownkey & (0x80 >> i)) ? C_WHITE : C_DARK);

	/* Mickeys: two bars growing from the centre, clamped to +/-60 px. */
	{
		int len;
		fill_rect (g_buf, 96, 176, 128, 7, C_DARK);
		fill_rect (g_buf, 96, 184, 128, 7, C_DARK);
		len = (int)mkx * 4; if (len > 60) len = 60; if (len < -60) len = -60;
		if (len >= 0) fill_rect (g_buf, 160, 176, len + 1, 7, C_YELLOW);
		else          fill_rect (g_buf, 160 + len, 176, -len, 7, C_YELLOW);
		len = (int)mky * 4; if (len > 60) len = 60; if (len < -60) len = -60;
		if (len >= 0) fill_rect (g_buf, 160, 184, len + 1, 7, C_YELLOW);
		else          fill_rect (g_buf, 160 + len, 184, -len, 7, C_YELLOW);
	}

	/* Six key boxes with 1..6 identifying dots underneath. keys[] polarity is
	 * inverted: 0x00 means the key IS held. */
	for (i = 0; i < 6; i++)
	{
		int j;
		bx = 8 + i * 50;
		fill_rect (g_buf, bx, 112, 44, 26, keys[sc[i]] == 0x00 ? C_GREEN : C_DARK);
		for (j = 0; j <= i; j++)
			fill_rect (g_buf, bx + 2 + j * 7, 140, 5, 5, C_WHITE);
	}

	/* Frame budget. */
	fill_rect (g_buf, 0, 196, FFE_SCREEN_W, 4, C_DARK);
	fill_rect (g_buf, 0, 196, frame * FFE_SCREEN_W / FRAMES, 4, C_YELLOW);
}

/* ---- blit and palette self-test ----------------------------------------- */

/* Value of the reference pattern at x,y. Cheap, and reproducible without
 * keeping a second 64 KB copy of it around. */
static UCHAR pat (int x, int y)
{
	return (UCHAR)(((x * 7) ^ (y * 13)) & 0xff);
}

/* Uses nothing but the ffeapi surface: VideoBlit puts a known pattern on the
 * screen, VideoMaskedBlit stamps a half-transparent sprite into it, and
 * VideoReverseBlit reads the result back for comparison. Returns the number of
 * failures, all of them logged. */
static int blit_selftest (void)
{
	static UCHAR sprite[16 * 16];
	static UCHAR readback[16 * 16];
	static UCHAR strip[8 * 36];			/* 32 wide, stride 36 -> jump 4 */
	int x, y, bad = 0, mism;

	for (y = 0; y < FFE_SCREEN_H; y++)
		for (x = 0; x < FFE_SCREEN_W; x++)
			g_buf[(long)y * FFE_SCREEN_W + x] = pat (x, y);
	VideoBlit (g_buf, 0, 0, FFE_SCREEN_W, FFE_SCREEN_H, 0);		/* copy + flip */

	/* Left half of every sprite row transparent (0), right half opaque. */
	for (y = 0; y < 16; y++)
		for (x = 0; x < 16; x++)
			sprite[y * 16 + x] = (UCHAR)(x < 8 ? 0 : 100 + y);

	VideoMaskedBlit (sprite, 100, 50, 16, 16, 0);
	VideoReverseBlit (readback, 100, 50, 16, 16, 0);

	mism = 0;
	for (y = 0; y < 16; y++)
		for (x = 0; x < 16; x++) {
			UCHAR want = sprite[y * 16 + x] ? sprite[y * 16 + x] : pat (100 + x, 50 + y);
			if (readback[y * 16 + x] != want) mism++;
		}
	if (mism) { logf_ ("FAIL masked/reverse blit: %d of 256 bytes wrong", mism); bad++; }
	else logf_ ("ok  masked blit transparency + reverse blit round trip (16x16)");

	/* Partial VideoBlit: 32x8 out of a 36-byte-stride source, i.e. jump = 4.
	 * Must NOT flip, and must not touch the 4 skipped source bytes per row. */
	for (y = 0; y < 8; y++)
		for (x = 0; x < 36; x++)
			strip[y * 36 + x] = (UCHAR)(x < 32 ? 200 + x % 32 : 0);
	VideoBlit (strip, 40, 30, 32, 8, 4);
	VideoReverseBlit (readback, 40, 30, 16, 8, 0);

	mism = 0;
	for (y = 0; y < 8; y++)
		for (x = 0; x < 16; x++)
			if (readback[y * 16 + x] != (UCHAR)(200 + x)) mism++;
	if (mism) { logf_ ("FAIL partial VideoBlit stride: %d of 128 bytes wrong", mism); bad++; }
	else logf_ ("ok  partial VideoBlit with jump=4 (source stride jump + w)");

	/* Palette round trip, 6-bit in, 6-bit out. */
	mism = 0;
	for (x = 0; x < 256; x++) {
		UCHAR in[3], out[3];
		in[0] = (UCHAR)(x & 63); in[1] = (UCHAR)((x * 3) & 63); in[2] = (UCHAR)((x * 5) & 63);
		VideoSetPalValue (x, in);
		VideoGetPalValue (x, out);
		if (out[0] != in[0] || out[1] != in[1] || out[2] != in[2]) mism++;
	}
	if (mism) { logf_ ("FAIL palette round trip: %d of 256 entries wrong", mism); bad++; }
	else logf_ ("ok  VideoSetPalValue / VideoGetPalValue round trip, all 256 entries");

	logf_ ("VideoPointerExclusive() = %ld (0 expected: Intuition owns the pointer)",
	       VideoPointerExclusive ());
	VideoPointerEnable ();
	VideoPointerDisable ();
	VideoPointerEnable ();

	{
		long jx = 123, jy = 456, jb;
		InputJoyReadPos (&jx, &jy);
		jb = InputJoyReadButtons ();
		logf_ ("InputJoyReadPos() = %ld,%ld  InputJoyReadButtons() = %ld (stubs, as in the SDL layer)",
		       jx, jy, jb);
		if (jx != 0 || jy != 0 || jb != 0) { logf_ ("FAIL joystick stubs"); bad++; }
	}

	/* TimerSleep must be about a millisecond, not the 20 ms an AmigaDOS
	 * Delay(1) would cost. 100 of them against the 20 ms-granular wall clock
	 * is enough to tell those two apart. */
	{
		unsigned long t0 = amigagfx_millis (), t1;
		int i;
		for (i = 0; i < 100; i++) TimerSleep ();
		t1 = amigagfx_millis ();
		logf_ ("100 x TimerSleep() = %lu ms wall (about 100 expected; 2000 would mean Delay(1))",
		       t1 - t0);
	}

	return bad;
}

/* ---- framebuffer dump --------------------------------------------------- */

#define DUMP_EVERY 200
static int g_last_dump = -1;

/* Write the exact buffer the game side draws into, plus the palette, so a
 * picture can be made ON THE HOST without capturing the emulator window at all.
 *
 * Two reasons this matters more than convenience. The user records the primary
 * monitor for a timelapse, so our window must never appear there; and capturing
 * a window parked on a secondary monitor is unreliable -- an earlier run
 * photographed a browser that happened to be on top and reported it as the
 * emulated screen. A dump is also better evidence than a screenshot: it is the
 * real 320x200 index buffer, not a scaled photograph of one.
 *
 * Format: "FFEFRM01", 768 bytes of palette (256 x r,g,b in the 6-bit VGA range
 * the game uses), then 320*200 colour indices. Rewritten in place every
 * DUMP_EVERY frames, so whatever moment the run is stopped at, the file holds a
 * recent frame. Reading the palette back through VideoGetPalValue exercises
 * that function too. */
static void dump_frame (int frame)
{
	FILE *f = fopen (ffe_pd ("ffe_frame.raw"), "wb");
	UCHAR pal[3];
	int i;

	if (f == NULL) return;
	fwrite ("FFEFRM01", 1, 8, f);
	for (i = 0; i < 256; i++) {
		VideoGetPalValue (i, pal);
		fwrite (pal, 1, 3, f);
	}
	fwrite (g_buf, 1, (size_t)FFE_SCREEN_W * FFE_SCREEN_H, f);
	fclose (f);
	g_last_dump = frame;
}

/* ---- main --------------------------------------------------------------- */

int main (void)
{
	UCHAR keys[256];
	long mx = 0, my = 0, mkx = 0, mky = 0, buttons = 0, lastkey, shownkey = 0xff;
	long max_mx = 0, max_my = 0;
	unsigned long t_start, t_end;
	ULONG stamp_start, stamp_end;
	int frame = 0, selftest_bad;
	int seen_left = 0, seen_right = 0, key_events = 0;
	int seen[6];
	static const int sc[6] = { SC_KP8, SC_KP2, SC_KP4, SC_KP6, SC_SPACE, SC_ESC };
	static const char *scname[6] = { "KP8", "KP2", "KP4", "KP6", "Space", "Esc" };
	const char *why = "frame budget";
	int i;

	memset (seen, 0, sizeof (seen));

	g_log = fopen (ffe_pd ("ffe_apitest.log"), "w");
	logf_ ("FFE platform layer probe -- video / input / timer, no game code");

	SystemInit ();
	if (!ffe_system_ready ()) {
		logf_ ("FAIL: SystemInit could not bring the layer up (screen refused?)");
		if (g_log) fclose (g_log);
		return 20;
	}
	logf_ ("SystemInit ok: 320x200x8 AGA, timer/video/input up");

	selftest_bad = blit_selftest ();
	logf_ ("self-test: %d failure(s)", selftest_bad);

	build_palette ();
	InputKeybSetLastKey (0xff);

	t_start     = amigagfx_millis ();
	stamp_start = TimerGetTimeStamp ();
	logf_ ("entering frame loop: %d frames max, Esc ends it after frame %d",
	       FRAMES, ESC_GRACE);

	for (frame = 0; frame < FRAMES; frame++)
	{
		InputKeybReadStates (keys);
		InputMouseReadPos (&mx, &my);
		InputMouseReadMickeys (&mkx, &mky);
		buttons = InputMouseReadButtons ();

		/* Consume the pending key the way FFE's text entry does: read it, then
		 * push 0xff back so the next press (or the next auto-repeat tick) is
		 * distinguishable from this one. */
		lastkey = InputKeybGetLastKey ();
		if (lastkey != 0xff) {
			shownkey = lastkey;
			key_events++;
			InputKeybSetLastKey (0xff);
		}

		if (mx > max_mx) max_mx = mx;
		if (my > max_my) max_my = my;
		if (buttons & 1) seen_left = 1;
		if (buttons & 2) seen_right = 1;
		for (i = 0; i < 6; i++) if (keys[sc[i]] == 0x00) seen[i] = 1;

		draw_frame (frame, mx, my, mkx, mky, buttons, shownkey, keys);
		VideoBlit (g_buf, 0, 0, FFE_SCREEN_W, FFE_SCREEN_H, 0);		/* the flip */
		TimerFrameUpdate ();

		if (frame % DUMP_EVERY == 0) dump_frame (frame);

		if (frame % LOG_EVERY == 0)
			logf_ ("f=%4d stamp=%lu wall=%lu pos=%ld,%ld mick=%ld,%ld btn=%ld "
			       "lastkey=0x%02lx keys KP8=%02x KP2=%02x KP4=%02x KP6=%02x SPC=%02x ESC=%02x",
			       frame, (unsigned long)TimerGetTimeStamp (),
			       amigagfx_millis () - t_start, mx, my, mkx, mky, buttons,
			       (unsigned long)shownkey,
			       keys[SC_KP8], keys[SC_KP2], keys[SC_KP4],
			       keys[SC_KP6], keys[SC_SPACE], keys[SC_ESC]);

		/* Esc is ignored for the first frames: ffe_agatest.c was being killed
		 * instantly by keystrokes left over from the boot, and the run script
		 * still detaches us the same way. */
		if (frame >= ESC_GRACE && keys[SC_ESC] == 0x00) { why = "Esc held"; break; }
	}

	t_end     = amigagfx_millis ();
	stamp_end = TimerGetTimeStamp ();

	dump_frame (frame);          /* final state, whatever it is */
	logf_ ("---- summary ----");
	logf_ ("framebuffer dumped to ffe_frame.raw at frame %d", g_last_dump);
	logf_ ("ended after %d frames: %s", frame, why);
	logf_ ("wall %lu ms, %lu ms/frame -> %lu fps",
	       t_end - t_start,
	       frame ? (t_end - t_start) / (unsigned long)frame : 0,
	       (t_end - t_start) ? 1000UL * (unsigned long)frame / (t_end - t_start) : 0);
	/* The two should track each other closely: the frame's own duration is
	 * walked into the game clock, and only what TimerFrameUpdate spends
	 * pumping input (plus any remainder below 5 ms) is dropped. A large gap
	 * would mean the clock is not being advanced at all. */
	logf_ ("game clock advanced %lu ms against %lu ms of wall time",
	       (unsigned long)(stamp_end - stamp_start), t_end - t_start);
	logf_ ("mouse max seen %ld,%ld (319,199 reachable)", max_mx, max_my);
	logf_ ("buttons seen: left=%s right=%s", seen_left ? "yes" : "no",
	       seen_right ? "yes" : "no");
	for (i = 0; i < 6; i++)
		logf_ ("key %-5s (scancode 0x%02x) held at least once: %s",
		       scname[i], sc[i], seen[i] ? "yes" : "no");
	logf_ ("last-key events consumed (presses + auto-repeats): %d", key_events);
	logf_ ("self-test failures: %d", selftest_bad);

	SystemCleanup ();
	logf_ ("SystemCleanup done, closed cleanly");
	if (g_log) fclose (g_log);
	return 0;
}
