#!/usr/bin/env python3
"""Instruction selection: one x86 instruction -> one 68020 sequence.

The register mapping is PLAN 3.1 and is not negotiable here:

    eax->d0  ecx->d1  edx->d2  ebx->d3  esi->a2  edi->a3  ebp->a5  esp->a7(sp)
    a0/a1 and d7 are translator scratch.

Every rule returns, besides the lines to emit, an honest declaration of how its
CCR result differs from the x86's EFLAGS result:

    ccr_extra   flags the 68k form WRITES where the x86 form leaves them alone
                (the systematic case: 68k `move` sets N/Z, x86 `mov` sets nothing)
    ccr_wrong   flags both forms write but with a DIFFERENT value
                (e.g. 68k `lsr` clears V, x86 `shr` sets OF from the old MSB)

`lower()` intersects that declaration with the liveness computed by flags.py and
refuses to emit if any of those flags is still live. That is the mechanism that
makes "eager flags" a proof rather than a hope -- and it is why this file can
use `moveq` and `pea` freely where the plain form would be flag-exact.

Two 68k facts do most of the work here, both checked against the M68000
Programmer's Reference Manual rather than remembered (see lo_mov and lo_adc):
MOVE and MOVEA do not touch the X bit, and MOVEA does not touch the CCR at all.
The first is what lets `add`/`mov`/`adc` chains translate one for one; the
second is what lets a `movea.l dN,a0` be slipped in between a flag setter and
its consumer to synthesise the addressing mode the 68k lacks.

Only the mnemonics and addressing modes this project has actually verified are
implemented. Everything else raises with the ffe.asm line number. The list of
what is handled lives in HANDLED below and is printed by translate.py --help.
"""
import os
from dataclasses import dataclass, field, replace
from typing import List, Optional

import flags as F
import storage
from parse import Imm, Insn, Mem, Reg, Sym, TranslationError, canon, fail

# x86 -> 68k. esp prints as `sp`: it is a7 and vasm accepts either, but every
# stack idiom below reads better as sp and matches the c2p sources next door.
XREG = {
    "eax": "d0", "ecx": "d1", "edx": "d2", "ebx": "d3",
    "esi": "a2", "edi": "a3", "ebp": "a5", "esp": "sp",
}

# The 8-bit x86 registers, which are the LOW BYTE of the same 68k register the
# 32-bit name maps to. That is not an approximation: a 68k byte operation on a
# data register writes bits 0..7 and leaves 8..31 exactly as they were, which is
# precisely what `mov cl,x` does to ecx. ah/ch/dh/bh are absent on purpose --
# bits 8..15 have no 68k addressing at all, and PLAN 3.5 handles their 22 sites
# with a different rule (`btst #n+8`).
XREG8 = {"al": "d0", "cl": "d1", "dl": "d2", "bl": "d3"}

# The 16-bit x86 registers, and only the four that live in 68k DATA registers.
# The argument is the same one that makes XREG8 exact: a 68k word operation on a
# data register writes bits 0..15 and leaves 16..31 alone, which is precisely
# what `mov dx,[edi+2]` does to edx.
#
# si/di/bp/sp are DELIBERATELY absent, and this is not an oversight. They map to
# a2/a3/a5/sp, and a word operation on an ADDRESS register is defined to
# sign-extend its result through all 32 bits (M68000PRM: "the entire destination
# address register is used regardless of the operation size"). x86 `mov si,x`
# leaves the top half of esi untouched. Those are different operations, so the
# translator refuses rather than picking one.
XREG16 = {"ax": "d0", "cx": "d1", "dx": "d2", "bx": "d3"}

SIZE_SFX = {1: ".b", 2: ".w", 4: ".l"}

# x86 index scales, as a shift count. The 68020 has all four in hardware; the
# arithmetic form below needs the shift.
SCALE_SHIFT = {1: 0, 2: 1, 4: 2, 8: 3}

HANDLED = """\
  mov push pop                register, immediate, (disp,An), absolute, and a
                              memory base held in a data register; mov also in
                              BYTE width (al/cl/dl/bl and byte memory) and WORD
                              width (ax/cx/dx/bx and word memory -- si/di/bp/sp
                              are refused, a 68k word op on an An sign-extends)
  movsx                       32-bit destination from a 16-bit register or an
                              explicit `word [mem]`: MOVE.W + EXT.L
  cdq                         sign-extend eax through edx, for the IDIV below
  add adc sub sbb             register or memory destination
  and or xor not neg test     ditto; 68k AND/OR/EOR forbid An as an operand, so
                              an esi/edi/ebp operand goes via scratch d7
  inc dec                     x86 leaves CF alone and 68k ADDQ/SUBQ do not --
                              the liveness pass refuses if that is observable;
                              on an address register ADDQ/SUBQ set NO flags,
                              which is checked the same way
  lea                         base+displacement, and base+index*scale+disp --
                              natively when everything is in address registers,
                              as arithmetic when the result is in a data one
  imul                        one-operand form only: edx:eax = eax * src
  div idiv                    one-operand form only: edx:eax / src, unsigned
                              and signed
  shl/sal shr sar             register or memory destination; immediate count
                              1..31, or a variable count in cl
  rcl                         register destination, count 1
  cmp                         data-register destination (long or word), an
                              address-register one via CMPA.L, and a memory one
                              against an immediate via CMPI
  call                        direct, to a translated FUNC_nnnnnn body
  jmp, all 26 jcc spellings   direct, to a label inside the same function
  ret                         without a stack-adjust operand\
"""

# PLAN 3.1's scratch registers. a1 and d4-d6 stay unused for now; one scratch
# address register is provably enough for the memory-base case because x86
# allows at most one memory operand per instruction.
SCRATCH_A = "a0"
SCRATCH_D = "d7"
# The second data scratch, for the one shape that needs BOTH operands out of
# address registers at once (`or esi,edi`). Legal because NO translated code
# uses d4-d6 at all (they map to nothing in PLAN 3.1) and the C ABI preserves
# them across any call a lowered unit could make -- and a unit never holds a
# scratch across an instruction boundary anyway.
SCRATCH_D2 = "d6"


@dataclass
class Line:
    """One output line.

    `text` None with an empty comment is a blank line, None with a comment is a
    comment line, and `col0` puts a label in column 1 where the assembler
    expects it -- everything else is indented like ordinary Amiga assembly."""
    text: Optional[str] = None
    comment: str = ""
    col0: bool = False

    def render(self):
        if self.text is None:
            return "; " + self.comment if self.comment else ""
        if self.col0:
            return self.text
        if self.comment:
            return "\t%s\t\t; %s" % (self.text, self.comment)
        return "\t" + self.text


def label(name):
    return Line(name + ":", col0=True)


@dataclass
class Lowered:
    lines: List[Line] = field(default_factory=list)
    ccr_extra: frozenset = F.NONE
    ccr_wrong: frozenset = F.NONE
    why: str = ""            # prose emitted above the instruction


def op(mnem, *args):
    return Line("%s\t%s" % (mnem, ",".join(args)) if args else mnem)


# ---- operands ---------------------------------------------------------------

def reg68(r, ins, size=4):
    """The 68k register an x86 register maps to, at the width the instruction is
    operating at. Passing the width in rather than reading it off `r` is what
    makes a mixed-width operand a refusal instead of a silent widening."""
    if r.size != size:
        fail(ins.line, ins.text,
             f"{r.size*8}-bit register {r.name!r} in a {size*8}-bit operation")
    if size == 4:
        return XREG[r.name]
    if size == 1:
        if r.high:
            fail(ins.line, ins.text,
                 f"{r.name!r} is an x86 HIGH byte (bits 8..15 of the register) "
                 "and the 68k has no addressing mode for it. PLAN 3.5 turns the "
                 "22 `test rh,imm` sites into BTST, which is a separate rule.")
        return XREG8[r.name]
    if size == 2:
        if r.name not in XREG16:
            fail(ins.line, ins.text,
                 f"{r.name!r} maps to {XREG[('e' + r.name)]}, an address "
                 "register, and a 68k WORD operation on one sign-extends "
                 "through all 32 bits while the x86 leaves the top half alone "
                 "(see XREG16). Refused rather than guessed.")
        return XREG16[r.name]
    fail(ins.line, ins.text,
         f"{size*8}-bit operand -- only longword, word and byte are handled")


def is_areg(r):
    return r == "sp" or (r.startswith("a") and r[1:].isdigit())


def op_size(ins):
    """The width the x86 instruction operates at, in bytes.

    Read off the REGISTER operands, or off an explicit NASM size prefix on a
    memory operand -- and deliberately NEVER off an immediate. `add esp,byte
    +0x10` spells an ENCODING hint (a sign-extended byte immediate), not a
    byte-wide add, and reading it as one would turn every cdecl stack adjustment
    in the game into a byte operation."""
    sizes = set()
    for o in ins.ops:
        if isinstance(o, Reg):
            sizes.add(o.size)
        elif isinstance(o, Mem) and o.size is not None:
            sizes.add(o.size)
    if not sizes:
        return 4
    if len(sizes) > 1:
        fail(ins.line, ins.text,
             "operands of different widths (%s) -- x86 sign/zero extension is a "
             "rule of its own (movsx/movzx, PLAN 3.5), not something to infer"
             % ", ".join("%d-bit" % (s * 8) for s in sorted(sizes)))
    return sizes.pop()


def imm68(v, size=4):
    """Small values in decimal, everything else in hex -- reviewing a bit
    pattern written as 4160749568 is how sign errors survive review.

    A byte immediate is printed as a bit pattern whatever its sign: NASM writes
    the 0..255 form and vasm's signed range for `#<data>.b` is -128..127, so
    `#$FF` is the spelling that means the same thing to both."""
    if size == 1:
        return "#$%02X" % (v & 0xFF)
    if size == 2 and not -32768 <= v <= 32767:
        # Same argument as the byte case: NASM writes the unsigned 0..65535
        # form, vasm's signed range for `#<data>.w` is -32768..32767, and the
        # bit pattern is the spelling both agree on.
        return "#$%04X" % (v & 0xFFFF)
    if -32768 <= v <= 32767:
        return "#%d" % v
    return "#$%08X" % (v & 0xFFFFFFFF)


# Appended to `why` wherever a MOVEA had to be inserted to build an address.
#
# Why MOVEA and not something else: the 68k reaches memory only through an
# address register, and of the ways to get a value into one, MOVEA is the only
# one that is documented as leaving the condition codes completely alone
# (M68000PRM, MOVEA, Condition Codes: "Not affected"). That is what makes it
# legal to slip between a flag setter and the instruction that consumes the
# flag -- including between an ADD and its ADDX, where X has to survive.
#
# Cost: one extra instruction per access, at the ~13,000 operands PLAN 3.1
# counted with a base in eax/ebx/ecx/edx. The obvious saving is to cache what
# a0 (and a1) currently hold across a basic block and re-emit the MOVEA only
# when the x86 base register is written, a label is passed, or a call
# intervenes -- the loops in the rasteriser reload the same base every line.
# It is NOT implemented here on purpose: it is an optimisation, and this
# project measures those before it writes them (PLAN 3.1, M6).
BASE_NOTE = (
    "Memory base in a data register: MOVEA.L dN,a0 first, then the access "
    "through (disp,a0). MOVEA does not affect the CCR at all (M68000PRM), so it "
    "is safe even between a flag setter and its consumer. PLAN 3.1: ~13,000 "
    "sites; caching the base across a block is left until it is measured.")

# Appended wherever an esi/edi/ebp operand had to be copied into d7.
#
# M68000PRM, OR (and identically AND, EOR, ADDX, SUBX): "The contents of an
# address register may not be used as an operand." esi/edi/ebp map to a2/a3/a5,
# so those operands take a detour through the scratch data register. MOVE.L
# An,Dn writes N/Z/V/C -- overwritten by the instruction that follows -- and
# leaves X alone, so a multiprecision chain loses nothing.
AREG_NOTE = (
    "An esi/edi/ebp operand goes through d7 first: AND/OR/EOR/ADDX/SUBX cannot "
    "take an address register (M68000PRM). MOVE.L An,Dn leaves X alone.")


