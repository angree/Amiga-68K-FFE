/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 angree. See LICENSE at the repository root.
 *
 * ffe-music (2026-10-24): renders the game's own music -- the 17 HMI songs
 * in the original DOS FFE/MUSIC/<song>.HMP -- into IMA ADPCM files that the game
 * streams to Paula/AHI while it plays ("pre-rendered" music modes). Runs on
 * the Amiga (once; it takes a while on a 68020) or on a PC (the same source,
 * see tools/music/build_render.sh), so no game data ever ships with AmiFFE.
 *
 *   ffe-music ADLIB [RATE=11025|22050] [Q=0|1] [IN=FFE] [OUT=Music]
 *   ffe-music GM    [RATE=...] [BANK=ffe_gm.bnk]
 * Q=1 is the accurate OPL2 model (the PC default: seconds for all 17 songs);
 * Q=0 is the cheap one, the Amiga default with RATE=11025 -- Q=1 at 22 kHz
 * would keep a 68020 busy for hours. IN= defaults to where the game finds its
 * data: PROGDIR:FFE, Elite3, FIRSTENC, PROGDIR: or the volume FFE:.
 *
 * ADLIB plays the songs on an OPL2 (AdLib) model with the game's own
 * MELODIC.BNK / DRUM.BNK -- what a 1995 PC with a Sound Blaster heard.
 * GM plays them with a General MIDI bank built from CC0 samples.
 * Output: OUT/AdLib/<song>.wav or OUT/GM/<song>.wav, mono, 4-bit ADPCM.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef __amigaos__
#include <proto/dos.h>
const char *ffe_pd (const char *name);	/* ffeapi/ffe_progdir.c: this program's folder */
#else
#include <sys/stat.h>
static const char *ffe_pd (const char *name) { return name; }
#endif

#include "../music/midi_synth.h"
#include "../music/hmp.h"
#include "../music/adlib_midi.h"
#ifdef FFE_HAVE_GM
#include "../music/gm_synth.h"
#endif
#include "../music/ima_enc.h"

#ifdef __amigaos__
static const char ver[] = "$VER: ffe-music 0.2.0 (30.10.2026)";
#endif

/* SoundPlaySong's index order (the game's own table, as in JJFFE) */
static const char *songs[17] = {
	"qqfront", "qqatmosp", "qqtravel", "qqdrama", "qqsuspen", "qqrock",
	"qqescape", "qqparadi", "qqfront2", "hallking", "intro", "babayaga",
	"barem", "jupalt", "valkries", "bluedan", "ggofkiev"
};

static unsigned char *slurp (const char *path, long *len)
{
	FILE *f = fopen (path, "rb");
	unsigned char *p;
	long n;

	if (f == NULL) return NULL;
	fseek (f, 0, SEEK_END); n = ftell (f); fseek (f, 0, SEEK_SET);
	p = (unsigned char *)malloc (n > 0 ? n : 1);
	if (p != NULL && fread (p, 1, n, f) != (size_t)n) { free (p); p = NULL; }
	fclose (f);
	*len = n;
	return p;
}

/* dir + "/" + name, without the "/" after a volume ("PROGDIR:/x" is the
 * PARENT directory on AmigaDOS) */
static void join (char *out, int n, const char *dir, const char *name)
{
	size_t l = strlen (dir);
	int sep = l > 0 && dir[l - 1] != ':' && dir[l - 1] != '/';
	snprintf (out, n, "%s%s%s", dir, sep ? "/" : "", name);
}

static void make_dir (const char *path)
{
#ifdef __amigaos__
	BPTR l = CreateDir ((CONST_STRPTR)path);
	if (l) UnLock (l);
#else
	mkdir (path, 0755);
#endif
}

static const char *arg (int argc, char **argv, const char *key, const char *def)
{
	size_t n = strlen (key);
	int i;

	for (i = 1; i < argc; i++)
		if (strncmp (argv[i], key, n) == 0) return argv[i] + n;
	return def;
}

