/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 angree. See LICENSE at the repository root.
 *
 * The platform surface Frontier: First Encounters expects, as implemented on
 * Amiga 68k.
 *
 * This is OUR OWN declaration, deliberately not a copy of ref/jjffe/ffeapi.h:
 * that file is part of the reference tree, carries the __CONFIGFILE__ machinery
 * of the DOS/SDL/Win32 builds and must never be modified or vendored. The
 * SIGNATURES below are fixed by the game, though -- the translated x86 code
 * calls these by name with cdecl arguments -- so they match ffeapi.h exactly,
 * including the three typedefs.
 *
 * Only the video, input, timer and system parts exist so far. Sound* and Dir*
 * are declared by the game as well, and land in later steps; nothing here
 * depends on them.
 *
 * NO AMIGA HEADERS HERE. This header is included by files that also include
 * the proto/ headers, and by files that must not; keeping it to plain C types
 * is the rule that made the OpenTTD port's amiga_gfx.h survive (Insert,
 * Remove, Allocate, struct Point... all collide the moment exec leaks into
 * game code).
 */

#ifndef FFE_FFEAPI_H
#define FFE_FFEAPI_H

#ifdef __cplusplus
extern "C" {
#endif

/* The three typedefs ref/jjffe/ffeapi.h uses, spelled so that a file which
 * ALSO includes the NDK's exec/types.h sees identical declarations instead of
 * a conflict. exec/types.h switches its own ULONG/USHORT between the C99
 * fixed-width types and plain unsigned long/short on exactly the condition
 * below (its __use_amiga_stdc_c99 test), and getting that wrong is a hard
 * error rather than a style point: uint32_t is `unsigned int` on
 * m68k-amigaos, which is NOT the same type as `unsigned long` even though
 * both are 32 bits wide, and gcc rejects the redefinition. Either way the
 * value is a 32-bit unsigned, which is all the game's cdecl ABI cares about.
 * UCHAR the NDK never declares -- it calls that one UBYTE. */
#if defined(__STDC__) && ((__STDC_VERSION__ >= 199901L) || defined(AMIGA_STDC_C99))
#include <stdint.h>
typedef unsigned char UCHAR;
typedef uint16_t      USHORT;
typedef uint32_t      ULONG;
#else
typedef unsigned char  UCHAR;
typedef unsigned short USHORT;
typedef unsigned long  ULONG;
#endif

/* FFE is a fixed 320x200 8bpp game: one back buffer of that size (see
 * DATA_007773 in ffedat.asm) and a 320x158 upper viewport inside it. Neither
 * number is configurable anywhere in the game, so they are constants here
 * rather than state. */
#define FFE_SCREEN_W 320
#define FFE_SCREEN_H 200

/* ---- system ------------------------------------------------------------ */

void SystemInit (void);
void SystemCleanup (void);

/* ---- video ------------------------------------------------------------- */

/* Copy h rows of w bytes into the screen buffer at x,y; the source advances by
 * jump + w per row. When w is 320 -- and only then -- this is also the frame
 * flip: FFE has no separate present call, the full-width blit IS the present.
 * See ref/jjffe/sdl/sdlvideo.cpp, "One flip per frame". */
void VideoBlit (UCHAR *pData, long x, long y, long w, long h, long jump);

/* As VideoBlit, but source bytes equal to 0 are transparent, and the source
 * advances by jump per row (NOT jump + w -- the inner loop walks the source
 * itself). Never flips. */
void VideoMaskedBlit (UCHAR *pData, long x, long y, long w, long h, long jump);

/* The other direction: screen -> pData, source stride jump + w. The game uses
 * it to save the area under the mouse pointer. Never flips. */
void VideoReverseBlit (UCHAR *pData, long x, long y, long w, long h, long jump);

/* pVal points at 3 bytes r,g,b in the 6-bit VGA range 0..63. */
void VideoGetPalValue (long palindex, UCHAR *pVal);
void VideoSetPalValue (long palindex, UCHAR *pVal);

/* Non-zero means "the game owns the pointer and must draw it itself". We use
 * the Intuition pointer, so this returns 0. */
long VideoPointerExclusive (void);
void VideoPointerEnable (void);
void VideoPointerDisable (void);

/* ---- input ------------------------------------------------------------- */

/* Bit 0 left button held, bit 1 right button held. */
long InputMouseReadButtons (void);

/* Movement since the previous call; the accumulator is zeroed by the call. */
void InputMouseReadMickeys (long *pXMick, long *pYMick);

/* Absolute position in GAME coordinates, i.e. 0..319 / 0..199. */
void InputMouseReadPos (long *pXPos, long *pYPos);

/* Both are stubs, exactly as in ref/jjffe/sdl/sdlinput.cpp. */
void InputJoyReadPos (long *pXPos, long *pYPos);
long InputJoyReadButtons (void);

/* Fills 256 entries indexed by DOS/PC set-1 scancode. THE POLARITY IS
 * INVERTED: 0x80 means the key is NOT held, 0x00 means it IS. */
void InputKeybReadStates (UCHAR *pKeyArray);

/* "Last key pressed", for text entry. 0xff when nothing is pending. */
long InputKeybGetLastKey (void);
void InputKeybSetLastKey (long key);

/* ---- timer ------------------------------------------------------------- */

/* The game clock, in milliseconds but quantised to 5 ms steps. */
ULONG TimerGetTimeStamp (void);

/* Advance the clock, pump input, then forget the time spent in the frame. */
void TimerFrameUpdate (void);

/* Sleep about a millisecond. */
void TimerSleep (void);

/* ---- sound -------------------------------------------------------------
 *
 * Implemented on Paula in ffe_sound.c, which also carries the argument for the
 * sixteen-software-channels -> four-DMA-channels priority policy. Every one of
 * these is a safe no-op when audio.device would not give up Paula. */

/* Non-zero *pAll and *pDigi once Paula is open; *pMidi is always 0. */
void SoundCheckInit (long *pAll, long *pDigi, long *pMidi);

/* vol is the game's 0..0x7fff scale, pitch is Hz with -1 meaning 22050. */
void SoundPlaySample (long index, long vol, long pitch);
void SoundStopAllSamples (void);

/* Music: stubs, as in ref/jjffe/sdl/sdlsound.cpp. SoundSongDone returns 0. */
void SoundPlaySong (long index);
void SoundStopSong (void);
long SoundSongDone (void);

/* Speech streaming for the bulletin-board videos: stubs, and the floppy
 * release has no speech to stream. GetUsedBlocks returns 0, Wait returns at
 * once rather than spinning. */
long SoundStreamGetUsedBlocks (void);
void SoundStreamReset (void);
void SoundStreamAddBlock (void *pData, long size);
void SoundStreamStart (void);
void SoundStreamWait (void);
void SoundStreamStop (void);

/* ---- Amiga-side internals ----------------------------------------------
 *
 * Not part of FFE's API: the game never calls any of these. They live here
 * rather than in a private header because the platform layer is four small
 * translation units that have to agree on init order and on one shared input
 * pump, and a fifth file carrying six prototypes would not earn its keep. */

void ffe_video_init (void);
void ffe_video_cleanup (void);
void ffe_input_init (void);
void ffe_input_cleanup (void);
void ffe_timer_init (void);
void ffe_timer_cleanup (void);

/* Sound. ffe_sound_init opens Paula and loads the 40-slot sample set into Chip
 * RAM; it must run AFTER ffe_dir_init, because the loader builds its paths with
 * DirMakeSampleName() and would otherwise look for every sample in the current
 * directory. (It no longer depends on the timer: channel stealing became
 * immediate once AmigaAudio_Stop existed, so there is nothing left to time.)
 * ffe_sound_cleanup closes audio.device before freeing the buffers Paula was
 * reading. */
void ffe_sound_init (void);
void ffe_sound_cleanup (void);

/* Re-arm looping voices, reap finished channels and start any queued alert.
 * Paula plays a buffer once, so a descriptor loopcount only happens if this is
 * called regularly -- once per frame from TimerFrameUpdate is where it belongs.
 * Every Sound* entry point calls it first, so a caller that forgets still gets
 * one-shots. */
void ffe_sound_service (void);

/* Override the directory the <name>.raw samples are read from. Empty by
 * default, in which case the loader asks DirMakeSampleName() and so follows
 * [DIR] SamplePath from the config. Only for test programs that do not read a
 * config. Must be set before ffe_sound_init; keep the trailing separator. */
void ffe_sound_set_sample_path (const char *path);

/* Load report, for the probes. Returns 1 when the slot holds playable data, 0
 * when it is silent by design ("nowt") and -1 when the file was missing, empty
 * or would not fit in Chip RAM. Any out parameter may be NULL. */
int ffe_sound_sample_info (int index, const char **pname,
                           long *pfilebytes, long *pchipbytes);

/* Chip RAM the whole sample set holds, in bytes. */
long ffe_sound_chip_bytes (void);

/* What Paula channel ch is playing: sample slot and priority class, -1 and 0
 * when idle. Returns non-zero while the channel is busy. */
int ffe_sound_chan_info (int ch, long *pindex, long *pprio);

/* Drain the Intuition event queue into the input state: pointer position,
 * mickey accumulators, mouse buttons and the raw-key held[] array. Called by
 * TimerFrameUpdate and by every input reader, so a game that polls input
 * without ever calling TimerFrameUpdate still sees fresh state. */
void ffe_pump_input (void);

/* Non-zero once SystemInit has brought the screen and the layer up. SystemInit
 * cannot report failure -- its signature belongs to the game -- so this is how
 * a test program tells "screen refused to open" from "nothing drawn yet". */
int ffe_system_ready (void);

/* ---- config file -------------------------------------------------------
 *
 * ABI, not ours: ffe.asm:1781-1784 declares CfgOpen, CfgClose, CfgFindSection
 * and CfgGetKeyVal EXTERN and FUNC_001416_TimerInit calls all four to read
 * [TIMER] maxfps. The struct layout below is field for field the one in
 * ref/jjffe/ffecfg.h, and it must not grow past 32 bytes -- that is how much
 * the game reserves for it (`pTmCfgStruct times 32 db 0x0`, ffe.asm:85556).
 * Five 4-byte members = 20 bytes on 68k, the same as on x86.
 *
 * CfgGetKeyStr and CfgGetKeyValDef the game never calls; they exist because
 * ffecfg.h declares them and the platform layer itself reads its settings
 * through them. */

/* Where the Amiga platform layer keeps ITS settings -- [DIR], and whatever the
 * other parts of the layer come to need. Not the same file as the game's own
 * jjffe.cfg, which the game opens by its own hard-coded name. */
#define FFE_CONFIGFILE "PROGDIR:ffeamiga.cfg"

typedef struct {
	char *name;
	char *data;
} CfgKey;

typedef struct {
	char *name;
	int   keycount;
	CfgKey *keys;
} CfgSection;

typedef struct {
	char *filename;
	int   wasmodified;
	int   sectioncount;
	CfgSection *sections;
	CfgSection *currentsection;
} CfgStruct;

/* 1 on success, 0 on failure, throughout. A failed CfgOpen leaves the struct
 * zeroed, so every call below is safe on it. */
int CfgOpen (CfgStruct *cfg, char *filename);
int CfgClose (CfgStruct *cfg);
int CfgFindSection (CfgStruct *cfg, char *sectname);
int CfgGetKeyVal (CfgStruct *cfg, char *keyname, int *value);
int CfgGetKeyStr (CfgStruct *cfg, char *keyname, char *value, int buflen);

/* Returns 1 if the key was found, 0 if def was used -- and def is stored either
 * way, including after a failed CfgOpen or CfgFindSection. */
int CfgGetKeyValDef (CfgStruct *cfg, char *keyname, int *value, int def);

/* ---- directories -------------------------------------------------------- */

/* pName is SIXTEEN bytes because that is what ref/jjffe/ffeapi.h says and the
 * game's own buffers are that size. type: 0 file, 2 directory (name carries a
 * trailing '/'), 4 the "(Parent Dir)/" entry, 6 a volume ("Work:"). */
typedef struct {
	char pName[16];
	int  type;
	int  size;
} FileInfo;

char *DirMakeAVIName (char *pBuf, char *pStub);
char *DirMakeSampleName (char *pBuf, char *pFilename);
char *DirMakeSongName (char *pBuf, char *pFilename);
char *DirMakeCmmdrName (char *pBuf, char *pFilename);
char *DirGetCmmdrPath (void);
void DirResetCmmdrPath (void);

/* The save browser's iterator. DirFindFirst resets the walk and returns the
 * first volume; DirFindNext then yields the remaining volumes, the parent
 * entry, every subdirectory and finally every plain file, and returns 0 when
 * there is nothing left. DirNavigateTree applies one entry to the commander
 * path: descend (2), up one level (4) or switch volume (6). */
void DirNavigateTree (FileInfo *pFile);
int DirFindFirst (FileInfo *pFile);
int DirFindNext (FileInfo *pFile);

/* Ours, not the game's: read the four [DIR] paths and enumerate the volumes.
 * ffe_dir_init uses FFE_CONFIGFILE; the _from variant takes the file name so a
 * probe can point the layer at a config it wrote itself. */
void ffe_dir_init (void);
void ffe_dir_init_from (const char *cfgfile);
void ffe_dir_cleanup (void);

/* ---- misc --------------------------------------------------------------- */

/* Clip the rectangle to the 320x200 screen, fix up the source pointer and the
 * jump, then call BlitFunc with the result. ABI again: ffe.asm:1786 declares it
 * EXTERN and the game calls it from fourteen sites, passing one of its OWN blit
 * routines as the last argument (ffe.asm:97056 pushes _BufferBlit). Nothing is
 * called at all when the rectangle clips away to nothing. */
void BlitClipWrapper (void *pData, int xpos, int ypos, int width, int height,
                      int jump, void (*BlitFunc)(void *, int, int, int, int, int));

#ifdef __cplusplus
}
#endif

#endif /* FFE_FFEAPI_H */
