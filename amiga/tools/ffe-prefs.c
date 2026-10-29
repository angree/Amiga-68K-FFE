/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 angree. See LICENSE at the repository root.
 *
 * ffe-prefs: picks the display for AmiFFE before the game starts (2026-10-16,
 * modelled on remom-prefs from the ReMoM port). A separate program because a
 * screen that does not open cannot show an options menu.
 *
 * Writes PROGDIR:amiffe.cfg, read by SystemInit (amiga/ffeapi/ffe_system.c):
 *   gfx=0|1|2    auto (= AGA) / AGA / RTG
 *   video=0|1|2  as the machine boots / PAL / NTSC (chipset screen only)
 *   tex=0|1|2    textures as the original / distant polygons flat / all flat
 *
 *   ffe-prefs                      window (also from Workbench)
 *   ffe-prefs SHOW                 print the settings
 *   ffe-prefs GFX=RTG VIDEO=PAL    save without a window
 *   ffe-prefs TEX=FAR              (TEX=ON|FAR|OFF)
 */

#include <stdio.h>
#include <string.h>

#include <exec/types.h>
#include <intuition/intuition.h>
#include <libraries/gadtools.h>
#include <dos/dostags.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <proto/gadtools.h>

const char *ffe_pd (const char *name);	/* ffeapi/ffe_progdir.c: this program's folder */
#define CFG ffe_pd ("amiffe.cfg")

static const char ver[] = "$VER: ffe-prefs 0.2.0 (30.10.2026)";

/* Messages are for the Shell only: started from Workbench, libnix would open
 * a console window for the first printf. Do NOT set __stdiowin to "NIL:" to
 * avoid that (2026-10-23): measured, the program then never reaches main()
 * when started from an icon, so the window never opened. */
static int from_wb;
#define say(...) do { if (!from_wb) printf (__VA_ARGS__); } while (0)

static int gfx, video, tex, sound = 1, music = 1, mrate;	/* mrate 0 = 11025, 1 = 22050 */
static int plod = 1;		/* planet detail: 1 reduced (default), 0 original */
static STRPTR plod_lab[] = { (STRPTR)"Original", (STRPTR)"Reduced (faster)", NULL };
static const char *plod_arg[] = { "ORIGINAL", "REDUCED" };

static STRPTR gfx_lab[]   = { (STRPTR)"Auto (AGA)", (STRPTR)"AGA", (STRPTR)"RTG (Picasso96/CGX)", NULL };
static STRPTR video_lab[] = { (STRPTR)"As booted", (STRPTR)"PAL", (STRPTR)"NTSC", NULL };
static const char *gfx_arg[]   = { "AUTO", "AGA", "RTG" };
static const char *video_arg[] = { "AUTO", "PAL", "NTSC" };
static STRPTR tex_lab[]   = { (STRPTR)"On (original)", (STRPTR)"Distant flat", (STRPTR)"Off (flat)", NULL };
static const char *tex_arg[]   = { "ON", "FAR", "OFF" };
static STRPTR sound_lab[] = { (STRPTR)"Off", (STRPTR)"Paula", (STRPTR)"AHI", NULL };
static const char *sound_arg[] = { "OFF", "PAULA", "AHI" };
static STRPTR music_lab[] = { (STRPTR)"Off", (STRPTR)"AdLib (pre-rendered)", (STRPTR)"General MIDI (pre-rendered)",
                              (STRPTR)"AdLib live (fast CPU)", (STRPTR)"General MIDI live (fast CPU)", NULL };
static const char *music_arg[] = { "OFF", "ADLIB", "GM", "ADLIBLIVE", "GMLIVE" };
static STRPTR mrate_lab[] = { (STRPTR)"11 kHz", (STRPTR)"22 kHz", NULL };
static const char *mrate_arg[] = { "11025", "22050" };

