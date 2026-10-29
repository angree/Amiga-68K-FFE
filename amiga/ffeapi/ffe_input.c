/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 angree. See LICENSE at the repository root.
 *
 * FFE mouse / keyboard / joystick API on Intuition input.
 *
 * All state comes from one place, ffe_pump_input(), which drains
 * amigagfx_poll() and updates: pointer position, the mickey (relative
 * movement) accumulators, the two mouse buttons and held[] -- one byte per
 * Amiga RAW key code. Every reader below pumps first, so the game sees fresh
 * state whether or not it called TimerFrameUpdate this frame; that mirrors the
 * SDL layer, where SDL's own event queue is what all the readers look at.
 *
 * The keyboard is the interesting half. The game asks for key state indexed by
 * DOS/PC set-1 SCANCODE, we receive Amiga RAW key codes, and the two have
 * nothing to do with each other -- ffe_keymap.c bridges them in the direction
 * the game asks in (scancode -> rawkey), so a query is one table lookup and
 * one array read. Two details are easy to get wrong and both are copied
 * verbatim from ref/jjffe/sdl/sdlinput.cpp:
 *   - the polarity of InputKeybReadStates is INVERTED: 0x80 not held, 0 held;
 *   - "last key" auto-repeat runs off the 5 ms game clock, not off real time,
 *     and its state machine lives inside InputKeybReadStates -- the game only
 *     gets repeats while it is actually reading the keyboard.
 *
 * No Amiga headers: amiga_gfx.h is the whole interface to Intuition.
 */

#include <stdio.h>
#include "ffe_dev.h"
#include <string.h>

#include "ffeapi.h"
#include "ffe_keymap.h"
#include "amiga_gfx.h"

/* FIRST-LIGHT DEBUG, remove once input is visually verified in the game: the
 * first key events land in PROGDIR:ffe_input.log so a run can prove whether
 * keystrokes reach the emulated side at all. */
static int g_dbg_keys;

static void dbg_key (int code)
{
	FILE *f;

	if (g_dbg_keys >= 40) return;
	f = ffe_logopen (ffe_pd ("ffe_input.log"), "a");
	if (f == NULL) return;
	fprintf (f, "EV_KEY raw=0x%02x %s\n", code & 0x7f,
	         (code & 0x80) ? "up" : "down");
	fclose (f);
	g_dbg_keys++;
}

/* Auto-repeat for InputKeybGetLastKey, in game-clock milliseconds. The SDL
 * layer reads these from ffelnxsdl.cfg ([KEYBOARD] repeatdelay/repeatrate);
 * these are its defaults, and we have no config file yet. */
#define KEY_REPEAT_DELAY 300
#define KEY_REPEAT_RATE  30

/* Held state of every Amiga RAW key. IDCMP_RAWKEY codes for real keys occupy
 * 0x00..0x7F -- bit 7 is the release marker, not part of the code -- so 128
 * entries cover the whole keyboard matrix. */
static UCHAR g_held[128];

/* Pointer position in GAME coordinates. amiga_gfx.c gives us a borderless
 * backdrop window that covers exactly the game area, so IDCMP window-relative
 * coordinates already ARE game coordinates and no scaling is needed (the SDL
 * layer has to divide by the window size because it can be stretched). */
static long g_mouse_x, g_mouse_y;
static int  g_have_pos;			/* see the note in pump_pos() */
static long g_mick_x, g_mick_y;
static long g_buttons;

/* DOS-scancode-indexed repeat machine, exactly the SDL layer's:
 *   0x80 = key is up,
 *   0x00 = held, initial press already reported, repeat not started,
 *   0xff = held and repeating.
 * g_keyb_time[i] is the game-clock time at which the next event is due. */
static UCHAR g_keyb_state[256];
static ULONG g_keyb_time[256];
static long  g_last_key = 0xff;

void ffe_input_init (void)
{
	memset (g_held, 0, sizeof (g_held));
	memset (g_keyb_state, 0x80, sizeof (g_keyb_state));
	memset (g_keyb_time, 0, sizeof (g_keyb_time));
	g_last_key = 0xff;

	g_mouse_x = FFE_SCREEN_W / 2;
	g_mouse_y = FFE_SCREEN_H / 2;
	g_have_pos = 0;
	g_mick_x = g_mick_y = 0;
	g_buttons = 0;
}

