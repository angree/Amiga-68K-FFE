/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 angree. See LICENSE at the repository root.
 *
 * Drives the FFE Paula sound layer (amiga/ffeapi/ffe_sound.c) with no game code
 * present: bring audio up, load the sample set, then play a scripted sequence
 * that exercises everything the four-channel policy has to get right.
 *
 * WHAT A CORRECT RUN SOUNDS LIKE -- the log proves the bookkeeping, the ear has
 * to prove the rest:
 *   - six one-shots in a row, each on a different channel, alternating between
 *     the left and the right jack (channels 0 and 3 are left, 1 and 2 right);
 *   - then the engine noise, which must keep sounding for several seconds
 *     without being re-triggered (loopcount 0x32, re-armed by
 *     ffe_sound_service) and must CHANGE PITCH AT ONCE, twice, rather than
 *     start a second copy of itself;
 *   - the station hum joins it; the two wind voices must be REFUSED, because
 *     only two channels may be held by sustained sounds;
 *   - two lasers fire over the top of them and must be heard -- that is the
 *     whole point of the cap;
 *   - an explosion arrives with all four channels busy and must be heard
 *     IMMEDIATELY: it takes the least important channel away from it mid-pass;
 *   - SoundStopAllSamples, after which everything is silent at once -- no
 *     drain, nothing plays itself out.
 *
 * The log records the channel occupancy after every step, and the nine things
 * that must be true of it are asserted as PASS/FAIL lines, so the policy is
 * checkable without listening. Every one of them is checked in the same breath
 * as the call that should have made it true: since amiga_audio.c grew
 * AmigaAudio_Stop, nothing in the sound layer is deferred to a later tick.
 *
 * Samples are loaded from "fx/" relative to the current directory, forced with
 * ffe_sound_set_sample_path(): the layer normally asks ffe_dir.c for the [DIR]
 * SamplePath key, and this probe deliberately runs without a config file.
 * Results go to PROGDIR:ffe_sndtest.log, one fflush per line: the run script
 * detaches us with `Run >NIL: <NIL:` and no shell redirection survives that.
 * Same reason ffe_apitest.c logs the way it does.
 *
 * Runs for about ten seconds -- two less than it used to, because nothing waits
 * for a queue to fire or for a channel to drain any more -- and exits on its
 * own. Its exit code is 5 if a sample would not load and 10 if an assertion
 * failed.
 */

#include <exec/types.h>
#include <proto/dos.h>

#include <stdio.h>
#include <stdarg.h>
#include <string.h>

#include "ffeapi/ffeapi.h"

#define NUM_SAMPLES 0x28	/* the 40 slots ffe_sound.c keeps */
#define NUM_CHANS   4		/* Paula */

/* Volumes are on the game's 0..0x7fff scale (FUNC_001908 shifts its internal
 * 0..63 left by nine). 0x4000 is half, and audible without being a hazard. */
#define VOL_NORMAL  0x4000
#define VOL_LOUD    0x6000

static FILE *g_log;
static int   g_fails;			/* assertions that did not hold */

static void logf_ (const char *fmt, ...)
{
	va_list ap;
	if (g_log == NULL) return;
	va_start (ap, fmt);
	vfprintf (g_log, fmt, ap);
	va_end (ap);
	fputc ('\n', g_log);
	fflush (g_log);			/* a Guru must not cost us the last line */
}

/* ---- helpers ------------------------------------------------------------ */

/* Wait n AmigaDOS ticks (1/50 s), servicing the sound layer the way the game
 * loop will. Nothing loops or frees a channel without this call. */
static void nap (int ticks)
{
	while (ticks-- > 0) {
		ffe_sound_service ();
		Delay (1);
	}
}

static long chan_mask (void)
{
	long mask = 0;
	int ch;
	for (ch = 0; ch < NUM_CHANS; ch++)
		if (ffe_sound_chan_info (ch, NULL, NULL)) mask |= 1L << ch;
	return mask;
}