static void load (void)
{
	char line[80];
	int v;
	FILE *f = fopen (CFG, "r");

	if (f == NULL) return;
	while (fgets (line, sizeof line, f) != NULL) {
		if (sscanf (line, "gfx=%d", &v) == 1) { if (v >= 0 && v <= 2) gfx = v; }
		else if (sscanf (line, "video=%d", &v) == 1) { if (v >= 0 && v <= 2) video = v; }
		else if (sscanf (line, "tex=%d", &v) == 1) { if (v >= 0 && v <= 2) tex = v; }
		else if (sscanf (line, "planetlod=%d", &v) == 1) plod = v != 0;
		else if (sscanf (line, "sound=%d", &v) == 1) { if (v >= 0 && v <= 2) sound = v; }
		else if (sscanf (line, "music=%d", &v) == 1) { if (v >= 0 && v <= 4) music = v; }
		else if (sscanf (line, "musicrate=%d", &v) == 1) mrate = v == 22050;
	}
	fclose (f);
}

static int save (void)
{
	FILE *f = fopen (CFG, "w");

	if (f == NULL) return 0;
	fprintf (f, "gfx=%d\nvideo=%d\ntex=%d\nplanetlod=%d\nsound=%d\nmusic=%d\nmusicrate=%s\n",
	         gfx, video, tex, plod, sound, music, mrate_arg[mrate]);
	fclose (f);
	return 1;
}

static int has_rtg (void)
{
	struct Library *l = OpenLibrary ((CONST_STRPTR)"cybergraphics.library", 41);

	if (l == NULL) return 0;
	CloseLibrary (l);
	return 1;
}

static void show (void)
{
	printf ("gfx=%s video=%s tex=%s sound=%s music=%s rate=%s  (RTG library %s)\n",
	        gfx_arg[gfx], video_arg[video], tex_arg[tex], sound_arg[sound], music_arg[music], mrate_arg[mrate],
	        has_rtg () ? "present" : "not found");
}

static int pick (const char *a, const char *key, const char **names, int *out)
{
	size_t n = strlen (key);
	int i;

	if (strncasecmp (a, key, n) != 0) return 0;
	for (i = 0; i < 3; i++)
		if (strcasecmp (a + n, names[i]) == 0) { *out = i; return 1; }
	return -1;
}

static int pick_n (const char *a, const char *key, const char **names, int count, int *out)
{
	size_t n = strlen (key);
	int i;

	if (strncasecmp (a, key, n) != 0) return 0;
	for (i = 0; i < count; i++)
		if (strcasecmp (a + n, names[i]) == 0) { *out = i; return 1; }
	return -1;
}

enum { G_GFX, G_VIDEO, G_TEX, G_PLOD, G_SOUND, G_MUSIC, G_MRATE, G_RQ, G_RENDER1, G_RENDER, G_STATUS, G_SAVE, G_CANCEL };
static struct Gadget *g_status, *g_render, *g_render1;

/* ---- rendering the songs from here (2026-10-24, developer: "like ReMoM") --
 * The pre-rendered music modes need PROGDIR:Music/AdLib or GM, written by
 * PROGDIR:ffe-music from the original DOS files. The button runs it with its
 * output in a console window, so the progress is visible; the prefs window
 * waits meanwhile. */
static int rquality;			/* 0 fast (11 kHz, cheap OPL model), 1 best (22 kHz) */
/* measured 2026-10-24 for all 17 songs (41 min of music) on a 68040/40-class
 * emulated machine without JIT: Fast about 19 min, Best about 4 hours */
static STRPTR rq_lab[] = { (STRPTR)"Fast (11 kHz, ~20 min)", (STRPTR)"Best (22 kHz, ~4 hours)", NULL };
static char status_txt[80];

static int rendered (const char *kind)
{
	char p[64];
	BPTR l;

	snprintf (p, sizeof p, "%sMusic/%s/qqfront.wav", ffe_pd (""), kind);
	l = Lock ((CONST_STRPTR)p, ACCESS_READ);
	if (l) UnLock (l);
	return l != 0;
}

static void make_status (void)
{
	snprintf (status_txt, sizeof status_txt, "Songs: AdLib %s, General MIDI %s",
	          rendered ("AdLib") ? "ready" : "not rendered",
	          rendered ("GM") ? "ready" : "not rendered");
}

