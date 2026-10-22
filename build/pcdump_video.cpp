/* SPDX-License-Identifier: MIT
 *
 * PC-REFERENCE FRAME DUMP -- the missing half of the Amiga/PC comparison.
 *
 * The Amiga build writes winuae\work\ffe_frame.raw in the FFEFRM01 format
 * (8-byte magic, 256*3 palette bytes, then 320*200 colour indices), and
 * build\framediff.py compares two of those. Until now there was no way to get
 * the same file out of the PC reference, so "compare the viewport against the
 * PC reference frame by frame" had no left-hand side.
 *
 * WHY A WRAPPER AND NOT AN EDIT: ref\jjffe is read-only by project rule, and
 * build-pc-ref.py must keep producing the unmodified executable the developer
 * plays. So this file #includes the stock sdlvideo.cpp under a renamed
 * VideoBlit and re-exports its own, exactly the way build-oracle.py wraps
 * ffe.asm to export a few extra symbols without touching it.
 *
 * WHERE THE HOOK GOES: ref\jjffe\sdl\sdlvideo.cpp:351-355 -- VideoBlit copies
 * into the static `pBackBuf` and, when `w == 320`, that is the one flip per
 * frame. So the whole frame is in pBackBuf at that moment, and because the
 * stock file is #included here its file-statics are visible to this code.
 *
 * TO BUILD IT: copy build\build-pc-ref.py to build\build-pc-dump.py, drop
 * r"sdl\sdlvideo.cpp" from CXX, add this file, and send the output to
 * build\m0-pc-dump\. Do not change build-pc-ref.py itself.
 *
 * FFE_DUMP_EVERY presents are skipped between dumps, matching the Amiga side's
 * behaviour of overwriting one file so that whenever the run is stopped the
 * file holds a recent frame.
 *
 * BEFORE READING ANYTHING INTO A DIFF: the Amiga build must have
 * FFE_LEVEL_CLOCKS = 0 (it now does). While that shortcut was on, the two
 * builds ran at different simulated times and no frame could match.
 */

#include <stdio.h>
#include <stdlib.h>

#define VideoBlit VideoBlit_stock
#include "sdl/sdlvideo.cpp"
#undef VideoBlit

#define FFE_DUMP_EVERY 128

#ifdef FFE_PROBE
extern "C" unsigned char DATA_008804[] __asm__("DATA_008804");
extern "C" void pcwc_tick (long clk);
extern "C" void pcf8_present (long clk);
#endif

#ifdef FFE_PROBE
/* CLOCK-DRIVEN INPUT (2026-10-16), twin of PROGDIR:ffe_clockkeys in
 * amiga\ffeapi\ffe_input.c -- same file format, path in env FFE_CLOCKKEYS:
 *     <clock> K <dos scancode> <hold ticks> [<modifier scancode>]
 *     <clock> M <x> <y>
 * Fires on the first frame flip whose unsigned game clock is >= <clock>, and
 * POSTS the messages to our own SDL window -- the same path the harness uses
 * (SDL 1.2 only updates its mouse state from window messages). */
#include <windows.h>
#include <SDL_syswm.h>
static void clockkeys_tick (unsigned long now)
{
	static int n = -1, i, down_vk = -1, down_sc, mod_vk = -1, mouse_down;
	static unsigned long t[64], hold[64], release;
	static long a[64], b[64], c[64];
	static char kind[64];
	static HWND hwnd;
	if (n < 0) {
		const char *path = getenv ("FFE_CLOCKKEYS");
		FILE *f = path ? fopen (path, "r") : 0;
		char line[96];
		SDL_SysWMinfo wi;
		n = 0;
		SDL_VERSION (&wi.version);
		if (SDL_GetWMInfo (&wi) > 0) hwnd = wi.window;
		if (!f) return;
		while (n < 64 && fgets (line, sizeof line, f)) {
			unsigned long tt; char k; long aa = 0, bb = 0, cc = -1;
			if (sscanf (line, "%lu %c %ld %ld %ld", &tt, &k, &aa, &bb, &cc) < 3) continue;
			t[n] = tt; kind[n] = k; a[n] = aa; b[n] = bb; c[n] = cc; hold[n] = k == 'K' ? (unsigned long)bb : 0; n++;
		}
		fclose (f);
	}
	if (!hwnd) return;
	if ((down_vk >= 0 || mouse_down) && now >= release) {
		if (down_vk >= 0) PostMessage (hwnd, WM_KEYUP, down_vk, 1 | (down_sc << 16) | 0xC0000000);
		if (mod_vk >= 0) PostMessage (hwnd, WM_KEYUP, mod_vk, 1 | (0x2A << 16) | 0xC0000000);
		if (mouse_down) PostMessage (hwnd, WM_LBUTTONUP, 0, mouse_down);
		down_vk = mod_vk = -1; mouse_down = 0;
		return;
	}
	if (down_vk >= 0 || mouse_down || i >= n || now < t[i]) return;
	if (kind[i] == 'K') {
		down_sc = (int)(a[i] & 0xff);
		down_vk = (int)MapVirtualKey (down_sc, 1 /* MAPVK_VSC_TO_VK */);
		if (c[i] >= 0) { mod_vk = 0xA0; PostMessage (hwnd, WM_KEYDOWN, mod_vk, 1 | (0x2A << 16)); }
		PostMessage (hwnd, WM_KEYDOWN, down_vk, 1 | (down_sc << 16));
		release = now + (hold[i] ? hold[i] : 200);
	} else if (kind[i] == 'M') {
		RECT r; GetClientRect (hwnd, &r);
		long x = a[i] * (r.right - r.left) / 320, y = b[i] * (r.bottom - r.top) / 200;
		mouse_down = (int)((y << 16) | x);
		PostMessage (hwnd, WM_MOUSEMOVE, 0, mouse_down);
		PostMessage (hwnd, WM_MOUSEMOVE, 0, mouse_down);
		PostMessage (hwnd, WM_LBUTTONDOWN, 1, mouse_down);
		release = now + 400;
	}
	i++;
}
#endif

