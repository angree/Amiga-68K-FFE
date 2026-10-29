/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 angree. See LICENSE at the repository root.
 *
 * Differential test, 68k side: runs the TRANSLATED functions over the same
 * vectors the x86 original ran under toolsoracleoracle.c, and prints the same
 * TSV.
 *
 * The first three are the floor the whole 3D engine stands on -- FUNC_001521
 * alone has 441 static call sites in ffe.asm -- and they are also the first
 * output of tools\translator\translate.py, so this probe is really a test of
 * the translator, not of the arithmetic. The five that follow each exist to
 * force one thing the translator could get wrong: a carry that has to survive a
 * store between the ADD and the ADDX (1333, 1335), multi-block control flow
 * (1656), a shift whose destination is memory and whose count is in a register
 * (1341), and a translated function calling a translated function (1669, nine
 * times into 1521). The ninth, FUNC_001601_DrawHLine, forces three more: a
 * scaled-index LEA (the x320 row stride), an 8-bit store, and reading a pointer
 * out of the game's own static data.
 *
 * WHY A TSV AND NOT PASS/FAIL: the authority on what these functions do is the
 * original x86 code, called directly on a PC (build\oracle\vectors-x86.tsv), NOT
 * a model of it restated in C. So this side records rather than judges, and
 * tools\translator\compare.py does the judging by diffing the two files. Every
 * field is a NUMBER, printed decimal, so byte order never enters the comparison
 * -- which is the point, given that the entire port is a little-endian to
 * big-endian move.
 *
 * The vector array below is a verbatim copy of ffe_vec[] in
 * toolsoracleoracle.c, and the three loops emit records in the same order.
 * Both of those are load-bearing: the comparison is positional.
 *
 * Log goes to PROGDIR:ffe_xlattest.log with one fflush per line, same as
 * amiga/probes/ffe_dirtest.c -- the run script detaches us with
 * `Run >NIL: <NIL:` and nothing else survives that.
 */

#include <stdio.h>
#include <string.h>

#include "ffe_bufdesc.h"

/* The translated code. translate.py emits a C-ABI entry named _FUNC_nnnnnn for
 * each function, which is exactly what gcc's decoration of these plain
 * declarations asks the linker for. That entry exists because the translated
 * body follows the t-ABI of PLAN 3.3, in which d2 (edx) and d7 (scratch) are
 * caller-saved -- the opposite of the m68k C ABI. Calling the body directly
 * would corrupt this file's own loop counters. */
extern int FUNC_001521 (int a, int b);		/* (a*b)>>31   */
extern int FUNC_001522 (int a);			/* (a*a)>>32   */
extern int FUNC_001523 (int a, int b);		/* fixp divide */

/* The four that write their result through a pointer argument. Signatures are
 * the frame offsets in ffe.asm read as cdecl arguments, so a 64-bit value is
 * two plain ints, low half first -- never a `long long`, which would drag the
 * two compilers' ideas of how to pass one into a test that is supposed to be
 * about the translation. */
extern void FUNC_001333 (int *r, int alo, int ahi, int blo, int bhi);
extern void FUNC_001335 (int *r, int alo, int ahi, int blo, int bhi);
extern void FUNC_001341 (int *val, int shift);
extern int  FUNC_001656 (int x);
extern void FUNC_001669 (int *out, const int *vec, const int *mat);
extern void FUNC_001601 (int xpos, int ypos, int length, int col);
/* The two clip wrappers above it. 1599 calls 1600 calls 1601, all three of them
 * translated -- so this is the first three-deep chain of translated code, and
 * the first time a translated function calls another one that itself calls a
 * third. Both come in through the C-ABI entry points (the leading underscore
 * the compiler adds), which is what keeps d2/d7 from being clobbered under the
 * probe's own loop counters. */
extern void FUNC_001600 (int xpos, int ypos, int length, int col);
extern void FUNC_001599 (int xpos, int ypos, int length, int col);

/* The game's own point, as DrawLine reads it: two 16-bit signed fields, four
 * bytes, no padding -- `word [edi]` and `word [edi+2]` at ffe.asm:96773. This
 * declaration is compiled for m68k and the matching one in toolsoracleoracle.c
 * for x86, so each machine builds a point in its own byte order and each
 * machine's code reads it back in that same order. Nothing about this struct
 * crosses between them, which is why DrawLine is a 16-bit INSTRUCTION test and
 * not an endianness one -- the endianness work starts at Sin16/Sin32, whose
 * table lives in the game's own static data. */
typedef struct { short x, y; } FfePoint;

/* FUNC_001616_DrawLine. It interpolates a 16.16 x down the scanlines and calls
 * FUNC_001601 once per row -- DIRECTLY, not through FUNC_001600, so it clips
 * nothing. Hence the guarded buffer below. */
extern void FUNC_001616 (const FfePoint *p1, const FfePoint *p2, int col);

/* FUNC_001604_DrawTriangle, 749 instructions -- by a factor of three the
 * largest function translated so far. It bypasses DrawHLine entirely: it sorts
 * the three vertices by y, interpolates two 16.16 x edges and fills each row
 * with a direct call to _memset. That call is the first time translated code
 * calls OUT to a C-ABI function, and it needs no thunk -- the thunk exists for
 * the other direction, because C calling into a translated body would have its
 * d2/d7 clobbered. A C callee saves strictly more than the t-ABI requires, and
 * cdecl argument layout is m68k C argument layout. */
extern void FUNC_001604 (const FfePoint *a, const FfePoint *b,
                         const FfePoint *c, int col);

/* FUNC_001607_DrawQuad -- THE CARRIER FOR INDIRECT CALL (PLAN 5). DrawQuad
 * pushes the bare t-ABI label FUNC_001601 (`pea (FUNC_001601).l`) as a sixth
 * argument to FUNC_001608_DrawQuadInternal, whose edge walker
 * FUNC_001602_DrawPolySegment then does `movea.l (36,a5),a0` + `jsr (a0)` --
 * the first function address to travel through a stack slot and be called
 * from it. Both ends of that journey are inside translated code, so it is
 * t-ABI throughout and no thunk is involved. The family also carries the
 * flag-exact An add/sub detour (1603) and the An-source one-operand IMUL
 * (1602). Spans land through the long-green 1601 and FUNC_001600. */
extern void FUNC_001607 (const FfePoint *a, const FfePoint *b,
                         const FfePoint *c, const FfePoint *d, int col);

/* The 64-bit and vector maths batch. Chosen by measurement: a dry run of the
 * translator over all 1,615 function labels says 407 already translate clean,
 * so the bottleneck is VERIFICATION rather than translation, and these six are
 * the cheapest clean ones to verify -- the vector tables above already drive
 * their siblings. Signatures and buffer sizes are read off the [ebp+d] frame
 * offsets and the displacements each pointer is dereferenced at; see the long
 * comment in toolsoracleoracle.c.
 *
 * 1671's SECOND argument is a caller-supplied SCRATCH vector, three ints, that
 * the routine writes and reads back between calls. Its final contents are part
 * of the record, so a translation that got the scratch pointer wrong cannot
 * pass on the strength of a correct `out`. */
extern void FUNC_001334 (int *r, int alo, int ahi, int b);
extern void FUNC_001339 (int *r, int a, int b);
extern void FUNC_001661 (int *a, const int *b);
extern void FUNC_001662 (int *a, const int *b);
extern void FUNC_001670 (int *out, const int *vec, const int *mat);
extern void FUNC_001671 (int *out, int *tmp, const int *vec, const int *mat);

/* ---- the pure-leaf batch --------------------------------------------------
 *
 * Twenty-five functions driven by one table. Selected by a script against three
 * conditions that together make a plain-integer call safe: no CALL, no named
 * DATA_ and no memory operand based on anything but ebp/esp (so no argument is
 * ever dereferenced), and every [ebp+d>=8] a full DWORD access.
 *
 * That third condition held four functions BACK. FUNC_001491..001494 read
 * `word [ebp+8]` and `word [ebp+0xa]` -- two shorts packed into ONE pushed
 * longword -- which is endianness-sensitive at the ABI boundary: on x86 the low
 * half of the pushed dword is at +8, on this machine the HIGH half is. Passing
 * a single `int` from C would hand the two machines a different pair of shorts
 * and the disagreement would look like a translator bug. See LEFTOFF.md.
 *
 * Every entry is declared int(int,int,int) and called with three arguments
 * whatever its real arity: cdecl and the m68k C ABI both pass all arguments on
 * the stack and both make the caller clean up, so surplus ones are never read.
 *
 * The nine zero-argument entries are empty stubs and a static check confirms
 * none of them ever writes eax, so their return register holds whatever the
 * caller left there. It is NOT recorded -- a constant 0 goes in the record
 * instead. What they do prove is the one thing that IS observable about them:
 * that they return with the stack intact. */
typedef int (*FfeLeaf3) (int, int, int);

extern int FUNC_000023 (int, int, int);
extern int FUNC_000039 (int, int, int);
extern int FUNC_000455 (int, int, int);
extern int FUNC_000456 (int, int, int);
extern int FUNC_000660 (int, int, int);
extern int FUNC_000693 (int, int, int);
extern int FUNC_000710 (int, int, int);
extern int FUNC_000715 (int, int, int);
extern int FUNC_000838 (int, int, int);
extern int FUNC_001017 (int, int, int);
extern int FUNC_001030 (int, int, int);
extern int FUNC_001349 (int, int, int);
extern int FUNC_001501 (int, int, int);
extern int FUNC_001666 (int, int, int);
extern int FUNC_001715 (int, int, int);
extern int FUNC_001716 (int, int, int);
extern int FUNC_001722 (int, int, int);
extern int FUNC_001727 (int, int, int);
extern int FUNC_001728 (int, int, int);
extern int FUNC_001729 (int, int, int);
extern int FUNC_001798 (int, int, int);
extern int FUNC_001824 (int, int, int);
extern int FUNC_001831 (int, int, int);
extern int FUNC_001832 (int, int, int);
extern int FUNC_001836 (int, int, int);

/* TWO SWITCHES, because the sub-word-stack problem split in half.
 *
 * FFE_SUBWORD_STACK_FIXED covers FUNC_001612_DrawCircle, whose colour slot the
 * frame map PROVES is a 4-byte scalar (`mov eax,[ebp+0x10]` appears at the
 * other call site), so `mov al,[ebp+0x10]` provably means its low byte and the
 * translator now emits (19,a5). Fixed and on.
 *
 * FFE_PACKED_SHORT_ARGS covers FUNC_001491..001494, which read `word [ebp+0x8]`
 * and `word [ebp+0xa]` and NEVER touch that slot at dword width. Nothing can
 * tell a packed pair of shorts from a struct of two shorts passed by value, and
 * the two want different code, so they stay refused. Answering them means
 * looking at what their callers push -- not at these four functions.
 *
 * The vectors below are correct and were validated against the real x86; they
 * are off because the translated side does not exist. Must stay in step with
 * the switches of the same names in the other file. */
#define FFE_SUBWORD_STACK_FIXED  1
#define FFE_PACKED_SHORT_ARGS    0


#if FFE_PACKED_SHORT_ARGS
/* ---- FUNC_001491..001494: TWO SHORTS PACKED INTO ONE PUSHED LONGWORD -----
 *
 * The first case in this project where the ENDIANNESS IS IN THE ARGUMENT. These
 * four read `word [ebp+0x8]` AND `word [ebp+0xa]`: one cdecl argument, two
 * 16-bit fields inside it. Passing a plain `int` would hand the two machines a
 * DIFFERENT PAIR of shorts, because on x86 the low half of the pushed dword is
 * at +8 and here the HIGH half is -- and the disagreement would look exactly
 * like a translator bug without being one.
 *
 * A struct by value fixes it, and that was verified on this compiler rather
 * than assumed: m68k gcc turns `f((struct P){0x1122,0x3344})` into
 * `move.l #$11223344,-(sp)`, so `word (8,a5)` is `a` and `word (10,a5)` is `b`
 * -- the same roles MinGW gives them on the other side.
 *
 * The vectors stay inside each routine's safe exponent range on purpose: all
 * four end in a variable shift by `15-b` or `31-b`, and x86 masks a 32-bit
 * shift count to five bits where the 68k takes it modulo 64. That is PLAN 3.6's
 * accepted risk and this is its first live instance; see the long comment in
 * toolsoracleoracle.c for the exact per-function predicate. */
typedef struct { short a, b; } FfePacked;

extern int FUNC_001491 (FfePacked p);
extern int FUNC_001492 (FfePacked p);
extern int FUNC_001493 (FfePacked p);
extern int FUNC_001494 (FfePacked p);
#endif /* FFE_PACKED_SHORT_ARGS */

/* ---- FUNC_001461_GuiAddHotRect2 -----------------------------------------
 *
 * void GuiAddHotRect2 (int x, int y, int w, int h, int id) -- ffe.asm:86596.
 * The function that lands the two new translator rules honestly. It opens with
 * `mov eax,DATA_007683` -- a bare label as a SOURCE, which in NASM is the
 * label's ADDRESS, not its contents. That was the second largest refusal class
 * in the game (136 functions).
 *
 * DATA_007683 is the GUI hot-rect table: 129 entries of {x0,y0,x1,y1,id}, 20
 * bytes each, 2580 bytes in all (ffedat.asm:23317..23640), terminated by an x0
 * of -1. The routine walks forward 20 bytes at a time to that sentinel, writes
 * a fresh one into the slot after the one it is about to fill, then stores
 * x0=x, y0=y, x1=x+w, y1=y+h, id.
 *
 * THIS SIDE DEFINES THE TABLE, exactly as it defines DATA_007773: the
 * translated .s only references the symbol. It is declared as ints rather than
 * as a copy of the 2580 bytes for the reason set out in ffe_bufdesc.h -- the
 * fields are longwords and each machine must lay them out in its own order.
 * The initial contents are ffedat.asm:23318 read as longwords: -1 then zeros. */
#define HR_ENTRIES 129
#define HR_WORDS   (HR_ENTRIES * 5)

int DATA_007683[HR_WORDS] __asm__("DATA_007683") = { -1 };