void ffe_input_cleanup (void)
{
}

/* Take the pointer position out of an event and turn the movement into
 * mickeys. The first positional event of the session only seeds the position:
 * before it we do not know where the pointer is, and the difference against a
 * made-up starting point would be a mickey burst of up to 320 units -- in
 * flight that is a full-deflection stick input on frame one. */
static void pump_pos (int x, int y)
{
	int gh = amigagfx_game_height ();

	if (x < 0) x = 0;
	if (y < 0) y = 0;
	if (x > FFE_SCREEN_W - 1) x = FFE_SCREEN_W - 1;
	if (gh > 0 && y > gh - 1) y = gh - 1;

	if (g_have_pos) {
		g_mick_x += (long)x - g_mouse_x;
		g_mick_y += (long)y - g_mouse_y;
	}
	g_have_pos = 1;
	g_mouse_x = x;
	g_mouse_y = y;
}

/* FIRST-LIGHT DEBUG: file-driven key injection. Focus-based host injection
 * (SendInput/PostMessage) proved unreliable and unsafe around the splash, so
 * the harness drops a file instead: PROGDIR:ffe_autokey holding one decimal
 * Amiga RAW code. The pump polls it every 32nd call, presses the key for
 * ~12 pumps and releases. PROGDIR is a host directory, so the host writes
 * the file and this side deletes it -- no focus, no synthetic events. */
static void autokey_poll (void)
{
	static int poll_n, down_raw = -1, down_mod = -1, down_left;
	FILE *f;
	int raw, hold, mod;

	if (down_left > 0 && --down_left == 0 && down_raw >= 0) {
		g_held[down_raw & 0x7f] = 0;
		down_raw = -1;
		if (down_mod >= 0) g_held[down_mod & 0x7f] = 0;
		down_mod = -1;
	}
	if (++poll_n < 32) return;
	poll_n = 0;
	f = fopen (ffe_pd ("ffe_autokey"), "r");
	if (f == NULL) return;
	raw = -1;
	hold = 12;
	mod = -1;
	/* optional 2nd number: pumps to hold (~300/s); optional 3rd: a modifier
	 * RAW key held together with it (0x60 = left Shift: Shift+F1..F5 are the
	 * time-acceleration icons, 2026-10-16) */
	fscanf (f, "%d %d %d", &raw, &hold, &mod);
	fclose (f);
	remove (ffe_pd ("ffe_autokey"));
	if (raw < 0 || raw > 0x7f) return;
	dbg_key (raw);
	if (mod >= 0 && mod <= 0x7f) { g_held[mod] = 1; down_mod = mod; }
	g_held[raw] = 1;
	down_raw = raw;
	down_left = hold > 0 ? hold : 12;
}

/* FIRST-LIGHT DEBUG: file-driven MOUSE injection, the autokey pattern for
 * clicks. PROGDIR:ffe_automouse holds "x y" (a target in game screen
 * coordinates, 320x200) with an optional third number: 1 = left-click when
 * the target is reached (the default), 0 = just move. The pump reads the
 * GAME'S OWN pointer position (ptrSavedX/Y in the translated support data)
 * and feeds bounded mickey deltas until the pointer arrives, then presses
 * the left button for ~8 pumps. Closed-loop, so it lands exactly even
 * though the game applies its own acceleration to the deltas. */
extern long ffe_ptr_x __asm__("ptrSavedX");
extern long ffe_ptr_y __asm__("ptrSavedY");

/* the GAME's own pointer position, packed x | y<<16, stored by FlipScreen
 * from GuiProcessInput's return every frame -- the ground truth to chase
 * (MouseReadPos is a DELTA the game integrates, so absolute targeting must
 * close the loop on the game's integrated state, not on g_mouse) */
