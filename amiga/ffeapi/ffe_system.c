/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 angree. See LICENSE at the repository root.
 *
 * SystemInit / SystemCleanup: the two calls the game makes around everything
 * else. SystemInit opens the 320x200 8bpp screen and brings the timer, the
 * video and the input parts up; SystemCleanup takes them down again.
 *
 * ORDER MATTERS in two places:
 *
 *  - ffe_video_init() caches the chunky buffer pointer, so it has to run after
 *    the screen is open, and ffe_timer_init() samples the millisecond clock, so
 *    it runs before the first frame but after the screen (opening a screen
 *    takes tens of milliseconds and would otherwise land inside the first
 *    frame's budget).
 *
 *  - **ffe_dir_init() must run before ffe_sound_init().** The sample loader
 *    builds its paths with DirMakeSampleName(), which is empty until the
 *    directory layer has read [DIR] SamplePath from the config; get this the
 *    wrong way round and every sample is looked for in the current directory
 *    and the game comes up silent, with nothing in the log saying why.
 *
 * ref/jjffe/sdl/sdlvideo.cpp uses Timer, Dir, Video, Input, Sound and we now
 * match it exactly.
 *
 * FAILURE: SystemInit returns void -- that is the game's signature, not a
 * choice -- and the SDL layer answers a failed video init with exit(0). We do
 * not exit from inside the platform layer: on Amiga the caller may still need
 * to close things down cleanly, and a test program needs to be able to write
 * WHY it failed to its log. Instead the layer stays down, ffe_system_ready()
 * reports 0 and every video/input entry point is a safe no-op.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ffeapi.h"
#include "amiga_gfx.h"

#define FFE_BAR_TITLE "AmiFFE 0.2.0"

/* Version string for the Version command, and the credits in plain text in
 * the binary. Frontier: First Encounters (c) Frontier Developments; this port
 * is an unofficial fan project based on John Jordan's JJFFE. */
const char ffe_version_tag[] =
	"$VER: AmiFFE 0.2.0 (30.10.2026) Frontier: First Encounters "
	"(c) Frontier Developments - Amiga port based on JJFFE by John Jordan";

static int g_ready;
int ffe_bar_on;		/* the screen has Intuition's title bar (ffe_video.c shows FPS there) */

extern void ffe_boot_mark (const char *tag);

void SystemCleanup (void);

extern void ffe_find_data_dir (void);

/* PREFS (2026-10-16, developer: "a small config like the ReMoM one"): the
 * display choice lives in PROGDIR:amiffe.cfg, written by the ffe-prefs tool
 * (amiga/tools/ffe-prefs.c) or by hand, one key=value per line:
 *   gfx=0|1|2    0 auto (= AGA), 1 AGA, 2 RTG (CyberGraphX/Picasso96 8-bit)
 *   video=0|1|2  0 as the machine boots, 1 PAL, 2 NTSC (chipset screen only)
 * A missing file, an unknown key or an out-of-range value keeps the default.
 * RTG falls back to AGA on its own when no 8-bit mode is available. */
#define FFE_PREFS_FILE ffe_pd ("amiffe.cfg")
static int g_pref_gfx, g_pref_video;
/*   tex=0|1|2    0 textures as the original, 1 distant (minified) textured
 *                polygons drawn flat, 2 every opaque textured polygon flat
 *                (2026-10-24, for slower machines; read by the texture span
 *                code that tools/perf/patch_speed.py emits, and by
 *                ffe_tex_flat_index below) */
short ffe_tex_mode;
/*   planetlod=0|1  0 planets as the original, 1 reduced: patches smaller
 *                  than ~75 px on screen stop subdividing early (the splitter
 *                  threshold below; tools/perf/patch_speed.py) */
long ffe_planet_lod = 0;	/* default: reduced (developer, 2026-10-24) */
/*   sound=0|1    0 off (the game runs silent), 1 Paula (audio.device)
 *   music=0..5   0 off, 1 AdLib pre-rendered, 2 General MIDI pre-rendered
 *                (PROGDIR:Music/AdLib|GM/<song>.wav, written by ffe-music),
 *                3 AdLib live, 4 General MIDI live (synthesised while the
 *                game runs: fast machines only), 5 external MIDI (camd)
 *   musicrate=11025|22050   for the live modes
 * Read by ffe_sound.c / ffe_music.c (2026-10-24). */
short ffe_sound_mode = 1, ffe_music_mode = 1;
long  ffe_music_rate = 11025;

/* The texel index that stands for a whole 64x64 texture when it is drawn flat
 * (tex=1/2): its most frequent non-zero index, which the caller then passes
 * through the quad's own colour table, so the polygon keeps its lighting.
 * A TRANSPARENT texture (trans != 0) with more than 1/16 of its texels in
 * colour 0 has holes that shape the object: -1 = keep it textured.
 * Textures can be regenerated in place (planets), so a cached result is only
 * reused when 16 sample bytes still match. Called once per span; the cache
 * makes repeated spans of one polygon cost a lookup. */
long ffe_tex_flat_index (const unsigned char *tex, long trans)
{
	static struct { const unsigned char *p; long trans, res; unsigned char probe[16]; } c[32];
	unsigned long h = ((unsigned long)tex >> 6) & 31;
	unsigned short n[256];
	long i, best = -1, bestn = 0;

	if (c[h].p == tex && c[h].trans == trans) {
		for (i = 0; i < 16; i++) if (c[h].probe[i] != tex[i * 256 + 37]) break;
		if (i == 16) return c[h].res;
	}
	memset (n, 0, sizeof n);
	for (i = 0; i < 4096; i++) n[tex[i]]++;
	for (i = 1; i < 256; i++) if (n[i] > bestn) { bestn = n[i]; best = i; }
	if (best < 0 || (trans && n[0] > 4096 / 16)) best = -1;
	else if (!trans && n[0] > bestn) best = 0;	/* opaque: 0 is an ordinary colour */
	c[h].p = tex; c[h].trans = trans; c[h].res = best;
	for (i = 0; i < 16; i++) c[h].probe[i] = tex[i * 256 + 37];
	return best;
}

static void ffe_read_prefs (void)
{
	char line[80];
	int v;
	FILE *f = fopen (FFE_PREFS_FILE, "r");

	if (f == NULL) return;
	while (fgets (line, sizeof line, f) != NULL) {
		if (sscanf (line, "gfx=%d", &v) == 1) { if (v >= 0 && v <= 2) g_pref_gfx = v; }
		else if (sscanf (line, "video=%d", &v) == 1) { if (v >= 0 && v <= 2) g_pref_video = v; }
		else if (sscanf (line, "tex=%d", &v) == 1) { if (v >= 0 && v <= 2) ffe_tex_mode = (short)v; }
		else if (sscanf (line, "planetlod=%d", &v) == 1) ffe_planet_lod = v ? 0 : 8;
		else if (sscanf (line, "sound=%d", &v) == 1) { if (v >= 0 && v <= 2) ffe_sound_mode = (short)v; }
		else if (sscanf (line, "music=%d", &v) == 1) { if (v >= 0 && v <= 5) ffe_music_mode = (short)v; }
		else if (sscanf (line, "musicrate=%d", &v) == 1) { if (v == 11025 || v == 22050) ffe_music_rate = v; }
	}
	fclose (f);
	fprintf (stdout, "ffe: prefs gfx=%d video=%d tex=%d\n", g_pref_gfx, g_pref_video, ffe_tex_mode);
	fflush (stdout);
}

void SystemInit (void)
{
	ffe_find_data_dir ();	/* the original DOS files, as copied (README) */
	if (g_ready) return;			/* safe to call once; a second call is a no-op */
	ffe_boot_mark ("SystemInit begin");

	/* 320x200, no title bar, AGA chunky-to-planar. FFE has exactly one screen
	 * size and never changes it. The backend is the AGA c2p path measured by
	 * amiga/probes/ffe_agatest.c; RTG and EHB exist in amiga_gfx.c and become
	 * a runtime option later, not here. */
	/* SCREEN BAR (2026-10-02, developer: "every port of ours has the screen
	 * bar"). Intuition's own title bar sits ABOVE the 320x200 game area, so the
	 * screen is opened taller by the bar height. That height depends on the
	 * screen font and is only known after the open, so: guess 11 lines
	 * (Topaz 8), and if the game area did not come out exactly 200 lines,
	 * reopen with the measured bar. Pens: FFE's palette 0..15 is a fixed grey
	 * ramp (0 black .. 15 near white), so text 15, bar 5, gadget fill 8.
	 * PROGDIR:ffe_nobar opens the old bar-less 320x200 screen. */
	ffe_read_prefs ();
	amigagfx_set_video_mode (g_pref_video);
	{
		FILE *nb = fopen (ffe_pd ("ffe_nobar"), "r");
		int bar = nb == NULL;
		int h = FFE_SCREEN_H + (bar ? 11 : 0);
		int be = g_pref_gfx == 2 ? AMIGAGFX_BACKEND_RTG : AMIGAGFX_BACKEND_AGA;

		if (nb != NULL) fclose (nb);
		amigagfx_set_title (FFE_BAR_TITLE);
		amigagfx_set_bar_pens (15, 5, 8);
		if (amigagfx_open (FFE_SCREEN_W, h, bar, be) != 0) {
			fprintf (stdout, "ffe: amigagfx_open(%d,%d) refused - platform layer stays down\n",
			         FFE_SCREEN_W, h);
			fflush (stdout);
			return;
		}
		ffe_bar_on = bar;
		if (bar && amigagfx_game_height () != FFE_SCREEN_H) {
			int real = h - amigagfx_game_height ();	/* the bar Intuition really drew */

			amigagfx_close ();
			h = FFE_SCREEN_H + real;
			if (amigagfx_open (FFE_SCREEN_W, h, bar, be) != 0
			    || amigagfx_game_height () < FFE_SCREEN_H) {
				fprintf (stdout, "ffe: screen with bar unusable - platform layer stays down\n");
				fflush (stdout);
				return;
			}
		}
	}

	ffe_boot_mark ("screen open");
	ffe_timer_init ();
	ffe_dir_init ();
	ffe_boot_mark ("dir init");		/* MUST precede ffe_sound_init - see the header */
	ffe_video_init ();
	ffe_input_init ();
	ffe_boot_mark ("video+input init");
	ffe_sound_init ();		/* failure here is survivable: the game runs silent */

	ffe_boot_mark ("sound init (SystemInit end)");
	g_ready = 1;
	atexit (SystemCleanup);	/* soft restart: close the screen and audio on exit() */
}

void SystemCleanup (void)
{
	if (!g_ready) return;

	ffe_boot_mark ("cleanup begin");
	ffe_sound_cleanup ();		/* frees the Chip RAM samples and closes Paula */
	ffe_boot_mark ("cleanup: sound done");
	ffe_input_cleanup ();
	ffe_video_cleanup ();
	ffe_dir_cleanup ();
	ffe_timer_cleanup ();
	ffe_boot_mark ("cleanup: input/video/dir/timer done");
	amigagfx_close ();
	ffe_boot_mark ("cleanup: screen closed");

	g_ready = 0;
}

int ffe_system_ready (void)
{
	return g_ready;
}
