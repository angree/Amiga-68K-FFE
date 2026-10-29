/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 angree. See LICENSE at the repository root.
 *
 * The one interface between the music sequencer (hmp.c: the game's HMI .HMP
 * songs) and whatever makes the sound (2026-10-24):
 *   - adlib_midi.c  OPL2 FM synthesis with the game's own MELODIC.BNK/DRUM.BNK
 *   - gm_synth.c    sample playback from a General MIDI bank built from CC0
 *                   samples (tools/music/)
 *   - an external MIDI device (camd.library), which renders nothing
 * The same synth runs LIVE on the Amiga (render() called from the music
 * process) or OFFLINE in the renderer tool (host or Amiga) that writes the
 * songs as IMA ADPCM files once.
 *
 * Rules for every implementation, because the same code must run on a
 * 68020 without FPU (bebbo gcc, -mcpu=68020 -msoft-float) and on the host:
 *   - plain C (C99 subset bebbo gcc 6.5 accepts), no float/double at run
 *     time (tables may be computed with integers or generated offline),
 *   - no global state outside the ctx, no stdio in render(),
 *   - render() ADDS nothing implicit: it writes exactly n mono signed 16-bit
 *     samples at the rate given to the synth's create function.
 */
#ifndef MIDI_SYNTH_H
#define MIDI_SYNTH_H

typedef struct MidiSynth {
	void *ctx;
	void (*note_on)    (void *ctx, int ch, int note, int vel);	/* vel 1..127 */
	void (*note_off)   (void *ctx, int ch, int note);
	void (*program)    (void *ctx, int ch, int prog);		/* 0..127 */
	void (*control)    (void *ctx, int ch, int cc, int val);	/* 7 volume, 10 pan, 64 sustain, 121/123 ... */
	void (*pitch_bend) (void *ctx, int ch, int value);		/* 0..16383, 8192 = centre */
	void (*all_off)    (void *ctx);					/* silence at once (song stop) */
	/* n mono samples, signed 16-bit; NULL for synths that render nothing */
	void (*render)     (void *ctx, short *out, int n);
	void (*destroy)    (void *ctx);
} MidiSynth;

#endif