extern void FUNC_001461 (int x, int y, int w, int h, int id);

/* The other two thirds of the hot-rect API, and the carriers for `rep movsd`.
 * 001440_GuiAddHotAreas appends a whole -1-terminated list, one `rep movsd`
 * (ecx = 5) per entry; 001443_GuiRemoveHotArea shifts entries DOWN over the
 * removed one, which is an OVERLAPPING forward copy -- and the 68k loop does
 * the same thing in the same direction. They cost almost no new harness because
 * they mutate the same DATA_007683 the 001461 vectors already reset and record.
 *
 * The direction is forward and that is MEASURED: the whole of ffe.asm contains
 * zero `std` and zero `cld`, so DF is never set. */
extern void FUNC_001440 (const int *src);

/* ---- the indexed-addressing batch ---------------------------------------
 *
 * Four functions that land the 68020 indexed load/store. `ea()` used to refuse
 * every `[base+index*scale+disp]` operand -- 103 functions -- because an
 * untested addressing mode is worse than a refusal. Both shapes are tested:
 *
 *   001565  xor [ecx+edx*4+0x9bd4],eax  -- a LARGE displacement, 39892, far
 *           outside the brief format, so this is the first translated function
 *           to need the 68020 FULL extension word.
 *   001558  mov [eax+edx*4+0x4],ecx     -- base + index*4 + small displacement.
 *   000093  mov [eax*4+DATA_008568],edx -- the SUPPRESSED-BASE form, written
 *   000094  mov eax,[eax*4+DATA_008568] -- ... and read back.
 *
 * The suppressed-base form is emitted as `lea (DATA_x).l,a0` plus the ordinary
 * indexed mode rather than the 68020's own base-suppressed encoding: one extra
 * instruction, and it stays inside an encoding already shipped and verified.
 * DATA_008568 is `resb 400` -- 100 ints -- so the indices stay inside 0..99. */
extern void  FUNC_001565 (int idx, int val, void *ctx);
extern void *FUNC_001558 (void *obj, int val);
extern void  FUNC_000093 (int idx, int val);
extern int   FUNC_000094 (int idx);

int DATA_008568[100] __asm__("DATA_008568");

/* ---- the byte-MOVSX and truncating-IMUL batch ---------------------------
 *
 * `movsx r32,byte src` is the byte twin of the word rule: MOVE.B then EXTB.L,
 * a 68020 instruction, which is this build's formal target. 52 functions.
 * The 2- and 3-operand IMUL keep only the LOW 32 bits, i.e. plain
 * MULS.L <ea>,Dn -- no register pair. 46 functions.
 *
 *   000657  movsx edx,byte [edx+0xca]
 *   000024  imul eax,edx,0xc22e   -- the THREE-operand form, on the remainder
 *           of a `div`, so it re-exercises the unsigned 64/32 divide too
 *   001515  imul ecx,edx          -- the TWO-operand form, twice, in a
 *           function that also uses the one-operand IMUL */
extern int FUNC_000657 (int want, const void *p);
extern int FUNC_000024 (int x);
extern int FUNC_001515 (int a, int b);

/* ---- the SETcc / XCHG batch ----------------------------------------------
 *
 * x86 SETcc stores 0 or 1, the 68k Scc stores 0x00 or 0xFF, so it is `scc`
 * plus `neg.b`. The condition comes from the SAME table the branches use, not
 * a second one -- duplicating it would be a second chance to invert a
 * condition. All 96 SETcc sites in the game target al, cl or dl.
 *
 * `xchg` is `exg`, native and flag-free, matching x86 XCHG exactly.
 * `xor aN,aN` became SUBA.L An,An -- the clear-a-register idiom, and the only
 * two-byte way to clear an address register.
 *
 *   000028  test byte [p+0x14c],0x10 / setnz al / and eax,1
 *   001514  three 64-bit adds whose CARRY is detected by `cmp` + `seta`, i.e.
 *           SETcc feeding arithmetic rather than a branch */
extern int  FUNC_000028 (const void *p);
extern void FUNC_001514 (void *obj, const int *d3);
extern void FUNC_001443 (int id);

/* ---- the alignment-padding batch ----------------------------------------
 *
 * Three functions refused for a reason that had nothing to do with them: a dry
 * run over all 1,615 labels found 173 refused with "control falls off the end
 * of the function", and in ALL 173 the last instruction is a `nop` -- ffedis
 * pads each function to an alignment boundary and the CFG saw a NOP falling
 * through into nothing. parse._strip_padding() now drops that run, after
 * proving it unreachable, and the clean count went 516 -> 597.
 *
 * 000030's `word [eax+0xc]` is NOT the sub-word-stack problem: the field sits
 * at a fixed offset in a struct laid out by whichever compiler is running, so a
 * short declared at offset 12 is at offset 12 on both machines. The stack case
 * is different because a cdecl slot is a 4-byte SCALAR whose bytes are ordered,
 * not a struct whose fields are placed. */
int DATA_008861 __asm__("DATA_008861");
int DATA_008890 __asm__("DATA_008890");
int DATA_009105 __asm__("DATA_009105");

/* The label-address / movzx batch. Sizes match what the x86 image gives each
 * label PLUS the deliberate spills the originals perform: FUNC_000038's
 * record 1 runs past the 24-byte resb (into the resb neighbours on the PC),
 * and FUNC_000178 copies 24 bytes over a 20-byte struct. One array per
 * symbol here reproduces the same contiguous layout. */
unsigned char DATA_008561[128]  __asm__("DATA_008561");
unsigned char DATA_008661[28]   __asm__("DATA_008661");
unsigned char DATA_007129[1028] __asm__("DATA_007129");
unsigned char DATA_007751[1160] __asm__("DATA_007751");
int   DATA_007830 __asm__("DATA_007830");
int   DATA_007831 __asm__("DATA_007831");
void *DATA_007832 __asm__("DATA_007832");
int   DATA_009255 __asm__("DATA_009255");

/* The base+symbol batch: FUNC_001327_DPal16toPal, three [reg+DATA] operands
 * and both remaining MOVZX shapes -- see the oracle's comment. */
unsigned char DATA_007770[4096] __asm__("DATA_007770");
unsigned char DATA_009139[4096] __asm__("DATA_009139");
unsigned char DATA_009140[8]    __asm__("DATA_009140");
extern int FUNC_001327 (int key);

extern int  FUNC_000038 (int key);
extern void FUNC_000178 (const void *src);
extern void FUNC_001785 (int arg);
extern int  FUNC_000412 (const void *a, const void *b);
extern int  FUNC_000417 (const void *p);
extern int  FUNC_000896 (int key);
extern int  FUNC_001597 (int ch);


extern int FUNC_000402 (int id);
extern int FUNC_000427 (int id);

typedef struct { unsigned char pad[12]; unsigned short v; } FfeThing;

extern int FUNC_000030 (const FfeThing *p);

/* ---- the operand-width batch --------------------------------------------
 *
 * Two functions that land the widening of every arithmetic rule. `test`, `and`,
 * `or`, `xor`, `not`, `neg`, `inc`, `dec`, `add`, `sub`, `adc`, `sbb` and the
 * shifts all used to emit a hardcoded `.l` and ignore the operand width, so
 * anything narrower was refused -- 165 functions. Threading the width through
 * took the clean count 597 -> 675, and all 49 already-green .o files came out
 * BYTE-IDENTICAL, which is what proves the change inert at 32 bits.
 *
 * 001340_Int64LogicShift is the sibling of the green 001341_Int64ArithShift and
 * shares its signature, so it reuses that loop exactly. It was blocked by a
 * second bug the widening exposed: a shift's width comes from its DESTINATION,
 * not from the 8-bit count in cl.
 *
 * 000422 does ten BYTE comparisons in a loop and is the one that actually
 * exercises `.b` arithmetic. */
extern void FUNC_001340 (int *val, int shift);
extern int  FUNC_000422 (const unsigned char *a, const unsigned char *b);

#if FFE_SUBWORD_STACK_FIXED
/* FUNC_001612_DrawCircle (const FfePoint *centre, int size, PALCOL col)
 * -- ffe.asm:96457, 164 instructions. The second carrier for the
 * `lea rD,[ebp-N]` rule and the more demanding one: it has a SEVEN-KILOBYTE
 * local frame (7608 bytes of span table at [ebp-7608]) whose address it takes
 * with exactly the instruction the rule was added for. The run script already
 * launches this probe under `Stack 1000000`, so the frame is fine.
 *
 * It fills the span table, then walks it calling the already-green
 * FUNC_001601_DrawHLine once per row -- an outline, two pixels a row, not a
 * disc. y is clipped at both ends (top clamped to 0, bottom to 158); x is not
 * clipped at all, which is why one case below runs off the left edge on
 * purpose and why the buffer keeps its guards. */
extern void FUNC_001612 (const FfePoint *centre, int size, int col);
#endif

/* The back buffer the translated FUNC_001601 draws into, as the game's own
 * descriptor. Written out as C fields rather than as a copy of the 24 bytes in
 * ffedat.asm, for the reason set out at length in ffe_bufdesc.h: 320 is
 * `0x40,0x01,0,0` there and would arrive here as 0x40010000.
 *
 * pixels is filled in at run time; the rest is what the original holds, so that
 * anything which later reads w/h out of this descriptor sees the same numbers
 * the x86 does. DrawHLine itself reads only the pointer. */
FfeBufDesc DATA_007773 = { 0, 0, 0, 320, 200, 0 };

static FILE *g_log;
static int   g_records;

/* FUNC_000038 carries `pea (FUNC_000045).l` + `jsr FUNC_000033` on a path
 * every vector avoids (field +8 is always 0). The linker still wants both
 * symbols, so they are stubs -- and a stub that RUNS writes a line into the
 * log, which throws the record count off and fails the compare loudly,
 * exactly as a silently-returning stub would not. */
static void stub_hit (const char *who)
{
	fprintf (g_log, "STUB %s CALLED\n", who);
	fflush (g_log);
	g_records++;
}
void STUB_FUNC_000033 (void) __asm__("FUNC_000033");
void STUB_FUNC_000033 (void) { stub_hit ("FUNC_000033"); }
void STUB_FUNC_000045 (void) __asm__("FUNC_000045");
void STUB_FUNC_000045 (void) { stub_hit ("FUNC_000045"); }
/* FUNC_000781's two callees sit on paths its vectors never take on a CORRECT
 * translation -- a wrong movzx/movsx extension walks into one of them and
 * the stub line breaks the record count. */
void STUB_FUNC_000783 (void) __asm__("FUNC_000783");
void STUB_FUNC_000783 (void) { stub_hit ("FUNC_000783"); }
void STUB_FUNC_000048 (void) __asm__("FUNC_000048");
void STUB_FUNC_000048 (void) { stub_hit ("FUNC_000048"); }

static const int ffe_vec[] = { 0, 1, -1, 2, -2, 3, 0x7fffffff, (int)0x80000000,
                               0x40000000, (int)0xc0000000, 0x12345678,
                               (int)0xdeadbeef, 65536, -65536, 1000000, -1000000 };

#define FFE_NVEC ((int)(sizeof ffe_vec / sizeof ffe_vec[0]))

/* Verbatim copies of ffe_i64[]/ffe_shift[]/ffe_msb[]/ffe_vmvec[]/ffe_vmmat[]
 * in toolsoracleoracle.c, for the same reason ffe_vec[] is one: the
 * comparison is positional, so the two vector sets and the two loop nests have
 * to agree exactly. */
static const int ffe_i64[][2] = {
	{          0,           0 },
	{          1,           0 },
	{         -1,           0 },
	{          0,           1 },
	{         -1,          -1 },
	{         -2,          -1 },
	{ (int)0x80000000,      0 },
	{ 0x7fffffff,          0 },
	{          0, (int)0x80000000 },
	{         -1,  0x7fffffff },
	{ 0x12345678, (int)0x9abcdef0 },
	{ (int)0xdeadbeef, 0x0000cafe },
};

#define FFE_NI64 ((int)(sizeof ffe_i64 / sizeof ffe_i64[0]))

static const int ffe_shift[] = { 0, 1, 2, 8, 31, 32, 33, 63,
                                 -1, -2, -8, -31, -32, -33, -63 };

#define FFE_NSHIFT ((int)(sizeof ffe_shift / sizeof ffe_shift[0]))

static const int ffe_msb[] = { 0, 1, 2, 3, 4, 5, 7, 8, 15, 16, 17, 31,
                               255, 256, 257, 0xffff, 0x10000, 0x10001,
                               0x7fffffff, (int)0x80000000, -1,
                               0x12345678, (int)0xdeadbeef };

#define FFE_NMSB ((int)(sizeof ffe_msb / sizeof ffe_msb[0]))

static const int ffe_vmvec[][3] = {
	{ 0, 0, 0 },
	{ 0x40000000, 0, 0 },
	{ 0, 0x40000000, 0 },
	{ 0x7fffffff, (int)0x80000000, 1 },
	{ 0x12345678, (int)0xdeadbeef, 0x40000000 },
	{ -1, 1, -65536 },
};

#define FFE_NVMVEC ((int)(sizeof ffe_vmvec / sizeof ffe_vmvec[0]))

static const int ffe_vmmat[][9] = {
	{ 0x7fffffff, 0, 0,  0, 0x7fffffff, 0,  0, 0, 0x7fffffff },
	{ 0, 0x7fffffff, 0,  (int)0x80000000, 0, 0,  0, 0, 0x7fffffff },
	{ 0x40000000, 0x40000000, 0x40000000,
	  0x40000000, (int)0xc0000000, 0x40000000,
	  0x12345678, (int)0xdeadbeef, 0x7fffffff },
	{ -1, -1, -1,  -1, -1, -1,  -1, -1, -1 },
};

#define FFE_NVMMAT ((int)(sizeof ffe_vmmat / sizeof ffe_vmmat[0]))