static void render_songs (int all)
{
	char cmd[160];
	int gm = (music == 2 || music == 4);
	BPTR con = Open ((CONST_STRPTR)"CON:20/20/560/220/AmiFFE: rendering the music/AUTO/CLOSE/WAIT", MODE_NEWFILE);

	char dir[200];
	BPTR here;

	if (con == 0) return;
	/* "PROGDIR:" means nothing to the Shell that runs the command (it asked
	 * for a volume called PROGDIR), so the program's folder is spelled out
	 * and also made the command's current directory */
	if (!NameFromLock (GetProgramDir (), (STRPTR)dir, sizeof dir)) strcpy (dir, "");
	snprintf (cmd, sizeof cmd, "\"%s%sffe-music\" %s RATE=%s Q=%d%s", dir,
	          (dir[0] && dir[strlen (dir) - 1] != ':') ? "/" : "",
	          gm ? "GM" : "ADLIB", rquality ? "22050" : "11025", rquality,
	          all ? "" : " SONG=qqfront");	/* the title theme, the one the intro plays */
	here = DupLock (GetProgramDir ());
	SystemTags ((CONST_STRPTR)cmd, SYS_Input, 0, SYS_Output, con, NP_StackSize, 65536,
	            here ? NP_CurrentDir : TAG_IGNORE, here, TAG_END);
	/* NP_CurrentDir hands the lock to the new process, which unlocks it */
	Close (con);
}

