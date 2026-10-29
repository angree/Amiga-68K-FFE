/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 angree. See LICENSE at the repository root.
 *
 * The game's music: HMI "HMIMIDIP" songs (the .HMP files in MUSIC/), loaded
 * and sequenced straight into a MidiSynth (midi_synth.h). No conversion to
 * standard MIDI happens at run time; tools/music/hmp2mid.c does that on the
 * host for listening, through this same parser.
 *
 * Plain C, no float/double, no stdio outside hmp_load_file(); the per-tick
 * path (hmp_advance) only does integer arithmetic and 32-bit divides, which a
 * 68020 has in hardware. See hmp.c for the format as it is read.
 */
#ifndef HMP_H
#define HMP_H

#include "midi_synth.h"

typedef struct HmpSong HmpSong;

/* One event as the parser sees it, in the order hmp_advance dispatches. */
typedef struct HmpEvent {
	long tick;			/* absolute song tick */
	int track;			/* chunk index in the file, 0 = first */
	int status;			/* 0x80..0xEF channel, 0xFF meta, 0xF0/0xF7 sysex */
	int d1, d2;			/* channel data (clamped to 127), meta: d1 = type */
	int len;			/* meta/sysex payload length, else data byte count */
	const unsigned char *data;	/* meta/sysex payload (inside the song), else NULL */
} HmpEvent;

/* Load from memory (the bytes are copied) or a file. NULL if it is not an
 * HMP or no track could be found. */
HmpSong *hmp_load_mem(const unsigned char *data, long len);
HmpSong *hmp_load_file(const char *path);
void hmp_free(HmpSong *s);

/* Back to tick 0, clock 0, header tick rate. */
void hmp_rewind(HmpSong *s);

/* Advance the song clock by usec microseconds and dispatch every event that
 * fell due, to synth (may be NULL: a silent run). Returns 1 while the song
 * plays, 0 once every track has ended. */
int hmp_advance(HmpSong *s, long usec, const MidiSynth *synth);

/* Length of the song in microseconds (dry run, cached) and the number of
 * track chunks (including the empty first one every shipped song has). */
long hmp_length_usec(HmpSong *s);
int hmp_tracks(HmpSong *s);

/* ---- for tools and tests ------------------------------------------------ */

/* Current song tick; inside a synth callback it is the event's own tick. */
long hmp_tick(const HmpSong *s);

/* Header values: ticks per quarter (0x34) and ticks per second (0x38). */
int hmp_division(const HmpSong *s);
int hmp_tick_rate(const HmpSong *s);

/* Override the tick period with num/den microseconds (the header's own is
 * 1000000/rate). Used to replay a song on another converter's time base;
 * hmp_rewind() does NOT undo it, hmp_set_tick_period(s, 0, 0) does. */
void hmp_set_tick_period(HmpSong *s, unsigned long num, unsigned long den);

/* Raw event iteration without a clock, same order as hmp_advance. Tempo
 * metas are applied as they pass. Returns 1 and fills ev, or 0 at the end.
 * Do not mix with hmp_advance without an hmp_rewind in between. */
int hmp_next_event(HmpSong *s, HmpEvent *ev);

#endif