/* How many channels are playing this sample slot right now. Used both ways: a
 * steal must make it 1, and a retune must KEEP it 1. */
static int chan_count (long slot)
{
	int ch, n = 0;

	for (ch = 0; ch < NUM_CHANS; ch++) {
		long index = -1;
		if (ffe_sound_chan_info (ch, &index, NULL) && index == slot) n++;
	}
	return n;
}

/* One thing that must be true of the channel map, checked immediately -- the
 * sound layer no longer defers anything, so there is never a reason to wait
 * before asking. */
static void expect (int ok, const char *what)
{
	if (!ok) g_fails++;
	logf_ ("  %-5s %s", ok ? "PASS" : "FAIL:", what);
}

static void log_chans (const char *when)
{
	char buf[96];
	int ch, n = 0;

	for (ch = 0; ch < NUM_CHANS; ch++) {
		long index = -1, prio = 0;
		ffe_sound_chan_info (ch, &index, &prio);
		n += sprintf (buf + n, "  ch%d=%3ld/%ld", ch, index, prio);
	}
	logf_ ("  after %-26s%s     (slot/priority, -1/0 = idle)", when, buf);
}

/* One scripted trigger, with the channel map before and after so a dropped or
 * stolen sound is visible in the log. */
static void play (const char *what, long index, long vol, long pitch)
{
	long before = chan_mask ();
	long after;

	SoundPlaySample (index, vol, pitch);
	after = chan_mask ();
	logf_ ("play %-24s slot 0x%02lx vol 0x%04lx pitch %6ld : channels 0x%lx -> 0x%lx%s",
	       what, index, vol, pitch, before, after,
	       (before == after) ? "  (same mask - dropped, retuned, or stolen in place)" : "");
}

/* ---- the sample set report ---------------------------------------------- */

/* Every slot, with the size found on disk and the length that came out of the
 * 16-bit-LE -> 8-bit conversion. The second must be half the first rounded down
 * to an even number; anything else means that file is not what we assumed and
 * will sound like noise. */
static void report_samples (int *ploaded, int *pfailed, int *psilent, int *pshared)
{
	int i;

	*ploaded = *pfailed = *psilent = *pshared = 0;

	logf_ ("---- sample set (fx/<name>.raw, 16-bit signed LE 22050 Hz) ----");
	logf_ ("slot name      file bytes  chip bytes  expect  state");

	for (i = 0; i < NUM_SAMPLES; i++) {
		const char *name = "?";
		long filebytes = 0, chipbytes = 0, expect;
		int state = ffe_sound_sample_info (i, &name, &filebytes, &chipbytes);
		const char *what;

		if (state == 0) { (*psilent)++; continue; }	/* "nowt", never loaded */

		expect = (filebytes / 2) & ~1L;
		if (state < 0) {
			(*pfailed)++;
			what = "FAILED to load";
		} else {
			(*ploaded)++;
			what = (chipbytes == expect) ? "ok" : "!! NOT half the file size";
		}

		logf_ ("0x%02x %-9s %10ld  %10ld  %6ld  %s",
		       i, name, filebytes, chipbytes, expect, what);
	}

	/* How much the shared-buffer loading saved: the reference loads one copy
	 * per slot, we load one per distinct file name. */
	{
		long perslot = 0;
		for (i = 0; i < NUM_SAMPLES; i++) {
			long cb = 0;
			if (ffe_sound_sample_info (i, NULL, NULL, &cb) == 1) perslot += cb;
		}
		*pshared = (int)((perslot - ffe_sound_chip_bytes ()) / 1024);
		logf_ ("slot-by-slot loading would cost %ld bytes; sharing costs %ld",
		       perslot, ffe_sound_chip_bytes ());
	}
}

/* ---- main --------------------------------------------------------------- */