static int window (void)
{
	struct Screen *scr = LockPubScreen (NULL);
	APTR vi = NULL;
	struct Gadget *glist = NULL, *g;
	struct Window *win = NULL;
	struct NewGadget ng;
	int fh, bt, lx, gw, done = 0, ok = 0;

	if (scr == NULL) { say ("ffe-prefs: no public screen\n"); return 0; }
	vi = GetVisualInfo (scr, TAG_END);
	if (vi == NULL) goto out;
	fh = scr->Font->ta_YSize;
	bt = scr->WBorTop + fh + 1;
	lx = scr->WBorLeft + 8 + 9 * 8;	/* room for "Display:" */
	gw = 30 * 8 + 24;	/* "General MIDI (pre-rendered)" */

	g = CreateContext (&glist);
	memset (&ng, 0, sizeof ng);
	ng.ng_VisualInfo = vi;
	ng.ng_TextAttr = scr->Font;
	ng.ng_Height = fh + 6;

	ng.ng_LeftEdge = lx; ng.ng_TopEdge = bt + 6; ng.ng_Width = gw;
	ng.ng_GadgetText = (STRPTR)"_Display"; ng.ng_GadgetID = G_GFX; ng.ng_Flags = PLACETEXT_LEFT;
	g = CreateGadget (CYCLE_KIND, g, &ng, GTCY_Labels, (ULONG)gfx_lab, GTCY_Active, gfx,
	                  GT_Underscore, '_', TAG_END);
	ng.ng_TopEdge += fh + 10;
	ng.ng_GadgetText = (STRPTR)"_Video"; ng.ng_GadgetID = G_VIDEO;
	g = CreateGadget (CYCLE_KIND, g, &ng, GTCY_Labels, (ULONG)video_lab, GTCY_Active, video,
	                  GT_Underscore, '_', TAG_END);
	ng.ng_TopEdge += fh + 10;
	ng.ng_GadgetText = (STRPTR)"_Textures"; ng.ng_GadgetID = G_TEX;
	g = CreateGadget (CYCLE_KIND, g, &ng, GTCY_Labels, (ULONG)tex_lab, GTCY_Active, tex,
	                  GT_Underscore, '_', TAG_END);
	ng.ng_TopEdge += fh + 10;
	ng.ng_GadgetText = (STRPTR)"_Planets"; ng.ng_GadgetID = G_PLOD;
	g = CreateGadget (CYCLE_KIND, g, &ng, GTCY_Labels, (ULONG)plod_lab, GTCY_Active, plod,
	                  GT_Underscore, '_', TAG_END);
	ng.ng_TopEdge += fh + 10;
	ng.ng_GadgetText = (STRPTR)"S_ound"; ng.ng_GadgetID = G_SOUND;
	g = CreateGadget (CYCLE_KIND, g, &ng, GTCY_Labels, (ULONG)sound_lab, GTCY_Active, sound,
	                  GT_Underscore, '_', TAG_END);
	ng.ng_TopEdge += fh + 10;
	ng.ng_GadgetText = (STRPTR)"_Music"; ng.ng_GadgetID = G_MUSIC;
	g = CreateGadget (CYCLE_KIND, g, &ng, GTCY_Labels, (ULONG)music_lab, GTCY_Active, music,
	                  GT_Underscore, '_', TAG_END);
	ng.ng_TopEdge += fh + 10;
	ng.ng_GadgetText = (STRPTR)"_Live rate"; ng.ng_GadgetID = G_MRATE;
	g = CreateGadget (CYCLE_KIND, g, &ng, GTCY_Labels, (ULONG)mrate_lab, GTCY_Active, mrate,
	                  GT_Underscore, '_', TAG_END);
	ng.ng_TopEdge += fh + 10;
	ng.ng_GadgetText = (STRPTR)"_Quality"; ng.ng_GadgetID = G_RQ;
	g = CreateGadget (CYCLE_KIND, g, &ng, GTCY_Labels, (ULONG)rq_lab, GTCY_Active, rquality,
	                  GT_Underscore, '_', TAG_END);
	ng.ng_TopEdge += fh + 10;
	/* two buttons: the title theme only (about a minute, Fast) or all 17;
	 * only the pre-rendered modes use rendered songs: disabled otherwise */
	{
		int x0 = scr->WBorLeft + 8, w2 = (lx + gw - x0 - 8) / 2;
		ng.ng_Flags = PLACETEXT_IN;
		ng.ng_LeftEdge = x0; ng.ng_Width = w2;
		ng.ng_GadgetText = (STRPTR)"Render _title song"; ng.ng_GadgetID = G_RENDER1;
		g_render1 = g = CreateGadget (BUTTON_KIND, g, &ng, GT_Underscore, '_',
		                              GA_Disabled, !(music == 1 || music == 2), TAG_END);
		ng.ng_LeftEdge = x0 + w2 + 8;
		ng.ng_GadgetText = (STRPTR)"Render _all 17 songs"; ng.ng_GadgetID = G_RENDER;
		g_render = g = CreateGadget (BUTTON_KIND, g, &ng, GT_Underscore, '_',
		                             GA_Disabled, !(music == 1 || music == 2), TAG_END);
		ng.ng_LeftEdge = x0; ng.ng_Width = lx + gw - x0;
	}
	ng.ng_TopEdge += fh + 10;
	make_status ();
	ng.ng_GadgetText = NULL; ng.ng_GadgetID = G_STATUS;
	g_status = g = CreateGadget (TEXT_KIND, g, &ng, GTTX_Text, (ULONG)status_txt, GTTX_Border, TRUE, TAG_END);
	ng.ng_Flags = PLACETEXT_LEFT;
	ng.ng_TopEdge += fh + 16; ng.ng_LeftEdge = scr->WBorLeft + 8; ng.ng_Width = 10 * 8;
	ng.ng_GadgetText = (STRPTR)"_Save"; ng.ng_GadgetID = G_SAVE; ng.ng_Flags = PLACETEXT_IN;
	g = CreateGadget (BUTTON_KIND, g, &ng, GT_Underscore, '_', TAG_END);
	ng.ng_LeftEdge = lx + gw - 10 * 8;
	ng.ng_GadgetText = (STRPTR)"_Cancel"; ng.ng_GadgetID = G_CANCEL;
	g = CreateGadget (BUTTON_KIND, g, &ng, GT_Underscore, '_', TAG_END);
	if (g == NULL) { say ("ffe-prefs: CreateGadget failed\n"); goto out; }

	win = OpenWindowTags (NULL,
		WA_PubScreen, (ULONG)scr,
		WA_Title, (ULONG)"AmiFFE prefs",
		WA_Left, 40, WA_Top, 30,
		WA_InnerWidth, lx + gw + 8 - scr->WBorLeft,
		WA_InnerHeight, ng.ng_TopEdge + ng.ng_Height + 6 - bt,
		WA_Gadgets, (ULONG)glist,
		WA_DragBar, TRUE, WA_DepthGadget, TRUE, WA_CloseGadget, TRUE, WA_Activate, TRUE,
		WA_IDCMP, CYCLEIDCMP | BUTTONIDCMP | IDCMP_CLOSEWINDOW | IDCMP_REFRESHWINDOW | IDCMP_VANILLAKEY,
		TAG_END);
	if (win == NULL) { say ("ffe-prefs: OpenWindow failed\n"); goto out; }
	if (!from_wb) { printf ("ffe-prefs: window open\n"); fflush (stdout); }
	GT_RefreshWindow (win, NULL);

	while (!done) {
		struct IntuiMessage *m;

		WaitPort (win->UserPort);
		while ((m = GT_GetIMsg (win->UserPort)) != NULL) {
			ULONG cl = m->Class;
			UWORD code = m->Code;
			struct Gadget *src = (struct Gadget *)m->IAddress;

			GT_ReplyIMsg (m);
			if (cl == IDCMP_CLOSEWINDOW) done = 1;
			else if (cl == IDCMP_REFRESHWINDOW) { GT_BeginRefresh (win); GT_EndRefresh (win, TRUE); }
			else if (cl == IDCMP_GADGETUP) {
				if (src->GadgetID == G_GFX) gfx = code;
				else if (src->GadgetID == G_VIDEO) video = code;
				else if (src->GadgetID == G_TEX) tex = code;
				else if (src->GadgetID == G_PLOD) plod = code;
				else if (src->GadgetID == G_SOUND) sound = code;
				else if (src->GadgetID == G_MUSIC) {
					music = code;
					GT_SetGadgetAttrs (g_render, win, NULL, GA_Disabled, !(music == 1 || music == 2), TAG_END);
					GT_SetGadgetAttrs (g_render1, win, NULL, GA_Disabled, !(music == 1 || music == 2), TAG_END);
				}
				else if (src->GadgetID == G_MRATE) mrate = code;
				else if (src->GadgetID == G_RQ) rquality = code;
				else if (src->GadgetID == G_RENDER || src->GadgetID == G_RENDER1) {
					render_songs (src->GadgetID == G_RENDER);
					make_status ();
					GT_SetGadgetAttrs (g_status, win, NULL, GTTX_Text, (ULONG)status_txt, TAG_END);
				}
				else if (src->GadgetID == G_SAVE) { ok = save (); done = 1; }
				else done = 1;
			} else if (cl == IDCMP_VANILLAKEY) {
				if (code == 's' || code == 'S' || code == 13) { ok = save (); done = 1; }
				else if (code == 'c' || code == 'C' || code == 27) done = 1;
			}
		}
	}
out:
	if (win != NULL) CloseWindow (win);
	FreeGadgets (glist);
	if (vi != NULL) FreeVisualInfo (vi);
	UnlockPubScreen (NULL, scr);
	return ok;
}