/* ---- FUNC_001601_DrawHLine ----------------------------------------------
 *
 * Verbatim copies of g_hlbuf[] / hl_emit() / hl_cases() in
 * toolsoracleoracle.c: same four-row buffer, same 0xAA fill, same case list
 * in the same order. Four rows because every span is drawn on row 1, which
 * leaves a guard row above and two below -- an overrun in either direction
 * moves `first` or `count` and shows up as a mismatch instead of vanishing.
 *
 * Alignment forced to 4, and it has to be SAID here rather than assumed: gcc on
 * m68k aligns a long to two bytes unless told otherwise, so a plain array might
 * start at an odd multiple of 2. It does not change the ANSWER -- DrawHLine
 * splits its phases on the x COORDINATE, not on the address, and a colour of
 * 0..255 gives a replicated longword whose four bytes are equal -- but it does
 * make this machine take the same path through the routine as the x86 did.
 */
#define HL_ROWS 4
#define HL_W    320
#define HL_N    (HL_W * HL_ROWS)
#define HL_FILL 0xAA

static unsigned char g_hlbuf[HL_N] __attribute__((aligned(4)));

static void hl_emit (int x, int y, int len, int col)
{
	unsigned char *buf = g_hlbuf;
	int i, first = -1, last = -1, count;

	memset (buf, HL_FILL, HL_N);
	DATA_007773.pixels = buf;
	FUNC_001601 (x, y, len, col);

	for (i = 0; i < HL_N; i++)
		if (buf[i] != HL_FILL) { if (first < 0) first = i; last = i; }
	count = (first < 0) ? 0 : last - first + 1;

	fprintf (g_log, "FUNC_001601\t%d\t%d\t%d\t%d\t%d\t%d",
	         x, y, len, col, first, count);
	for (i = 0; i < count; i++) fprintf (g_log, "\t%d", buf[first + i]);
	fprintf (g_log, "\n");
	fflush (g_log);
	g_records++;
}

static void hl_cases (void)
{
	int a, L;

	for (a = 0; a < 4; a++)
		for (L = 0; L <= 17; L++)
			hl_emit (64 + a, 1, L, 1 + (a * 18 + L));

	hl_emit (64, 1, -1, 5);
	hl_emit (64, 1, -100, 6);
	hl_emit (67, 1, -1, 7);
	hl_emit (0, 1, HL_W, 8);
	hl_emit (316, 1, 8, 9);
	hl_emit (65, 1, 255, 11);
	hl_emit (70, 1, 5, 0);
	hl_emit (70, 1, 5, 255);
}

/* ---- FUNC_001600_DrawHLineClip / FUNC_001599_DrawHLineConClip ------------
 *
 * Verbatim copies of g_clbuf[] / cl_emit() / cl_cases() in
 * toolsoracleoracle.c -- same guarded buffer, same case list, same order.
 * The model_clip() half is deliberately NOT copied: this side records what the
 * translated code did and compare.py holds it against the x86 record. A second
 * model here would only be a second chance to be wrong in the same way.
 *
 * The guards at BOTH ends carry the weight of this slice. 1599 gates on
 *
 *     cmp eax,0x9e ; jnc <return>
 *
 * which is UNSIGNED, so ypos = -1 (0xFFFFFFFF) is rejected along with
 * ypos >= 158. Translated as the signed `bge` instead of `bcc`, ypos = -1 would
 * pass the gate and write 320 bytes IN FRONT of the pixel origin. The front
 * guard turns that from an out-of-bounds write on the Amiga -- which on a
 * machine with no memory protection means corrupting whatever happens to live
 * there -- into a negative `first` that compare.py reports as a mismatch.
 */
#define CL_W     320
#define CL_ROWS  202
#define CL_GUARD (CL_W * 2)
#define CL_N     (CL_GUARD + CL_W * CL_ROWS + CL_GUARD)
#define CL_FILL  0xAA

static unsigned char g_clbuf[CL_N] __attribute__((aligned(4)));

static void cl_emit (int which, int x, int y, int len, int col)
{
	unsigned char *pix = g_clbuf + CL_GUARD;
	int i, first = -1, last = -1, count;

	memset (g_clbuf, CL_FILL, CL_N);
	DATA_007773.pixels = pix;
	if (which == 1600) FUNC_001600 (x, y, len, col);
	else               FUNC_001599 (x, y, len, col);

	for (i = 0; i < CL_N; i++)
		if (g_clbuf[i] != CL_FILL) { if (first < 0) first = i; last = i; }
	count = (first < 0) ? 0 : last - first + 1;
	first = (count == 0) ? 0 : first - CL_GUARD;

	fprintf (g_log, "FUNC_%06d\t%d\t%d\t%d\t%d\t%d\t%d",
	         which, x, y, len, col, first, count);
	for (i = 0; i < count; i++)
		fprintf (g_log, "\t%d", g_clbuf[CL_GUARD + first + i]);
	fprintf (g_log, "\n");
	fflush (g_log);
	g_records++;
}

static void cl_cases (void)
{
	static const int xl[][2] = {
		{  10,  20 }, {   0,  20 }, {   0, 320 }, {  -5,  20 },
		{  -1,   8 }, {  -2,   8 }, {  -3,   8 }, { -19,  20 },
		{ -20,  20 }, { -21,  20 }, {-100,  20 }, {  -5, 400 },
		{ 300,  20 }, { 300,  40 }, { 319,   1 }, { 319,  10 },
		{ 320,  10 }, { 400,  10 }, {  10,   0 }, {  10,  -5 },
		{   0,  -1 },
	};
	static const int ys[] = { 0, 1, 100, 157, 158, 159, 199, -1, -2 };
	const int nxl = (int)(sizeof xl / sizeof xl[0]);
	const int nys = (int)(sizeof ys / sizeof ys[0]);
	int i, j, c = 1;

	for (i = 0; i < nxl; i++)
		cl_emit (1600, xl[i][0], 1, xl[i][1], 1 + (c++ & 0xfe));
	for (j = 0; j < nys; j++)
		if (ys[j] >= 0)
			cl_emit (1600, 10, ys[j], 20, 1 + (c++ & 0xfe));

	for (j = 0; j < nys; j++)
		for (i = 0; i < nxl; i++)
			cl_emit (1599, xl[i][0], ys[j], xl[i][1], 1 + (c++ & 0xfe));
}

/* ---- FUNC_001616_DrawLine ------------------------------------------------
 *
 * Verbatim copy of g_lnbuf[] / ln_emit() / ln_cases() in toolsoracleoracle.c
 * -- same guarded buffer, same case list, same order, same record shape. The
 * colour invariant the oracle checks in its human-readable mode is deliberately
 * NOT copied: this side records and compare.py judges.
 *
 * The record is a PER-ROW SUMMARY rather than the raw bytes, because a
 * corner-to-corner diagonal spans ~64,000 bytes between its first and last
 * written pixel. Each touched row contributes five numbers -- row, first column,
 * extent, count of written bytes, and their sum -- which between them catch a
 * shifted span (first), a wrong length (extent/count), a gap (extent != count)
 * and a wrong colour (sum, since every byte written is col).
 *
 * Two guard ROWS at each end, not two guard bytes: DrawLine does not clip, so a
 * translation that got the y loop wrong would run off the top or the bottom of
 * the buffer, and on a machine with no memory protection that is silent
 * corruption rather than a report. Rows are numbered relative to the pixel
 * origin, so anything above it comes out negative.
 */
#define LN_W     320
#define LN_ROWS  202
#define LN_GUARD 2                        /* whole rows, at BOTH ends */
#define LN_TOTAL (LN_ROWS + 2 * LN_GUARD)
#define LN_N     (LN_TOTAL * LN_W)
#define LN_FILL  0xAA

static unsigned char g_lnbuf[LN_N] __attribute__((aligned(4)));

static void ln_emit (int x1, int y1, int x2, int y2, int col)
{
	unsigned char *pix = g_lnbuf + LN_GUARD * LN_W;
	FfePoint p1, p2;
	int r, i, nrows = 0;

	memset (g_lnbuf, LN_FILL, LN_N);
	DATA_007773.pixels = pix;
	p1.x = (short)x1; p1.y = (short)y1;
	p2.x = (short)x2; p2.y = (short)y2;
	FUNC_001616 (&p1, &p2, col);

	for (r = 0; r < LN_TOTAL; r++) {
		const unsigned char *row = g_lnbuf + r * LN_W;
		for (i = 0; i < LN_W; i++)
			if (row[i] != LN_FILL) { nrows++; break; }
	}

	fprintf (g_log, "FUNC_001616\t%d\t%d\t%d\t%d\t%d\t%d",
	         x1, y1, x2, y2, col, nrows);
	for (r = 0; r < LN_TOTAL; r++) {
		const unsigned char *row = g_lnbuf + r * LN_W;
		int first = -1, last = -1, n = 0, sum = 0;
		for (i = 0; i < LN_W; i++)
			if (row[i] != LN_FILL) {
				if (first < 0) first = i;
				last = i; n++; sum += row[i];
			}
		if (first < 0) continue;
		fprintf (g_log, "\t%d\t%d\t%d\t%d\t%d",
		         r - LN_GUARD, first, last - first + 1, n, sum);
	}
	fprintf (g_log, "\n");
	fflush (g_log);
	g_records++;
}

static void ln_cases (void)
{
	static const int pts[][4] = {
		{  10,   5,  50,   5 }, {  50,   6,  10,   6 },
		{ 100,   7, 100,   7 }, {  30,  10,  30,  40 },
		{  30,  50,  30,  20 }, {  10,  60,  40,  90 },
		{  40, 100,  10,  70 }, {  10,  10,  41,  40 },
		{  10, 110, 100, 120 }, {  10, 120,  20, 160 },
		{ 100,  10,  70,  40 }, { 100,  10,  40,  50 },
		{  70,  40, 100,  10 }, {   0,   0, 319,   3 },
		{   0, 100, 319, 100 }, {   0, 199, 319, 199 },
		{ 319,   0,   0, 199 }, {   0,   0, 319, 199 },
		{   0,   0,   0,   0 }, { -10,  50,  10,  50 },
		{ 310,  60, 330,  60 },
	};
	const int n = (int)(sizeof pts / sizeof pts[0]);
	int i, c = 1;

	for (i = 0; i < n; i++)
		ln_emit (pts[i][0], pts[i][1], pts[i][2], pts[i][3],
		         1 + (c++ & 0xfe));

	ln_emit (10, 150, 30, 170, 0);
	ln_emit (10, 150, 30, 170, 255);
	ln_emit (10, 150, 30, 170, 1);
}

/* ---- FUNC_001604_DrawTriangle --------------------------------------------
 *
 * Verbatim copy of g_trbuf[] / tr_emit() / tr_cases() in toolsoracleoracle.c
 * -- same guarded buffer, same case list in the same order, same record shape.
 * The two invariants the oracle checks in its human-readable mode are not
 * copied: this side records and compare.py judges.
 *
 * DrawTriangle DOES clip, in both axes, so a correct translation can never write
 * outside the 320x158 rectangle. The guards are here anyway because the y test
 * is the same UNSIGNED `cmp ebx,0x9e / jnc` that FUNC_001599 uses -- translate
 * it as the signed `bge` and the y = -50 and y = -1 vertices below write in
 * FRONT of the pixel origin. The guard rows turn that into a negative row number
 * in the record instead of into corrupted memory.
 *
 * The same triangle is submitted in all six vertex orders: the function opens
 * with a three-compare vertex sort, and that is the only way to walk every arm
 * of it. A translation that got one swap wrong gives six different answers where
 * the original gives one.
 */
static unsigned char g_trbuf[LN_N] __attribute__((aligned(4)));

static void tr_emit (int x0, int y0, int x1, int y1, int x2, int y2, int col)
{
	unsigned char *pix = g_trbuf + LN_GUARD * LN_W;
	FfePoint a, b, c;
	int r, i, nrows = 0;

	memset (g_trbuf, LN_FILL, LN_N);
	DATA_007773.pixels = pix;
	a.x = (short)x0; a.y = (short)y0;
	b.x = (short)x1; b.y = (short)y1;
	c.x = (short)x2; c.y = (short)y2;
	FUNC_001604 (&a, &b, &c, col);

	for (r = 0; r < LN_TOTAL; r++) {
		const unsigned char *row = g_trbuf + r * LN_W;
		for (i = 0; i < LN_W; i++)
			if (row[i] != LN_FILL) { nrows++; break; }
	}

	fprintf (g_log, "FUNC_001604\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d",
	         x0, y0, x1, y1, x2, y2, col, nrows);
	for (r = 0; r < LN_TOTAL; r++) {
		const unsigned char *row = g_trbuf + r * LN_W;
		int first = -1, last = -1, n = 0, sum = 0;
		for (i = 0; i < LN_W; i++)
			if (row[i] != LN_FILL) {
				if (first < 0) first = i;
				last = i; n++; sum += row[i];
			}
		if (first < 0) continue;
		fprintf (g_log, "\t%d\t%d\t%d\t%d\t%d",
		         r - LN_GUARD, first, last - first + 1, n, sum);
	}
	fprintf (g_log, "\n");
	fflush (g_log);
	g_records++;
}

static void tr_cases (void)
{
	static const int tri[][6] = {
		{  50,  10, 100,  60,  20,  90 },
		{  50,  10,  20,  90, 100,  60 },
		{ 100,  60,  50,  10,  20,  90 },
		{ 100,  60,  20,  90,  50,  10 },
		{  20,  90,  50,  10, 100,  60 },
		{  20,  90, 100,  60,  50,  10 },
		{  20,  20, 100,  20,  60,  80 },
		{ 100,  20,  20,  20,  60,  80 },
		{  60,  20,  20,  80, 100,  80 },
		{  60,  20, 100,  80,  20,  80 },
		{  50,  50,  50,  50,  50,  50 },
		{  20,  30, 100,  30,  60,  30 },
		{  20,  20,  20,  80,  20, 140 },
		{  10,  10,  50,  50,  90,  90 },
		{  90,  90,  50,  50,  10,  10 },
		{  10,  10,  11, 100,  12,  10 },
		{  10,  10, 300,  11,  10,  12 },
		{  20, 150, 100, 157,  60, 158 },
		{  20, 150, 100, 160,  60, 170 },
		{  20, -50, 100,  50,  60, 100 },
		{  20, -50, 100, -10,  60,  -1 },
		{  20, 200, 100, 250,  60, 300 },
		{ -60,  10,  60,  10,   0,  60 },
		{ 260,  10, 380,  10, 320,  60 },
		{-100,  10, 420,  20, 160, 100 },
		{   0,   0, 319,   0,   0, 157 },
	};
	const int n = (int)(sizeof tri / sizeof tri[0]);
	int i, c = 1;

	for (i = 0; i < n; i++)
		tr_emit (tri[i][0], tri[i][1], tri[i][2], tri[i][3],
		         tri[i][4], tri[i][5], 1 + (c++ & 0xfe));

	tr_emit (30, 30, 90, 40, 50, 100, 0);
	tr_emit (30, 30, 90, 40, 50, 100, 255);
	tr_emit (30, 30, 90, 40, 50, 100, 1);
}

