/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 angree. See LICENSE at the repository root.
 *
 * FFE's sound API on Paula: samples, and the stubs for music and speech.
 *
 * Modelled on ref/jjffe/sdl/sdlsound.cpp -- the 40-entry descriptor table, the
 * pitch arithmetic, the fixed-voice rule and the engine-noise retune are all
 * that file's, reproduced here. What is NOT that file's is the mixer: SDL gets
 * sixteen software channels resampled into a 44.1 kHz stream, we get four DMA
 * channels that play one buffer each at one period each. Everything below that
 * is not a straight copy exists because of that difference.
 *
 * NO AMIGA HEADERS: everything hardware lives behind amiga_audio.h, which
 * exposes nothing but ints and void pointers. Same rule as ffe_video.c.
 *
 * ---- sample format ------------------------------------------------------
 *
 * <SamplePath>/<name>.raw is 16-bit SIGNED LITTLE-ENDIAN mono at 22050 Hz.
 * The directory is the [DIR] SamplePath key, reached through ffe_dir.c's
 * DirMakeSampleName() -- the same builder the game itself would use -- so the
 * samples follow the config rather than a hard-coded "fx/". Paula plays
 * 8-bit signed, so the conversion is "keep the high byte of each pair", and
 * because the file is little endian the high byte is the ODD one -- offsets
 * 1, 3, 5... The length halves. Done once at load, straight into Chip RAM;
 * nothing converts at play time.
 *
 * Verified against the 22 non-empty files in the shipped fx/ directory: for
 * every one of them the mean absolute first difference is far smaller read as
 * 16-bit LE than read as 16-bit BE or as 8-bit (LASER1.RAW, which an earlier
 * check flagged as suspicious, is in fact the SMOOTHEST of the set). The
 * loader still records each file's size and the length it produced, so a file
 * that does not obey the rule is visible in the probe log rather than merely
 * audible as noise.
 *
 * Identical names are loaded ONCE and shared: airopen appears in four slots,
 * station, wind, bip, shipexp and missexpl in two each. Slot-by-slot loading
 * the way the reference does it costs 644 KB of Chip RAM; sharing costs 372 KB.
 * On a machine with ~1.6 MB of Chip free that is not a micro-optimisation.
 * Nothing ever writes to sample data, and two Paula channels reading the same
 * buffer is fine, so the sharing is safe.
 *
 * ---- FOUR channels, not sixteen: the priority policy ---------------------
 *
 * The reference reserves logical channel 0 for music and 1 for speech, gives
 * the eight "fixed voice" sounds (engine, station hum, two winds, ecm, necm,
 * siren, boom) a channel each, and lets one-shots fight over 10..15 -- falling
 * back to stealing whichever channel is playing laser2, the one sound it is
 * willing to cut off. Sixteen channels means it essentially never has to
 * choose. We have four and have to choose constantly.
 *
 * Every slot gets one of four priority classes:
 *
 *   4 ALERT   explosions, damage, siren, ecm, necm, boom -- sounds that tell
 *             the player something just went wrong. Missing one is a gameplay
 *             bug, not a cosmetic one.
 *   3 EVENT   lasers, beeps, bong, chime, airlock, launch, hyperspace, camera.
 *             Short, self-terminating, and the texture of playing the game.
 *   2 ENGINE  slot 16 only: the thrust noise. Sustained, but it is the only
 *             continuous feedback the player has for the throttle, so it
 *             outranks the other ambiences.
 *   1 BACK    station hum and the two winds. Atmosphere. Pleasant, expendable.
 *
 * Two rules, and the first one does the real work:
 *
 * (a) SUSTAINED CAP. Classes 1 and 2 together may hold at most two of the four
 *     channels (SND_MAX_SUSTAINED). Ambient voices are the only sounds that
 *     would otherwise sit on a channel indefinitely -- engine and station hum
 *     re-arm fifty times, one wind pass runs five seconds -- so capping them
 *     is what guarantees a laser or an explosion always has somewhere to go.
 *     Half the hardware is the least that works: a firefight routinely wants
 *     two one-shots at once (the shot and what it hit), and on four channels
 *     anything stricter than half would let the engine hum drown the fight.
 *     A sustained request over the cap is simply dropped; the game re-issues
 *     engine and wind every frame, so its own polling is the retry.
 *
 * (b) IMMEDIATE PRE-EMPTION. A newcomer that finds no idle channel TAKES one:
 *     the least important channel below its own class (lowest class, oldest
 *     start on a tie -- the oldest is nearest to finishing and has already
 *     been heard) is silenced with AmigaAudio_Stop and the newcomer starts on
 *     it in the same call. An explosion is therefore audible in the frame that
 *     asked for it, which is the whole point: a laser or a hull hit that
 *     arrives half a second after the event it belongs to is worse than
 *     silence. A newcomer that finds nothing below its own class is dropped --
 *     exactly what the reference does when its own slot search fails.
 *
 *     The cost of a steal, worst case, is one AbortIO + WaitIO pair on the
 *     victim's request; audio.device replies an aborted write immediately, so
 *     WaitIO cannot block. There is no queue and no deferred state anywhere in
 *     this file: a sound either plays now or is dropped now.
 *
 * Idle channels are handed out round robin in the order 0, 1, 3, 2, which
 * alternates Paula's jacks (0 and 3 are LEFT, 1 and 2 are RIGHT), so
 * consecutive sounds spread across the stereo image instead of piling into one
 * ear.
 *
 * ---- what Paula does not do directly ------------------------------------
 *
 * Looping. AmigaAudio_Play sets ioa_Cycles = 1, so a descriptor loopcount is
 * honoured by RE-ARMING the channel from ffe_sound_service(), which must
 * therefore be called regularly -- once per frame from TimerFrameUpdate is the
 * intended home. Every public entry point here services first, so the layer
 * still limps along if nobody wires that up.
 *
 * Retuning. The reference changes a playing channel's step and volume in
 * place; audio.device would need ADCMD_PERVOL, which amiga_audio.h does not
 * expose. The engine retune (slot 16) therefore stops the channel and starts
 * the same sample again at the new period and volume, so the new pitch is
 * heard at once. What it costs is that station.raw restarts from its first
 * byte -- inaudible on a hum with no attack, and the throttle note is what the
 * player is actually listening to.
 *
 * Stopping. AmigaAudio_Stop(ch) aborts the write in flight, so
 * SoundStopAllSamples silences every non-music channel outright instead of
 * letting a looping wind sample drain for seconds.
 *
 * Top speed. Paula's PAL floor is period 124, about 28.6 kHz. The game drives
 * the engine noise up past 65 kHz (FUNC_001910 hands SoundPlaySample a pitch
 * of 0x10000 plus change), so at high thrust it saturates a bit over an octave
 * below the DOS version. There is no fix short of resampling at load time.
 *
 * ---- music and speech ---------------------------------------------------
 *
 * Both are stubs, as they are in the SDL build. See SoundPlaySong and
 * SoundStreamReset below for the details and for where a Paula implementation
 * would attach.
 */