static void ffe_dump_frame (void)
{
	FILE *f = fopen ("ffe_frame.raw", "wb");
	UCHAR pal[3];
	int i;

	if (f == 0) return;
	fwrite ("FFEFRM01", 1, 8, f);
	for (i = 0; i < 256; i++) {
		VideoGetPalValue (i, pal);
		fwrite (pal, 1, 3, f);
	}
	fwrite (pBackBuf, 1, 320 * 200, f);
	fclose (f);
}

extern "C" void VideoBlit (UCHAR *pData, long x, long y, long w, long h, long jump)
{
	static unsigned long presents;

	VideoBlit_stock (pData, x, y, w, h, jump);
	/* w == 320 is the stock file's own test for "this was the frame flip"
	 * (sdlvideo.cpp:355), so the back buffer is complete right here. */
	if (w != 320) return;
#ifdef FFE_PROBE
	clockkeys_tick (*(const unsigned long *)DATA_008804);
	pcwc_tick (*(const long *)DATA_008804);
	pcf8_present (*(const long *)DATA_008804);
#endif
	if ((++presents % FFE_DUMP_EVERY) == 1) ffe_dump_frame ();
	/* SEQUENCE MODE (2026-09-25): FFE_DUMP_SEQ=<n> additionally writes every
	 * n-th present to seqNNNNN.raw (NNNNN = present number), so the intro can
	 * be compared against the Amiga scene by scene instead of from one frame. */
	{
		static int seq = -1;
		if (seq < 0) { const char *e = getenv ("FFE_DUMP_SEQ"); seq = e ? atoi (e) : 0; }
		/* CLOCK WINDOW (2026-10-16): FFE_SEQWIN="lo hi" also writes EVERY present
		 * whose unsigned game clock is in [lo,hi] -- twin of PROGDIR:ffe_seqwin. */
		static int win_read; static unsigned long win_lo, win_hi;
		int in_win = 0;
		if (!win_read) { const char *w = getenv ("FFE_SEQWIN"); win_read = 1; if (w) sscanf (w, "%lu %lu", &win_lo, &win_hi); }
#ifdef FFE_PROBE
		in_win = win_hi > 0 && (unsigned long)*(const long *)DATA_008804 >= win_lo && (unsigned long)*(const long *)DATA_008804 <= win_hi;
#endif
		if ((seq > 0 && (presents % seq) == 0 && presents <= 60000) || in_win) {
			char name[64];
			FILE *f;
			UCHAR pal[3];
			int i;
			snprintf (name, sizeof name, "seq/f%05lu.raw", presents);
			f = fopen (name, "wb");
			if (f) {
				fwrite ("FFEFRM01", 1, 8, f);
				for (i = 0; i < 256; i++) { VideoGetPalValue (i, pal); fwrite (pal, 1, 3, f); }
				fwrite (pBackBuf, 1, 320 * 200, f);
				fclose (f);
			}
#ifdef FFE_PROBE
			/* frame -> game clock, so the two machines can be compared at
			 * EQUAL CLOCK rather than equal frame number (probe build only:
			 * DATA_008804 is exported there). */
			{
				FILE *ix = fopen ("seq/index.txt", "a");
				if (ix) {
					fprintf (ix, "f%05lu %ld\n", presents, *(const long *)DATA_008804);
					fclose (ix);
				}
			}
#endif
		}
	}
}
