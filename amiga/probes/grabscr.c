/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 angree. See LICENSE at the repository root.
 *
 * grabscr (2026-10-24): screenshot from INSIDE the emulated Amiga. Waits,
 * then reads the frontmost screen through graphics.library ReadPixelArray8
 * (works on planar AGA screens, RTG screens and AROS alike) and its palette
 * through GetRGB32, and writes FFEFRM01-style data so the host can turn it
 * into a picture -- independent of the host display (PrintWindow returns
 * black while the host monitors sleep).
 *   grabscr SECONDS FILE      e.g. grabscr 60 Work:T/g60.raw
 * File: "GRAB" w.w h.w, 256*3 bytes RGB, then w*h pixel indices.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <exec/types.h>
#include <exec/memory.h>
#include <intuition/intuition.h>
#include <intuition/intuitionbase.h>
#include <graphics/gfx.h>
#include <graphics/rastport.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/graphics.h>
#include <proto/intuition.h>

extern struct IntuitionBase *IntuitionBase;

int main (int argc, char **argv)
{
	struct Screen *s;
	struct RastPort tmp;
	struct BitMap *tbm;
	UBYTE *pix;
	ULONG *rgb;
	FILE *f;
	int w, h, i;
	ULONG lk;

	if (argc < 3) { printf ("grabscr SECONDS FILE\n"); return 10; }
	Delay (atoi (argv[1]) * 50);
	lk = LockIBase (0);
	s = IntuitionBase->FirstScreen;
	UnlockIBase (lk);
	w = s->Width; h = s->Height;
	pix = AllocVec ((ULONG)((w + 15) & ~15) * h, MEMF_ANY | MEMF_CLEAR);
	rgb = AllocVec (256 * 3 * 4 + 8, MEMF_ANY | MEMF_CLEAR);
	tbm = AllocBitMap ((w + 15) & ~15, 1, 8, BMF_CLEAR, s->RastPort.BitMap);
	if (!tbm) tbm = AllocBitMap ((w + 15) & ~15, 1, 8, BMF_CLEAR, NULL);	/* RTG friend refused */
	if (!pix || !rgb || !tbm) { printf ("grabscr: no memory (pix %p rgb %p tbm %p)\n", pix, rgb, tbm); return 20; }
	tmp = s->RastPort;
	tmp.Layer = NULL;
	tmp.BitMap = tbm;
	/* ATOMIC: on AROS ReadPixelArray8 is slow enough to span several game
	 * frames, and the picture came back as bands from different frames.
	 * Forbid() keeps the game from drawing while the screen is read. */
	Forbid ();
	ReadPixelArray8 (&s->RastPort, 0, 0, w - 1, h - 1, pix, &tmp);
	GetRGB32 (s->ViewPort.ColorMap, 0, 256, rgb);
	Permit ();
	f = fopen (argv[2], "wb");
	if (!f) { printf ("grabscr: cannot write %s\n", argv[2]); return 20; }
	fwrite ("GRAB", 1, 4, f);
	fputc (w >> 8, f); fputc (w, f); fputc (h >> 8, f); fputc (h, f);
	for (i = 0; i < 256 * 3; i++) fputc ((int)(rgb[i] >> 24), f);
	for (i = 0; i < h; i++) fwrite (pix + (ULONG)i * ((w + 15) & ~15), 1, w, f);
	fclose (f);
	printf ("grabscr: '%s' %dx%d -> %s\n", s->Title ? (char *)s->Title : "", w, h, argv[2]);
	return 0;
}