/* DATA_009208 is X, DATA_009209 is Y -- the disassembly is explicit:
 * FUNC_001632_UpdatePtr stores GuiProcessInput's packed return (x | y<<16)
 * as a dword, and the game's own readers do movzx word [DATA_009208] ->
 * ptrSavedX / [DATA_009209] -> ptrSavedY, with init mov word [DATA_009208],
 * 0xa0 (=160, screen center X). ffe.asm:97575-97578, 85710. The be16-dword
 * store machinery half-swaps so each word lands at its own label. */
extern unsigned short ffe_gui_x __asm__("DATA_009208");
extern unsigned short ffe_gui_y __asm__("DATA_009209");

static int am_poll_n, am_click_left, am_chase_left;
static long am_tgt_x, am_tgt_y, am_want_click;
#define poll_n am_poll_n
#define click_left am_click_left
#define chase_left am_chase_left
#define tgt_x am_tgt_x
#define tgt_y am_tgt_y
#define want_click am_want_click
static void automouse_poll (void)
{
	FILE *f;

	if (chase_left > 0) {
		long cx = (long)ffe_gui_x;
		long cy = (long)ffe_gui_y;
		long dx = tgt_x - cx, dy = tgt_y - cy;

		chase_left--;
		if (dx >= -1 && dx <= 1 && dy >= -1 && dy <= 1) {
			chase_left = 0;
			if (want_click) {
				g_buttons |= 1;
				click_left = 24;
				{
					/* HOTRECTS (2026-10-09): the GUI hot-area list as it
					 * stands when the injected click lands -- rows of five
					 * dwords (x1,y1,x2,y2,action) up to the -1 terminator
					 * (FUNC_001432's walker, ffe.asm:86069-86086). */
					extern long ffe_hot_list[] __asm__("DATA_007683");
					FILE *hf = ffe_logopen (ffe_pd ("ffe_hotrects.log"), "w");
					int r;
					if (hf != NULL) {
						fprintf (hf, "CLICK at gui=%ld,%ld%c", cx, cy, 10);
						for (r = 0; r < 100 && ffe_hot_list[5 * r] != -1; r++)
							fprintf (hf, "R %ld %ld %ld %ld act=%lx%c", ffe_hot_list[5 * r],
							         ffe_hot_list[5 * r + 1], ffe_hot_list[5 * r + 2],
							         ffe_hot_list[5 * r + 3], ffe_hot_list[5 * r + 4], 10);
						fclose (hf);
					}
				}
			}
			return;
		}
		if (chase_left == 0) {
			/* expired without converging: say where the game's pointer
			 * actually was -- ground truth for the storage plumbing */
			FILE *d = ffe_logopen (ffe_pd ("ffe_game.log"), "a");
			if (d) {
				fprintf (d, "CHASE expired at %ld,%ld tgt %ld,%ld\n",
				         cx, cy, tgt_x, tgt_y);
				fclose (d);
			}
			return;
		}
		/* one full-delta correction per ~8 pumps: the game integrates the
		 * delta once per FRAME (~6 pumps), so pushing every pump would
		 * overshoot and oscillate */
		if ((chase_left & 7) == 0) {
			g_mouse_x += dx;
			g_mouse_y += dy;
			/* the game's non-exclusive path does NOT clamp -- keep the
			 * pointer we hand it inside the screen ourselves */
			if (g_mouse_x < 0) g_mouse_x = 0;
			if (g_mouse_x > 319) g_mouse_x = 319;
			if (g_mouse_y < 0) g_mouse_y = 0;
			if (g_mouse_y > 199) g_mouse_y = 199;
		}
		return;
	}
	if (click_left > 0) {
		if (--click_left == 0)
			g_buttons &= ~1L;
		return;
	}
	if (++poll_n < 32) return;
	poll_n = 0;
	f = fopen (ffe_pd ("ffe_automouse"), "r");
	if (f == NULL) return;
	tgt_x = tgt_y = -1;
	want_click = 1;
	fscanf (f, "%ld %ld %ld", &tgt_x, &tgt_y, &want_click);
	fclose (f);
	remove (ffe_pd ("ffe_automouse"));
	if (tgt_x == 999) {
		/* magic: dump the LIVE hot-rect table (DATA_007683, 129 entries
		 * of 5 longs x,y,w,h,action; first x == -1 is the sentinel) so
		 * the harness can aim at REAL click targets instead of guessing */
		extern long ffe_hotrects[] __asm__("DATA_007683");
		FILE *d = ffe_logopen (ffe_pd ("ffe_hotareas.log"), "w");
		if (d) {
			int i;
			for (i = 0; i < 129; i++) {
				long *e = ffe_hotrects + i * 5;
				if (e[0] == -1) break;
				fprintf (d, "%d: x=%ld y=%ld w=%ld h=%ld act=%02lx\n",
				         i, e[0], e[1], e[2], e[3],
				         (unsigned long)e[4]);
			}
			fclose (d);
		}
		return;
	}
	if (tgt_x < 0 || tgt_y < 0 || tgt_x > 319 || tgt_y > 199) return;
	chase_left = 240;    /* up to ~40 frames of closed-loop convergence */
}