/* ---- the 64-bit / vector maths batch -------------------------------------
 *
 * Verbatim mirror of v64_fill() / math_cases() in toolsoracleoracle.c: same
 * loop nests in the same order, same record shapes. The 3x int64 vectors are
 * built by sliding a window over ffe_i64[] rather than from a second table, so
 * the two machines cannot disagree about the inputs without also disagreeing
 * about ffe_i64 itself -- which the FUNC_001333/001335 records already pin down.
 */
static void v64_fill (int *v, int k)
{
	int i;
	for (i = 0; i < 3; i++) {
		v[i * 2 + 0] = ffe_i64[(k + i) % FFE_NI64][0];
		v[i * 2 + 1] = ffe_i64[(k + i) % FFE_NI64][1];
	}
}

static void math_cases (void)
{
	int i, j, k;

	for (i = 0; i < FFE_NI64; i++)
		for (j = 0; j < FFE_NVEC; j++) {
			int r[2];
			r[0] = r[1] = 0;
			FUNC_001334 (r, ffe_i64[i][0], ffe_i64[i][1], ffe_vec[j]);
			fprintf (g_log, "FUNC_001334\t%d\t%d\t%d\t%d\t%d\n",
			         ffe_i64[i][0], ffe_i64[i][1], ffe_vec[j], r[0], r[1]);
			fflush (g_log);
			g_records++;
		}

	for (i = 0; i < FFE_NVEC; i++)
		for (j = 0; j < FFE_NVEC; j++) {
			int r[2];
			r[0] = r[1] = 0;
			FUNC_001339 (r, ffe_vec[i], ffe_vec[j]);
			fprintf (g_log, "FUNC_001339\t%d\t%d\t%d\t%d\n",
			         ffe_vec[i], ffe_vec[j], r[0], r[1]);
			fflush (g_log);
			g_records++;
		}

	/* `a` is both input and output, so a copy is kept and printed: printing
	 * the post-call buffer as the input would hide exactly the mistake an
	 * in-place routine is most likely to make. */
	for (i = 0; i < FFE_NI64; i++)
		for (j = 0; j < FFE_NI64; j++) {
			int a[6], b[6], a0[6];
			v64_fill (a, i);
			v64_fill (b, j);
			memcpy (a0, a, sizeof a0);
			FUNC_001661 (a, b);
			fprintf (g_log, "FUNC_001661");
			for (k = 0; k < 6; k++) fprintf (g_log, "\t%d", a0[k]);
			for (k = 0; k < 6; k++) fprintf (g_log, "\t%d", b[k]);
			for (k = 0; k < 6; k++) fprintf (g_log, "\t%d", a[k]);
			fprintf (g_log, "\n");
			fflush (g_log);
			g_records++;
		}

	for (i = 0; i < FFE_NI64; i++)
		for (j = 0; j < FFE_NI64; j++) {
			int a[6], b[6], a0[6];
			v64_fill (a, i);
			v64_fill (b, j);
			memcpy (a0, a, sizeof a0);
			FUNC_001662 (a, b);
			fprintf (g_log, "FUNC_001662");
			for (k = 0; k < 6; k++) fprintf (g_log, "\t%d", a0[k]);
			for (k = 0; k < 6; k++) fprintf (g_log, "\t%d", b[k]);
			for (k = 0; k < 6; k++) fprintf (g_log, "\t%d", a[k]);
			fprintf (g_log, "\n");
			fflush (g_log);
			g_records++;
		}

	for (i = 0; i < FFE_NVMVEC; i++)
		for (j = 0; j < FFE_NVMMAT; j++) {
			int out[3];
			out[0] = out[1] = out[2] = 0;
			FUNC_001670 (out, ffe_vmvec[i], ffe_vmmat[j]);
			fprintf (g_log, "FUNC_001670\t%d\t%d\t%d\t%d\t%d\n",
			         i, j, out[0], out[1], out[2]);
			fflush (g_log);
			g_records++;
		}

	for (i = 0; i < FFE_NVMVEC; i++)
		for (j = 0; j < FFE_NVMMAT; j++) {
			int out[3], tmp[3];
			out[0] = out[1] = out[2] = 0;
			tmp[0] = tmp[1] = tmp[2] = 0;
			FUNC_001671 (out, tmp, ffe_vmvec[i], ffe_vmmat[j]);
			fprintf (g_log, "FUNC_001671\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\n",
			         i, j, out[0], out[1], out[2], tmp[0], tmp[1], tmp[2]);
			fflush (g_log);
			g_records++;
		}
}

/* Verbatim mirror of ffe_leaf[] / leaf_emit() / leaf_cases() in
 * toolsoracleoracle.c -- same entries in the same order, same loop nests. */
static const struct { const char *name; FfeLeaf3 fn; int arity; } ffe_leaf[] = {
	{ "FUNC_000023", FUNC_000023, 1 },
	{ "FUNC_000039", FUNC_000039, 1 },
	{ "FUNC_000455", FUNC_000455, 0 },
	{ "FUNC_000456", FUNC_000456, 0 },
	{ "FUNC_000660", FUNC_000660, 0 },
	{ "FUNC_000693", FUNC_000693, 0 },
	{ "FUNC_000710", FUNC_000710, 0 },
	{ "FUNC_000715", FUNC_000715, 0 },
	{ "FUNC_000838", FUNC_000838, 0 },
	{ "FUNC_001017", FUNC_001017, 0 },
	{ "FUNC_001030", FUNC_001030, 0 },
	{ "FUNC_001349", FUNC_001349, 1 },
	{ "FUNC_001501", FUNC_001501, 2 },
	{ "FUNC_001666", FUNC_001666, 2 },
	{ "FUNC_001715", FUNC_001715, 1 },
	{ "FUNC_001716", FUNC_001716, 1 },
	{ "FUNC_001722", FUNC_001722, 3 },
	{ "FUNC_001727", FUNC_001727, 3 },
	{ "FUNC_001728", FUNC_001728, 3 },
	{ "FUNC_001729", FUNC_001729, 3 },
	{ "FUNC_001798", FUNC_001798, 1 },
	{ "FUNC_001824", FUNC_001824, 1 },
	{ "FUNC_001831", FUNC_001831, 1 },
	{ "FUNC_001832", FUNC_001832, 1 },
	{ "FUNC_001836", FUNC_001836, 1 },
};

#define FFE_NLEAF ((int)(sizeof ffe_leaf / sizeof ffe_leaf[0]))

static void leaf_emit (const char *name, FfeLeaf3 fn, int a, int b, int c,
                       int keep)
{
	int r = fn (a, b, c);
	if (!keep) r = 0;
	fprintf (g_log, "%s\t%d\t%d\t%d\t%d\n", name, a, b, c, r);
	fflush (g_log);
	g_records++;
}

static void leaf_cases (void)
{
	static const int dense[] = { 0, 1, -1, 0x7fffffff };
	const int nd = (int)(sizeof dense / sizeof dense[0]);
	int k, i, j, m;

	for (k = 0; k < FFE_NLEAF; k++) {
		const char *nm = ffe_leaf[k].name;
		FfeLeaf3 f = ffe_leaf[k].fn;
		switch (ffe_leaf[k].arity) {
		case 0:
			leaf_emit (nm, f, 0, 0, 0, 0);
			break;
		case 1:
			for (i = 0; i < FFE_NVEC; i++)
				leaf_emit (nm, f, ffe_vec[i], 0, 0, 1);
			break;
		case 2:
			for (i = 0; i < FFE_NVEC; i++)
				for (j = 0; j < FFE_NVEC; j++)
					leaf_emit (nm, f, ffe_vec[i], ffe_vec[j], 0, 1);
			break;
		default:
			for (i = 0; i < FFE_NVEC; i++)
				leaf_emit (nm, f, ffe_vec[i],
				           ffe_vec[(i + 1) % FFE_NVEC],
				           ffe_vec[(i + 2) % FFE_NVEC], 1);
			for (i = 0; i < nd; i++)
				for (j = 0; j < nd; j++)
					for (m = 0; m < nd; m++)
						leaf_emit (nm, f, dense[i], dense[j],
						           dense[m], 1);
			break;
		}
	}
}

#if FFE_PACKED_SHORT_ARGS
/* Verbatim mirror of the packed-short tables and loops in
 * toolsoracleoracle.c -- same values in the same order. */
static const int ffe_pk_a[] = { 0, 1, -1, 2, -2, 255, -256, 1000, -1000,
                                16384, -16384, 32767, -32768 };
static const int ffe_pk_b[]    = { 0, 1, 2, 3, 7, 8, 15, 16, 17, 30, 31 };
static const int ffe_pk_bhi[]  = { 32, 33, 40, 100 };
static const int ffe_pk_blo[]  = { -1, -8, -16 };

#define FFE_NPKA   ((int)(sizeof ffe_pk_a   / sizeof ffe_pk_a[0]))
#define FFE_NPKB   ((int)(sizeof ffe_pk_b   / sizeof ffe_pk_b[0]))
#define FFE_NPKBHI ((int)(sizeof ffe_pk_bhi / sizeof ffe_pk_bhi[0]))
#define FFE_NPKBLO ((int)(sizeof ffe_pk_blo / sizeof ffe_pk_blo[0]))

static void pk_emit (const char *name, int (*fn)(FfePacked), int a, int b)
{
	FfePacked p;
	int r;
	p.a = (short)a;
	p.b = (short)b;
	r = fn (p);
	fprintf (g_log, "%s\t%d\t%d\t%d\n", name, a, b, r);
	fflush (g_log);
	g_records++;
}

static void pk_one (const char *name, int (*fn)(FfePacked), int hi, int lo)
{
	int i, j;
	for (i = 0; i < FFE_NPKA; i++) {
		for (j = 0; j < FFE_NPKB; j++)
			pk_emit (name, fn, ffe_pk_a[i], ffe_pk_b[j]);
		if (hi)
			for (j = 0; j < FFE_NPKBHI; j++)
				pk_emit (name, fn, ffe_pk_a[i], ffe_pk_bhi[j]);
		if (lo)
			for (j = 0; j < FFE_NPKBLO; j++)
				pk_emit (name, fn, ffe_pk_a[i], ffe_pk_blo[j]);
	}
}

static void pk_cases (void)
{
	pk_one ("FUNC_001491", FUNC_001491, 1, 0);
	pk_one ("FUNC_001492", FUNC_001492, 0, 0);
	pk_one ("FUNC_001493", FUNC_001493, 1, 1);
	pk_one ("FUNC_001494", FUNC_001494, 1, 1);
}
#endif /* FFE_PACKED_SHORT_ARGS */

/* Verbatim mirror of hr_reset() / hr_emit() / ffe_hr[] / hr_cases() in
 * toolsoracleoracle.c -- same reset, same case list, same order. The whole
 * live prefix of the table is recorded after every call, not just the entry
 * that was added: an off-by-one in the 20-byte walk, or a sentinel written into
 * the wrong slot, moves something in there even when the new entry looks right.
 *
 * Watch ffe_hr[10] = {-1,-1,-1,-1,10}: its x0 is the SENTINEL value, so after
 * that append the walk stops early and the next call overwrites the entry. Both
 * machines have to agree about that, which is exactly why the case is here. */
static void hr_reset (void)
{
	int i;
	for (i = 0; i < HR_WORDS; i++) DATA_007683[i] = 0;
	DATA_007683[0] = -1;
}

static void hr_emit (int x, int y, int w, int h, int id)
{
	int n = 0, i, j;

	FUNC_001461 (x, y, w, h, id);
	while (n < HR_ENTRIES && DATA_007683[n * 5] != -1) n++;

	fprintf (g_log, "FUNC_001461\t%d\t%d\t%d\t%d\t%d\t%d", x, y, w, h, id, n);
	for (i = 0; i < n; i++)
		for (j = 0; j < 5; j++)
			fprintf (g_log, "\t%d", DATA_007683[i * 5 + j]);
	fprintf (g_log, "\n");
	fflush (g_log);
	g_records++;
}

/* The live prefix of the table, as its own record. Shared by the three
 * hot-rect functions so their records have one shape. */
static void hr_report (const char *name, int a, int b)
{
	int n = 0, i, j;
	while (n < HR_ENTRIES && DATA_007683[n * 5] != -1) n++;
	fprintf (g_log, "%s\t%d\t%d\t%d", name, a, b, n);
	for (i = 0; i < n; i++)
		for (j = 0; j < 5; j++)
			fprintf (g_log, "\t%d", DATA_007683[i * 5 + j]);
	fprintf (g_log, "\n");
	fflush (g_log);
	g_records++;
}

static const int ffe_hr[][5] = {
	{   0,   0,   0,   0,   0 },
	{  10,  20,  30,  40,   1 },
	{   0,   0, 320, 200,   2 },
	{ 319, 199,   1,   1,   3 },
	{  -5, -10,  20,  30,   4 },
	{  10,  20,  -5, -10,   5 },
	{   1,   2,   3,   4,  -1 },
	{ 100, 100,   0,   0,   7 },
	{ 0x7fffffff, 0, 1, 0, 8 },
	{ 0, 0x7fffffff, 0, 1, 9 },
	{ -1, -1, -1, -1, 10 },
	{  50,  60,  70,  80,  11 },
};

#define FFE_NHR ((int)(sizeof ffe_hr / sizeof ffe_hr[0]))