#include <stdio.h>
#include <string.h>

#include "ffeapi.h"
#include "amiga_audio.h"

/* ---- constants ---------------------------------------------------------- */

#define SND_NUM_SAMPLES  0x28		/* 40 slots, as in sdlsound.cpp */
#define SND_NUM_VOICES   16		/* logical voices the descriptors index */
#define SND_ENGINE       0x10		/* the one slot that retunes instead of dropping */
#define SND_RATE         22050L		/* what the .raw files were sampled at */
#define SND_PAL_CLOCK    3546895UL	/* Paula period = clock / Hz, PAL */
#define SND_NTSC_CLOCK   3579545UL	/* ... and NTSC (amigagfx_is_ntsc, the prefs) */
extern int amigagfx_is_ntsc (void);
#define SND_MIN_PERIOD   124		/* ~28.6 kHz, the PAL DMA ceiling */
#define SND_MAX_BYTES    131070L	/* Paula's length register, in bytes */
#define SND_MAX_SUSTAINED 2		/* channels classes 1+2 may hold at once */
#define SND_NAME_MAX     16		/* "<name>.raw" and its terminator */
#define SND_PATH_MAX     288		/* ffe_dir.c's DIR_PATHMAX (256) plus a name */

/* Priority classes; 0 means "this slot makes no sound at all". */
#define SND_P_SILENT 0
#define SND_P_BACK   1
#define SND_P_ENGINE 2
#define SND_P_EVENT  3
#define SND_P_ALERT  4