#undef poll_n
#undef click_left
#undef chase_left
#undef tgt_x
#undef tgt_y
#undef want_click

/* CLOCK-DRIVEN INPUT (2026-10-16): PROGDIR:ffe_clockkeys holds lines
 *     <clock> K <dos scancode> <hold ticks> [<modifier scancode>]
 *     <clock> M <x> <y>
 * in the order they fire. An event fires on the first pump whose UNSIGNED
 * game clock (DATA_008804) is >= <clock>; a key is held until the clock has
 * advanced by <hold ticks>. The twin on the PC is FFE_CLOCKKEYS in
 * build\pcdump_video.cpp, same file format, so both machines receive the
 * same input at the same GAME time -- the real-time harnesses drift apart
 * because the emulated 68020 runs fewer game ticks per second. */
extern unsigned long ck_clock[2] __asm__("DATA_008804");
#define CK_MAX 64
static unsigned long ck_t[CK_MAX], ck_hold[CK_MAX];
static long ck_a[CK_MAX], ck_b[CK_MAX], ck_c[CK_MAX];
static char ck_kind[CK_MAX];
static int ck_n = -1, ck_i, ck_down_raw = -1, ck_down_mod = -1;
static unsigned long ck_release;
static int ck_mouse_down;
static void clockkeys_poll (void)
{
	unsigned long now = ck_clock[0];

	if (ck_n < 0) {
		FILE *f = fopen (ffe_pd ("ffe_clockkeys"), "r");
		char line[96];
		ck_n = 0;
		if (f == NULL) return;
		while (ck_n < CK_MAX && fgets (line, sizeof line, f) != NULL) {
			unsigned long t, h = 0; char k; long a = 0, b = 0, c = -1;
			if (sscanf (line, "%lu %c %ld %ld %ld", &t, &k, &a, &b, &c) < 3) continue;
			if (k == 'K') h = (unsigned long)b;
			ck_t[ck_n] = t; ck_kind[ck_n] = k; ck_a[ck_n] = a; ck_b[ck_n] = b;
			ck_c[ck_n] = c; ck_hold[ck_n] = h; ck_n++;
		}
		fclose (f);
	}
	if (ck_mouse_down && now >= ck_release) { g_buttons &= ~1L; ck_mouse_down = 0; }
	if (ck_down_raw >= 0 && now >= ck_release) {
		g_held[ck_down_raw & 0x7f] = 0; ck_down_raw = -1;
		if (ck_down_mod >= 0) { g_held[ck_down_mod & 0x7f] = 0; ck_down_mod = -1; }
	}
	if (ck_i >= ck_n || ck_down_raw >= 0 || ck_mouse_down || am_chase_left > 0 || am_click_left > 0) return;
	/* +2 frames (2 x 994 ticks): the PC twin posts window messages that the
	 * game sees 2 frames later than our direct key state (measured 2026-10-16
	 * with FFE_SEQWIN / ffe_seqwin around the key); match it. */
	if (now < ck_t[ck_i] + 994) return;	/* clicks: 1 frame (RNGWIN, 2026-10-16) */
	if (ck_kind[ck_i] == 'K') {
		int raw = ffe_scancode_to_rawkey[ck_a[ck_i] & 0xff];
		if (raw != FFE_RAWKEY_NONE) {
			if (ck_c[ck_i] >= 0) {
				int m = ffe_scancode_to_rawkey[ck_c[ck_i] & 0xff];
				if (m != FFE_RAWKEY_NONE) { g_held[m & 0x7f] = 1; ck_down_mod = m; }
			}
			dbg_key (raw);
			g_held[raw & 0x7f] = 1; ck_down_raw = raw;
			ck_release = now + (ck_hold[ck_i] ? ck_hold[ck_i] : 200);
		}
	} else if (ck_kind[ck_i] == 'M') {
		/* direct, no chase: the non-exclusive GUI path takes InputMouseReadPos
		 * verbatim, and the PC twin posts move + button-down together; the
		 * chase cost ~3 frames and shifted every later RNG draw (measured
		 * 2026-10-16, RNGWIN) */
		g_mouse_x = ck_a[ck_i]; g_mouse_y = ck_b[ck_i]; g_have_pos = 1;
		g_buttons |= 1; ck_mouse_down = 1; ck_release = now + 400;
	}
	ck_i++;
}

