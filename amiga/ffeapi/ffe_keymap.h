/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 angree. See LICENSE at the repository root.
 *
 * DOS/PC set-1 scancode -> AmigaOS RAWKEY lookup table for the FFE Amiga
 * platform layer. See ffe_keymap.c for the full derivation notes, the
 * NOKEY convention and the list of entries that could not be verified with
 * full confidence.
 */

#ifndef FFE_KEYMAP_H
#define FFE_KEYMAP_H

#ifdef __cplusplus
extern "C" {
#endif

/* Sentinel value: no Amiga keyboard key corresponds to this DOS/PC scancode
 * (e.g. Print Screen, Scroll Lock, Num Lock, Pause, Insert, Page Up/Down,
 * End, right Ctrl -- none of these exist on an Amiga keyboard), or the
 * scancode slot is unused/reserved and never produced by jjffe's own
 * DOS-scancode conversion table in the first place. */
#define FFE_RAWKEY_NONE 0xFF

/* ffe_scancode_to_rawkey[dosScancode] gives the AmigaOS IDCMP_RAWKEY code
 * (as delivered in InputEvent.ie_Code, key-down form, bit 7 clear) for the
 * SAME PHYSICAL KEY on an Amiga keyboard, or FFE_RAWKEY_NONE if there is no
 * such key. Indexed 0-255 by the DOS/PC set-1 scancode that
 * InputKeybReadStates(UCHAR *pKeys) in ffeapi.h expects to index pKeys by. */
extern const unsigned char ffe_scancode_to_rawkey[256];

#ifdef __cplusplus
}
#endif

#endif /* FFE_KEYMAP_H */