int main (int argc, char **argv)
{
	const char *in, *out, *mode;
	char path[256], dir[256];
	int rate, gm, i, done = 0, quality;
	long total_ms = 0, done_ms = 0;
	time_t t_start;
	unsigned char *mel = NULL, *drm = NULL, *bank = NULL;
	long mlen = 0, dlen = 0, blen = 0;
	static short pcm[4096];

#ifdef __amigaos__
	(void)ver;
#endif
	if (argc < 2 || strcmp (argv[1], "?") == 0) {
		printf ("ffe-music ADLIB|GM [RATE=11025|22050] [IN=<dir with MUSIC/>] [OUT=<dir>] [BANK=<gm bank>]\n");
		return 0;
	}
	mode = argv[1];
	gm = (mode[0] == 'G' || mode[0] == 'g');
#ifdef __amigaos__
	rate = atoi (arg (argc, argv, "RATE=", "11025"));
	quality = atoi (arg (argc, argv, "Q=", "0"));
	in = arg (argc, argv, "IN=", NULL);
	if (in == NULL) {
		static char dirs_buf[4][260];
		static const char *dirs[6];
		static const char *const names[4] = { "FFE", "Elite3", "FIRSTENC", "" };
		for (i = 0; i < 4; i++) { snprintf (dirs_buf[i], sizeof dirs_buf[i], "%s", ffe_pd (names[i])); dirs[i] = dirs_buf[i]; }
		dirs[4] = "FFE:"; dirs[5] = NULL;
		BPTR l;
		for (i = 0; dirs[i] != NULL; i++) {
			snprintf (path, sizeof path, "%s%sMELODIC.BNK", dirs[i],
			          (dirs[i][strlen (dirs[i]) - 1] == ':' || dirs[i][strlen (dirs[i]) - 1] == '/') ? "" : "/");
			if ((l = Lock ((CONST_STRPTR)path, ACCESS_READ)) != 0) { UnLock (l); break; }
		}
		in = dirs[i] != NULL ? dirs[i] : dirs[0];
	}
	{ static char ob[260]; snprintf (ob, sizeof ob, "%s", ffe_pd ("Music")); out = arg (argc, argv, "OUT=", ob); }
#else
	rate = atoi (arg (argc, argv, "RATE=", "22050"));
	quality = atoi (arg (argc, argv, "Q=", "1"));
	in = arg (argc, argv, "IN=", "FFE");
	out = arg (argc, argv, "OUT=", "Music");
#endif
	if (rate != 11025 && rate != 22050) rate = 11025;
	quality = quality ? 1 : 0;
	if (gm) {
#ifdef FFE_HAVE_GM
		bank = slurp (arg (argc, argv, "BANK=",
#ifdef __amigaos__
		                   ffe_pd ("ffe_gm.bnk")
#else
		                   "ffe_gm.bnk"
#endif
		                   ), &blen);
		if (bank == NULL) { printf ("ffe-music: no GM bank (BANK=)\n"); return 10; }
#else
		printf ("ffe-music: built without the GM synth\n");
		return 10;
#endif
	} else {
		join (path, sizeof path, in, "MELODIC.BNK"); mel = slurp (path, &mlen);
		join (path, sizeof path, in, "DRUM.BNK"); drm = slurp (path, &dlen);
		if (mel == NULL || drm == NULL) {
			printf ("ffe-music: %s/MELODIC.BNK and DRUM.BNK are needed (the original DOS game folder)\n", in);
			return 10;
		}
	}
	make_dir (out);
	join (dir, sizeof dir, out, gm ? "GM" : "AdLib");
	make_dir (dir);

	/* PROGRESS (2026-10-24, developer: "a minute or an hour -- how is the user
	 * to know?"): the total length of the work is known up front (the songs'
	 * lengths plus their release tails), so the console line shows the song
	 * and the whole run in per cent, the time so far and an estimate of the
	 * rest, rewritten in place with a carriage return. */
	{
		long k;
		for (k = 0; k < 17; k++) {
			const char *only = arg (argc, argv, "SONG=", NULL);
			char nm[40];
			HmpSong *s;
			if (only != NULL && strcmp (only, songs[k]) != 0) continue;
			snprintf (nm, sizeof nm, "MUSIC/%s.HMP", songs[k]);
			join (path, sizeof path, in, nm);
			if ((s = hmp_load_file (path)) == NULL) continue;
			total_ms += hmp_length_usec (s) / 1000 + 1500;
			hmp_free (s);
		}
		t_start = time (NULL);
		printf ("ffe-music: %s, %d Hz, %ld:%02ld of music to render into %s\n",
		        gm ? "General MIDI" : (quality ? "AdLib (accurate OPL2)" : "AdLib (fast OPL2)"),
		        rate, total_ms / 60000, (total_ms / 1000) % 60, dir);
		fflush (stdout);
	}

	for (i = 0; i < 17; i++) {
		const char *only = arg (argc, argv, "SONG=", NULL);	/* one song only (tests) */
		HmpSong *s;

		if (only != NULL && strcmp (only, songs[i]) != 0) continue;
		MidiSynth *syn;
		ImaWriter *w;
		long acc = 0, ms = 0, tail, song_ms;
		int last_pct = -1;

		{ char nm[40]; snprintf (nm, sizeof nm, "MUSIC/%s.HMP", songs[i]); join (path, sizeof path, in, nm); }
		s = hmp_load_file (path);
		if (s == NULL) { printf ("ffe-music: %s missing, skipped\n", path); continue; }
#ifdef FFE_HAVE_GM
		syn = gm ? gm_synth_create (rate, bank, blen, 24)
		         : adlib_midi_create (rate, mel, mlen, drm, dlen, quality);
#else
		syn = adlib_midi_create (rate, mel, mlen, drm, dlen, quality);
#endif
		if (syn == NULL) { printf ("ffe-music: synth refused\n"); hmp_free (s); return 20; }
		{ char nm[40]; snprintf (nm, sizeof nm, "%s.wav", songs[i]); join (path, sizeof path, dir, nm); }
		w = ima_open (path, rate);
		if (w == NULL) { printf ("ffe-music: cannot write %s\n", path); return 20; }
		song_ms = hmp_length_usec (s) / 1000 + 1500;
		/* 1 ms steps: the sequencer dispatches what is due, the synth renders
		 * rate/1000 samples (remainder carried, so the length is exact) */
		tail = 0;
		for (;;) {
			int playing = hmp_advance (s, 1000, syn);
			int n;

			acc += rate;
			n = (int)(acc / 1000);
			acc -= (long)n * 1000;
			syn->render (syn->ctx, pcm, n);
			ima_write (w, pcm, n);
			ms++;
			if (!playing && ++tail > 1500) break;	/* 1.5 s of release after the last event */
			/* once per second of WALL time (a 1 % step of the whole run can be
			 * minutes on a 68020 -- the line looked frozen at 0 %) */
			if ((ms & 63) == 0 && total_ms > 0) {
				long el = (long)(time (NULL) - t_start);
				if (el != last_pct) {
					long all = done_ms + ms;
					long pm = all * 10 / (total_ms / 100);	/* per mille */
					last_pct = (int)el;
					printf ("\r%-9s %ld:%02ld / %ld:%02ld   all %ld.%ld%%   %ld:%02ld so far",
					        songs[i], ms / 60000, (ms / 1000) % 60, song_ms / 60000, (song_ms / 1000) % 60,
					        pm / 10, pm % 10, el / 60, el % 60);
					if (el >= 5 && all >= 1000) {
						/* in seconds, so el * rest stays within 32 bits */
						long left = el * ((total_ms - all) / 1000) / (all / 1000);
						printf (", about %ld:%02ld:%02ld left   ", left / 3600, (left / 60) % 60, left % 60);
					} else
						printf (", measuring speed...   ");
					fflush (stdout);
				}
			}
		}
		ima_close (w);
		syn->destroy (syn->ctx);
		hmp_free (s);
		done_ms += ms;
		printf ("\r%-9s done (%ld:%02ld of music)                                        \n",
		        songs[i], ms / 60000, (ms / 1000) % 60);
		done++;
	}
	free (mel); free (drm); free (bank);
	printf ("ffe-music: %d songs in %s\n", done, dir);
	return done ? 0 : 5;
}