/* ---- the sample set ----------------------------------------------------- */

/* One row per slot, the reference's table plus our priority class. Its `flags`
 * field is dropped: it is 0 in all forty entries and the SDL mixer never reads
 * it. loopcount is the number of EXTRA passes -- 0 plays once, 0x32 plays 51
 * times -- which is what the reference's `if (loop-- == 0) stop` amounts to. */
typedef struct {
	const char *name;	/* fx/<name>.raw; "nowt" is silence and is never loaded */
	short loopcount;
	short voice;		/* logical voice this slot owns, -1 = free pool */
	long  pitchmod;		/* added to the requested pitch, in Hz */
	short prio;
} snd_desc;

static const snd_desc g_desc[SND_NUM_SAMPLES] = {
	{ "nowt",     0,      -1,       0, SND_P_SILENT },	/* 0x00 */
	{ "nowt",     0,      -1,       0, SND_P_SILENT },
	{ "nowt",     0,      -1,       0, SND_P_SILENT },
	{ "nowt",     0,      -1,       0, SND_P_SILENT },
	{ "nowt",     0,      -1,       0, SND_P_SILENT },	/* 0x04 */
	{ "shipexp",  0,      -1,       0, SND_P_ALERT  },
	{ "missexpl", 0,      -1,       0, SND_P_ALERT  },
	{ "nowt",     0,      -1,       0, SND_P_SILENT },
	{ "nowt",     0,      -1,       0, SND_P_SILENT },	/* 0x08 */
	{ "nowt",     0,      -1,       0, SND_P_SILENT },
	{ "laser1",   0,      -1,       0, SND_P_EVENT  },
	{ "laser2",   0,      -1,       0, SND_P_EVENT  },	/* the reference's own steal victim */
	{ "airopen",  0,      -1,       0, SND_P_EVENT  },	/* 0x0c */
	{ "airopen",  0,      -1,       0, SND_P_EVENT  },
	{ "wind",     0,       8, -0x4000, SND_P_BACK   },
	{ "station",  0x32,    9,       0, SND_P_BACK   },	/* station hum */
	{ "station",  0x32,    2,  0x1000, SND_P_ENGINE },	/* 0x10 engine noise */
	{ "nowt",     0,      -1,       0, SND_P_SILENT },
	{ "airopen",  0,      -1,       0, SND_P_EVENT  },
	{ "airopen",  0,      -1,       0, SND_P_EVENT  },
	{ "bip",      0,      -1,       0, SND_P_EVENT  },	/* 0x14 */
	{ "bip",      0,      -1,       0, SND_P_EVENT  },
	{ "bong",     0,      -1,       0, SND_P_EVENT  },
	{ "chime",    0,      -1,       0, SND_P_EVENT  },
	{ "nowt",     0,      -1,       0, SND_P_SILENT },	/* 0x18 */
	{ "ecm",      2,       3,       0, SND_P_ALERT  },
	{ "necm",     2,       4,       0, SND_P_ALERT  },
	{ "shipexp",  0,      -1,       0, SND_P_ALERT  },
	{ "missexpl", 0,      -1,       0, SND_P_ALERT  },	/* 0x1c */
	{ "damage",   0,      -1,       0, SND_P_ALERT  },
	{ "siren",    3,       5,       0, SND_P_ALERT  },
	{ "boom",     0,       6,       0, SND_P_ALERT  },
	{ "launch",   0,      -1,       0, SND_P_EVENT  },	/* 0x20 */
	{ "hype",     0,      -1,       0, SND_P_EVENT  },
	{ "nowt",     0,      -1,       0, SND_P_SILENT },
	{ "laser3",   0,      -1,       0, SND_P_EVENT  },
	{ "laser4",   0,      -1,       0, SND_P_EVENT  },	/* 0x24 */
	{ "wind",     0,       7, -0x4000, SND_P_BACK   },
	{ "nowt",     0,      -1,       0, SND_P_SILENT },
	{ "camera",   0,      -1,       0, SND_P_EVENT  }
};