static void hr_cases (void)
{
	int i;

	for (i = 0; i < FFE_NHR; i++) {
		hr_reset ();
		hr_emit (ffe_hr[i][0], ffe_hr[i][1], ffe_hr[i][2], ffe_hr[i][3],
		         ffe_hr[i][4]);
	}

	hr_reset ();
	for (i = 0; i < FFE_NHR; i++)
		hr_emit (ffe_hr[i][0], ffe_hr[i][1], ffe_hr[i][2], ffe_hr[i][3],
		         ffe_hr[i][4]);

	for (i = 0; i < FFE_NHR; i++) {
		int src[4 * 5 + 5], k, m, e = (i % 3) + 1;
		hr_reset ();
		for (k = 0; k < e; k++)
			for (m = 0; m < 5; m++)
				src[k * 5 + m] = ffe_hr[(i + k) % FFE_NHR][m];
		src[e * 5] = -1;
		FUNC_001440 (src);
		hr_report ("FUNC_001440", i, e);
	}

	hr_reset ();
	for (i = 0; i < FFE_NHR; i++)
		FUNC_001461 (ffe_hr[i][0], ffe_hr[i][1], ffe_hr[i][2], ffe_hr[i][3],
		             ffe_hr[i][4]);
	for (i = 0; i < FFE_NHR; i++) {
		FUNC_001443 (ffe_hr[i][4]);
		hr_report ("FUNC_001443", i, ffe_hr[i][4]);
	}

	hr_reset ();
}

#if FFE_SUBWORD_STACK_FIXED
/* Verbatim mirror of cr_emit() / cr_cases() in toolsoracleoracle.c. g_trbuf
 * is REUSED rather than a fourth 66 KB canvas allocated: every emit memsets it
 * first, so the three drawing tests cannot see each other. */
static void cr_emit (int cx, int cy, int size, int col)
{
	unsigned char *pix = g_trbuf + LN_GUARD * LN_W;
	FfePoint c;
	int r, i, nrows = 0;

	memset (g_trbuf, LN_FILL, LN_N);
	DATA_007773.pixels = pix;
	c.x = (short)cx;
	c.y = (short)cy;
	FUNC_001612 (&c, size, col);

	for (r = 0; r < LN_TOTAL; r++) {
		const unsigned char *row = g_trbuf + r * LN_W;
		for (i = 0; i < LN_W; i++)
			if (row[i] != LN_FILL) { nrows++; break; }
	}

	fprintf (g_log, "FUNC_001612\t%d\t%d\t%d\t%d\t%d", cx, cy, size, col, nrows);
	for (r = 0; r < LN_TOTAL; r++) {
		const unsigned char *row = g_trbuf + r * LN_W;
		int first = -1, last = -1, n = 0, sum = 0;
		for (i = 0; i < LN_W; i++)
			if (row[i] != LN_FILL) {
				if (first < 0) first = i;
				last = i; n++; sum += row[i];
			}
		if (first < 0) continue;
		fprintf (g_log, "\t%d\t%d\t%d\t%d\t%d",
		         r - LN_GUARD, first, last - first + 1, n, sum);
	}
	fprintf (g_log, "\n");
	fflush (g_log);
	g_records++;
}

static void cr_cases (void)
{
	static const int cr[][3] = {
		{ 160,  79,   0 }, { 160,  79,   1 }, { 160,  79,   2 },
		{ 160,  79,   3 }, { 160,  79,   4 }, { 160,  79,   5 },
		{ 160,  79,   6 }, { 160,  79,   7 }, { 160,  79,   8 },
		{ 160,  79,   9 }, { 160,  79,  10 }, { 160,  79,  16 },
		{ 160,  79,  20 }, { 160,  79,  40 }, { 160,  79, 100 },
		{ 160,  79,  -1 }, { 160,  79,  -4 },
		{ 160,   5,  40 }, { 160,   0,  40 }, { 160, -10,  40 },
		{ 160, 150,  40 }, { 160, 158,  40 }, { 160, 170,  40 },
		{  10,  20,   8 }, { 310,  20,   8 }, {   5,  30,   4 },
		{   2,  60,  20 },
	};
	const int n = (int)(sizeof cr / sizeof cr[0]);
	int i, c = 1;

	for (i = 0; i < n; i++)
		cr_emit (cr[i][0], cr[i][1], cr[i][2], 1 + (c++ & 0xfe));

	cr_emit (160, 79, 20, 0);
	cr_emit (160, 79, 20, 255);
}
#endif /* FFE_SUBWORD_STACK_FIXED */

/* Verbatim mirror of pad_cases() in toolsoracleoracle.c. */
static void pad_cases (void)
{
	static const int ids[] = { 0, 1, -1, 2, 7, 0x7fffffff, (int)0x80000000 };
	static const int vals[][2] = { { 0, 0 }, { 11, 22 }, { -1, -2 },
	                               { 0x7fffffff, (int)0x80000000 } };
	static const int ws[] = { 0, 1, 0x75, 0x76, 0x77, 0x84, 0x85, 0x86,
	                          0xff, 0x100, 0x7fff, 0x8000, 0xffff };
	const int nid = (int)(sizeof ids / sizeof ids[0]);
	const int nval = (int)(sizeof vals / sizeof vals[0]);
	const int nw = (int)(sizeof ws / sizeof ws[0]);
	int i, j;

	for (j = 0; j < nval; j++) {
		DATA_008861 = ids[j % nid];
		DATA_008890 = vals[j][0];
		DATA_009105 = vals[j][1];
		for (i = 0; i < nid; i++) {
			fprintf (g_log, "FUNC_000402\t%d\t%d\t%d\t%d\n", ids[i],
			         DATA_008861, DATA_008890, FUNC_000402 (ids[i]));
			fprintf (g_log, "FUNC_000427\t%d\t%d\t%d\t%d\n", ids[i],
			         DATA_008861, DATA_009105, FUNC_000427 (ids[i]));
			fflush (g_log);
			g_records += 2;
		}
	}
	DATA_008861 = DATA_008890 = DATA_009105 = 0;

	for (i = 0; i < nw; i++) {
		FfeThing t;
		memset (&t, 0, sizeof t);
		t.v = (unsigned short)ws[i];
		fprintf (g_log, "FUNC_000030\t%d\t%d\n", ws[i], FUNC_000030 (&t));
		fflush (g_log);
		g_records++;
	}
}

/* Verbatim mirror of wid_cases() in tools\\oracle\\oracle.c. */
static void wid_cases (void)
{
	static const unsigned char pats[][10] = {
		{ 0,0,0,0,0,0,0,0,0,0 },
		{ 7,7,7,7,7,7,7,7,7,7 },
		{ 7,0,0,0,0,0,0,0,0,0 },
		{ 0,0,0,0,0,0,0,0,0,7 },
		{ 7,0,7,0,7,0,7,0,7,0 },
		{ 255,255,255,255,255,255,255,255,255,255 },
		{ 1,2,3,4,5,6,7,8,9,10 },
		{ 128,129,130,131,132,133,134,135,136,137 },
	};
	static const int keys[] = { 0, 7, 1, 10, 128, 255, 137 };
	const int np = (int)(sizeof pats / sizeof pats[0]);
	const int nk = (int)(sizeof keys / sizeof keys[0]);
	int i, j;

	for (i = 0; i < FFE_NI64; i++)
		for (j = 0; j < FFE_NSHIFT; j++) {
			int out[2];
			out[0] = ffe_i64[i][0];
			out[1] = ffe_i64[i][1];
			FUNC_001340 (out, ffe_shift[j]);
			fprintf (g_log, "FUNC_001340\t%d\t%d\t%d\t%d\t%d\n",
			         ffe_i64[i][0], ffe_i64[i][1], ffe_shift[j], out[0], out[1]);
			fflush (g_log);
			g_records++;
		}

	{
		unsigned char a[0xd6 + 16], b[0x13 + 4];
		for (i = 0; i < np; i++)
			for (j = 0; j < nk; j++) {
				memset (a, 0xAA, sizeof a);
				memset (b, 0xAA, sizeof b);
				memcpy (a + 0xd6, pats[i], 10);
				b[0x12] = (unsigned char)keys[j];
				fprintf (g_log, "FUNC_000422\t%d\t%d\t%d\n", i, keys[j],
				         FUNC_000422 (a, b));
				fflush (g_log);
				g_records++;
			}
	}
}

/* Verbatim mirror of ix_cases() in tools\\oracle\\oracle.c. */
#define IX_CTX (0x9bd4 + 64 * 4 + 64)

static void ix_cases (void)
{
	static const int idxs[] = { 0, 1, 2, 3, 7, 8, 15, 16, 31, 63 };
	static const int vals[] = { 0, 1, -1, 0x12345678, (int)0xdeadbeef,
	                            0x7fffffff, (int)0x80000000 };
	const int ni = (int)(sizeof idxs / sizeof idxs[0]);
	const int nv = (int)(sizeof vals / sizeof vals[0]);
	static unsigned char ctx[IX_CTX];
	int i, j;

	for (i = 0; i < ni; i++)
		for (j = 0; j < nv; j++) {
			int *slot;
			memset (ctx, 0, sizeof ctx);
			slot = (int *)(ctx + 0x9bd4 + idxs[i] * 4);
			*slot = 0x0f0f0f0f;
			FUNC_001565 (idxs[i], vals[j], ctx);
			fprintf (g_log, "FUNC_001565\t%d\t%d\t%d\n", idxs[i], vals[j], *slot);
			fflush (g_log);
			g_records++;
		}

	{
		int obj[32];
		for (j = 0; j < nv; j++) {
			int k;
			memset (obj, 0, sizeof obj);
			for (k = 0; k < 12; k++) {
				int m;
				void *r = FUNC_001558 (obj, vals[(j + k) % nv]);
				fprintf (g_log, "FUNC_001558\t%d\t%d\t%d\t%d",
				         j, k, (int)((unsigned char *)r - (unsigned char *)obj),
				         obj[0]);
				for (m = 1; m <= 11; m++)
					fprintf (g_log, "\t%d", obj[m]);
				fprintf (g_log, "\n");
				fflush (g_log);
				g_records++;
			}
		}
	}

	for (i = 0; i < 100; i++) DATA_008568[i] = 0;
	for (i = 0; i < ni; i++)
		for (j = 0; j < nv; j++) {
			FUNC_000093 (idxs[i], vals[j]);
			fprintf (g_log, "FUNC_000093\t%d\t%d\t%d\n", idxs[i], vals[j],
			         DATA_008568[idxs[i]]);
			fprintf (g_log, "FUNC_000094\t%d\t%d\n", idxs[i], FUNC_000094 (idxs[i]));
			fflush (g_log);
			g_records += 2;
		}
	for (i = 0; i < 100; i++) DATA_008568[i] = 0;
}

/* Verbatim mirror of mi_cases() in tools\oracle\oracle.c. */
static void mi_cases (void)
{
	static const int bytes[] = { 0, 1, 127, 128, 129, 200, 255, 0x7f, 0x80 };
	static const int wants[] = { 0, 1, -1, 127, -128, 128, 200, -56, 255 };
	const int nb = (int)(sizeof bytes / sizeof bytes[0]);
	const int nw = (int)(sizeof wants / sizeof wants[0]);
	unsigned char buf[0x100];
	int i, j;

	for (i = 0; i < nb; i++)
		for (j = 0; j < nw; j++) {
			memset (buf, 0xAA, sizeof buf);
			buf[0xca] = (unsigned char)bytes[i];
			fprintf (g_log, "FUNC_000657\t%d\t%d\t%d\n", bytes[i], wants[j],
			         FUNC_000657 (wants[j], buf));
			fflush (g_log);
			g_records++;
		}

	for (i = 0; i < FFE_NVEC; i++) {
		fprintf (g_log, "FUNC_000024\t%d\t%d\n", ffe_vec[i],
		         FUNC_000024 (ffe_vec[i]));
		fflush (g_log);
		g_records++;
	}

	for (i = 0; i < FFE_NVEC; i++)
		for (j = 0; j < FFE_NVEC; j++) {
			fprintf (g_log, "FUNC_001515\t%d\t%d\t%d\n", ffe_vec[i],
			         ffe_vec[j], FUNC_001515 (ffe_vec[i], ffe_vec[j]));
			fflush (g_log);
			g_records++;
		}
}

/* Verbatim mirror of sc_cases() in tools\oracle\oracle.c. The object
 * fields are read with memcpy, not a cast: 0x3e is even but not a multiple of
 * four. Each machine still lays its own longword out its own way, which is
 * right -- the buffer never crosses between them. */
static void sc_cases (void)
{
	static const int bits[] = { 0x00, 0x10, 0x0f, 0xff, 0xef, 0x90, 0x01 };
	static const int deltas[] = { 0, 1, -1, 0x7fffffff, (int)0x80000000,
	                              0x40000000, -0x40000000 };
	static const int seeds[] = { 0, 1, -1, 0x7fffffff, (int)0xffffffff,
	                             (int)0x80000000 };
	const int nb = (int)(sizeof bits / sizeof bits[0]);
	const int nd = (int)(sizeof deltas / sizeof deltas[0]);
	const int ns = (int)(sizeof seeds / sizeof seeds[0]);
	static const int LO[3] = { 0x3e, 0x46, 0x4e };
	unsigned char obj[0x60];
	int i, j, k;

	for (i = 0; i < nb; i++) {
		unsigned char big[0x160];
		memset (big, 0, sizeof big);
		big[0x14c] = (unsigned char)bits[i];
		fprintf (g_log, "FUNC_000028\t%d\t%d\n", bits[i], FUNC_000028 (big));
		fflush (g_log);
		g_records++;
	}

	for (i = 0; i < ns; i++)
		for (j = 0; j < nd; j++) {
			int d3[3], v;
			memset (obj, 0, sizeof obj);
			for (k = 0; k < 3; k++) {
				v = seeds[(i + k) % ns];
				memcpy (obj + LO[k], &v, 4);
				v = 0;
				memcpy (obj + LO[k] + 4, &v, 4);
			}
			for (k = 0; k < 3; k++) d3[k] = deltas[(j + k) % nd];
			FUNC_001514 (obj, d3);
			fprintf (g_log, "FUNC_001514\t%d\t%d", i, j);
			for (k = 0; k < 3; k++) {
				int lo, hi;
				memcpy (&lo, obj + LO[k], 4);
				memcpy (&hi, obj + LO[k] + 4, 4);
				fprintf (g_log, "\t%d\t%d", lo, hi);
			}
			fprintf (g_log, "\n");
			fflush (g_log);
			g_records++;
		}
}

