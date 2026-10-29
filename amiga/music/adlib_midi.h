/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 angree. See LICENSE at the repository root.
 *
 * MIDI -> OPL2 driver: a MidiSynth (midi_synth.h) that plays General MIDI
 * events on opl2.c with the game's own AdLib banks, MELODIC.BNK (programs
 * 0..127) and DRUM.BNK (channel 10, one instrument per key).
 *
 * The two banks are the caller's bytes; they are parsed at create time and
 * may be freed afterwards. quality 0 = cheapest (LIVE playback on a 68020),
 * 1 = closest to the chip (OFFLINE rendering). Returns NULL if either bank is
 * not an "ADLIB-" bank of 30-byte instruments, or on a bad rate / no memory.
 */
#ifndef ADLIB_MIDI_H
#define ADLIB_MIDI_H

#include "midi_synth.h"

MidiSynth *adlib_midi_create(int rate, const unsigned char *melodic_bnk, long mlen,
			     const unsigned char *drum_bnk, long dlen, int quality);

#endif
