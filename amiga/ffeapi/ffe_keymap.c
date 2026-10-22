/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 angree. See LICENSE at the repository root.
 *
 * DOS/PC set-1 scancode -> AmigaOS RAWKEY lookup table.
 *
 * WHY THIS TABLE EXISTS
 * ----------------------
 * Frontier: First Encounters asks the platform layer for key state indexed
 * by DOS scancode: InputKeybReadStates(UCHAR *pKeys) fills pKeys[scancode]
 * (see ref/jjffe/ffeapi.h). On Amiga we receive IDCMP_RAWKEY events carrying
 * Amiga raw key codes, not DOS scancodes. ffe_scancode_to_rawkey[] lets the
 * Amiga input layer keep a rawkey-indexed "key currently down" array (256
 * bytes, one per Amiga raw key, filled straight from IDCMP_RAWKEY) and
 * answer a DOS-scancode query by looking up
 * key_down[ffe_scancode_to_rawkey[scancode]] -- i.e. it maps in the
 * DIRECTION THE GAME ASKS IN, from scancode to rawkey, even though the data
 * arrives from the OS the other way around.
 *
 * HOW IT WAS DERIVED
 * -------------------
 * The authoritative statement of which scancode means which physical key is
 * ref/jjffe/sdl/sdlinput.cpp, the pKeybConvTable[256] array (scancode ->
 * SDL 1.2 SDLK_* symbol; 103 of the 256 slots are non-zero, the rest are
 * unused/reserved scancodes that jjffe itself never produces a key for).
 * Extended (E0-prefixed) PC scancodes are stored by jjffe, as usual for DOS
 * code of this vintage, as (original low byte | 0x80) -- e.g. Right Ctrl
 * (E0 1D) is slot 0x9D, the numeric-keypad Enter (E0 1C) is slot 0x9C, the
 * cursor keys are slots 0xC8/0xCB/0xCD/0xD0, etc.
 *
 * For every non-zero slot, this file gives the AmigaOS raw key code (from
 * the standard, fixed Amiga keyboard matrix documented in the Amiga
 * Hardware Reference Manual / devices/rawkeycodes.h -- e.g. 0x00 backquote,
 * 0x01-0x0A = "1234567890", 0x10-0x19 = QWERTYUIOP, 0x20-0x28 = ASDFGHJKL,
 * 0x31-0x37 = ZXCVBNM, 0x40 Space, 0x44 Return, 0x45 Esc, 0x50-0x59 =
 * F1-F10, 0x60/0x61 = L/R Shift, 0x62 CapsLock, 0x63 Ctrl (Amiga has only
 * one), 0x64/0x65 = L/R Alt, 0x66/0x67 = L/R Amiga, 0x4C/0x4D/0x4E/0x4F =
 * cursor Up/Down/Right/Left, 0x46 Delete, 0x5F Help, and the numeric keypad
 * spread over 0x0F, 0x1D-0x1F, 0x2D-0x2F, 0x3C-0x3F, 0x4A, 0x43, 0x5A-0x5E)
 * for the SAME PHYSICAL KEY, not the byte value or the character label --
 * those occasionally differ between the two keyboards, see the two notes
 * below.
 *
 * THE NOKEY (FFE_RAWKEY_NONE) CONVENTION
 * ----------------------------------------
 * FFE_RAWKEY_NONE (0xFF) is used for two different reasons, both written
 * out per-entry below:
 *   1. jjffe's own pKeybConvTable slot is 0 there (scancode unused/reserved,
 *      no comment needed -- there was never a named key to translate); or
 *   2. the slot names a real PC key that simply has no Amiga counterpart:
 *      Print Screen/SysRq, Scroll Lock, Num Lock, Pause, Insert, Page
 *      Up/Down, Home, End, right Ctrl, and the Menu/Application key. The
 *      Amiga keyboard does not have any of these, so inventing a mapping
 *      would be worse than admitting there isn't one.
 * F11 and F12 are also mapped to FFE_RAWKEY_NONE: the classic 68k Amiga
 * keyboard (A500/A1200/A2000/A3000/A4000, i.e. the hardware this port
 * targets) has only F1-F10. Raw key codes for F11/F12 and other extras
 * (Home, PageUp/Down, ScrollLock...) do exist in newer, USB-keyboard-era
 * AmigaOS4/MorphOS headers, but those are not part of the classic-Amiga
 * keyboard.device matrix and are deliberately not used here.
 *
 * TWO ENTRIES WHERE THE AMIGA KEY IS AT A DIFFERENT SPOT ON THE ROW
 * --------------------------------------------------------------------
 * - Backslash (PC scancode 0x2B) sits directly above Enter on a PC
 *   keyboard. On an Amiga keyboard the backslash key instead sits at the
 *   end of the number row, after "=" (raw key 0x0D). Both keys produce the
 *   same character, they are simply not in the same row/column on the two
 *   keyboards; mapped by character, not physical row position.
 * - PC scancode 0x29 is, on every scancode chart, the top-left key (left of
 *   "1", below Esc) -- normally the "grave/backtick/tilde" key. jjffe's own
 *   table labels it SDLK_HASH rather than SDLK_BACKQUOTE, most likely
 *   because sdlinput.cpp was developed/tested against a UK keyboard/X11
 *   keymap combination where that physical key reports as '#' under SDL
 *   1.2. It is mapped here to Amiga raw key 0x00 (TILDE/backquote), the
 *   same physical top-left key regardless of which character label SDL
 *   reported for it -- see the UNSURE note below.
 *
 * CROSS-CHECKS PERFORMED
 * ------------------------
 * The Amiga raw key table used here was cross-checked between two
 * independent sources describing the classic Amiga keyboard matrix (the
 * MorphOS SDK's devices/rawkeycodes.h, and the tkoecker/amigakb Amiga
 * keyboard firmware project on GitHub); both agree on every code used
 * below. The digit row (PC 0x02-0x0B -> Amiga 0x01-0x0A) and the QWERTY row
 * (PC 0x10-0x19 -> Amiga 0x10-0x19, numerically identical) were used as an
 * internal sanity check against ref/jjffe/sdl/sdlinput.cpp's own ordering,
 * and the letter counts per row (10 QWERTY, 9 ASDF, 7 ZXCVBNM) match on
 * both sides.
 *
 * A Python script was used to generate the table body mechanically from a
 * per-scancode dictionary (scratch file, not checked in) to avoid manual
 * transcription errors across 256 entries; the dictionary content is what
 * is documented in this comment and in the per-entry inline comments below.
 */

