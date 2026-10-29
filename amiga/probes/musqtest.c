/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 angree. See LICENSE at the repository root.
 *
 * musqtest (2026-10-24): amiga_audio.c's music streamer on its own -- a
 * 440 Hz tone through AmigaAudio_MusicStart for 5 s, refilled from the loop,
 * then with the refill process. Settles "the streamer or the game around it?"
 * when the game hung in the second queued CMD_WRITE.
 */
#include <stdio.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include "../amiga_audio.h"

static long phase;
static int tone (void *ud, signed char *dst, int max)
{
	int i;
	(void)ud;
	for (i = 0; i < max; i++, phase++) dst[i] = (signed char)(((phase / 25) & 1) ? 40 : -40);
	return max;
}

int main (void)
{
	int i, thr;

	printf ("open %d\n", AmigaAudio_Open ()); fflush (stdout);
	printf ("start %d\n", AmigaAudio_MusicStart (3546895 / 22050, 4096, tone, NULL)); fflush (stdout);
	for (i = 0; i < 250; i++) { AmigaAudio_MusicService (); Delay (1); }
	printf ("loop refill ok, underruns %lu\n", AmigaAudio_MusicUnderruns ()); fflush (stdout);
	AmigaAudio_MusicStop ();
	thr = AmigaAudio_MusicThreadStart (1);
	printf ("thread %d, start %d\n", thr, AmigaAudio_MusicStart (3546895 / 22050, 4096, tone, NULL)); fflush (stdout);
	Delay (250);
	printf ("thread refill ok, underruns %lu\n", AmigaAudio_MusicUnderruns ()); fflush (stdout);
	AmigaAudio_MusicStop ();
	AmigaAudio_MusicThreadStop ();
	AmigaAudio_Close ();
	printf ("done\n");
	return 0;
}