void ffe_pump_input (void)
{
	AmigaGfxEvent ev;

	autokey_poll ();
	automouse_poll ();
	clockkeys_poll ();

	while (amigagfx_poll (&ev)) switch (ev.type)
	{
	case AMIGAGFX_EV_MOUSEMOVE:
		pump_pos (ev.x, ev.y);
		break;

	case AMIGAGFX_EV_MOUSEDOWN:
		pump_pos (ev.x, ev.y);
		if (ev.code == AMIGAGFX_BUTTON_LEFT)  g_buttons |= 1;
		if (ev.code == AMIGAGFX_BUTTON_RIGHT) g_buttons |= 2;
		break;

	case AMIGAGFX_EV_MOUSEUP:
		pump_pos (ev.x, ev.y);
		if (ev.code == AMIGAGFX_BUTTON_LEFT)  g_buttons &= ~1L;
		if (ev.code == AMIGAGFX_BUTTON_RIGHT) g_buttons &= ~2L;
		break;

	case AMIGAGFX_EV_KEY:
		/* Every raw code arrives, releases included: bit 7 set means release,
		 * the low 7 bits are the key. */
		dbg_key (ev.code);
		{
			/* Test machines only: PROGDIR:ffe_nokeys drops the keyboard. WinUAE
			 * reads raw input even in the BACKGROUND, so an unattended run once
			 * played the developer's typing from another window (2026-10-16). */
			static int nokeys = -1;
			if (nokeys < 0) {
				FILE *nk = ffe_dev_mode () ? fopen (ffe_pd ("ffe_nokeys"), "r") : NULL;
				nokeys = nk != NULL;
				if (nk != NULL) fclose (nk);
			}
			if (nokeys) break;
		}
		g_held[ev.code & 0x7f] = (UCHAR)((ev.code & 0x80) ? 0 : 1);
		break;

	default:
		/* AMIGAGFX_EV_QUIT among others. The backdrop window carries no close
		 * gadget, so amiga_gfx.c never raises it for us; quitting is the
		 * game's own business (Esc / its menu), never the platform layer's. */
		break;
	}
}

extern void ffe_dbg_trace (const char *tag);	/* FIRST-LIGHT DEBUG */

/* FIRST-LIGHT DEBUG: expose the platform-side held state so the clock dump
 * can tell "the event never updated g_held" from "ReadStates never saw it". */
int ffe_dbg_held (int raw)
{
	return g_held[raw & 0x7f];
}

long InputMouseReadButtons (void)
{
	ffe_dbg_trace ("MouseReadButtons");
	ffe_pump_input ();
	return g_buttons;
}

void InputMouseReadMickeys (long *pXMick, long *pYMick)
{
	ffe_dbg_trace ("MouseReadMickeys");
	ffe_pump_input ();
	*pXMick = g_mick_x;
	*pYMick = g_mick_y;
	g_mick_x = g_mick_y = 0;
}