/* The seventeen song names of ref/jjffe/sdl/sdlsound.cpp are deliberately not
 * reproduced: nothing here can play them yet, and an unused 272-byte table
 * would only suggest otherwise. They come back with the music implementation. */

/* ---- state -------------------------------------------------------------- */

static signed char *g_data[SND_NUM_SAMPLES];	/* Chip RAM, 8-bit signed */
static long         g_len[SND_NUM_SAMPLES];	/* playable bytes, always even */
static long         g_filebytes[SND_NUM_SAMPLES];	/* what was found on disk */
static char         g_owned[SND_NUM_SAMPLES];	/* 1 = this slot allocated the buffer */
static long         g_chipbytes;		/* total Chip RAM we hold */

/* What a Paula channel is doing. `used` means a write of ours is outstanding;
 * ffe_sound_service clears it once audio.device replies. */
typedef struct {
	int  used;
	int  index;		/* sample slot, -1 when idle */
	int  prio;		/* the newcomer test: strictly lower loses */
	int  voice;		/* logical voice holding it, -1 for the free pool */
	int  loops;		/* re-arms still owed */
	int  period;
	int  vol;
	unsigned long serial;	/* allocation order, for the oldest-first tie break */
} snd_chan;

static snd_chan     g_chan[AMIGA_AUDIO_CHANNELS];
static int          g_voice_chan[SND_NUM_VOICES];	/* voice -> channel, -1 free */
static unsigned long g_serial;
static int          g_rr;				/* round-robin cursor */
static int          g_inited;

/* Empty means "ask ffe_dir.c", which is the normal case; a non-empty string is
 * the ffe_sound_set_sample_path() override, used by the probe, which runs
 * without a config file. It always ends in a separator. */
static char  g_path[SND_PATH_MAX];

/* ---- loading ------------------------------------------------------------ */

/* One buffer's worth of raw file bytes, converted 2 -> 1 as it is read. Static
 * because the Amiga default stack is 4 KB and this is most of it. */
static unsigned char g_stage[4096];

/* "<name>.raw", or "<NAME>.RAW" for the second attempt. */
static void snd_makename (char *dst, const char *name, int upper)
{
	int i = 0;
	const char *s;

	for (s = name; *s != '\0' && i < SND_NAME_MAX - 5; s++)
		dst[i++] = (upper && *s >= 'a' && *s <= 'z') ? (char)(*s - 'a' + 'A') : *s;
	strcpy (dst + i, upper ? ".RAW" : ".raw");
}

/* The sample's full name. DirMakeSampleName() is ffe_dir.c's builder for the
 * [DIR] SamplePath key -- the same one the game's own loader would use, and the
 * reason this file no longer knows the string "fx/". The override wins when it
 * was set, and is a plain concatenation because it already carries its
 * separator. dst must hold SND_PATH_MAX bytes. */
static void snd_makepath (char *dst, const char *name, int upper)
{
	char nm[SND_NAME_MAX];

	snd_makename (nm, name, upper);

	if (g_path[0] != '\0') {
		strcpy (dst, g_path);
		strcat (dst, nm);
		return;
	}
	DirMakeSampleName (dst, nm);
}

/* Lower case first, then upper. AmigaDOS filesystems are case insensitive, but
 * WinUAE directory mounts inherit the host's rules and the shipped files are
 * upper case -- one extra fopen beats a silent game. */
static FILE *snd_open (const char *name, char *path)
{
	FILE *f;

	snd_makepath (path, name, 0);
	f = fopen (path, "rb");
	if (f != NULL) return f;

	snd_makepath (path, name, 1);
	return fopen (path, "rb");
}

/* Read <name>.raw into a fresh Chip buffer as 8-bit signed. Returns the buffer
 * or NULL; *pfilebytes is the size on disk (0 if the file would not open) and
 * *pchipbytes the length that came out of it. */