int main (int argc, char **argv)
{
	int i, any = 0;

	(void)ver;
	load ();
	from_wb = (argc == 0);
	if (argc <= 1) return window () ? 0 : 5;	/* argc 0 = Workbench */

	for (i = 1; i < argc; i++) {
		int r;

		if (strcasecmp (argv[i], "SHOW") == 0) { show (); continue; }
		if (strcmp (argv[i], "?") == 0) {
			printf ("ffe-prefs [GFX=AUTO|AGA|RTG] [VIDEO=AUTO|PAL|NTSC] [TEX=ON|FAR|OFF]\n"
			        "          [SOUND=OFF|PAULA] [MUSIC=OFF|ADLIB|GM|ADLIBLIVE|GMLIVE] [RATE=11025|22050] [SHOW]\n");
			return 0;
		}
		r = pick (argv[i], "GFX=", gfx_arg, &gfx);
		if (r == 0) r = pick (argv[i], "VIDEO=", video_arg, &video);
		if (r == 0) r = pick (argv[i], "TEX=", tex_arg, &tex);
		if (r == 0) r = pick_n (argv[i], "PLANETS=", plod_arg, 2, &plod);
		if (r == 0) r = pick_n (argv[i], "SOUND=", sound_arg, 3, &sound);
		if (r == 0) r = pick_n (argv[i], "MUSIC=", music_arg, 5, &music);
		if (r == 0) r = pick_n (argv[i], "RATE=", mrate_arg, 2, &mrate);
		if (r <= 0) { printf ("ffe-prefs: unknown argument %s (try ?)\n", argv[i]); return 10; }
		any = 1;
	}
	if (any) {
		if (!save ()) { printf ("ffe-prefs: cannot write %s\n", CFG); return 10; }
		show ();
	}
	return 0;
}
