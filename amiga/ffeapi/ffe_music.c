/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 angree. See LICENSE at the repository root.
 *
 * The game's music (2026-10-24). FFE plays 17 HMI songs (FFE/MUSIC/<song>.HMP on
 * the DOS floppy, originally on an AdLib/OPL with MELODIC.BNK + DRUM.BNK).
 * The game only asks for a song once SoundCheckInit reported MIDI, so this
 * file decides that, and serves SoundPlaySong / SoundStopSong / SoundSongDone.
 *
 * PROGDIR:amiffe.cfg music= (ffe_system.c):
 *   1, 2  PRE-RENDERED: PROGDIR:Music/AdLib|GM/<song>.wav, IMA ADPCM written
 *         once by the ffe-music tool (on the Amiga or a PC). Streamed from
 *         disk; costs little CPU. The method of the ReMoM port.
 *   3, 4  LIVE: the song is sequenced (music/hmp.c) and synthesised (OPL2 /
 *         CC0 General MIDI samples) while the game runs. Needs a fast CPU.
 *   5     external MIDI through camd.library (not yet).
 * Output is the Paula streamer of amiga_audio.c on channels 2+3 (the same
 * mono stream left and right); sound effects keep channels 0+1. Its buffers
 * are refilled by a separate process every 20 ms, so the game's low frame
 * rate (2-4 fps) cannot starve the music.
 *
 * THE REFILL RUNS IN THAT PROCESS, so nothing it calls may use stdio (the
 * game's newlib stdio is not re-entrant): the ADPCM file is read with
 * dos.library Read() in 32 KB pieces, and the decoder below is our own. The
 * HMP file and the banks are loaded by the game task before the stream starts.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <exec/types.h>
#include <dos/dos.h>
#include <proto/exec.h>
#include <proto/dos.h>

#include "ffeapi.h"
#include "amiga_gfx.h"
#include "../amiga_audio.h"
#include "../music/midi_synth.h"
#include "../music/hmp.h"
#include "../music/softclip.h"	/* x2.5 with a soft knee: the music was barely audible */
#include "../music/adlib_midi.h"
#ifdef FFE_HAVE_GM
#include "../music/gm_synth.h"
#endif

extern short ffe_music_mode, ffe_sound_mode;
extern long  ffe_music_rate;

#define PAL_CLOCK  3546895UL
#define NTSC_CLOCK 3579545UL
#define MUS_CHUNK  4096			/* 8-bit samples per Paula buffer */

static const char *songs[17] = {
	"qqfront", "qqatmosp", "qqtravel", "qqdrama", "qqsuspen", "qqrock",
	"qqescape", "qqparadi", "qqfront2", "hallking", "intro", "babayaga",
	"barem", "jupalt", "valkries", "bluedan", "ggofkiev"
};

/* ---- pre-rendered: IMA ADPCM from disk, no stdio ------------------------- */

static const short idx_tab[16] = { -1, -1, -1, -1, 2, 4, 6, 8, -1, -1, -1, -1, 2, 4, 6, 8 };
static const short step_tab[89] = {
	7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37,
	41, 45, 50, 55, 60, 66, 73, 80, 88, 97, 107, 118, 130, 143, 157, 173,
	190, 209, 230, 253, 279, 307, 337, 371, 408, 449, 494, 544, 598, 658,
	724, 796, 876, 963, 1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066,
	2272, 2499, 2749, 3024, 3327, 3660, 4026, 4428, 4871, 5358, 5894, 6484,
	7132, 7845, 8630, 9493, 10442, 11487, 12635, 13899, 15289, 16818,
	18500, 20350, 22385, 24623, 27086, 29794, 32767
};

#define FILE_BUF (32 * 1024)

typedef struct {
	BPTR fh;
	long rate, align, left;		/* data bytes not yet read from the file */
	UBYTE *buf; long blen, bpos;	/* file bytes in hand */
	long pred, index;
	int  nib;			/* nibbles left in the current block, -1 = at a header */
	UBYTE cur; int hi;		/* the byte being consumed */
} Ima;

static long rd32 (const UBYTE *p) { return p[0] | (p[1] << 8) | ((long)p[2] << 16) | ((long)p[3] << 24); }

static int ima_byte (Ima *s, UBYTE *b)
{
	if (s->bpos >= s->blen) {
		long n = s->left < FILE_BUF ? s->left : FILE_BUF;
		if (n <= 0) return 0;
		n = Read (s->fh, s->buf, n);
		if (n <= 0) return 0;
		s->left -= n; s->blen = n; s->bpos = 0;
	}
	*b = s->buf[s->bpos++];
	return 1;
}

static Ima *ima_open (const char *path)
{
	UBYTE h[64];
	Ima *s;
	BPTR fh = Open ((CONST_STRPTR)path, MODE_OLDFILE);
	long p;

	if (!fh) return NULL;
	if (Read (fh, h, 60) != 60 || memcmp (h, "RIFF", 4) || memcmp (h + 8, "WAVE", 4)
	    || (h[20] | (h[21] << 8)) != 0x11 || (h[22] | (h[23] << 8)) != 1) { Close (fh); return NULL; }
	s = (Ima *)calloc (1, sizeof *s);
	if (s) s->buf = (UBYTE *)malloc (FILE_BUF);
	if (!s || !s->buf) { if (s) free (s); Close (fh); return NULL; }
	s->fh = fh;
	s->rate = rd32 (h + 24);
	s->align = h[32] | (h[33] << 8);
	/* the chunks after fmt: find "data" (ffe-music writes it at 52) */
	for (p = 12 + 8 + rd32 (h + 16); p + 8 <= 60; ) {
		if (!memcmp (h + p, "data", 4)) { s->left = rd32 (h + p + 4); break; }
		p += 8 + rd32 (h + p + 4);
	}
	if (s->left <= 0 || s->align < 8) { Close (fh); free (s->buf); free (s); return NULL; }
	Seek (fh, p + 8, OFFSET_BEGINNING);
	s->nib = -1;
	return s;
}

static void ima_close (Ima *s)
{
	if (!s) return;
	Close (s->fh);
	free (s->buf);
	free (s);
}

static int ima_refill (void *ud, signed char *dst, int max)
{
	Ima *s = (Ima *)ud;
	int n = 0;

	while (n < max) {
		int code;
		long step, dq;

		if (s->nib < 0) {			/* block header: predictor, index */
			UBYTE b0, b1, b2, b3;
			if (!ima_byte (s, &b0) || !ima_byte (s, &b1) || !ima_byte (s, &b2) || !ima_byte (s, &b3)) break;
			s->pred = (short)(b0 | (b1 << 8));
			s->index = b2 > 88 ? 88 : b2;
			s->nib = (int)(s->align - 4) * 2;
			s->hi = 0;
			dst[n++] = ffe_softclip[(s->pred + 32768) >> 6];
			continue;
		}
		if (s->nib == 0) { s->nib = -1; continue; }
		if (!s->hi) { if (!ima_byte (s, &s->cur)) break; code = s->cur & 15; s->hi = 1; }
		else { code = s->cur >> 4; s->hi = 0; }
		s->nib--;
		step = step_tab[s->index];
		dq = step >> 3;
		if (code & 4) dq += step;
		if (code & 2) dq += step >> 1;
		if (code & 1) dq += step >> 2;
		s->pred += (code & 8) ? -dq : dq;
		if (s->pred > 32767) s->pred = 32767;
		if (s->pred < -32768) s->pred = -32768;
		s->index += idx_tab[code];
		if (s->index < 0) s->index = 0;
		if (s->index > 88) s->index = 88;
		dst[n++] = ffe_softclip[(s->pred + 32768) >> 6];
	}
	return n & ~1;
}

/* ---- live: sequencer + synth in the refill process ----------------------- */

typedef struct {
	HmpSong *song;
	MidiSynth *syn;
	long rate, acc;			/* microsecond remainder */
	int ended, tail;
	short pcm[MUS_CHUNK];
} Live;

static int live_refill (void *ud, signed char *dst, int max)
{
	Live *l = (Live *)ud;
	long us;
	int i;

	if (max > MUS_CHUNK) max = MUS_CHUNK;
	max &= ~1;
	if (l->ended && l->tail <= 0) return 0;
	/* advance the song by exactly max samples of time, remainder carried.
	 * 64-bit: max * 1000000 does not fit a 32-bit long (4096 * 10^6) -- with
	 * long it went negative on the Amiga, the sequencer never moved and the
	 * live modes were silent (2026-10-24; the host test has a 64-bit long). */
	{
		unsigned long long a = (unsigned long long)l->acc + (unsigned long long)max * 1000000ULL;
		us = (long)(a / (unsigned long long)l->rate);
		l->acc = (long)(a - (unsigned long long)us * (unsigned long long)l->rate);
	}
	if (!l->ended && !hmp_advance (l->song, us, l->syn)) { l->ended = 1; l->tail = (int)l->rate * 3 / 2; }
	l->syn->render (l->syn->ctx, l->pcm, max);
	for (i = 0; i < max; i++) dst[i] = ffe_softclip[((long)l->pcm[i] + 32768) >> 6];
	if (l->ended) l->tail -= max;
	return max;
}

/* ---- state, owned by the game task --------------------------------------- */

static int g_mode;			/* the effective music= (0 = no music) */
static int g_thread;
static Ima *g_ima;
static Live *g_live;
static unsigned char *g_mel, *g_drm, *g_bank;
static long g_mlen, g_dlen, g_blen;
static int g_playing;

static unsigned char *slurp (const char *path, long *len)
{
	FILE *f = fopen (path, "rb");
	unsigned char *p = NULL;
	long n;

	if (f == NULL) return NULL;
	fseek (f, 0, SEEK_END); n = ftell (f); fseek (f, 0, SEEK_SET);
	if (n > 0) p = (unsigned char *)malloc (n);
	if (p != NULL && fread (p, 1, n, f) != (size_t)n) { free (p); p = NULL; }
	fclose (f);
	*len = n;
	return p;
}

/* ffe_find_data_dir (ffe_dir.c) made the game's data folder the current
 * directory, so the DOS files are reached by their relative names */
static const char *data_dir (void)
{
	return "";
}

static int file_exists (const char *path)
{
	BPTR l = Lock ((CONST_STRPTR)path, ACCESS_READ);
	if (l) UnLock (l);
	return l != 0;
}

static void song_path (char *out, int n, int index)
{
	snprintf (out, n, ffe_pd ("Music/%s/%s.wav"), g_mode == 2 ? "GM" : "AdLib", songs[index]);
}

void ffe_music_init (void)
{
	char p[160];

	g_mode = 0;
	if (ffe_sound_mode == 0 || ffe_music_mode == 0) return;
	switch (ffe_music_mode) {
	case 1: case 2:
		g_mode = ffe_music_mode;
		song_path (p, sizeof p, 0);
		if (!file_exists (p)) {
			fprintf (stdout, "ffe: music: %s not found - run ffe-music %s first; no music\n",
			         p, g_mode == 2 ? "GM" : "ADLIB");
			g_mode = 0;
		}
		break;
	case 3:
		snprintf (p, sizeof p, "%sMELODIC.BNK", data_dir ()); g_mel = slurp (p, &g_mlen);
		snprintf (p, sizeof p, "%sDRUM.BNK", data_dir ()); g_drm = slurp (p, &g_dlen);
		if (g_mel && g_drm) g_mode = 3;
		else fprintf (stdout, "ffe: music: AdLib live needs MELODIC.BNK and DRUM.BNK - no music\n");
		break;
#ifdef FFE_HAVE_GM
	case 4:
		g_bank = slurp (ffe_pd ("ffe_gm.bnk"), &g_blen);
		if (g_bank) g_mode = 4;
		else fprintf (stdout, "ffe: music: ffe_gm.bnk (next to the game) missing - no music\n");
		break;
#endif
	default:
		fprintf (stdout, "ffe: music=%d is not available in this build - no music\n", ffe_music_mode);
	}
	if (g_mode) {
		BPTR nt = Lock ((CONST_STRPTR)ffe_pd ("ffe_nomusthread"), ACCESS_READ);	/* diagnostic */
		AmigaAudio_MusicSetVolume (64);	/* the streams peak around half scale; 48 was quiet */
		if (nt) UnLock (nt);
		g_thread = nt ? 0 : AmigaAudio_MusicThreadStart (1);
		fprintf (stdout, "ffe: music mode %d, refill %s\n", g_mode,
		         g_thread ? "by its own process" : "from the game loop");
	}
	fflush (stdout);
}

int ffe_music_available (void) { return g_mode != 0; }

void ffe_music_stop (void)
{
	if (!g_playing) return;
	AmigaAudio_MusicStop ();
	fprintf (stdout, "ffe: music: stopped, %lu underruns so far\n", AmigaAudio_MusicUnderruns ());
	fflush (stdout);
	if (g_ima) { ima_close (g_ima); g_ima = NULL; }
	if (g_live) {
		g_live->syn->destroy (g_live->syn->ctx);
		hmp_free (g_live->song);
		free (g_live);
		g_live = NULL;
	}
	g_playing = 0;
}

void ffe_music_play (long index)
{
	unsigned long clock = amigagfx_is_ntsc () ? NTSC_CLOCK : PAL_CLOCK;
	char p[160];
	int ok = 0;

	ffe_music_stop ();
	if (!g_mode || index < 0 || index >= 17) return;
	if (g_mode <= 2) {
		song_path (p, sizeof p, (int)index);
		g_ima = ima_open (p);
		if (g_ima) ok = AmigaAudio_MusicStart ((int)(clock / (unsigned long)g_ima->rate), MUS_CHUNK, ima_refill, g_ima);
		if (!ok && g_ima) { ima_close (g_ima); g_ima = NULL; }
	} else {
		HmpSong *s = NULL;
		snprintf (p, sizeof p, "%sMUSIC/%s.HMP", data_dir (), songs[index]);
		s = hmp_load_file (p);
		g_live = s ? (Live *)calloc (1, sizeof *g_live) : NULL;
		if (g_live) {
			g_live->song = s;
			g_live->rate = ffe_music_rate;
#ifdef FFE_HAVE_GM
			g_live->syn = g_mode == 4 ? gm_synth_create ((int)ffe_music_rate, g_bank, g_blen, 12)
			                          : adlib_midi_create ((int)ffe_music_rate, g_mel, g_mlen, g_drm, g_dlen, 0);
#else
			g_live->syn = adlib_midi_create ((int)ffe_music_rate, g_mel, g_mlen, g_drm, g_dlen, 0);
#endif
		}
		if (g_live && g_live->syn)
			ok = AmigaAudio_MusicStart ((int)(clock / (unsigned long)ffe_music_rate), MUS_CHUNK, live_refill, g_live);
		if (!ok) {
			if (g_live && g_live->syn) g_live->syn->destroy (g_live->syn->ctx);
			if (s) hmp_free (s);
			free (g_live); g_live = NULL;
		}
	}
	g_playing = ok;
	fprintf (stdout, "ffe: music: song %ld (%s) %s\n", index, songs[index], ok ? "playing" : "FAILED");
	fflush (stdout);
}

long ffe_music_done (void)
{
	if (!g_playing) return 1;
	if (!AmigaAudio_MusicFinished ()) return 0;
	ffe_music_stop ();
	return 1;
}

void ffe_music_service (void)
{
	/* without the refill process the game loop has to do it */
	if (g_playing && !g_thread) AmigaAudio_MusicService ();
}

void ffe_music_cleanup (void)
{
	ffe_music_stop ();
	if (g_thread) { AmigaAudio_MusicThreadStop (); g_thread = 0; }
	free (g_mel); free (g_drm); free (g_bank);
	g_mel = g_drm = g_bank = NULL;
	g_mode = 0;
}