static signed char *snd_load (const char *name, long *pfilebytes, long *pchipbytes)
{
	char path[SND_PATH_MAX];
	FILE *f;
	signed char *buf;
	long filebytes, want, got = 0;
	int have = 0;			/* odd byte carried over from the last read */

	*pfilebytes = 0;
	*pchipbytes = 0;

	f = snd_open (name, path);
	if (f == NULL) {
		fprintf (stdout, "ffe: sample %s missing (last tried %s)\n", name, path);
		fflush (stdout);
		return NULL;
	}

	fseek (f, 0, SEEK_END);
	filebytes = ftell (f);
	fseek (f, 0, SEEK_SET);
	*pfilebytes = filebytes;

	want = (filebytes / 2) & ~1L;		/* half the length, and even for Paula */
	if (want > SND_MAX_BYTES) want = SND_MAX_BYTES;
	if (want < 2) {				/* nowt.raw is a real, empty file */
		fclose (f);
		return NULL;
	}

	buf = (signed char *)AmigaAudio_AllocSample ((unsigned long)want);
	if (buf == NULL) {
		fprintf (stdout, "ffe: no Chip RAM for sample %s (%ld bytes)\n", name, want);
		fflush (stdout);
		fclose (f);
		return NULL;
	}

	while (got < want) {
		size_t n = fread (g_stage + have, 1, sizeof (g_stage) - (size_t)have, f);
		long total, i;

		if (n == 0) break;		/* short file: keep what we got */
		total = (long)n + have;
		for (i = 1; i < total && got < want; i += 2)
			buf[got++] = (signed char)g_stage[i];	/* the odd byte is the high byte */
		have = (int)(total & 1);
		if (have) g_stage[0] = g_stage[total - 1];
	}
	fclose (f);

	got &= ~1L;
	if (got < 2) {
		AmigaAudio_FreeSample (buf);
		return NULL;
	}
	*pchipbytes = got;
	return buf;
}

/* ---- channel bookkeeping ------------------------------------------------ */

static void snd_clear_chan (int ch)
{
	g_chan[ch].used   = 0;
	g_chan[ch].index  = -1;
	g_chan[ch].prio   = SND_P_SILENT;
	g_chan[ch].voice  = -1;
	g_chan[ch].loops  = 0;
	g_chan[ch].period = SND_MIN_PERIOD;
	g_chan[ch].vol    = 0;
	g_chan[ch].serial = 0;
}

/* Channels 2 and 3 belong to the music streamer while it runs; AmigaAudio_Play
 * refuses them anyway, but the allocator should not waste a slot pick on them.
 * With music off -- which is always, for now -- all four are ours. */
static int snd_usable (int ch)
{
	if (AmigaAudio_MusicActive () && (ch == 2 || ch == 3)) return 0;
	return 1;
}

/* Game volume is 0..0x7fff (FUNC_001908 shifts the game's internal 0..63 left
 * by nine and clamps). The SDL mixer scales that to 0..255 with vol >> 7;
 * Paula's hardware volume is 0..64, so ours is vol >> 9 -- the same fraction to
 * within half a percent, just two bits coarser. Volumes below 512 fall to
 * silence, which is where the engine-noise path already was: it asks for
 * 0..430, i.e. under 1.7% in the SDL build too. */
static int snd_volume (long vol)
{
	long v;

	if (vol <= 0) return 0;
	v = vol >> 9;
	if (v > 64) v = 64;
	return (int)v;
}

/* Take channel ch back NOW: silence Paula, release the logical voice, forget
 * what was playing. Returns 0 and changes nothing if audio.device will not give
 * the channel up, which can only happen for one the music streamer holds. */
static int snd_stop_chan (int ch)
{
	if (!AmigaAudio_Stop (ch)) return 0;

	if (g_chan[ch].voice >= 0) g_voice_chan[g_chan[ch].voice] = -1;
	snd_clear_chan (ch);
	return 1;
}

/* How many channels classes 1 and 2 hold -- the quantity rule (a) caps. */
static int snd_sustained (void)
{
	int ch, n = 0;

	for (ch = 0; ch < AMIGA_AUDIO_CHANNELS; ch++)
		if (g_chan[ch].used && g_chan[ch].prio >= SND_P_BACK &&
		    g_chan[ch].prio <= SND_P_ENGINE) n++;
	return n;
}

