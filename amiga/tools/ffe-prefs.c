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
 *
 *   ffe-prefs                      window (also from Workbench)
 *   ffe-prefs SHOW                 print the settings
 *   ffe-prefs GFX=RTG VIDEO=PAL    save without a window
 */

#include <stdio.h>
#include <string.h>

#include <exec/types.h>
#include <intuition/intuition.h>
#include <libraries/gadtools.h>
#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/gadtools.h>

#define CFG "PROGDIR:amiffe.cfg"

static const char ver[] = "$VER: ffe-prefs 0.1.3 (24.10.2026)";

/* Messages are for the Shell only: started from Workbench, libnix would open
 * a console window for the first printf. Do NOT set __stdiowin to "NIL:" to
 * avoid that (2026-10-23): measured, the program then never reaches main()
 * when started from an icon, so the window never opened. */
static int from_wb;
#define say(...) do { if (!from_wb) printf (__VA_ARGS__); } while (0)

static int gfx, video;

static STRPTR gfx_lab[]   = { (STRPTR)"Auto (AGA)", (STRPTR)"AGA", (STRPTR)"RTG (Picasso96/CGX)", NULL };
static STRPTR video_lab[] = { (STRPTR)"As booted", (STRPTR)"PAL", (STRPTR)"NTSC", NULL };
static const char *gfx_arg[]   = { "AUTO", "AGA", "RTG" };
static const char *video_arg[] = { "AUTO", "PAL", "NTSC" };

static void load (void)
{
	char line[80];
	int v;
	FILE *f = fopen (CFG, "r");

	if (f == NULL) return;
	while (fgets (line, sizeof line, f) != NULL) {
		if (sscanf (line, "gfx=%d", &v) == 1) { if (v >= 0 && v <= 2) gfx = v; }
		else if (sscanf (line, "video=%d", &v) == 1) { if (v >= 0 && v <= 2) video = v; }
	}
	fclose (f);
}

static int save (void)
{
	FILE *f = fopen (CFG, "w");

	if (f == NULL) return 0;
	fprintf (f, "gfx=%d\nvideo=%d\n", gfx, video);
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
	printf ("gfx=%s video=%s  (RTG library %s)\n", gfx_arg[gfx], video_arg[video],
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

enum { G_GFX, G_VIDEO, G_SAVE, G_CANCEL };

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
	gw = 22 * 8 + 24;

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
			printf ("ffe-prefs [GFX=AUTO|AGA|RTG] [VIDEO=AUTO|PAL|NTSC] [SHOW]\n");
			return 0;
		}
		r = pick (argv[i], "GFX=", gfx_arg, &gfx);
		if (r == 0) r = pick (argv[i], "VIDEO=", video_arg, &video);
		if (r <= 0) { printf ("ffe-prefs: unknown argument %s (try ?)\n", argv[i]); return 10; }
		any = 1;
	}
	if (any) {
		if (!save ()) { printf ("ffe-prefs: cannot write %s\n", CFG); return 10; }
		show ();
	}
	return 0;
}
