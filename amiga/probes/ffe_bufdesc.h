/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 angree. See LICENSE at the repository root.
 *
 * The game's back-buffer descriptor, for the probes that run translated code.
 *
 * FUNC_001601_DrawHLine -- and, in time, most of the rasteriser -- reads its
 * destination out of [DATA_007773]: ffedat.asm:24815, six longwords,
 * `{pixels, x, y, 320, 200, jump}`. So any probe that calls translated drawing
 * code has to DEFINE that symbol, because the translated .s only references it.
 *
 * THIS IS THE FIRST LIVE INSTANCE OF THE ENDIANNESS PROBLEM (PLAN 6), and it is
 * handled here in the way the plan reserves for exactly this situation: BY HAND,
 * as a big-endian C structure, not as a byte copy of the x86 image. The original
 * in ffedat.asm is written as
 *
 *     DATA_007773:  dd DATA_009226
 *                   db 0,0,0,0, 0,0,0,0
 *                   db 0x40,0x1,0,0,  0xc8,0,0,0
 *                   db 0,0,0,0
 *
 * -- 0x40,0x01 is 320 and 0xc8 is 200, little-endian, low byte first. Copying
 * those twenty-four bytes onto a 68k would give a width of 0x40010000. Writing
 * the fields as C longwords instead lets the m68k compiler lay them out in ITS
 * byte order, and 320 stays 320. Doing it by hand is correct precisely because
 * the general converter does not exist yet: this is one struct with six fields
 * whose meaning is documented, not a 2.26 MB blob, and it costs nothing to be
 * right about it now. The 8,596-label version of the same job is the datamap
 * pipeline, and it is a different piece of work.
 *
 * The layout must match the x86 one field for field, which it does: six 32-bit
 * quantities, 24 bytes, no padding on either machine.
 *
 * Each probe defines its OWN instance -- see the definitions in
 * ffe_xlattest.c (a small buffer it can scan byte by byte) and in
 * ffe_drawtest.c (the real chunky framebuffer). Only the type and the argument
 * live here.
 */

#ifndef FFE_BUFDESC_H
#define FFE_BUFDESC_H

typedef struct {
	unsigned char *pixels;	/* +0   the 8bpp buffer DrawHLine writes into  */
	long x;			/* +4   both zero in the original              */
	long y;			/* +8                                          */
	long w;			/* +12  320                                    */
	long h;			/* +16  200 here, 158 in DATA_007774           */
	long jump;		/* +20  0                                      */
} FfeBufDesc;

/* The symbol the translated code references. gcc decorates C names with a
 * leading underscore on m68k-amigaos -- which is why the translator's C-ABI
 * entry points are spelled _FUNC_nnnnnn in the .s -- so the asm name has to be
 * given explicitly, exactly as tools\oracle\oracle.c does on the x86 side. */
extern FfeBufDesc DATA_007773 __asm__("DATA_007773");

#endif /* FFE_BUFDESC_H */