#include "ffe_keymap.h"

/* Local shorthand so the table below reads as "NOKEY" instead of the full
 * macro name; undefined again right after the table. Same value as
 * FFE_RAWKEY_NONE (0xFF) -- see ffe_keymap.h. */
#define NOKEY FFE_RAWKEY_NONE

const unsigned char ffe_scancode_to_rawkey[256] = {
	NOKEY, 0x45 /* Esc */, 0x01 /* 1 */, 0x02 /* 2 */, 0x03 /* 3 */, 0x04 /* 4 */, 0x05 /* 5 */, 0x06 /* 6 */,  /* 0x00-0x07 */
	0x07 /* 7 */, 0x08 /* 8 */, 0x09 /* 9 */, 0x0A /* 0 */, 0x0B /* - */, 0x0C /* = */, 0x41 /* Backspace */, 0x42 /* Tab */,  /* 0x08-0x0F */
	0x10 /* Q */, 0x11 /* W */, 0x12 /* E */, 0x13 /* R */, 0x14 /* T */, 0x15 /* Y */, 0x16 /* U */, 0x17 /* I */,  /* 0x10-0x17 */
	0x18 /* O */, 0x19 /* P */, 0x1A /* [ */, 0x1B /* ] */, 0x44 /* Return */, 0x63 /* LCtrl (Amiga has a single Ctrl key) */, 0x20 /* A */, 0x21 /* S */,  /* 0x18-0x1F */
	0x22 /* D */, 0x23 /* F */, 0x24 /* G */, 0x25 /* H */, 0x26 /* J */, 0x27 /* K */, 0x28 /* L */, 0x29 /* ; */,  /* 0x20-0x27 */
	0x2A /* ' */, 0x00 /* UNSURE: grave/backtick key, top-left of '1' -- jjffe table calls it SDLK_HASH (UK/X11 quirk?), see header note */, 0x60 /* LShift */, 0x0D /* \ (Amiga backslash key is repositioned, see header note) */, 0x31 /* Z */, 0x32 /* X */, 0x33 /* C */, 0x34 /* V */,  /* 0x28-0x2F */
	0x35 /* B */, 0x36 /* N */, 0x37 /* M */, 0x38 /* , */, 0x39 /* . */, 0x3A /* / */, 0x61 /* RShift */, 0x5D /* KP * */,  /* 0x30-0x37 */
	0x64 /* LAlt */, 0x40 /* Space */, 0x62 /* CapsLock */, 0x50 /* F1 */, 0x51 /* F2 */, 0x52 /* F3 */, 0x53 /* F4 */, 0x54 /* F5 */,  /* 0x38-0x3F */
	0x55 /* F6 */, 0x56 /* F7 */, 0x57 /* F8 */, 0x58 /* F9 */, 0x59 /* F10 */, NOKEY /* NumLock, no Amiga key */, NOKEY /* ScrollLock, no Amiga key */, 0x3D /* KP 7 */,  /* 0x40-0x47 */
	0x3E /* KP 8 */, 0x3F /* KP 9 */, 0x4A /* KP - */, 0x2D /* KP 4 */, 0x2E /* KP 5 */, 0x2F /* KP 6 */, 0x5E /* KP + */, 0x1D /* KP 1 */,  /* 0x48-0x4F */
	0x1E /* KP 2 */, 0x1F /* KP 3 */, 0x0F /* KP 0 */, 0x3C /* KP . */, NOKEY, NOKEY, NOKEY, NOKEY /* F11, no Amiga key */,  /* 0x50-0x57 */
	NOKEY /* F12, no Amiga key */, NOKEY, NOKEY, NOKEY, NOKEY, NOKEY, NOKEY, NOKEY,  /* 0x58-0x5F */
	NOKEY, NOKEY, NOKEY, NOKEY, NOKEY, NOKEY, NOKEY, NOKEY,  /* 0x60-0x67 */
	NOKEY, NOKEY, NOKEY, NOKEY, NOKEY, NOKEY, NOKEY, NOKEY,  /* 0x68-0x6F */
	NOKEY, NOKEY, NOKEY, NOKEY, NOKEY, NOKEY, NOKEY, NOKEY,  /* 0x70-0x77 */
	NOKEY, NOKEY, NOKEY, NOKEY, NOKEY, NOKEY, NOKEY, NOKEY,  /* 0x78-0x7F */
	NOKEY, NOKEY, NOKEY, NOKEY, NOKEY, NOKEY, NOKEY, NOKEY,  /* 0x80-0x87 */
	NOKEY, NOKEY, NOKEY, NOKEY, NOKEY, NOKEY, NOKEY, NOKEY,  /* 0x88-0x8F */
	NOKEY, NOKEY, NOKEY, NOKEY, NOKEY, NOKEY, NOKEY, NOKEY,  /* 0x90-0x97 */
	NOKEY, NOKEY, NOKEY, NOKEY, 0x43 /* KP Enter (ext.) */, NOKEY /* RCtrl, no Amiga key (single Ctrl only) */, NOKEY, NOKEY,  /* 0x98-0x9F */
	NOKEY, NOKEY, NOKEY, NOKEY, NOKEY, NOKEY, NOKEY, NOKEY,  /* 0xA0-0xA7 */
	NOKEY, NOKEY, NOKEY, NOKEY, NOKEY, NOKEY, NOKEY, NOKEY,  /* 0xA8-0xAF */
	NOKEY, NOKEY, NOKEY, NOKEY, NOKEY, 0x5C /* KP / (ext.) */, NOKEY, NOKEY /* SysRq/PrintScreen, no Amiga key */,  /* 0xB0-0xB7 */
	0x65 /* RAlt (ext.) */, NOKEY, NOKEY, NOKEY, NOKEY, NOKEY, NOKEY, NOKEY,  /* 0xB8-0xBF */
	NOKEY, NOKEY, NOKEY, NOKEY, NOKEY, NOKEY, NOKEY, NOKEY /* Home, no Amiga key */,  /* 0xC0-0xC7 */
	0x4C /* Up (ext.) */, NOKEY /* PageUp, no Amiga key */, NOKEY, 0x4F /* Left (ext.) */, NOKEY, 0x4E /* Right (ext.) */, NOKEY, NOKEY /* End, no Amiga key */,  /* 0xC8-0xCF */
	0x4D /* Down (ext.) */, NOKEY /* PageDown, no Amiga key */, NOKEY /* Insert, no Amiga key */, 0x46 /* Delete (ext.) */, NOKEY, NOKEY, NOKEY, NOKEY,  /* 0xD0-0xD7 */
	NOKEY, NOKEY, NOKEY, 0x66 /* UNSURE: LWin/LSuper -> Left Amiga key, see header note */, 0x67 /* UNSURE: RWin/RSuper -> Right Amiga key, see header note */, NOKEY /* Menu/App key, no Amiga key */, NOKEY, NOKEY,  /* 0xD8-0xDF */
	NOKEY, NOKEY, NOKEY, NOKEY, NOKEY, NOKEY, NOKEY, NOKEY,  /* 0xE0-0xE7 */
	NOKEY, NOKEY, NOKEY, NOKEY, NOKEY, NOKEY, NOKEY, NOKEY,  /* 0xE8-0xEF */
	NOKEY, NOKEY, NOKEY, NOKEY, NOKEY, NOKEY, NOKEY, NOKEY,  /* 0xF0-0xF7 */
	NOKEY, NOKEY, NOKEY, NOKEY, NOKEY, NOKEY, NOKEY, NOKEY,  /* 0xF8-0xFF */
};

