/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 angree. See LICENSE at the repository root.
 *
 * A YM3812 (OPL2) register-level FM synthesiser, integer only.
 *
 * The interface is the chip's: the driver writes registers (0x01, 0x08,
 * 0x20..0xF5, the same byte the DOS game would have sent to port 0x389) and
 * asks for samples. Nothing here knows about MIDI; adlib_midi.c is the driver.
 *
 * Two render paths behind one register file:
 *
 *   quality 1  the chip as it is: every operator stepped at the chip's own
 *              49716 Hz, envelope generator ticked per chip sample with the
 *              real rate/step patterns, tremolo and vibrato LFOs per sample,
 *              then box-filtered down to the output rate. For OFFLINE use.
 *   quality 0  the same operators computed straight at the output rate,
 *              envelopes and LFOs advanced once per OPL2_BLOCK output samples
 *              with averaged rates. For LIVE use on a 68020.
 *
 * Rhythm mode (0xBD bit 5), the timers and CSM are not implemented: the
 * driver never uses them (see adlib_midi.c for why).
 *
 * Output: mono signed 16-bit, at the rate given to opl2_create(). render()
 * overwrites out[0..n-1]. No float, no global mutable state, no stdio.
 */
#ifndef OPL2_H
#define OPL2_H

#define OPL2_CHIP_RATE	49716	/* 14.31818 MHz / 4 / 72 */
#define OPL2_BLOCK	16	/* quality 0: output samples per envelope/LFO step */

typedef struct Opl2 Opl2;

Opl2 *opl2_create(int rate, int quality);	/* NULL on bad args / no memory */
void  opl2_destroy(Opl2 *o);
void  opl2_reset(Opl2 *o);			/* all registers 0, all voices silent */
void  opl2_write(Opl2 *o, int reg, int val);	/* reg 0x00..0xFF, val 0..255 */
void  opl2_render(Opl2 *o, short *out, int n);
/* chip mix -> 16-bit gain, Q8 (256 = one chip unit per output unit; default 512) */
void  opl2_set_gain(Opl2 *o, int gain_q8);
/* nonzero when every operator's envelope is at full attenuation */
int   opl2_silent(const Opl2 *o);

#endif