/* ---- FUNC_001607_DrawQuad: the indirect-call family ----------------------
 *
 * Verbatim mirror of qd_emit() / qd_cases() in toolsoracleoracle.c -- same
 * guarded g_trbuf, same case list in the same order, same record shape as the
 * triangle's. Colour stays <= 255 for the DrawHLine dword-fill reason. */
static void qd_emit (int x0, int y0, int x1, int y1, int x2, int y2,
                     int x3, int y3, int col)
{
	unsigned char *pix = g_trbuf + LN_GUARD * LN_W;
	FfePoint a, b, c, d;
	int r, i, nrows = 0;

	memset (g_trbuf, LN_FILL, LN_N);
	DATA_007773.pixels = pix;
	a.x = (short)x0; a.y = (short)y0;
	b.x = (short)x1; b.y = (short)y1;
	c.x = (short)x2; c.y = (short)y2;
	d.x = (short)x3; d.y = (short)y3;
	FUNC_001607 (&a, &b, &c, &d, col);

	for (r = 0; r < LN_TOTAL; r++) {
		const unsigned char *row = g_trbuf + r * LN_W;
		for (i = 0; i < LN_W; i++)
			if (row[i] != LN_FILL) { nrows++; break; }
	}

	fprintf (g_log, "FUNC_001607\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d",
	         x0, y0, x1, y1, x2, y2, x3, y3, col, nrows);
	for (r = 0; r < LN_TOTAL; r++) {
		const unsigned char *row = g_trbuf + r * LN_W;
		int first = -1, last = -1, n = 0, sum = 0;
		for (i = 0; i < LN_W; i++)
			if (row[i] != LN_FILL) {
				if (first < 0) first = i;
				last = i; n++; sum += row[i];
			}
		if (first < 0) continue;
		fprintf (g_log, "\t%d\t%d\t%d\t%d\t%d",
		         r - LN_GUARD, first, last - first + 1, n, sum);
	}
	fprintf (g_log, "\n");
	fflush (g_log);
	g_records++;
}

static void qd_cases (void)
{
	static const int qd[][8] = {
		{  40,  20, 120,  30, 130, 100,  30,  90 },
		{ 120,  30, 130, 100,  30,  90,  40,  20 },
		{ 130, 100,  30,  90,  40,  20, 120,  30 },
		{  30,  90,  40,  20, 120,  30, 130, 100 },
		{  30,  90, 130, 100, 120,  30,  40,  20 },
		{  40,  20,  30,  90, 130, 100, 120,  30 },
		{  20,  20, 100,  20, 100,  80,  20,  80 },
		{ 100,  20,  20,  20,  20,  80, 100,  80 },
		{  50,  10, 100,  60, 100,  60,  20,  90 },
		{  20,  30,  60,  30, 100,  30, 140,  30 },
		{  20,  20,  20,  60,  20, 100,  20, 140 },
		{  50,  50,  50,  50,  50,  50,  50,  50 },
		{  10,  10,  11, 100,  12, 100,  13,  10 },
		{  10,  10, 300,  11, 300,  12,  10,  13 },
		{  20, 150, 100, 150, 100, 170,  20, 170 },
		{  20, -50, 100, -40, 120,  60,  10,  80 },
		{  20, -80, 100, -70,  90, -20,  10, -10 },
		{  20, 200, 100, 210,  90, 280,  10, 260 },
		{ -60,  10,  60,  20,  50,  70, -80,  60 },
		{ 260,  10, 380,  20, 370,  70, 250,  60 },
		{-100,  10, 420,  20, 400, 100, -90,  90 },
		{   0,   0, 319,   0, 319, 157,   0, 157 },
		{  20,  20, 120,  90, 120,  20,  20,  90 },
	};
	const int n = (int)(sizeof qd / sizeof qd[0]);
	int i, c = 1;

	for (i = 0; i < n; i++)
		qd_emit (qd[i][0], qd[i][1], qd[i][2], qd[i][3], qd[i][4],
		         qd[i][5], qd[i][6], qd[i][7], 1 + (c++ & 0xfe));

	qd_emit (40, 20, 120, 30, 130, 100, 30, 90, 0);
	qd_emit (40, 20, 120, 30, 130, 100, 30, 90, 255);
	qd_emit (40, 20, 120, 30, 130, 100, 30, 90, 1);
}

/* ---- the label-address / MOVZX batch -------------------------------------
 *
 * Verbatim mirror of la_cases() in toolsoracleoracle.c: same vectors, same
 * order, same record shapes. Every table slot a vector reads is WRITTEN by
 * this harness first, so nothing depends on the static-data conversion that
 * does not exist yet, and the one pointer in play is printed as an OFFSET
 * from DATA_009255, never as a value. */
static void la_cases (void)
{
	int i, r;

	{
		static int aux[4];
		struct v38 { int key, stored, state, ptrfield; };
		static const struct v38 v[] = {
			{ 0x00010000, 0x00010000, 3, 5 },
			{ 0x00020001, 0x00020001, 4, 7 },
			{ 0x00050000, 0x00060000, 3, 5 },
			{ 0x00010000, 0x00010000, 0, 5 },
			{ 0x00010000, 0x00010000, 5, 5 },
		};
		for (i = 0; i < (int)(sizeof v / sizeof v[0]); i++) {
			unsigned char *rec = DATA_008561 + 36 * (v[i].key & 0xffff);
			int state_out;
			memset (rec, 0, 36);
			memcpy (rec + 0, &v[i].state, 4);
			memcpy (rec + 4, &v[i].stored, 4);
			aux[3] = v[i].ptrfield;
			{ void *p = aux; memcpy (rec + 0x1c, &p, 4); }
			FUNC_000038 (v[i].key);
			memcpy (&state_out, rec + 0, 4);
			fprintf (g_log, "FUNC_000038\t%d\t%d\t%d\t%d\n",
			         v[i].key, v[i].state, v[i].ptrfield, state_out);
			fflush (g_log);
			g_records++;
		}
	}

	for (r = 0; r < 3; r++) {
		int src[6], back[6];
		for (i = 0; i < 6; i++) src[i] = (r + 1) * 0x01010101 + i * 7 - 3;
		memset (DATA_008661, 0xa5, 24);
		FUNC_000178 (src);
		memcpy (back, DATA_008661, 24);
		fprintf (g_log, "FUNC_000178\t%d", r);
		for (i = 0; i < 6; i++) fprintf (g_log, "\t%d", back[i]);
		fprintf (g_log, "\n");
		fflush (g_log);
		g_records++;
	}

	{
		static const int args[] = { 12345, -1 };
		for (i = 0; i < 2; i++) {
			int off;
			DATA_009255 = 0x5a5a5a5a;
			DATA_007831 = 0x3c3c3c3c;
			DATA_007830 = 0;
			DATA_007832 = 0;
			FUNC_001785 (args[i]);
			off = (int)((char *)DATA_007832 - (char *)&DATA_009255);
			fprintf (g_log, "FUNC_001785\t%d\t%d\t%d\t%d\t%d\n", args[i],
			         DATA_009255, DATA_007831, DATA_007830, off);
			fflush (g_log);
			g_records++;
		}
	}

	{
		static const int pairs[][2] = {
			{ 0x05, 0x05 }, { 0x00, 0x00 }, { 0x7f, 0x7f },
			{ 0xff, 0xff },
			{ 0x80, 0x80 },
			{ 0x01, 0x02 },
		};
		unsigned char a[0xd8], b[0x18];
		memset (a, 0, sizeof a); memset (b, 0, sizeof b);
		for (i = 0; i < (int)(sizeof pairs / sizeof pairs[0]); i++) {
			a[0xd0] = (unsigned char)pairs[i][0];
			b[0x12] = (unsigned char)pairs[i][1];
			r = FUNC_000412 (a, b);
			fprintf (g_log, "FUNC_000412\t%d\t%d\t%d\n",
			         pairs[i][0], pairs[i][1], r);
			fflush (g_log);
			g_records++;
		}
	}

	{
		static const int words[] = { 0, 0x3f, 0x40, 0x1234, 0x8000, 0xffff };
		unsigned char p[0xe8];
		memset (p, 0, sizeof p);
		for (i = 0; i < (int)(sizeof words / sizeof words[0]); i++) {
			unsigned short w = (unsigned short)words[i];
			memcpy (p + 0xe2, &w, 2);
			r = FUNC_000417 (p);
			fprintf (g_log, "FUNC_000417\t%d\t%d\n", words[i], r);
			fflush (g_log);
			g_records++;
		}
	}

	{
		static const int keys[] = { 0x0000, 0x0040, 0x7fff0040, 0x3fc0,
		                            0x2480, 0x4001, (int)0x80004001,
		                            (int)0xffffffff };
		static const int slots[] = { 0, 1, 0xff, 0x92 };
		for (i = 0; i < 4; i++) {
			short w = (short)(0x1111 * slots[i] + 7);
			memcpy (DATA_007129 + slots[i] * 4, &w, 2);
		}
		for (i = 0; i < (int)(sizeof keys / sizeof keys[0]); i++) {
			r = FUNC_000896 (keys[i]);
			fprintf (g_log, "FUNC_000896\t%d\t%d\n", keys[i], r);
			fflush (g_log);
			g_records++;
		}
	}

	{
		static const int chars[] = { 0x20, 0x41, 0x61, 0x7a, 0x7f,
		                             0x1f, 0x00, 0x80, 0xff, -1 };
		for (i = 0; i < 961; i++)
			DATA_007751[i] = (unsigned char)((i * 7 + 3) & 0x7f);
		for (i = 0; i < (int)(sizeof chars / sizeof chars[0]); i++) {
			r = FUNC_001597 (chars[i]);
			fprintf (g_log, "FUNC_001597\t%d\t%d\n", chars[i], r);
			fflush (g_log);
			g_records++;
		}
	}
}

/* ---- the base+symbol batch -----------------------------------------------
 *
 * Verbatim mirror of bs_cases() in toolsoracleoracle.c. */
static void bs_cases (void)
{
	static const int keys[] = { 0x0000, 0x0001, 0x0042, 0x0fff,
	                            0x10042, 0x0100 };
	int i, r;

	for (i = 0; i < 8; i++) DATA_009140[i] = (unsigned char)(0x20 + i * 3);
	DATA_007770[0x000] = 5;    DATA_009139[0x000] = 0;
	DATA_007770[0x001] = 0;    DATA_009139[0x001] = 3;
	DATA_007770[0x042] = 0xff; DATA_009139[0x042] = 1;
	DATA_007770[0xfff] = 0;    DATA_009139[0xfff] = 7;
	DATA_007770[0x100] = 1;    DATA_009139[0x100] = 5;

	for (i = 0; i < (int)(sizeof keys / sizeof keys[0]); i++) {
		r = FUNC_001327 (keys[i]);
		fprintf (g_log, "FUNC_001327\t%d\t%d\n", keys[i], r);
		fflush (g_log);
		g_records++;
	}
}

/* ---- FUNC_000211: REPNE SCASB, the strlen idiom --------------------------
 *
 * Verbatim mirror of rp_cases() in toolsoracleoracle.c. */
extern void FUNC_000211 (const char *name);
unsigned char DATA_008919[22] __asm__("DATA_008919");
unsigned char DATA_008920[26] __asm__("DATA_008920");

static void rp_cases (void)
{
	static const char *const names[] = {
		"", "A", "Jameson", "ABCDEFGHIJKLMNOPQRST"
	};
	int i, k;

	for (i = 0; i < (int)(sizeof names / sizeof names[0]); i++) {
		memset (DATA_008919, 0xee, 22);
		memset (DATA_008920, 0xee, 26);
		FUNC_000211 (names[i]);
		fprintf (g_log, "FUNC_000211\t%d", i);
		for (k = 0; k < 22; k++) fprintf (g_log, "\t%d", DATA_008919[k]);
		for (k = 0; k < 26; k++) fprintf (g_log, "\t%d", DATA_008920[k]);
		fprintf (g_log, "\n");
		fflush (g_log);
		g_records++;
	}
}

/* ---- the address-register AND/OR + MOVZX batch ---------------------------
 *
 * Verbatim mirror of ar_cases() in toolsoracleoracle.c -- see the oracle's
 * comment for why 0xFF/0xFF and 0x80/0x80 are the sentinels. */
extern int  FUNC_000504 (unsigned *p, int n);
extern int  FUNC_000505 (unsigned *p, int n);
extern void FUNC_000781 (unsigned char *a, unsigned char *b, int c);

static void ar_cases (void)
{
	static const unsigned vals[] = { 0u, 1u, 0x80000000u, 0xdeadbeefu,
	                                 0xffff0001u };
	static const int cnts[] = { 0, 1, 7, 16, 31 };
	static const int pairs[][2] = {
		{ 0x05, 0x07 }, { 0xff, 0xff }, { 0x80, 0x80 }, { 0x40, 0x41 },
	};
	int i, j, r;
	unsigned x;

	for (i = 0; i < (int)(sizeof vals / sizeof vals[0]); i++)
		for (j = 0; j < (int)(sizeof cnts / sizeof cnts[0]); j++) {
			x = vals[i];
			r = FUNC_000504 (&x, cnts[j]);
			fprintf (g_log, "FUNC_000504\t%d\t%d\t%d\t%d\n",
			         (int)vals[i], cnts[j], (int)x, r);
			fflush (g_log);
			g_records++;
			x = vals[i];
			r = FUNC_000505 (&x, cnts[j]);
			fprintf (g_log, "FUNC_000505\t%d\t%d\t%d\t%d\n",
			         (int)vals[i], cnts[j], (int)x, r);
			fflush (g_log);
			g_records++;
		}

	for (i = 0; i < (int)(sizeof pairs / sizeof pairs[0]); i++) {
		unsigned char a[0x100], b[0x101];
		memset (a, 0, sizeof a); memset (b, 0, sizeof b);
		a[0xfe] = (unsigned char)pairs[i][0];
		b[0x100] = 0x77;
		FUNC_000781 (a, b, pairs[i][1]);
		fprintf (g_log, "FUNC_000781\t%d\t%d\t%d\t%d\n",
		         pairs[i][0], pairs[i][1], a[0xfe], a[0xff]);
		fflush (g_log);
		g_records++;
	}
}