/* An idle usable channel, round robin over the L R L R order, or -1. */
static int snd_pick (void)
{
	static const int order[AMIGA_AUDIO_CHANNELS] = { 0, 1, 3, 2 };	/* L R L R */
	int i, ch;

	for (i = 0; i < AMIGA_AUDIO_CHANNELS; i++) {
		ch = order[(g_rr + i) & (AMIGA_AUDIO_CHANNELS - 1)];
		if (!snd_usable (ch) || g_chan[ch].used) continue;
		g_rr = (g_rr + i + 1) & (AMIGA_AUDIO_CHANNELS - 1);
		return ch;
	}
	return -1;
}

/* Rule (b): silence the least important channel below `prio` -- oldest first on
 * a tie -- and hand it to the caller, idle and ready to be played on. Returns
 * -1 when nothing playing is less important than the newcomer, which is the
 * newcomer's cue to drop itself. */
static int snd_steal (int prio)
{
	int ch, best = -1;

	for (ch = 0; ch < AMIGA_AUDIO_CHANNELS; ch++) {
		if (!snd_usable (ch) || !g_chan[ch].used) continue;
		if (g_chan[ch].prio >= prio) continue;
		if (best < 0 || g_chan[ch].prio < g_chan[best].prio ||
		    (g_chan[ch].prio == g_chan[best].prio &&
		     g_chan[ch].serial < g_chan[best].serial))
			best = ch;
	}
	if (best < 0) return -1;

	return snd_stop_chan (best) ? best : -1;
}

static void snd_start (int ch, int index, int period, int vol)
{
	snd_chan *c = &g_chan[ch];

	if (!AmigaAudio_Play (ch, g_data[index], (unsigned long)g_len[index], period, vol))
		return;				/* channel not actually free; drop it */

	c->used   = 1;
	c->index  = index;
	c->prio   = g_desc[index].prio;
	c->voice  = g_desc[index].voice;
	c->loops  = g_desc[index].loopcount;
	c->period = period;
	c->vol    = vol;
	c->serial = ++g_serial;
	if (c->voice >= 0) g_voice_chan[c->voice] = ch;
}

/* ---- the service tick --------------------------------------------------- */

void ffe_sound_service (void)
{
	int ch;

	if (!g_inited) return;

	for (ch = 0; ch < AMIGA_AUDIO_CHANNELS; ch++) {
		snd_chan *c = &g_chan[ch];

		if (!c->used || !AmigaAudio_ChannelIdle (ch)) continue;

		/* A loopcount is honoured by re-issuing the write; ioa_Cycles is 1 and
		 * amiga_audio.h has no repeat. The period and volume used are the
		 * channel's current ones, i.e. whatever the last retune left there. */
		if (c->loops > 0 && c->index >= 0 && g_data[c->index] != NULL) {
			c->loops--;
			if (AmigaAudio_Play (ch, g_data[c->index],
			                     (unsigned long)g_len[c->index],
			                     c->period, c->vol))
				continue;
		}
		if (c->voice >= 0) g_voice_chan[c->voice] = -1;
		snd_clear_chan (ch);
	}
}

/* ---- init / cleanup ----------------------------------------------------- */

void ffe_sound_set_sample_path (const char *path)
{
	int i = 0;

	if (path == NULL) return;
	while (path[i] != '\0' && i < SND_PATH_MAX - SND_NAME_MAX) { g_path[i] = path[i]; i++; }
	g_path[i] = '\0';
}

