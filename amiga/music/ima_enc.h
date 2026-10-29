/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 angree. See LICENSE at the repository root.
 *
 * IMA ADPCM WAV writer (2026-10-24): the exact format amiga/amiga_adpcm.c
 * streams -- wFormatTag 0x11, mono, 4-bit, blockAlign 1024 = 2041 samples
 * per block. Plain C on stdio, no float; host and Amiga.
 */
#ifndef IMA_ENC_H
#define IMA_ENC_H

#include <stdio.h>

typedef struct ImaWriter ImaWriter;

ImaWriter *ima_open (const char *path, int rate);	/* NULL on failure */
void       ima_write (ImaWriter *w, const short *pcm, long n);
int        ima_close (ImaWriter *w);			/* flushes, patches the header; 0 = ok */

#endif