/* ---- the small-rules batch: sqrt chain + mantissa normaliser -------------
 *
 * Verbatim mirror of sq_cases() in toolsoracleoracle.c. */
extern int FUNC_001467 (int x);
extern int FUNC_001465 (const int *v);
extern int FUNC_001496 (int arg);
unsigned char DATA_007977[6144] __asm__("DATA_007977");

static void sq_cases (void)
{
	static const int roots[] = {
		0, 1, 0x3ff, 0x400, 0x3fff, 0x4000, 0x3ffff, 0x40000,
		0x3fffff, 0x400000, 0x3ffffff, 0x4000000, 0xfffffff,
		0x10000000, 0x3fffffff, 0x40000000, 0x7fffffff, -1
	};
	static const int vecs[][3] = {
		{ 0, 0, 0 }, { 3, 4, 0 }, { 1000, 2000, 3000 },
		{ -5, -12, 0 }, { 100000, 0, 0 }, { 32767, 32767, 32767 },
	};
	static const int mants[] = {
		1, 0x1234, 0x4000, 0x7fff, 0x9000, 0xffff,
		(int)0xdead0001, (int)0xdead0000, 0x00050123
	};
	int i;

	for (i = 0; i < 3072; i++) {
		short w = (short)((i * 2657 + 123) & 0xffff);
		memcpy (DATA_007977 + i * 2, &w, 2);
	}

	for (i = 0; i < (int)(sizeof roots / sizeof roots[0]); i++) {
		fprintf (g_log, "FUNC_001467\t%d\t%d\n", roots[i],
		         FUNC_001467 (roots[i]));
		fflush (g_log);
		g_records++;
	}

	for (i = 0; i < (int)(sizeof vecs / sizeof vecs[0]); i++) {
		fprintf (g_log, "FUNC_001465\t%d\t%d\t%d\t%d\n", vecs[i][0],
		         vecs[i][1], vecs[i][2], FUNC_001465 (vecs[i]));
		fflush (g_log);
		g_records++;
	}

	for (i = 0; i < (int)(sizeof mants / sizeof mants[0]); i++) {
		fprintf (g_log, "FUNC_001496\t%d\t%d\n", mants[i],
		         FUNC_001496 (mants[i]));
		fflush (g_log);
		g_records++;
	}
}

/* ---- FUNC_000168_CdrFileScramble: words landing in si --------------------
 *
 * Verbatim mirror of si_cases() in toolsoracleoracle.c. */
extern int FUNC_000168 (void *data, int size);
int DATA_008669 __asm__("DATA_008669");

static void si_cases (void)
{
	static const int sizes[] = { 0, 2, 7, 16 };
	static const int seeds[] = { 0, 0x5a5a5a5a };
	int i, j, k, r;

	for (j = 0; j < 2; j++)
		for (i = 0; i < (int)(sizeof sizes / sizeof sizes[0]); i++) {
			unsigned char buf[16];
			int nw = sizes[i] / 2;
			for (k = 0; k < 8; k++) {
				short w = (short)((k * 0x1357 + 9) & 0xffff);
				memcpy (buf + k * 2, &w, 2);
			}
			DATA_008669 = seeds[j];
			r = FUNC_000168 (buf, sizes[i]);
			fprintf (g_log, "FUNC_000168\t%d\t%d\t%d", sizes[i], seeds[j], r);
			for (k = 0; k < nw; k++) {
				short w;
				memcpy (&w, buf + k * 2, 2);
				fprintf (g_log, "\t%d", w);
			}
			fprintf (g_log, "\n");
			fflush (g_log);
			g_records++;
		}
}

/* ---- FUNC_000021: THE FIRST JUMP TABLE -----------------------------------
 *
 * Verbatim mirror of jt_cases() in toolsoracleoracle.c -- see the oracle's
 * comment. The dc.l table travels INSIDE FUNC_000021.s; nothing here
 * defines it. */
extern int FUNC_000021 (int idx);
int DATA_008911 __asm__("DATA_008911");
int DATA_008888 __asm__("DATA_008888");
int DATA_008922 __asm__("DATA_008922");
int DATA_008885 __asm__("DATA_008885");
int DATA_008804 __asm__("DATA_008804");
int DATA_008807 __asm__("DATA_008807");
signed char DATA_008874 __asm__("DATA_008874");
int DATA_008862 __asm__("DATA_008862");
signed char DATA_008870 __asm__("DATA_008870");
int DATA_008892 __asm__("DATA_008892");
int DATA_008923 __asm__("DATA_008923");
int DATA_008924 __asm__("DATA_008924");
int DATA_008925 __asm__("DATA_008925");
int DATA_008926 __asm__("DATA_008926");

static void jt_cases (void)
{
	static unsigned char objA[0x100], objB[0x100];
	int idx;

	objA[0x86] = 0x5a;
	objB[0x86] = 0xa7;
	DATA_008861 = (int)objA;
	DATA_008862 = (int)objB;
	DATA_008911 = 1111111;
	DATA_008888 = -2222222;
	DATA_008922 = 33333333;
	DATA_008885 = -4;
	DATA_008804 = 123456789;
	DATA_008807 = 0x200000;
	DATA_008874 = -100;
	DATA_008870 = 100;
	DATA_008890 = 555;
	DATA_008892 = -666;
	DATA_008923 = 7;
	DATA_008924 = 8;
	DATA_008925 = 9;
	DATA_008926 = 10;

	for (idx = -1; idx <= 0x15; idx++) {
		fprintf (g_log, "FUNC_000021\t%d\t%d\n", idx, FUNC_000021 (idx));
		fflush (g_log);
		g_records++;
	}
}

/* ---- the LEA symbol+scaled-index pair ------------------------------------
 *
 * Verbatim mirror of lt_cases() in toolsoracleoracle.c. */
extern void FUNC_000517 (int idx);
extern void FUNC_000199 (const void *fi);
unsigned char DATA_009004[232]  __asm__("DATA_009004");
int           DATA_008659       __asm__("DATA_008659");
unsigned char DATA_008665[1536] __asm__("DATA_008665");

/* ---- packed-float family. Verbatim mirror of pf_cases() in
 * tools\oracle\oracle.c -- same values, same order, same masking. */
/* 1463 ends in `call near [DATA_007723]` (= FUNC_001467, the sqrt-exp
 * helper). The PC links the real ffedat, which holds that pointer; THIS
 * side defines it, aimed at the BARE t-ABI body -- a translated-to-
 * translated call, exactly as in the game. FUNC_001467 reads the word
 * table DATA_007977, which sq_cases() has already filled by the time
 * pf_cases() runs -- same order on both sides. */
extern char ffe_f1467 __asm__("FUNC_001467");
void *DATA_007723 __asm__("DATA_007723") = &ffe_f1467;

extern long PFF_1463(long v)          __asm__("_FUNC_001463");
extern long PFF_1482(long a, long b)  __asm__("_FUNC_001482");
extern long PFF_1483(long a, long b)  __asm__("_FUNC_001483");
/* 1486 is a C SHIM (ffe_pfloat.c) after the oracle caught its
 * pointer-aliased local composition -- call the bare C symbol
 * directly, C to C. */
extern long PFF_1486(long a, long b)  __asm__("FUNC_001486");
extern long PFF_1491(long p)          __asm__("_FUNC_001491");
extern long PFF_1492(long p)          __asm__("_FUNC_001492");
extern long PFF_1493(long p)          __asm__("_FUNC_001493");
extern long PFF_1494(long p)          __asm__("_FUNC_001494");

#define PF(m, e) ((long)((((unsigned long)(e) & 0xFFFFul) << 16) \
                         | ((unsigned long)(m) & 0xFFFFul)))

#define PFREC1(name, fn, a) do { \
	fprintf (g_log, name "\t%ld\t%ld\n", (long)(a), fn (a)); \
	fflush (g_log); g_records++; } while (0)
#define PFREC2(name, fn, a, b) do { \
	fprintf (g_log, name "\t%ld\t%ld\t%ld\n", (long)(a), (long)(b), \
	         fn (a, b)); \
	fflush (g_log); g_records++; } while (0)

static void pf_cases (void)
{
	static const long pf[] = {
		0,
		PF(0x4000, 0),  PF(0x7FFF, 0),  PF(-0x4000, 0), PF(-0x7FFF, 0),
		PF(0x4000, 10), PF(0x5A82, 3),  PF(-0x5A82, 3), PF(0x4000, -10),
		PF(0x4001, 14), PF(0x6000, 1),  PF(-0x4800, 7),
	};
	static const long iv[] = {
		0, 1, -1, 2, 100, -100, 32767, -32768, 65536, 0x100000,
		-0x100000, 715827882,
	};
	int i, j;
	const int npf = (int)(sizeof pf / sizeof pf[0]);
	const int niv = (int)(sizeof iv / sizeof iv[0]);

	for (i = 0; i < niv; i++)
		PFREC1 ("FUNC_001463", PFF_1463, iv[i]);
	for (i = 0; i < npf; i++)
		for (j = 0; j < npf; j++) {
			PFREC2 ("FUNC_001482", PFF_1482, pf[i], pf[j]);
			PFREC2 ("FUNC_001483", PFF_1483, pf[i], pf[j]);
			PFREC2 ("FUNC_001486", PFF_1486, pf[i], pf[j]);
		}
	for (i = 0; i < npf; i++) {
		long e = (pf[i] >> 16) & 0xFFFF;
		int neg = (e & 0x8000) != 0;
		if (!neg)
			PFREC1 ("FUNC_001491", PFF_1491, pf[i]);
		if (!neg)
			PFREC1 ("FUNC_001492", PFF_1492, pf[i]);
		PFREC1 ("FUNC_001493", PFF_1493, pf[i]);
		PFREC1 ("FUNC_001494", PFF_1494, pf[i]);
	}
}

/* Verbatim mirror of pfneg_cases() in tools\oracle\oracle.c, and the carrier
 * for the cl-count mask landed in lower.py on 2026-08-06. Every exponent drives
 * the shift count past 31 -- the region pf_cases() above skips, because until
 * today an unmasked 68k took the count modulo 64 where x86 masks it to five
 * bits. Emitted LAST on this side too: compare.py lines the two files up by
 * POSITION, so a mirror is only a mirror if the ORDER matches as well as the
 * values. */
static void pfneg_cases (void)
{
	static const long mant[] = { 0, 0x4000, 0x7FFF, -0x4000, -0x7FFF, 1, -1 };
	static const long expo[] = { -1, -2, -8, -16, -17, -20, -32, -33, -48,
	                             -64, -100 };
	const int nm = (int)(sizeof mant / sizeof mant[0]);
	const int ne = (int)(sizeof expo / sizeof expo[0]);
	int i, j;

	for (i = 0; i < nm; i++)
		for (j = 0; j < ne; j++) {
			long p = PF (mant[i], expo[j]);
			PFREC1 ("FUNC_001491", PFF_1491, p);
			PFREC1 ("FUNC_001492", PFF_1492, p);
			PFREC1 ("FUNC_001493", PFF_1493, p);
			PFREC1 ("FUNC_001494", PFF_1494, p);
		}
}

static void lt_cases (void)
{
	int i, k, v;

	{
		static const int dels[] = { 1, 0, 4 };
		int d;
		for (d = 0; d < (int)(sizeof dels / sizeof dels[0]); d++) {
			for (i = 0; i < 5; i++)
				for (k = 0; k < 6; k++) {
					v = (k == 0) ? 100 + i : i * 11 + k;
					memcpy (DATA_009004 + i * 24 + k * 4, &v, 4);
				}
			v = 0;
			for (k = 0; k < 6; k++)
				memcpy (DATA_009004 + 5 * 24 + k * 4,
				        (k == 0 ? &v : &v), 4);
			FUNC_000517 (dels[d]);
			fprintf (g_log, "FUNC_000517\t%d", dels[d]);
			for (i = 0; i < 36; i++) {
				memcpy (&v, DATA_009004 + i * 4, 4);
				fprintf (g_log, "\t%d", v);
			}
			fprintf (g_log, "\n");
			fflush (g_log);
			g_records++;
		}
	}

	{
		static const int counts[] = { 0, 1, 0x3f, 0x40 };
		int c, fi[6], slot;
		memset (DATA_008665, 0, 1536);
		for (c = 0; c < (int)(sizeof counts / sizeof counts[0]); c++) {
			for (k = 0; k < 6; k++) fi[k] = 0x1000 * (c + 1) + k;
			DATA_008659 = counts[c];
			FUNC_000199 (fi);
			slot = (counts[c] < 0x40) ? counts[c] : 0x3f;
			fprintf (g_log, "FUNC_000199\t%d\t%d", counts[c], DATA_008659);
			for (k = 0; k < 6; k++) {
				memcpy (&v, DATA_008665 + slot * 24 + k * 4, 4);
				fprintf (g_log, "\t%d", v);
			}
			fprintf (g_log, "\n");
			fflush (g_log);
			g_records++;
		}
	}
}

/* ---- FUNC_001613_DrawSpans + FUNC_001406: the residual-rules batch -------
 *
 * Verbatim mirror of ds_row()/ds_emit()/ds_cases() in toolsoracleoracle.c.
 * DrawSpans's callbacks resolve to the long-green FUNC_001600/FUNC_001601
 * bodies at link time -- bare t-ABI labels stored to a stack slot and
 * called from it. */
extern void FUNC_001613 (void *ctx, int col);
extern int  FUNC_001406 (int key);
int           DATA_007687[64]  __asm__("DATA_007687");
unsigned char DATA_007691[256] __asm__("DATA_007691");

