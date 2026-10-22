#include <stdio.h>
#include "ffe_dev.h"
/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 angree. See LICENSE at the repository root.
 *
 * BlitClipWrapper: clip a rectangle to the 320x200 screen, then hand the
 * clipped rectangle to whichever blit function the caller passed in.
 *
 * Also NOT a helper of ours. ffe.asm:1786 declares _BlitClipWrapper EXTERN and
 * the game calls it from fourteen sites, the first at ffe.asm:97063 in
 * FUNC_001617_BlitVidToBuf, which pushes _BufferBlit as the last argument --
 * i.e. the game supplies its own blit routine and expects us to do nothing but
 * arithmetic. cdecl, seven arguments, `add esp, 0x1c` after the call: 7 x 4
 * bytes, which is the signature below on the nose.
 *
 * NO AMIGA HEADERS, and none are needed: this is integer arithmetic and one
 * indirect call. ffeapi.h is included only for FFE_SCREEN_W / FFE_SCREEN_H, and
 * that header is plain C by policy.
 */

#include "ffeapi.h"

void BlitClipWrapper (void *pData, int xpos, int ypos, int width, int height,
                      int jump, void (*BlitFunc)(void *, int, int, int, int, int))
{
	int srcx, srcy, t;
	int scanw = width + jump;		/* source stride: the blits are jump + w */

	if (xpos >= 0) srcx = 0;
	else { srcx = -xpos; width  += xpos; xpos = 0; }
	if (ypos >= 0) srcy = 0;
	else { srcy = -ypos; height += ypos; ypos = 0; }

	if ((t = xpos + width  - FFE_SCREEN_W) > 0) width  -= t;
	if ((t = ypos + height - FFE_SCREEN_H) > 0) height -= t;

	/* ref/jjffe/ffemisc.c stops here and calls BlitFunc unconditionally, which
	 * for a rectangle entirely off the left edge means calling it with a
	 * NEGATIVE width -- xpos = -1000, width = 10 leaves width at -990. The SDL
	 * and Win32 blits survive that because their inner loops are `for (i = 0;
	 * i < w; i++)` and simply do nothing; ours are memcpy-based, and a negative
	 * length promoted to size_t is 4 GB of copy. So we return instead. Nothing
	 * is lost: a rectangle with no extent has nothing to draw. */
	if (width <= 0 || height <= 0) return;

	jump  = scanw - width;
	pData = (void *)((char *)pData + srcy * scanw + srcx);

	BlitFunc (pData, xpos, ypos, width, height, jump);
}

/* sprintf FOR THE TRANSLATED GAME CODE (2026-10-09). ffe.asm calls _sprintf at
 * 17 sites (file names such as "cabin%i.bmp", HUD and menu strings). On this
 * toolchain's libnix sprintf() produces nonsense -- wrong values, shifted
 * arguments, empty %s (measured on the OpenXcom port; fprintf, snprintf and
 * vsnprintf are correct). Defining it here keeps the linker from pulling the
 * broken one in. 1024 is far above any buffer the game formats into. */
#include <stdarg.h>
int sprintf (char *buf, const char *fmt, ...)
{
	va_list ap;
	int n;

	va_start (ap, fmt);
	n = vsnprintf (buf, 1024, fmt, ap);
	va_end (ap);
	{
		/* diagnostic (2026-10-09): the first 40 results, to see which file
		 * names the game builds */
		static int logged;
		if (logged < 40 && !(fmt[0] == '%' && fmt[1] == '0')) {	/* not the per-frame clock */
			FILE *lf = ffe_logopen ("PROGDIR:ffe_sprintf.log", logged ? "a" : "w");
			logged++;
			if (lf != NULL) { fprintf (lf, "[%s] <- [%s]%c", buf, fmt, 10); fclose (lf); }
		}
	}
	return n;
}