void ffe_sound_init (void)
{
	int i, j;

	if (g_inited) return;

	for (i = 0; i < SND_NUM_SAMPLES; i++) {
		g_data[i] = NULL; g_len[i] = 0; g_filebytes[i] = 0; g_owned[i] = 0;
	}
	for (i = 0; i < AMIGA_AUDIO_CHANNELS; i++) snd_clear_chan (i);
	for (i = 0; i < SND_NUM_VOICES; i++) g_voice_chan[i] = -1;
	g_chipbytes = 0;
	g_serial    = 0;
	g_rr        = 0;

	/* All four channels at once, ADIOF_NOWAIT. If anything else holds Paula we
	 * stay down and every entry point below is a no-op -- the game runs silent
	 * rather than not at all. */
	if (!AmigaAudio_Open ()) {
		fprintf (stdout, "ffe: audio.device would not give up Paula - running silent\n");
		fflush (stdout);
		return;
	}

	for (i = 0; i < SND_NUM_SAMPLES; i++) {
		if (g_desc[i].prio == SND_P_SILENT) continue;	/* "nowt": never loaded */

		for (j = 0; j < i; j++)		/* four slots share airopen, two share wind... */
			if (g_data[j] != NULL && strcmp (g_desc[j].name, g_desc[i].name) == 0)
				break;
		if (j < i) {
			g_data[i]      = g_data[j];
			g_len[i]       = g_len[j];
			g_filebytes[i] = g_filebytes[j];
			continue;			/* g_owned stays 0: not ours to free */
		}

		g_data[i] = snd_load (g_desc[i].name, &g_filebytes[i], &g_len[i]);
		if (g_data[i] != NULL) {
			g_owned[i]   = 1;
			g_chipbytes += g_len[i];
		}
	}

	g_inited = 1;
}

void ffe_sound_cleanup (void)
{
	int i;

	SoundStopAllSamples ();

	/* Close BEFORE freeing: AmigaAudio_Close aborts every outstanding write,
	 * and Paula must not still be reading a buffer we hand back. */
	AmigaAudio_Close ();

	for (i = 0; i < SND_NUM_SAMPLES; i++) {
		if (g_owned[i]) AmigaAudio_FreeSample (g_data[i]);
		g_data[i] = NULL; g_len[i] = 0; g_owned[i] = 0;
	}
	for (i = 0; i < AMIGA_AUDIO_CHANNELS; i++) snd_clear_chan (i);
	for (i = 0; i < SND_NUM_VOICES; i++) g_voice_chan[i] = -1;
	g_chipbytes = 0;
	g_inited    = 0;
}

/* ---- the API the game calls: samples ------------------------------------ */

void SoundCheckInit (long *pAll, long *pDigi, long *pMidi)
{
	if (pAll  != NULL) *pAll  = g_inited;
	if (pDigi != NULL) *pDigi = g_inited;
	if (pMidi != NULL) *pMidi = 0;		/* no MIDI on Paula, and none wanted */
}

void SoundPlaySample (long index, long vol, long pitch)
{
	const snd_desc *d;
	int period, volume, ch;

	if (!g_inited) return;
	ffe_sound_service ();
	if (index < 0 || index >= SND_NUM_SAMPLES) return;
	if (g_data[index] == NULL) return;	/* "nowt", or the file never loaded */

	d = &g_desc[index];

	if (pitch == -1) pitch = SND_RATE;
	pitch += d->pitchmod;
	if (pitch > 100000) pitch = 100000;
	if (pitch < 100)    pitch = 100;

	period = (int)((amigagfx_is_ntsc () ? SND_NTSC_CLOCK : SND_PAL_CLOCK) / (unsigned long)pitch);
	if (period < SND_MIN_PERIOD) period = SND_MIN_PERIOD;	/* see the header */
	volume = snd_volume (vol);

	/* A slot with a fixed voice owns that voice: while it sounds, a second
	 * request is dropped -- except the engine, which retunes instead. The
	 * retune is a stop and a restart on the same channel, so the new pitch is
	 * heard immediately; see "Retuning" in the header. If the channel will not
	 * stop -- music holds it, which cannot happen while music is a stub -- the
	 * new period and volume are left for the next re-arm to pick up. */
	if (d->voice >= 0) {
		ch = g_voice_chan[d->voice];
		if (ch >= 0) {
			if (index != SND_ENGINE) return;
			if (snd_stop_chan (ch)) {
				snd_start (ch, (int)index, period, volume);
			} else {
				g_chan[ch].period = period;
				g_chan[ch].vol    = volume;
				g_chan[ch].loops  = d->loopcount;
			}
			return;
		}
	}

	/* Rule (a): ambience never takes the last channels off the one-shots. Over
	 * the cap the request is simply dropped -- the game re-issues engine and
	 * wind every frame, so its own polling is the retry. */
	if (d->prio <= SND_P_ENGINE && snd_sustained () >= SND_MAX_SUSTAINED) return;

	ch = snd_pick ();
	if (ch < 0) ch = snd_steal (d->prio);	/* rule (b), and it happens now */
	if (ch < 0) return;			/* nothing less important: dropped */

	snd_start (ch, (int)index, period, volume);
}

