/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 angree. See LICENSE at the repository root.
 *
 * The reference oracle for the x86 -> 68k translator.
 *
 * Every one of the game's 1,621 functions is declared GLOBAL in
 * ref\jjffe\ffe.asm, so the object file the PC reference build already produces
 * (build\m0-pc\ffe.obj) can be linked against directly and any FUNC_nnnnnn
 * called with arguments of our choosing. That makes the ORIGINAL the authority
 * on what a translated function must do -- not a hand-derived model of it, and
 * not my reading of the assembly.
 *
 * The comparison format is deliberately endian-neutral: results are printed as
 * NUMBERS, never as bytes. A 68k build of the same test prints the same numbers
 * or the translation is wrong. Byte-level comparison would fail on byte order
 * alone and tell us nothing.
 *
 * Two shapes of function are covered. The fixed-point leaves return their
 * result in eax and are simply called; the 64-bit and vector helpers write
 * through a pointer argument, so the buffer is allocated here and EVERY output
 * word is printed, still as a decimal number. FUNC_001521 is the single
 * most-called function in the game (441 static call sites) and FUNC_001669
 * calls it nine times, which is what makes it the first non-leaf here.
 *
 * Build: tools\oracle\build-oracle.py
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* cdecl. The explicit asm names are load-bearing: ffe.asm declares these as
 * `GLOBAL FUNC_001521` with NO leading underscore, while 32-bit MinGW decorates
 * every C symbol with one, so a plain `extern int FUNC_001521(...)` looks for
 * `_FUNC_001521` and does not find it. (The platform symbols are the other way
 * round -- ffe.asm spells those `EXTERN _VideoBlit`, matching C's decoration,
 * which is why only the game's own functions need this.)
 *
 * The signatures are read straight off the frame offsets in ffe.asm: every
 * [ebp+d] with d >= 8 is one 4-byte cdecl argument, so a 64-bit value is two
 * plain ints, low half first, and never a `long long` -- passing one of those
 * would work on x86 but leaves the m68k side of the comparison arguing about a
 * calling convention instead of about the translation. */
extern int  FUNC_001521(int a, int b) __asm__("FUNC_001521");  /* (a*b)>>31   */
extern int  FUNC_001522(int a)        __asm__("FUNC_001522");  /* (a*a)>>32   */
extern int  FUNC_001523(int a, int b) __asm__("FUNC_001523");  /* fixp divide */

/* The asm name is the label as ffe.asm spells it, which for the 278 functions
 * JJ managed to name is FUNC_nnnnnn_Something -- and that is what `GLOBAL`
 * exports (ffe.asm:1122 and friends). The translator, by contrast, names its
 * output after the bare FUNC_nnnnnn it was asked for, so the 68k side of this
 * comparison declares `FUNC_001333` while this side declares
 * `FUNC_001333_Int64Add64`. The two meet in the TSV, where the record name is
 * written out as the bare number on both sides. */

/* void F1333 (int *rval, int64 op1, int64 op2)   -- ffe.asm:83461 */
extern void FUNC_001333(int *r, int alo, int ahi, int blo, int bhi)
                                      __asm__("FUNC_001333_Int64Add64");
/* void F1335 (int64 *rval, int64 op1, int64 op2) -- ffe.asm:83499 */
extern void FUNC_001335(int *r, int alo, int ahi, int blo, int bhi)
                                      __asm__("FUNC_001335_Int64Sub64");
/* void F1341 (int64 *val, int shift)             -- ffe.asm:83671 */
extern void FUNC_001341(int *val, int shift)
                                      __asm__("FUNC_001341_Int64ArithShift");
/* int FindMSB (int x)                            -- ffe.asm:100057 */
extern int  FUNC_001656(int x)        __asm__("FUNC_001656_FindMSB");
/* void VecMatMul (int *out3, const int *vec3, const int *mat9) -- ffe.asm:100698 */
extern void FUNC_001669(int *out, const int *vec, const int *mat)
                                      __asm__("FUNC_001669_VecMatMul");
/* void F1601 (int xpos, int ypos, int length, PALCOL col) -- ffe.asm:94260.
 * The horizontal span the whole polygon filler is built on. */
extern void FUNC_001601(int xpos, int ypos, int length, int col)
                                      __asm__("FUNC_001601_DrawHLine");
/* void F1600 (int xpos, int ypos, int length, PALCOL col) -- ffe.asm:94222.
 * DrawHLine with the span clipped to the 320-pixel width. */
extern void FUNC_001600(int xpos, int ypos, int length, int col)
                                      __asm__("FUNC_001600_DrawHLineClip");
/* void F1599 (int xpos, int ypos, int length, PALCOL col) -- ffe.asm:94197.
 * ... and with the whole span dropped when ypos is outside the 158-line
 * console viewport. */
extern void FUNC_001599(int xpos, int ypos, int length, int col)
                                      __asm__("FUNC_001599_DrawHLineConClip");
/* The game's own point, as DrawLine reads it: two 16-bit signed fields, four
 * bytes, no padding -- `word [edi]` and `word [edi+2]` at ffe.asm:96773 and
 * friends. Compiled here for x86 and in amiga\probes\ffe_xlattest.c for m68k, so
 * each machine builds one in its own byte order and each machine's code reads it
 * back in that same order. Nothing about this struct crosses between them. */
typedef struct { short x, y; } FfePoint;

/* void F1616 (const FfePoint *p1, const FfePoint *p2, PALCOL col)
 * -- ffe.asm:96763. The line drawer: it interpolates a 16.16 x down the
 * scanlines and calls FUNC_001601 once per row -- DIRECTLY, not through the clip
 * wrapper, so it does not clip at all. */
extern void FUNC_001616(const FfePoint *p1, const FfePoint *p2, int col)
                                      __asm__("FUNC_001616_DrawLine");
/* void F1604 (const FfePoint *a, const FfePoint *b, const FfePoint *c,
 *             PALCOL col)               -- ffe.asm:94539.
 * The filled triangle. It bypasses DrawHLine entirely and fills each row with
 * _memset, and unlike DrawLine it clips both axes -- see tr_emit() below. */
extern void FUNC_001604(const FfePoint *a, const FfePoint *b, const FfePoint *c,
                        int col)      __asm__("FUNC_001604_DrawTriangle");
/* void F1607 (P2D p1, P2D p2, P2D p3, P2D p4, COLPAL col)  -- ffe.asm:95478.
 * The filled quad, and THE CARRIER FOR INDIRECT CALL (PLAN 5): DrawQuad pushes
 * FUNC_001601_DrawHLine as a sixth argument to FUNC_001608_DrawQuadInternal,
 * whose edge walker FUNC_001602_DrawPolySegment then does `call near
 * [ebp+0x24]` -- a function address travelling through a stack slot and being
 * called from it. The family also carries the flag-exact An add/sub detour
 * (`sub esi,ecx` with N live, in FUNC_001603_CalcLineGrad) and the one-operand
 * IMUL with an An source (`imul esi`, in 1602). Spans land through 1601 and
 * FUNC_001600_DrawHLineClip, both long green. */
extern void FUNC_001607(const FfePoint *a, const FfePoint *b, const FfePoint *c,
                        const FfePoint *d, int col)
                                      __asm__("FUNC_001607_DrawQuad");

/* THE 64-BIT AND VECTOR MATHS FAMILY -- the batch chosen by measurement rather
 * than by reading the plan. A dry run of the translator over all 1,615 function
 * labels in ffe.asm says 407 of them already translate clean, so the bottleneck
 * is VERIFICATION, not translation; these six are the cheapest clean ones to
 * verify, because the ffe_i64 / ffe_vec / ffe_vmvec / ffe_vmmat tables below
 * already drive their siblings.
 *
 * Every signature is read off the [ebp+d] frame offsets, and every buffer size
 * off the displacements each pointer is dereferenced at -- e.g. 1670 touches
 * [esi+0..32], so its third argument is nine ints. Guessing a size here would
 * turn a stack overrun into a "translator bug". */

/* void Int64Add32 (int64 *r, int64 a, int32 b)   -- ffe.asm:83479.
 * r = a + (int64)b, the 32-bit addend sign-extended by CDQ. */
extern void FUNC_001334(int *r, int alo, int ahi, int b)
                                      __asm__("FUNC_001334_Int64Add32");
/* void Int64MulCreate (int64 *r, int32 a, int32 b) -- ffe.asm:83584.
 * r = (int64)a * (int64)b: the one-operand IMUL, full 64-bit product. */
extern void FUNC_001339(int *r, int a, int b)
                                      __asm__("FUNC_001339_Int64MulCreate");
/* void Vec64Add (int64 a[3], const int64 b[3])   -- ffe.asm:100373.
 * a += b, in place, as three calls to the already-green FUNC_001333. */
extern void FUNC_001661(int *a, const int *b)
                                      __asm__("FUNC_001661_Vec64Add");
/* void Vec64Sub (int64 a[3], const int64 b[3])   -- ffe.asm:100411. */
extern void FUNC_001662(int *a, const int *b)
                                      __asm__("FUNC_001662_Vec64Sub");
/* void VecMatTMul (int *out3, const int *vec3, const int *mat9)
 * -- ffe.asm:100801. VecMatMul's transposed twin; nine calls to FUNC_001521. */
extern void FUNC_001670(int *out, const int *vec, const int *mat)
                                      __asm__("FUNC_001670_VecMatTMul");
/* void SpecialVecMatMul (int *out3, int *tmp3, const int *vec3,
 *                        const int *mat9)        -- ffe.asm:100904.
 * The second argument is a SCRATCH vector the caller supplies: the routine
 * stores each first partial product through it and reads it back
 * (`mov [edi],eax` / `add eax,[edi]`). It is dereferenced at +0/+4/+8, so it is
 * three ints, not one -- and its final contents are recorded below, because a
 * translation that got the scratch pointer wrong would otherwise pass. */
extern void FUNC_001671(int *out, int *tmp, const int *vec, const int *mat)
                                      __asm__("FUNC_001671_SpecialVecMatMul");

/* ---- the pure-leaf batch -------------------------------------------------
 *
 * Twenty-five functions driven by one table instead of twenty-five hand-written
 * loops. They were selected by a script, not by browsing, against three
 * conditions that together make a plain-integer call SAFE:
 *
 *   1. no CALL of any kind          -- so nothing else has to be translated;
 *   2. no named DATA_ symbol and no memory operand whose base is anything but
 *      ebp/esp -- so the function never DEREFERENCES an argument, and an
 *      integer where a pointer was expected cannot fault;
 *   3. every [ebp+d] with d >= 8 is a full DWORD access.
 *
 * Condition 3 exists because four functions failed it -- FUNC_001491..001494
 * read `word [ebp+8]` AND `word [ebp+0xa]`, i.e. two shorts PACKED INTO ONE
 * pushed longword. Those are endianness-sensitive at the ABI boundary: on x86
 * the low half of the pushed dword sits at +8, on a 68k the HIGH half does, so
 * passing one `int` from C would hand the two machines a different pair of
 * shorts and the disagreement would look like a translator bug. They are held
 * back deliberately and want a two-short struct instead. See LEFTOFF.md.
 *
 * EVERY entry is declared int(int,int,int) and called with three arguments
 * whatever its real arity. That is safe on both machines and for the same
 * reason: cdecl and the m68k C ABI both pass every argument on the stack and
 * both make the CALLER clean up, so surplus arguments are simply never read.
 *
 * The nine zero-argument entries are empty stubs -- `push ebp / mov ebp,esp /
 * pop ebp / ret` and nothing else -- and a static check confirms none of them
 * ever writes eax. Their "return value" is therefore whatever the caller left
 * there and means nothing, so it is NOT recorded; the record prints a constant
 * 0 and exists to prove the one thing that IS observable about them, that they
 * return with the stack intact. Recording the register instead would compare
 * two different pieces of garbage and fail at random. */
typedef int (*FfeLeaf3)(int, int, int);

extern int FUNC_000023(int, int, int) __asm__("FUNC_000023");
extern int FUNC_000039(int, int, int) __asm__("FUNC_000039");
extern int FUNC_000455(int, int, int) __asm__("FUNC_000455");
extern int FUNC_000456(int, int, int) __asm__("FUNC_000456");
extern int FUNC_000660(int, int, int) __asm__("FUNC_000660");
extern int FUNC_000693(int, int, int) __asm__("FUNC_000693");
extern int FUNC_000710(int, int, int) __asm__("FUNC_000710");
extern int FUNC_000715(int, int, int) __asm__("FUNC_000715");
extern int FUNC_000838(int, int, int) __asm__("FUNC_000838");
extern int FUNC_001017(int, int, int) __asm__("FUNC_001017");
extern int FUNC_001030(int, int, int) __asm__("FUNC_001030");
extern int FUNC_001349(int, int, int) __asm__("FUNC_001349_FF30Null");
extern int FUNC_001501(int, int, int) __asm__("FUNC_001501");
extern int FUNC_001666(int, int, int) __asm__("FUNC_001666_SpecialDiv");
extern int FUNC_001715(int, int, int) __asm__("FUNC_001715");
extern int FUNC_001716(int, int, int) __asm__("FUNC_001716");
extern int FUNC_001722(int, int, int) __asm__("FUNC_001722");
extern int FUNC_001727(int, int, int) __asm__("FUNC_001727");
extern int FUNC_001728(int, int, int) __asm__("FUNC_001728");
extern int FUNC_001729(int, int, int) __asm__("FUNC_001729");
extern int FUNC_001798(int, int, int) __asm__("FUNC_001798");
extern int FUNC_001824(int, int, int) __asm__("FUNC_001824");
extern int FUNC_001831(int, int, int) __asm__("FUNC_001831");
extern int FUNC_001832(int, int, int) __asm__("FUNC_001832");
extern int FUNC_001836(int, int, int) __asm__("FUNC_001836");

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
 * The first case in this project where the ENDIANNESS IS IN THE ARGUMENT.
 *
 * These four read `word [ebp+0x8]` AND `word [ebp+0xa]` -- one cdecl argument
 * (arg_count correctly says 1), two 16-bit fields inside it. Hand both machines
 * the same `int` and they receive a DIFFERENT PAIR: on x86 the low half of the
 * pushed dword is at +8, on a 68k the high half is. The disagreement would look
 * exactly like a translator bug and would not be one -- the translated code is
 * right; the C prototype would be wrong.
 *
 * A struct passed BY VALUE fixes it, because then each compiler lays field `a`
 * at +8 and field `b` at +0xa in its own byte order, which is what the game's
 * own callers do. That is verified rather than assumed: m68k gcc compiles
 * `f((struct P){0x1122, 0x3344})` to `move.l #$11223344,-(sp)`, so `word (8,a5)`
 * is `a` and `word (10,a5)` is `b` -- the same roles MinGW gives them.
 *
 * WHAT THE VECTORS DELIBERATELY LEAVE OUT, and why it is not laziness.
 * All four end in a variable shift whose count is `15 - b` or `31 - b` in cl,
 * and x86 masks a 32-bit shift count to FIVE BITS while the 68k takes it
 * MODULO 64. They agree for 0..31 and diverge for 32..63 -- PLAN 3.6's accepted
 * risk, which until now had no live instance. It has one now, and the predicate
 * is exact:
 *
 *   1491  cl = 31-b, guarded by `b <= 31`     -> safe for every b >= 0
 *   1492  cl = 31-b, NO GUARD AT ALL          -> safe only for b in [0,31]
 *   1493  cl = 15-b, guarded by `b <= 15`     -> safe for every b >= -16
 *   1494  cl = 15-b or b-15, both guarded     -> safe for every b >= -16
 *
 * Outside those ranges the two machines really do compute different numbers,
 * and no vector below asks them to. Feeding one in would turn a documented
 * semantic gap into a red compare and teach nobody anything.
 *
 * 2026-08-06: THE SHIFT-COUNT HALF OF THIS GAP IS CLOSED. lower.py now masks a
 * cl count with `and.l #31' before the shift, which is exactly what x86 does to
 * it, so counts of 32..63 no longer diverge. This block stays switched off for
 * the OTHER reason it was switched off -- a packed pair of shorts still cannot
 * be told from a struct of two shorts by value -- and the carrier for the mask
 * is pfneg_cases(), which reaches the same four functions through the LIVE
 * single-dword prototypes at the end of this file. */
typedef struct { short a, b; } FfePacked;

extern int FUNC_001491(FfePacked p) __asm__("FUNC_001491");
extern int FUNC_001492(FfePacked p) __asm__("FUNC_001492");
extern int FUNC_001493(FfePacked p) __asm__("FUNC_001493");
extern int FUNC_001494(FfePacked p) __asm__("FUNC_001494");
#endif /* FFE_PACKED_SHORT_ARGS */

/* ---- FUNC_001461_GuiAddHotRect2 -----------------------------------------
 *
 * void GuiAddHotRect2 (int x, int y, int w, int h, int id) -- ffe.asm:86596.
 *
 * The function this slice exists to prove, because it is the one that lands the
 * two new rules honestly. It opens with
 *
 *     mov eax,DATA_007683
 *
 * -- a bare label as a SOURCE, which in NASM is the label's ADDRESS and not its
 * contents (a load would be `mov eax,[DATA_007683]`). That was the second
 * largest refusal class in the game, 136 functions, and the mapping is the one
 * lo_lea already uses for `lea r,[sym]`.
 *
 * What it does: DATA_007683 is a table of 129 entries of {x0,y0,x1,y1,id},
 * 20 bytes each, 2580 bytes in all (ffedat.asm:23317..23640), terminated by an
 * x0 of -1. The routine walks forward 20 bytes at a time until it finds that
 * sentinel, writes a fresh sentinel into the slot AFTER the one it is about to
 * fill, and then stores x0=x, y0=y, x1=x+w, y1=y+h, id.
 *
 * That makes it a much better test than its 22 instructions suggest: it reads a
 * label's address, does pointer arithmetic on the game's own static memory,
 * walks a sentinel-terminated list, and mutates it. The vectors reset the table
 * and record it in full after every call, so an off-by-one in the walk or a
 * misplaced sentinel shows up immediately. */
extern int DATA_007683[] __asm__("DATA_007683");

extern void FUNC_001461(int x, int y, int w, int h, int id)
                                      __asm__("FUNC_001461_GuiAddHotRect2");

/* The other two thirds of the hot-rect API, and the carriers for `rep movsd`.
 *
 *   void GuiAddHotAreas   (const int *src)  -- ffe.asm:86185. Appends a whole
 *                     -1-terminated list of 20-byte entries, one `rep movsd`
 *                     (ecx = 5) each.
 *   void GuiRemoveHotArea (int id)          -- ffe.asm:86300. Walks the table
 *                     and shifts entries DOWN over the removed one, again with
 *                     `rep movsd` -- an OVERLAPPING forward copy, which is
 *                     exactly what the 68k loop does too.
 *
 * They land the string-move rule, and they cost almost no new harness because
 * they mutate the same DATA_007683 the FUNC_001461 vectors already reset and
 * record in full. */
extern void FUNC_001440(const int *src) __asm__("FUNC_001440_GuiAddHotAreas");

/* ---- the indexed-addressing batch ---------------------------------------
 *
 * Four functions that land the 68020 indexed load/store. `ea()` used to refuse
 * every `[base+index*scale+disp]` operand -- 103 functions -- on the grounds
 * that an untested addressing mode is worse than a refusal. The mode is now
 * emitted, and these four test both shapes it comes in:
 *
 *   001565  xor [ecx+edx*4+0x9bd4],eax   -- base + index*4 + a LARGE
 *           displacement. 0x9bd4 is 39892, far outside the brief format's
 *           -128..127, so this is the first translated function to need the
 *           68020 FULL extension word. If vasm gets that wrong, it shows here.
 *   001558  mov [eax+edx*4+0x4],ecx      -- base + index*4 + small disp, a
 *           store into a caller-supplied object, guarded by a count at [obj].
 *   000093  mov [eax*4+DATA_008568],edx  -- the SUPPRESSED-BASE form, written
 *   000094  mov eax,[eax*4+DATA_008568]  -- ... and read back.
 *
 * The suppressed-base form is emitted as `lea (DATA_x).l,a0` plus the ordinary
 * indexed mode rather than as the 68020's own base-suppressed encoding: one
 * extra instruction, and it stays inside an encoding this project has already
 * shipped. DATA_008568 is `resb 400`, i.e. 100 ints (ffedat.asm:27625), so the
 * indices below stay inside 0..99. */
extern void FUNC_001565(int idx, int val, void *ctx) __asm__("FUNC_001565");
extern void *FUNC_001558(void *obj, int val)         __asm__("FUNC_001558");
extern void FUNC_000093(int idx, int val)            __asm__("FUNC_000093");
extern int  FUNC_000094(int idx)                     __asm__("FUNC_000094");

extern int DATA_008568[] __asm__("DATA_008568");

/* ---- the byte-MOVSX and truncating-IMUL batch ---------------------------
 *
 * Two more exact rules, 98 functions between them.
 *
 * `movsx r32,byte src` is the byte twin of the word rule already in: MOVE.B
 * into the low byte then EXTB.L, which sign-extends the low byte through the
 * whole longword. EXTB.L is a 68020 addition and this build's formal target is
 * a 68020, so it is available. 52 functions.
 *
 * The 2- and 3-operand IMUL keep only the LOW 32 bits of the product, which is
 * plain `MULS.L <ea>,Dn` -- no register pair, no 64-bit result. Only the
 * one-operand form needs Dh:Dl. 46 functions.
 *
 *   000657  movsx edx,byte [edx+0xca]  -- byte source from a caller buffer
 *   000024  imul eax,edx,0xc22e        -- the THREE-operand form, with an
 *           immediate, on the remainder of a `div` (so it also re-exercises
 *           the unsigned 64/32 divide)
 *   001515  imul ecx,edx               -- the TWO-operand form, twice, inside
 *           a 32x32 fixed-point multiply that also uses the one-operand IMUL,
 *           so both IMUL shapes appear in one function */
extern int FUNC_000657(int want, const void *p) __asm__("FUNC_000657");
extern int FUNC_000024(int x)                   __asm__("FUNC_000024");
extern int FUNC_001515(int a, int b)            __asm__("FUNC_001515");

/* ---- the SETcc / XCHG batch ----------------------------------------------
 *
 * x86 SETcc stores 0 or 1; the 68k Scc stores 0x00 or 0xFF, so it is `scc`
 * plus `neg.b`, which maps 0xFF to 0x01 and leaves 0x00 alone in two bytes.
 * The condition comes from the SAME table the branches use rather than a second
 * one -- duplicating it would be a second chance to invert a condition.
 * Measured: all 96 SETcc sites target al, cl or dl.
 *
 * `xchg` is `exg`, which the 68k has natively and which touches no flags,
 * matching x86 XCHG exactly. All 40 sites are register pairs.
 *
 * `xor aN,aN` (52 functions) went in at the same time: it is the clear-a-
 * register idiom, EOR cannot take an address register at all, and SUBA.L An,An
 * clears one in two bytes -- while affecting NO condition codes, where the x86
 * sets Z and clears N/V/C. Declared unreliable and checked dead.
 *
 *   000028  test byte [p+0x14c],0x10 / setnz al / and eax,1  -- a bit test
 *   001514  three 64-bit adds where the CARRY is detected by `cmp` + `seta`,
 *           i.e. SETcc feeding arithmetic rather than a branch, three times
 *           over, on a caller-supplied object */
extern int  FUNC_000028(const void *p)            __asm__("FUNC_000028");
extern void FUNC_001514(void *obj, const int *d3) __asm__("FUNC_001514");
extern void FUNC_001443(int id)         __asm__("FUNC_001443_GuiRemoveHotArea");

/* ---- the alignment-padding batch ----------------------------------------
 *
 * Three functions that were refused for a reason that turned out to be nothing
 * to do with them. A dry run over all 1,615 labels found 173 refused with
 * "control falls off the end of the function", and in ALL 173 the last
 * instruction is a `nop` -- ffedis pads each function out to an alignment
 * boundary and the CFG saw a NOP falling through into nothing.
 * parse._strip_padding() now drops that run, but only after proving it
 * unreachable, and the clean count went 516 -> 597.
 *
 * These three carry the rule. They were picked because they are cheap to drive,
 * not because they are interesting:
 *
 *   000402 / 000427   int f(int id) -- compare the argument against a global
 *                     and return one of two other globals, or 0. Three plain
 *                     4-byte BSS words (all `resb 4`), so the oracle can set
 *                     them and the probe can define them.
 *   000030            int f(const FfeThing *p) -- reads `word [eax+0xc]` and
 *                     does two UNSIGNED word compares against 0x76 and 0x85.
 *
 * 000030's word field is NOT the sub-word-stack problem: the field is at a
 * fixed offset in a struct laid out by whichever compiler is running, so a
 * `short` declared at offset 12 is at offset 12 on both machines. The stack
 * case is different precisely because a cdecl slot is a 4-byte SCALAR whose
 * bytes are ordered, not a struct whose fields are placed. */
extern int DATA_008861 __asm__("DATA_008861");
extern int DATA_008890 __asm__("DATA_008890");
extern int DATA_009105 __asm__("DATA_009105");

extern int FUNC_000402(int id) __asm__("FUNC_000402");
extern int FUNC_000427(int id) __asm__("FUNC_000427");

/* Only the field at +0xc is ever read, so the rest is opaque padding. Declared
 * as bytes so neither compiler can insert padding of its own and move it. */
typedef struct { unsigned char pad[12]; unsigned short v; } FfeThing;

extern int FUNC_000030(const FfeThing *p) __asm__("FUNC_000030");

/* ---- the operand-width batch --------------------------------------------
 *
 * Two functions that land the widening of every arithmetic rule. Until now
 * `test`, `and`, `or`, `xor`, `not`, `neg`, `inc`, `dec`, `add`, `sub`, `adc`,
 * `sbb` and the shifts all emitted a hardcoded `.l` and ignored op_size(), so
 * anything narrower was refused -- 165 functions between the two messages
 * "N-byte memory operand in an M-byte access" and "narrow register in a wider
 * operation". Threading the width through took the clean count 597 -> 675, and
 * the 49 already-green .o files came out BYTE-IDENTICAL, which is the proof
 * that the change is inert at 32 bits.
 *
 * void Int64LogicShift (int64 *val, int shift)   -- ffe.asm:83602.
 * The sibling of the already-green FUNC_001341_Int64ArithShift and the same
 * signature, so it reuses that loop exactly. It was blocked by a SECOND bug the
 * widening exposed: `shl eax,cl` has a 32-bit destination and an 8-bit count,
 * and op_size() read the count as a width and refused the instruction as
 * "operands of different widths". A shift's width comes from its destination
 * alone -- the count says how far, not how wide.
 *
 * int F422 (const void *a, const void *b)        -- ffe.asm:29949.
 * Ten BYTE comparisons in a loop: `mov bl,[esi+0x12]` then `cmp bl,[eax]`
 * against the ten bytes at a+0xd6, counting the matches. This is the one that
 * actually exercises `.b` arithmetic, and it does it through two
 * caller-supplied buffers, so nothing about the game's static data enters. */
extern void FUNC_001340(int *val, int shift) __asm__("FUNC_001340_Int64LogicShift");
extern int  FUNC_000422(const unsigned char *a, const unsigned char *b)
                                             __asm__("FUNC_000422");

#if FFE_SUBWORD_STACK_FIXED
/* void DrawCircle (const FfePoint *centre, int size, PALCOL col)
 * -- ffe.asm:96457, 164 instructions.
 *
 * The second carrier for the `lea rD,[ebp-N]` rule, and the more interesting
 * one: it has a SEVEN-KILOBYTE local frame (`add esp,0xfffff004` then
 * `add esp,0xfffff248`, 7608 bytes of span table at [ebp-7608]) and takes its
 * address with exactly the instruction the rule was added for. The harness
 * already runs the probe under `Stack 1000000`, so the frame is not a problem.
 *
 * It builds the span table once and then walks it calling FUNC_001601_DrawHLine
 * -- already green -- once per row. y is clipped at BOTH ends (the top row is
 * clamped to 0 by skipping forward in the table, the bottom to 158 by
 * `cmp eax,0x9e`); x is NOT clipped at all, so the vectors keep the circle
 * inside 320 except for one case that deliberately runs off the left edge to
 * record what the original does with it.
 *
 * `size` is a diameter: the radius is `size/2` rounded toward zero. There is a
 * quirk at size 7 -- `add edx,-2 / cmp edx,5 / jnz / add edx,3` turns 7 into 8 -
 * and the case list covers every size from 0 to 10 so that it cannot be
 * translated away by accident. */
extern void FUNC_001612(const FfePoint *centre, int size, int col)
                                      __asm__("FUNC_001612_DrawCircle");
#endif

/* THE FIRST FUNCTION HERE THAT READS GAME STATE, and the reason
 * build-oracle.py assembles its own copy of ffe.asm.
 *
 * DrawHLine gets its destination from [DATA_007773] -- the back-buffer
 * descriptor at ffedat.asm:24815, `{pixels, x, y, 320, 200, jump}` -- so the
 * only way to test it is to point that first field at a buffer we own. The
 * label is not GLOBAL in ffe.asm (of 8,596 DATA_ labels exactly one is), hence
 * the GLOBAL-adding wrapper; ref\jjffe itself is untouched.
 *
 * The struct is written out field by field rather than as a byte blob because
 * that is the only form that means the same thing on both machines: this
 * declaration is compiled for x86 here and the matching one in
 * amiga\probes\ffe_drawtest.c is compiled for m68k, and each compiler lays its
 * own longwords out in its own byte order. See the long comment there. */
typedef struct {
    unsigned char *pixels;
    int x, y, w, h, jump;
} FfeBufDesc;

extern FfeBufDesc     DATA_007773 __asm__("DATA_007773");
extern unsigned char  DATA_009226[] __asm__("DATA_009226");

static int g_fail;

static void check(const char *name, int got, int want, const char *args)
{
    int ok = (got == want);
    if (!ok) g_fail++;
    printf("%-4s %-12s %-28s got %11d (0x%08x)  want %11d (0x%08x)\n",
           ok ? "ok" : "FAIL", name, args, got, (unsigned)got, want, (unsigned)want);
}

/* What the x86 actually computes, restated in C so a disagreement points at a
 * misreading of the assembly rather than at the translator. FUNC_001521 is
 *     mov eax,[a] ; imul dword [b] ; shl eax,1 ; rcl edx,1 ; mov eax,edx
 * i.e. the full 64-bit product shifted left one and its HIGH half returned,
 * which is a >> 31 of the product. */
static int model_1521(int a, int b) { return (int)(((long long)a * b) >> 31); }
static int model_1522(int a)        { return (int)(((long long)a * a) >> 32); }

/* The same restatement for the 64-bit helpers. `long long` is used HERE, in the
 * model, and deliberately not in the declarations above: this is the place
 * where saying "what the assembly does is a 64-bit add" and being told whether
 * that is true costs nothing. */
static long long j64(int lo, int hi)
{
    return (long long)(((unsigned long long)(unsigned)hi << 32)
                       | (unsigned)lo);
}

static void check64(const char *name, const int *got, long long want,
                    const char *args)
{
    long long g = j64(got[0], got[1]);
    int ok = (g == want);
    if (!ok) g_fail++;
    printf("%-4s %-12s %-40s got %20lld  want %20lld\n",
           ok ? "ok" : "FAIL", name, args, g, want);
}

/* FUNC_001341 for |shift| <= 63; see the note on ffe_shift for why the range
 * stops there. Left shifts go through unsigned to keep the model itself out of
 * undefined behaviour -- the game's own code is a bit pattern shift. */
static long long model_1341(long long v, int s)
{
    if (s == 0) return v;
    if (s > 0)  return (long long)((unsigned long long)v << s);
    return v >> (-s);
}

static int model_1656(unsigned x)
{
    int n = 0;
    while (x >= 2u) { x >>= 1; n++; }
    return n;
}

/* The vector set both sides run. Kept in one place so the 68k side cannot
 * drift: edges, sign boundaries, powers of two and a few arbitrary bit
 * patterns. */
const int ffe_vec[] = { 0, 1, -1, 2, -2, 3, 0x7fffffff, (int)0x80000000,
                        0x40000000, (int)0xc0000000, 0x12345678,
                        (int)0xdeadbeef, 65536, -65536, 1000000, -1000000 };
const int ffe_nvec = (int)(sizeof ffe_vec / sizeof ffe_vec[0]);

/* 64-bit values as the game stores them: [0] is the low half, [1] the high
 * half. Chosen so that the carry between the halves actually does work --
 * a low half of -1 (0xffffffff) carries on any addition, a low half of 0
 * borrows on any subtraction, and both ends of the signed 64-bit range are in
 * here. Without those the add/adc pair would pass with the adc translated as a
 * plain add. */
const int ffe_i64[][2] = {
    {          0,           0 },        /* 0                                 */
    {          1,           0 },        /* 1                                 */
    {         -1,           0 },        /* 2^32-1: low half carries on +1    */
    {          0,           1 },        /* 2^32:   low half borrows on -1    */
    {         -1,          -1 },        /* -1                                */
    {         -2,          -1 },        /* -2                                */
    { (int)0x80000000,      0 },        /* low half at its own sign boundary */
    { 0x7fffffff,          0 },
    {          0, (int)0x80000000 },    /* INT64_MIN                         */
    {         -1,  0x7fffffff },        /* INT64_MAX: +1 carries into sign   */
    { 0x12345678, (int)0x9abcdef0 },
    { (int)0xdeadbeef, 0x0000cafe },
};
const int ffe_ni64 = (int)(sizeof ffe_i64 / sizeof ffe_i64[0]);

/* Shift counts for FUNC_001341, both signs, and both sides of the 32-bit
 * boundary where the function switches to its second code path.
 *
 * DELIBERATELY STOPPING AT 63. Past that the two architectures genuinely
 * disagree: the far path computes `cl = |shift| - 32` and x86 then masks the
 * count to 5 bits while the 68k takes it modulo 64, so a |shift| of 64..95
 * shifts by 32..63 on the 68k and by 0..31 on the x86. Testing there would be
 * testing a divergence this project has knowingly accepted (PLAN 3.6), not the
 * translation. */
const int ffe_shift[] = { 0, 1, 2, 8, 31, 32, 33, 63,
                          -1, -2, -8, -31, -32, -33, -63 };
const int ffe_nshift = (int)(sizeof ffe_shift / sizeof ffe_shift[0]);

/* FindMSB walks a chain of compares, so the vectors sit on every one of its
 * thresholds (2, 4, 16, 256, 0x10000) and just either side of them. */
const int ffe_msb[] = { 0, 1, 2, 3, 4, 5, 7, 8, 15, 16, 17, 31,
                        255, 256, 257, 0xffff, 0x10000, 0x10001,
                        0x7fffffff, (int)0x80000000, -1,
                        0x12345678, (int)0xdeadbeef };
const int ffe_nmsb = (int)(sizeof ffe_msb / sizeof ffe_msb[0]);

/* VecMatMul: a 3-vector times a 3x3 matrix in 0.31 fixed point, nine calls to
 * FUNC_001521. 0x7fffffff is ~1.0 and 0x40000000 is 0.5. */
const int ffe_vmvec[][3] = {
    { 0, 0, 0 },
    { 0x40000000, 0, 0 },
    { 0, 0x40000000, 0 },
    { 0x7fffffff, (int)0x80000000, 1 },
    { 0x12345678, (int)0xdeadbeef, 0x40000000 },
    { -1, 1, -65536 },
};
const int ffe_nvmvec = (int)(sizeof ffe_vmvec / sizeof ffe_vmvec[0]);

const int ffe_vmmat[][9] = {
    { 0x7fffffff, 0, 0,  0, 0x7fffffff, 0,  0, 0, 0x7fffffff },
    { 0, 0x7fffffff, 0,  (int)0x80000000, 0, 0,  0, 0, 0x7fffffff },
    { 0x40000000, 0x40000000, 0x40000000,
      0x40000000, (int)0xc0000000, 0x40000000,
      0x12345678, (int)0xdeadbeef, 0x7fffffff },
    { -1, -1, -1,  -1, -1, -1,  -1, -1, -1 },
};
const int ffe_nvmmat = (int)(sizeof ffe_vmmat / sizeof ffe_vmmat[0]);

/* ---- FUNC_001601_DrawHLine ---------------------------------------------- */

/* A four-row stand-in for the back buffer. Rows are 320 bytes, every span is
 * drawn on row 1, and every byte outside the span is left at HL_FILL -- so the
 * row above and the two below are guard land: an overrun of any size, in either
 * direction, moves `first` or `count` below and shows up as a mismatch instead
 * of being invisible.
 *
 * Alignment is forced to 4 rather than assumed. It does not change the ANSWER
 * -- the routine splits its phases on the x COORDINATE, not on the address, and
 * for a colour of 0..255 all four bytes of its replicated longword are equal --
 * but it does make the two machines take the same PATH through the routine, and
 * that is what these vectors exist to exercise. Spelled out on both sides
 * because the m68k side needs it: gcc there aligns a long to two bytes unless
 * told otherwise. */
#define HL_ROWS 4
#define HL_W    320
#define HL_N    (HL_W * HL_ROWS)
#define HL_FILL 0xAA

static unsigned char g_hlbuf[HL_N] __attribute__((aligned(4)));

/* Colours stay inside 0..255, and that is a real boundary rather than a lack of
 * imagination. DrawHLine replicates its colour argument with
 * `shl 8 / or / shl 16 / or` and then stores the 32-bit result -- for a palette
 * index the four bytes are identical and byte order cannot matter, but for any
 * argument above 255 they differ and the little-endian store writes them in the
 * opposite order to the 68k's. Testing there would be testing PLAN 6's
 * endianness problem, not the translation, and the game only ever passes a
 * PALCOL. HL_FILL is 0xAA = 170 and no case uses that colour, so "changed"
 * really does mean changed. */
static void hl_emit(int x, int y, int len, int col, int tsv)
{
    unsigned char *buf = g_hlbuf;
    int i, first = -1, last = -1, count;

    memset(buf, HL_FILL, HL_N);
    DATA_007773.pixels = buf;
    FUNC_001601(x, y, len, col);

    for (i = 0; i < HL_N; i++)
        if (buf[i] != HL_FILL) { if (first < 0) first = i; last = i; }
    count = (first < 0) ? 0 : last - first + 1;

    if (tsv) {
        printf("FUNC_001601\t%d\t%d\t%d\t%d\t%d\t%d",
               x, y, len, col, first, count);
        for (i = 0; i < count; i++) printf("\t%d", buf[first + i]);
        printf("\n");
        return;
    }

    /* Human mode checks against the obvious model: a span of `len` bytes of
     * `col` starting at y*320+x, and nothing at all for len <= 0. */
    {
        int wfirst = (len > 0) ? y * HL_W + x : -1;
        int wcount = (len > 0) ? len : 0;
        int bad = (first != wfirst) || (count != wcount);
        for (i = 0; !bad && i < count; i++)
            if (buf[first + i] != (unsigned char)col) bad = 1;
        if (bad) g_fail++;
        printf("%-4s 1601  x=%3d y=%d len=%4d col=%3d   first=%5d count=%4d"
               "  (want first=%5d count=%4d)\n",
               bad ? "FAIL" : "ok", x, y, len, col, first, count,
               wfirst, wcount);
    }
}

/* The cases, in one place so the 68k side cannot drift. Every length 0..17 at
 * every start alignment 0..3 walks the routine's three phases -- single bytes
 * until the x coordinate is a multiple of 4, then whole longwords, then the
 * trailing bytes -- including the lengths that skip a phase entirely. */
static void hl_cases(int tsv)
{
    int a, L;

    for (a = 0; a < 4; a++)
        for (L = 0; L <= 17; L++)
            hl_emit(64 + a, 1, L, 1 + (a * 18 + L), tsv);

    hl_emit(64, 1, -1, 5, tsv);      /* negative: returns before touching a thing */
    hl_emit(64, 1, -100, 6, tsv);
    hl_emit(67, 1, -1, 7, tsv);      /* ... and from an unaligned x too */
    hl_emit(0, 1, HL_W, 8, tsv);     /* a whole row, aligned start and end */
    hl_emit(316, 1, 8, 9, tsv);      /* past the right edge: it does NOT clip */
    hl_emit(65, 1, 255, 11, tsv);    /* long run from an odd x */
    hl_emit(70, 1, 5, 0, tsv);       /* colour 0 */
    hl_emit(70, 1, 5, 255, tsv);     /* colour 255 */
}

/* ---- FUNC_001600_DrawHLineClip / FUNC_001599_DrawHLineConClip ------------ */

/* These two sit directly on top of DrawHLine and are the forms the game
 * actually calls: 1600 clips the span to the 320-pixel width, and 1599 adds the
 * console clip -- it drops the whole span when ypos is outside the 158-line 3D
 * viewport, which is exactly the height of DATA_007774, the upper backbuffer.
 *
 * A FULL-SIZE, GUARDED buffer, and the guards are not decoration.
 *
 * Neither routine clips ypos. 1600 takes it on trust, and 1599's own check is
 *
 *     cmp eax,0x9e ; jnc <return>
 *
 * which is an UNSIGNED comparison -- so it rejects ypos >= 158 and, because
 * -1 is 0xFFFFFFFF unsigned, it rejects every negative ypos too. That single
 * `jnc` is the most interesting thing in this slice: translate it as the signed
 * `bge` instead of `bcc` and ypos = -1 sails through and writes 320 bytes in
 * front of the buffer. The vectors below deliberately include that case, so the
 * buffer is padded at BOTH ends and the whole allocation is scanned. A wrong
 * translation lands in the guard and is reported as a mismatch; without the
 * front guard it would be an out-of-bounds write, which is a crash on a good
 * day and silent corruption on a bad one.
 *
 * 202 rows rather than 4 for the same reason: if the ypos check were dropped
 * altogether, ypos = 200 must land inside the allocation to be REPORTED rather
 * than to destroy the heap. */
#define CL_W     320
#define CL_ROWS  202
#define CL_GUARD (CL_W * 2)
#define CL_N     (CL_GUARD + CL_W * CL_ROWS + CL_GUARD)
#define CL_FILL  0xAA

static unsigned char g_clbuf[CL_N] __attribute__((aligned(4)));

/* What the two routines are specified to do, read straight off ffe.asm:94222
 * and 94197. Used only by the human-readable mode; --tsv records what the
 * original actually did and never consults this. */
static int model_clip(int which, int x, int y, int len, int *ox, int *olen)
{
    if (which == 1599 && (unsigned)y >= 0x9eu) return 0;   /* UNSIGNED, see above */
    if (x < 0) {
        if (len + x <= 0) return 0;
        len += x;
        x = 0;
    }
    if (x + len > CL_W) {
        len = CL_W - x;
        if (len <= 0) return 0;
    }
    if (len <= 0) return 0;          /* passed on to DrawHLine, which draws nothing */
    *ox = x; *olen = len;
    return 1;
}

static void cl_emit(int which, int x, int y, int len, int col, int tsv)
{
    unsigned char *pix = g_clbuf + CL_GUARD;
    int i, first = -1, last = -1, count;

    memset(g_clbuf, CL_FILL, CL_N);
    DATA_007773.pixels = pix;
    if (which == 1600) FUNC_001600(x, y, len, col);
    else               FUNC_001599(x, y, len, col);

    for (i = 0; i < CL_N; i++)
        if (g_clbuf[i] != CL_FILL) { if (first < 0) first = i; last = i; }
    count = (first < 0) ? 0 : last - first + 1;
    /* Report the offset from the PIXEL ORIGIN, so a write in front of the
     * buffer comes out as a negative number instead of being indistinguishable
     * from a legitimate one. count == 0 is the "nothing drawn" case; first is
     * then meaningless and is printed as 0 so the two machines agree on it. */
    first = (count == 0) ? 0 : first - CL_GUARD;

    if (tsv) {
        printf("FUNC_%06d\t%d\t%d\t%d\t%d\t%d\t%d",
               which, x, y, len, col, first, count);
        for (i = 0; i < count; i++)
            printf("\t%d", g_clbuf[CL_GUARD + first + i]);
        printf("\n");
        return;
    }

    {
        int mx = 0, ml = 0;
        int drew = model_clip(which, x, y, len, &mx, &ml);
        int wfirst = drew ? y * CL_W + mx : 0;
        int wcount = drew ? ml : 0;
        int bad = (first != wfirst) || (count != wcount);
        for (i = 0; !bad && i < count; i++)
            if (g_clbuf[CL_GUARD + first + i] != (unsigned char)col) bad = 1;
        if (bad) g_fail++;
        printf("%-4s %d  x=%5d y=%5d len=%5d col=%3d   first=%6d count=%4d"
               "  (want first=%6d count=%4d)\n",
               bad ? "FAIL" : "ok", which, x, y, len, col, first, count,
               wfirst, wcount);
    }
}

/* One case list for both routines, so the 68k side cannot drift from this one.
 * 1599 is 1600 plus the ypos gate, so every 1600 case is worth running through
 * 1599 as well at a ypos the gate accepts -- that proves the gate lets normal
 * work through, which a table of rejections alone would not. */
static void cl_cases(int tsv)
{
    /* {x, len} pairs walking the clip's decision tree. y and col are added
     * below. Ordered so that the pure-x cases come first. */
    static const int xl[][2] = {
        {  10,  20 },   /* entirely inside, nothing to clip                    */
        {   0,  20 },   /* flush against the left edge                         */
        {   0, 320 },   /* the whole row exactly                               */
        {  -5,  20 },   /* overlaps the left edge: x -> 0, len -> 15           */
        {  -1,   8 },   /* clipped start lands on x=0 ...                      */
        {  -2,   8 },   /* ... at each alignment, so the clipped span still    */
        {  -3,   8 },   /*     walks DrawHLine's byte/longword/byte phases     */
        { -19,  20 },   /* one pixel of it survives                            */
        { -20,  20 },   /* len+x == 0 exactly: the `jng` boundary, draws none  */
        { -21,  20 },   /* one past that boundary                              */
        {-100,  20 },   /* far off to the left                                 */
        {  -5, 400 },   /* clipped at BOTH ends in one call                    */
        { 300,  20 },   /* ends exactly at 320: the `cmp 0x140` boundary       */
        { 300,  40 },   /* overlaps the right edge: len -> 20                  */
        { 319,   1 },   /* the last pixel of the row                           */
        { 319,  10 },   /* ... and the same start running off the end          */
        { 320,  10 },   /* starts exactly at the edge: len -> 0, draws none    */
        { 400,  10 },   /* far off to the right: len -> -80                    */
        {  10,   0 },   /* zero length, reaches DrawHLine and draws nothing    */
        {  10,  -5 },   /* negative length, likewise                           */
        {   0,  -1 },
    };
    /* ypos values. The first four are inside the 158-line viewport and must be
     * drawn by both routines; the rest are outside it and must be dropped by
     * 1599 while 1600 -- which does not check ypos at all -- still draws them.
     * -1 and -2 are the unsigned-comparison trap. */
    static const int ys[] = { 0, 1, 100, 157, 158, 159, 199, -1, -2 };
    const int nxl = (int)(sizeof xl / sizeof xl[0]);
    const int nys = (int)(sizeof ys / sizeof ys[0]);
    int i, j, c = 1;

    /* 1600 over every x case at a safe ypos, then the ypos sweep on one x. */
    for (i = 0; i < nxl; i++)
        cl_emit(1600, xl[i][0], 1, xl[i][1], 1 + (c++ & 0xfe), tsv);
    for (j = 0; j < nys; j++)
        if (ys[j] >= 0)                  /* 1600 has no ypos guard; a negative
                                          * ypos here would be a real overrun
                                          * of the original's own making. */
            cl_emit(1600, 10, ys[j], 20, 1 + (c++ & 0xfe), tsv);

    /* 1599 over the full cross product: every x case at every ypos. */
    for (j = 0; j < nys; j++)
        for (i = 0; i < nxl; i++)
            cl_emit(1599, xl[i][0], ys[j], xl[i][1], 1 + (c++ & 0xfe), tsv);
}

/* ---- FUNC_001616_DrawLine -----------------------------------------------
 *
 * The first function here whose arguments are POINTERS TO A STRUCT rather than
 * plain integers: DrawLine(const FfePoint *p1, const FfePoint *p2, PALCOL col)
 * with FfePoint = { short x, y; }, read straight off [ebp+8] / [ebp+0xc] and the
 * `word [edi]` / `word [edi+2]` accesses in ffe.asm:96771-96773.
 *
 * That sounds like an endianness problem and is not one. The two points are
 * built HERE, in the test's own memory, by whichever compiler is compiling this
 * file -- MinGW for x86, bebbo gcc for the 68k -- so each machine lays its own
 * shorts out in its own byte order and the code that reads them back is compiled
 * for that same machine. Nothing crosses. The static game data in ffedat.asm is
 * the endianness problem, and Sin16/Sin32 are the functions that first meet it.
 * What this slice tests is the 16-bit INSTRUCTION width: `mov dx,[edi+2]`,
 * `cmp ax,[esi]`, and the 47 `movsx eax,word [...]` that make DrawLine's slope
 * arithmetic work.
 *
 * WHY THE BUFFER IS GUARDED, again. DrawLine calls FUNC_001601_DrawHLine
 * DIRECTLY -- not the clipping wrapper FUNC_001600 -- so it clips nothing at
 * all. Every coordinate below is inside 320x200 except the last two, which
 * deliberately run off the left and right edges to record what the original does
 * with them; both stay inside the allocation, and the two guard rows at each end
 * catch anything that does not.
 *
 * WHY THE RECORD IS A PER-ROW SUMMARY rather than the raw bytes hl_emit prints:
 * a corner-to-corner diagonal touches 200 rows, so the span from the first
 * non-fill byte to the last is ~64,000 bytes and printing them all would make
 * the TSV unreadable and the Amiga-side log enormous. Instead each touched row
 * contributes five numbers -- row, first column, extent, count of written bytes
 * and their sum. Between them those pin down every way the translation could go
 * wrong: a shifted span moves `first`, a wrong length moves `extent` and
 * `count`, a gap makes `extent` and `count` disagree, and a wrong colour moves
 * `sum` (every byte DrawHLine writes is `col`, so sum == count * col exactly).
 */
#define LN_W     320
#define LN_ROWS  202
#define LN_GUARD 2                        /* whole rows, at BOTH ends */
#define LN_TOTAL (LN_ROWS + 2 * LN_GUARD)
#define LN_N     (LN_TOTAL * LN_W)
#define LN_FILL  0xAA

static unsigned char g_lnbuf[LN_N] __attribute__((aligned(4)));

static void ln_emit(int x1, int y1, int x2, int y2, int col, int tsv)
{
    unsigned char *pix = g_lnbuf + LN_GUARD * LN_W;
    FfePoint p1, p2;
    int r, i, nrows = 0, npix = 0, bad = 0;

    memset(g_lnbuf, LN_FILL, LN_N);
    DATA_007773.pixels = pix;
    p1.x = (short)x1; p1.y = (short)y1;
    p2.x = (short)x2; p2.y = (short)y2;
    FUNC_001616(&p1, &p2, col);

    for (r = 0; r < LN_TOTAL; r++) {
        const unsigned char *row = g_lnbuf + r * LN_W;
        for (i = 0; i < LN_W; i++)
            if (row[i] != LN_FILL) { nrows++; break; }
    }

    if (tsv) {
        printf("FUNC_001616\t%d\t%d\t%d\t%d\t%d\t%d",
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
            printf("\t%d\t%d\t%d\t%d\t%d",
                   r - LN_GUARD, first, last - first + 1, n, sum);
        }
        printf("\n");
        return;
    }

    /* No model: DrawLine's slope arithmetic restated in C would only be a
     * second chance to be wrong in the same way, and --tsv already records what
     * the original does. What IS checked is an invariant that costs nothing and
     * catches a buffer mix-up outright -- every byte written must be the colour
     * that was asked for. (No case below uses colour 0xAA, which would be
     * indistinguishable from the fill.) */
    for (i = 0; i < LN_N; i++)
        if (g_lnbuf[i] != LN_FILL) {
            npix++;
            if (g_lnbuf[i] != (unsigned char)col) bad = 1;
        }
    if (bad) g_fail++;
    printf("%-4s 1616 (%4d,%4d)-(%4d,%4d) col=%3d   rows=%4d pixels=%6d\n",
           bad ? "FAIL" : "ok", x1, y1, x2, y2, col, nrows, npix);
}

/* The case list, walking every branch of the routine that is reachable.
 *
 * Three of DrawLine's blocks are NOT reachable and no vector tries to be: the
 * `jz near JUMP_006152` at ffe.asm:96818 tests a dy the entry test at 96774-75
 * has already proved non-zero, and the `jns JUMP_006150` at 96822 tests a dy the
 * endpoint swap at 96811-13 has already made non-negative. So JUMP_006152 and
 * the whole JUMP_006149 arm are dead code in the original. They are translated
 * anyway -- refusing to translate a block because it looks unreachable is
 * exactly the kind of cleverness this project does not do -- but they cannot be
 * covered, and saying so here is better than leaving a coverage gap unexplained.
 */
static void ln_cases(int tsv)
{
    static const int pts[][4] = {
        {  10,   5,  50,   5 },  /* horizontal, left to right                  */
        {  50,   6,  10,   6 },  /* ... and right to left: the JUMP_006146 half*/
        { 100,   7, 100,   7 },  /* one pixel: dx == dy == 0                   */
        {  30,  10,  30,  40 },  /* vertical down: slope 0                     */
        {  30,  50,  30,  20 },  /* vertical up: forces the endpoint swap      */
        {  10,  60,  40,  90 },  /* 45 degrees: slope is exactly 0x10000, the
                                  * `jg JUMP_006156` boundary at 96897-98      */
        {  40, 100,  10,  70 },  /* the same line entered the other way round  */
        {  10,  10,  41,  40 },  /* one step past 45: the multi-pixel run loop */
        {  10, 110, 100, 120 },  /* shallow: dx >> dy, long runs per row       */
        {  10, 120,  20, 160 },  /* steep: dy >> dx, one pixel per row         */
        { 100,  10,  70,  40 },  /* slope exactly -0x10000: the `jl` boundary
                                  * at 96965-66, which must NOT be taken       */
        { 100,  10,  40,  50 },  /* slope -1.5: the mirrored run, JUMP_006162  */
        {  70,  40, 100,  10 },  /* ... and that line entered the other way    */
        {   0,   0, 319,   3 },  /* full width in four rows                    */
        {   0, 100, 319, 100 },  /* a whole row, exactly                       */
        {   0, 199, 319, 199 },  /* the last row of the buffer                 */
        { 319,   0,   0, 199 },  /* the long anti-diagonal, corner to corner   */
        {   0,   0, 319, 199 },  /* ... and the long diagonal                  */
        {   0,   0,   0,   0 },  /* the very first pixel                       */
        { -10,  50,  10,  50 },  /* off the left edge: DrawLine does NOT clip  */
        { 310,  60, 330,  60 },  /* ... and off the right one                  */
    };
    const int n = (int)(sizeof pts / sizeof pts[0]);
    int i, c = 1;

    for (i = 0; i < n; i++)
        ln_emit(pts[i][0], pts[i][1], pts[i][2], pts[i][3],
                1 + (c++ & 0xfe), tsv);

    /* The two ends of the colour range, on a shape short enough to read. */
    ln_emit(10, 150, 30, 170, 0, tsv);
    ln_emit(10, 150, 30, 170, 255, tsv);
    ln_emit(10, 150, 30, 170, 1, tsv);
}

/* ---- FUNC_001604_DrawTriangle -------------------------------------------
 *
 * void DrawTriangle(const FfePoint *a, const FfePoint *b, const FfePoint *c,
 *                   PALCOL col)          -- ffe.asm:94539, 749 instructions.
 *
 * The filled triangle, and the largest function translated so far by a factor of
 * three. It does NOT go through DrawHLine: it sorts the three vertices by y,
 * interpolates two 16.16 x edges down the scanlines and fills each row with a
 * direct call to _memset, six sites in all. The destination row pointer comes
 * from [DATA_007773] once, at ffe.asm:94577, and then advances by 0x140 a row.
 *
 * UNLIKE DrawLine, THIS ONE CLIPS -- completely, in both axes, which is what
 * makes the cases below safe to write at any coordinate:
 *
 *     cmp ebx,0x9e ; jnc <skip row>            (ffe.asm:94765)
 *
 * is the same UNSIGNED test as FUNC_001599_DrawHLineConClip's, against the same
 * 158-line viewport, so it drops y >= 158 and -- because -1 is 0xFFFFFFFF
 * unsigned -- every negative y as well. The x ends are clamped separately, to 0
 * by `test eax,eax / jnl / xor eax,eax` and to 320 by `cmp edx,0x140`, and an
 * empty or inverted span is skipped by `cmp edx,eax / jng`.
 *
 * So a correct translation can never write outside the 320x158 rectangle. The
 * buffer is still guarded at both ends, for exactly the reason the clip slice
 * was: translate that `jnc` as the signed `bge` and a negative y sails through
 * and writes in front of the pixel origin. The cases below include y = -50 and
 * y = -1 vertices precisely so that a wrong branch lands in a guard row and is
 * REPORTED rather than corrupting memory on a machine that has none to protect.
 */
static unsigned char g_trbuf[LN_N] __attribute__((aligned(4)));

static void tr_emit(int x0, int y0, int x1, int y1, int x2, int y2,
                    int col, int tsv)
{
    unsigned char *pix = g_trbuf + LN_GUARD * LN_W;
    FfePoint a, b, c;
    int r, i, nrows = 0, npix = 0, bad = 0;

    memset(g_trbuf, LN_FILL, LN_N);
    DATA_007773.pixels = pix;
    a.x = (short)x0; a.y = (short)y0;
    b.x = (short)x1; b.y = (short)y1;
    c.x = (short)x2; c.y = (short)y2;
    FUNC_001604(&a, &b, &c, col);

    for (r = 0; r < LN_TOTAL; r++) {
        const unsigned char *row = g_trbuf + r * LN_W;
        for (i = 0; i < LN_W; i++)
            if (row[i] != LN_FILL) { nrows++; break; }
    }

    if (tsv) {
        printf("FUNC_001604\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d",
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
            printf("\t%d\t%d\t%d\t%d\t%d",
                   r - LN_GUARD, first, last - first + 1, n, sum);
        }
        printf("\n");
        return;
    }

    /* Two invariants, both cheap and both a real check rather than a model: no
     * byte may be anything other than the requested colour, and NO ROW OUTSIDE
     * 0..157 MAY BE TOUCHED AT ALL -- which is the `jnc` trap restated as an
     * assertion. Reproducing the edge interpolation in C would only be a second
     * chance to misread ffe.asm; --tsv already records what the original does. */
    for (r = 0; r < LN_TOTAL; r++) {
        const unsigned char *row = g_trbuf + r * LN_W;
        for (i = 0; i < LN_W; i++)
            if (row[i] != LN_FILL) {
                npix++;
                if (row[i] != (unsigned char)col) bad = 1;
                if (r - LN_GUARD < 0 || r - LN_GUARD > 157) bad = 1;
            }
    }
    if (bad) g_fail++;
    printf("%-4s 1604 (%4d,%4d)(%4d,%4d)(%4d,%4d) col=%3d  rows=%4d pixels=%6d\n",
           bad ? "FAIL" : "ok", x0, y0, x1, y1, x2, y2, col, nrows, npix);
}

/* The cases. DrawTriangle's first three blocks are a three-compare vertex sort,
 * so the SAME triangle is submitted in all six vertex orders -- that is the only
 * way to walk every arm of the sort, and a translation that got one swap wrong
 * would produce six different answers where the original produces one. */
static void tr_cases(int tsv)
{
    static const int tri[][6] = {
        {  50,  10, 100,  60,  20,  90 },  /* the six orderings of one triangle */
        {  50,  10,  20,  90, 100,  60 },
        { 100,  60,  50,  10,  20,  90 },
        { 100,  60,  20,  90,  50,  10 },
        {  20,  90,  50,  10, 100,  60 },
        {  20,  90, 100,  60,  50,  10 },
        {  20,  20, 100,  20,  60,  80 },  /* flat top, both edge orders        */
        { 100,  20,  20,  20,  60,  80 },
        {  60,  20,  20,  80, 100,  80 },  /* flat bottom, both edge orders     */
        {  60,  20, 100,  80,  20,  80 },
        {  50,  50,  50,  50,  50,  50 },  /* all three vertices coincide       */
        {  20,  30, 100,  30,  60,  30 },  /* all three on one scanline         */
        {  20,  20,  20,  80,  20, 140 },  /* all three in one column           */
        {  10,  10,  50,  50,  90,  90 },  /* colinear: zero area               */
        {  90,  90,  50,  50,  10,  10 },  /* ... entered the other way round   */
        {  10,  10,  11, 100,  12,  10 },  /* a tall thin sliver                */
        {  10,  10, 300,  11,  10,  12 },  /* ... and a wide flat one           */
        {  20, 150, 100, 157,  60, 158 },  /* straddles the `cmp ebx,0x9e` edge */
        {  20, 150, 100, 160,  60, 170 },  /* mostly past it                    */
        {  20, -50, 100,  50,  60, 100 },  /* apex above the buffer: the UNSIGNED
                                            * test must drop those rows         */
        {  20, -50, 100, -10,  60,  -1 },  /* entirely above it                 */
        {  20, 200, 100, 250,  60, 300 },  /* entirely below it                 */
        { -60,  10,  60,  10,   0,  60 },  /* off the left edge: x clamps to 0  */
        { 260,  10, 380,  10, 320,  60 },  /* off the right: clamps to 320      */
        {-100,  10, 420,  20, 160, 100 },  /* clipped at BOTH ends at once      */
        {   0,   0, 319,   0,   0, 157 },  /* exactly the corners it may touch  */
    };
    const int n = (int)(sizeof tri / sizeof tri[0]);
    int i, c = 1;

    for (i = 0; i < n; i++)
        tr_emit(tri[i][0], tri[i][1], tri[i][2], tri[i][3], tri[i][4],
                tri[i][5], 1 + (c++ & 0xfe), tsv);

    tr_emit(30, 30, 90, 40, 50, 100, 0, tsv);
    tr_emit(30, 30, 90, 40, 50, 100, 255, tsv);
    tr_emit(30, 30, 90, 40, 50, 100, 1, tsv);
}

/* ---- the 64-bit / vector maths batch ------------------------------------
 *
 * Recorded, never modelled. Four of the six are plumbing around functions that
 * are already green -- 1661/1662 are three calls to Int64Add64/Int64Sub64 and
 * 1670/1671 are nine calls to FUNC_001521 apiece -- so a model here would only
 * restate arithmetic that is already held to the original elsewhere. What these
 * vectors actually test is the PLUMBING: the pointer arithmetic between the
 * calls, the stack discipline around them, and (for 1671) a caller-supplied
 * scratch buffer that is written and read back between two of them.
 *
 * The 3x int64 vectors are built by sliding a window over ffe_i64[] rather than
 * by writing a second table, so the two machines cannot disagree about the
 * inputs without also disagreeing about ffe_i64 itself, which the 1333/1335
 * records already pin down. */
static void v64_fill(int *v, int k)
{
    int i;
    for (i = 0; i < 3; i++) {
        v[i * 2 + 0] = ffe_i64[(k + i) % ffe_ni64][0];
        v[i * 2 + 1] = ffe_i64[(k + i) % ffe_ni64][1];
    }
}

static void math_cases(int tsv)
{
    int i, j, k;

    for (i = 0; i < ffe_ni64; i++)
        for (j = 0; j < ffe_nvec; j++) {
            int r[2];
            r[0] = r[1] = 0;
            FUNC_001334(r, ffe_i64[i][0], ffe_i64[i][1], ffe_vec[j]);
            if (tsv)
                printf("FUNC_001334\t%d\t%d\t%d\t%d\t%d\n",
                       ffe_i64[i][0], ffe_i64[i][1], ffe_vec[j], r[0], r[1]);
        }

    for (i = 0; i < ffe_nvec; i++)
        for (j = 0; j < ffe_nvec; j++) {
            int r[2];
            r[0] = r[1] = 0;
            FUNC_001339(r, ffe_vec[i], ffe_vec[j]);
            if (tsv)
                printf("FUNC_001339\t%d\t%d\t%d\t%d\n",
                       ffe_vec[i], ffe_vec[j], r[0], r[1]);
        }

    /* `a` is both the input and the output, so a copy is kept and printed --
     * printing the post-call buffer as if it were the input would hide exactly
     * the mistake an in-place routine is most likely to make. */
    for (i = 0; i < ffe_ni64; i++)
        for (j = 0; j < ffe_ni64; j++) {
            int a[6], b[6], a0[6];
            v64_fill(a, i);
            v64_fill(b, j);
            memcpy(a0, a, sizeof a0);
            FUNC_001661(a, b);
            if (tsv) {
                printf("FUNC_001661");
                for (k = 0; k < 6; k++) printf("\t%d", a0[k]);
                for (k = 0; k < 6; k++) printf("\t%d", b[k]);
                for (k = 0; k < 6; k++) printf("\t%d", a[k]);
                printf("\n");
            }
        }

    for (i = 0; i < ffe_ni64; i++)
        for (j = 0; j < ffe_ni64; j++) {
            int a[6], b[6], a0[6];
            v64_fill(a, i);
            v64_fill(b, j);
            memcpy(a0, a, sizeof a0);
            FUNC_001662(a, b);
            if (tsv) {
                printf("FUNC_001662");
                for (k = 0; k < 6; k++) printf("\t%d", a0[k]);
                for (k = 0; k < 6; k++) printf("\t%d", b[k]);
                for (k = 0; k < 6; k++) printf("\t%d", a[k]);
                printf("\n");
            }
        }

    /* Indices rather than the 12 input words: ffe_vmvec[] and ffe_vmmat[] are
     * already pinned down word for word by the FUNC_001669 records above, so
     * repeating them here would add length without adding evidence. */
    for (i = 0; i < ffe_nvmvec; i++)
        for (j = 0; j < ffe_nvmmat; j++) {
            int out[3];
            out[0] = out[1] = out[2] = 0;
            FUNC_001670(out, ffe_vmvec[i], ffe_vmmat[j]);
            if (tsv)
                printf("FUNC_001670\t%d\t%d\t%d\t%d\t%d\n",
                       i, j, out[0], out[1], out[2]);
        }

    /* The scratch vector is cleared before the call and printed after it, so a
     * translation that left the scratch pointer pointing somewhere else shows
     * up here rather than passing on the strength of a correct `out`. */
    for (i = 0; i < ffe_nvmvec; i++)
        for (j = 0; j < ffe_nvmmat; j++) {
            int out[3], tmp[3];
            out[0] = out[1] = out[2] = 0;
            tmp[0] = tmp[1] = tmp[2] = 0;
            FUNC_001671(out, tmp, ffe_vmvec[i], ffe_vmmat[j]);
            if (tsv)
                printf("FUNC_001671\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\n",
                       i, j, out[0], out[1], out[2], tmp[0], tmp[1], tmp[2]);
        }

    if (!tsv)
        printf("     %d vectors over 1334 1339 1661 1662 1670 1671, recorded "
               "only (see the comment above)\n",
               ffe_ni64 * ffe_nvec + ffe_nvec * ffe_nvec + 2 * ffe_ni64 * ffe_ni64
               + 2 * ffe_nvmvec * ffe_nvmmat);
}

/* ---- the pure-leaf batch, driven by the table ---------------------------- */

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

/* `keep` is 0 for the nine stubs, whose return register is undefined -- see the
 * long comment by the declarations. The arguments are printed whatever the
 * arity, so every record is self-describing and the file stays positional. */
static void leaf_emit(const char *name, FfeLeaf3 fn, int a, int b, int c,
                      int keep, int tsv)
{
    int r = fn(a, b, c);
    if (!keep) r = 0;
    if (tsv) printf("%s\t%d\t%d\t%d\t%d\n", name, a, b, c, r);
}

static void leaf_cases(int tsv)
{
    /* For the three-argument entries: a sliding window over the whole of
     * ffe_vec, so every value is seen in every position, plus a dense 4x4x4
     * over the four values that break things most often. 16 + 64 beats 16^3. */
    static const int dense[] = { 0, 1, -1, 0x7fffffff };
    const int nd = (int)(sizeof dense / sizeof dense[0]);
    int k, i, j, m, n = 0;

    for (k = 0; k < FFE_NLEAF; k++) {
        const char *nm = ffe_leaf[k].name;
        FfeLeaf3 f = ffe_leaf[k].fn;
        switch (ffe_leaf[k].arity) {
        case 0:
            leaf_emit(nm, f, 0, 0, 0, 0, tsv); n++;
            break;
        case 1:
            for (i = 0; i < ffe_nvec; i++) {
                leaf_emit(nm, f, ffe_vec[i], 0, 0, 1, tsv); n++;
            }
            break;
        case 2:
            for (i = 0; i < ffe_nvec; i++)
                for (j = 0; j < ffe_nvec; j++) {
                    leaf_emit(nm, f, ffe_vec[i], ffe_vec[j], 0, 1, tsv); n++;
                }
            break;
        default:
            for (i = 0; i < ffe_nvec; i++) {
                leaf_emit(nm, f, ffe_vec[i], ffe_vec[(i + 1) % ffe_nvec],
                          ffe_vec[(i + 2) % ffe_nvec], 1, tsv);
                n++;
            }
            for (i = 0; i < nd; i++)
                for (j = 0; j < nd; j++)
                    for (m = 0; m < nd; m++) {
                        leaf_emit(nm, f, dense[i], dense[j], dense[m], 1, tsv);
                        n++;
                    }
            break;
        }
    }
    if (!tsv)
        printf("     %d vectors over %d pure leaves, recorded only\n", n, FFE_NLEAF);
}

#if FFE_PACKED_SHORT_ARGS
/* ---- the packed-short batch ---------------------------------------------- */

/* Mantissas: the extremes of a signed short, the small values, and a couple of
 * powers of two, so that a shift landing one place out is visible. */
static const int ffe_pk_a[] = { 0, 1, -1, 2, -2, 255, -256, 1000, -1000,
                                16384, -16384, 32767, -32768 };
/* Exponents inside every one of the four safe ranges above. */
static const int ffe_pk_b[]    = { 0, 1, 2, 3, 7, 8, 15, 16, 17, 30, 31 };
/* Past the guard, where the routine returns a saturated value without shifting
 * -- legal for the three that HAVE a guard, and not for 1492, which has none. */
static const int ffe_pk_bhi[]  = { 32, 33, 40, 100 };
/* Negative exponents, legal only where the count is 15-b and so stays <= 31. */
static const int ffe_pk_blo[]  = { -1, -8, -16 };

#define FFE_NPKA   ((int)(sizeof ffe_pk_a   / sizeof ffe_pk_a[0]))
#define FFE_NPKB   ((int)(sizeof ffe_pk_b   / sizeof ffe_pk_b[0]))
#define FFE_NPKBHI ((int)(sizeof ffe_pk_bhi / sizeof ffe_pk_bhi[0]))
#define FFE_NPKBLO ((int)(sizeof ffe_pk_blo / sizeof ffe_pk_blo[0]))

static void pk_emit(const char *name, int (*fn)(FfePacked), int a, int b, int tsv)
{
    FfePacked p;
    int r;
    p.a = (short)a;
    p.b = (short)b;
    r = fn(p);
    if (tsv) printf("%s\t%d\t%d\t%d\n", name, a, b, r);
}

static void pk_one(const char *name, int (*fn)(FfePacked), int hi, int lo, int tsv)
{
    int i, j;
    for (i = 0; i < FFE_NPKA; i++) {
        for (j = 0; j < FFE_NPKB; j++)
            pk_emit(name, fn, ffe_pk_a[i], ffe_pk_b[j], tsv);
        if (hi)
            for (j = 0; j < FFE_NPKBHI; j++)
                pk_emit(name, fn, ffe_pk_a[i], ffe_pk_bhi[j], tsv);
        if (lo)
            for (j = 0; j < FFE_NPKBLO; j++)
                pk_emit(name, fn, ffe_pk_a[i], ffe_pk_blo[j], tsv);
    }
}

static void pk_cases(int tsv)
{
    pk_one("FUNC_001491", FUNC_001491, 1, 0, tsv);   /* guarded above, not below */
    pk_one("FUNC_001492", FUNC_001492, 0, 0, tsv);   /* no guard either way      */
    pk_one("FUNC_001493", FUNC_001493, 1, 1, tsv);
    pk_one("FUNC_001494", FUNC_001494, 1, 1, tsv);
    if (!tsv)
        printf("     %d vectors over 1491 1492 1493 1494, recorded only\n",
               FFE_NPKA * (4 * FFE_NPKB + 3 * FFE_NPKBHI + 2 * FFE_NPKBLO));
}
#endif /* FFE_PACKED_SHORT_ARGS */

/* ---- FUNC_001461_GuiAddHotRect2 ------------------------------------------ */

#define HR_ENTRIES 129            /* 2580 bytes / 20, ffedat.asm:23317..23640 */
#define HR_WORDS   (HR_ENTRIES * 5)

/* The table's initial state, read straight off ffedat.asm:23318: the first
 * dword is 0xFFFFFFFF and every byte after it is zero. Restoring it before a
 * sequence is what makes each sequence independent of the one before. */
static void hr_reset(void)
{
    int i;
    for (i = 0; i < HR_WORDS; i++) DATA_007683[i] = 0;
    DATA_007683[0] = -1;
}

/* The whole live prefix of the table, not just the entry that was added: an
 * off-by-one in the 20-byte walk, or a sentinel written into the wrong slot,
 * moves something in here even when the new entry itself looks right. */
static void hr_emit(int x, int y, int w, int h, int id, int tsv)
{
    int n = 0, i, j;

    FUNC_001461(x, y, w, h, id);
    while (n < HR_ENTRIES && DATA_007683[n * 5] != -1) n++;

    if (tsv) {
        printf("FUNC_001461\t%d\t%d\t%d\t%d\t%d\t%d", x, y, w, h, id, n);
        for (i = 0; i < n; i++)
            for (j = 0; j < 5; j++)
                printf("\t%d", DATA_007683[i * 5 + j]);
        printf("\n");
    }
}

/* The live prefix of the table, as its own record. Shared by the three hot-rect
 * functions so their records have one shape. */
static void hr_report(const char *name, int a, int b, int tsv)
{
    int n = 0, i, j;
    while (n < HR_ENTRIES && DATA_007683[n * 5] != -1) n++;
    if (!tsv) return;
    printf("%s\t%d\t%d\t%d", name, a, b, n);
    for (i = 0; i < n; i++)
        for (j = 0; j < 5; j++)
            printf("\t%d", DATA_007683[i * 5 + j]);
    printf("\n");
}

static const int ffe_hr[][5] = {
    {   0,   0,   0,   0,   0 },   /* everything zero: the degenerate rect    */
    {  10,  20,  30,  40,   1 },
    {   0,   0, 320, 200,   2 },   /* the whole screen                        */
    { 319, 199,   1,   1,   3 },   /* the far corner                          */
    {  -5, -10,  20,  30,   4 },   /* negative origin -- it does not clip     */
    {  10,  20,  -5, -10,   5 },   /* negative extent: x1 < x0                */
    {   1,   2,   3,   4,  -1 },   /* an id of -1, the same value the SENTINEL
                                    * uses -- but in the id field, where it is
                                    * just data. If the walk ever tested the
                                    * wrong offset this is what would catch it */
    { 100, 100,   0,   0,   7 },
    { 0x7fffffff, 0, 1, 0, 8 },    /* x + w wraps: plain 32-bit arithmetic on
                                    * both machines, and it must wrap the same */
    { 0, 0x7fffffff, 0, 1, 9 },
    { -1, -1, -1, -1, 10 },
    {  50,  60,  70,  80,  11 },
};

#define FFE_NHR ((int)(sizeof ffe_hr / sizeof ffe_hr[0]))

static void hr_cases(int tsv)
{
    int i;

    /* One append into an empty table, twelve times: always the FIRST slot, so
     * the walk terminates immediately and only the store is exercised. */
    for (i = 0; i < FFE_NHR; i++) {
        hr_reset();
        hr_emit(ffe_hr[i][0], ffe_hr[i][1], ffe_hr[i][2], ffe_hr[i][3],
                ffe_hr[i][4], tsv);
    }

    /* Then all twelve into ONE table without resetting, so every call walks
     * further than the last and the table grows to twelve entries. This is the
     * half that actually tests the loop. */
    hr_reset();
    for (i = 0; i < FFE_NHR; i++)
        hr_emit(ffe_hr[i][0], ffe_hr[i][1], ffe_hr[i][2], ffe_hr[i][3],
                ffe_hr[i][4], tsv);

    /* ---- FUNC_001440_GuiAddHotAreas: whole lists at a time --------------- */
    for (i = 0; i < FFE_NHR; i++) {
        int src[4 * 5 + 5], k, m, e = (i % 3) + 1;
        hr_reset();
        for (k = 0; k < e; k++)
            for (m = 0; m < 5; m++)
                src[k * 5 + m] = ffe_hr[(i + k) % FFE_NHR][m];
        src[e * 5] = -1;                       /* the list's own terminator */
        FUNC_001440(src);
        hr_report("FUNC_001440", i, e, tsv);
    }

    /* ---- FUNC_001443_GuiRemoveHotArea: the overlapping shift ------------- */
    hr_reset();
    for (i = 0; i < FFE_NHR; i++)
        FUNC_001461(ffe_hr[i][0], ffe_hr[i][1], ffe_hr[i][2], ffe_hr[i][3],
                    ffe_hr[i][4]);
    for (i = 0; i < FFE_NHR; i++) {
        FUNC_001443(ffe_hr[i][4]);
        hr_report("FUNC_001443", i, ffe_hr[i][4], tsv);
    }

    /* Leave the game's own table as it was found. */
    hr_reset();
    if (!tsv)
        printf("     %d vectors over 001461 001440 001443, recorded only\n",
               4 * FFE_NHR);
}

#if FFE_SUBWORD_STACK_FIXED
/* ---- FUNC_001612_DrawCircle ----------------------------------------------
 *
 * Same guarded buffer and same per-row record as the line and the triangle, and
 * g_trbuf is REUSED rather than a fourth 66 KB canvas allocated: every emit
 * memsets it first, so the three tests cannot see each other. */
static void cr_emit(int cx, int cy, int size, int col, int tsv)
{
    unsigned char *pix = g_trbuf + LN_GUARD * LN_W;
    FfePoint c;
    int r, i, nrows = 0;

    memset(g_trbuf, LN_FILL, LN_N);
    DATA_007773.pixels = pix;
    c.x = (short)cx;
    c.y = (short)cy;
    FUNC_001612(&c, size, col);

    for (r = 0; r < LN_TOTAL; r++) {
        const unsigned char *row = g_trbuf + r * LN_W;
        for (i = 0; i < LN_W; i++)
            if (row[i] != LN_FILL) { nrows++; break; }
    }

    if (tsv) {
        printf("FUNC_001612\t%d\t%d\t%d\t%d\t%d", cx, cy, size, col, nrows);
        for (r = 0; r < LN_TOTAL; r++) {
            const unsigned char *row = g_trbuf + r * LN_W;
            int first = -1, last = -1, n = 0, sum = 0;
            for (i = 0; i < LN_W; i++)
                if (row[i] != LN_FILL) {
                    if (first < 0) first = i;
                    last = i; n++; sum += row[i];
                }
            if (first < 0) continue;
            printf("\t%d\t%d\t%d\t%d\t%d",
                   r - LN_GUARD, first, last - first + 1, n, sum);
        }
        printf("\n");
    }
}

static void cr_cases(int tsv)
{
    static const int cr[][3] = {
        { 160,  79,   0 },   /* every size 0..10, so the size-7 quirk at        */
        { 160,  79,   1 },   /* ffe.asm:96471-73 (`cmp edx,5` after `add -2`,   */
        { 160,  79,   2 },   /* i.e. size 7 -> 8) cannot be translated away by  */
        { 160,  79,   3 },   /* accident                                        */
        { 160,  79,   4 },
        { 160,  79,   5 },
        { 160,  79,   6 },
        { 160,  79,   7 },
        { 160,  79,   8 },
        { 160,  79,   9 },
        { 160,  79,  10 },
        { 160,  79,  16 },
        { 160,  79,  20 },
        { 160,  79,  40 },
        { 160,  79, 100 },
        { 160,  79,  -1 },   /* negative diameters: recorded, not reasoned about */
        { 160,  79,  -4 },
        { 160,   5,  40 },   /* the top clip: rows above 0 are skipped in the    */
        { 160,   0,  40 },   /* span table and the start row clamped to 0        */
        { 160, -10,  40 },
        { 160, 150,  40 },   /* the bottom clip: `cmp eax,0x9e`, 158 lines       */
        { 160, 158,  40 },
        { 160, 170,  40 },
        {  10,  20,   8 },   /* small circles away from the centre               */
        { 310,  20,   8 },
        {   5,  30,   4 },
        {   2,  60,  20 },   /* deliberately off the LEFT edge: DrawCircle does
                              * not clip x, so this records a real overrun of the
                              * original's own making. It stays inside the
                              * allocation, and the guard rows would catch it if
                              * the translation went further than the x86 did.  */
    };
    const int n = (int)(sizeof cr / sizeof cr[0]);
    int i, c = 1;

    for (i = 0; i < n; i++)
        cr_emit(cr[i][0], cr[i][1], cr[i][2], 1 + (c++ & 0xfe), tsv);

    cr_emit(160, 79, 20, 0, tsv);
    cr_emit(160, 79, 20, 255, tsv);

    if (!tsv)
        printf("     %d vectors over FUNC_001612, recorded only\n", n + 2);
}
#endif /* FFE_SUBWORD_STACK_FIXED */

/* ---- the alignment-padding batch ---------------------------------------- */

static void pad_cases(int tsv)
{
    /* The globals are set from here, so the record carries them and the two
     * machines cannot silently disagree about the starting state. */
    static const int ids[] = { 0, 1, -1, 2, 7, 0x7fffffff, (int)0x80000000 };
    static const int vals[][2] = { { 0, 0 }, { 11, 22 }, { -1, -2 },
                                   { 0x7fffffff, (int)0x80000000 } };
    /* The two boundaries are 0x76 and 0x85, both UNSIGNED, so the interesting
     * values sit either side of each and at the ends of a 16-bit range. */
    static const int ws[] = { 0, 1, 0x75, 0x76, 0x77, 0x84, 0x85, 0x86,
                              0xff, 0x100, 0x7fff, 0x8000, 0xffff };
    const int nid = (int)(sizeof ids / sizeof ids[0]);
    const int nval = (int)(sizeof vals / sizeof vals[0]);
    const int nw = (int)(sizeof ws / sizeof ws[0]);
    int i, j, n = 0;

    for (j = 0; j < nval; j++) {
        DATA_008861 = ids[j % nid];
        DATA_008890 = vals[j][0];
        DATA_009105 = vals[j][1];
        for (i = 0; i < nid; i++) {
            if (tsv)
                printf("FUNC_000402\t%d\t%d\t%d\t%d\n", ids[i], DATA_008861,
                       DATA_008890, FUNC_000402(ids[i]));
            if (tsv)
                printf("FUNC_000427\t%d\t%d\t%d\t%d\n", ids[i], DATA_008861,
                       DATA_009105, FUNC_000427(ids[i]));
            n += 2;
        }
    }
    DATA_008861 = DATA_008890 = DATA_009105 = 0;

    for (i = 0; i < nw; i++) {
        FfeThing t;
        memset(&t, 0, sizeof t);
        t.v = (unsigned short)ws[i];
        if (tsv)
            printf("FUNC_000030\t%d\t%d\n", ws[i], FUNC_000030(&t));
        n++;
    }

    if (!tsv)
        printf("     %d vectors over 000402 000427 000030, recorded only\n", n);
}

/* ---- the operand-width batch --------------------------------------------- */

static void wid_cases(int tsv)
{
    /* 1340 is driven by the very loop FUNC_001341 uses, over the same two
     * tables, so a disagreement between the twins would stand out at once. */
    int i, j, n = 0;

    for (i = 0; i < ffe_ni64; i++)
        for (j = 0; j < ffe_nshift; j++) {
            int out[2];
            out[0] = ffe_i64[i][0];
            out[1] = ffe_i64[i][1];
            FUNC_001340(out, ffe_shift[j]);
            if (tsv)
                printf("FUNC_001340\t%d\t%d\t%d\t%d\t%d\n",
                       ffe_i64[i][0], ffe_i64[i][1], ffe_shift[j], out[0], out[1]);
            n++;
        }

    /* 422 reads ten bytes at a+0xd6 and one byte at b+0x12, and counts how many
     * of the ten equal it. The patterns are chosen so that every case has a
     * known answer: all ten, none, each end of the run, and alternating. */
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
        unsigned char a[0xd6 + 16], b[0x13 + 4];
        for (i = 0; i < np; i++)
            for (j = 0; j < nk; j++) {
                memset(a, 0xAA, sizeof a);
                memset(b, 0xAA, sizeof b);
                memcpy(a + 0xd6, pats[i], 10);
                b[0x12] = (unsigned char)keys[j];
                if (tsv)
                    printf("FUNC_000422\t%d\t%d\t%d\n", i, keys[j],
                           FUNC_000422(a, b));
                n++;
            }
    }

    if (!tsv)
        printf("     %d vectors over 001340 000422, recorded only\n", n);
}

/* ---- the indexed-addressing batch --------------------------------------- */

#define IX_CTX (0x9bd4 + 64 * 4 + 64)      /* room for idx 0..63 at scale 4 */

static void ix_cases(int tsv)
{
    static const int idxs[] = { 0, 1, 2, 3, 7, 8, 15, 16, 31, 63 };
    static const int vals[] = { 0, 1, -1, 0x12345678, (int)0xdeadbeef,
                                0x7fffffff, (int)0x80000000 };
    const int ni = (int)(sizeof idxs / sizeof idxs[0]);
    const int nv = (int)(sizeof vals / sizeof vals[0]);
    static unsigned char ctx[IX_CTX];
    int i, j, n = 0;

    /* 001565: xor into ctx[0x9bd4 + idx*4]. The buffer is recorded as INTS at
     * the touched offset, not as bytes, because that is the width the routine
     * writes -- printing bytes would compare two byte orders and prove nothing.
     * It is cleared before each call so the xor's input is known. */
    for (i = 0; i < ni; i++)
        for (j = 0; j < nv; j++) {
            int *slot;
            memset(ctx, 0, sizeof ctx);
            slot = (int *)(ctx + 0x9bd4 + idxs[i] * 4);
            *slot = 0x0f0f0f0f;
            FUNC_001565(idxs[i], vals[j], ctx);
            if (tsv)
                printf("FUNC_001565\t%d\t%d\t%d\n", idxs[i], vals[j], *slot);
            n++;
        }

    /* 001558: appends val at obj[1 + count] while the count at obj[0] is under
     * ten, and returns obj+0x2c whatever happens. The return is recorded as an
     * OFFSET from the object, never as a pointer -- the two machines have
     * different addresses and the same offset. */
    {
        int obj[32];
        for (j = 0; j < nv; j++) {
            int k;
            memset(obj, 0, sizeof obj);
            for (k = 0; k < 12; k++) {          /* twelve appends, cap is ten */
                void *r = FUNC_001558(obj, vals[(j + k) % nv]);
                if (tsv) {
                    int m;
                    printf("FUNC_001558\t%d\t%d\t%d\t%d",
                           j, k, (int)((unsigned char *)r - (unsigned char *)obj),
                           obj[0]);
                    for (m = 1; m <= 11; m++) printf("\t%d", obj[m]);
                    printf("\n");
                }
                n++;
            }
        }
    }

    /* 000093 / 000094: the suppressed-base form, written then read back. */
    for (i = 0; i < 100; i++) DATA_008568[i] = 0;
    for (i = 0; i < ni; i++)
        for (j = 0; j < nv; j++) {
            FUNC_000093(idxs[i], vals[j]);
            if (tsv)
                printf("FUNC_000093\t%d\t%d\t%d\n", idxs[i], vals[j],
                       DATA_008568[idxs[i]]);
            if (tsv)
                printf("FUNC_000094\t%d\t%d\n", idxs[i], FUNC_000094(idxs[i]));
            n += 2;
        }
    for (i = 0; i < 100; i++) DATA_008568[i] = 0;

    if (!tsv)
        printf("     %d vectors over 001565 001558 000093 000094, recorded only\n", n);
}

/* ---- the byte-MOVSX and truncating-IMUL batch ---------------------------- */

static void mi_cases(int tsv)
{
    /* The byte at [p+0xca] is sign-extended, so both halves of the signed byte
     * range matter and so does the boundary at 0x80. */
    static const int bytes[] = { 0, 1, 127, 128, 129, 200, 255, 0x7f, 0x80 };
    static const int wants[] = { 0, 1, -1, 127, -128, 128, 200, -56, 255 };
    const int nb = (int)(sizeof bytes / sizeof bytes[0]);
    const int nw = (int)(sizeof wants / sizeof wants[0]);
    unsigned char buf[0x100];
    int i, j, n = 0;

    for (i = 0; i < nb; i++)
        for (j = 0; j < nw; j++) {
            memset(buf, 0xAA, sizeof buf);
            buf[0xca] = (unsigned char)bytes[i];
            if (tsv)
                printf("FUNC_000657\t%d\t%d\t%d\n", bytes[i], wants[j],
                       FUNC_000657(wants[j], buf));
            n++;
        }

    for (i = 0; i < ffe_nvec; i++) {
        if (tsv)
            printf("FUNC_000024\t%d\t%d\n", ffe_vec[i], FUNC_000024(ffe_vec[i]));
        n++;
    }

    for (i = 0; i < ffe_nvec; i++)
        for (j = 0; j < ffe_nvec; j++) {
            if (tsv)
                printf("FUNC_001515\t%d\t%d\t%d\n", ffe_vec[i], ffe_vec[j],
                       FUNC_001515(ffe_vec[i], ffe_vec[j]));
            n++;
        }

    if (!tsv)
        printf("     %d vectors over 000657 000024 001515, recorded only\n", n);
}

/* ---- the SETcc / XCHG batch ----------------------------------------------
 *
 * 001514's object holds three int64 at 0x3e, 0x46 and 0x4e (low half) with the
 * high halves 4 bytes after each. They are read back with memcpy rather than a
 * cast: 0x3e is even but not a multiple of four, and a cast would be undefined
 * behaviour on the x86 side and an alignment question on the 68k one. memcpy is
 * neither, and each machine still lays its own longword out its own way -- which
 * is right, because the buffer never crosses between them. */
static void sc_cases(int tsv)
{
    static const int bits[] = { 0x00, 0x10, 0x0f, 0xff, 0xef, 0x90, 0x01 };
    /* Values chosen so that the 32-bit add at [obj+0x3e] carries, does not
     * carry, and lands exactly on the boundary -- the carry is what `seta`
     * detects, so a wrong condition shows up in the high half. */
    static const int deltas[] = { 0, 1, -1, 0x7fffffff, (int)0x80000000,
                                  0x40000000, -0x40000000 };
    static const int seeds[] = { 0, 1, -1, 0x7fffffff, (int)0xffffffff,
                                 (int)0x80000000 };
    const int nb = (int)(sizeof bits / sizeof bits[0]);
    const int nd = (int)(sizeof deltas / sizeof deltas[0]);
    const int ns = (int)(sizeof seeds / sizeof seeds[0]);
    static const int LO[3] = { 0x3e, 0x46, 0x4e };
    unsigned char obj[0x60];
    int i, j, k, n = 0;

    for (i = 0; i < nb; i++) {
        memset(obj, 0, sizeof obj);
        obj[0x14c % sizeof obj] = 0;            /* keep the index in range */
        {
            unsigned char big[0x160];
            memset(big, 0, sizeof big);
            big[0x14c] = (unsigned char)bits[i];
            if (tsv)
                printf("FUNC_000028\t%d\t%d\n", bits[i], FUNC_000028(big));
            n++;
        }
    }

    for (i = 0; i < ns; i++)
        for (j = 0; j < nd; j++) {
            int d3[3], v;
            memset(obj, 0, sizeof obj);
            for (k = 0; k < 3; k++) {
                v = seeds[(i + k) % ns];
                memcpy(obj + LO[k], &v, 4);
                v = 0;
                memcpy(obj + LO[k] + 4, &v, 4);
            }
            for (k = 0; k < 3; k++) d3[k] = deltas[(j + k) % nd];
            FUNC_001514(obj, d3);
            if (tsv) {
                printf("FUNC_001514\t%d\t%d", i, j);
                for (k = 0; k < 3; k++) {
                    int lo, hi;
                    memcpy(&lo, obj + LO[k], 4);
                    memcpy(&hi, obj + LO[k] + 4, 4);
                    printf("\t%d\t%d", lo, hi);
                }
                printf("\n");
            }
            n++;
        }

    if (!tsv)
        printf("     %d vectors over 000028 001514, recorded only\n", n);
}

/* ---- FUNC_001607_DrawQuad: the indirect-call family ---------------------
 *
 * Same guarded buffer and record shape as tr_emit(): name, the eight
 * coordinates, col, the touched-row count, then (row, first, len, n, sum)
 * per touched row. The buffer is g_trbuf again -- memset every case, so
 * sharing it with the triangle costs nothing.
 *
 * Colour stays <= 255 for the same reason the DrawHLine vectors stop there:
 * the span fill replicates the colour byte through a dword store, and for
 * col > 255 the two machines legitimately store different byte sequences.
 * That is the data-typing question, not this test's. */
static void qd_emit(int x0, int y0, int x1, int y1, int x2, int y2,
                    int x3, int y3, int col, int tsv)
{
    unsigned char *pix = g_trbuf + LN_GUARD * LN_W;
    FfePoint a, b, c, d;
    int r, i, nrows = 0, npix = 0, bad = 0;

    memset(g_trbuf, LN_FILL, LN_N);
    DATA_007773.pixels = pix;
    a.x = (short)x0; a.y = (short)y0;
    b.x = (short)x1; b.y = (short)y1;
    c.x = (short)x2; c.y = (short)y2;
    d.x = (short)x3; d.y = (short)y3;
    FUNC_001607(&a, &b, &c, &d, col);

    for (r = 0; r < LN_TOTAL; r++) {
        const unsigned char *row = g_trbuf + r * LN_W;
        for (i = 0; i < LN_W; i++)
            if (row[i] != LN_FILL) { nrows++; break; }
    }

    if (tsv) {
        printf("FUNC_001607\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d",
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
            printf("\t%d\t%d\t%d\t%d\t%d",
                   r - LN_GUARD, first, last - first + 1, n, sum);
        }
        printf("\n");
        return;
    }

    for (r = 0; r < LN_TOTAL; r++) {
        const unsigned char *row = g_trbuf + r * LN_W;
        for (i = 0; i < LN_W; i++)
            if (row[i] != LN_FILL) {
                npix++;
                if (row[i] != (unsigned char)col) bad = 1;
                if (r - LN_GUARD < 0 || r - LN_GUARD > 157) bad = 1;
            }
    }
    if (bad) g_fail++;
    printf("%-4s 1607 (%4d,%4d)(%4d,%4d)(%4d,%4d)(%4d,%4d) col=%3d  "
           "rows=%4d pixels=%6d\n",
           bad ? "FAIL" : "ok", x0, y0, x1, y1, x2, y2, x3, y3, col,
           nrows, npix);
}

static void qd_cases(int tsv)
{
    static const int qd[][8] = {
        /* one convex quad in all four rotations ...                       */
        {  40,  20, 120,  30, 130, 100,  30,  90 },
        { 120,  30, 130, 100,  30,  90,  40,  20 },
        { 130, 100,  30,  90,  40,  20, 120,  30 },
        {  30,  90,  40,  20, 120,  30, 130, 100 },
        /* ... and the reversed winding, two starts                        */
        {  30,  90, 130, 100, 120,  30,  40,  20 },
        {  40,  20,  30,  90, 130, 100, 120,  30 },
        /* axis-aligned rectangle, both windings: flat top AND bottom      */
        {  20,  20, 100,  20, 100,  80,  20,  80 },
        { 100,  20,  20,  20,  20,  80, 100,  80 },
        /* a triangle wearing a quad's clothes: two vertices coincide      */
        {  50,  10, 100,  60, 100,  60,  20,  90 },
        /* all four on one scanline / in one column / on one point         */
        {  20,  30,  60,  30, 100,  30, 140,  30 },
        {  20,  20,  20,  60,  20, 100,  20, 140 },
        {  50,  50,  50,  50,  50,  50,  50,  50 },
        /* slivers, tall and wide                                          */
        {  10,  10,  11, 100,  12, 100,  13,  10 },
        {  10,  10, 300,  11, 300,  12,  10,  13 },
        /* straddles the bottom clip edge                                  */
        {  20, 150, 100, 150, 100, 170,  20, 170 },
        /* apex above the buffer: rows must be dropped, not drawn negative */
        {  20, -50, 100, -40, 120,  60,  10,  80 },
        /* entirely above / entirely below                                 */
        {  20, -80, 100, -70,  90, -20,  10, -10 },
        {  20, 200, 100, 210,  90, 280,  10, 260 },
        /* off the left edge / the right / both at once                    */
        { -60,  10,  60,  20,  50,  70, -80,  60 },
        { 260,  10, 380,  20, 370,  70, 250,  60 },
        {-100,  10, 420,  20, 400, 100, -90,  90 },
        /* exactly the writable corners                                    */
        {   0,   0, 319,   0, 319, 157,   0, 157 },
        /* non-convex bowtie: recorded, whatever the original does with it */
        {  20,  20, 120,  90, 120,  20,  20,  90 },
    };
    const int n = (int)(sizeof qd / sizeof qd[0]);
    int i, c = 1;

    for (i = 0; i < n; i++)
        qd_emit(qd[i][0], qd[i][1], qd[i][2], qd[i][3], qd[i][4],
                qd[i][5], qd[i][6], qd[i][7], 1 + (c++ & 0xfe), tsv);

    qd_emit(40, 20, 120, 30, 130, 100, 30, 90, 0, tsv);
    qd_emit(40, 20, 120, 30, 130, 100, 30, 90, 255, tsv);
    qd_emit(40, 20, 120, 30, 130, 100, 30, 90, 1, tsv);
}

/* ---- the label-address / MOVZX batch ------------------------------------
 *
 * Eight rules landed at once and each is pinned by a function below:
 *
 *   FUNC_000178  `mov edi,DATA_008661`  -> LEA into an address register
 *   FUNC_001785  `mov dword [DATA_007832],DATA_009255` -> address to memory
 *   FUNC_000038  `add ebx,DATA_008561`  -> label address as ADDI source
 *   FUNC_000412  `movzx ecx,byte [...]` -> MOVEQ #0 + MOVE.B, paired against
 *                a movsx of the same value so 0x80..0xFF split the two
 *   FUNC_000417  `movzx eax,word [...]` -> MOVEQ #0 + MOVE.W
 *   FUNC_000896  `movzx eax,ax`         -> the self-extend, AND.L #65535
 *   FUNC_001597  `movzx eax,byte [eax*2+DATA_007751]` -> zero-extend off a
 *                scaled-index-plus-symbol EA
 *
 * DATA content rule: every table slot a vector reads is WRITTEN by this
 * harness first, on both machines alike, so nothing here depends on the
 * static-data conversion that does not exist yet. Pointers are printed as
 * OFFSETS from an exported label, never as values. */
extern int  FUNC_000038(int key)            __asm__("FUNC_000038");
extern void FUNC_000178(const void *src)
                             __asm__("FUNC_000178_CdrFileSetLastLoaded");
extern void FUNC_001785(int arg)            __asm__("FUNC_001785");
extern int  FUNC_000412(const void *a, const void *b) __asm__("FUNC_000412");
extern int  FUNC_000417(const void *p)      __asm__("FUNC_000417");
extern int  FUNC_000896(int key)            __asm__("FUNC_000896");
extern int  FUNC_001597(int ch)             __asm__("FUNC_001597_CharAdvanceWidth");

extern unsigned char DATA_008561[] __asm__("DATA_008561");
extern unsigned char DATA_008661[] __asm__("DATA_008661");
extern unsigned char DATA_007129[] __asm__("DATA_007129");
extern unsigned char DATA_007751[] __asm__("DATA_007751");
extern int           DATA_007830   __asm__("DATA_007830");
extern int           DATA_007831   __asm__("DATA_007831");
extern void         *DATA_007832   __asm__("DATA_007832");
extern int           DATA_009255   __asm__("DATA_009255");

static void la_cases(int tsv)
{
    int i, r;

    /* FUNC_000038: a state machine record at DATA_008561 + 36*(key&0xffff).
     * Field +8 is held at 0 in every vector, which skips the one path that
     * calls out (and, with it, the aliased `inc word [+6]` -- that word
     * overlays the +4 dword, and which byte it lands on is a DATA-TYPING
     * question this test refuses to depend on). Fields printed never
     * overlap. */
    {
        static int aux[4];
        /* stored is what goes in the record's +4 field; key is what the call
         * passes. They differ in exactly one vector, the mismatch one. The
         * ptrfield ([+0x1c]->+0xc) stays >= 0 in every vector: negative takes
         * the `inc word [+6]` branch, and that word overlays the +4 dword --
         * which byte it lands on is a DATA-TYPING question this test refuses
         * to depend on. */
        struct { int key, stored, state, ptrfield; } v[] = {
            { 0x00010000, 0x00010000, 3, 5 },  /* idx 0, match, state 1..4  */
            { 0x00020001, 0x00020001, 4, 7 },  /* idx 1                     */
            { 0x00050000, 0x00060000, 3, 5 },  /* key mismatch: early ret   */
            { 0x00010000, 0x00010000, 0, 5 },  /* state 0: out of 1..4      */
            { 0x00010000, 0x00010000, 5, 5 },  /* state 5: out of 1..4      */
        };
        for (i = 0; i < (int)(sizeof v / sizeof v[0]); i++) {
            unsigned char *rec = DATA_008561 + 36 * (v[i].key & 0xffff);
            int state_out;
            memset(rec, 0, 36);
            memcpy(rec + 0, &v[i].state, 4);
            memcpy(rec + 4, &v[i].stored, 4);
            aux[3] = v[i].ptrfield;               /* [+0x1c] -> aux, +0xc */
            { void *p = aux; memcpy(rec + 0x1c, &p, 4); }
            FUNC_000038(v[i].key);
            memcpy(&state_out, rec + 0, 4);
            if (tsv)
                printf("FUNC_000038\t%d\t%d\t%d\t%d\n",
                       v[i].key, v[i].state, v[i].ptrfield, state_out);
        }
    }

    /* FUNC_000178: rep movsd of 6 ints into DATA_008661. */
    for (r = 0; r < 3; r++) {
        int src[6], back[6];
        for (i = 0; i < 6; i++) src[i] = (r + 1) * 0x01010101 + i * 7 - 3;
        memset(DATA_008661, 0xa5, 24);
        FUNC_000178(src);
        memcpy(back, DATA_008661, 24);
        if (tsv) {
            printf("FUNC_000178\t%d", r);
            for (i = 0; i < 6; i++) printf("\t%d", back[i]);
            printf("\n");
        }
    }

    /* FUNC_001785: two ints zeroed, one int stored, one ADDRESS stored --
     * printed as the offset from DATA_009255, which a correct translation
     * makes 0 on both machines. */
    {
        static const int args[] = { 12345, -1 };
        for (i = 0; i < 2; i++) {
            int off;
            DATA_009255 = 0x5a5a5a5a;
            DATA_007831 = 0x3c3c3c3c;
            DATA_007830 = 0;
            DATA_007832 = 0;
            FUNC_001785(args[i]);
            off = (int)((char *)DATA_007832 - (char *)&DATA_009255);
            if (tsv)
                printf("FUNC_001785\t%d\t%d\t%d\t%d\t%d\n", args[i],
                       DATA_009255, DATA_007831, DATA_007830, off);
        }
    }

    /* FUNC_000412: movSx byte [a+0xd0] == movZx byte [b+0x12] ? 1 : 0.
     * 0x80 and 0xff are the vectors that split a wrong extension: sign-
     * extended they are negative, zero-extended they are 128 and 255. */
    {
        static const int pairs[][2] = {
            { 0x05, 0x05 }, { 0x00, 0x00 }, { 0x7f, 0x7f },
            { 0xff, 0xff },       /* movsx -1 vs movzx 255: NOT equal      */
            { 0x80, 0x80 },       /* movsx -128 vs movzx 128: NOT equal    */
            { 0x01, 0x02 },
        };
        unsigned char a[0xd8], b[0x18];
        memset(a, 0, sizeof a); memset(b, 0, sizeof b);
        for (i = 0; i < (int)(sizeof pairs / sizeof pairs[0]); i++) {
            a[0xd0] = (unsigned char)pairs[i][0];
            b[0x12] = (unsigned char)pairs[i][1];
            r = FUNC_000412(a, b);
            if (tsv)
                printf("FUNC_000412\t%d\t%d\t%d\n",
                       pairs[i][0], pairs[i][1], r);
        }
    }

    /* FUNC_000417: (zero-extended word at +0xe2) >> 6. 0x8000 and 0xffff
     * are the vectors a sign extension would send through the dead
     * negative-adjust arm. */
    {
        static const int words[] = { 0, 0x3f, 0x40, 0x1234, 0x8000, 0xffff };
        unsigned char p[0xe8];
        memset(p, 0, sizeof p);
        for (i = 0; i < (int)(sizeof words / sizeof words[0]); i++) {
            unsigned short w = (unsigned short)words[i];
            memcpy(p + 0xe2, &w, 2);
            r = FUNC_000417(p);
            if (tsv) printf("FUNC_000417\t%d\t%d\n", words[i], r);
        }
    }

    /* FUNC_000896: `movzx eax,ax` -- the self-extend. The 0x7fff0040 vector
     * proves the top half is CLEARED, not preserved: it must answer exactly
     * as 0x0040 does. Slots are written as shorts at [idx*4] first. */
    {
        static const int keys[] = { 0x0000, 0x0040, 0x7fff0040, 0x3fc0,
                                    0x2480, 0x4001, (int)0x80004001,
                                    (int)0xffffffff };
        static const int slots[] = { 0, 1, 0xff, 0x92 };
        for (i = 0; i < 4; i++) {
            short w = (short)(0x1111 * slots[i] + 7);
            memcpy(DATA_007129 + slots[i] * 4, &w, 2);
        }
        for (i = 0; i < (int)(sizeof keys / sizeof keys[0]); i++) {
            r = FUNC_000896(keys[i]);
            if (tsv) printf("FUNC_000896\t%d\t%d\n", keys[i], r);
        }
    }

    /* FUNC_001597: char advance width off DATA_007751[(c-0x20)*10], with
     * everything outside 0x20..0x7f (after the add/movsx dance) answered 0.
     * The table is filled by formula so the content is the harness's, not
     * the unconverted game image's. */
    {
        static const int chars[] = { 0x20, 0x41, 0x61, 0x7a, 0x7f,
                                     0x1f, 0x00, 0x80, 0xff, -1 };
        for (i = 0; i < 961; i++)
            DATA_007751[i] = (unsigned char)((i * 7 + 3) & 0x7f);
        for (i = 0; i < (int)(sizeof chars / sizeof chars[0]); i++) {
            r = FUNC_001597(chars[i]);
            if (tsv) printf("FUNC_001597\t%d\t%d\n", chars[i], r);
        }
    }

    if (!tsv)
        printf("     the label-address / movzx batch, recorded only\n");
}

/* ---- the base+symbol batch ----------------------------------------------
 *
 * `mov cl,[edx+DATA_007770]` -- a register plus a label in one operand, 33
 * functions' first refusal. Lowered as the suppressed-base indexed mode with
 * a scale of 1 (LEA label into a0, then (0,a0,d2.l*1)), i.e. the same 68020
 * form the scaled-index vectors already pin down, at a new scale.
 *
 * FUNC_001327_DPal16toPal carries it three times over, and both remaining
 * MOVZX shapes with it: `movzx edx,ax` (register word source) and
 * `movzx eax,byte [eax+DATA_009140]` -- whose destination is the INDEX of
 * its own source EA, the exact hazard the oracle caught in FUNC_001597.
 * The 0x10042 key must answer as 0x42 does: `movzx edx,ax` keeps 16 bits. */
extern int FUNC_001327(int key) __asm__("FUNC_001327_DPal16toPal");

extern unsigned char DATA_007770[] __asm__("DATA_007770");
extern unsigned char DATA_009139[] __asm__("DATA_009139");
extern unsigned char DATA_009140[] __asm__("DATA_009140");

static void bs_cases(int tsv)
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
        r = FUNC_001327(keys[i]);
        if (tsv) printf("FUNC_001327\t%d\t%d\n", keys[i], r);
    }

    if (!tsv)
        printf("     the base+symbol batch, recorded only\n");
}

/* ---- FUNC_001337_Int64Mul64: one-operand MUL --------------------------------
 *
 * void F1337(int64 *out, int64 a, int64 b) -- out = a * b, low 64 bits, built
 * from three unsigned 32x32->64 MULs exactly the schoolbook way. The unsigned
 * twin of the IMUL the quad family carries; MULU.L <ea>,Dh:Dl. The vectors
 * slide over ffe_i64[] like every other 64-bit family member, so the two
 * machines cannot disagree about the inputs without disagreeing about
 * records that have been green for a day. */
extern void FUNC_001337(int *out, int alo, int ahi, int blo, int bhi)
                                       __asm__("FUNC_001337_Int64Mul64");

static void mu_cases(int tsv)
{
    int i, j, out[2];

    for (i = 0; i < ffe_ni64; i++)
        for (j = 0; j < ffe_ni64; j++) {
            out[0] = out[1] = 0;
            FUNC_001337(out, ffe_i64[i][0], ffe_i64[i][1],
                        ffe_i64[j][0], ffe_i64[j][1]);
            if (tsv)
                printf("FUNC_001337\t%d\t%d\t%d\t%d\t%d\t%d\n",
                       ffe_i64[i][0], ffe_i64[i][1],
                       ffe_i64[j][0], ffe_i64[j][1], out[0], out[1]);
        }

    if (!tsv)
        printf("     %d vectors over FUNC_001337, recorded only\n",
               ffe_ni64 * ffe_ni64);
}

/* ---- FUNC_000211: REPNE SCASB, the strlen idiom -------------------------
 *
 * void F211(const char *name): strcpy(DATA_008919, name), then
 * strcpy(DATA_008920, DATA_008919) -- both lengths measured by
 * `or ecx,-1 / repne scasb / not ecx` and both copies done by the
 * rep movsd + rep movsb pair that is already green. The record dumps BOTH
 * buffers in full (22 + 26 bytes as numbers), over a 0xEE prefill, so the
 * copy length is pinned exactly: one byte too many or too few shows up as
 * a 0xEE in the wrong place on one machine and not the other. */
extern void FUNC_000211(const char *name)
                        __asm__("FUNC_000211_SysMenuUpdateCmmdrName");
extern unsigned char DATA_008919[] __asm__("DATA_008919");
extern unsigned char DATA_008920[] __asm__("DATA_008920");

static void rp_cases(int tsv)
{
    static const char *const names[] = {
        "", "A", "Jameson", "ABCDEFGHIJKLMNOPQRST" /* 20 chars, fits 22 */
    };
    int i, k;

    for (i = 0; i < (int)(sizeof names / sizeof names[0]); i++) {
        memset(DATA_008919, 0xee, 22);
        memset(DATA_008920, 0xee, 26);
        FUNC_000211(names[i]);
        if (tsv) {
            printf("FUNC_000211\t%d", i);
            for (k = 0; k < 22; k++) printf("\t%d", DATA_008919[k]);
            for (k = 0; k < 26; k++) printf("\t%d", DATA_008920[k]);
            printf("\n");
        }
    }

    if (!tsv)
        printf("     %d vectors over FUNC_000211, recorded only\n",
               (int)(sizeof names / sizeof names[0]));
}

/* ---- the address-register AND/OR + MOVZX batch --------------------------
 *
 * FUNC_000504/000505 are rotate-right/rotate-left through a pointer:
 * `or esi,ebx` with esi in a2 -- AND/OR have no An form at all, so the
 * scratch-d7 detour is the ONLY spelling and these two carry it, along with
 * variable shifts whose destination also lives in an address register.
 *
 * FUNC_000781 carries `movzx esi,byte [eax+0xfe]` (zero-extend into an
 * address register) compared against `movsx ebx,cl`. The 0xFF/0xFF and
 * 0x80/0x80 vectors are the sentinels: on the original the two extensions
 * DIFFER so the compare is not-equal and nothing happens; a translation
 * that sign-extends the movzx (or zero-extends the movsx) makes them equal
 * and walks into a call whose 68k side is a LOGGING STUB -- the record
 * count breaks and the compare fails loudly. The genuinely-equal path
 * always calls out, so it is deliberately not exercised. */
extern int  FUNC_000504(unsigned *p, int n) __asm__("FUNC_000504");
extern int  FUNC_000505(unsigned *p, int n) __asm__("FUNC_000505");
extern void FUNC_000781(unsigned char *a, unsigned char *b, int c)
                                            __asm__("FUNC_000781");

static void ar_cases(int tsv)
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
            r = FUNC_000504(&x, cnts[j]);
            if (tsv) printf("FUNC_000504\t%d\t%d\t%d\t%d\n",
                            (int)vals[i], cnts[j], (int)x, r);
            x = vals[i];
            r = FUNC_000505(&x, cnts[j]);
            if (tsv) printf("FUNC_000505\t%d\t%d\t%d\t%d\n",
                            (int)vals[i], cnts[j], (int)x, r);
        }

    for (i = 0; i < (int)(sizeof pairs / sizeof pairs[0]); i++) {
        unsigned char a[0x100], b[0x101];
        memset(a, 0, sizeof a); memset(b, 0, sizeof b);
        a[0xfe] = (unsigned char)pairs[i][0];
        b[0x100] = 0x77;
        FUNC_000781(a, b, pairs[i][1]);
        if (tsv) printf("FUNC_000781\t%d\t%d\t%d\t%d\n",
                        pairs[i][0], pairs[i][1], a[0xfe], a[0xff]);
    }

    if (!tsv)
        printf("     the address-register AND/OR + MOVZX batch, recorded only\n");
}

/* ---- the small-rules batch: sqrt chain + the mantissa normaliser --------
 *
 * FUNC_001467 is the game's integer square root: eight unsigned range
 * gates, each ending in a MOVZX off the word table DATA_007977 (filled by
 * this harness, so nothing depends on unconverted data) and an SAR. Leaf.
 * FUNC_001465 is vec3 magnitude: `imul dword [ebx]` -- the one-operand
 * multiply with its memory base in a DATA register, which needed the `pre`
 * plumbing -- plus two truncating imuls, then a call into 1467, so the
 * chain is two-deep translated code.
 * FUNC_001496 normalises a packed short: `test ah,0x40` lowered to
 * BTST #14, and its word-sized stack slots [ebp+0x8]/[ebp+0xa] are the
 * PROVEN sub-word case -- the function also reads the slot as a dword, so
 * frame.py flips the displacements, and these vectors hold that flip to
 * the oracle. The low word 0x8000 would not terminate in the ORIGINAL
 * either, so no vector uses it. */
extern int FUNC_001467(int x)        __asm__("FUNC_001467");
extern int FUNC_001465(const int *v) __asm__("FUNC_001465");
extern int FUNC_001496(int arg)      __asm__("FUNC_001496");
extern unsigned char DATA_007977[]   __asm__("DATA_007977");

static void sq_cases(int tsv)
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
        memcpy(DATA_007977 + i * 2, &w, 2);
    }

    for (i = 0; i < (int)(sizeof roots / sizeof roots[0]); i++)
        if (tsv) printf("FUNC_001467\t%d\t%d\n", roots[i],
                        FUNC_001467(roots[i]));
        else FUNC_001467(roots[i]);

    for (i = 0; i < (int)(sizeof vecs / sizeof vecs[0]); i++)
        if (tsv) printf("FUNC_001465\t%d\t%d\t%d\t%d\n", vecs[i][0],
                        vecs[i][1], vecs[i][2], FUNC_001465(vecs[i]));
        else FUNC_001465(vecs[i]);

    for (i = 0; i < (int)(sizeof mants / sizeof mants[0]); i++)
        if (tsv) printf("FUNC_001496\t%d\t%d\n", mants[i],
                        FUNC_001496(mants[i]));
        else FUNC_001496(mants[i]);

    if (!tsv)
        printf("     the sqrt chain + mantissa normaliser, recorded only\n");
}

/* ---- FUNC_000168_CdrFileScramble: words landing in si -------------------
 *
 * The XOR scrambler: `and si,byte -0x1` is the insert-low-word detour into
 * an address register, and `xor [edx],si` is the same register as a word
 * SOURCE -- both new spellings. The evolving key starts at 0x12345678 +
 * DATA_008669 and shifts with each word, so every word of the dump pins a
 * different key state. The word count is size/2 via a one-operand DIV. */
extern int FUNC_000168(void *data, int size)
                       __asm__("FUNC_000168_CdrFileScramble");
extern int DATA_008669 __asm__("DATA_008669");

static void si_cases(int tsv)
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
                memcpy(buf + k * 2, &w, 2);
            }
            DATA_008669 = seeds[j];
            r = FUNC_000168(buf, sizes[i]);
            if (tsv) {
                printf("FUNC_000168\t%d\t%d\t%d", sizes[i], seeds[j], r);
                for (k = 0; k < nw; k++) {
                    short w;
                    memcpy(&w, buf + k * 2, 2);
                    printf("\t%d", w);
                }
                printf("\n");
            }
        }

    if (!tsv)
        printf("     8 vectors over FUNC_000168, recorded only\n");
}

/* ---- FUNC_000021: THE FIRST JUMP TABLE ----------------------------------
 *
 * A 21-entry `jmp near [edx*4+DATA_000000]` switch: get-game-variable by
 * index. The table is an embedded SECTION .data block the parser now
 * carries as a Table item and the emitter writes as dc.l -- PLAN 5's
 * invariant makes the assembler put 68k label addresses in the slots. The
 * liveness pass enumerates the table's entries as the jmp's successors, so
 * every arm's flags are computed exactly.
 *
 * Every arm reads a different game variable, all exported and set here; two
 * arms dereference a POINTER variable (+0x86 byte), planted to local
 * buffers and never printed. idx -1 and 0x15 prove the unsigned `ja`
 * bounds check; 5..10 all land on the div arm through duplicate entries. */
extern int FUNC_000021(int idx) __asm__("FUNC_000021");
extern int DATA_008911 __asm__("DATA_008911");
extern int DATA_008888 __asm__("DATA_008888");
extern int DATA_008922 __asm__("DATA_008922");
extern int DATA_008885 __asm__("DATA_008885");
extern int DATA_008804 __asm__("DATA_008804");
extern int DATA_008807 __asm__("DATA_008807");
extern signed char DATA_008874 __asm__("DATA_008874");
extern int DATA_008862 __asm__("DATA_008862");
extern signed char DATA_008870 __asm__("DATA_008870");
extern int DATA_008892 __asm__("DATA_008892");
extern int DATA_008923 __asm__("DATA_008923");
extern int DATA_008924 __asm__("DATA_008924");
extern int DATA_008925 __asm__("DATA_008925");
extern int DATA_008926 __asm__("DATA_008926");

static void jt_cases(int tsv)
{
    static unsigned char objA[0x100], objB[0x100];
    int idx;

    objA[0x86] = 0x5a;
    objB[0x86] = 0xa7;
    DATA_008861 = (int)(long)objA;      /* the two pointer arms */
    DATA_008862 = (int)(long)objB;
    DATA_008911 = 1111111;
    DATA_008888 = -2222222;
    DATA_008922 = 33333333;
    DATA_008885 = -4;
    DATA_008804 = 123456789;            /* the div arm's dividend */
    DATA_008807 = 0x200000;             /* ... and its scaled addend */
    DATA_008874 = -100;
    DATA_008870 = 100;
    DATA_008890 = 555;
    DATA_008892 = -666;
    DATA_008923 = 7;
    DATA_008924 = 8;
    DATA_008925 = 9;
    DATA_008926 = 10;

    for (idx = -1; idx <= 0x15; idx++)
        if (tsv) printf("FUNC_000021\t%d\t%d\n", idx, FUNC_000021(idx));
        else FUNC_000021(idx);

    if (!tsv)
        printf("     23 vectors over FUNC_000021, recorded only\n");
}

/* ---- the LEA symbol+scaled-index pair -----------------------------------
 *
 * FUNC_000517 deletes record `idx` from the 24-byte-record list at
 * DATA_009004: `lea eax,[eax*8+DATA_009004]` lands in a DATA register, the
 * arithmetic path. FUNC_000199 appends a FileInfo to DATA_008665:
 * `lea edi,[eax*8+DATA_008665]` lands in an ADDRESS register, the two-LEA
 * flag-exact path. Records are written and dumped as INTS via memcpy, so
 * byte order never enters the comparison. */
extern void FUNC_000517(int idx)        __asm__("FUNC_000517");
extern void FUNC_000199(const void *fi)
                                __asm__("FUNC_000199_SysMenuAddFile");
extern unsigned char DATA_009004[] __asm__("DATA_009004");
extern int           DATA_008659   __asm__("DATA_008659");
extern unsigned char DATA_008665[] __asm__("DATA_008665");

static void lt_cases(int tsv)
{
    int i, k, v;

    /* 517: five live records, a terminator, then delete idx 1 and idx 0. */
    {
        static const int dels[] = { 1, 0, 4 };
        int d;
        for (d = 0; d < (int)(sizeof dels / sizeof dels[0]); d++) {
            for (i = 0; i < 5; i++)
                for (k = 0; k < 6; k++) {
                    v = (k == 0) ? 100 + i : i * 11 + k;
                    memcpy(DATA_009004 + i * 24 + k * 4, &v, 4);
                }
            v = 0;
            for (k = 0; k < 6; k++)
                memcpy(DATA_009004 + 5 * 24 + k * 4,
                       (k == 0 ? &v : &v), 4);
            FUNC_000517(dels[d]);
            if (tsv) {
                printf("FUNC_000517\t%d", dels[d]);
                for (i = 0; i < 36; i++) {
                    memcpy(&v, DATA_009004 + i * 4, 4);
                    printf("\t%d", v);
                }
                printf("\n");
            }
        }
    }

    /* 199: append at count 0, 1, at the last slot 0x3f, and the full-list
     * refusal at 0x40. */
    {
        static const int counts[] = { 0, 1, 0x3f, 0x40 };
        int c, fi[6], slot;
        memset(DATA_008665, 0, 1536);
        for (c = 0; c < (int)(sizeof counts / sizeof counts[0]); c++) {
            for (k = 0; k < 6; k++) fi[k] = 0x1000 * (c + 1) + k;
            DATA_008659 = counts[c];
            FUNC_000199(fi);
            slot = (counts[c] < 0x40) ? counts[c] : 0x3f;
            if (tsv) {
                printf("FUNC_000199\t%d\t%d", counts[c], DATA_008659);
                for (k = 0; k < 6; k++) {
                    memcpy(&v, DATA_008665 + slot * 24 + k * 4, 4);
                    printf("\t%d", v);
                }
                printf("\n");
            }
        }
    }

    if (!tsv)
        printf("     the LEA symbol+scaled-index pair, recorded only\n");
}

/* ---- FUNC_001613_DrawSpans + FUNC_001406: the residual-rules batch ------
 *
 * DrawSpans is THE function this project once listed as blocked on indirect
 * control flow. It takes a span-list context and a colour: per row (walked
 * from [ctx+0x1b30]-1 down to [ctx+0x1b2c]), it bubble-sorts the row's
 * endpoint list (44-byte records: int count, then up to ten int endpoints)
 * and draws sorted PAIRS from the END through a callback held in a stack
 * slot -- FUNC_001601_DrawHLine, or FUNC_001600_DrawHLineClip when
 * [ctx+0x1b28] is set. The unsorted vectors prove the sort; the odd-count
 * row proves the pairing drops the leftover; the clip vectors send x and y
 * out of range only when the CLIP callback is selected. Record shape is the
 * triangle's: guarded framebuffer, per-row summaries.
 *
 * FUNC_001406 carries the `cmp eax,1 / sbb eax,eax / neg eax` idiom -- the
 * self-SBB that reads C (exact after CMP) instead of the X that CMP never
 * writes -- plus the indexed scancode map with its -1 sentinel. */
extern void FUNC_001613(void *ctx, int col) __asm__("FUNC_001613_DrawSpans");
extern int  FUNC_001406(int key)     __asm__("FUNC_001406_KeybGetKeyState");
extern int           DATA_007687[] __asm__("DATA_007687");
extern unsigned char DATA_007691[] __asm__("DATA_007691");

static unsigned char g_dsctx[0x1b40] __attribute__((aligned(4)));

static void ds_row(int row, int n, const int *ends)
{
    int k;
    memcpy(g_dsctx + row * 44, &n, 4);
    for (k = 0; k < n; k++)
        memcpy(g_dsctx + row * 44 + 4 + k * 4, &ends[k], 4);
}

static void ds_emit(int flag, int ymin, int yhi, int col, int tsv)
{
    unsigned char *pix = g_trbuf + LN_GUARD * LN_W;
    int r, i, nrows = 0;

    memset(g_trbuf, LN_FILL, LN_N);
    DATA_007773.pixels = pix;
    memcpy(g_dsctx + 0x1b28, &flag, 4);
    memcpy(g_dsctx + 0x1b2c, &ymin, 4);
    memcpy(g_dsctx + 0x1b30, &yhi, 4);
    FUNC_001613(g_dsctx, col);

    for (r = 0; r < LN_TOTAL; r++) {
        const unsigned char *row = g_trbuf + r * LN_W;
        for (i = 0; i < LN_W; i++)
            if (row[i] != LN_FILL) { nrows++; break; }
    }

    if (tsv) {
        printf("FUNC_001613\t%d\t%d\t%d\t%d\t%d",
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
            printf("\t%d\t%d\t%d\t%d\t%d",
                   r - LN_GUARD, first, last - first + 1, n, sum);
        }
        printf("\n");
    }
}

static void ds_cases(int tsv)
{
    static const int rowA[] = { 50, 20, 100, 80 };
    static const int rowB[] = { 30, 60 };
    static const int rowC[] = { 90, 10, 40 };
    static const int rowF[] = { 0, 320 };
    static const int rowX[] = { -10, 400 };
    static const int rowM[] = { 300, -50, 120, 40, 310, 200 };
    int r, i;

    /* A: unsorted rows, the raw DrawHLine callback, everything in range. */
    memset(g_dsctx, 0, sizeof g_dsctx);
    ds_row(10, 4, rowA);
    ds_row(11, 2, rowB);
    ds_row(12, 3, rowC);
    ds_row(13, 0, rowA);
    ds_emit(0, 10, 14, 7, tsv);

    /* B: one full-width span through the raw callback. */
    memset(g_dsctx, 0, sizeof g_dsctx);
    ds_row(0, 2, rowF);
    ds_emit(0, 0, 1, 255, tsv);

    /* C: eighteen clipped rows up to the structural maximum. The context
     * holds EXACTLY 158 row records (0x1b28 = 158*44 -- the header sits
     * right after row 157), so y >= 158 cannot even be asked for; the
     * first cut of this vector tried and proved only that reading past a
     * struct diverges, which is the data's business, not the translator's. */
    memset(g_dsctx, 0, sizeof g_dsctx);
    for (r = 140; r < 158; r++) ds_row(r, 2, rowX);
    ds_emit(1, 140, 158, 3, tsv);

    /* D: a six-endpoint unsorted row, clipped at both x ends. */
    memset(g_dsctx, 0, sizeof g_dsctx);
    ds_row(5, 6, rowM);
    ds_emit(1, 5, 6, 9, tsv);

    /* E: an empty range draws nothing. */
    memset(g_dsctx, 0, sizeof g_dsctx);
    ds_row(8, 2, rowB);
    ds_emit(1, 8, 8, 5, tsv);

    /* FUNC_001406: map[key] < 0 -> 0; else state[map[key]] >= 1. */
    {
        static const int maps[]   = { 0, -1, 3, 7, 200 };
        static const int states[] = { 0, 1, 0x80, 0xff, 2, 0, 0, 5 };
        static const int keys[]   = { 0, 1, 2, 3, 4 };
        for (i = 0; i < 5; i++) DATA_007687[i] = maps[i];
        for (i = 0; i < 8; i++) DATA_007691[i] = (unsigned char)states[i];
        DATA_007691[200] = 0;
        for (i = 0; i < 5; i++)
            if (tsv) printf("FUNC_001406\t%d\t%d\n", keys[i],
                            FUNC_001406(keys[i]));
            else FUNC_001406(keys[i]);
    }

    if (!tsv)
        printf("     DrawSpans + KeybGetKeyState, recorded only\n");
}

/* --tsv writes one record per call as  name <TAB> arg... <TAB> result...,
 * decimal. The Amiga side prints byte-identical lines for a correct
 * translation, so verification is a plain textual diff -- and because every
 * field is a NUMBER, byte order never enters into it.
 *
 * (wid_cases is defined just above; see the operand-width comment by the
 * FUNC_001340 / FUNC_000422 declarations.)
 *
 * For the functions that write through a pointer there is no single result to
 * print, so EVERY output word goes in the record. A 64-bit result is two
 * fields, low half then high half; a vector result is three. Printing the
 * buffer as words rather than as a 64-bit value is the same rule as everywhere
 * else here: the moment anything is printed as bytes or as a type whose layout
 * differs between the two machines, the comparison stops being about the
 * translation. */
/* ---- the PACKED-FLOAT family: mantissa in the LOW word, exponent in the
 * HIGH word of one dword. Their original vectors were disavowed when the
 * packed-short test harness was caught matching displacements instead of
 * value semantics; TODAY the real convention was settled from the callers
 * (whole dwords, like every cdecl arg) and frame.py proves the slots, so
 * the family gets real vectors: conversions both ways, the arithmetic trio,
 * and the zero-divisor guard path of 1483. Exponents stay inside the safe
 * shift ranges LEFTOFF records for 1491..1494 (x86 masks a shift count to 5
 * bits, the 68k takes it mod 64; they agree only for 0..31). */
extern int FUNC_001463(int v)        __asm__("FUNC_001463");
extern int FUNC_001482(int a, int b) __asm__("FUNC_001482");
extern int FUNC_001483(int a, int b) __asm__("FUNC_001483");
extern int FUNC_001486(int a, int b) __asm__("FUNC_001486");
extern int FUNC_001491(int p)        __asm__("FUNC_001491");
extern int FUNC_001492(int p)        __asm__("FUNC_001492");
extern int FUNC_001493(int p)        __asm__("FUNC_001493");
extern int FUNC_001494(int p)        __asm__("FUNC_001494");

#define PF(m, e) ((int)((((unsigned)(e) & 0xFFFFu) << 16) \
                        | ((unsigned)(m) & 0xFFFFu)))

static void pf_cases(int tsv)
{
    static const int pf[] = {
        0,                      /* zero: mantissa 0 */
        PF(0x4000, 0),  PF(0x7FFF, 0),  PF(-0x4000, 0), PF(-0x7FFF, 0),
        PF(0x4000, 10), PF(0x5A82, 3),  PF(-0x5A82, 3), PF(0x4000, -10),
        PF(0x4001, 14), PF(0x6000, 1),  PF(-0x4800, 7),
    };
    static const int iv[] = {
        0, 1, -1, 2, 100, -100, 32767, -32768, 65536, 0x100000,
        -0x100000, 715827882,
    };
    int i, j;
    const int npf = (int)(sizeof pf / sizeof pf[0]);
    const int niv = (int)(sizeof iv / sizeof iv[0]);

    for (i = 0; i < niv; i++)
        if (tsv) printf("FUNC_001463\t%d\t%d\n", iv[i], FUNC_001463(iv[i]));
    for (i = 0; i < npf; i++)
        for (j = 0; j < npf; j++) {
            if (tsv) printf("FUNC_001482\t%d\t%d\t%d\n", pf[i], pf[j],
                            FUNC_001482(pf[i], pf[j]));
            if (tsv) printf("FUNC_001483\t%d\t%d\t%d\n", pf[i], pf[j],
                            FUNC_001483(pf[i], pf[j]));
            if (tsv) printf("FUNC_001486\t%d\t%d\t%d\n", pf[i], pf[j],
                            FUNC_001486(pf[i], pf[j]));
        }
    for (i = 0; i < npf; i++) {
        /* 1491 needs exp >= 0; 1492 exp in [0,31]; 1493/94 exp >= -16.
         * The pf[] exponents above run -10..14, so mask per function.
         * That restriction is HISTORY as of 2026-08-06 -- lower.py masks the
         * shift count now -- but the skip stays so these records keep their
         * positions; pfneg_cases() covers the excluded region at the end. */
        int e = (pf[i] >> 16) & 0xFFFF;
        int neg = (e & 0x8000) != 0;
        if (!neg && tsv)
            printf("FUNC_001491\t%d\t%d\n", pf[i], FUNC_001491(pf[i]));
        if (!neg && tsv)
            printf("FUNC_001492\t%d\t%d\n", pf[i], FUNC_001492(pf[i]));
        if (tsv) printf("FUNC_001493\t%d\t%d\n", pf[i], FUNC_001493(pf[i]));
        if (tsv) printf("FUNC_001494\t%d\t%d\n", pf[i], FUNC_001494(pf[i]));
    }
    if (!tsv) printf("     packed-float family vectors, recorded only\n");
}

/* ---- THE cl-COUNT MASK, and the vectors that carry it (2026-08-06) -------
 *
 * pf_cases() above skips 1491 and 1492 whenever the exponent is negative, and
 * says why: "1491 needs exp >= 0; 1492 exp in [0,31]". That was not a property
 * of the game -- FUNC_001492 has no guard at all and its callers may hand it
 * anything -- it was a property of the TRANSLATION. x86 masks a shift count to
 * five bits for byte, word and dword operands alike; an unmasked 68k takes the
 * count register modulo 64. lower.py now emits `and.l #31' ahead of every
 * cl-count shift (360 sites in the game, 8 of them in oracle-carried
 * functions), and these vectors walk straight through the region the skip used
 * to fence off. The count is 31-e for 1491/1492 and 15-e for 1493/1494:
 *
 *   e = -1   -> 32   x86 shifts by 0,  unmasked 68k by 32   DIVERGES
 *   e = -2   -> 33   x86 1,            unmasked 33          DIVERGES
 *   e = -8   -> 39   x86 7,            unmasked 39          DIVERGES
 *   e = -16  -> 47   x86 15,           unmasked 47          DIVERGES
 *   e = -32  -> 63   x86 31,           unmasked 63          DIVERGES
 *   e = -33  -> 64   x86 0,            unmasked 0           AGREES
 *   e = -64  -> 95   x86 31,           unmasked 31          AGREES
 *   e = -100 -> 131  x86 3,            unmasked 3           AGREES
 *
 * The three that AGREE are in the list on purpose. The two semantics differ
 * exactly when bit 5 of the count is set, so a mask that also moved the other
 * cases would be a fresh bug wearing the fix's clothes, and only a vector that
 * cannot fail either way can show that it does not.
 *
 * Emitted LAST, after every existing case: compare.py lines the two files up by
 * POSITION, so appending is the only way to add vectors without moving every
 * record that already matches. */
static void pfneg_cases(int tsv)
{
    static const int mant[] = { 0, 0x4000, 0x7FFF, -0x4000, -0x7FFF, 1, -1 };
    static const int expo[] = { -1, -2, -8, -16, -17, -20, -32, -33, -48,
                                -64, -100 };
    const int nm = (int)(sizeof mant / sizeof mant[0]);
    const int ne = (int)(sizeof expo / sizeof expo[0]);
    int i, j;

    for (i = 0; i < nm; i++)
        for (j = 0; j < ne; j++) {
            int p = PF(mant[i], expo[j]);
            if (tsv) {
                printf("FUNC_001491\t%d\t%d\n", p, FUNC_001491(p));
                printf("FUNC_001492\t%d\t%d\n", p, FUNC_001492(p));
                printf("FUNC_001493\t%d\t%d\n", p, FUNC_001493(p));
                printf("FUNC_001494\t%d\t%d\n", p, FUNC_001494(p));
            }
        }
    if (!tsv)
        printf("     %d vectors past the 5-bit shift-count boundary\n",
               nm * ne * 4);
}

/* ---- FUNC_000869 + FUNC_000868: the star-system lookup ------------------
 *
 * The carrier for the handcoded-system table's storage class. That table is
 * 340+8 records of 54 bytes spanning ten labels from DATA_007106 to
 * DATA_007115, and until 2026-08-05 three of those ten were byte-swapped in
 * the converted image (DATA_007107 be32, DATA_007110 be32, DATA_007112 be16)
 * while the other seven stayed raw -- so the same field read two different
 * ways depending on which record it was in. All ten are raw now, and the four
 * wider-than-byte reads that reach the records through a run-time index
 * (ffe.asm:60790, 60912, 60915, 60938) swap at the site.
 *
 * Why this pair is a good carrier: FUNC_000868 is a 123-line leaf that walks
 * the table comparing a DWORD per record, and FUNC_000869 is 44 lines that
 * index the same records for two dwords, five bytes and a word -- every width
 * the storage question has, driven by one scalar argument. It also exercises
 * three sub-word stack accesses frame.py has to place: `mov ax,[ebp+0x8]'
 * (60816), `mov dl,[ebp+0xb]' (60828) and `and byte [ebp-0x1],0x3' (60800).
 *
 * The ids below cover every exit: a handcoded match (0x1aa51717 is the one the
 * instrumented reference was measured resolving to index 323 on 2026-08-05),
 * the four DATA_007116 entries that return -1 through the masked compare, and
 * seven distances hitting each bucket of the -2..-8 classifier.
 */
extern void FUNC_000869(int id, int *o2, int *o3, int *o4, int *o5, int *o6,
                        int *o7, int *o8, int *o9) __asm__("FUNC_000869");
extern int  FUNC_000868(int id) __asm__("FUNC_000868");

/* FUNC_000871 -- THE STAR ITSELF, and the first thing the system generator
 * computes (FUNC_000878 calls it before anything else, ffe.asm:63878-63888).
 * It resolves the id through FUNC_000868 and then reads two fields of the same
 * 54-byte record: a BYTE at +44 split into two nibbles (the two star classes)
 * and the WORD at +10 shifted right by 6. So it exercises the fixed table at
 * both widths in one call -- the byte read that must NOT swap and the word
 * read that must. The two temperatures come from DATA_008912/8913, filled by
 * the harness so no live game state is involved.
 * All seven arguments are pointers except the id; arg5 is DATA_007128 in the
 * game, which is written, not read. */
extern void FUNC_000871(int id, int *o2, int *o3, int *o4, int *o5, int *o6,
                        int *o7) __asm__("FUNC_000871");
extern int DATA_008912 __asm__("DATA_008912");
extern int DATA_008913 __asm__("DATA_008913");

#define SY_XY(x, y) (int)((((unsigned)(y) & 0x1fff) << 13) | ((unsigned)(x) & 0x1fff))

static const int sy_ids[] = {
    0x1aa51717,                 /* the player's own system: handcoded, idx 323 */
    0x02a49718, 0x06a49718,     /* records 0 and 1 of the handcoded table */
    0x12a49718, 0x16a49718,     /* records 2 and 3 */
    0x1aa53717, 0x06a53716,     /* -> DATA_007116 through the masked compare */
    0x02a53715, 0x02a55717,     /* the other two DATA_007116 entries */
    0x03a53717,                 /* top byte & 3 == 3: the same bits, NO match */
    0,                          /* degenerate: matches the terminator's zero */
    SY_XY(0x1718, 0x1524),      /* distance 0      -> -2 */
    SY_XY(0x171b, 0x1524),      /* 9              -> -3 */
    SY_XY(0x171e, 0x1524),      /* 36             -> -4 */
    SY_XY(0x1720, 0x1524),      /* 64             -> -5 */
    SY_XY(0x1723, 0x1524),      /* 121            -> -6 */
    SY_XY(0x172c, 0x1524),      /* 400            -> -7 */
    SY_XY(0x17e0, 0x1524),      /* 40000          -> -8 */
    SY_XY(0x1718, 0x1530),      /* the y term alone */
    SY_XY(0x0001, 0x0001),      /* both terms far negative */
    SY_XY(0x1fff, 0x1fff),      /* both terms at the 13-bit ceiling */
};

static void sy_cases(int tsv)
{
    const int n = (int)(sizeof sy_ids / sizeof sy_ids[0]);
    int i;

    for (i = 0; i < n; i++) {
        int o2 = 0, o3 = 0, o4 = 0, o5 = 0, o6 = 0, o7 = 0, o8 = 0, o9 = 0;
        int idx = FUNC_000868(sy_ids[i]);

        FUNC_000869(sy_ids[i], &o2, &o3, &o4, &o5, &o6, &o7, &o8, &o9);
        if (tsv)
            printf("FUNC_000869\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\n",
                   sy_ids[i], idx, o2, o3, o4, o5, o6, o7, o8, o9);
    }
    DATA_008912 = 0x0BADF00D;
    DATA_008913 = 0x1234ABCD;
    for (i = 0; i < n; i++) {
        int o2 = 0, o3 = 0, o4 = 0, o5 = 0, o6 = 0, o7 = 0;

        FUNC_000871(sy_ids[i], &o2, &o3, &o4, &o5, &o6, &o7);
        if (tsv)
            printf("FUNC_000871\t%d\t%d\t%d\t%d\t%d\t%d\t%d\n",
                   sy_ids[i], o2, o3, o4, o5, o6, o7);
    }
    if (!tsv)
        printf("     star-system lookup vectors, recorded only\n");
}

static int emit_tsv(void)

{
    int i, j;
    int out[3];

    for (i = 0; i < ffe_nvec; i++)
        for (j = 0; j < ffe_nvec; j++)
            printf("FUNC_001521\t%d\t%d\t%d\n", ffe_vec[i], ffe_vec[j],
                   FUNC_001521(ffe_vec[i], ffe_vec[j]));
    for (i = 0; i < ffe_nvec; i++)
        printf("FUNC_001522\t%d\t%d\n", ffe_vec[i], FUNC_001522(ffe_vec[i]));
    for (i = 0; i < ffe_nvec; i++)
        for (j = 0; j < ffe_nvec; j++)
            printf("FUNC_001523\t%d\t%d\t%d\n", ffe_vec[i], ffe_vec[j],
                   FUNC_001523(ffe_vec[i], ffe_vec[j]));

    for (i = 0; i < ffe_ni64; i++)
        for (j = 0; j < ffe_ni64; j++) {
            out[0] = out[1] = 0;
            FUNC_001333(out, ffe_i64[i][0], ffe_i64[i][1],
                             ffe_i64[j][0], ffe_i64[j][1]);
            printf("FUNC_001333\t%d\t%d\t%d\t%d\t%d\t%d\n",
                   ffe_i64[i][0], ffe_i64[i][1], ffe_i64[j][0], ffe_i64[j][1],
                   out[0], out[1]);
        }

    for (i = 0; i < ffe_ni64; i++)
        for (j = 0; j < ffe_ni64; j++) {
            out[0] = out[1] = 0;
            FUNC_001335(out, ffe_i64[i][0], ffe_i64[i][1],
                             ffe_i64[j][0], ffe_i64[j][1]);
            printf("FUNC_001335\t%d\t%d\t%d\t%d\t%d\t%d\n",
                   ffe_i64[i][0], ffe_i64[i][1], ffe_i64[j][0], ffe_i64[j][1],
                   out[0], out[1]);
        }

    /* In place, so the buffer is both the input and the output. */
    for (i = 0; i < ffe_ni64; i++)
        for (j = 0; j < ffe_nshift; j++) {
            out[0] = ffe_i64[i][0];
            out[1] = ffe_i64[i][1];
            FUNC_001341(out, ffe_shift[j]);
            printf("FUNC_001341\t%d\t%d\t%d\t%d\t%d\n",
                   ffe_i64[i][0], ffe_i64[i][1], ffe_shift[j], out[0], out[1]);
        }

    for (i = 0; i < ffe_nmsb; i++)
        printf("FUNC_001656\t%d\t%d\n", ffe_msb[i], FUNC_001656(ffe_msb[i]));

    for (i = 0; i < ffe_nvmvec; i++)
        for (j = 0; j < ffe_nvmmat; j++) {
            out[0] = out[1] = out[2] = 0;
            FUNC_001669(out, ffe_vmvec[i], ffe_vmmat[j]);
            printf("FUNC_001669\t%d\t%d\t%d"
                   "\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d"
                   "\t%d\t%d\t%d\n",
                   ffe_vmvec[i][0], ffe_vmvec[i][1], ffe_vmvec[i][2],
                   ffe_vmmat[j][0], ffe_vmmat[j][1], ffe_vmmat[j][2],
                   ffe_vmmat[j][3], ffe_vmmat[j][4], ffe_vmmat[j][5],
                   ffe_vmmat[j][6], ffe_vmmat[j][7], ffe_vmmat[j][8],
                   out[0], out[1], out[2]);
        }

    /* LAST, and it has to stay last: compare.py lines the two files up by
     * position, so appending is the only way to add vectors without moving
     * every record the 68k side has already matched. */
    hl_cases(1);
    cl_cases(1);
    ln_cases(1);
    tr_cases(1);
    math_cases(1);
    leaf_cases(1);
#if FFE_PACKED_SHORT_ARGS
    pk_cases(1);
#endif
    hr_cases(1);
#if FFE_SUBWORD_STACK_FIXED
    cr_cases(1);
#endif
    pad_cases(1);
    wid_cases(1);
    ix_cases(1);
    mi_cases(1);
    sc_cases(1);
    qd_cases(1);
    la_cases(1);
    bs_cases(1);
    mu_cases(1);
    rp_cases(1);
    ar_cases(1);
    sq_cases(1);
    si_cases(1);
    jt_cases(1);
    lt_cases(1);
    ds_cases(1);
    pf_cases(1);
    sy_cases(1);
    /* LAST, always: appended 2026-08-06, and compare.py matches positionally,
     * so anything inserted ahead of these moves every record that already
     * agrees. New vectors go at the END. */
    pfneg_cases(1);
    return 0;
}

int main(int argc, char **argv)
{
    const int *v = ffe_vec;
    const int n = ffe_nvec;
    int i, j;
    char buf[64];

    if (argc > 1 && strcmp(argv[1], "--tsv") == 0) return emit_tsv();

    printf("=== oracle: the ORIGINAL x86 functions, called directly ===\n\n");

    printf("--- FUNC_001521  fixed-point multiply, (a*b)>>31 ---\n");
    for (i = 0; i < n; i++)
        for (j = 0; j < n; j++) {
            sprintf(buf, "(%d, %d)", v[i], v[j]);
            check("1521", FUNC_001521(v[i], v[j]), model_1521(v[i], v[j]), buf);
        }

    printf("\n--- FUNC_001522  square high, (a*a)>>32 ---\n");
    for (i = 0; i < n; i++) {
        sprintf(buf, "(%d)", v[i]);
        check("1522", FUNC_001522(v[i]), model_1522(v[i]), buf);
    }

    /* 1523 has a saturating branch we deliberately do NOT model: the point here
     * is to record what the original returns, so the translated version can be
     * held to it. Printed, not checked. */
    printf("\n--- FUNC_001523  fixed-point divide (recorded, not modelled) ---\n");
    for (i = 0; i < n; i++)
        for (j = 0; j < n; j++)
            printf("     1523 (%d, %d) = %d (0x%08x)\n",
                   v[i], v[j], FUNC_001523(v[i], v[j]),
                   (unsigned)FUNC_001523(v[i], v[j]));

    printf("\n--- FUNC_001333  64-bit add, result through a pointer ---\n");
    for (i = 0; i < ffe_ni64; i++)
        for (j = 0; j < ffe_ni64; j++) {
            int out[2] = { 0, 0 };
            long long a = j64(ffe_i64[i][0], ffe_i64[i][1]);
            long long b = j64(ffe_i64[j][0], ffe_i64[j][1]);
            FUNC_001333(out, ffe_i64[i][0], ffe_i64[i][1],
                             ffe_i64[j][0], ffe_i64[j][1]);
            sprintf(buf, "(%lld, %lld)", a, b);
            check64("1333", out, (long long)((unsigned long long)a
                                             + (unsigned long long)b), buf);
        }

    printf("\n--- FUNC_001335  64-bit subtract ---\n");
    for (i = 0; i < ffe_ni64; i++)
        for (j = 0; j < ffe_ni64; j++) {
            int out[2] = { 0, 0 };
            long long a = j64(ffe_i64[i][0], ffe_i64[i][1]);
            long long b = j64(ffe_i64[j][0], ffe_i64[j][1]);
            FUNC_001335(out, ffe_i64[i][0], ffe_i64[i][1],
                             ffe_i64[j][0], ffe_i64[j][1]);
            sprintf(buf, "(%lld, %lld)", a, b);
            check64("1335", out, (long long)((unsigned long long)a
                                             - (unsigned long long)b), buf);
        }

    printf("\n--- FUNC_001341  64-bit arithmetic shift, in place ---\n");
    for (i = 0; i < ffe_ni64; i++)
        for (j = 0; j < ffe_nshift; j++) {
            int out[2];
            long long a = j64(ffe_i64[i][0], ffe_i64[i][1]);
            out[0] = ffe_i64[i][0];
            out[1] = ffe_i64[i][1];
            FUNC_001341(out, ffe_shift[j]);
            sprintf(buf, "(%lld, %d)", a, ffe_shift[j]);
            check64("1341", out, model_1341(a, ffe_shift[j]), buf);
        }

    printf("\n--- FUNC_001656  index of the most significant set bit ---\n");
    for (i = 0; i < ffe_nmsb; i++) {
        sprintf(buf, "(0x%08x)", (unsigned)ffe_msb[i]);
        check("1656", FUNC_001656(ffe_msb[i]),
              model_1656((unsigned)ffe_msb[i]), buf);
    }

    /* Recorded, not modelled: it is nine calls to FUNC_001521, so a model here
     * would only be restating 1521's rounding a second time. */
    printf("\n--- FUNC_001669  vector x matrix (recorded, not modelled) ---\n");
    for (i = 0; i < ffe_nvmvec; i++)
        for (j = 0; j < ffe_nvmmat; j++) {
            int out[3] = { 0, 0, 0 };
            FUNC_001669(out, ffe_vmvec[i], ffe_vmmat[j]);
            printf("     1669 vec%d x mat%d = (%d, %d, %d)\n",
                   i, j, out[0], out[1], out[2]);
        }

    /* The descriptor is the game's own memory, so say what was in it before we
     * repoint it. If this does not print DATA_009226 the wrapper exported the
     * wrong symbols and every span below would be measuring the wrong thing. */
    printf("\n--- FUNC_001601  horizontal span ---\n");
    printf("     DATA_007773 = { %p, %d, %d, %d, %d, %d }; DATA_009226 = %p%s\n",
           (void *)DATA_007773.pixels, DATA_007773.x, DATA_007773.y,
           DATA_007773.w, DATA_007773.h, DATA_007773.jump, (void *)DATA_009226,
           DATA_007773.pixels == DATA_009226 ? "  (they agree)"
                                             : "  *** MISMATCH ***");
    if (DATA_007773.pixels != DATA_009226) g_fail++;
    {
        unsigned char *saved = DATA_007773.pixels;
        hl_cases(0);
        printf("\n--- FUNC_001600 / FUNC_001599  clipped span ---\n");
        cl_cases(0);
        printf("\n--- FUNC_001616  DrawLine (recorded; only the colour is checked) ---\n");
        ln_cases(0);
        printf("\n--- FUNC_001604  DrawTriangle (colour and the y clip are checked) ---\n");
        tr_cases(0);
        DATA_007773.pixels = saved;
    }
    {
        printf("\n--- the 64-bit / vector maths batch (recorded, not modelled) ---\n");
        math_cases(0);
        printf("\n--- the pure-leaf batch (recorded, not modelled) ---\n");
        leaf_cases(0);
#if FFE_PACKED_SHORT_ARGS
        printf("\n--- the packed-short batch (recorded, not modelled) ---\n");
        pk_cases(0);
#endif
        printf("\n--- FUNC_001461_GuiAddHotRect2 (recorded, not modelled) ---\n");
        hr_cases(0);
        printf("\n--- the alignment-padding batch (recorded, not modelled) ---\n");
        pad_cases(0);
        printf("\n--- the operand-width batch (recorded, not modelled) ---\n");
        wid_cases(0);
        printf("\n--- the indexed-addressing batch (recorded, not modelled) ---\n");
        ix_cases(0);
        printf("\n--- byte MOVSX and truncating IMUL (recorded, not modelled) ---\n");
        mi_cases(0);
        printf("\n--- the SETcc / XCHG batch (recorded, not modelled) ---\n");
        sc_cases(0);
        printf("\n--- FUNC_001607  DrawQuad (colour and the y clip are checked) ---\n");
        qd_cases(0);
        printf("\n--- the label-address / movzx batch (recorded, not modelled) ---\n");
        la_cases(0);
        printf("\n--- the base+symbol batch (recorded, not modelled) ---\n");
        bs_cases(0);
        printf("\n--- FUNC_001337  Int64Mul64 (recorded, not modelled) ---\n");
        mu_cases(0);
        printf("\n--- FUNC_000211  repne scasb / strcpy (recorded, not modelled) ---\n");
        rp_cases(0);
        printf("\n--- the address-register AND/OR + MOVZX batch (recorded) ---\n");
        ar_cases(0);
        printf("\n--- the sqrt chain + mantissa normaliser (recorded) ---\n");
        sq_cases(0);
        printf("\n--- FUNC_000168  the si word detours (recorded) ---\n");
        si_cases(0);
        printf("\n--- FUNC_000021  the first jump table (recorded) ---\n");
        jt_cases(0);
        printf("\n--- the LEA symbol+scaled-index pair (recorded) ---\n");
        lt_cases(0);
        printf("\n--- DrawSpans + KeybGetKeyState (recorded) ---\n");
        ds_cases(0);
        printf("\n--- the star-system lookup FUNC_000869/868 (recorded) ---\n");
        sy_cases(0);
    }
#if FFE_SUBWORD_STACK_FIXED
    {
        unsigned char *saved = DATA_007773.pixels;
        printf("\n--- FUNC_001612_DrawCircle (recorded, not modelled) ---\n");
        cr_cases(0);
        DATA_007773.pixels = saved;
    }
#endif

    printf("\n=== %d disagreement(s) between the real code and the model ===\n", g_fail);
    return g_fail ? 1 : 0;
}