int main (void)
{
	long all = 0, digi = 0, midi = 0;
	int loaded = 0, failed = 0, silent = 0, savedkb = 0;
	static UCHAR dummyblock[64];

	g_log = fopen (ffe_pd ("ffe_sndtest.log"), "w");
	logf_ ("FFE Paula sound layer probe -- amiga/ffeapi/ffe_sound.c, no game code");

	/* The timer first, the same order SystemInit will use. No screen is opened:
	 * this probe has nothing to show.
	 *
	 * The sample path is forced to "fx/" because no config file is read here --
	 * without the override ffe_sound.c would ask ffe_dir.c, whose SamplePath is
	 * empty until ffe_dir_init() has run, and every sample would be looked for
	 * in the current directory. */
	ffe_timer_init ();
	ffe_sound_set_sample_path ("fx/");
	ffe_sound_init ();

	SoundCheckInit (&all, &digi, &midi);
	logf_ ("SoundCheckInit: all=%ld digi=%ld midi=%ld  (midi is always 0 on Paula)",
	       all, digi, midi);
	if (!all) {
		logf_ ("FAIL: audio did not come up - Paula is held by something else, or");
		logf_ ("      audio.device is missing. Nothing below would prove anything.");
		logf_ ("---- summary ----");
		logf_ ("audio came up: NO");
		ffe_sound_cleanup ();
		ffe_timer_cleanup ();
		if (g_log) fclose (g_log);
		return 20;
	}

	report_samples (&loaded, &failed, &silent, &savedkb);
	logf_ ("loaded %d, failed %d, silent by design %d, of %d slots",
	       loaded, failed, silent, NUM_SAMPLES);
	logf_ ("Chip RAM held by the sample set: %ld bytes (%ld KB)",
	       ffe_sound_chip_bytes (), ffe_sound_chip_bytes () / 1024);

	/* ---- one-shots: six in a row, each should take a channel of its own and
	 *      the round robin should alternate the left and right jacks. */
	logf_ ("---- one-shots ----");
	play ("laser1",  0x0a, VOL_NORMAL, -1); nap (18);
	play ("laser2",  0x0b, VOL_NORMAL, -1); nap (18);
	play ("bip",     0x14, VOL_NORMAL, -1); nap (20);
	log_chans ("four one-shots");
	play ("bong",    0x16, VOL_NORMAL, -1); nap (22);
	play ("chime",   0x17, VOL_NORMAL, -1); nap (24);
	play ("camera",  0x27, VOL_NORMAL, -1); nap (24);
	log_chans ("one-shot burst");

	/* ---- the looping voice. Slot 0x10 is the engine noise: station.raw on
	 *      voice 2 with loopcount 0x32, i.e. 51 passes of ~0.3 s. It must stay
	 *      up without being re-triggered, and a second call must retune it --
	 *      by stopping and restarting the same channel, so the new pitch is
	 *      heard now and not at the next re-arm. */
	logf_ ("---- looping voice: engine noise (slot 0x10, voice 2) ----");
	play ("engine start", 0x10, VOL_NORMAL, -1);
	log_chans ("engine started");
	expect (chan_count (0x10) == 1, "engine took exactly one channel");
	nap (30);
	log_chans ("1.2 s later (re-armed)");
	expect (chan_count (0x10) == 1, "engine still up 1.2 s on, without being re-triggered");
	play ("engine retune down", 0x10, VOL_NORMAL, 12000);
	log_chans ("retune down");
	expect (chan_count (0x10) == 1, "retune restarted the voice in place, no second copy");
	nap (30);
	play ("engine retune up",   0x10, VOL_LOUD,   40000);
	log_chans ("retune up");
	expect (chan_count (0x10) == 1, "second retune likewise, still one channel");
	nap (30);
	log_chans ("two retunes");

	/* ---- the sustained cap. Engine already holds one channel; the station hum
	 *      may take the second, and BOTH winds must then be refused. */
	logf_ ("---- sustained cap: at most two channels for classes 1 and 2 ----");
	play ("station hum",    0x0f, VOL_NORMAL, -1);
	play ("wind (voice 8)", 0x0e, VOL_NORMAL, -1);
	play ("wind (voice 7)", 0x25, VOL_NORMAL, -1);
	log_chans ("3 ambients requested");
	expect (chan_count (0x0e) == 0 && chan_count (0x25) == 0,
	        "both winds refused: classes 1+2 already hold their two channels");

	/* ---- and the reason for the cap: one-shots still get through. */
	play ("laser3", 0x23, VOL_NORMAL, -1);
	play ("laser4", 0x24, VOL_NORMAL, -1);
	log_chans ("2 ambients + 2 lasers");
	expect (chan_mask () == 0x0f, "all four channels busy: 2 ambients + 2 lasers");

	/* ---- channel stealing. All four are busy and an ALERT arrives; it must
	 *      take the least important channel mid-pass and be playing before
	 *      SoundPlaySample returns. The very next state dump is the assertion:
	 *      no nap, no second chance. */
	logf_ ("---- stealing: an ALERT with all four channels busy ----");
	play ("shipexp (ALERT)", 0x05, VOL_LOUD, -1);
	log_chans ("alert requested");
	expect (chan_count (0x05) == 1, "ALERT audible AT ONCE: it stole a channel in the same call");
	play ("boom (ALERT, voice 6)", 0x1f, VOL_LOUD, -1);
	log_chans ("boom requested");
	expect (chan_count (0x1f) == 1, "second ALERT stole again, also without waiting");
	nap (40);
	log_chans ("1.6 s later");

	/* ---- stop everything. Every channel is silenced on the spot: there is no
	 *      drain, the wind pass is not allowed to play itself out. */
	logf_ ("---- SoundStopAllSamples ----");
	SoundStopAllSamples ();
	log_chans ("SoundStopAllSamples");
	expect (chan_mask () == 0, "every channel idle IMMEDIATELY, with no drain wait");

	/* ---- the stubs. SoundStreamWait is the dangerous one: the reference spins
	 *      on a pointer, and a stub that spun would hang the game. */
	logf_ ("---- music and speech stubs ----");
	SoundPlaySong (0);
	logf_ ("SoundSongDone() = %ld (0 expected: music is a stub)", SoundSongDone ());
	SoundStopSong ();
	SoundStreamReset ();
	memset (dummyblock, 0, sizeof (dummyblock));
	SoundStreamAddBlock (dummyblock, (long)sizeof (dummyblock));
	logf_ ("SoundStreamGetUsedBlocks() = %ld (0 expected)", SoundStreamGetUsedBlocks ());
	SoundStreamStart ();
	SoundStreamWait ();			/* must return immediately, never spin */
	SoundStreamStop ();
	logf_ ("stream stubs returned, SoundStreamWait did not spin");

	logf_ ("---- summary ----");
	logf_ ("audio came up: yes (all=%ld digi=%ld midi=%ld)", all, digi, midi);
	logf_ ("samples loaded: %d", loaded);
	logf_ ("samples failed: %d", failed);
	logf_ ("slots silent by design (\"nowt\"): %d", silent);
	logf_ ("Chip RAM used by the sample set: %ld bytes (%ld KB), %d KB saved by sharing",
	       ffe_sound_chip_bytes (), ffe_sound_chip_bytes () / 1024, savedkb);
	logf_ ("channels still busy at the end: 0x%lx (0 expected)", chan_mask ());
	logf_ ("policy assertions failed: %d (0 expected)", g_fails);

	ffe_sound_cleanup ();
	ffe_timer_cleanup ();
	logf_ ("ffe_sound_cleanup done, closed cleanly");
	if (g_log) fclose (g_log);
	if (failed) return 5;
	return g_fails ? 10 : 0;
}