#undef NOKEY

/*
 * SUMMARY
 * -------
 * 91  scancodes map to a real Amiga raw key.
 * 165 scancodes are FFE_RAWKEY_NONE (103 - 91 = 12 of those are PC keys
 *     with no Amiga counterpart and carry an explanatory comment above;
 *     the remaining 153 are scancode slots jjffe's own pKeybConvTable
 *     never assigns a key to, i.e. genuinely unused/reserved).
 * 91 + 165 = 256.
 *
 * UNSURE ENTRIES (3 total) -- also marked inline above with UNSURE comments
 *   0x29 -> 0x00 (TILDE/backquote): jjffe's SDL table calls this scancode
 *           SDLK_HASH rather than the expected SDLK_BACKQUOTE. The physical
 *           position (top-left, left of '1') is unambiguous from the
 *           standard PC set-1 scancode chart, so the target Amiga key
 *           (0x00) is believed correct, but *why* jjffe's own table used
 *           SDLK_HASH for it (a UK-keyboard/X11-keymap quirk, presumed) was
 *           not independently confirmed.
 *   0xDB -> 0x66 (Left Amiga key): PC scancode 0xDB is the Left
 *           Windows/Super key (E0 5B), which has no Amiga keyboard
 *           counterpart at all. Mapped to the Left Amiga key as the closest
 *           functional analog (an "OS modifier" key next to the other
 *           Alt/Amiga cluster), matching the convention used by common
 *           Amiga emulators (e.g. WinUAE's default host-key mapping), not a
 *           verified same-physical-position match.
 *   0xDC -> 0x67 (Right Amiga key): same reasoning as 0xDB, for the Right
 *           Windows/Super key (E0 5C).
 */