# THE STATIC-DATA TWIN OF frame.py. The converters store each label be32 /
# be16 / le / plain / split (see tools\translator\storage.py); an access whose
# width disagrees with the storage is corrected where that is provable and
# refused where it is not. Found the hard way: the dword-converted
# scancode->ASCII table returned 'u' for the splash's 'y', and a byte-written,
# dword-read variable produced a 0x15000000 index and a wild read -- the
# nondeterministic first-light boots of 2026-07-29.
def _static_disp(o, ins, w):
    """Displacement to emit for a named static access of width `w`, corrected
    for the storage of the label that actually COVERS the addressed byte
    (ffedis loves addressing one label through an earlier one -- see
    storage.resolve). o.swapped means a MOV rule already owns the byte-swap
    for this operand and the displacement must pass through."""
    # A BYTE INSIDE A BIG-ENDIAN 32-BIT RECORD WHOSE BASE IS AN EARLIER LABEL.
    # storage.resolve() only walks forward, so the manifest cannot type these;
    # storage.BE32_MEMBER names them explicitly, with the evidence. The
    # correction is the same mirror the be32 branch below applies, written out
    # for a record the walk cannot reach: the access sits at base + K where
    # K = member_offset + o.disp, and big-endian it must move to base + 3 - K.
    # Relative to o.sym (which is base + member_offset) that displacement is
    # 3 - 2*member_offset - o.disp.
    # A NAMED narrow access into a 'plain' (address-taken, native-order) dword:
    # plain storage gets no correction from the manifest, so a word/byte that
    # x86 reads at the LOW end of the dword is named by line in
    # lo_byte_sites.tsv with its delta, exactly like the pointer sites.
    # DATA_008958 (the hyperspace target id, ffe.asm:58572) is the first.
    _d = storage.lo_byte_site(ins.line)
    if _d and not o.swapped and w < 4:
        _k = storage.kind(o.sym)
        if _k is not None and _k[0] == "plain":
            return o.disp + _d
    _m = storage.be32_member(o.sym)
    if _m is not None and not o.swapped:
        _base, _k0 = _m
        _K = _k0 + o.disp
        if w != 1:
            fail(ins.line, ins.text,
                 "%d-byte access at %s: it names a byte inside the big-endian "
                 "32-bit record based at %s, and only single-byte reads of "
                 "that record have been proven (storage.BE32_MEMBER)"
                 % (w, o.sym, _base))
        if not 0 <= _K < 4:
            fail(ins.line, ins.text,
                 "%s%+d is byte %d of the 4-byte record based at %s -- outside "
                 "it, so the mirror would be meaningless"
                 % (o.sym, o.disp, _K, _base))
        return 3 - 2 * _k0 - o.disp

    if storage.kind(o.sym) is None or o.swapped:
        return o.disp
    r = storage.resolve(o.sym, o.disp)
    if r is None:
        fail(ins.line, ins.text,
             "cannot resolve which label covers %s%+d (negative displacement, "
             "a support-data island, or past the end of the data image), so "
             "its storage is unknowable" % (o.sym, o.disp))
    lab, local = r
    knd, size = storage.kind(lab)
    delta = o.disp - local          # global disp = delta + corrected local
    if knd == "plain":
        return o.disp
    if knd == "split":
        fail(ins.line, ins.text,
             "%s stores dc.l content MIXED with raw bytes ('split' in the "
             "storage manifest); no access to it can be proven right" % o.sym)
    if knd == "be32":
        if w == 4:
            if local % 4 == 0 and local + 4 <= size:
                return o.disp
            fail(ins.line, ins.text,
                 "dword access at %s+%d is not one aligned big-endian group "
                 "of label %s -- an unaligned read straddling two swapped "
                 "groups has NO x86-equivalent value" % (o.sym, o.disp, lab))
        g = (local // 4) * 4
        k = local - g
        if k + w > 4 or g + 4 > size:
            fail(ins.line, ins.text,
                 "%d-byte access at %s+%d straddles a big-endian 4-byte "
                 "group boundary (or the group runs past label %s)"
                 % (w, o.sym, o.disp, lab))
        return delta + g + (4 - w - k)
    if knd == "be16":
        if w == 2:
            return o.disp
        if w == 1:
            g = (local // 2) * 2
            if g + 2 > size:
                fail(ins.line, ins.text,
                     "byte access at %s+%d: its word pair runs past label %s"
                     % (o.sym, o.disp, lab))
            return delta + g + 1 - (local - g)
        fail(ins.line, ins.text,
             "%d-byte access into be16 label %s: a dword over a word pair "
             "needs the SWAP only the MOV rules implement" % (w, lab))
    if knd == "le":
        if w == 1:
            return o.disp
        fail(ins.line, ins.text,
             "%d-byte access to %s, whose bytes are stored in x86 order "
             "('le'): needs the explicit byte-swap only the MOV rules "
             "implement, and this instruction is not one of them"
             % (w, o.sym))
    fail(ins.line, ins.text, "unknown storage kind %r for %s" % (knd, o.sym))


def ea(o, ins, want_size=4, pre=None):
    """Render an operand as a 68k effective address.

    `pre` collects setup lines the caller must emit BEFORE the instruction
    proper. It exists for the one addressing mode the 68k does not have: a
    memory base held in a data register (`[edx]`, `[eax+0x18]`), which becomes
    `movea.l dN,a0` and then an access through (disp,a0). A rule that passes no
    `pre` refuses that case instead of quietly reordering anything."""
    if isinstance(o, Reg):
        return reg68(o, ins, want_size)

    if isinstance(o, Imm):
        return imm68(o.value, want_size)

    if isinstance(o, Mem):
        if o.size not in (None, want_size):
            fail(ins.line, ins.text,
                 f"{o.size}-byte memory operand in a {want_size}-byte access")
        if o.index is not None:
            return _indexed_ea(o, ins, want_size, pre)
        if o.base is not None:
            if o.sym is not None:
                # `[edx+DATA_005058]`: a register plus a label is the
                # suppressed-base indexed form with a scale of 1 -- the same
                # 68020 mode the scaled forms already use, so it is rewritten
                # as index*1+symbol rather than given a second rule. The
                # register is the INDEX (x86 adds it whole, .l), the label is
                # the base displacement.
                from dataclasses import replace as _replace
                return _indexed_ea(_replace(o, base=None, index=o.base,
                                            scale=1), ins, want_size, pre)

            # THE SUB-WORD STACK ACCESS. Found by the oracle, on
            # FUNC_001612_DrawCircle at ffe.asm:96611:
            #
            #     mov al,[ebp+0x10]        ; the colour argument, low byte
            #
            # cdecl pushes a 4-BYTE slot. On x86 the low byte of that longword
            # is at displacement +0; on a 68k it is at +3, because the same
            # 32-bit value is stored the other way round. Emitting the same
            # displacement therefore reads the WRONG BYTE -- and it does it
            # silently: DrawCircle drew its circle in colour 0 instead of the
            # colour asked for, on exactly the rows that go through this path,
            # and every other row was right. 22 records out of 4037.
            #
            # The same applies to `word [ebp+0x8]` / `word [ebp+0xa]`, which is
            # one pushed longword read as two shorts: x86 gets (low, high) and a
            # 68k reading the same displacements gets (high, low).
            #
            # THIS IS NOT A CASE FOR A CLEVER DEFAULT. The right displacement
            # depends on what the slot HOLDS -- a 4-byte scalar needs 3-k, a
            # 4-char array needs k unchanged -- and nothing here knows which.
            # The provable case is a slot the same function also touches at full
            # dword width; implementing that is the next piece of work and it
            # needs the frame map threaded through this function. Until then
            # this refuses, loudly, which is the whole point of the refusal
            # discipline: a wrong answer that looks right is the one failure
            # mode this project cannot afford.
            if o.base in ("ebp", "esp") and want_size != 4 and not o.swapped:
                fail(ins.line, ins.text,
                     "%d-bit access to a stack slot at [%s%+d] that frame.py "
                     "could not prove is a 4-byte scalar. cdecl slots are 4 "
                     "bytes and the two machines store them in opposite byte "
                     "orders, so the SAME displacement names a different byte "
                     "here than it does on x86 -- but the correction is only "
                     "right if the slot really holds a scalar, and a 4-char "
                     "array would want the displacement left alone. Proof means "
                     "the same function also touching the slot at dword width; "
                     "this one does not, so it is refused rather than guessed."
                     % (want_size * 8, o.base, o.disp))

            # THE LOW BYTE OF A 16-BIT FIELD, REACHED THROUGH A POINTER.
            # x86 keeps a word's low half at offset +0; the 68k keeps it at
            # +1. So `mov al,[eax]` on a WORD field must read (1,a0), not
            # (a0) -- there is nothing to byte-swap in a single byte, only a
            # displacement to move, which is why this is a separate list from
            # le_read_sites.tsv.
            # Registered per ffe.asm line in tools\datamap\lo_byte_sites.tsv
            # because the operand carries no symbol for the manifest to type.
            # Found on FUNC_001330_DPalBuild: it takes the low byte of six
            # word-sized colour-box bounds that hold 0..0x10, so the HIGH byte
            # the 68k read instead was ALWAYS ZERO -- every palette entry came
            # out R=G=B and the whole 3D viewport rendered in greys while the
            # 2D console was in full colour. See the header of that file.
            # 2026-08-03: the same displacement question arises for a WORD
            # inside a DWORD field, so the site table now carries an explicit
            # delta and this admits width 2 as well. Only listed lines move,
            # and every line listed before today is a byte with delta +1, so
            # nothing already built changes.
            lo_shift = (storage.lo_byte_site(ins.line)
                        if (want_size in (1, 2) and not o.swapped
                            and o.base not in ("ebp", "esp")) else 0)

            b = XREG[o.base]
            if not is_areg(b):
                if pre is None:
                    fail(ins.line, ins.text,
                         f"memory base {o.base} maps to {b}, a data register, "
                         "and this form has nowhere to put the `movea.l dN,a0` "
                         "that would fix it (PLAN 3.1)")
                if any(_is_base_setup(l) for l in pre):
                    fail(ins.line, ins.text,
                         "two memory operands would both need the scratch "
                         "address register -- x86 does not have such a form, so "
                         "this is a parser bug rather than a missing rule")
                pre.append(op("movea.l", b, SCRATCH_A))
                b = SCRATCH_A
            d = o.disp + lo_shift
            return "(%d,%s)" % (d, b) if d else "(%s)" % b
        if o.sym is not None:
            d = _static_disp(o, ins, want_size)
            s = o.sym if not d else "%s%+d" % (o.sym, d)
            return "(%s).l" % s
        fail(ins.line, ins.text, "memory operand with neither base nor symbol")

    if isinstance(o, Sym):
        # A bare label as a SOURCE operand is its ADDRESS in NASM (a load is
        # spelled with brackets), i.e. an immediate the assembler fills in.
        # PLAN 5's invariant holds: a 68k assembler puts a 68k address there,
        # so no x86 address ever exists at run time. A Sym can never be a
        # destination -- x86 has no store-to-immediate -- so returning an
        # immediate form here cannot leak into a write.
        if want_size != 4:
            fail(ins.line, ins.text,
                 "a label address is 32 bits; this operation is narrower")
        return "#" + canon(o.name)

    fail(ins.line, ins.text, f"cannot use {o} as an effective address")


# Appended wherever an indexed memory operand had to build its base first.
#
# The 68020 addresses `(bd,An,Xn.SIZE*scale)` natively, with all four scales, so
# an x86 `[base+index*4+disp]` is ONE addressing mode here and not arithmetic --
# unlike the LEA case in lo_lea, where the result has to end up in a data
# register and the mode cannot deliver it. The only thing the mode insists on is
# that the BASE lives in an address register, which is why a base in eax/ebx/ecx
# /edx costs a MOVEA first (and MOVEA touches no condition codes, so it is safe
# anywhere).
#
# The suppressed-base form `[index*4+DATA_x]` -- the 35 jump tables and the
# lookup tables, 453 sites -- could use the 68020's own base-suppressed encoding
# `(DATA_x,ZA0,Dn.l*4)`. It does not: `lea (DATA_x).l,a0` followed by the
# ordinary mode is one extra instruction and stays inside the encoding this
# project has already shipped and verified. An untested encoding to save four
# bytes is the wrong trade here.
INDEX_NOTE = (
    "Indexed memory operand: the 68020 has `(bd,An,Xn.l*scale)` in hardware with "
    "scales 1/2/4/8, so this is one addressing mode rather than arithmetic. The "
    "mode needs its BASE in an address register -- hence the MOVEA when the x86 "
    "base is eax/ebx/ecx/edx, and the LEA when the 'base' is really a label. "
    "Neither touches the CCR (M68000PRM), so both are safe to insert between a "
    "flag setter and its consumer.")


def _indexed_ea(o, ins, want_size, pre):
    """`[base + index*scale + disp]` or `[index*scale + symbol]` as a real 68020
    addressing mode. See INDEX_NOTE."""
    if o.scale not in SCALE_SHIFT:
        fail(ins.line, ins.text, "index scale %d is not 1/2/4/8" % o.scale)
    if pre is None:
        fail(ins.line, ins.text,
             "indexed memory operand in a form with nowhere to put the base "
             "setup it may need")
    if o.base is not None and o.sym is not None:
        fail(ins.line, ins.text,
             "base register AND symbol AND index in one operand -- x86 has the "
             "form, no translated function has needed it, and it would want the "
             "68020 full format with both a base displacement and an outer one")

    idx = XREG[o.index]
    if is_areg(idx) and idx == "sp":
        fail(ins.line, ins.text, "esp cannot be an index register")

    if o.sym is not None:
        # `[edx*4+DATA_007975]`: the label IS the base. One LEA puts it in the
        # scratch address register and the ordinary indexed mode does the rest.
        #
        # Storage check with a RUNTIME index. Provable outright when the
        # access width AGREES with the storage -- dword into be32, word into
        # be16, byte into le/plain -- plus whatever a MOV rule marked
        # `swapped` because it owns the byte-swap itself.
        #
        # ONE more case is provable: a sub-word access whose index SCALE is a
        # whole number of storage groups (scale%4==0 into be32, %2 into be16).
        # Then the runtime index moves the access by whole groups and the
        # offset WITHIN the group is fixed by the constant displacement alone,
        # so the frame.py flip applies exactly as if there were no index.
        # Carrier: FUNC_000089's `test byte [esi*8+DATA_008574],0x2` -- the
        # stride-56 channel array, LSB of an LE flag dword, whole extent
        # be32 (DATA_008574 + DATA_008578, both 4-aligned resb). The flip
        # assumes every group the index can reach shares the storage of the
        # one the constant displacement names -- the same region convention
        # every indexed dword access into be32 already relies on.
        st = None if o.sym in LOCAL_TABLES else storage.kind(o.sym)
        if st is not None and not o.swapped:
            knd = st[0]
            ok = (knd == "plain"
                  or (knd == "be32" and want_size == 4)
                  or (knd == "be16" and want_size == 2)
                  or (knd == "le" and want_size == 1)
                  # a BYTE into 'split' storage is exact by construction:
                  # single bytes have no order, and the db parts of a split
                  # label are emitted raw (FUNC_000086's stride-16 flag
                  # test into DATA_002284 -- byte flags between dd pointers;
                  # reading a POINTER bytewise would be broken on x86 too)
                  or (knd == "split" and want_size == 1))
            if not ok and knd in ("be32", "be16"):
                g = 4 if knd == "be32" else 2
                if want_size < g and o.scale % g == 0:
                    r = storage.resolve(o.sym, o.disp)
                    if r is not None:
                        lab, local = r
                        cover = storage.kind(lab)
                        k = local % g
                        if cover and cover[0] == knd and k + want_size <= g:
                            o = replace(o, disp=o.disp + (g - want_size - 2 * k))
                            ok = True
            if not ok:
                fail(ins.line, ins.text,
                     "%d-byte RUNTIME-indexed access into %s whose storage is "
                     "'%s' -- the index is unknown here and its scale is not a "
                     "whole number of storage groups, so no displacement flip "
                     "can fix it, and only the MOV rules carry the byte-swap"
                     % (want_size, o.sym, knd))
        if any(_is_base_setup(l) or _is_index_setup(l) for l in pre):
            fail(ins.line, ins.text,
                 "two memory operands would both need the scratch address "
                 "register -- x86 has no such form, so this is a parser bug "
                 "rather than a missing rule")
        pre.append(op("lea", "(%s).l" % o.sym, SCRATCH_A))
        b = SCRATCH_A
    elif o.base is not None:
        b = XREG[o.base]
        if not is_areg(b):
            if any(_is_base_setup(l) or _is_index_setup(l) for l in pre):
                fail(ins.line, ins.text,
                     "two memory operands would both need the scratch address "
                     "register")
            pre.append(op("movea.l", b, SCRATCH_A))
            b = SCRATCH_A
    else:
        fail(ins.line, ins.text,
             "indexed operand with neither a base register nor a symbol")

    # THE SAME DISPLACEMENT QUESTION, ONE OPERAND SHAPE FURTHER OUT.
    # A sub-word access to a field of a RUN-TIME record is neither a stack slot
    # (frame.py) nor a named label (storage.py), so nothing types it -- and the
    # generator's records are full of packed floats stored as a DWORD and then
    # bumped at their HIGH half:
    #
    #     mov  [ebx+edi*4+0xe],eax            ; the packed float, a dword
    #     add  word [ebx+edi*4+0x10],byte +0x19   ; +25 to its EXPONENT
    #
    # On x86 the high half of a dword at +0xe is at +0x10; on a 68k it is at
    # +0xe. Emitting +0x10 unchanged adds 25 to the MANTISSA instead, silently.
    # frame.py already fixes exactly this idiom on the stack (`add word
    # [ebp-0x12],byte +0x32' over the dword at [ebp-0x14]); this is the same
    # correction for the indexed form, and it comes from the same per-line site
    # table with the same explicit delta, because the operand carries no symbol
    # and no frame slot for anything to type it from.
    lo_shift = (storage.lo_byte_site(ins.line)
                if want_size in (1, 2) and not o.swapped else 0)
    return "(%d,%s,%s.l*%d)" % (o.disp + lo_shift, b, idx, o.scale)


def _is_index_setup(line):
    """`lea (SYM).l,a0` -- the suppressed-base sequence from _indexed_ea()."""
    return (line.text.startswith("lea\t(")
            and line.text.endswith("," + SCRATCH_A))


def _is_base_setup(line):
    """`movea.l dN,a0` -- the memory-base sequence, as emitted by ea()."""
    return (line.text.startswith("movea.l\td")
            and line.text.endswith("," + SCRATCH_A))


def _is_areg_setup(line):
    """`move.l aN,d7` -- the address-register-operand sequence, from data_ea()."""
    return (line.text.startswith("move.l\ta")
            and line.text.endswith("," + SCRATCH_D))


def data_ea(o, ins, pre, size=4):
    """Like ea(), but for the 68k forms that forbid an address register operand:
    an esi/edi/ebp operand is copied into the scratch data register.

    The copy is always a LONG move even when the operation is narrower: the
    whole point is to get the address register's 32 bits somewhere legal, and
    a narrower move would leave the top of d7 holding the last rule's rubbish.
    x86 has no 8- or 16-bit name for esi/edi/ebp anyway, so this only ever
    happens at full width."""
    if isinstance(o, Reg) and o.size == 4 and is_areg(reg68(o, ins)):
        pre.append(op("move.l", reg68(o, ins), SCRATCH_D))
        return SCRATCH_D
    return ea(o, ins, want_size=size, pre=pre)


def dest_ea(o, ins, pre, size=4):
    """A destination that may be a register or memory, but not an immediate."""
    if isinstance(o, Reg):
        return reg68(o, ins, size)
    if isinstance(o, Mem):
        return ea(o, ins, want_size=size, pre=pre)
    fail(ins.line, ins.text, "expected a register or memory destination")


def mk(pre, lines, ccr_extra=F.NONE, ccr_wrong=F.NONE, why=""):
    """Assemble a Lowered from the setup lines ea()/data_ea() asked for and the
    instruction proper, adding the note that explains whichever setup ran."""
    notes = []
    if any(_is_base_setup(l) for l in pre):
        notes.append(BASE_NOTE)
    if any(_is_index_setup(l) for l in pre):
        notes.append(INDEX_NOTE)
    if any(_is_areg_setup(l) for l in pre):
        notes.append(AREG_NOTE)
    return Lowered(pre + lines, ccr_extra, ccr_wrong,
                   "\n".join(([why] if why else []) + notes))


def want_reg(o, ins, data_only=False):
    if not isinstance(o, Reg):
        fail(ins.line, ins.text, "expected a register operand")
    r = reg68(o, ins)
    if data_only and is_areg(r):
        fail(ins.line, ins.text,
             f"{o.name} maps to {r}; this form needs a data register, which "
             "would take a scratch move that is not emitted yet")
    return r


# ---- rules ------------------------------------------------------------------
#
# One function per mnemonic. `ctx` is unused for now but is where the a0/a1 base
# cache (PLAN 3.1) will hang once indexed addressing lands.

def lo_push(ctx, ins):
    (src,) = ins.ops
    if isinstance(src, Mem):
        pre = []
        sk = _swap_kind(src, 4)
        if sk:
            # `push dword [DATA_008818]` where the label's bytes are in x86
            # order: load, swap, push -- the debug-string pushes in _asmmain
            # and the screen-switch family go through here.
            s = ea(replace(src, swapped=True), ins, want_size=4, pre=pre)
            return mk(pre, [op("move.l", s, SCRATCH_D)]
                      + _bswap_ops(SCRATCH_D, 4, sk)
                      + [op("move.l", SCRATCH_D, "-(sp)")],
                      ccr_extra=F.ALL, why=LE_SWAP_NOTE)
        return mk(pre, [op("move.l", ea(src, ins, pre=pre), "-(sp)")],
                  ccr_extra=F.ALL,
                  why="no CCR-free push exists for a memory source, so MOVE.L is "
                      "used and the flags it writes must be dead.")
    if isinstance(src, Reg) and is_areg(reg68(src, ins)):
        # PEA pushes the effective address, which for (An) is An itself -- and
        # unlike MOVE it does not touch the CCR, exactly like the x86 PUSH.
        return Lowered([op("pea", "(%s)" % reg68(src, ins))],
                       why="x86 PUSH leaves the flags alone; PEA is the 68k "
                           "push that does the same (MOVE would set N/Z).")
    if isinstance(src, Sym):
        # `push dword DATA_x` -- the ADDRESS of a label, not its contents. This
        # is the single largest refusal class in the game (189 functions), and
        # the mapping is the same one `push ebp` already uses: PEA pushes an
        # effective address and, unlike MOVE.L, touches no condition codes at
        # all -- exactly like the x86 PUSH. So it is flag-EXACT and needs no
        # liveness argument.
        #
        # canon() for the same reason lo_call needs it: ffe.asm spells 278 of
        # the functions with a name attached, and a pushed FUNC_ address must be
        # the same symbol the definition emits or the link fails.
        #
        # A pushed FUNCTION address is allowed here on purpose even though the
        # indirect CALL that consumes it is not. PLAN 5's invariant holds either
        # way -- the assembler puts a 68k address there, so no x86 address ever
        # exists at run time -- and refusing at the push instead of at the call
        # would only move the refusal somewhere less informative. The target
        # is the bare body label, i.e. the t-ABI entry, matching lo_call.
        return Lowered([op("pea", "(%s).l" % canon(src.name))],
                       why="`push dword <label>` pushes the label's ADDRESS. PEA "
                           "does exactly that and, like the x86 PUSH, leaves the "
                           "CCR completely alone (MOVE.L would set N/Z and clear "
                           "V/C), so this is flag-exact rather than merely "
                           "value-exact.")

    if isinstance(src, Imm):
        if -32768 <= src.value <= 32767:
            return Lowered([op("pea", "(%d).w" % src.value)],
                           why="PEA of a short absolute address: pushes the "
                               "constant without touching the CCR.")
        return Lowered([op("pea", "($%08X).l" % (src.value & 0xFFFFFFFF))],
                       why="PEA of a long absolute address, CCR untouched.")
    return Lowered([op("move.l", ea(src, ins), "-(sp)")],
                   ccr_extra=F.ALL,
                   why="no CCR-free push exists for this source, so MOVE.L is "
                       "used and the flags it writes must be dead.")


def lo_pop(ctx, ins):
    (dst,) = ins.ops
    r = want_reg(dst, ins)
    if is_areg(r):
        # MOVEA never affects the CCR: an exact match for x86 POP.
        return Lowered([op("movea.l", "(sp)+", r)],
                       why="MOVEA does not touch the CCR, like x86 POP.")
    return Lowered([op("move.l", "(sp)+", r)], ccr_extra=F.ALL)


# The one sentence the 64-bit arithmetic rests on, so it is quoted rather than
# paraphrased. M68000 Programmer's Reference Manual, MOVE, Condition Codes:
#     "X Not affected.  N Set if the result is negative. Cleared otherwise.
#      Z Set if the result is zero. Cleared otherwise.  V Always cleared.
#      C Always cleared."
# and MOVEA, Condition Codes: "Not affected."
# X is written only by ADD/SUB/ADDX/SUBX/NEG/NEGX, the shifts and rotates
# through extend, and the BCD instructions. That is why `add.l` / `move.l` /
# `addx.l` carries the same bit across the store in the middle that x86's
# `add` / `mov` / `adc` does -- x86 MOV does not write CF either.
MOVE_X_NOTE = (
    "68k MOVE sets N/Z and clears V/C where x86 MOV sets nothing -- legal only "
    "while those four are dead. It does NOT touch X (M68000PRM, MOVE: `X Not "
    "affected'), so an ADD/MOVE/ADDX chain keeps its carry across the store in "
    "the middle exactly as the x86 does.")


# The other half of the byte story, and the reason 8-bit operands cost nothing
# here. M68000PRM, MOVE: for a byte operation on a data register destination
# "only the low-order byte is changed" -- bits 8..31 are left exactly as they
# were. That is the same guarantee x86 gives for `mov cl,x`, which writes cl and
# leaves the top 24 bits of ecx alone. So `cl` really is `d1` at .b width, with
# no masking, no extension and no scratch register anywhere.
BYTE_NOTE = (
    "BYTE WIDTH: cl is the low byte of ecx and d1's low byte is the low byte of "
    "d1 -- a 68k .b operation on a data register writes bits 0..7 and leaves "
    "8..31 untouched (M68000PRM), exactly as the x86 8-bit register does inside "
    "its 32-bit parent. N/Z come from the byte on both sides.")


# The word half of the same story, added by the DrawLine slice. M68000PRM, MOVE,
# for a word operation with a data register destination: "only the low-order
# word is changed". x86 `mov dx,[edi+2]` writes dx and leaves bits 16..31 of edx
# exactly as they were. Identical guarantee, so a 16-bit x86 register really is
# the same 68k data register at .w width -- no masking, no scratch, no
# extension. (Which is also why the upper half being stale afterwards is not a
# bug: DrawLine reads dx back through `movsx edx,dx`, and EXT.L rebuilds the top
# half from the sign of the low one on both machines alike.)
WORD_NOTE = (
    "WORD WIDTH: dx is the low word of edx and a 68k .w operation on a data "
    "register writes bits 0..15 and leaves 16..31 untouched (M68000PRM), which "
    "is exactly what the x86 16-bit register does inside its 32-bit parent. N/Z "
    "come from the word on both sides. Address registers are excluded from this "
    "rule -- see XREG16.")


# The byte-swap MOV shapes for labels stored 'le' (x86 byte order preserved --
# see tools\translator\storage.py). ROL.W #8 swaps the two bytes of the low
# word; ROL/SWAP/ROL reverses all four bytes of a longword. ROL writes N/Z/C
# and clears V where x86 MOV writes nothing -- the same spurious-flags
# declaration every MOV already makes -- and does NOT touch X (only the
# rotates THROUGH extend do, M68000PRM), so carry chains survive.
LE_SWAP_NOTE = (
    "This label's bytes are stored in x86 order ('le' in the storage "
    "manifest): byte readers elsewhere depend on that layout, so a wider "
    "access must swap explicitly. ROL.W #8 (and SWAP for a longword) reverses "
    "the bytes; the flags it writes are declared spurious exactly as MOV's "
    "own, and X is untouched.")


def _swap_kind(o, size):
    """None, or the swap the MOV shapes must own for this named static
    operand: 'le' (full byte reversal at width 2/4) or 'be16' (a dword over a
    big-endian word pair -- one SWAP exchanges the halves). The COVERING
    label decides, not the named one, except under a runtime index where
    only the named label is knowable."""
    if not (isinstance(o, Mem) and o.sym is not None and not o.swapped):
        return None
    if storage.kind(o.sym) is None:
        return None
    if o.index is None and o.base is None:
        r = storage.resolve(o.sym, o.disp)
        if r is None:
            return None                # ea() will refuse with the reason
        st = storage.kind(r[0])
    else:
        st = storage.kind(o.sym)
    if st[0] == "le" and size in (2, 4):
        return "le"
    if st[0] == "be16" and size == 4:
        return "be16"
    return None


# The labels of the CURRENT function's own embedded tables (set by
# emit.emit before lowering). References to them bind LOCALLY in the emitted
# object -- dc.l entries and raw dc.b bytes the translator itself wrote --
# so the storage manifest (which describes the support-file ORPHAN copy of
# the same name, often 'split') must not veto them: the local copy's layout
# is known exactly.
LOCAL_TABLES = set()


def _site_swap(ins, o, size):
    """'le' when this instruction is a REGISTERED pointer-based access to LE
    static data (tools\\datamap\\le_read_sites.tsv). Pointer operands carry no
    symbol for the storage manifest, so the site list names them by ffe.asm
    line; the width recorded there must match the access or the translation
    fails -- the guard against the list drifting from the disassembly."""
    # A NAMED operand is allowed here too, but only when the site list asks for
    # it by line. The manifest is per LABEL and is the right channel whenever a
    # label describes the whole access; it CANNOT describe a courier dword that
    # spans several labels. DATA_009217/9218/9219 are three consecutive
    # `resb 1' bytes (r,g,b of the current draw colour) that FUNC_001579 and
    # FUNC_001330 read WHOLE with `mov eax,[DATA_009217]' -- so the access is
    # four bytes while the label is one, and the same label is also read as a
    # plain byte by `cmp al,[DATA_009217]' at 93588, which must NOT swap. One
    # label, two widths, two different correct answers: only the line number
    # separates them. This is the same shape as DATA_007811, whose record base
    # is an earlier label than the bytes it spans.
    #
    # No double-swap risk: callers ask the manifest FIRST (`_swap_kind(...) or
    # _site_swap(...)'), so a label the manifest already types never reaches
    # here.
    if not isinstance(o, Mem):
        return None
    # `swapped' means two different things. On a pointer/named operand it
    # means "the bytes are already accounted for" and must stop a second swap.
    # On a STACK operand it is set by frame.apply() and means only "the
    # DISPLACEMENT is accounted for" (a PROVEN_SUBWORD pass-through or the
    # single-width rule) -- it says nothing about byte order. A registered
    # stack line with swapped=True therefore still gets its swap
    # (2026-09-25, FUNC_001703's `mov [ebp-0x3],ax', a word stored into a
    # by-value code that is later read as an x86 dword). Safe for everything
    # registered before: any such line that emitted no swap was already a
    # hard refusal from the audit in lower(), so none exists.
    if o.swapped and o.base not in ("ebp", "esp"):
        return None
    w = storage.le_site(ins.line)
    if w is None:
        return None
    if w != size:
        fail(ins.line, ins.text,
             "le_read_sites.tsv registers this line at width %d but the "
             "access is %d bytes -- the site list has drifted" % (w, size))
    return storage.le_site_kind(ins.line)


def _bswap_ops(reg, size, kind="le"):
    if kind == "be16":
        # Memory holds [X.be16][Y.be16]; the x86 dword is Y:X (Y in the high
        # half). MOVE.L reads X:Y, one SWAP makes it Y:X. Words are already
        # right, so no byte rotates.
        return [op("swap", reg)]
    if size == 4:
        return [op("rol.w", "#8", reg), op("swap", reg),
                op("rol.w", "#8", reg)]
    return [op("rol.w", "#8", reg)]


def lo_mov(ctx, ins):
    dst, src = ins.ops
    size = op_size(ins)
    sfx = SIZE_SFX[size]
    pre = []

    # `mov al,bh` -- an x86 HIGH byte (bits 8..15) as the SOURCE of a plain
    # byte move (ScrnShotWritePalette's green channel). The 68k has no
    # addressing mode for it, so the whole register shifts down in d7 and
    # the low byte lands normally. x86 MOV writes no flags; the detour's
    # writes are declared spurious as usual. A high-byte DESTINATION stays
    # refused -- nothing in the game needs it.
    if (isinstance(src, Reg) and src.name in ("ah", "bh", "ch", "dh")
            and isinstance(dst, Reg) and dst.size == 1
            and dst.name not in ("ah", "bh", "ch", "dh")):
        return Lowered([op("move.l", XREG["e" + src.name[0] + "x"], SCRATCH_D),
                        op("lsr.l", "#8", SCRATCH_D),
                        op("move.b", SCRATCH_D, reg68(dst, ins, 1))],
                       ccr_extra=F.ALL,
                       why="high-byte source: the parent register shifts "
                           "right 8 in d7 and the low byte moves out. All "
                           "flag writes declared spurious (x86 MOV writes "
                           "none) and checked dead.")

    # LOAD from a swap-needing label (or a registered LE pointer site):
    # swap after the load.
    sk = (_swap_kind(src, size) or _site_swap(ins, src, size)) \
        if isinstance(src, Mem) else None
    if sk and isinstance(dst, Reg):
        s = ea(replace(src, swapped=True), ins, want_size=size, pre=pre)
        if size == 4:
            d = reg68(dst, ins)
            if is_areg(d):
                return mk(pre, [op("move.l", s, SCRATCH_D)]
                          + _bswap_ops(SCRATCH_D, 4, sk)
                          + [op("movea.l", SCRATCH_D, d)],
                          ccr_extra=F.ALL, why=LE_SWAP_NOTE)
            return mk(pre, [op("move.l", s, d)] + _bswap_ops(d, 4, sk),
                      ccr_extra=F.ALL, why=LE_SWAP_NOTE)
        if dst.name in ("si", "di", "bp"):
            # Insert-low-word detour WITH a swap: the corrected word is built
            # in d6 first, then spliced into the An's low half through d7
            # (the plain insert-low-word shape). MOVEA writes no flags.
            a = XREG["e" + dst.name]
            return mk(pre, [op("move.w", s, SCRATCH_D2),
                            op("rol.w", "#8", SCRATCH_D2),
                            op("move.l", a, SCRATCH_D),
                            op("move.w", SCRATCH_D2, SCRATCH_D),
                            op("movea.l", SCRATCH_D, a)],
                      ccr_extra=F.ALL, why=LE_SWAP_NOTE + "\n" + WORD_NOTE)
        d = reg68(dst, ins, 2)
        return mk(pre, [op("move.w", s, d), op("rol.w", "#8", d)],
                  ccr_extra=F.ALL, why=LE_SWAP_NOTE + "\n" + WORD_NOTE)

    # STORE to a swap-needing label (or a registered LE pointer site):
    # swap the value first.
    dk = (_swap_kind(dst, size) or _site_swap(ins, dst, size)) \
        if isinstance(dst, Mem) else None
    if dk:
        if getattr(ctx, "live_after", F.NONE) & F.ALL:
            fail(ins.line, ins.text,
                 "swapped store with LIVE flags: the ROL/SWAP that reverses "
                 "the bytes writes the CCR and no MOVEM trick can hide it")
        d = ea(replace(dst, swapped=True), ins, want_size=size, pre=pre)
        if isinstance(src, Imm):
            v = src.value & (0xFFFFFFFF if size == 4 else 0xFFFF)
            if size == 4 and dk == "be16":
                sv = ((v & 0xFFFF) << 16) | (v >> 16)
                if sv >= 2 ** 31:
                    sv -= 2 ** 32
            elif size == 4:
                sv = (((v & 0xFF) << 24) | ((v >> 8 & 0xFF) << 16)
                      | ((v >> 16 & 0xFF) << 8) | (v >> 24 & 0xFF))
                if sv >= 2 ** 31:
                    sv -= 2 ** 32
            else:
                sv = ((v & 0xFF) << 8) | (v >> 8)
                if sv >= 2 ** 15:
                    sv -= 2 ** 16
            return mk(pre, [op("move" + sfx, "#%d" % sv, d)],
                      ccr_extra=F.ALL,
                      why=LE_SWAP_NOTE + "\nThe constant is byte-swapped at "
                          "TRANSLATE time, so the store itself is a plain "
                          "MOVE.")
        if isinstance(src, Reg):
            s = (XREG["e" + src.name]
                 if size == 2 and src.name in ("si", "di", "bp")
                 else reg68(src, ins, size))
            return mk(pre, [op("move" + sfx, s, SCRATCH_D)]
                      + _bswap_ops(SCRATCH_D, size, dk)
                      + [op("move" + sfx, SCRATCH_D, d)],
                      ccr_extra=F.ALL, why=LE_SWAP_NOTE)
        fail(ins.line, ins.text, "unsupported swapped-store source")
    # `mov esi,DATA_008804` -- a bare label as a source is its ADDRESS in NASM,
    # not its contents (a load is spelled `mov eax,[DATA_x]`). 136 functions
    # refuse on this and the mapping is already settled: it is exactly what
    # lo_lea does for `lea r,[sym]`, so the two share one argument rather than
    # inventing a second. This block sits ABOVE the address-register branch on
    # purpose: `mov esi,DATA` must reach the LEA here, not hand a bare Sym to
    # ea() below. A Sym source with a MEMORY destination falls through to the
    # generic store, where ea() renders the address as an immediate.
    if isinstance(src, Sym) and isinstance(dst, Reg):
        if size != 4:
            fail(ins.line, ins.text,
                 "a label address can only be moved into a 32-bit register")
        d = reg68(dst, ins)
        if is_areg(d):
            return Lowered([op("lea", "(%s).l" % canon(src.name), d)],
                           why="the ADDRESS of a label into an address "
                               "register. LEA touches no condition codes and "
                               "neither does x86 MOV, so this is flag-exact.")
        return Lowered([op("move.l", "#" + canon(src.name), d)], ccr_extra=F.ALL,
                       why="the ADDRESS of a label into a data register, as a "
                           "MOVE.L of that address as an immediate. PLAN 5's "
                           "invariant holds -- the assembler puts a 68k address "
                           "there, so no x86 address ever exists at run time.\n"
                           "MOVE.L sets N/Z and clears V/C where x86 MOV writes "
                           "nothing, so all four are declared spurious and the "
                           "liveness pass has to report them dead. The address-"
                           "register form above needs no such argument.")

    # Only a longword MOV can land in an address register: the 68k has no
    # MOVEA.B, and x86 has no 8-bit name for esi/edi/ebp either, so this can
    # only ever be the 32-bit case.
    if size == 4 and isinstance(dst, Reg) and is_areg(reg68(dst, ins)):
        return mk(pre, [op("movea.l", ea(src, ins, pre=pre), reg68(dst, ins))],
                  why="MOVEA does not touch the CCR at all, like x86 MOV.")

    # `mov di,[...]` -- a WORD landing in si/di, whose 68k home is an address
    # register. MOVEA.W would sign-extend through all 32 bits where the x86
    # leaves the top half alone, so the register is pulled into d7 whole, the
    # word is inserted there (a .w move on a DATA register writes bits 0..15
    # and leaves 16..31 -- the exact x86 semantics), and the whole goes back
    # through MOVEA.L.
    if (size == 2 and isinstance(dst, Reg)
            and dst.name in ("si", "di", "bp")):
        a = XREG["e" + dst.name]
        s = ea(src, ins, want_size=2, pre=pre)
        return mk(pre, [op("move.l", a, SCRATCH_D),
                        op("move.w", s, SCRATCH_D),
                        op("movea.l", SCRATCH_D, a)],
                  ccr_extra=F.ALL,
                  why="the insert-low-word detour: si lives in a2 and a "
                      "68k word op on an An sign-extends, so the insertion "
                      "happens in d7 where MOVE.W has the x86's exact "
                      "leave-the-top-half semantics. MOVE writes N/Z and "
                      "clears V/C where x86 MOV writes nothing -- declared "
                      "spurious, checked dead. MOVEA back writes none.")

    why = MOVE_X_NOTE
    if size == 1:
        why += "\n" + BYTE_NOTE
    elif size == 2:
        why += "\n" + WORD_NOTE
    if isinstance(dst, Reg):
        return mk(pre, [op("move" + sfx, ea(src, ins, want_size=size, pre=pre),
                           reg68(dst, ins, size))],
                  ccr_extra=F.ALL, why=why)
    if isinstance(dst, Mem):
        # The destination EA is built first so that, when the base needs the
        # scratch MOVEA, it lands before the store rather than between the two
        # halves of it. Only one of the two operands can be memory on x86.
        d = ea(dst, ins, want_size=size, pre=pre)
        live = getattr(ctx, "live_after", F.NONE)
        if (live & F.ALL) and size == 4 and isinstance(src, Reg):
            # (Any MOVEA/LEA the EA needed in `pre` is itself CCR-silent.)
            # A store with LIVE flags -- the dec/store/branch idiom
            # (`dec edx / mov [ebp+0x18],edx / jnz`). MOVE would overwrite
            # the Z the branch reads; MOVEM affects NO condition codes
            # (M68000PRM), exactly like the x86 MOV, so the one-register
            # MOVEM is the flag-exact spelling. Chosen only when a flag is
            # live, so every dead-flag site keeps MOVE and its old bytes.
            return mk(pre, [op("movem.l", reg68(src, ins), d)],
                      why="store with live flags: MOVEM.L of one register "
                          "writes no condition codes at all, matching x86 "
                          "MOV exactly. The ordinary MOVE would clobber the "
                          "Z/N a following branch reads.")
        if (isinstance(src, Reg) and src.size == 2
                and src.name in ("si", "di", "bp")):
            # `mov [ebx+4],si` -- storing the low word of an An. MOVE.W with
            # an ADDRESS-REGISTER SOURCE is legal (only the destination side
            # forbids An for word moves) and reads exactly the low word,
            # which IS si. N/Z from that word, V/C cleared -- the usual MOV
            # declaration.
            s = XREG["e" + src.name]
        else:
            s = ea(src, ins, want_size=size, pre=pre)
        return mk(pre, [op("move" + sfx, s, d)],
                  ccr_extra=F.ALL, why=why)
    fail(ins.line, ins.text, "unsupported MOV form")


# Sign-extending a byte and sign-extending a word are the same shape with a
# different instruction at the end. EXT.L takes the low WORD through the whole
# longword; EXTB.L takes the low BYTE -- a 68020 addition, which this build
# targets formally, so it is available.
EXT_MOVE = {1: "move.b", 2: "move.w"}
EXT_TO_LONG = {1: "extb.l", 2: "ext.l"}

MOVSX_NOTE = (
    "`movsx r32,<byte|word> src` is a load followed by a sign extension, and "
    "the 68k spells it as exactly that: MOVE into the low half, then EXT.L "
    "(from a word) or EXTB.L (from a byte) to fill the rest from the sign bit. "
    "Both are defined as sign-extending through the whole longword (M68000PRM), "
    "so the result is bit-identical to the x86's. EXTB.L is a 68020 addition, "
    "which is the formal target here.\n"
    "The order matters and is not interchangeable with the 8-bit case: the MOVE.W "
    "leaves bits 16..31 holding whatever was there before, and it is the EXT.L "
    "that makes them right. Emitting the MOVE.W alone would produce a value that "
    "is correct in its low half and silently wrong in its high half -- the exact "
    "shape of bug this translator exists to refuse.\n"
    "x86 MOVSX writes NO flags at all, while MOVE.W writes N/Z and clears V/C and "
    "EXT.L does the same, so all four are declared spurious and the liveness pass "
    "has to report them dead -- the same argument already used for 32-bit MOV. "
    "Neither instruction touches X.")


def lo_movsx(ctx, ins):
    """`movsx r32, r16` / `movsx r32, word [mem]`. 47 sites in DrawLine alone."""
    dst, src = ins.ops
    if not isinstance(dst, Reg) or dst.size != 4:
        fail(ins.line, ins.text,
             "MOVSX with a destination that is not a 32-bit register")
    d = reg68(dst, ins, 4)
    pre = []

    # A registered LE pointer site: load the word, swap its bytes in the
    # scratch register, THEN sign-extend -- the extension must see the
    # corrected value. Same declaration as the plain form (all spurious).
    if (isinstance(src, Mem) and src.size == 2
            and (_swap_kind(src, 2) or _site_swap(ins, src, 2))):
        s = ea(replace(src, swapped=True), ins, want_size=2, pre=pre)
        seq = [op("move.w", s, SCRATCH_D),
               op("rol.w", "#8", SCRATCH_D),
               op("ext.l", SCRATCH_D)]
        if is_areg(d):
            return mk(pre, seq[:2] + [op("movea.w", SCRATCH_D, d)],
                      ccr_extra=F.ALL, why=MOVSX_NOTE + "\n" + LE_SWAP_NOTE)
        return mk(pre, seq + [op("move.l", SCRATCH_D, d)],
                  ccr_extra=F.ALL, why=MOVSX_NOTE + "\n" + LE_SWAP_NOTE)

    # An ADDRESS-REGISTER destination is the better case, not the harder one.
    # M68000PRM, MOVEA: "Word-size source operands are sign-extended to 32-bit
    # quantities", and the whole register is written -- which is the definition
    # of MOVSX. And MOVEA's condition codes are "Not affected", matching x86
    # MOVSX, which writes no flags either. So this is one instruction and
    # flag-EXACT, where the data-register form needs two and a liveness proof.
    if is_areg(d):
        ssize = src.size if isinstance(src, (Reg, Mem)) and src.size else None
        if ssize not in (1, 2):
            fail(ins.line, ins.text,
                 "MOVSX needs a byte or word source (memory sources need the "
                 "explicit NASM size prefix)")
        if ssize == 1:
            # MOVEA.W only sign-extends a WORD; a byte source is extended in
            # the scratch data register and MOVEA'd over whole. The
            # destination is written last, so it may appear in the source EA.
            s = (reg68(src, ins, 1) if isinstance(src, Reg)
                 else ea(src, ins, want_size=1, pre=pre))
            return mk(pre, [op("move.b", s, SCRATCH_D),
                            op("extb.l", SCRATCH_D),
                            op("movea.l", SCRATCH_D, d)],
                      ccr_extra=F.ALL, why=MOVSX_NOTE)
        if isinstance(src, Reg):
            s = reg68(src, ins, 2)
        elif isinstance(src, Mem):
            s = ea(src, ins, want_size=2, pre=pre)
        else:
            fail(ins.line, ins.text, "unsupported MOVSX source")
        return mk(pre, [op("movea.w", s, d)],
                  why="`movsx aN,word src` is a single MOVEA.W: the 68k defines "
                      "a word move into an ADDRESS register as sign-extending "
                      "the source through all 32 bits (M68000PRM, MOVEA), which "
                      "is exactly what MOVSX does, and MOVEA's condition codes "
                      "are `Not affected', exactly as x86 MOVSX writes none.\n"
                      "So this form is flag-EXACT and needs no liveness argument "
                      "at all -- unlike the data-register form below, which "
                      "costs a MOVE.W plus an EXT.L and has to prove N/Z/V/C "
                      "dead. It is also the reason a 16-bit MOV into an address "
                      "register is REFUSED (see XREG16): that same sign "
                      "extension is wrong for `mov si,x` and right here.")


    if isinstance(src, Reg):
        if src.size not in (1, 2):
            fail(ins.line, ins.text,
                 f"MOVSX from the {src.size*8}-bit register {src.name!r}")
        if src.size == 2 and src.name in ("si", "di", "bp"):
            # `movsx eax,di` -- the low word of an An. MOVE.W with an
            # ADDRESS-REGISTER SOURCE is legal and reads exactly the low
            # word (the same PRM fact as the `mov [mem],si` store), then
            # EXT.L in the destination. Flags declared as the others.
            return mk(pre, [op("move.w", XREG["e" + src.name], d),
                            op("ext.l", d)],
                      ccr_extra=F.ALL, why=MOVSX_NOTE)
        s = reg68(src, ins, src.size)   # refuses ah/ch/dh/bh and sp
        # `movsx edx,dx` is the common case and needs no move at all: the value
        # is already in the low half of the destination.
        lines = ([] if s == d else [op(EXT_MOVE[src.size], s, d)])             + [op(EXT_TO_LONG[src.size], d)]
    elif isinstance(src, Mem):
        if src.size not in (1, 2):
            fail(ins.line, ins.text,
                 "MOVSX from memory without an explicit `byte` or `word` size "
                 "prefix -- the width of a sign extension is never inferred here")
        lines = [op(EXT_MOVE[src.size],
                    ea(src, ins, want_size=src.size, pre=pre), d),
                 op(EXT_TO_LONG[src.size], d)]
    else:
        fail(ins.line, ins.text, "unsupported MOVSX source")

    return mk(pre, lines, ccr_extra=F.ALL, why=MOVSX_NOTE)


MOVZX_NOTE = (
    "`movzx r32,<byte|word> src` is a load with a ZERO extension, and the 68k "
    "of this era has no single instruction for it (MVZ is ColdFire): it is "
    "MOVEQ #0 to clear the whole destination, then MOVE.B/MOVE.W into the low "
    "part, which the M68000PRM defines as leaving the rest untouched. The "
    "order is load-bearing twice over: the clear must come first, and when "
    "the source is the destination's own low part the clear would destroy it, "
    "so that case is an AND.L mask instead.\n"
    "x86 MOVZX writes NO flags; MOVEQ, MOVE and AND all write N/Z and clear "
    "V/C, so all four are declared spurious and the liveness pass has to "
    "report them dead -- the same argument as 32-bit MOV. None touches X.")


def lo_movzx(ctx, ins):
    dst, src = ins.ops
    if not isinstance(dst, Reg) or dst.size != 4:
        fail(ins.line, ins.text,
             "MOVZX with a destination that is not a 32-bit register")
    d = reg68(dst, ins, 4)
    ssize = src.size if isinstance(src, (Reg, Mem)) and src.size else None
    if ssize not in (1, 2):
        fail(ins.line, ins.text, "MOVZX needs a byte or word source")

    # A registered LE pointer site: zero-extend in the scratch register with
    # the byte swap between load and use, so the hazard cases below cannot
    # interact with it. ROL.W only touches the low word; the cleared upper
    # half survives.
    if (isinstance(src, Mem) and ssize == 2
            and (_swap_kind(src, 2) or _site_swap(ins, src, 2))):
        pre = []
        s = ea(replace(src, swapped=True), ins, want_size=2, pre=pre)
        seq = [op("moveq", "#0", SCRATCH_D),
               op("move.w", s, SCRATCH_D),
               op("rol.w", "#8", SCRATCH_D)]
        last = (op("movea.l", SCRATCH_D, d) if is_areg(d)
                else op("move.l", SCRATCH_D, d))
        return mk(pre, seq + [last],
                  ccr_extra=F.ALL, why=MOVZX_NOTE + "\n" + LE_SWAP_NOTE)

    # An address-register destination: MOVEA.W would SIGN-extend (that is
    # lo_movsx's single-instruction case), so the zero-extension is built in
    # the scratch data register and MOVEA'd over whole. The destination is
    # written LAST, so it may freely appear in the source EA.
    if is_areg(d):
        pre = []
        s = ea(src, ins, want_size=ssize, pre=pre)
        return mk(pre, [op("moveq", "#0", SCRATCH_D),
                        op(EXT_MOVE[ssize], s, SCRATCH_D),
                        op("movea.l", SCRATCH_D, d)],
                  ccr_extra=F.ALL, why=MOVZX_NOTE)

    # `movzx eax,di` -- the low word of an An. MOVE.W with an An SOURCE is
    # legal, so it is MOVEQ #0 then MOVE.W aN,dN; the destination register
    # can never BE the An source, so the clear-first hazard does not arise.
    if (isinstance(src, Reg) and src.size == 2
            and src.name in ("si", "di", "bp")):
        return Lowered([op("moveq", "#0", d),
                        op("move.w", XREG["e" + src.name], d)],
                       ccr_extra=F.ALL, why=MOVZX_NOTE)

    # `movzx eax,al` / `movzx eax,ax`: the source IS the destination's low
    # part, so clearing first would destroy it. Mask instead. The source is
    # rendered at ITS OWN width -- al and ax both land on d0 -- and compared
    # against the 32-bit destination register.
    if isinstance(src, Reg) and reg68(src, ins, ssize) == d:
        mask = "#255" if ssize == 1 else "#65535"
        return Lowered([op("and.l", mask, d)], ccr_extra=F.ALL, why=MOVZX_NOTE)

    pre = []
    s = ea(src, ins, want_size=ssize, pre=pre)
    mask = "#255" if ssize == 1 else "#65535"

    # THE SECOND MISCOMPILE THE ORACLE CAUGHT (FUNC_001597, ffe.asm:94076):
    # `movzx eax,byte [eax*2+DATA_007751]` -- the destination register is the
    # INDEX of the source EA, and clearing it first reads byte 0 for every
    # input. When the destination appears anywhere in the source address,
    # load FIRST (the EA is evaluated before the destination byte is
    # written), then mask. MOVSX never had the hazard: it loads first by
    # construction.
    if isinstance(src, Mem) and d in (XREG.get(src.base), XREG.get(src.index)):
        return mk(pre, [op(EXT_MOVE[ssize], s, d),
                        op("and.l", mask, d)],
                  ccr_extra=F.ALL, why=MOVZX_NOTE)

    return mk(pre, [op("moveq", "#0", d),
                    op(EXT_MOVE[ssize], s, d)],
              ccr_extra=F.ALL, why=MOVZX_NOTE)


def lo_nop(ctx, ins):
    return Lowered([op("nop")],
                   why="a NOP that survived the padding stripper is one the "
                       "code can actually reach; neither machine's NOP touches "
                       "a flag or a register.")


def lo_cdq(ctx, ins):
    """`cdq` -- sign-extend eax through edx, i.e. d0 through d2."""
    if ins.ops:
        fail(ins.line, ins.text, "CDQ takes no operands")
    return Lowered(
        [op("move.l", XREG["eax"], XREG["edx"]),
         op("moveq", "#31", SCRATCH_D),
         op("asr.l", SCRATCH_D, XREG["edx"])],
        ccr_extra=F.ALLX,
        why="CDQ fills edx with copies of eax's sign bit, which is what turns a "
            "32-bit value into the 64-bit dividend the one-operand IDIV wants. "
            "d2 = d0 >> 31 arithmetically is the same thing; the shift count "
            "goes through the scratch register because 68k immediate shift "
            "counts stop at 8.\n"
            "This is NOT fused with the IDIV that follows it. Recognising the "
            "pair and emitting a plain 32/32 DIVS.L would be three instructions "
            "cheaper, but it would only be correct if edx were dead afterwards -- "
            "and this translator has no register liveness, only flag liveness. "
            "Emitting what the x86 actually does needs no such proof.\n"
            "x86 CDQ writes no flags; MOVE and ASR between them write all four "
            "and ASR writes X as well, so all five are declared spurious and "
            "checked dead.")


def lo_idiv(ctx, ins):
    if len(ins.ops) != 1:
        fail(ins.line, ins.text, "only the one-operand IDIV is handled yet")
    (src,) = ins.ops
    pre = []
    if isinstance(src, Reg) and is_areg(reg68(src, ins)):
        # DIVS.L cannot read an An: same scratch detour as the one-operand
        # IMUL, proven on the quad family's `imul esi`.
        pre.append(op("move.l", reg68(src, ins), SCRATCH_D))
        s = SCRATCH_D
    else:
        s = ea(src, ins, pre=pre)
    return mk(pre,
        [op("divs.l", s, "d2:d0")],
        ccr_wrong=F.ALLX,
        why="one-operand IDIV is a SIGNED edx:eax / src with the quotient in eax "
            "and the remainder in edx; DIVS.L <ea>,Dr:Dq is the identical "
            "operation with the identical operand roles (Dr=d2, Dq=d0).\n"
            "Same two caveats as the unsigned form. x86 raises #DE when the "
            "quotient does not fit or the divisor is zero and the 68k merely sets "
            "V -- in DrawLine neither is reachable: the dividend is a 16-bit "
            "difference shifted left 16, so |dividend| <= 0x7FFF0000, and the "
            "divisor is a y difference the code has already proved non-zero (the "
            "`jz` at ffe.asm:96818) and then forced positive, so |quotient| <= "
            "0x7FFF0000 and fits. And the 64-bit DIVS.L form is not implemented "
            "in 68060 hardware; acceptable on the formal 68020 this build "
            "targets, and PLAN 3.4's ffe060 replaces it.")


def lo_imul(ctx, ins):
    # The 2- and 3-operand forms are the ordinary truncating multiply: the
    # result is 32 bits and the high half is thrown away, which is exactly
    # MULS.L <ea>,Dn. Only the ONE-operand form produces the 64-bit product,
    # and only that one needs the Dh:Dl pair.
    if len(ins.ops) in (2, 3):
        dst = ins.ops[0]
        pre = []
        if not isinstance(dst, Reg):
            fail(ins.line, ins.text,
                 "IMUL needs a register destination")
        d = reg68(dst, ins)
        srcs = ins.ops[1:]
        lines = []
        post = []
        if is_areg(d):
            # MULS.L cannot write an An: compute in d7 and MOVEA over. The
            # 2-operand form READS its destination, so d7 is seeded from the
            # An first; the 3-operand form overwrites it anyway.
            if len(srcs) == 1:
                lines.append(op("move.l", d, SCRATCH_D))
            post = [op("movea.l", SCRATCH_D, d)]
            d = SCRATCH_D
        if len(srcs) == 2:
            # `imul dst,src,imm` -- dst = src * imm, dst is not read.
            lines.append(op("move.l", ea(srcs[0], ins, pre=pre), d))
            factor = srcs[1]
        else:
            factor = srcs[0]
        if isinstance(factor, Reg) and is_areg(reg68(factor, ins)):
            # MULS.L cannot read an An, so the factor detours through d7 --
            # unless d7 IS the target (areg destination): then the seed above
            # already put the destination's value there, which is the factor
            # exactly when dst == factor (`imul esi,esi`, squaring d7);
            # a DIFFERENT An factor would need a second scratch and is
            # refused until something needs it.
            if d != SCRATCH_D:
                lines.append(op("move.l", reg68(factor, ins), SCRATCH_D))
                f = SCRATCH_D
            elif reg68(factor, ins) == reg68(dst, ins):
                f = SCRATCH_D
            else:
                # `imul esi,edi` -- An destination (seeded into d7 above) AND
                # a DIFFERENT An factor. The second scratch data register
                # carries the factor, exactly as `or esi,edi` already does.
                lines.append(op("move.l", reg68(factor, ins), SCRATCH_D2))
                f = SCRATCH_D2
        else:
            f = ea(factor, ins, pre=pre)
        lines.append(op("muls.l", f, d))
        return mk(pre, lines + post, ccr_wrong=F.ALLX,
                  why="the 2- and 3-operand IMUL keep only the low 32 bits of "
                      "the product, which is what MULS.L <ea>,Dn computes -- no "
                      "register pair and no 64-bit result.\n"
                      "x86 sets CF/OF when the full product does not fit in 32 "
                      "bits and leaves SF/ZF UNDEFINED; the 68k sets N/Z from "
                      "the result and V on the same overflow condition. Close, "
                      "but not provably identical, so all five are declared "
                      "unreliable and the liveness pass has to report them dead.")

    if len(ins.ops) != 1:
        fail(ins.line, ins.text, "unsupported IMUL form")
    (src,) = ins.ops
    if isinstance(src, Reg) and is_areg(reg68(src, ins)):
        # MULS.L cannot take an An source. MOVE.L An,d7 first: it disturbs
        # N/Z/V/C, but the MULS that follows rewrites everything this unit
        # declares anyway, so the flag story below is unchanged.
        return Lowered(
            [op("move.l", reg68(src, ins), "d7"),
             op("muls.l", "d7", "d2:d0")],
            ccr_wrong=F.ALL,
            why="one-operand IMUL with its source in an address register: "
                "MULS.L cannot read an An, so the value detours through the "
                "scratch d7. Flag story identical to the plain form below.")
    pre = []
    return mk(pre,
        [op("muls.l", ea(src, ins, pre=pre), "d2:d0")],
        ccr_wrong=F.ALL,
        why="one-operand IMUL is edx:eax = eax * src, a signed 32x32->64 "
            "multiply; MULS.L <ea>,Dh:Dl is the same operation with the same "
            "operand roles (Dh=d2=edx high, Dl=d0=eax low).\n"
            "NOTE for the planned 060 build: this 64-bit MULS.L form is NOT "
            "implemented in 68060 hardware and traps to the 68060 support "
            "package. Acceptable on the formal 68020 we target; PLAN 3.4 "
            "replaces it with a mulu.w helper for ffe060.\n"
            "x86 leaves SF/ZF undefined here and defines CF/OF differently from "
            "68k V; all four are checked dead.")


def lo_mul(ctx, ins):
    if len(ins.ops) != 1:
        fail(ins.line, ins.text, "only the one-operand MUL is handled yet")
    (src,) = ins.ops
    pre = []
    if isinstance(src, Reg) and is_areg(reg68(src, ins)):
        # MULU.L cannot read an An: same d7 detour as the signed twin.
        pre.append(op("move.l", reg68(src, ins), SCRATCH_D))
        s = SCRATCH_D
    else:
        s = ea(src, ins, pre=pre)
    return mk(pre,
        [op("mulu.l", s, "d2:d0")],
        ccr_wrong=F.ALL,
        why="one-operand MUL is edx:eax = eax * src, an UNSIGNED 32x32->64 "
            "multiply; MULU.L <ea>,Dh:Dl is the same operation with the same "
            "operand roles (Dh=d2, Dl=d0) -- the unsigned twin of the IMUL "
            "below, with the same 68060 caveat (the 64-bit form traps to the "
            "support package there; fine on the formal 68020 target).\n"
            "x86 MUL sets CF/OF when the high half is non-zero and leaves "
            "SF/ZF undefined; the 68k sets N/Z from the 64-bit result and "
            "clears V/C. Nothing matches, so all four are declared unreliable "
            "and the liveness pass has to report them dead.")


def lo_div(ctx, ins):
    if len(ins.ops) != 1:
        fail(ins.line, ins.text, "only the one-operand DIV is handled yet")
    (src,) = ins.ops
    pre = []
    if isinstance(src, Reg) and is_areg(reg68(src, ins)):
        # `div edi` (StringConvertTime's 60/3600 splits): DIVU.L refuses an
        # An source, so the divisor detours through d7. MOVE.L writes N/Z --
        # irrelevant, the DIVU immediately rewrites the CCR and the whole
        # set is declared unreliable anyway (same as the MUL detour above).
        pre.append(op("move.l", reg68(src, ins), SCRATCH_D))
        s = SCRATCH_D
    else:
        s = ea(src, ins, pre=pre)
    return mk(pre,
        [op("divu.l", s, "d2:d0")],
        ccr_wrong=F.ALL,
        why="one-operand DIV is an unsigned edx:eax / src with the quotient in "
            "eax and the remainder in edx; DIVU.L <ea>,Dr:Dq is the identical "
            "operation -- Dr=d2 is both the high half of the dividend and the "
            "remainder, Dq=d0 both the low half and the quotient.\n"
            "Divergence to know about: x86 raises #DE when the quotient does "
            "not fit (or the divisor is zero); the 68k sets V and carries on. "
            "That only differs on paths where the original would have crashed.")


# What the register form of a 68k shift does with its count, quoted from the
# M68000PRM (ASR, and identically for ASL/LSL/LSR):
#     "1. Immediate -- The shift count is specified in the instruction (shift
#         range 1-8).
#      2. Register -- The shift count is the value in the data register
#         specified in instruction modulo 64."
# x86 masks a 32-bit shift count to FIVE bits, so the two agree for every count
# whose bit 5 is clear and disagree for counts 32..63 (and 96..127, ...): x86
# shifts by count-32, the 68k by the full count. There is no cheap way to
# reproduce the x86 masking, and PLAN 3.6 accepts the difference as the same
# class of risk the C source had; this note is the lint that goes with it.
COUNT_NOTE = (
    "COUNT IN cl: x86 masks a shift count to FIVE BITS for byte, word and dword "
    "operands alike, while the 68k takes the count register MODULO 64. They "
    "agree for 0..31 and diverge for 32..63, so the count is masked with "
    "`and.l #31' into a scratch register before the shift -- this is the whole "
    "of PLAN 3.6's accepted risk, and it is now closed rather than accepted. "
    "The mask goes to a scratch because ecx is live afterwards at most of the "
    "360 sites (FUNC_001751 reloads it between two shifts, but FUNC_001492 does "
    "not). And for a count of ZERO x86 leaves every flag alone where the 68k "
    "does not -- which is why all four are declared unreliable rather than "
    "just V.")


def _shift_lines(mnem, reg, count):
    """The shift itself, on a data register. `count` is ('imm', n) or ('reg', r)."""
    if count[0] == "reg":
        # THE MASK IS NOT OPTIONAL, and it was a live bug rather than a
        # theoretical one: FUNC_001492 computes cl = 31 - b with NO guard, so
        # any negative b makes the count 32 or more. x86 then shifts by
        # (31-b) & 31 while the 68k shifts by (31-b) mod 64 -- for b = -1 that
        # is "no shift at all" against "shift out every bit". The oracle's own
        # comment named this predicate exactly and steered its vectors around
        # it; the vectors at the end of the list now walk straight through it.
        #
        # d6 rather than d7 when d7 already holds the VALUE (memory or
        # address-register destination). SCRATCH_D2 is the register the
        # `or esi,edi' shape already uses, on the same grounds: no translated
        # code maps to d4-d6, and the C ABI preserves them across any call a
        # lowered unit could make.
        tmp = SCRATCH_D2 if reg == SCRATCH_D else SCRATCH_D
        if os.environ.get("FFE_NO_SHIFT_MASK"):
            # ISOLATION SWITCH, not a supported mode, and it earned its keep the
            # day it was written: a game build made WITH the mask reached fewer
            # frames than the milestone binary, and the obvious reading was that
            # the rule had broken something. It had not. Rebuilding with this
            # set, then diffing the two link maps object by object, showed every
            # single size difference to be +-20 or +-24 bytes -- the size of an
            # inject_count payload -- and NOT ONE of the 81 functions that
            # contain a cl-count shift among them. The translation was identical
            # to the milestone's; what varied was the probe set and the frame
            # the run happened to be on. Keep the switch: an attribution like
            # that is cheap with it and guesswork without it.
            return [op(mnem, count[1], reg)]
        return [op("move.l", count[1], tmp),
                op("and.l", "#31", tmp),
                op(mnem, tmp, reg)]
    n = count[1]
    if n <= 8:
        return [op(mnem, "#%d" % n, reg)]
    if reg != SCRATCH_D:
        # 68k immediate shift counts stop at 8; anything larger goes through the
        # scratch data register (PLAN 3.1). MOVEQ writes N/Z/V/C, but the shift
        # that follows overwrites all four, so nothing can observe it.
        return [op("moveq", "#%d" % n, SCRATCH_D), op(mnem, SCRATCH_D, reg)]
    # The value being shifted is ITSELF in the only scratch data register (a
    # memory destination, or an esi/edi operand), so the count has nowhere to
    # go. Shift in chunks of eight instead: repeated shifts compose exactly, and
    # the last chunk leaves the same bit in X and C as one big shift would.
    lines = []
    while n:
        k = min(n, 8)
        lines.append(op(mnem, "#%d" % k, reg))
        n -= k
    return lines


def _shift(ctx, ins, mnem, why):
    dst, cnt = ins.ops
    # The width comes from the DESTINATION alone. op_size() cannot be used here
    # because `shl eax,cl` has a 32-bit destination and an 8-bit count, and the
    # count is not an operand of the shift in the width sense -- it says how far,
    # not how wide. Reading it as a width made op_size refuse the whole
    # instruction as "operands of different widths".
    if isinstance(dst, Reg):
        size = dst.size
    elif isinstance(dst, Mem):
        size = dst.size or 4
    else:
        fail(ins.line, ins.text, "unsupported shift destination")
    sfx = SIZE_SFX[size]
    mnem = mnem.split(".")[0] + sfx

    if isinstance(cnt, Reg) and cnt.name == "cl":
        count, wrong, why = ("reg", XREG["ecx"]), F.ALL, why + "\n" + COUNT_NOTE
    elif isinstance(cnt, Imm) and 1 <= cnt.value <= 31:
        count, wrong = ("imm", cnt.value), frozenset(F.V)
    elif isinstance(cnt, Imm) and cnt.value == 0:
        # `shr eax,0x0` (FUNC_000078's colour unpack, the 0-bit channel): an
        # x86 shift by a ZERO immediate moves nothing and writes NO flags at
        # all -- emitting nothing is flag-exact and value-exact.
        return Lowered([], why="shift by an immediate 0: x86 defines it as a "
                               "complete no-op (no value change, no flag "
                               "writes), so nothing is emitted.")
    else:
        fail(ins.line, ins.text,
             f"{ins.mnem} count must be an immediate in 1..31 or the register cl")

    pre = []
    if isinstance(dst, Reg):
        r = reg68(dst, ins, size)
        if not is_areg(r):
            return mk(pre, _shift_lines(mnem, r, count), ccr_wrong=wrong, why=why)
        # esi/edi/ebp map to address registers and the 68k shifts only data
        # registers and memory. Out and back through the scratch: the MOVEA on
        # the way back does not touch the CCR, so the shift's own flags are
        # still there afterwards.
        return mk(pre,
                  [op("move.l", r, SCRATCH_D)]
                  + _shift_lines(mnem, SCRATCH_D, count)
                  + [op("movea.l", SCRATCH_D, r)],
                  ccr_wrong=wrong, why=why + "\n" + AREG_NOTE)

    if isinstance(dst, Mem):
        d = ea(dst, ins, want_size=size, pre=pre)
        return mk(pre,
                  [op("move" + sfx, d, SCRATCH_D)]
                  + _shift_lines(mnem, SCRATCH_D, count)
                  + [op("move" + sfx, SCRATCH_D, d)],
                  ccr_wrong=wrong | F.ALL,
                  why=why + "\nMEMORY DESTINATION: the 68k shifts memory only by "
                            "one and only a word at a time, so this is load / "
                            "shift / store through d7. The store overwrites "
                            "N/Z/V/C -- all four declared unreliable -- while X "
                            "survives, because MOVE does not touch it.")

    fail(ins.line, ins.text, "unsupported shift destination")


def lo_shl(ctx, ins):
    return _shift(ctx, ins, "lsl.l",
                  "C and X both take the bit shifted out, matching x86 CF. "
                  "68k LSL clears V where x86 SHL sets OF from the top bit; V "
                  "is checked dead.")


def lo_shr(ctx, ins):
    return _shift(ctx, ins, "lsr.l",
                  "logical right shift; C/X take the last bit shifted out, "
                  "matching x86 CF. V differs as for SHL and is checked dead.")


def lo_sar(ctx, ins):
    return _shift(ctx, ins, "asr.l",
                  "arithmetic right shift; C/X take the last bit shifted out.")


def lo_rcl(ctx, ins):
    dst, cnt = ins.ops
    r = want_reg(dst, ins, data_only=True)
    if not isinstance(cnt, Imm) or cnt.value != 1:
        fail(ins.line, ins.text, "only RCL by 1 is handled yet")
    return Lowered(
        [op("roxl.l", "#1", r)],
        ccr_extra=frozenset(F.Z + F.N),
        ccr_wrong=frozenset(F.V),
        why="rotate through carry. This is the pairing that makes the 64-bit "
            "shift exact: the preceding LSL.L put the bit that left eax into "
            "BOTH C and X, and ROXL.L rotates X back in at the bottom -- x86's "
            "CF and the 68k's X are carrying the same bit, with no instruction "
            "in between to disturb X (MOVE does not affect X, but nothing here "
            "even tries).\n"
            "x86 RCL leaves SF/ZF untouched while ROXL sets N/Z from the "
            "result, and V differs; all three are checked dead.")


def lo_cmp(ctx, ins):
    dst, src = ins.ops
    size = op_size(ins)
    sfx = SIZE_SFX[size]
    pre = []

    # An ADDRESS-REGISTER destination -- `cmp edi,eax`, the loop bound in
    # DrawLine, where edi is the current scanline in a3. CMPA.L is a full 32-bit
    # compare that sets N/Z/V/C exactly as CMP does; the only thing to be careful
    # about is that it must be the LONG form, since CMPA.W would sign-extend the
    # source word instead of comparing all 32 bits.
    if isinstance(dst, Reg) and size == 4 and is_areg(reg68(dst, ins)):
        # ...but a swap-needing SOURCE has to be swapped here too. This branch
        # used to return before the swap check below ever ran, so a source
        # registered in le_read_sites.tsv was silently compared in raw x86
        # order -- no refusal, no warning, just the wrong number. That is what
        # ffe.asm:105331 (`cmp edi,[eax+0x4]', edi -> a3) did to the loop bound
        # of DATA_007811's shade records: a count of 7 read as 0x07000000 and
        # a bounded `mov [esi],eax' loop became a 117-million-iteration wild
        # write over the 68k exception vectors. CMPA accepts every addressing
        # mode including Dn (M68000PRM), so d7 is a legal source.
        ak = (_swap_kind(src, size) or _site_swap(ins, src, size)) \
            if isinstance(src, Mem) else None
        if ak:
            s = ea(replace(src, swapped=True), ins, want_size=size, pre=pre)
            return mk(pre, [op("move.l", s, SCRATCH_D)]
                      + _bswap_ops(SCRATCH_D, size, ak)
                      + [op("cmpa.l", SCRATCH_D, reg68(dst, ins))],
                      ccr_wrong=frozenset(F.X),
                      why="address-register destination against a swap-needing "
                          "source: the source is loaded and byte-swapped in d7 "
                          "first, then CMPA.L compares the true value against "
                          "the whole 32-bit An. Same flags as the plain form "
                          "below; the swap's own N/Z are overwritten by the "
                          "compare and X is untouched by both.\n" + LE_SWAP_NOTE)
        return mk(pre, [op("cmpa.l", ea(src, ins, pre=pre), reg68(dst, ins))],
                  ccr_wrong=frozenset(F.X),
                  why="address-register destination: CMPA.L subtracts the source "
                      "from the whole 32-bit An and sets N/Z/V/C from it, so the "
                      "signed and unsigned branches that follow read the same "
                      "flags they would after a CMP. Operand order is swapped "
                      "here for the same reason as below.\n"
                      "It must be CMPA.L and not CMPA.W: the word form "
                      "sign-extends its source to 32 bits first, which is a "
                      "different comparison.")

    # A MEMORY destination against an immediate -- `cmp dword [ebp-4],0x10000`,
    # the slope test. Only CMPI has that shape: `cmp <ea>,Dn` computes Dn - <ea>,
    # which is the wrong way round, and there is no `cmp Dn,<ea>` direction at
    # all. A non-immediate source is therefore refused rather than reordered.
    if isinstance(dst, Mem):
        if isinstance(src, Reg) and not is_areg(reg68(src, ins, size)):
            # `cmp [ptrSavedY],eax` (FlipScreenLow): the 68k has no
            # mem-minus-reg CMP direction, so the memory operand is loaded
            # into d7 and compared there -- `cmp.l s,d7` computes d7 - s =
            # mem - reg, the exact x86 difference. The load's N/Z are
            # overwritten by the compare; X untouched throughout.
            d = ea(dst, ins, want_size=size, pre=pre)
            return mk(pre, [op("move" + sfx, d, SCRATCH_D),
                            op("cmp" + sfx, reg68(src, ins, size), SCRATCH_D)],
                      ccr_wrong=frozenset(F.X),
                      why="memory-destination CMP against a register: the "
                          "memory side is loaded into d7 first, keeping the "
                          "x86 operand order (mem - reg) exactly.")
        if not isinstance(src, Imm):
            fail(ins.line, ins.text,
                 "CMP with a memory destination and a non-immediate source: the "
                 "68k has only CMPI (#imm,<ea>) in that direction, and swapping "
                 "the operands to use CMP <ea>,Dn would compute the difference "
                 "backwards")
        # A registered LE pointer site: load and swap the true value, then
        # compare it against the immediate. A plain CMPI would get Z right
        # only for a zero immediate and N/C wrong always.
        dk = (_swap_kind(dst, size) or _site_swap(ins, dst, size))             if size in (2, 4) else None
        if dk:
            d = ea(replace(dst, swapped=True), ins, want_size=size, pre=pre)
            return mk(pre, [op("move" + sfx, d, SCRATCH_D)]
                      + _bswap_ops(SCRATCH_D, size, dk)
                      + [op("cmpi" + sfx, imm68(src.value, size), SCRATCH_D)],
                      ccr_wrong=frozenset(F.X),
                      why="CMP of a registered LE pointer site against an "
                          "immediate: value loaded and byte-swapped in d7 "
                          "first.\n" + LE_SWAP_NOTE)
        d = ea(dst, ins, want_size=size, pre=pre)
        return mk(pre, [op("cmpi" + sfx, imm68(src.value, size), d)],
                  ccr_wrong=frozenset(F.X),
                  why="CMPI computes <ea> - #imm, which is the x86 order for "
                      "`cmp mem,imm` without any swap at all -- unlike the "
                      "register form below.")

    if not isinstance(dst, Reg):
        fail(ins.line, ins.text, "expected a register or memory CMP destination")
    r = reg68(dst, ins, size)
    if is_areg(r):
        fail(ins.line, ins.text,
             f"{dst.name} maps to {r}; a {size*8}-bit compare against an address "
             "register is not the same operation as CMPA.L and is refused")
    # `cmp cx,si` -- si/di/bp as a WORD source. CMP.W accepts an address
    # register as its SOURCE (only the destination side forbids An for word
    # ops, M68000PRM) and reads exactly the low word, which IS si -- the
    # same fact the `mov [mem],si` store and `add bx,di` already rest on.
    if (size == 2 and isinstance(src, Reg)
            and src.name in ("si", "di", "bp")):
        return mk(pre, [op("cmp.w", XREG["e" + src.name], r)],
                  ccr_wrong=frozenset(F.X),
                  why="word compare with an ADDRESS-REGISTER source: legal, "
                      "reads exactly the low word. Operand order swapped as "
                      "below; X untouched by CMP as always.")
    # A registered LE pointer site as the SOURCE: load and swap the true value
    # first; the final CMP's flags are exact and rewrite the swap's.
    sk = (_swap_kind(src, size) or _site_swap(ins, src, size))         if isinstance(src, Mem) and size in (2, 4) else None
    if sk:
        s = ea(replace(src, swapped=True), ins, want_size=size, pre=pre)
        return mk(pre, [op("move" + sfx, s, SCRATCH_D)]
                  + _bswap_ops(SCRATCH_D, size, sk)
                  + [op("cmp" + sfx, SCRATCH_D, r)],
                  ccr_wrong=frozenset(F.X),
                  why="CMP against a registered LE pointer site: the source is "
                      "loaded and byte-swapped in d7 first, then the ordinary "
                      "compare.\n" + LE_SWAP_NOTE)
    return mk(
        pre, [op("cmp" + sfx, ea(src, ins, want_size=size, pre=pre), r)],
        ccr_wrong=frozenset(F.X),
        why=("WORD WIDTH: CMP.W compares the low 16 bits of the register against "
             "a 16-bit source and sets N/Z/V/C from that word subtraction, which "
             "is exactly what the x86 16-bit CMP does. The upper half of the "
             "register is not read on either side, so its contents -- stale after "
             "a preceding 16-bit MOV -- cannot affect the answer.\n"
             if size == 2 else "") +
            "OPERAND ORDER: x86 `cmp a,b` computes a-b, the 68k `cmp <src>,<dst>` "
            "computes dst-src, so the operands are swapped -- and ONLY the "
            "operands. The branch condition is NOT mirrored (PLAN 4.4); doing "
            "both is the classic double-negative bug in hand ports.\n"
            "C/N/Z/V are exact afterwards: C is a borrow on both sides with the "
            "same polarity. X is NOT: 68k CMP leaves it holding whatever the "
            "last real arithmetic put there, so a CMP feeding an ADC/SBB is "
            "refused rather than guessed at (PLAN 4.3 -- 13 sites in the game, "
            "all one idiom).")


# ---- add / sub, and the multiprecision pair that hangs off them -------------

def _addsub(ctx, ins, kind, why):
    """`add`/`sub` with a register or memory destination.

    ADDQ/SUBQ replace the immediate form for 1..8 -- same arithmetic, same five
    flags, two bytes instead of six -- except against an address register, where
    the 68k defines them not to touch the CCR at all."""
    dst, src = ins.ops
    size = op_size(ins)
    sfx = SIZE_SFX[size]
    pre = []
    # Only for a POSITIVE immediate. Turning `add r,-4` into `subq.l #4` would
    # be value-exact and flag-WRONG: x86 computes r + 0xfffffffc and sets CF on
    # the carry out (r >= 4), the 68k computes r - 4 and sets C on the borrow
    # (r < 4). Inverted. The address-register case below is the exception, and
    # only because it produces no flags at all.
    quick = isinstance(src, Imm) and 1 <= src.value <= 8
    qmnem = {"add": "addq" + sfx, "sub": "subq" + sfx}

    if isinstance(dst, Reg) and size == 4 and is_areg(reg68(dst, ins)):
        a = reg68(dst, ins)
        live = getattr(ctx, "live_after", F.NONE)
        # A swap-needing memory SOURCE has to be swapped here too. Both paths
        # below call ea() directly, so a source registered in le_read_sites.tsv
        # was silently added in raw x86 order -- and at ffe.asm:36674 and 49006
        # (`add esi,[edx+0x18]', esi -> a2) that goes straight into a POINTER.
        # Found by the audit in lower(), not by anything failing.
        ak = (_swap_kind(src, size) or _site_swap(ins, src, size)) \
            if isinstance(src, Mem) else None
        if ak:
            s = ea(replace(src, swapped=True), ins, want_size=4, pre=pre)
            swap = [op("move.l", s, SCRATCH_D)] + _bswap_ops(SCRATCH_D, size, ak)
            if not (live & F.ALLX):
                # ADDA/SUBA accept a data register as their source and leave
                # the CCR alone, exactly as the immediate forms below do.
                return mk(pre, swap + [op(kind + "a.l", SCRATCH_D, a)],
                          ccr_wrong=F.ALLX,
                          why=why + "\nAddress-register destination with a "
                                    "swap-needing source: the true value is "
                                    "built in d7, then ADDA/SUBA applies it "
                                    "flag-silently. All five x86 flags must be "
                                    "dead, as in the plain An case below.\n"
                              + LE_SWAP_NOTE)
            if kind != "add":
                fail(ins.line, ins.text,
                     "SUB into an address register from a swap-needing memory "
                     "source while a flag is live: the value has to be built in "
                     "d7, but then d7 holds the SOURCE and `sub.l An,d7' would "
                     "compute src - An, the wrong way round. ADD is commutative "
                     "so it is safe; SUB is refused rather than guessed at.")
            # ADD is commutative in value AND in all four flags, so accumulating
            # into d7 -- which already holds the swapped source -- is exact.
            return mk(pre, swap + [op("add.l", a, SCRATCH_D),
                                   op("movea.l", SCRATCH_D, a)],
                      why=why + "\nAddress-register destination with LIVE "
                                "flag(s) %s and a swap-needing source: the true "
                                "value is built in d7 and the An added INTO it. "
                                "Addition is commutative in N/Z/V/C as well as "
                                "in value, so the flags are exact; MOVEA back "
                                "writes none.\n" % "".join(sorted(live))
                          + LE_SWAP_NOTE)
        if live & F.ALLX:
            # A downstream instruction reads a flag this ADD/SUB must set, so
            # the CCR-free An forms below are not an option. Detour through the
            # scratch data register: `move.l` sets N/Z and clears V/C on the
            # way IN, but the ADD/SUB.L then rewrites all five exactly as the
            # x86 does, and MOVEA back writes none -- net flag-exact. Chosen
            # only when a flag is live so every dead-flag site keeps the
            # cheaper silent form and its object file stays byte-identical.
            s = ea(src, ins, want_size=4, pre=pre)
            return mk(pre, [op("move.l", a, "d7"),
                            op(kind + ".l", s, "d7"),
                            op("movea.l", "d7", a)],
                      why=why + "\nAddress-register destination with LIVE "
                                "flag(s) %s: computed in d7 where the 68k "
                                "produces the full x86 CCR, then moved back "
                                "flag-silently." % "".join(sorted(live)))
        quick = isinstance(src, Imm) and 1 <= abs(src.value) <= 8
        qamnem = {("add", 1): "addq.l", ("add", -1): "subq.l",
                  ("sub", 1): "subq.l", ("sub", -1): "addq.l"}
        # PLAN 3.3: this is the cdecl stack adjustment and nothing else, and the
        # 68k forms for it are all CCR-free. ADDQ/SUBQ on an address register
        # ("the entire destination address register is used regardless of the
        # operation size" and the condition codes are not altered) and LEA are
        # both silent, so the x86 flags are simply not produced.
        if quick:
            lines = [op(qamnem[(kind, 1 if src.value > 0 else -1)],
                        "#%d" % abs(src.value), a)]
        elif isinstance(src, Imm) and -32768 <= (src.value if kind == "add"
                                                 else -src.value) <= 32767:
            d = src.value if kind == "add" else -src.value
            lines = [op("lea", "(%d,%s)" % (d, a), a)]
        else:
            lines = [op(kind + "a.l", ea(src, ins, pre=pre), a)]
        return mk(pre, lines, ccr_wrong=F.ALLX,
                  why=why + "\nAddress-register destination: ADDQ/SUBQ/LEA/ADDA "
                            "against an An leave the CCR alone, so none of the "
                            "x86 flags are produced and all five must be dead. "
                            "In practice this is only ever `add esp,imm`.")

    if (isinstance(dst, Reg) and size == 2
            and dst.name in ("si", "di", "bp")):
        # `add di,bx` -- the insert-low-word detour (see lo_mov): whole
        # register to d7, the .w arithmetic there -- where the 68k produces
        # the x86's exact word flags -- and MOVEA.L back. An An SOURCE
        # (`add si,di`) needs no extra step: ADD.W accepts an address
        # register as its source and reads exactly the low word.
        a = XREG["e" + dst.name]
        if isinstance(src, Reg) and src.name in ("si", "di", "bp"):
            s = XREG["e" + src.name]
        else:
            s = ea(src, ins, want_size=2, pre=pre)
        return mk(pre, [op("move.l", a, SCRATCH_D),
                        op(kind + ".w", s, SCRATCH_D),
                        op("movea.l", SCRATCH_D, a)],
                  why=why + "\nInsert-low-word detour for a word landing in "
                      "si/di; the .w arithmetic in d7 carries the exact x86 "
                      "flags out, MOVEA writes none.")

    if (isinstance(dst, Reg) and size == 2 and isinstance(src, Reg)
            and src.name in ("si", "di", "bp")):
        # `add bx,di` -- di as a word SOURCE. ADD.W (and SUB.W) accept an
        # address register as the source operand (only the DESTINATION side
        # forbids An for word ops, M68000PRM) and read exactly its low word,
        # which IS di -- the same PRM fact the verified `mov [mem],si` store
        # already rests on. Flag-exact.
        return mk(pre, [op(kind + ".w", XREG["e" + src.name],
                           reg68(dst, ins, 2))],
                  why=why + "\nWord arithmetic with an ADDRESS-REGISTER "
                      "source: legal (only the destination side forbids An "
                      "for word ops) and it reads exactly the low word, "
                      "which IS " + src.name + ".")

    if isinstance(dst, Reg):
        r = reg68(dst, ins, size)
        # A registered LE pointer site as the SOURCE: load and swap the true
        # value in the scratch register first, then the ordinary arithmetic,
        # whose flags are exact. The pre-swap ROL writes flags the final
        # ADD/SUB immediately rewrites.
        ssk = (_swap_kind(src, size) or _site_swap(ins, src, size))             if isinstance(src, Mem) and size in (2, 4) else None
        if ssk:
            s = ea(replace(src, swapped=True), ins, want_size=size, pre=pre)
            return mk(pre, [op("move" + sfx, s, SCRATCH_D)]
                      + _bswap_ops(SCRATCH_D, size, ssk)
                      + [op(kind + sfx, SCRATCH_D, r)],
                      why=why + "\n" + LE_SWAP_NOTE)
        if quick:
            return mk(pre, [op(qmnem[kind], "#%d" % src.value, r)], why=why)
        return mk(pre, [op(kind + sfx, ea(src, ins, want_size=size, pre=pre), r)],
                  why=why)

    if isinstance(dst, Mem):
        # RMW ADD/SUB on an le-stored dword (the RNG seed DATA_008818, whose
        # dword overlaps an independently byte-tested flag, so it must stay
        # in x86 byte order): load+swap the true value into d7, do the
        # arithmetic there -- where the full x86 CCR is produced and X is
        # set correctly -- then swap back and store. The swap-back ROLs
        # rewrite N/Z/V/C (declared wrong, checked dead) but do NOT touch X,
        # so multiprecision carries survive.
        if _swap_kind(dst, size) == "le" and size == 4 \
                and (isinstance(src, Imm)
                     or (isinstance(src, Reg)
                         and not is_areg(reg68(src, ins)))):
            d = ea(replace(dst, swapped=True), ins, want_size=4, pre=pre)
            s = imm68(src.value, 4) if isinstance(src, Imm) \
                else reg68(src, ins)
            return mk(pre, [op("move.l", d, SCRATCH_D)]
                      + _bswap_ops(SCRATCH_D, 4)
                      + [op(kind + ".l", s, SCRATCH_D)]
                      + _bswap_ops(SCRATCH_D, 4)
                      + [op("move.l", SCRATCH_D, d)],
                      ccr_wrong=frozenset(F.N + F.Z + F.V + F.C),
                      why=why + "\n" + LE_SWAP_NOTE)
        d = ea(dst, ins, want_size=size, pre=pre)
        if quick:
            return mk(pre, [op(qmnem[kind], "#%d" % src.value, d)], why=why)
        if isinstance(src, Imm):
            return mk(pre, [op(kind + sfx, imm68(src.value, size), d)], why=why)
        # ADD/SUB with a memory destination is the Dn,<ea> direction only.
        if (isinstance(src, Reg) and src.size == 2
                and src.name in ("si", "di", "bp")):
            # `add [ebx+0xb0],si` -- si as a word source; whole An to d7,
            # the .w op reads its low word, which IS si.
            pre.append(op("move.l", XREG["e" + src.name], SCRATCH_D))
            s = SCRATCH_D
        else:
            s = data_ea(src, ins, pre, size)
        if not s.startswith("d"):
            fail(ins.line, ins.text,
                 "%s to memory needs its source in a data register" % ins.mnem)
        return mk(pre, [op(kind + sfx, s, d)], why=why)

    fail(ins.line, ins.text, "unsupported %s form" % ins.mnem)


def lo_add(ctx, ins):
    return _addsub(ctx, ins, "add",
                   "68k ADD is flag-exact against x86 ADD: same N/Z/V, and C is "
                   "the carry out on both. It additionally sets X to that same "
                   "carry, which is what a following ADC will read.")


def lo_sub(ctx, ins):
    return _addsub(ctx, ins, "sub",
                   "68k SUB is flag-exact against x86 SUB, borrow included: both "
                   "set C when the subtraction borrows, with the same polarity. "
                   "X takes the borrow too, ready for a following SBB.")


def _addxsubx(ctx, ins, mnem, why):
    """`adc`/`sbb` -> ADDX/SUBX, whose only forms are Dy,Dx and -(Ay),-(Ax)."""
    dst, src = ins.ops
    size = op_size(ins)
    sfx = SIZE_SFX[size]
    mnem = mnem.split(".")[0] + sfx
    if isinstance(dst, Mem) and isinstance(src, Reg) \
            and not (src.size == 4 and is_areg(reg68(src, ins))):
        # `adc [eax+0xac],edx` -- the 64-bit RMW tail (credits/clock adds
        # into an object). The memory half rides through d7: the LOAD must
        # not touch X (MOVE does not), the ADDX/SUBX consumes and rewrites
        # X/C correctly, and the store-back MOVE clears V/C and rewrites
        # N/Z from the same result (N stays right, V/C/Z declared wrong --
        # Z was already sticky-wrong for ADDX, see below).
        pre = []
        d = ea(dst, ins, want_size=size, pre=pre)
        return mk(pre, [op("move" + sfx, d, SCRATCH_D),
                        op(mnem, reg68(src, ins, size), SCRATCH_D),
                        op("move" + sfx, SCRATCH_D, d)],
                  ccr_wrong=frozenset(F.Z + F.V + F.C), why=why)
    if not isinstance(dst, Reg) or (size == 4 and is_areg(reg68(dst, ins))):
        fail(ins.line, ins.text,
             "%s needs a data-register destination: ADDX/SUBX exist only as "
             "Dy,Dx and -(Ay),-(Ax)" % ins.mnem)
    r = reg68(dst, ins, size)
    pre = []
    if isinstance(src, Reg) and not (src.size == 4 and is_areg(reg68(src, ins))):
        s = reg68(src, ins, size)
    else:
        # Same restriction, so a memory/immediate/An source is loaded first. The
        # load MUST leave X alone or the carry it is about to consume would be
        # destroyed; MOVE and MOVEA are both documented as not affecting X.
        s = SCRATCH_D
        pre.append(op("move" + sfx, ea(src, ins, want_size=size, pre=pre),
                      SCRATCH_D))
    return mk(pre, [op(mnem, s, r)], ccr_wrong=frozenset(F.Z), why=why)


def lo_adc(ctx, ins):
    return _addxsubx(
        ctx, ins, "addx.l",
        "ADDX takes its carry-in from X, exactly where the ADD that produced it "
        "left the x86 CF, and ADDX Dy,Dx computes Dx+Dy+X just as `adc dst,src` "
        "computes dst+src+CF.\n"
        "Z differs and is checked dead: x86 ADC sets ZF from this result alone, "
        "while the 68k ACCUMULATES it (M68000PRM, ADDX: `Z Cleared if the result "
        "is nonzero. Unchanged otherwise', so that a multiprecision chain can "
        "test the whole value at the end). PLAN 1 measured zero readers of ZF "
        "after an adc/sbb in the entire game.")


def lo_sbb(ctx, ins):
    dst, src = ins.ops
    if (dst == src and isinstance(dst, Reg) and dst.size == 4
            and not is_areg(reg68(dst, ins))):
        # `sbb eax,eax` = -(CF), the carry-to-mask idiom. Reading C instead
        # of X (see flags.effect) is what lets the CMP before it stay legal:
        # Scs stores 0xFF/0x00 from C -- which the CMP produced exactly --
        # and EXTB.L makes that -1/0, the identical value.
        d = reg68(dst, ins)
        return Lowered(
            [op("scs", d), op("extb.l", d)],
            ccr_wrong=frozenset(F.C + F.X),
            why="`sbb r,r` computes -(CF) regardless of r, so it is lowered "
                "as Scs + EXTB.L reading the 68k C -- which the preceding "
                "CMP/SUB produced with the x86's exact borrow -- instead of "
                "the X that CMP never writes.\n"
                "After it, N and Z match the x86 (result -1 or 0, EXTB sets "
                "both) and V is 0 on both sides; the x86 PRESERVES CF while "
                "EXTB clears C and Scs/EXTB leave X alone, so C and X are "
                "declared unreliable and checked dead.")
    return _addxsubx(
        ctx, ins, "subx.l",
        "SUBX Dy,Dx computes Dx-Dy-X, matching `sbb dst,src` = dst-src-CF, with "
        "the borrow arriving in X from the SUB that produced it.\n"
        "Z accumulates rather than being set from this result alone, as for "
        "ADDX, and is checked dead.")


# ---- logic -----------------------------------------------------------------

def _logic(ctx, ins, mnem, why):
    """`and`/`or` -- both directions exist (<ea>,Dn and Dn,<ea>) but neither
    accepts an address register operand."""
    dst, src = ins.ops
    size = op_size(ins)
    mnem += SIZE_SFX[size]
    pre = []
    if isinstance(dst, Reg) and size == 4 and is_areg(reg68(dst, ins)):
        # AND/OR simply have no An form (M68000PRM), so the scratch detour is
        # the only spelling, not an alternative: value into d7, operate there
        # -- where the 68k produces exactly the x86's N/Z and clears V/C like
        # the x86 clears OF/CF -- and MOVEA back, which writes no flags. The
        # MOVE.L in has its flags overwritten by the logic op, so the net
        # story is identical to the data-register path.
        a = reg68(dst, ins)
        ask = (_swap_kind(src, 4) or _site_swap(ins, src, 4)) \
            if isinstance(src, Mem) else None
        if ask:
            # A swap-needing memory source into an An destination: the true
            # value is built in d6 first (`or esi,[DATA_008713]`,
            # FUNC_000819's MSB accumulate over an le global).
            s = ea(replace(src, swapped=True), ins, want_size=4, pre=pre)
            pre.append(op("move.l", s, SCRATCH_D2))
            pre += _bswap_ops(SCRATCH_D2, 4, ask)
            s = SCRATCH_D2
        else:
            s = ea(src, ins, want_size=4, pre=pre)
            if is_areg(s):
                # `or esi,edi` -- the SOURCE is an An too, and AND/OR forbid
                # it on that side as well (vasm caught the first attempt).
                # The second scratch carries it.
                pre.append(op("move.l", s, SCRATCH_D2))
                s = SCRATCH_D2
        return mk(pre, [op("move.l", a, SCRATCH_D),
                        op(mnem, s, SCRATCH_D),
                        op("movea.l", SCRATCH_D, a)],
                  why=why + "\n" + AREG_NOTE)
    if (isinstance(dst, Reg) and size == 2
            and dst.name in ("si", "di", "bp")):
        # `and si,imm` -- the insert-low-word detour (see lo_mov): the whole
        # register into d7, the word operation there -- where the 68k
        # produces the x86's exact word flags -- and MOVEA.L back.
        a = XREG["e" + dst.name]
        s = ea(src, ins, want_size=2, pre=pre)
        return mk(pre, [op("move.l", a, SCRATCH_D),
                        op(mnem, s, SCRATCH_D),
                        op("movea.l", SCRATCH_D, a)],
                  why=why + "\nInsert-low-word detour for a word landing in "
                      "si/di: the logic op runs on d7 and its flags are the "
                      "x86's; MOVE.L in is overwritten, MOVEA back writes "
                      "none.")
    if isinstance(dst, Reg):
        # A swap-needing MEMORY source (named le/be16 or a registered LE
        # pointer site): load and swap the true value in d7 first, then the
        # ordinary logic op, whose flags are exact. Carrier:
        # FUNC_000819's `or esi,[DATA_008713]` (an MSB accumulate -- the
        # unswapped value would move the found bit position).
        lsk = (_swap_kind(src, size) or _site_swap(ins, src, size)) \
            if isinstance(src, Mem) and size in (2, 4) else None
        if lsk:
            s = ea(replace(src, swapped=True), ins, want_size=size, pre=pre)
            return mk(pre, [op("move" + SIZE_SFX[size], s, SCRATCH_D)]
                      + _bswap_ops(SCRATCH_D, size, lsk)
                      + [op(mnem, SCRATCH_D, reg68(dst, ins, size))],
                      why=why + "\n" + LE_SWAP_NOTE)
        return mk(pre, [op(mnem, data_ea(src, ins, pre, size),
                           reg68(dst, ins, size))], why=why)
    if isinstance(dst, Mem):
        # A DWORD OR/XOR on an le-stored label with an immediate whose set
        # bits all live in ONE byte is exactly a BYTE op at that byte's LE
        # offset -- the other three bytes are untouched by OR/EOR with zero.
        # This is what makes `or dword [edx*2+DATA_005101],1` (a bit-0 flag
        # in a stride-62 struct array, runtime-indexed, un-flippable)
        # translatable at all. N/Z then come from the byte, not the dword --
        # declared wrong and checked dead; V/C are cleared by both forms.
        if (size == 4 and isinstance(src, Imm)
                and ins.mnem in ("or", "xor", "and")
                and _swap_kind(dst, 4) == "le"):
            v = src.value & 0xFFFFFFFF
            for bi in range(4):
                if ins.mnem == "and":
                    # AND leaves a byte alone iff its mask bits are ALL ONES;
                    # the op narrows when exactly one byte differs from 0xFF.
                    others = v | (0xFF << (8 * bi))
                    hit = others == 0xFFFFFFFF and ((v >> (8 * bi)) & 0xFF) != 0xFF
                else:
                    hit = bool(v) and v == (v & (0xFF << (8 * bi)))
                if hit:
                    d = ea(replace(dst, disp=dst.disp + bi, size=1,
                                   swapped=True),
                           ins, want_size=1, pre=pre)
                    bv = (v >> (8 * bi)) & 0xFF
                    if bv > 127:
                        bv -= 256          # vasm wants byte imms in range
                    return mk(pre, [op(mnem[:-2] + ".b" if mnem.endswith(".l")
                                       else mnem,
                                       "#%d" % bv, d)],
                              ccr_wrong=frozenset(F.N + F.Z),
                              why=why + "\nLE-storage dword bitwise op "
                                  "narrowed to the one byte its immediate "
                                  "touches (OR/XOR: the other bytes' bits "
                                  "are zero; AND: they are all ones); N/Z "
                                  "from the byte are declared wrong and "
                                  "checked dead.")
        d = ea(dst, ins, want_size=size, pre=pre)
        if isinstance(src, Imm):
            return mk(pre, [op(mnem, imm68(src.value, size), d)], why=why)
        if (isinstance(src, Reg) and src.size == 2
                and src.name in ("si", "di", "bp")):
            # `xor [edx],si` -- si as a word SOURCE. AND/OR/EOR to memory
            # need a Dn source, so the whole register goes to d7 and the .w
            # form reads its low word -- which IS si, exactly.
            pre.append(op("move.l", XREG["e" + src.name], SCRATCH_D))
            s = SCRATCH_D
        else:
            s = data_ea(src, ins, pre, size)
        if not s.startswith("d"):
            fail(ins.line, ins.text,
                 "%s to memory needs its source in a data register" % ins.mnem)
        return mk(pre, [op(mnem, s, d)], why=why)
    fail(ins.line, ins.text, "unsupported %s form" % ins.mnem)


LOGIC_WHY = ("68k %s sets N/Z from the result and clears V and C, exactly as the "
             "x86 sets SF/ZF and clears OF/CF. X is left alone where the x86 "
             "cleared CF, so a following ADC/SBB would see a stale carry -- "
             "declared and checked dead.")


def lo_and(ctx, ins):
    return _logic(ctx, ins, "and", LOGIC_WHY % "AND")


def lo_or(ctx, ins):
    dst, src = ins.ops
    # dst.size == 4 comes FIRST: reg68 at its default width refuses dl/bx
    # loudly, and this guard is only about the 32-bit MOVEQ idiom anyway.
    if (isinstance(dst, Reg) and dst.size == 4
            and not is_areg(reg68(dst, ins))
            and isinstance(src, Imm) and src.value == -1):
        # `or r,-1` is a set-to-all-ones whose result does not depend on r, so
        # MOVEQ #-1 is not merely equivalent in value: it is equivalent in
        # FLAGS too. x86 OR leaves SF=1 ZF=0 CF=OF=0 for a result of -1, and
        # MOVEQ #-1 leaves N=1 Z=0 V=C=0. Identical. The liveness check below
        # would have caught it either way, and it reports these flags dead here.
        return Lowered(
            [op("moveq", "#-1", reg68(dst, ins))],
            ccr_wrong=frozenset(F.X),
            why="`or eax,-1` sets every bit regardless of the old value, so "
                "MOVEQ #-1 replaces it (2 bytes instead of 6). This is flag-"
                "exact, not merely value-exact: OR producing -1 leaves "
                "SF=1 ZF=0 CF=OF=0 and MOVEQ #-1 leaves N=1 Z=0 V=C=0. The "
                "liveness pass additionally reports all four dead here.")
    return _logic(ctx, ins, "or", LOGIC_WHY % "OR")


def lo_xor(ctx, ins):
    dst, src = ins.ops
    pre = []
    if dst == src and isinstance(dst, Reg) and not is_areg(reg68(dst, ins)):
        # PLAN 3.5. Flag-exact as well as value-exact: x86 `xor r,r` leaves
        # ZF=1 SF=0 OF=CF=0 and MOVEQ #0 leaves Z=1 N=0 V=C=0.
        return Lowered([op("moveq", "#0", reg68(dst, ins))],
                       ccr_wrong=frozenset(F.X),
                       why="`xor r,r` clears the register regardless of its old "
                           "value, so MOVEQ #0 replaces it; the flags match "
                           "exactly (Z=1, N=V=C=0 on both sides).")
    size = op_size(ins)
    sfx = SIZE_SFX[size]
    if (dst == src and isinstance(dst, Reg) and size == 4
            and is_areg(reg68(dst, ins))):
        # `xor esi,esi` is the clear-a-register idiom, and it is the single
        # biggest reason EOR met an address register at all. SUBA.L An,An
        # clears the register in two bytes and, uniquely among the ways to do
        # it, touches no condition codes -- which is the divergence, not a
        # saving: the x86 leaves Z=1 N=0 V=C=0 and the 68k leaves whatever was
        # there. All five are therefore declared unreliable and checked dead.
        a = reg68(dst, ins)
        return Lowered([op("suba.l", a, a)], ccr_wrong=F.ALLX,
                       why="`xor aN,aN` clears the register whatever it held, so "
                           "SUBA.L An,An replaces it. EOR cannot take an address "
                           "register at all, and SUBA is the only two-byte way "
                           "to clear one.\n"
                           "SUBA affects NO condition codes while the x86 sets "
                           "Z and clears N/V/C, so all five are declared "
                           "unreliable rather than merely spurious -- they are "
                           "stale here, not wrong-but-written.")
    if isinstance(dst, Reg) and size == 4 and is_areg(reg68(dst, ins)):
        # `xor edi,X` -- EOR takes no An operand on either side, so the value
        # detours through d7 exactly as AND/OR already do: the EOR there
        # produces the x86's N/Z and clears V/C, MOVEA back writes none. The
        # source must be a data register or immediate (EOR is Dn,<ea> only);
        # an An source takes the second scratch.
        a = reg68(dst, ins)
        s = ea(src, ins, want_size=4, pre=pre)
        if isinstance(src, Imm):
            body = [op("eori.l", s, SCRATCH_D)]
        else:
            if not (s.startswith("d") and len(s) == 2):
                # EOR is Dn,<ea> ONLY -- a memory or An source must land in
                # the second scratch data register first.
                pre.append(op("move.l", s, SCRATCH_D2))
                s = SCRATCH_D2
            body = [op("eor.l", s, SCRATCH_D)]
        return mk(pre, [op("move.l", a, SCRATCH_D)] + body
                  + [op("movea.l", SCRATCH_D, a)],
                  why=LOGIC_WHY % "EOR" + "\n" + AREG_NOTE)
    d = dest_ea(dst, ins, pre, size)
    if isinstance(src, Imm):
        return mk(pre, [op("eor" + sfx, imm68(src.value, size), d)],
                  why=LOGIC_WHY % "EOR")
    # EOR has no <ea>,Dn direction at all, only Dn,<ea>, so the source has to be
    # in a data register whatever it started as.
    if (isinstance(src, Reg) and src.size == 2
            and src.name in ("si", "di", "bp")):
        # `xor [edx],si` -- si as a word source lives in a2; the whole
        # register goes to d7 and EOR.W reads its low word, which IS si.
        pre.append(op("move.l", XREG["e" + src.name], SCRATCH_D))
        s = SCRATCH_D
    else:
        s = data_ea(src, ins, pre, size)
    if not s.startswith("d"):
        pre.append(op("move" + sfx, s, SCRATCH_D))
        s = SCRATCH_D
    return mk(pre, [op("eor" + sfx, s, d)],
              why=LOGIC_WHY % "EOR" + "\nEOR exists only in the Dn,<ea> "
                  "direction, so the source goes through a data register.")


def lo_not(ctx, ins):
    (dst,) = ins.ops
    pre = []
    size = op_size(ins)
    if isinstance(dst, Reg) and size == 4 and is_areg(reg68(dst, ins)):
        # Same d7 detour as NEG below; NOT's flags are declared spurious
        # either way (x86 NOT writes none), so nothing changes there.
        a = reg68(dst, ins)
        return Lowered([op("move.l", a, SCRATCH_D),
                        op("not.l", SCRATCH_D),
                        op("movea.l", SCRATCH_D, a)],
                       ccr_extra=F.ALL,
                       why="NOT into an address register, via d7. x86 NOT "
                           "writes no flags; the detour's N/Z/V writes are "
                           "declared spurious and checked dead. MOVEA "
                           "writes none.")
    d = dest_ea(dst, ins, pre, size)
    return mk(pre, [op("not" + SIZE_SFX[size], d)], ccr_extra=F.ALL,
              why="x86 NOT is defined to leave every flag alone; 68k NOT sets "
                  "N/Z and clears V/C. All four are checked dead. (X is left "
                  "alone by both.)")


def lo_neg(ctx, ins):
    (dst,) = ins.ops
    pre = []
    size = op_size(ins)
    if isinstance(dst, Reg) and size == 4 and is_areg(reg68(dst, ins)):
        # NEG has no An form, so the scratch detour is the only spelling --
        # and it is flag-exact: NEG.L in d7 produces the full x86 CCR
        # (including C as the same not-zero predicate and X as its copy),
        # and the MOVEA back writes none.
        a = reg68(dst, ins)
        return Lowered([op("move.l", a, SCRATCH_D),
                        op("neg.l", SCRATCH_D),
                        op("movea.l", SCRATCH_D, a)],
                       why="NEG into an address register, via d7. Flag-exact: "
                           "the MOVE.L in is overwritten by NEG's full CCR, "
                           "and MOVEA writes nothing. " + AREG_NOTE)
    d = dest_ea(dst, ins, pre, size)
    return mk(pre, [op("neg" + SIZE_SFX[size], d)],
              why="flag-exact. x86 NEG clears CF for a zero operand and sets it "
                  "otherwise; the M68000PRM gives 68k NEG as `C Cleared if the "
                  "result is zero. Set otherwise', which is the same predicate, "
                  "and N/Z/V agree too. X takes a copy of C.")


_HIGH8 = {"ah": "d0", "ch": "d1", "dh": "d2", "bh": "d3"}


def lo_test(ctx, ins):
    a, b = ins.ops
    pre = []

    # `test ah,0x40` -- a single-bit test of a HIGH byte, which the 68k has no
    # addressing mode for. BTST #(8+n),dN tests exactly that bit of the parent
    # register and writes ONLY Z -- and Z is x86's (ah & mask) == 0, the same
    # predicate. N/V/C, which x86 TEST does write (N from the result, V/C
    # cleared), stay stale, and so does X where x86 cleared CF -- all four are
    # declared and must be dead. All nine game sites are `j[n]z` afterwards.
    if (isinstance(a, Reg) and a.name in _HIGH8 and isinstance(b, Imm)
            and 0 < b.value <= 0xff and (b.value & (b.value - 1)) == 0):
        bit = 8 + b.value.bit_length() - 1
        return Lowered([op("btst", "#%d" % bit, _HIGH8[a.name])],
                       ccr_wrong=frozenset(F.N + F.V + F.C + F.X),
                       why="`test %s,#0x%x` is a single-bit test of a high "
                           "byte: BTST #%d on the parent register tests the "
                           "identical bit and sets Z to the identical "
                           "predicate. Only Z is produced; N/V/C/X are "
                           "declared unreliable and checked dead."
                           % (a.name, b.value, bit))

    size = op_size(ins)
    sfx = SIZE_SFX[size]
    if a == b:
        if (isinstance(a, Reg) and a.size == 2
                and a.name in ("si", "di", "bp")):
            # `test si,si` -- TST.W on an address register is a 68020
            # addition (this build's formal target) and reads the low word,
            # which IS si. Same flag story as every other TST.
            return Lowered([op("tst.w", XREG["e" + a.name])],
                           ccr_wrong=frozenset(F.X),
                           why="`test si,si` as TST.W on the An itself -- a "
                               "68020 addition. N/Z from the low word, V/C "
                               "cleared, matching the x86; only X differs "
                               "and is checked dead.")
        return mk(pre, [op("tst" + sfx, ea(a, ins, want_size=size, pre=pre))],
                  ccr_wrong=frozenset(F.X),
                  why="`test a,a` is a pure flag setter, which is exactly what "
                      "TST is: N/Z from the operand, V and C cleared, matching "
                      "x86 SF/ZF/OF/CF. Only X differs -- the x86 cleared CF, "
                      "the 68k leaves X alone. (TST on an address register is a "
                      "68020 addition; this build targets a formal 68020.)")
    # `test a,b` with different operands is an AND whose result is discarded, so
    # it needs somewhere to discard it to. AND is commutative, so the operand
    # that cannot be an <ea> for it -- an address register -- is the one loaded.
    if isinstance(b, Reg) and b.size == 4 and is_areg(reg68(b, ins)):
        a, b = b, a
    if isinstance(b, Reg) and b.size == 4 and is_areg(reg68(b, ins)):
        fail(ins.line, ins.text,
             "TEST of two address registers would need a second scratch data "
             "register")
    pre.append(op("move" + sfx, ea(a, ins, want_size=size, pre=pre), SCRATCH_D))
    return mk(pre, [op("and" + sfx, ea(b, ins, want_size=size, pre=pre),
                       SCRATCH_D)],
              ccr_wrong=frozenset(F.X),
              why="`test a,b` ANDs the two and keeps only the flags, so the AND "
                  "is done into the scratch register and the result thrown "
                  "away. N/Z/V/C match; X is left stale where the x86 cleared "
                  "CF, and is checked dead.")


def lo_incdec(ctx, ins):
    (dst,) = ins.ops
    pre = []
    size = op_size(ins)
    mnem = ("addq" if ins.mnem == "inc" else "subq") + SIZE_SFX[size]
    if isinstance(dst, Reg) and size == 4 and is_areg(reg68(dst, ins)):
        a = reg68(dst, ins)
        live = getattr(ctx, "live_after", F.NONE)
        if live & frozenset(F.Z + F.N + F.V):
            # `dec esi / jg` with the counter in an address register (the
            # FUNC_000149 hand-patch loop). ADDQ/SUBQ on an An write no CCR,
            # so the flags the branch reads have to come from the d7 detour:
            # the SUBQ on the data register sets N/Z/V exactly as x86 DEC
            # does, MOVEA back writes nothing. The cost is C and X, which
            # the 68k add writes and x86 INC/DEC deliberately leave alone --
            # declared spurious, so a live carry still refuses.
            return Lowered([op("move.l", a, SCRATCH_D),
                            op("addq.l" if ins.mnem == "inc" else "subq.l",
                               "#1", SCRATCH_D),
                            op("movea.l", SCRATCH_D, a)],
                           ccr_extra=frozenset(F.C + F.X),
                           why="`%s %s` on an address register WITH live N/Z/V: "
                               "ADDQ/SUBQ on an An write no condition codes at "
                               "all, so the arithmetic detours through d7 where "
                               "the SUBQ/ADDQ sets N/Z/V exactly as x86 INC/DEC "
                               "do; MOVEA back is CCR-silent. C and X are "
                               "spuriously written by the detour (x86 INC/DEC "
                               "preserve CF) and must be dead."
                               % (ins.mnem, dst.name))
        # A DIFFERENT divergence from the data-register one below, so it gets a
        # different declaration rather than the same one. ADDQ/SUBQ against an
        # address register operate on all 32 bits whatever the size suffix says
        # and, uniquely, alter NO condition codes at all (M68000PRM, ADDQ:
        # "the condition codes are not affected when the destination is an
        # address register"). So the three flags x86 INC/DEC do produce are
        # simply missing here, while CF -- which x86 deliberately leaves alone --
        # agrees for free, in C and in X.
        return Lowered([op("addq.l" if ins.mnem == "inc" else "subq.l",
                           "#1", a)],
                       ccr_wrong=frozenset(F.Z + F.N + F.V),
                       why="`%s %s` on an address register: ADDQ/SUBQ leave the "
                           "CCR completely alone there, so Z/N/V are stale "
                           "afterwards and have to be dead. C and X are the "
                           "flags that DO agree, because x86 INC/DEC do not "
                           "write CF either -- the opposite of the data-register "
                           "case, where C and X are the problem."
                           % (ins.mnem, dst.name))
    d = dest_ea(dst, ins, pre, size)
    return mk(pre, [op(mnem, "#1", d)], ccr_extra=frozenset(F.C + F.X),
              why="THE ONE DIFFERENCE THAT IS NOT NEGOTIABLE: x86 INC and DEC "
                  "deliberately do NOT write CF, so that they can be used inside "
                  "a multiprecision loop, while 68k ADDQ/SUBQ write C and X like "
                  "any other add. Both are therefore declared as spuriously "
                  "written, and the liveness pass refuses the translation if "
                  "anything downstream still reads the carry. PLAN 1 measured "
                  "zero such places in the whole game; this is the check that "
                  "makes that a proof here rather than a memory of a "
                  "measurement.")


def _lea_disp(d, disp):
    """Add a displacement to a data register, shortest form first."""
    if not disp:
        return []
    if 1 <= disp <= 8:
        return [op("addq.l", "#%d" % disp, d)]
    if -8 <= disp <= -1:
        return [op("subq.l", "#%d" % -disp, d)]
    return [op("add.l", imm68(disp), d)]


# WHICH FORM AN INDEXED LEA TAKES, AND WHY.
#
# The 68020 does have (bd,An,Xn.l*scale), and lo_lea uses it whenever the
# operands are already where that mode needs them. But the mode is an ADDRESS
# calculation: the base and the result both have to be address registers. In
# `lea edx,[edx+edx*4]` -- the x320 row stride in FUNC_001601_DrawHLine, paired
# with the `shl edx,6` in front of it -- the base, the index and the destination
# are all edx, i.e. d2. Reaching the 68020 mode from there costs
# `movea.l d2,a0` + `lea (0,a0,d2.l*4),a0` + `move.l a0,d2`: three instructions,
# one of them carrying a full-format extension word, plus a round trip through
# the address unit whose scaled-index calculation is itself slower than a shift.
# The arithmetic form is the same three instructions with none of that -- MOVE,
# LSL, ADD, all long, all on data registers. So it is not a fallback: for a
# result wanted in a data register it is strictly the cheaper way to say
# `d2 = d2 * 5`, and that is why it is chosen.
LEA_INDEX_NOTE = (
    "`lea dst,[base+index*scale+disp]` with the result in a DATA register is "
    "arithmetic, not an addressing mode: the 68020's own (d8,An,Xn.l*s) needs "
    "the base and the destination in ADDRESS registers, and getting d2 there and "
    "back would cost the same three instructions plus a full-format extension "
    "word and the address unit's slower scaled-index calculation. MOVE / LSL / "
    "ADD is chosen because it is cheaper here, not because the mode is missing.\n"
    "The index is copied into d7 FIRST, which is what lets base, index and "
    "destination all be the same register -- as they are in the x320 stride.\n"
    "x86 LEA writes no flags and every instruction here writes some, so all five "
    "are declared spurious and the liveness pass has to report them dead.")


def _lea_indexed(ctx, ins, src, d):
    """`lea dst,[base + index*scale + disp]`. See LEA_INDEX_NOTE."""
    if src.scale not in SCALE_SHIFT:
        fail(ins.line, ins.text, "index scale %d is not 1/2/4/8" % src.scale)
    idx = XREG[src.index]

    if src.sym is not None:
        # `lea eax,[edx*4+DATA_x]` -- the address of a table slot. The label
        # is the (suppressed) base. Into an ADDRESS register: LEA the label
        # into a0, then the machine's own indexed LEA -- two instructions,
        # no flags touched, exactly like the x86. Into a DATA register it is
        # arithmetic (copy index, shift, add the label as an immediate), and
        # every step writes flags the x86 LEA does not -- declared, checked
        # dead. PLAN 5's invariant holds either way: the assembler resolves
        # the label to a 68k address.
        if src.base is not None:
            fail(ins.line, ins.text,
                 "LEA with base register AND symbol AND index -- the 68020 "
                 "full format with two displacements; nothing has needed it")
        s = canon(src.sym)
        if is_areg(d):
            if not -128 <= src.disp <= 127:
                fail(ins.line, ins.text,
                     "indexed LEA with displacement %d: only the brief format "
                     "(-128..127) is emitted" % src.disp)
            return Lowered(
                [op("lea", "(%s).l" % s, SCRATCH_A),
                 op("lea", "(%d,%s,%s.l*%d)"
                    % (src.disp, SCRATCH_A, idx, src.scale), d)],
                why="LEA of symbol + scaled index into an address register: "
                    "the label into a0, then the native (d8,An,Xn.l*s). "
                    "Neither LEA touches the CCR -- flag-exact.")
        lines = [] if idx == d else [op("move.l", idx, d)]
        k = SCALE_SHIFT[src.scale]
        if k:
            lines.append(op("lsl.l", "#%d" % k, d))
        disp = ("%+d" % src.disp) if src.disp else ""
        lines.append(op("add.l", "#%s%s" % (s, disp), d))
        return Lowered(lines, ccr_extra=F.ALLX, why=LEA_INDEX_NOTE)

    if is_areg(d):
        if src.base is None or not is_areg(XREG[src.base]):
            # `lea esi,[ebx+ebx*4]` -- the base is in a DATA register, so
            # the sum is built in d7 and MOVEA'd over whole. MOVEA writes no
            # flags; the arithmetic in d7 does where x86 LEA writes none, so
            # all five are declared and checked dead.
            b = XREG[src.base] if src.base is not None else None
            lines = [op("move.l", idx, SCRATCH_D)]
            k = SCALE_SHIFT[src.scale]
            if k:
                lines.append(op("lsl.l", "#%d" % k, SCRATCH_D))
            if b is not None:
                lines.append(op("add.l", b, SCRATCH_D))
            lines += _lea_disp(SCRATCH_D, src.disp)
            lines.append(op("movea.l", SCRATCH_D, d))
            return Lowered(lines, ccr_extra=F.ALLX, why=LEA_INDEX_NOTE)
        if not -128 <= src.disp <= 127:
            # A displacement outside the brief format (`lea edi,
            # [edi+eax*4+0x5128]`, FUNC_000246's save-slot copy): rather than
            # trusting the 68020 full-format encoding, build the sum in d7 --
            # the same arithmetic route every data-register LEA already
            # takes, all proven shapes. MOVEA back writes no flags.
            lines = [op("move.l", idx, SCRATCH_D)]
            k = SCALE_SHIFT[src.scale]
            if k:
                lines.append(op("lsl.l", "#%d" % k, SCRATCH_D))
            lines.append(op("add.l", XREG[src.base], SCRATCH_D))
            lines.append(op("add.l", imm68(src.disp), SCRATCH_D))
            lines.append(op("movea.l", SCRATCH_D, d))
            return Lowered(lines, ccr_extra=F.ALLX, why=LEA_INDEX_NOTE)
        return Lowered(
            [op("lea", "(%d,%s,%s.l*%d)"
                % (src.disp, XREG[src.base], idx, src.scale), d)],
            why="a real 68020 indexed LEA: base and result are both address "
                "registers, so the machine's own (d8,An,Xn.l*scale) does it in "
                "one instruction and, like the x86 LEA, touches no flags.")

    lines = [op("move.l", idx, SCRATCH_D)]
    k = SCALE_SHIFT[src.scale]
    if k:
        lines.append(op("lsl.l", "#%d" % k, SCRATCH_D))
    if src.base is None:
        lines.append(op("move.l", SCRATCH_D, d))
    else:
        b = XREG[src.base]
        # The index is already safe in d7, so the base may be read after the
        # destination has been written -- which is what makes base == index ==
        # destination (the x320 case) work without a second scratch register.
        if b != d:
            lines.append(op("move.l", b, d))
        lines.append(op("add.l", SCRATCH_D, d))
    lines += _lea_disp(d, src.disp)
    return Lowered(lines, ccr_extra=F.ALLX, why=LEA_INDEX_NOTE)


def lo_lea(ctx, ins):
    dst, src = ins.ops
    if not isinstance(dst, Reg):
        fail(ins.line, ins.text, "LEA into something that is not a register")
    if not isinstance(src, Mem):
        fail(ins.line, ins.text, "LEA of something that is not a memory operand")
    d = reg68(dst, ins)
    if src.index is not None:
        return _lea_indexed(ctx, ins, src, d)

    if src.base is not None and src.sym is not None:
        # `lea ecx,[edx+DATA_007854]` -- a register plus a label's address.
        # Into an ADDRESS register this is MOVEA + ADDA, both CCR-free, so it
        # is flag-exact like the x86 LEA. Into a DATA register it is MOVE +
        # ADD, which write flags the x86 does not -- declared, checked dead.
        b = XREG[src.base]
        s = "#%s%s" % (canon(src.sym),
                       "%+d" % src.disp if src.disp else "")
        if is_areg(d):
            lines = ([] if b == d else [op("movea.l", b, d)])
            lines.append(op("adda.l", s, d))
            return Lowered(lines,
                           why="`lea aD,[reg+DATA]`: MOVEA + ADDA, neither of "
                               "which touches the CCR -- flag-exact against "
                               "the x86 LEA, which writes none.")
        lines = ([] if b == d else [op("move.l", b, d)])
        lines.append(op("add.l", s, d))
        return Lowered(lines, ccr_extra=F.ALLX,
                       why="`lea rD,[reg+DATA]` with a data-register result is "
                           "an ADD of the label's address (PLAN 5's invariant: "
                           "the assembler puts a 68k address there). x86 LEA "
                           "writes no flags and MOVE/ADD write all five, so "
                           "all are declared spurious and checked dead.")

    if src.base is not None and src.sym is None:
        b = XREG[src.base]
        if is_areg(b) and is_areg(d):
            return Lowered([op("lea", "(%d,%s)" % (src.disp, b), d)],
                           why="a real LEA: address register in, address "
                               "register out, and like the x86 LEA it touches "
                               "no flags.")
        if is_areg(b):
            # `lea eax,[ebp-0x4]` -- the ADDRESS OF A LOCAL, wanted in a data
            # register. 144 functions refuse on this and it is the third largest
            # class in the game. The 68k LEA works only between address
            # registers, so the address is moved out and adjusted with ordinary
            # arithmetic; _lea_disp picks the shortest form.
            #
            # A note on why this is safe even though it looks like it dodges the
            # addressing unit: the value produced is identical either way, and
            # PLAN 5's invariant is untouched -- the number in the register is a
            # real 68k stack address, because a5 holds one. Nothing here can
            # manufacture an x86 address.
            return Lowered([op("move.l", b, d)] + _lea_disp(d, src.disp),
                           ccr_extra=F.ALLX,
                           why="`lea rD,[An+disp]` is an addition once the "
                               "result has to land in a DATA register: the 68k "
                               "LEA writes only address registers. In practice "
                               "this is always the address of a local, i.e. "
                               "[ebp-N].\n"
                               "x86 LEA writes no flags and MOVE/ADDQ/SUBQ/ADD "
                               "write all of them -- and SUBQ/ADDQ write X as "
                               "well -- so all five are declared spurious and "
                               "the liveness pass has to report them dead.")
        # Base in a data register and result wanted in one: this is not an
        # address computation on the 68k at all, it is an add. The x86 LEA sets
        # no flags and every 68k way of doing this does, so all five are
        # declared spurious and have to be dead.
        lines = [] if b == d else [op("move.l", b, d)]
        if src.disp:
            if 1 <= src.disp <= 8:
                lines.append(op("addq.l", "#%d" % src.disp, d))
            elif -8 <= src.disp <= -1:
                lines.append(op("subq.l", "#%d" % -src.disp, d))
            else:
                lines.append(op("add.l", imm68(src.disp), d))
        if not lines:
            lines = [op("move.l", b, d)]
        return Lowered(lines, ccr_extra=F.ALLX,
                       why="`lea r,[dN+disp]` is an addition, not an addressing "
                           "mode: the 68k LEA works only between address "
                           "registers. x86 LEA writes no flags and MOVE/ADDQ/ADD "
                           "write all of them, so every flag is declared "
                           "spurious and checked dead.")

    if src.base is None and src.sym is not None:
        s = src.sym if not src.disp else "%s%+d" % (src.sym, src.disp)
        if is_areg(d):
            return Lowered([op("lea", "(%s).l" % s, d)],
                           why="address of a label into an address register; no "
                               "flags on either side.")
        return Lowered([op("move.l", "#" + s, d)], ccr_extra=F.ALL,
                       why="address of a label into a data register: MOVE.L of "
                           "the address as an immediate. PLAN 5's invariant "
                           "holds -- the assembler puts a 68k address there, so "
                           "no x86 address ever exists at run time.")

    fail(ins.line, ins.text, "unsupported LEA form")



# The string operations. PLAN 3.5 left these until something needed them; the
# survey says 192 functions do (152 `rep movsd`, 40 `rep movsb`), which makes it
# the largest single class of refusal left.
#
# DIRECTION IS FORWARD, AND THAT IS MEASURED, NOT ASSUMED. x86 string moves run
# backwards when DF is set, and reproducing that would double this rule. A grep
# over the whole of ffe.asm finds **zero** `std` and **zero** `cld`: the game
# never touches the direction flag, so DF holds whatever it had on entry, and
# cdecl requires that to be clear. Forward it is. If a `std` ever appears, this
# rule is wrong and must be revisited -- hence the check being written down here
# rather than remembered.
#
# The registers are the x86 ones by definition: count in ecx (d1), source in esi
# (a2), destination in edi (a3). Post-increment addressing is exactly what the
# x86 does to esi/edi, so the register state afterwards matches without any
# fixing up: d1 = 0, a2 and a3 advanced by size * count.
STRING_SIZE = {"movsb": 1, "movsw": 2, "movsd": 4}


def _string_op(ins):
    # `rep movsd` parses as mnem `rep` + operand Sym('movsd'); a BARE `movsw`
    # parses as mnem `movsw` with no operands -- both spellings land here.
    (which,) = ins.ops if ins.ops else (None,)
    name = which.name.lower() if isinstance(which, Sym) else ins.mnem
    if name not in STRING_SIZE:
        fail(ins.line, ins.text,
             "only the MOVS string moves are handled (%s). SCAS/STOS/CMPS and "
             "the REPNE forms each need their own rule -- REPNE SCASB in "
             "particular has to leave the flags of its LAST comparison behind, "
             "which a loop written for MOVS does not."
             % ", ".join(sorted(STRING_SIZE)))
    return name, STRING_SIZE[name]


def lo_movs(ctx, ins):
    """A single MOVS, no REP prefix: one element from (esi) to (edi)."""
    name, size = _string_op(ins)
    return Lowered(
        [op("move" + SIZE_SFX[size], "(%s)+" % XREG["esi"], "(%s)+" % XREG["edi"])],
        ccr_extra=F.ALL,
        why="MOVS%s copies one element from (esi) to (edi) and advances both. "
            "68k post-increment addressing is the same operation on the same "
            "two registers.\n"
            "x86 MOVS writes no flags; 68k MOVE writes N/Z and clears V/C, so "
            "all four are declared spurious and checked dead. X is untouched by "
            "both." % name[-1].upper())


def lo_rep(ctx, ins):
    """`rep movsb/movsw/movsd` -- the block copy, as a counted loop."""
    name, size = _string_op(ins)
    top = ".rep%d" % ins.line
    end = ".repend%d" % ins.line
    return Lowered(
        [op("tst.l", XREG["ecx"]),
         op("beq", end),
         label(top),
         op("move" + SIZE_SFX[size], "(%s)+" % XREG["esi"], "(%s)+" % XREG["edi"]),
         op("subq.l", "#1", XREG["ecx"]),
         op("bne", top),
         label(end)],
        ccr_extra=F.ALLX,
        why="`rep %s` copies ecx elements from (esi) to (edi) forwards. The 68k "
            "has no such instruction, so it is a counted loop -- and the loop "
            "leaves exactly the register state the x86 leaves: d1 (ecx) zero, "
            "a2 and a3 (esi/edi) advanced by %d * count, because post-increment "
            "addressing does to them what the x86 does.\n"
            "THE ZERO CASE IS NOT OPTIONAL: x86 `rep` with ecx = 0 copies "
            "nothing and leaves esi/edi alone, so the loop is entered only "
            "after TST/BEQ. A do-while would copy one element too many at every "
            "site where the count can be zero.\n"
            "FORWARD ONLY, and that is measured: the game contains zero `std` "
            "and zero `cld`, so DF is never set and cdecl guarantees it clear "
            "on entry. A backward MOVS is refused rather than guessed.\n"
            "x86 REP MOVS writes no flags at all, while TST/SUBQ between them "
            "write all five, so all five are declared spurious and the liveness "
            "pass has to report them dead.\n"
            "The labels are vasm LOCAL labels (a leading dot), so they belong to "
            "the enclosing function symbol and cannot collide with another "
            "function's loop. The ffe.asm line number keeps two loops in the "
            "SAME function apart." % (name, size))



def lo_repne(ctx, ins):
    """`repne scasb` -- the strlen idiom, as a counted loop."""
    (which,) = ins.ops if ins.ops else (None,)
    name = which.name.lower() if isinstance(which, Sym) else None
    if name != "scasb":
        fail(ins.line, ins.text,
             "only REPNE SCASB is handled -- the other REPNE forms have no "
             "site in the game to be proven against")
    top = ".rns%d" % ins.line
    end = ".rnse%d" % ins.line
    return Lowered(
        [label(top),
         op("tst.l", XREG["ecx"]),
         op("beq", end),
         op("subq.l", "#1", XREG["ecx"]),
         op("cmp.b", "(%s)+" % XREG["edi"], "d0"),
         op("bne", top),
         label(end)],
        ccr_wrong=F.ALLX,
        why="REPNE SCASB scans (edi) forwards for al, decrementing ecx per "
            "element INCLUDING the matching one, and leaves edi past the "
            "match. The loop reproduces that register state exactly: the "
            "counter check comes first (ecx = 0 scans nothing and the "
            "not-taken path leaves edi/ecx alone, same as the x86), then "
            "decrement, then `cmp.b (a3)+,d0` -- which computes al - [edi] in "
            "the same operand order as SCASB -- then loop while not equal.\n"
            "FLAGS ARE DECLARED UNRELIABLE, ALL FIVE: x86 leaves the flags of "
            "the LAST comparison (or the incoming flags when ecx = 0), while "
            "this loop's exhausted path exits through TST. Measured over the "
            "whole game: every one of the 38 REPNE SCASB sites is followed by "
            "`not ecx` -- the strlen idiom -- and NOTHING reads a flag after "
            "it, so the liveness pass proves the difference dead at every "
            "site rather than trusting this comment.\n"
            "FORWARD ONLY, same measurement as REP MOVS: the game contains "
            "zero `std`.")


# ---- setcc / xchg -----------------------------------------------------------
#
# x86 SETcc -> 68k Scc, and the CONDITION TABLE IS NOT REWRITTEN: `set<cc>` uses
# exactly the condition `j<cc>` uses, so the mapping is derived from flags.CC by
# turning its branch mnemonic `b<xx>` into `s<xx>`. Writing a second table would
# be a second chance to get a condition backwards, which is the classic bug this
# project keeps naming.
#
# The one real difference is the VALUE. x86 SETcc stores 0 or 1; the 68k Scc
# stores 0x00 or 0xFF. `neg.b` turns 0xFF into 0x01 and leaves 0x00 alone, in
# two bytes -- `andi.b #1` would need four.
#
# Measured: all 96 SETcc sites in the game target al, cl or dl, i.e. a data
# register at byte width, and every one of the eight spellings that occurs is
# covered by flags.CC.
SETCC = {}
for _c, (_b, _u) in F.CC.items():
    SETCC["set" + _c[1:]] = ("s" + _b[1:], _u)


def lo_setcc(ctx, ins):
    (dst,) = ins.ops
    scc, _used = SETCC[ins.mnem]
    pre = []
    if not isinstance(dst, Reg) or dst.size != 1:
        fail(ins.line, ins.text,
             "SETcc into something other than an 8-bit register -- the byte "
             "memory form exists on x86 but no translated function needs it")
    d = reg68(dst, ins, 1)
    return mk(pre, [op(scc, d), op("neg.b", d)],
              ccr_extra=F.ALLX,
              why="`%s` -> `%s` + NEG.B. The 68k Scc stores 0x00 or 0xFF where "
                  "the x86 stores 0 or 1, and NEG.B maps 0xFF to 0x01 and 0x00 "
                  "to 0x00 -- two bytes, against four for ANDI.B #1.\n"
                  "The condition comes from the SAME table the branches use "
                  "(flags.CC), not from a second one: `set<cc>` and `j<cc>` "
                  "read the same flags, and duplicating the mapping would be a "
                  "second chance to invert a condition.\n"
                  "Scc itself does not affect the CCR, but NEG.B writes all "
                  "five, and x86 SETcc writes none -- so all five are declared "
                  "spurious and the liveness pass has to report them dead."
                  % (ins.mnem, scc))


def lo_xchg(ctx, ins):
    a, b = ins.ops
    if not (isinstance(a, Reg) and isinstance(b, Reg)
            and a.size == 4 and b.size == 4):
        fail(ins.line, ins.text,
             "XCHG with a memory operand or a narrow register -- EXG swaps two "
             "whole 32-bit registers and nothing else. All 40 sites in the game "
             "are register pairs, so the other forms are refused rather than "
             "written blind.")
    return Lowered([op("exg", reg68(a, ins), reg68(b, ins))],
                   why="EXG swaps two registers, of either kind, and affects no "
                       "condition codes -- exactly as x86 XCHG does. Flag-exact "
                       "with nothing to prove.")


INDIRECT_CALL_NOTE = (
    "Indirect CALL (PLAN 5). Neither x86 CALL nor 68k MOVEA/JSR touches a "
    "flag, so the pair is flag-exact with nothing to prove. The pointer being "
    "jumped through is ALWAYS a 68k address of a bare t-ABI body label: every "
    "store into these slots is either the assembler resolving a `dd FUNC_x` "
    "in converted data or translated code doing `pea (FUNC_x).l` / "
    "`move.l #FUNC_x`, and both emit the bare label. No x86 address exists at "
    "run time, and no C-ABI wrapper address may ever be stored here -- a C "
    "function pointer would need a reverse thunk, which the platform layer "
    "must provide on its own side of the fence.")


def lo_call(ctx, ins):
    (t,) = ins.ops
    if isinstance(t, Reg):
        if t.size != 4:
            fail(ins.line, ins.text, "CALL through a narrow register")
        r = reg68(t, ins)
        if is_areg(r):
            return Lowered([op("jsr", "(%s)" % r)], why=INDIRECT_CALL_NOTE)
        return Lowered([op("movea.l", r, "a0"), op("jsr", "(a0)")],
                       why=INDIRECT_CALL_NOTE)
    if isinstance(t, Mem):
        pre = []
        src = ea(t, ins, want_size=4, pre=pre)
        return Lowered(pre + [op("movea.l", src, "a0"), op("jsr", "(a0)")],
                       why=INDIRECT_CALL_NOTE)
    if not isinstance(t, Sym):
        fail(ins.line, ins.text, "unsupported CALL target form")
    return Lowered(
        [op("jsr", canon(t.name))],
        why="JSR pushes a 4-byte return address exactly as CALL does, and cdecl "
            "argument layout is m68k C argument layout, so the caller-side "
            "PUSH / `add esp,N` around this need no adjustment (PLAN 3.3).\n"
            "The target is the BARE body label, not the underscored C-ABI "
            "wrapper: translated code calls translated code under the t-ABI, "
            "where d0/d1/d2/d7/a0/a1 are caller-saved. Note also what this is "
            "NOT doing: FUNC_001521 is called from 441 sites and inlining it is "
            "the obvious optimisation, but that decision belongs to a measured "
            "step (PLAN 3.4/M6) and baking it in here would cement an untested "
            "choice everywhere at once.")


def lo_jmp(ctx, ins):
    (t,) = ins.ops
    if isinstance(t, Mem):
        # `jmp near [edx*4+DATA_x]` -- the jump-table dispatch. The CFG pass
        # has already proven DATA_x is an embedded table of this function's
        # own labels, and the emitter writes it as dc.l, so the slot loaded
        # here holds a 68k address. Neither x86 JMP nor MOVEA/JMP touches a
        # flag.
        pre = []
        src = ea(t, ins, want_size=4, pre=pre)
        return Lowered(pre + [op("movea.l", src, "a0"), op("jmp", "(a0)")],
                       why="indirect JMP through an embedded jump table; see "
                           "the dc.l block below the dispatch. Flag-exact "
                           "for free, like the indirect CALL.")
    if not isinstance(t, Sym):
        fail(ins.line, ins.text, "unsupported JMP target form")
    return Lowered([op("bra", canon(t.name))])


def lo_jcc(ctx, ins):
    (t,) = ins.ops
    if not isinstance(t, Sym):
        fail(ins.line, ins.text, "indirect conditional branch")
    b, used = F.CC[ins.mnem]
    return Lowered([op(b, canon(t.name))],
                   why="%s -> %s; reads %s" % (ins.mnem, b, "".join(sorted(used))))


def lo_ret(ctx, ins):
    if ins.ops:
        fail(ins.line, ins.text, "RET with a stack adjustment is not cdecl")
    return Lowered([op("rts")])


RULES = {
    "push": lo_push, "pop": lo_pop, "mov": lo_mov, "lea": lo_lea,
    "movsx": lo_movsx, "movzx": lo_movzx, "cdq": lo_cdq, "nop": lo_nop,
    "imul": lo_imul, "mul": lo_mul, "div": lo_div, "idiv": lo_idiv,
    "shl": lo_shl, "sal": lo_shl, "shr": lo_shr, "sar": lo_sar, "rcl": lo_rcl,
    "add": lo_add, "adc": lo_adc, "sub": lo_sub, "sbb": lo_sbb,
    "and": lo_and, "or": lo_or, "xor": lo_xor, "not": lo_not, "neg": lo_neg,
    "test": lo_test, "inc": lo_incdec, "dec": lo_incdec,
    "cmp": lo_cmp, "call": lo_call,
    "rep": lo_rep, "repne": lo_repne,
    "movsb": lo_movs, "movsw": lo_movs, "movsd": lo_movs,
    "xchg": lo_xchg,
    "jmp": lo_jmp, "ret": lo_ret,
}
for _cc in F.CC:
    RULES[_cc] = lo_jcc
for _sc in SETCC:
    RULES[_sc] = lo_setcc


class _Ctx:
    """What a rule may ask about its surroundings. Today: only the flag
    liveness AFTER the instruction, so a rule can pick a CCR-free form when
    the flags are dead (keeping every already-green object byte-identical)
    and pay for a flag-exact detour only when something downstream reads
    them."""
    __slots__ = ("live_after",)


def lower(ctx, ins, live_after):
    """Select code for one instruction and prove its flag behaviour legal."""
    rule = RULES.get(ins.mnem)
    if rule is None:
        fail(ins.line, ins.text,
             f"no translation rule for {ins.mnem!r}. Handled so far:\n"
             + HANDLED)

    if ctx is None:
        ctx = _Ctx()
    ctx.live_after = live_after
    lw = rule(ctx, ins)

    # A line registered in le_read_sites.tsv MUST come out of the lowering with
    # a byte swap in it. The swap check lives inside each lowering rule, and a
    # rule that returns down a path where the check was never reached produces
    # NO refusal and NO warning -- just the raw x86 bytes, silently. That is
    # exactly what `cmp edi,[eax+0x4]' did at ffe.asm:105331 (the CMPA.L branch
    # returned first), and it turned a loop bound of 7 into 0x07000000 and a
    # bounded write loop into a wild write over the exception vectors. The
    # width guard in _site_swap only catches the list drifting from the
    # disassembly; this catches the LOWERING drifting from the list.
    if storage.le_site(ins.line) is not None:
        if not any(l.text and l.text.split("\t")[0] in ("rol.w", "swap")
                   for l in lw.lines):
            fail(ins.line, ins.text,
                 "le_read_sites.tsv registers this line as a pointer-based "
                 "access to LE static data, but the lowering emitted no byte "
                 "swap for it. Either the rule that handled this instruction "
                 "returns before its swap check runs (add the check to that "
                 "path, as lo_cmp's CMPA.L branch needed), or the registration "
                 "is on the wrong line. Do NOT silence this by deleting the "
                 "entry: a missing swap here reads x86 bytes as 68k values.")

    bad = live_after & (lw.ccr_extra | lw.ccr_wrong)
    if bad:
        fail(ins.line, ins.text,
             "the 68k form would corrupt flag(s) %s, which are still live "
             "here. PLAN 4.2 lists the three ways out (reorder, a CCR-free "
             "form, or move ccr,d7 / move d7,ccr around it); none is "
             "implemented yet because nothing measured needs one."
             % "".join(sorted(bad)))
    return lw
