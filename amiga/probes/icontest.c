/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 angree. See LICENSE at the repository root.
 *
 * icontest (2026-10-23): checks the release icons on the real OS. Loads each
 * .info named on the command line with icon.library's GetDiskObject, prints
 * what Workbench will read from it, and draws both images in a window on the
 * Workbench screen so a screenshot shows them as Workbench does.
 *   icontest Work:AmiFFE Work:AmiFFE/ffe_game ... >Work:icontest.log
 */
#include <stdio.h>
#include <exec/types.h>
#include <intuition/intuition.h>
#include <workbench/workbench.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <proto/icon.h>

struct Library *IconBase;

int main (int argc, char **argv)
{
	static const char *kind[] = { "?", "DISK", "DRAWER", "TOOL", "PROJECT", "GARBAGE", "DEVICE", "KICK", "APPICON" };
	struct Window *win;
	int i, x = 10;

	IconBase = OpenLibrary ("icon.library", 37);
	if (!IconBase) { printf ("icon.library v37 missing\n"); return 20; }
	win = OpenWindowTags (NULL, WA_Left, 20, WA_Top, 300, WA_Width, 600, WA_Height, 120,
	                      WA_Title, (ULONG)"icontest", WA_DragBar, TRUE, WA_DepthGadget, TRUE,
	                      TAG_END);
	for (i = 1; i < argc; i++) {
		struct DiskObject *d = GetDiskObject ((STRPTR)argv[i]);
		struct Image *im;
		if (!d) { printf ("%s: GetDiskObject FAILED, IoErr %ld\n", argv[i], (long)IoErr ()); continue; }
		im = (struct Image *)d->do_Gadget.GadgetRender;
		printf ("%s: magic %04x ver %d type %s %dx%d flags %04x stack %ld pos %ld,%ld tool '%s' drawer %s\n",
		        argv[i], d->do_Magic, d->do_Version,
		        d->do_Type < 9 ? kind[d->do_Type] : "?",
		        d->do_Gadget.Width, d->do_Gadget.Height, d->do_Gadget.Flags,
		        (long)d->do_StackSize, (long)d->do_CurrentX, (long)d->do_CurrentY,
		        d->do_DefaultTool ? (char *)d->do_DefaultTool : "",
		        d->do_DrawerData ? "yes" : "no");
		if (d->do_DrawerData)
			printf ("   drawer window %d,%d %dx%d\n", d->do_DrawerData->dd_NewWindow.LeftEdge,
			        d->do_DrawerData->dd_NewWindow.TopEdge, d->do_DrawerData->dd_NewWindow.Width,
			        d->do_DrawerData->dd_NewWindow.Height);
		if (im) printf ("   image %dx%d depth %d pick %d\n", im->Width, im->Height, im->Depth, im->PlanePick);
		if (win && im) {
			DrawImage (win->RPort, im, x, 16);
			if (d->do_Gadget.SelectRender)
				DrawImage (win->RPort, (struct Image *)d->do_Gadget.SelectRender, x, 16 + im->Height + 4);
			x += im->Width + 14;
		}
		FreeDiskObject (d);
	}
	fflush (stdout);
	Delay (50 * 60);
	if (win) CloseWindow (win);
	CloseLibrary (IconBase);
	return 0;
}