static unsigned char g_dsctx[0x1b40] __attribute__((aligned(4)));

static void ds_row (int row, int n, const int *ends)
{
	int k;
	memcpy (g_dsctx + row * 44, &n, 4);
	for (k = 0; k < n; k++)
		memcpy (g_dsctx + row * 44 + 4 + k * 4, &ends[k], 4);
}

static void ds_emit (int flag, int ymin, int yhi, int col)
{
	unsigned char *pix = g_trbuf + LN_GUARD * LN_W;
	int r, i, nrows = 0;

	memset (g_trbuf, LN_FILL, LN_N);
	DATA_007773.pixels = pix;
	memcpy (g_dsctx + 0x1b28, &flag, 4);
	memcpy (g_dsctx + 0x1b2c, &ymin, 4);
	memcpy (g_dsctx + 0x1b30, &yhi, 4);
	FUNC_001613 (g_dsctx, col);

	for (r = 0; r < LN_TOTAL; r++) {
		const unsigned char *row = g_trbuf + r * LN_W;
		for (i = 0; i < LN_W; i++)
			if (row[i] != LN_FILL) { nrows++; break; }
	}

	fprintf (g_log, "FUNC_001613\t%d\t%d\t%d\t%d\t%d",
	         flag, ymin, yhi, col, nrows);
	for (r = 0; r < LN_TOTAL; r++) {
		const unsigned char *row = g_trbuf + r * LN_W;
		int first = -1, last = -1, n = 0, sum = 0;
		for (i = 0; i < LN_W; i++)
			if (row[i] != LN_FILL) {
				if (first < 0) first = i;
				last = i; n++; sum += row[i];
			}
		if (first < 0) continue;
		fprintf (g_log, "\t%d\t%d\t%d\t%d\t%d",
		         r - LN_GUARD, first, last - first + 1, n, sum);
	}
	fprintf (g_log, "\n");
	fflush (g_log);
	g_records++;
}

/* ---- FUNC_000869 + FUNC_000868: the star-system lookup -------------------
 *
 * Verbatim mirror of sy_cases() in tools\oracle\oracle.c. The carrier for the
 * handcoded-system table's storage class: 348 records of 54 bytes spanning ten
 * labels, three of which used to be byte-swapped in the converted image while
 * the other seven stayed raw. Drives every width the table has -- a dword walk
 * in FUNC_000868, two dwords, five bytes and a word in FUNC_000869 -- plus the
 * three sub-word stack accesses frame.py has to place (ffe.asm:60800, 60816,
 * 60828). */
extern void FUNC_000869 (int id, int *o2, int *o3, int *o4, int *o5, int *o6,
                         int *o7, int *o8, int *o9);
extern int  FUNC_000868 (int id);

/* FUNC_000871 -- the star itself, the first thing FUNC_000878 computes. Same
 * record, both widths in one call: the BYTE at +44 (two nibbles = the two star
 * classes, must NOT swap) and the WORD at +10 shifted right by 6 (must swap).
 * The two temperatures come from DATA_008912/8913, which the probe defines and
 * fills itself, so no live game state is involved. */
extern void FUNC_000871 (int id, int *o2, int *o3, int *o4, int *o5, int *o6,
                         int *o7);
int DATA_008912 __asm__("DATA_008912");
int DATA_008913 __asm__("DATA_008913");

#define SY_XY(x, y) (int)((((unsigned)(y) & 0x1fff) << 13) | ((unsigned)(x) & 0x1fff))

static const int sy_ids[] = {
	0x1aa51717,
	0x02a49718, 0x06a49718,
	0x12a49718, 0x16a49718,
	0x1aa53717, 0x06a53716,
	0x02a53715, 0x02a55717,
	0x03a53717,
	0,
	SY_XY (0x1718, 0x1524),
	SY_XY (0x171b, 0x1524),
	SY_XY (0x171e, 0x1524),
	SY_XY (0x1720, 0x1524),
	SY_XY (0x1723, 0x1524),
	SY_XY (0x172c, 0x1524),
	SY_XY (0x17e0, 0x1524),
	SY_XY (0x1718, 0x1530),
	SY_XY (0x0001, 0x0001),
	SY_XY (0x1fff, 0x1fff),
};

static void sy_cases (void)
{
	const int n = (int)(sizeof sy_ids / sizeof sy_ids[0]);
	int i;

	for (i = 0; i < n; i++) {
		int o2 = 0, o3 = 0, o4 = 0, o5 = 0, o6 = 0, o7 = 0, o8 = 0, o9 = 0;
		int idx = FUNC_000868 (sy_ids[i]);

		FUNC_000869 (sy_ids[i], &o2, &o3, &o4, &o5, &o6, &o7, &o8, &o9);
		fprintf (g_log,
		         "FUNC_000869\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\n",
		         sy_ids[i], idx, o2, o3, o4, o5, o6, o7, o8, o9);
		g_records++;
	}

	DATA_008912 = 0x0BADF00D;
	DATA_008913 = 0x1234ABCD;
	for (i = 0; i < n; i++) {
		int o2 = 0, o3 = 0, o4 = 0, o5 = 0, o6 = 0, o7 = 0;

		FUNC_000871 (sy_ids[i], &o2, &o3, &o4, &o5, &o6, &o7);
		fprintf (g_log, "FUNC_000871\t%d\t%d\t%d\t%d\t%d\t%d\t%d\n",
		         sy_ids[i], o2, o3, o4, o5, o6, o7);
		g_records++;
	}
}

static void ds_cases (void)
{
	static const int rowA[] = { 50, 20, 100, 80 };
	static const int rowB[] = { 30, 60 };
	static const int rowC[] = { 90, 10, 40 };
	static const int rowF[] = { 0, 320 };
	static const int rowX[] = { -10, 400 };
	static const int rowM[] = { 300, -50, 120, 40, 310, 200 };
	int r, i;

	memset (g_dsctx, 0, sizeof g_dsctx);
	ds_row (10, 4, rowA);
	ds_row (11, 2, rowB);
	ds_row (12, 3, rowC);
	ds_row (13, 0, rowA);
	ds_emit (0, 10, 14, 7);

	memset (g_dsctx, 0, sizeof g_dsctx);
	ds_row (0, 2, rowF);
	ds_emit (0, 0, 1, 255);

	memset (g_dsctx, 0, sizeof g_dsctx);
	for (r = 140; r < 158; r++) ds_row (r, 2, rowX);
	ds_emit (1, 140, 158, 3);

	memset (g_dsctx, 0, sizeof g_dsctx);
	ds_row (5, 6, rowM);
	ds_emit (1, 5, 6, 9);

	memset (g_dsctx, 0, sizeof g_dsctx);
	ds_row (8, 2, rowB);
	ds_emit (1, 8, 8, 5);

	{
		static const int maps[]   = { 0, -1, 3, 7, 200 };
		static const int states[] = { 0, 1, 0x80, 0xff, 2, 0, 0, 5 };
		static const int keys[]   = { 0, 1, 2, 3, 4 };
		for (i = 0; i < 5; i++) DATA_007687[i] = maps[i];
		for (i = 0; i < 8; i++) DATA_007691[i] = (unsigned char)states[i];
		DATA_007691[200] = 0;
		for (i = 0; i < 5; i++) {
			fprintf (g_log, "FUNC_001406\t%d\t%d\n", keys[i],
			         FUNC_001406 (keys[i]));
			fflush (g_log);
			g_records++;
		}
	}
}

/* ---- FUNC_001337_Int64Mul64: one-operand MUL -----------------------------
 *
 * Verbatim mirror of mu_cases() in toolsoracleoracle.c. */
extern void FUNC_001337 (int *out, int alo, int ahi, int blo, int bhi);

static void mu_cases (void)
{
	int i, j, out[2];

	for (i = 0; i < FFE_NI64; i++)
		for (j = 0; j < FFE_NI64; j++) {
			out[0] = out[1] = 0;
			FUNC_001337 (out, ffe_i64[i][0], ffe_i64[i][1],
			             ffe_i64[j][0], ffe_i64[j][1]);
			fprintf (g_log, "FUNC_001337\t%d\t%d\t%d\t%d\t%d\t%d\n",
			         ffe_i64[i][0], ffe_i64[i][1],
			         ffe_i64[j][0], ffe_i64[j][1], out[0], out[1]);
			fflush (g_log);
			g_records++;
		}
}

static void rec2 (const char *name, int a, int r)
{
	fprintf (g_log, "%s\t%d\t%d\n", name, a, r);
	fflush (g_log);
	g_records++;
}

static void rec3 (const char *name, int a, int b, int r)
{
	fprintf (g_log, "%s\t%d\t%d\t%d\n", name, a, b, r);
	fflush (g_log);
	g_records++;
}

/* 64-bit in, 64-bit out: four argument words then the two result words. */
static void rec64 (const char *name, const int *a, const int *b, const int *r)
{
	fprintf (g_log, "%s\t%d\t%d\t%d\t%d\t%d\t%d\n",
	         name, a[0], a[1], b[0], b[1], r[0], r[1]);
	fflush (g_log);
	g_records++;
}

int main (void)
{
	int i, j;

	g_log = fopen (ffe_pd ("ffe_xlattest.log"), "w");
	if (g_log == NULL) return 20;

	/* The result is fetched into a local first so the order of the calls is
	 * fixed by the language rather than by the compiler's argument
	 * evaluation order. It costs nothing and removes a variable from any
	 * later argument about why two runs differ. */
	for (i = 0; i < FFE_NVEC; i++)
		for (j = 0; j < FFE_NVEC; j++) {
			int r = FUNC_001521 (ffe_vec[i], ffe_vec[j]);
			rec3 ("FUNC_001521", ffe_vec[i], ffe_vec[j], r);
		}

	for (i = 0; i < FFE_NVEC; i++) {
		int r = FUNC_001522 (ffe_vec[i]);
		rec2 ("FUNC_001522", ffe_vec[i], r);
	}

	for (i = 0; i < FFE_NVEC; i++)
		for (j = 0; j < FFE_NVEC; j++) {
			int r = FUNC_001523 (ffe_vec[i], ffe_vec[j]);
			rec3 ("FUNC_001523", ffe_vec[i], ffe_vec[j], r);
		}

	/* The output buffer is cleared before each call rather than after,
	 * so a function that wrote nothing at all would show up as zeros
	 * instead of as the previous call's answer. */
	for (i = 0; i < FFE_NI64; i++)
		for (j = 0; j < FFE_NI64; j++) {
			int out[2];
			out[0] = out[1] = 0;
			FUNC_001333 (out, ffe_i64[i][0], ffe_i64[i][1],
			                  ffe_i64[j][0], ffe_i64[j][1]);
			rec64 ("FUNC_001333", ffe_i64[i], ffe_i64[j], out);
		}

	for (i = 0; i < FFE_NI64; i++)
		for (j = 0; j < FFE_NI64; j++) {
			int out[2];
			out[0] = out[1] = 0;
			FUNC_001335 (out, ffe_i64[i][0], ffe_i64[i][1],
			                  ffe_i64[j][0], ffe_i64[j][1]);
			rec64 ("FUNC_001335", ffe_i64[i], ffe_i64[j], out);
		}

	/* Shifts in place: the buffer is both input and output. */
	for (i = 0; i < FFE_NI64; i++)
		for (j = 0; j < FFE_NSHIFT; j++) {
			int out[2];
			out[0] = ffe_i64[i][0];
			out[1] = ffe_i64[i][1];
			FUNC_001341 (out, ffe_shift[j]);
			fprintf (g_log, "FUNC_001341\t%d\t%d\t%d\t%d\t%d\n",
			         ffe_i64[i][0], ffe_i64[i][1], ffe_shift[j],
			         out[0], out[1]);
			fflush (g_log);
			g_records++;
		}

	for (i = 0; i < FFE_NMSB; i++) {
		int r = FUNC_001656 (ffe_msb[i]);
		rec2 ("FUNC_001656", ffe_msb[i], r);
	}

	for (i = 0; i < FFE_NVMVEC; i++)
		for (j = 0; j < FFE_NVMMAT; j++) {
			int out[3];
			out[0] = out[1] = out[2] = 0;
			FUNC_001669 (out, ffe_vmvec[i], ffe_vmmat[j]);
			fprintf (g_log, "FUNC_001669\t%d\t%d\t%d"
			                "\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d"
			                "\t%d\t%d\t%d\n",
			         ffe_vmvec[i][0], ffe_vmvec[i][1], ffe_vmvec[i][2],
			         ffe_vmmat[j][0], ffe_vmmat[j][1], ffe_vmmat[j][2],
			         ffe_vmmat[j][3], ffe_vmmat[j][4], ffe_vmmat[j][5],
			         ffe_vmmat[j][6], ffe_vmmat[j][7], ffe_vmmat[j][8],
			         out[0], out[1], out[2]);
			fflush (g_log);
			g_records++;
		}

	/* LAST, and it has to stay last: compare.py lines the two files up by
	 * position, so appending is the only way to add vectors without moving
	 * every record that already matches. */
	hl_cases ();
	cl_cases ();
	ln_cases ();
	tr_cases ();
	math_cases ();
	leaf_cases ();
#if FFE_PACKED_SHORT_ARGS
	pk_cases ();
#endif
	hr_cases ();
#if FFE_SUBWORD_STACK_FIXED
	cr_cases ();
#endif
	pad_cases ();
	wid_cases ();
	ix_cases ();
	mi_cases ();
	sc_cases ();
	qd_cases ();
	la_cases ();
	bs_cases ();
	mu_cases ();
	rp_cases ();
	ar_cases ();
	sq_cases ();
	si_cases ();
	jt_cases ();
	lt_cases ();
	ds_cases ();
	pf_cases ();
	sy_cases ();
	/* LAST, always -- see pfneg_cases(). */
	pfneg_cases ();

	/* Trailer, so a log truncated by a Guru or by the run script's
	 * quiet-timeout cannot be mistaken for a complete pass. */
	fprintf (g_log, "# records %d\n", g_records);
	fflush (g_log);
	fclose (g_log);
	return 0;
}