void SoundStopAllSamples (void)
{
	int i;

	if (!g_inited) return;

	/* Silence every channel we hold, on the spot. Nothing is left playing out:
	 * "stop" means stop, and a wind pass would otherwise be heard for another
	 * five seconds. Music, if it were ever streaming, keeps its two channels --
	 * SoundStopSong is the call for those. */
	for (i = 0; i < AMIGA_AUDIO_CHANNELS; i++) snd_stop_chan (i);
	for (i = 0; i < SND_NUM_VOICES; i++) g_voice_chan[i] = -1;
}

/* ---- the API the game calls: music -------------------------------------- */

/* Stubs, exactly as in ref/jjffe/sdl/sdlsound.cpp -- the SDL build has no music
 * either, and the seventeen qq*.raw tracks are not part of the floppy release.
 *
 * Where a Paula implementation attaches: amiga_audio.h already carries a
 * complete streamer (AmigaAudio_MusicStart / MusicService / MusicStop /
 * MusicFinished) that ping-pongs two Chip buffers across channels 2 and 3, so
 * SoundPlaySong becomes "open the track, MusicStart with a refill callback",
 * SoundSongDone becomes AmigaAudio_MusicFinished, and ffe_sound_service gains
 * one AmigaAudio_MusicService call. snd_usable() above already yields channels
 * 2 and 3 to the streamer, and SND_MAX_SUSTAINED should drop to 1 at the same
 * time, since the SFX pool halves. */

void SoundPlaySong (long index)
{
	(void)index;
}

void SoundStopSong (void)
{
}

long SoundSongDone (void)
{
	return 0;
}

/* ---- the API the game calls: speech streaming --------------------------- */

/* Used only for the speech in the bulletin-board videos, which the floppy
 * release does not ship; the SDL layer buffers 20 seconds of it into a static
 * array and plays it on its reserved channel 1. Stubbed here, with the two
 * observable behaviours preserved: GetUsedBlocks answers 0, which is what the
 * reference answers while no block has been added, and Wait returns at once,
 * which is what its `while (GetStreamPos () != NULL);` does when the stream was
 * never started. A spin here would hang the game outright. */

void SoundStreamReset (void)
{
}

void SoundStreamAddBlock (void *pData, long size)
{
	(void)pData;
	(void)size;
}

long SoundStreamGetUsedBlocks (void)
{
	return 0;
}

void SoundStreamStart (void)
{
}

void SoundStreamWait (void)
{
}

void SoundStreamStop (void)
{
}

/* ---- what the probes look at -------------------------------------------- */

int ffe_sound_sample_info (int index, const char **pname,
                           long *pfilebytes, long *pchipbytes)
{
	if (index < 0 || index >= SND_NUM_SAMPLES) return -1;

	if (pname      != NULL) *pname      = g_desc[index].name;
	if (pfilebytes != NULL) *pfilebytes = g_filebytes[index];
	if (pchipbytes != NULL) *pchipbytes = g_len[index];

	if (g_desc[index].prio == SND_P_SILENT) return 0;
	return g_data[index] != NULL ? 1 : -1;
}

long ffe_sound_chip_bytes (void)
{
	return g_chipbytes;
}

int ffe_sound_chan_info (int ch, long *pindex, long *pprio)
{
	if (ch < 0 || ch >= AMIGA_AUDIO_CHANNELS) return 0;

	if (pindex != NULL) *pindex = g_chan[ch].used ? (long)g_chan[ch].index : -1L;
	if (pprio  != NULL) *pprio  = g_chan[ch].used ? (long)g_chan[ch].prio  :  0L;
	return g_chan[ch].used;
}
