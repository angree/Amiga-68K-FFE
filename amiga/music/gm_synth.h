/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 angree. See LICENSE at the repository root.
 *
 * gm_synth: a sample-playback General MIDI synthesiser behind midi_synth.h.
 *
 * Plays a bank built by tools/music/build_gmbank.py from CC0 recordings
 * (VCSL, VSCO 2 CE) and generated waveforms; provenance of every sample is in
 * tools/music/GMBANK-SOURCES.md. Integer-only at run time -- no float, no
 * double, no 64-bit arithmetic -- so the same file builds for a 68020 without
 * FPU (bebbo gcc -mcpu=68020 -msoft-float -O1) and for the host. Output is
 * mono signed 16-bit; pan is accepted and ignored. Deterministic: the same
 * events at the same sample positions give the same samples, bit for bit.
 *
 * The bank is NOT copied: gm_synth_create() parses the tables into the ctx
 * and points the voices straight into the caller's bank memory for sample
 * data, so the bank must stay allocated (and unchanged) until destroy().
 * The bank may sit anywhere; nothing assumes alignment.
 *
 * ---- bank format, version 1 (ALL multi-byte fields BIG-ENDIAN) -----------
 *
 *   header, 48 bytes
 *     0  char[4]  magic "GMBK"
 *     4  u16      version (1)
 *     6  u16      header size (48)
 *     8  u16      number of instruments
 *    10  u16      number of zones
 *    12  u32      offset of the program table   (128 entries x 12 bytes)
 *    16  u32      offset of the drum table      (128 entries x 8 bytes)
 *    20  u32      offset of the instrument table (n x 20 bytes)
 *    24  u32      offset of the zone table       (n x 28 bytes)
 *    28  u32      offset of the sample data
 *    32  u32      length of the sample data
 *    36  u32      total file length
 *    40  8 bytes  reserved, zero
 *
 *   program, 12 bytes -- one per GM program 0..127
 *     0  u8   instrument index
 *     1  s8   transpose, semitones (applied to the key before zone lookup)
 *     2  u8   volume 0..127, linear
 *     3  u8   flags (reserved, 0)
 *     4  u16  attack, ms (linear rise)
 *     6  u16  decay, ms for a 96 dB fall (0 = none; see TAIL below)
 *     8  u16  sustain level as attenuation, 1/256 octave (0 = full, 4096 = silent)
 *    10  u16  release, ms for a 96 dB fall
 *
 *   drum, 8 bytes -- one per channel-10 key 0..127
 *     0  u8   instrument index, 0xFF = silent key
 *     1  u8   key to play the instrument at (60 = as recorded)
 *     2  u8   volume 0..127, linear
 *     3  u8   exclusive group (0 = none): a new hit cuts the others of its group
 *     4  u16  decay, ms for a 96 dB fall (0 = play the whole sample)
 *     6  u16  reserved
 *     Drum voices ignore note-off.
 *
 *   instrument, 20 bytes
 *     0  char[16] name, NUL-padded
 *    16  u16      first zone
 *    18  u16      number of zones (consecutive, ascending key ranges)
 *
 *   zone, 28 bytes -- one sample
 *     0  u8   lowest key    1  u8  highest key    2  u8  root key
 *     3  s8   fine tune, cents
 *     4  u16  sample rate, Hz (4000..32000)
 *     6  u8   gain, 128 = unity (loudness matching between zones)
 *     7  u8   flags: 1 = LOOP, 2 = TAIL
 *     8  u16  natural decay, ms for a 96 dB fall (TAIL zones)
 *    10  u16  reserved
 *    12  u32  offset of the sample, relative to the sample data
 *    16  u32  length in sample frames (< 65472)
 *    20  u32  loop start   24 u32 loop end (frames; LOOP only, start < end <= length)
 *     Samples are 8-bit signed, one frame per byte, and every sample is
 *     followed by ONE GUARD BYTE (data[length]): the first loop sample for a
 *     looping zone, 0 otherwise -- that is what lets the interpolator read
 *     frame i+1 without a test.
 *     TAIL: the sample holds its own natural attack and decay up to the loop
 *     start and a level-flattened loop after it. The envelope HOLDS until
 *     playback first enters the loop, then decays at the zone's decay (or the
 *     program's, when that is non-zero) toward silence.
 *
 * ---- what the synth implements -------------------------------------------
 *
 *   note on/off with velocity (40 log curve), program change, pitch bend
 *   (RPN 0 sets the range, default +-2 semitones), CC 7 volume, 11 expression,
 *   64 sustain pedal, 6/38/100/101 RPN data entry, 120 all sound off,
 *   121 reset controllers, 123 all notes off. CC 10 (pan) is ignored.
 *   Channel 10 (index 9) is the GM drum kit; its program changes are ignored.
 *   Polyphony is the `voices` argument (1..64); when it runs out, the
 *   quietest releasing voice is taken, else the oldest one.
 */
#ifndef GM_SYNTH_H
#define GM_SYNTH_H

#include "midi_synth.h"

#define GM_BANK_MAGIC	"GMBK"
#define GM_BANK_VERSION	1

/* rate: output rate in Hz (8000..48000). bank/len: the whole bank file.
 * voices: polyphony limit, clamped to 1..64. NULL on a bad bank or no memory. */
MidiSynth *gm_synth_create(int rate, const unsigned char *bank, long len, int voices);

/* Linear interpolation between sample frames: 1 = on (the default; offline
 * rendering), 0 = off (nearest frame; about a third cheaper per voice). */
void gm_synth_set_interp(MidiSynth *s, int on);

/* Master gain, 256 = unity (the default). The mix is hard-clipped to 16 bits. */
void gm_synth_set_master(MidiSynth *s, int gain256);

/* Number of voices sounding right now (for diagnostics and benchmarks). */
int gm_synth_active_voices(MidiSynth *s);

#endif