void InputMouseReadPos (long *pXPos, long *pYPos)
{
	/* ABSOLUTE position. GuiProcessInput has two consumers of this value
	 * (ffe.asm:86000-86051): the ptrExclusive==0 path -- ours, since
	 * VideoPointerExclusive() returns 0 -- takes it VERBATIM as the pointer
	 * position (`mov [esi],ax`, no integration, no clamp), while only the
	 * exclusive path integrates it as a delta over DATA_009208/9209. The
	 * earlier delta semantics here fed deltas into the verbatim path: the
	 * game's pointer sat at (0,0) every frame the host mouse did not move,
	 * and every automouse click resolved at (0,0) into action ff. */
	ffe_pump_input ();
	*pXPos = g_mouse_x;
	*pYPos = g_mouse_y;
}

/* Is the DOS scancode's key held? FFE_RAWKEY_NONE means the PC key has no
 * Amiga counterpart at all (Num Lock, Page Up, right Ctrl...) or the scancode
 * slot is unused; either way it can never be held. */
static int scancode_held (int scancode)
{
	unsigned char raw = ffe_scancode_to_rawkey[scancode];

	if (raw == FFE_RAWKEY_NONE) return 0;
	return g_held[raw & 0x7f] != 0;
}

void InputKeybReadStates (UCHAR *pKeys)
{
	int i;
	ULONG curtime;

	ffe_dbg_trace ("KeybReadStates");
	ffe_pump_input ();
	curtime = TimerGetTimeStamp ();

	for (i = 0; i < 256; i++)
	{
		if (scancode_held (i))
		{
			if (g_keyb_state[i] == 0x80) {			/* Initial press */
				g_last_key = i;
				g_keyb_state[i] = 0;
				g_keyb_time[i] = curtime + KEY_REPEAT_DELAY;
			}
			else if (g_keyb_state[i] == 0) {		/* hold */
				if (g_keyb_time[i] < curtime) {
					g_keyb_state[i] = 0xff;
				}
			}
			if (g_keyb_state[i] == 0xff) {			/* Repeat */
				if (g_keyb_time[i] < curtime) {
					g_last_key = i;
					g_keyb_time[i] += KEY_REPEAT_RATE;
				}
			}
		}
		else g_keyb_state[i] = 0x80;
	}

	/* INVERTED polarity, and it is not a typo: 0x80 means "not held". */
	for (i = 0; i < 256; i++)
		pKeys[i] = (UCHAR)(g_keyb_state[i] == 0x80 ? 0x80 : 0x00);

	/* THE MOUSE BUTTONS ARE PSEUDO-SCANCODES. GuiReadAllInput derives the
	 * click state from THIS array -- [0x7e] feeds DATA_007708 (left) and
	 * [0x7d] feeds DATA_007711 (right); InputMouseReadButtons is not the
	 * game's click source at all. Same inverted polarity as the keys.
	 * Missing this overlay left every GUI click dead (found 2026-07-31 by
	 * clicking exact hot-rect centres to zero effect). */
	pKeys[0x7e] = (UCHAR)((g_buttons & 1) ? 0x00 : 0x80);
	pKeys[0x7d] = (UCHAR)((g_buttons & 2) ? 0x00 : 0x80);
}

long InputKeybGetLastKey (void)
{
	ffe_dbg_trace ("KeybGetLastKey");
	return g_last_key;
}

void InputKeybSetLastKey (long key)
{
	g_last_key = key;
}

void InputJoyReadPos (long *pXPos, long *pYPos)
{
	ffe_dbg_trace ("JoyReadPos");
	/* Stubbed, exactly as in ref/jjffe/sdl/sdlinput.cpp: FFE reads the
	 * joystick only when the player selected one in its own options, and the
	 * reference layer never gives it one. Amiga joystick/CD32 pad support, if
	 * it ever happens, replaces this function and nothing else. */
	*pXPos = 0;
	*pYPos = 0;
}

long InputJoyReadButtons (void)
{
	/* Stubbed, see InputJoyReadPos. */
	return 0;
}
