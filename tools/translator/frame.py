#!/usr/bin/env python3
"""The frame map: which stack slots are 4-byte scalars, and what that does to a
sub-word access.

WHY THIS EXISTS. The oracle caught a silent miscompile in
FUNC_001612_DrawCircle at ffe.asm:96611:

    mov al,[ebp+0x10]        ; the colour argument, low byte

cdecl pushes a FOUR-BYTE slot. On x86 the low byte of that longword sits at
displacement +0; on a 68k it sits at +3, because the same 32-bit value is stored
the other way round. Translating the displacement unchanged reads the wrong
byte -- and it reads 0 for every colour <= 255, so DrawCircle drew its outline
in colour 0 while its end caps, which go through a different path, were right.
22 records out of 4037. A wrong answer that looks almost right.

WHY IT CANNOT BE A DEFAULT. The correction depends on what the slot HOLDS:

    a 4-byte scalar read at byte offset k   ->  the 68k offset is 3 - k
    a 4-char array read at byte offset k    ->  the 68k offset is k, unchanged

Nothing in the instruction itself distinguishes them, so `lower.ea()` refuses
every sub-word [ebp/esp+d] access outright unless this module has proved the
slot.

WHAT COUNTS AS A PROOF. A slot is a 4-byte scalar if the SAME FUNCTION also
touches it at full dword width. That is a fact about the original x86 code, read
straight off it, not a heuristic: a byte array would never be loaded whole into
a 32-bit register by the code that also indexes it a byte at a time, and if it
were, the dword access would itself be endianness-sensitive and the whole
function would need looking at anyway.

DrawCircle passes: `mov eax,[ebp+0x10]` appears at its other call site, so the
slot is provably a 32-bit scalar and `mov al,[ebp+0x10]` provably means "its low
byte".

FUNC_001491..001494 do NOT pass. They read `word [ebp+0x8]` and `word [ebp+0xa]`
and never touch the slot at dword width, so nothing here can tell a packed pair
of shorts from a struct of two shorts passed by value -- and the two want
different code. They stay refused, and answering them means looking at what
their callers push.

THE ARITHMETIC. For a proven slot whose dword access is at displacement `s`, an
access of width `w` at displacement `d = s + k` becomes

    d' = s + (4 - w - k)

which is 3-k for a byte and 2-k for a word, i.e. exactly the little-endian to
big-endian offset flip inside a 4-byte quantity.

THE CONNECTION TO THE DATA PIPELINE, which is not handled here and is not
solved. Static game data has the same problem seen from the other side: once a
region is byte-swapped to big-endian longwords, a `word [DATA_x+2]` into it
needs this same correction, and if the region is converted as words instead it
does not. The sub-word displacement rule and the data-typing pipeline are one
problem with two faces. This module deals only with the stack, where the slot
width is a property of the calling convention and can be proved.
"""
from dataclasses import replace

import lower as L
from parse import Insn, Mem, TranslationError

STACK_BASES = ("ebp", "esp")


def _width(ins, o):
    """The width this memory operand is accessed at, or None if it cannot be
    read off the instruction. An explicit NASM size prefix wins; otherwise the
    register operands decide, exactly as lower.op_size does. Never an immediate
    -- `add esp,byte +0x10` is an encoding hint."""
    if o.size is not None:
        return o.size
    try:
        return L.op_size(ins)
    except TranslationError:
        return None


def _slots(fn):
    """{(base, disp): set of widths} for every plain [ebp/esp+disp] operand.
    Indexed operands are skipped: they are refused elsewhere, and an index makes
    the displacement meaningless as a slot identity."""
    seen = {}
    for ins in fn.insns:
        # `LEA` IS NOT AN ACCESS -- it computes an address and reads nothing,
        # so its memory operand must not contribute a width. _width() falls
        # back to op_size(), which for `lea esi,[ebp-0x10]` reports 4 from the
        # DESTINATION register; that marked the slot a proven dword and the
        # enclosing-slot rule then flipped every byte written to it.
        #
        # The effect was exactly backwards. A `lea` on a stack slot is the
        # strongest evidence available that the slot is a BYTE ARRAY -- the
        # address escapes, and whoever receives it addresses the bytes in
        # ascending order -- yet it was the one thing forcing the flip.
        #
        # Measured, not assumed: this is the common cause behind every
        # escaping-aggregate miscompile found on 2026-08-02 (FUNC_001761,
        # FUNC_001695, FUNC_001712 x3, FUNC_001692), each of which pairs
        # `lea` with byte stores. FUNC_001692 is the clean demonstration --
        # all seven of its accesses to [ebp-0x10..-0x0e] are BYTE writes plus
        # one `lea`, no dword access exists anywhere in the function, and the
        # bytes were still scattered to (-13,a5)/(-14,a5)/(-15,a5) while
        # `lea (-16,a5),a2` and `rep movsd` copied from -16. Byte 0 of the
        # copied block was never written at all.
        # NOT ACTED ON -- MEASURED AND REVERTED, 2026-08-02. Skipping `lea`
        # here re-translates 50 functions and NONE of them is oracle-carried,
        # so the 5101 vectors cannot license it; it also makes FUNC_001700
        # refuse (a 16-bit access at [ebp-4] it can no longer prove), i.e. it
        # COSTS coverage. This module's own header records that the blanket
        # incoming-argument rule was reverted for exactly this reason -- the
        # oracle stayed green while the game stopped taking the splash keys,
        # because the oracle can license a rule only over what it covers.
        # The five 2026-08-02 miscompiles are therefore fixed one function at
        # a time through PROVEN_SUBWORD below, each with its own evidence.
        # To land the general rule later: pick oracle carriers that pair `lea`
        # with byte stores on a stack slot, add them to the vector list FIRST,
        # then re-enable this and require both the vectors and the 50 diffs to
        # be explained.
        # if ins.mnem == "lea":
        #     continue
        for o in ins.ops:
            if (isinstance(o, Mem) and o.base in STACK_BASES
                    and o.index is None and o.sym is None):
                w = _width(ins, o)
                if w is not None:
                    seen.setdefault((o.base, o.disp), set()).add(w)
    return seen


# Slots proven 4-byte scalars BY READING THE CALLERS, not by an in-function
# dword access -- the annotation channel THE BUG's write-up called for. Every
# entry must cite its evidence; an entry without a caller reading is a guess
# and does not belong here.
#
#   FUNC_001326_DPal32to16: reads bytes [ebp+8]/[+9]/[+0xa] (b, g, r of an
#   RGB32). All call sites go through the [DATA_007629] slot and every one
#   pushes a full dword (`mov eax,[ebp-0x3] / push eax` at ffe.asm:12605+,
#   `mov eax,[DATA_009217] / push eax` at 82970+, 93613+). One pushed
#   32-bit value, picked apart bytewise -- a scalar.
#
#   FUNC_001598_DrawChar: `movsx eax,byte [ebp+0x8]` (arg 1, the character)
#   and `mov bl,[ebp+0xc]` (arg 2, the colour). Both direct call sites push
#   whole dwords: `push ebx / push byte +0x0` at ffe.asm:94002-94003 and
#   `push ebx / push eax` at 94013-94014. Pushed dwords read at their low
#   bytes -- scalars.
#
#   FUNC_001415_GuiSetLastAction / FUNC_001410_...Internal: the action code.
#   Every site of the [DATA_007673] slot pushes a whole register (`push ebx`
#   at ffe.asm:10493, 10678, 13896), and 1415 forwards with `push eax`.
#   Pushed dwords read at their low byte -- scalars.
#   FUNC_000873 (solar-system body setup): word reads of args at +0x14,
#   +0x28, +0x34, +0x38 and a byte read at +0x2c -- five of the 14 dword
#   cdecl args (`add esp,byte +0x38` at every site). All sites live in
#   FUNC_000875 and push whole dwords -- `push byte +0x0` runs at
#   ffe.asm:61285-61299, 61303-61317... (a byte-immediate push is still a
#   4-byte push).
PROVEN_BY_CALLERS = {
    "FUNC_001326": {("ebp", 8)},
    "FUNC_001598": {("ebp", 8), ("ebp", 12)},
    "FUNC_001415": {("ebp", 8)},
    "FUNC_001410": {("ebp", 8)},
    "FUNC_000873": {("ebp", 0x14), ("ebp", 0x28), ("ebp", 0x2c),
                    ("ebp", 0x34), ("ebp", 0x38)},
    # FUNC_000874 (system init, 873's sibling): `mov ax,[ebp+0x18]` and
    # `mov ax,[ebp+0x1c]` -- args 5 and 6 of a six-dword cdecl call. All
    # sites sit in FUNC_000875 (ffe.asm:61382..61531) and push whole dwords:
    # `push byte +0x5 / push byte +0x54` are 4-byte pushes.
    "FUNC_000874": {("ebp", 0x18), ("ebp", 0x1c)},
    # FUNC_000832: `mov al,[ebp+0xc]` and `mov al,[ebp+0x10]` -- args 2 and 3.
    # All four direct sites push whole dwords: `push edx` (masked 0xff) and
    # `push dword [edi+eax*4+0xc]` at ffe.asm:56254-56259, and
    # `push dword [ebp-0x10/-0xc/-0x8]` at 57005-57010 / 57070-57075 / 57686+.
    "FUNC_000832": {("ebp", 0xc), ("ebp", 0x10)},
    # FUNC_001691 (model vertex colour add): one dword arg picked apart --
    # byte RMWs at +0xc/+0xd/+0xe and word reads of +0xc. All 16 call sites
    # (ffe.asm:103488..129460, the model handlers) push whole dwords:
    # `push dword [eax+0x4]` of a material struct / `push dword [ebp-0x40]`.
    "FUNC_001691": {("ebp", 0xc)},
    # The PACKED-FLOAT family (mantissa in the LOW word, exponent in the
    # HIGH word of ONE pushed dword; every access is a word read of one
    # half). All-word access made the single-width rule pass the slots
    # through UNFLIPPED, which on the big-endian stack swaps mantissa and
    # exponent -- caught live 2026-07-30: the starport-placement chain
    # (FUNC_000882 -> 1463/1491/1482/1483) hung the new game on garbage
    # values. Proving the slots dword restores the frame.py flip.
    # (This also retires LEFTOFF's old open question about what the
    # callers push for 1491..1494: whole dwords, like every cdecl arg.)
    "FUNC_001463": {("ebp", 0x8)},
    "FUNC_001482": {("ebp", 0x8), ("ebp", 0xc)},
    "FUNC_001483": {("ebp", 0x8), ("ebp", 0xc)},
    "FUNC_001486": {("ebp", 0x8), ("ebp", 0xc)},
    "FUNC_001491": {("ebp", 0x8)},
    "FUNC_001492": {("ebp", 0x8)},
    "FUNC_001493": {("ebp", 0x8)},
    "FUNC_001494": {("ebp", 0x8)},
    # FUNC_001712 (quad-draw colour mix): a LOCAL byte triple at
    # [ebp-0x17..-0x15], written once as a word at the ODD offset -0x17 and
    # then byte-RMWed per component. No dword access exists to prove the
    # slot, but the triple lives entirely inside the -0x18 dword and every
    # access flips consistently under the k'=4-w-k rule (the odd-offset
    # word lands at BE offset 1: legal on the 68020). -0x18 itself is
    # untouched padding.
    "FUNC_001712": {("ebp", -0x18)},
    # FUNC_001708 (triangle-draw colour mix): the same byte-triple idiom at
    # [ebp-0x23..-0x21] with the word store at the odd offset -0x23; slot
    # base -0x24, the base byte untouched.
    # PLUS arg5 at [ebp+0x18], the THIRD VERTEX INDEX, read only as bytes
    # (`movsx eax,byte [ebp+0x18]` 104372/104378, `mov al,[ebp+0x18]` 104384).
    # Both call sites (FUNC_001707, ffe.asm:104255 and 104293) push it as
    # `xor eax,eax / mov al,[ebx+0x2] / (xor eax,1) / push eax` -- a zero-
    # extended dword. Unflipped, the 68k read its HIGH byte (always 0), so
    # every textured triangle of every model used vertex 0 as its third
    # corner: measured 2026-09-26 as ITEMLOG `I 1807 ... 169 53` repeated on
    # ours where the reference's third point varies -- the spiky, fanned
    # asteroids.
    "FUNC_001708": {("ebp", -0x24), ("ebp", 0x18)},
    # The 6-byte packed-vector idiom (combat/nav family): a dword store fills
    # one local slot (auto-proven) and a WORD store fills the low half of the
    # next; byte reads pick the vector apart. The word slot needs the hand
    # proof; its upper two bytes are untouched padding. Flips check out:
    # word@k=0 -> +2, byte@k=0 -> +3, neighbours live in the auto slot.
    "FUNC_000580": {("ebp", -0x4)},
    "FUNC_000581": {("ebp", -0xc)},
    "FUNC_000820": {("ebp", -0x8)},
    "FUNC_000823": {("ebp", -0x10)},
    # FUNC_000830: word reads of [ebp+0x14] and byte reads of [ebp+0x17] --
    # one dword arg picked apart (0x17 is byte 3 of the 0x14 slot). All eight
    # sites push whole dwords: `push eax / push edx / push dword [ebp+0x10] /
    # push dword [ebp-0x4]` at ffe.asm:56584-56588, `push byte +0x0 / lea+push
    # / push dword DATA_008656` at 56639-56643, and the rest alike.
    "FUNC_000830": {("ebp", 0x14)},
    # FUNC_001495 (packed float -> 64-bit): arg2 at [ebp+0xc] is ONE pushed
    # dword, mantissa in the low word (`movsx eax,word [ebp+0xc]`) and
    # exponent in the high word (`movsx eax,word [ebp+0xe]`). All five call
    # sites (ffe.asm:3560, 3573, 41988, 91018, 91030) build it in a local as
    # two words and push it whole (`mov eax,[ebp-0x4] / push eax`). Unflipped,
    # the 68k used the exponent as the mantissa: FUNC_000036 wrote the planet
    # radii obj+0x138/+0x140 as {0, 31744} where the PC has {1649999872, 0}
    # (31 << 42), FUNC_000581's gate rejected every terrain-height query,
    # FUNC_000574 returned -1 and every starport sat ~11% too far out --
    # measured 2026-10-09 with the paired P581 probe.
    "FUNC_001495": {("ebp", 0xc)},
    # FUNC_001053 (camera eye offset for the cockpit views): arg3 at
    # [ebp+0x10] is read only as a word (ffe.asm:77511/77513/77518/77523) and
    # multiplied into the camera's up row. All four call sites in FUNC_001054
    # push a whole dword whose LOW word carries the value: `push byte +0x0`
    # (77639) and `mov dx,[eax+0x1a] / push edx` (77705-77711, 77728-77734,
    # 77766-77772). Unflipped, the 68k read the HIGH word -- stale d2 -- so
    # the landed camera hovered 194299 units above the pad instead of 7593
    # (FRAME1478 2026-10-09: every surface object low by 194299, the planet
    # centre by 194299/32, the sun by ~1; all matrices identical).
    "FUNC_001053": {("ebp", 0x10)},
    # FUNC_001629_DrawPixelToBuf(x, y, col): `mov cl,[ebp+0x10]` -- the colour
    # byte of a pushed dword. One call site (ffe.asm:59325-59328, the galaxy
    # map): `push dword [eax*4+DATA_006821]`, a dword-only colour table.
    "FUNC_001629": {("ebp", 0x10)},
    "FUNC_001444": {("ebp", 0x8), ("ebp", 0xc)},   # GuiSetXYAccum(word x, word y): all four callers push whole dwords (push byte 0 / push dword 0x100 / movsx+push eax, ffe.asm:56881-56883, 57273-57275, 72238-72240, 74434-74438); unflipped the accumulators were always set to 0 and the first flight frame saw a (0-0x200)<<6 pitch kick (2026-10-09)
}

# THE MODULE-EVENT HANDLER FAMILY. The 14 subtables of DATA_004892 ("Module
# event functions tables", ffedat.asm:3736-3750) hold 52 handlers, and every
# one is reached ONLY through the dispatchers, which push whole dwords:
# FUNC_000144 does `mov edx,[ebp+0x10] / push edx / call eax`
# (ffe.asm:14837-14840), FUNC_000148 the same shape. The handlers read their
# event-code argument as a byte (`mov al,[ebp+0x8]`) -- a pushed dword read
# at its low byte, i.e. a scalar, for the whole family by the same proof.
# Collected mechanically from the tables; the first two members to surface at
# runtime (FUNC_000259, FUNC_000687) were verified by hand first.
for _f in ("FUNC_000118 FUNC_000209 FUNC_000216 FUNC_000218 FUNC_000224 "
           "FUNC_000227 FUNC_000232 FUNC_000258 FUNC_000259 FUNC_000273 "
           "FUNC_000320 FUNC_000321 FUNC_000339 FUNC_000343 FUNC_000354 "
           "FUNC_000357 FUNC_000616 FUNC_000647 FUNC_000648 FUNC_000660 "
           "FUNC_000673 FUNC_000687 FUNC_000705 FUNC_000707 FUNC_000779 "
           "FUNC_000828 FUNC_000829 FUNC_000834 FUNC_000839 FUNC_000840 "
           "FUNC_000864 FUNC_000866 FUNC_000867 FUNC_000876 FUNC_000959 "
           "FUNC_000962 FUNC_000963 FUNC_000964 FUNC_000966 FUNC_000977 "
           "FUNC_000982 FUNC_000985 FUNC_001000 FUNC_001002 FUNC_001006 "
           "FUNC_001007 FUNC_001009 FUNC_001010 FUNC_001023 FUNC_001099 "
           "FUNC_001103 FUNC_001110").split():
    PROVEN_BY_CALLERS.setdefault(_f, set()).add(("ebp", 8))

# BULK-REVIEWED SLOTS (2026-10-09): tools/translator/proven_slots.tsv, one
# `function <tab> ebp displacement <tab> evidence' row per slot whose callers
# were all read and found to push a scalar dword.
def _load_proven_slots():
    import os as _os
    p = _os.path.join(_os.path.dirname(_os.path.abspath(__file__)), "proven_slots.tsv")
    if not _os.path.exists(p):
        return
    for l in open(p, encoding="utf-8"):
        if l.startswith("#") or not l.strip():
            continue
        f = l.rstrip(chr(10)).split(chr(9))
        PROVEN_BY_CALLERS.setdefault(f[0], set()).add(("ebp", int(f[1], 16)))


_load_proven_slots()

#   FUNC_000927 (the object allocator), slot ebp+0x10 -- the allocation FLAG.
#   Read only as a byte (`mov al,[ebp+0x10]` and `test byte [ebp+0x10],0x20`,
#   ffe.asm:3cc85-3cc8f), so no in-function dword access proves it and the
#   single-width rule was passing the displacement through unchanged. Both
#   call sites push a whole dword: FUNC_000880 pushes the immediate
#   `push byte +0x1d` (ffe.asm:64620 -- a byte-form PUSH still writes four
#   bytes) and FUNC_000996's path reaches it through FUNC_000928's
#   `push byte +0x4f`. So the byte read means "the low byte of a pushed
#   dword" and needs the flip: +0x10 -> +0x13.
#
#   This one is not cosmetic. With the wrong byte the flag read ZERO, every
#   allocated slot stayed marked free, all seven bodies of the star system
#   were written into slot 114 on top of each other, the starport never
#   existed, FUNC_000994 copied a blank station's zero matrix into the ship,
#   every vertex projected onto the view centre and the 3D viewport rendered
#   nothing. Measured 2026-08-02: `ALLOC n=7` with base and index identical
#   (101ea94e/114) on all seven calls.
PROVEN_BY_CALLERS.setdefault("FUNC_000927", set()).add(("ebp", 0x10))

#   FUNC_001472 (vertex-index resolver), slot ebp+0xc. Reads it ONLY as a byte
#   (`mov al,[ebp+0xc]`, ffe.asm:515df) and forwards it to FUNC_001478, so no
#   in-function dword access proves the slot. Every call site pushes a whole
#   dword whose low byte carries the value -- `mov eax,esi / and al,0x1 /
#   xor al,[ebx+0x3] / push eax` at ffe.asm:87318-87321 and the same shape at
#   87344-87347, 87429-87432, 87455-87458, 87487-87490, 87519-87522 -- and
#   FUNC_001874 reaches it the same way (`mov al,[ebx+0x3] / push eax`,
#   6d4ae-6d4b2). A pushed dword read at its low byte: a scalar.
#
#   Why it matters: with the wrong byte the resolver gets the STALE upper bits
#   of eax instead of the vertex index, the model interpreter FUNC_001772
#   never reaches its opcode-0 terminator (FUNC_001754, the only writer that
#   clears DATA_007824) and spins forever. Traced 2026-08-02: the entry log
#   ends at FUNC_001874 (opcode 0x1f) -> FUNC_001470 -> FUNC_001472, and a
#   25-minute run produced no frame at all.
PROVEN_BY_CALLERS.setdefault("FUNC_001472", set()).add(("ebp", 0xc))

#   FUNC_001478 (the vertex resolver FUNC_001472 forwards to), slot ebp+0xc.
#   Read three times as `movsx eax,byte [ebp+0xc]` and once as
#   `cmp byte [ebp+0xc],0` (ffe.asm:51bf6-51c1a) and never at dword width.
#   Its only caller is FUNC_001472, which pushes a whole dword:
#   `mov al,[ebp+0xc] / push eax` (515de-515e1). Same fault, same path, one
#   link further down: the trace ends at 1472 -> 1478 and the model
#   interpreter never reaches its opcode-0 terminator.
PROVEN_BY_CALLERS.setdefault("FUNC_001478", set()).add(("ebp", 0xc))

#   FUNC_001358_StringConvertDate, slot ebp+0x10 -- the "print the year" FLAG.
#   Its own header names it: `char *F1358 (char *dest, int days, int yearflag)`
#   / "if yearflag = 0, don't output year." (ffe.asm:84262-84264). The function
#   touches the slot exactly once, as a byte -- `cmp byte [ebp+0x10],0x0`
#   (84302) -- so nothing inside it proves the width, and the single-width rule
#   was passing the displacement through unchanged.
#
#   All ELEVEN call sites push a whole dword: `push byte +0x1` at ffe.asm:8296,
#   20818, 20899, 84113, 84128, 84143, 84157 and `push byte +0x0` at 84068,
#   84083, 84098 -- NASM's byte form of PUSH is an imm8 sign-extended to four
#   bytes, not a one-byte push. So the byte read means "the low byte of a
#   pushed dword" and needs the flip: +0x10 -> +0x13.
#
#   Measured 2026-08-03, and this is what made it findable: the cockpit date
#   bar read `1-Jan 12:02:18` against the PC reference's `1-Jan-3250 12:00:00`.
#   With the wrong byte the flag reads the dword's HIGH byte, which is 0 for
#   both 0 and 1, so the year branch is never taken and EVERY date in the game
#   loses its year -- the HUD, the mission texts and the save descriptions
#   alike.
PROVEN_BY_CALLERS.setdefault("FUNC_001358", set()).add(("ebp", 0x10))

#   FUNC_001482 / FUNC_001483, slots ebp+0xa and ebp+0xe -- the HIGH halves of
#   the dwords pushed at +8 and +0xc. Every call site pushes whole dwords
#   (`push eax`, `push dword [ebp-0x14]`, e.g. ffe.asm around the FUNC_001489
#   results), so a word read at +0xa is "the high half of a pushed dword" and
#   lands at +8 on a big-endian machine, +0xe likewise at +0xc. Both functions
#   are ORACLE-CARRIED, so this entry is decided by execution rather than by
#   reading -- if the flip were wrong for them compare.py would say so.
for _f in ("FUNC_001482", "FUNC_001483"):
    PROVEN_BY_CALLERS.setdefault(_f, set()).add(("ebp", 8))
    PROVEN_BY_CALLERS.setdefault(_f, set()).add(("ebp", 0xc))

#   FUNC_000897 (the star system's body-placement recursion), slot ebp+0x14.
#   Touched EXACTLY ONCE in the whole function, and as a word:
#   `movzx eax,word [ebp+0x14]` (ffe.asm:66794), so nothing inside it proves
#   the width. All FOUR call sites push the full 32-bit return value of
#   FUNC_000894 -- the generator's PRNG -- as that argument: ffe.asm:63951,
#   64010, 64069 and 67006 each do `call FUNC_000894 / ... / push eax` with the
#   pushes for arg1..arg3 after it. A pushed dword read at its low half: a
#   scalar, so +0x14 -> +0x16.
#
#   MEASURED, both machines, same scenario and a bit-identical starting record
#   (2026-08-05). FUNC_000881 is entered 15 times on the reference and 8 times
#   for us; at the SECOND call the PRNG state is IDENTICAL on both
#   (s0=2a4c9872 s1=398f0676) while the arguments computed from that word are
#   not -- reference a3=0x4619 a6=0x9a4, ours a3=0x10147 a6=0x1391. Identical
#   seeds with different derived values is exactly the signature of a data read
#   going wrong rather than a branch: `add ebx,eax` at 66795 puts the word
#   straight into the orbital radius, and `edi` (hence the recursive call's
#   arg6) is derived from it four instructions later. That one word is the
#   whole 27-bodies-against-8 difference.
PROVEN_BY_CALLERS.setdefault("FUNC_000897", set()).add(("ebp", 0x14))

#   THE REST OF THE SAME RECURSION, found by the same scan and each read at its
#   callers. The system generator passes orbital radii, angles and counters
#   between FUNC_000877 / 000881 / 000882 / 000892 / 000897 as whole dwords and
#   reads them back at 16 bits; none of them is a packed aggregate, and the
#   counter-example the blanket rule tripped over (a four-char system code read
#   BYTE by byte) cannot apply -- every read here is a word, of a number.
#
#   FUNC_000877 (9 args), slot ebp+0x1c = arg6. Read only as `mov ax,[ebp+0x1c]`
#   (ffe.asm:63608, 63665). Two call sites push a literal zero, which is
#   byte-order invariant; the third, ffe.asm:66879, pushes `mov eax,edi /
#   shr eax,1 / add ax,si' -- a 16-bit ADD into the low half of a dword whose
#   top half is edi>>1, i.e. the value lives in the low word by construction.
#   Slot ebp+0x20 = arg7, read as `mov ax,[ebp+0x20]` (63629, 63774, 63801,
#   63828). Every site pushes a small dword: `movzx eax,word [DATA_008844] /
#   sar eax,3' at 63963-63964 and `[DATA_008844] & 0xffff >> 0xb' at
#   66872-66874 (a 0..31 quantity).
PROVEN_BY_CALLERS.setdefault("FUNC_000877", set()).add(("ebp", 0x1c))
PROVEN_BY_CALLERS.setdefault("FUNC_000877", set()).add(("ebp", 0x20))

#   FUNC_000881 (11 args), slot ebp+0x1c = arg6, read once as
#   `mov ax,[ebp+0x1c]` (ffe.asm:65113). Four sites forward FUNC_000877's own
#   arg5 whole (`mov eax,[ebp+0x18] / push eax', 63617/63639/63784/63812), one
#   pushes zero (63924) and one pushes the same `edi>>1 + si` low-word dword
#   (66879). Same shape, same argument.
PROVEN_BY_CALLERS.setdefault("FUNC_000881", set()).add(("ebp", 0x1c))

#   FUNC_000882 (6 args), slot ebp+0x18 = arg5, read once as
#   `movzx eax,word [ebp+0x18]` (ffe.asm:65159). BOTH call sites push it as
#   `mov ax,[ebp+0x1c] / push eax` (63670-63671 and 65117-65118) -- the caller
#   itself loads only AX and pushes the whole EAX with a stale top half, which
#   is the clearest possible statement that the value is the LOW word of a
#   pushed dword.
PROVEN_BY_CALLERS.setdefault("FUNC_000882", set()).add(("ebp", 0x18))

#   FUNC_000892 (5 args), slot ebp+0x18 = arg5, read once as
#   `mov dx,[ebp+0x18]` (ffe.asm:66520). All nine call sites push a whole
#   register (eax at 65474/65492/66050, edx at 65550/65670/65760/65793/65890/
#   65976); at 65535-65550 it is `mov edx,[ebp-0x4] / inc edx / push edx`, a
#   plain dword counter.
PROVEN_BY_CALLERS.setdefault("FUNC_000892", set()).add(("ebp", 0x18))

#   FUNC_001750 (7 args), slot ebp+0x14 = arg4, IN THE MODEL INTERPRETER'S
#   RECURSION. Two sub-word reads of the same pushed dword, back to back:
#   `mov ax,[ebp+0x14] / mov [ebp-0x80],ax` then `mov al,[ebp+0x16] /
#   mov [ebp-0x7e],al` (ffe.asm:109159-109162), unpacking a {word, byte} pair
#   into the child context that is then handed to FUNC_001740 and FUNC_001773.
#   Its ONLY caller is FUNC_001749, which pushes the slot as a WHOLE DWORD --
#   `mov eax,[esi+0xf4] / push eax` (ffe.asm:108959-108960), the fourth of seven
#   pushes, confirmed by `add esp,byte +0x1c` at 108984. So the word at +0x14 is
#   the LOW half of a pushed dword and the byte at +0x16 is its byte index 2;
#   on the 68k those live at +0x16 and +0x15, and frame.py's enclosing-dword
#   search reaches the +0x16 access from this one entry. Left alone, the child
#   context is built from the wrong halves of the object's [+0xf4] field on
#   every subobject in every model. Found by tools\translator\scan_arg_slots.py,
#   which reports it as FUNC_001750 0x14 (width 2) and 0x16 (width 1).
PROVEN_BY_CALLERS.setdefault("FUNC_001750", set()).add(("ebp", 0x14))

#   FUNC_001712 (7 args), slot ebp+0x10 = arg3, ON THE PATH THE MACHINE DIES ON.
#   Read THREE times and never as a dword: `movsx eax,byte [ebp+0x10]`
#   (ffe.asm:105004 and 105011) and `mov al,[ebp+0x10]` (105017). Its only
#   caller is FUNC_001711, the opcode-4 handler, which builds the slot as
#   `xor eax,eax / mov al,[ebx+0x4] / push eax` (ffe.asm:104880-104882) -- a
#   zero-extended BYTE pushed as a whole dword, which is the clearest possible
#   statement that the byte is that dword's low end. On the 68k it therefore
#   lives at +0x13, and reading +0x10 returns the top byte of a zero-extended
#   value, i.e. ZERO, on every call.
#   WHY IT MATTERS BEYOND WRONG COLOURS: the opcode trace (523 dispatches, twice
#   over) shows the machine dying immediately after a dispatch of opcode 4 ->
#   FUNC_001711 -> FUNC_001712, and the run before it recorded the interpreter's
#   stream pointer reaching 0x000008FC -- low memory, the 68k exception vectors,
#   which is why the reset leaves no crash log at all.
#   Found by tools\translator\scan_arg_slots.py as FUNC_001712 0x10 (width 1,
#   3 accesses, first at ffe.asm:105004).
PROVEN_BY_CALLERS.setdefault("FUNC_001712", set()).add(("ebp", 0x10))
# 2026-09-25 (from docs\escaping_aggregates_review-2026-09-25.md, re-checked):
# FUNC_001507 reads its packed-float argument as `movsx eax,word [ebp+0xc]'
# (mantissa) and `word [ebp+0xe]' (exponent), never at dword width -- so the
# single-width rule passed both through and the 68k took the exponent for the
# mantissa. Every caller pushes a NUMERIC dword: `mov eax,[ebx+0xb0] / push
# eax' with +0xb0 written `mov [ebx+0xb0],eax' (ffe.asm:4144-4152, 60639-
# 60646, 64831-64834 via [DATA_007741]), or its own dword arg (46879).
PROVEN_BY_CALLERS.setdefault("FUNC_001507", set()).add(("ebp", 0xc))
# FUNC_000596: FUNC_000592 fills THREE dwords through one pointer (lea
# [ebp-0x14]; it writes [p], [p+4], [p+8] as values), and 596 reads the low
# word of each (`mov ax,[ebp-0x14]/[ebp-0x10]/[ebp-0xc]', 39899-39903). Only
# -0x14 carried a lea, so -0x10 and -0xc were passed through and read their
# HIGH halves. They are dword values exactly like -0x14.
PROVEN_BY_CALLERS.setdefault("FUNC_000596", set()).update({("ebp", -0x10), ("ebp", -0xc)})

# The OPPOSITE annotation: slots proven to be TRUE sub-word storage -- word or
# byte FIELDS of a local aggregate -- where the displacement must pass through
# UNCHANGED. The refusal machinery cannot tell them from sub-word reads of a
# dword scalar; a human reading the function can. Every entry cites evidence.
#
#   FUNC_001330_DPalBuild: `mov word [esp+0x28..0x32],...` builds six
#   consecutive words -- three (base,step) pairs -- then passes THE ADDRESS
#   (`lea eax,[esp+0x28] / push eax`) to FUNC_001329, which walks them as
#   words (ffe.asm:82976-82987). A word array by construction, not a scalar's
#   half; the offsets are field offsets and stay as they are.
#   FUNC_001330_DPalBuild, second group: `mov [esp+0x10..0x12],al` builds
#   three palette BYTES (r,g,b), compared bytewise against DATA_009144..46
#   and copied with the word-pair idiom `mov ax,[esp+0x10]` /
#   `mov [edx+DATA_009144],ax` -- a two-byte memcpy, byte-order-preserving on
#   both machines as long as EVERY side stays plain (ffe.asm:83339-83375;
#   DATA_009144..46 are forced 'plain' in tools\datamap\overrides.tsv for the
#   same reason).
#
#   FUNC_001560, the SPAN ADDER -- and this one was a live miscompile on the
#   3D path, not a precaution. It opens by copying a caller's vertex WHOLE:
#       mov eax,[ebp+0xc] / mov eax,[eax] / mov [ebp-0x4],eax   (92790-92792)
#       mov eax,[ebp+0x10] / mov eax,[eax] / mov [ebp-0x8],eax  (92793-92795)
#   so [ebp-4] and [ebp-8] are dword slots by the in-function proof, and the
#   nine word accesses at [ebp-0x2] / [ebp-0x6] were being flipped to
#   [ebp-0x4] / [ebp-0x8]. They must NOT be: the local is a byte-for-byte COPY
#   of a `struct { short x, y; }`, which has the SAME field order on both
#   machines -- x at +0, y at +2 -- because the load and the store are both
#   longwords. The proof that it is a struct and not a scalar is in the same
#   function: it passes THE ADDRESS of each local (`lea eax,[ebp-0x4]` /
#   `lea eax,[ebp-0x8]`, 92853-92856) to FUNC_001559, which reads them as
#   `movsx ebx,word [edx+0x2]` / `movsx edi,word [eax]` (92731-92740) -- x at
#   +0, y at +2. A scalar's halves are never addressed that way.
#   What the flip cost: every `word [ebp-0x2]` is the vertex's Y, and flipped
#   it returned the X. So the row bounds written to [ctx+0x1b2c]/[ctx+0x1b30]
#   (92841-92848) were X values, 0..319, where the row array holds 158 rows of
#   44 bytes. FUNC_001613_DrawSpans then walks rows up to y=319 -- 44*319 =
#   14036, far past the 6952-byte record -- and bubble-sorts in place there.
#   That is the unbounded write that hard-reset the emulated machine with no
#   CPU exception, right after the renderer first reached DrawSpans.
#   This is exactly the case this module's header says nothing here can decide
#   in general (a packed pair of shorts and a scalar's halves want opposite
#   code); it is decided here by reading FUNC_001559, one function, one slot
#   at a time.
PROVEN_SUBWORD = {
    "FUNC_001330": {("esp", 0x28), ("esp", 0x2a), ("esp", 0x2c),
                    ("esp", 0x2e), ("esp", 0x30), ("esp", 0x32),
                    ("esp", 0x10), ("esp", 0x11), ("esp", 0x12)},
#   The same shape exists in exactly TWO other functions -- a scan for "a local
#   dword loaded whole from a dereference, later read sub-word two bytes above"
#   returns 1560, 1567 and 1704 and nothing else -- and both are the same case:
#     FUNC_001567 is 1560's twin (93224-93232 copy two vertices whole, compare
#     [ebp-0x2] against [ebp-0x6], then hand `lea eax,[ebp-0x4]` /
#     `lea eax,[ebp-0x8]` to FUNC_001563 and FUNC_001559, which read x at +0
#     and y at +2).
#     FUNC_001704 proves it with CONSTANTS, which is as good as evidence gets
#     (104042-104053): `word [ebp-0x4]` and `word [ebp-0x8]` are compared
#     against 0x140 = 320, the screen WIDTH, and `word [ebp-0x2]` / `word
#     [ebp-0x6]` against 0x9e = 158, the viewport HEIGHT. +0 is x, +2 is y.
#   All four displacements of each pair are listed, not just the +2 ones: a
#   word read AT the dword displacement (`movsx eax,word [ebp-0x4]`, the x)
#   would otherwise be moved to [ebp-0x2] by the enclosing-slot rule and read
#   the y instead.
#   FUNC_000580 (collision: one model vertex against an object, ffe.asm:37753)
#   copies a 6-byte vertex descriptor WHOLE into a local (mov ecx,[edx+eax*2] /
#   mov [ebp-0x8],ecx / mov cx,[edx+eax*2+0x4] / mov [ebp-0x4],cx) and then picks
#   its bytes: x = [ebp-0x6], y = [ebp-0x5], z = [ebp-0x4]. Raw model bytes in
#   memory order -- a pass-through, no flip. Flipped, x read byte 1 and y byte 0
#   of the descriptor; harmless while the vertex INDEX argument was itself read
#   from the wrong end (always vertex 0), fatal the moment that was fixed: the
#   ship "collided" on the pad and exploded at take-off (2026-10-09).
    "FUNC_000580": {("ebp", -8), ("ebp", -6), ("ebp", -5), ("ebp", -4)},
#   FUNC_000583 (collision: one plane of a model region against a point,
#   ffe.asm:38817-38893) is the same idiom twice: a NORMAL record and a VERTEX
#   record of 6 raw bytes each (DATA_003592 / DATA_003591, storage le = x86
#   order) copied whole -- mov esi,[ebx+ecx*2-0x6] / mov [ebp-0x8],esi / word
#   into [ebp-0x4]; mov edi,[esi+ecx*2] / mov [ebp-0x10],edi / word into
#   [ebp-0xc] -- then picked apart bytewise ([ebp-0x8] index, [ebp-6..-4] the
#   normal, [ebp-0xe..-0xc] the vertex). Pass-through, no flip. Flipped, every
#   normal read (0,0,0) and the docking-bay region of the station model never
#   registered: the autopilot flew through the bay and crashed (2026-10-16).
    "FUNC_000583": {("ebp", -0x10), ("ebp", -0xe), ("ebp", -0xd), ("ebp", -0xc),
                    ("ebp", -8), ("ebp", -6), ("ebp", -5), ("ebp", -4)},
#   FUNC_000825 is FUNC_000583's twin (ffe.asm:55650+): the same two raw
#   6-byte records from the model header's +0x18 / +0x1c tables, copied whole
#   into [ebp-0x8]/[ebp-0x4] and [ebp-0x10]/[ebp-0xc] and picked bytewise.
    "FUNC_000825": {("ebp", -0x10), ("ebp", -0xe), ("ebp", -0xd), ("ebp", -0xc),
                    ("ebp", -8), ("ebp", -6), ("ebp", -5), ("ebp", -4)},
    "FUNC_001560": {("ebp", -2), ("ebp", -4), ("ebp", -6), ("ebp", -8)},
    "FUNC_001567": {("ebp", -2), ("ebp", -4), ("ebp", -6), ("ebp", -8)},
    "FUNC_001704": {("ebp", -2), ("ebp", -4), ("ebp", -6), ("ebp", -8)},
#   FUNC_001761 -- TWO 3-BYTE RGB RECORDS, and the reason every polygon in the
#   3D viewport was painted colour ZERO (measured: `COL calls=320 n=16`, all
#   sixteen sampled colours 0).
#   The frame holds two adjacent RGB triples and a 3-byte swap scratch:
#       [ebp-0x12][-0x11][-0x10]   record B
#       [ebp-0x0f][-0x0e][-0x0d]   record A
#       [ebp-0x15][-0x14][-0x13]   scratch used to exchange A and B
#   The ADDRESS OF RECORD A ESCAPES: `lea eax,[ebp-0xf]` (109916) is handed to
#   FUNC_001691, which fills three bytes through the pointer in ASCENDING
#   address order. No displacement flip can survive that -- the callee decides
#   the layout, and it writes R,G,B low-to-high. So both records and the
#   scratch are byte arrays and pass through unchanged.
#   Record B is built the same way: `mov dx,[esi+eax+0x84]` / `mov [ebp-0x12],dx`
#   plus `mov dl,[esi+eax+0x86]` / `mov [ebp-0x10],dl` copy a 3-byte record out
#   of a 3-byte-stride table (index is `lea eax,[eax+eax*2]`), and 109928-109933
#   add A into B component by component. Every one of those moves preserves
#   address order on both machines, so pass-through is self-consistent; so is
#   the A<->B exchange at 109964-109975, which only copies bytes about.
#   What frame.py did BEFORE this entry, and why it produced zero: the two
#   dword reads (`mov eax,[ebp-0xf]` at 110040, `mov eax,[ebp-0x12]` at 110086)
#   put both displacements in the proven-dword set, so the enclosing-slot rule
#   scattered the bytes -- R landed at (-12,a5), G at (-13,a5), B at (-14,a5)
#   -- while the dword read still came from (-15,a5), which nothing had
#   written. The colour handed to DPal32to16 was whatever was in the frame.
#   THE DWORD READS STILL NEED A SWAP, and it is registered separately in
#   le_read_sites.tsv (110040, 110086, width 4). Both feed
#   FUNC_001326_DPal32to16, which re-reads the pushed dword BYTE-WISE at
#   [ebp+8]/[ebp+9]/[ebp+0xa] -- and the translator flips those three (they are
#   an incoming pushed dword, correctly: `move.b (11,a5),d0` / `(10,a5)` /
#   `(9,a5)`). So the courier dword must carry R in bits 7..0 exactly as on
#   x86, which a plain big-endian MOVE.L of a byte array does not: it puts R in
#   bits 31..24. The 4-byte reverse restores it.
    "FUNC_001761": {("ebp", -0x15), ("ebp", -0x13),
                    ("ebp", -0x12), ("ebp", -0x11), ("ebp", -0x10),
                    ("ebp", -0x0f), ("ebp", -0x0e), ("ebp", -0x0d)},
#   FUNC_001695 -- the same escaping-byte-aggregate shape as FUNC_001761, but
#   in INCOMING ARGUMENT slots, which is why the argument rule flipped it.
#   This one PROVES ITSELF, and that is the whole point of listing it:
#       lea esi,[ebp+0x10]   (102761)
#       mov ecx,0x4
#       rep movsd            (102764)
#   the function copies [ebp+0x10..0x1f] out as a RAW 16-BYTE BUFFER, in
#   ascending address order. Any byte-level access into that buffer must use
#   address-order displacements or it touches a different byte than the copy
#   does. So `add [ebp+0x10],cl` / `[ebp+0x11]` / `[ebp+0x12]` (102755-102759,
#   three colour components added from the object at +0xf4/+0xf5/+0xf6) cannot
#   be flipped, whatever the caller thinks the dword means. Before this entry
#   they became add.b d1,(19,a5) / (18,a5) / (17,a5) -- scattered backwards
#   across the very buffer the rep movsd forwards.
#   The callers agree, checked at both sites (ffe.asm:105063-105070 and
#   104442-104448): each pushes FOUR dwords out of a local block and passes
#   the ADDRESS of that same block as arg1, so the callee's +0x10.. is a
#   by-value copy of the caller's [ebp-0x3c].. and the rep movsd writes it
#   back. Pass-through on both sides is self-consistent end to end.
#   Found by tracing the F3 crash into the model interpreter: this is on the
#   'non-reflected quad' opcode path (FUNC_001772 -> 001711 -> 001712 ->
#   001692 -> 001695), and FUNC_001695 was the last function entered before
#   the machine reset.
    "FUNC_001695": {("ebp", 0x10), ("ebp", 0x11), ("ebp", 0x12)},
#   FUNC_001712 -- THE SECOND COLOUR PRODUCER, the same courier shape as
#   FUNC_001761 and on the 'non-reflected quad' opcode path. Two 3-byte RGB
#   aggregates:
#       [ebp-0x17][-0x16][-0x15]   the object's own tint
#       [ebp-0x06][-0x05][-0x04]   the accumulated colour
#   Built exactly the way 001761 builds its records -- a WORD store followed by
#   a BYTE store fills three consecutive bytes (105266-105268, 105299-105301),
#   then the components are added one byte at a time (105273-105279,
#   105302-105307).
#   TWO independent proofs that these are byte arrays and not scalars:
#     (1) each component is CLAMPED SEPARATELY to 0xf (105308-105318:
#         `cmp byte [ebp-0x6],0xf / mov byte [ebp-0x6],0xf`, and the same for
#         -0x5 and -0x4). Four-bit-per-channel RGB; a scalar's bytes are never
#         clamped one by one.
#     (2) the dword at 105320 goes straight to FUNC_001326_DPal32to16
#         (105321-105322), which re-reads the pushed dword BYTE-WISE at
#         [ebp+8]/[ebp+9]/[ebp+0xa]. Same courier, same consumer, same fix as
#         FUNC_001761 -- and 105320 is registered in le_read_sites.tsv for the
#         4-byte reverse the courier needs.
#   This is why fixing FUNC_001761 alone only half-fixed the colours: 001761
#   feeds the triangle/general path and 001712 feeds the quad path.
#   THE THIRD aggregate in this frame, and the one that makes the FUNC_001695
#   fix consistent: [ebp-0x3c][-0x3b][-0x3a] is where the tint COMES FROM
#   (105265-105268 copy it into [ebp-0x17] as a word plus a byte). Its ADDRESS
#   ESCAPES TWICE -- `lea eax,[ebp-0x3c]` at 104934 (to FUNC_001694) and at
#   105069 (to FUNC_001695) -- so pass-through is forced exactly as it is for
#   FUNC_001761's record A.
#   The four `push dword [ebp-0x3c]`/[-0x38]/[-0x34]/[-0x30] at 105063-105066
#   and 105339-105342 are the by-value copy FUNC_001695 receives at its
#   +0x10.., and a plain MOVE.L push preserves address order, so they need no
#   swap -- which is what makes the caller and the (now pass-through) callee
#   agree end to end. Missing this displacement would have left the producer
#   flipped while the consumer was not.
#   FUNC_001692 -- the CLEANEST demonstration of the whole class, and the one
#   that names the underlying cause. It unpacks a 4-bit RGB triple (the exact
#   inverse of FUNC_001326_DPal32to16): R = (v>>8)&0xf, G = (v>>4)&0xf,
#   B = v&0xf, written to [ebp-0x10]/[-0x0f]/[-0x0e], then
#   `lea esi,[ebp-0x10] / mov ecx,4 / rep movsd` copies the 16-byte block to
#   the caller.
#   ALL SEVEN accesses to those three displacements are BYTE writes
#   (102633-102635 zero them, 102644/102648/102651 fill them) and there is NO
#   dword access to the slot anywhere in the function -- yet the bytes were
#   emitted to (-13,a5)/(-14,a5)/(-15,a5) while the copy starts at
#   `lea (-16,a5),a2`. Byte 0 of the copied block was NEVER WRITTEN and the
#   three channels came out reversed.
#   WHY, and it is worth knowing: _slots() counts `lea esi,[ebp-0x10]` as a
#   4-byte ACCESS, because _width() falls back to op_size(), which reads the
#   width off the DESTINATION register. That marks the slot a proven dword and
#   the enclosing-slot rule flips every byte in it. The effect is exactly
#   backwards -- a `lea` on a stack slot is the strongest evidence the slot is
#   a byte array, since the address escapes and the receiver walks the bytes
#   in ascending order. See the measured-and-reverted note in _slots().
#   FUNC_001691 -- THE COLOUR FILL ROUTINE, and the reason the 3D viewport was
#   MONOCHROME while the 2D console was in full colour. Its second argument is
#   an RGB triple passed BY VALUE inside one pushed dword, and it is read and
#   written a byte at a time:
#       add [ebp+0xc],dl / add [ebp+0xd],dl / add [ebp+0xe],dl   (102589-102601)
#       mov dx,[ebp+0xc] / mov [eax],dx                          (102602-102603)
#       mov dl,[ebp+0xe] / mov [eax+0x2],dl                      (102604-102605)
#   The incoming-argument flip claimed all three -- they became (15,a5),
#   (14,a5), (13,a5) and the word read became `move.w (14,a5)`, i.e. (G,R)
#   instead of (R,G) -- so the three channels came out of the wrong argument
#   bytes entirely. Measured at DPal32to16: R=0x60, G=0x3a against the 0..15 a
#   4-bit channel allows, which indexes far into the 64K DATA_007770 table and
#   returns the extremes. That is exactly a black-and-white 3D view.
#   PASS-THROUGH IS SELF-CONSISTENT END TO END: the caller pushes the triple
#   as `push dword [eax]` straight out of a byte buffer (FUNC_001712:105350-
#   105353), and a dword pushed from memory keeps its byte ORDER across the
#   push on both machines; the callee then writes the bytes back out in
#   ascending address order through `mov [eax],dx` + `mov [eax+2],dl`, into
#   buffers ([ebp-0x3], [ebp-0xf]) that every consumer reads as byte arrays.
#   This is the same escaping-byte-aggregate family as FUNC_001761/001692/
#   001695/001712, and the sixth member of it found on 2026-08-02.
    "FUNC_001691": {("ebp", 0x0c), ("ebp", 0x0d), ("ebp", 0x0e)},
    "FUNC_001692": {("ebp", -0x10), ("ebp", -0x0f), ("ebp", -0x0e)},
    "FUNC_001712": {("ebp", -0x3c), ("ebp", -0x3b), ("ebp", -0x3a),
                    ("ebp", -0x17), ("ebp", -0x16), ("ebp", -0x15),
                    ("ebp", -0x06), ("ebp", -0x05), ("ebp", -0x04)},
#   2026-09-25 -- THE REST OF THE COURIER FAMILY, found by auditing EVERY call
#   of FUNC_001326_DPal32to16 (36 sites) against its emitted 68k: after the
#   2026-08-02 fixes to FUNC_001761/001712 only those two had the 4-byte
#   reverse; the courier locals below were still either flipped as scalars
#   while a callee filled them in address order, or flipped onto the SAVED
#   FRAME POINTER. Symptom measured in the intro: every object drawn from the
#   static grey ramp (the FRONTIER / DAVID BRABEN logo, the ships, the
#   asteroids) came out index 0 -- black -- while the planet texture was right.
#   Each entry lists only the sub-word displacements the function touches; the
#   courier's dword reads are registered in le_read_sites.tsv (same date).
#
#   FUNC_000115 / FUNC_000125 -- the picture loaders' palette conversion.
#   `mov [ebp-0x3],al / [ebp-0x2],al / [ebp-0x1],al` then `mov eax,[ebp-0x3]`
#   (12596-12605, 14197-14206). Flipped as a scalar dword the first byte went
#   to (0,a5) -- the high byte of the CALLER'S SAVED a5 -- because the x86
#   dword at [ebp-3] spans [ebp+0]. Pass-through keeps all three inside the
#   frame; the swapped dword read then carries R in bits 7..0.
    "FUNC_000115": {("ebp", -0x3), ("ebp", -0x2), ("ebp", -0x1)},
    "FUNC_000125": {("ebp", -0x3), ("ebp", -0x2), ("ebp", -0x1)},
#   FUNC_001700 -- `lea eax,[ebp-0x7]` escapes to FUNC_001691 (103486-103488),
#   which writes R,G,B in ascending address order, and the function then adds
#   and rescales the channels one byte at a time (103500-103549, 103813-
#   103846) -- the FUNC_001761 record-A shape exactly. ALSO: [ebp-0x4] and
#   [ebp-0x2] are a PAIR OF SHORTS (x, y) whose address escapes three times
#   (`lea eax,[ebp-0x4]` 103729/103749/103764) to FUNC_001701, which reads
#   `movsx eax,word [esi]` / `word [esi+0x2]` -- x at +0, y at +2 in address
#   order. The flip put x at (-2,a5) while the callee read it at (-4,a5): the
#   same packed-point case as FUNC_001560/001567/001704 above.
    "FUNC_001700": {("ebp", -0x7), ("ebp", -0x6), ("ebp", -0x5),
                    ("ebp", -0x4), ("ebp", -0x2)},
#   FUNC_001708 -- `mov ax,[ebx] / mov [ebp-0x6],ax / mov al,[ebx+0x2] /
#   mov [ebp-0x4],al` copies three RGB bytes out of a DATA_007811 shade record
#   (raw bytes, address order), then each channel is added and CLAMPED to 0xf
#   separately (104632-104648) -- the FUNC_001712 proof (1) exactly.
#   SECOND AGGREGATE, same function (added the same day after RGBMAP showed
#   FUNC_001708's colours as f77/f88/fbb against the reference's 777/888/bbb,
#   i.e. red always clamped): the tint [ebp-0x23..-0x21] is copied as
#   `mov dx,[ebp-0x48] / mov [ebp-0x23],dx / mov dl,[ebp-0x46] /
#   mov [ebp-0x21],dl' (104597-104600) and then read and added BYTE-wise
#   (104605-104611, 104632-104636). The emitted code stored the word at
#   (-35,a5) but read the bytes at (-34)/(-35)/(-36) -- the word and byte
#   views of the same three bytes disagreed. [ebp-0x48] is the first dword of
#   a 16-byte block filled by `rep movsd' (104339-104340) and handed to
#   FUNC_001695 through `lea eax,[ebp-0x48]' (104447): a byte aggregate in
#   address order, exactly FUNC_001712's [ebp-0x3c].
    "FUNC_001708": {("ebp", -0x6), ("ebp", -0x5), ("ebp", -0x4),
                    ("ebp", -0x48), ("ebp", -0x46),
                    ("ebp", -0x23), ("ebp", -0x22), ("ebp", -0x21)},
#   FUNC_001738 -- `lea eax,[ebp-0x3]` escapes (107208) and the three channels
#   are rebuilt byte by byte twice (107433-107449, 107555-107571).
#   ...and its SOURCE triple [ebp-0x48..-0x46] (added the same day: RGBMAP
#   showed red as junk -- (c8,c,c), (64,6,6) -- against the reference's
#   044/066/088): `lea eax,[ebp-0x48]' (107200) hands the block to
#   FUNC_001692, which writes the unpacked R,G,B bytes in address order, and
#   `push dword [ebp-0x48]' (107207) forwards it by value; the rebuild loops
#   then read it a byte at a time (107425-107444, 107547-107566). Flipped, the
#   three reads came from (-69)/(-70)/(-71) -- the fourth, never-written
#   byte of the dword among them.
    "FUNC_001738": {("ebp", -0x3), ("ebp", -0x2), ("ebp", -0x1),
                    ("ebp", -0x48), ("ebp", -0x47), ("ebp", -0x46)},
#   FUNC_001766 -- `lea eax,[ebp-0x6]` escapes to the fill (110706), then a
#   3-byte copy `mov ax,[ebp-0x6] / mov [ebp-0x3],ax / mov al,[ebp-0x4] /
#   mov [ebp-0x1],al` (110710-110713) -- word+byte, address order both sides.
    "FUNC_001766": {("ebp", -0x6), ("ebp", -0x4), ("ebp", -0x3),
                    ("ebp", -0x1)},
#   FUNC_001693 / FUNC_001694 -- FUNC_001695's two SIBLINGS, the identical
#   shape it was fixed for on 2026-08-02 and missed then: channel adds into
#   the incoming-argument bytes [ebp+0x10]/[+0x11]/[+0x12] (from the object at
#   +0xf4..0xf6, and in 1693 also from the by-value triple at [ebp+0x20..0x22])
#   followed by `lea esi,[ebp+0x10] / mov ecx,4 / rep movsd` copying the
#   16-byte block out in ADDRESS order. Emitted before this entry: add.b to
#   (19,a5)/(18,a5)/(17,a5) while the copy starts at (16,a5) -- so byte 0 of
#   the copied colour never got its red, and the tint went into the unused
#   fourth byte. MEASURED: PALMAP showed FUNC_001809's codes as 077/033/066
#   (red channel zero, i.e. teal) where the reference's are 777/333/666; its
#   producer is FUNC_001712 -> FUNC_001693 (105347) -> FUNC_001691. Every
#   caller (14 sites) passes the block as four `push dword [eax+k]' out of a
#   FUNC_001692 byte buffer, and 1693's [ebp+0x20] is `mov eax,[eax] / push
#   eax' of a raw 3-byte record -- address order end to end.
    "FUNC_001693": {("ebp", 0x10), ("ebp", 0x11), ("ebp", 0x12),
                    ("ebp", 0x20), ("ebp", 0x21), ("ebp", 0x22)},
    "FUNC_001694": {("ebp", 0x10), ("ebp", 0x11), ("ebp", 0x12)},
#   FUNC_001878 -- the surface-shade lookup behind the planet halo. It copies
#   a 3-byte RGB triple out of the object record (`mov cx,[edx+eax+0x84] /
#   mov [ebp-0x4],cx / mov cl,[edx+eax+0x86] / mov [ebp-0x2],cl',
#   132264-132267) and returns it as ONE dword (`mov eax,[ebp-0x4]', 132273,
#   registered in le_read_sites.tsv). The word came from a POINTER read of raw
#   bytes, so it is r<<8|g on the 68k, while the flip treated the slot as a
#   scalar and stored it in the low half: the dword came back as
#   junk|b|r|g -- measured through FUNC_001879 as halo colours 7ed/076/779
#   where the reference has d97/dde/aab. Pass-through keeps r,g,b in address
#   order; the swapped read then yields the x86 value r | g<<8 | b<<16.
    "FUNC_001878": {("ebp", -0x4), ("ebp", -0x2)},
#   FUNC_001750 -- the sub-model setup behind opcode 14 (FUNC_001749). Its 4th
#   argument is the parent context's 3-byte colour [esi+0xf4] pushed as ONE
#   dword (mov eax,[esi+0xf4] / push eax, 108959-108960) and copied into the
#   child context byte for byte: mov ax,[ebp+0x14] / mov [ebp-0x80],ax /
#   mov al,[ebp+0x16] / mov [ebp-0x7e],al (109159-109162) -- the same raw RGB
#   triple as FUNC_001693's [ebp+0x20]. Flipped, the child got b,x,g
#   (2026-10-16).
    "FUNC_001750": {("ebp", 0x14), ("ebp", 0x16)},
#   FUNC_001680 -- builds the 16-byte colour block FUNC_001700 receives by
#   value: `mov ax,[edi+0x24] / mov [ebp-0x19c],ax / mov al,[edi+0x26] /
#   mov [ebp-0x19a],al' copies a raw RGB triple, bit 7 of R is tested and the
#   three channels are masked to 4 bits one byte at a time (102074-102088),
#   then `push dword [ebp-0x19c]' (102098) passes the first dword on. Flipped,
#   the bytes went to (-409..-411) while the push read (-412..-409): RGBMAP
#   saw FUNC_001700's colours as (0,84,38)/(0,6c,a) against the reference's
#   091/00b, and 1700 took the FUNC_001701/1704 paths the reference never runs.
    "FUNC_001680": {("ebp", -0x19c), ("ebp", -0x19b), ("ebp", -0x19a)},
#   THE DEFERRED-DRAW HANDLERS' SCREEN POINTS (2026-09-25) -- the packed
#   {short x, short y} case of FUNC_001560/001567/001704 above, five more
#   times. Each handler copies its item's coordinates into `mov [ebp-0x4],ax
#   / mov [ebp-0x2],ax' (and -8/-6, -0xc/-0xa, -0x10/-0xe for the 2nd..4th
#   point) and passes `lea eax,[ebp-0x4]' to a routine that reads x at +0 and
#   y at +2 in address order: FUNC_001610_DrawParticleClipped (`mov cx,[eax]'
#   ...), FUNC_001611_DrawParticle, FUNC_001616_DrawLine (oracle-carried with
#   C {short x,y} structs) and FUNC_001546 (the quad). Flipped, x went to -2
#   and y to -4, so every callee read the point TRANSPOSED. Found by
#   tools\translator\scan_escaping_aggregates.py together with the item
#   field reads registered in lo_byte_sites.tsv the same day.
    "FUNC_001800": {("ebp", -0x4), ("ebp", -0x2)},
    "FUNC_001801": {("ebp", -0x4), ("ebp", -0x2)},
    "FUNC_001803": {("ebp", -0x4), ("ebp", -0x2), ("ebp", -0x8), ("ebp", -0x6)},
    "FUNC_001811": {("ebp", -0x4), ("ebp", -0x2), ("ebp", -0x8), ("ebp", -0x6),
                    ("ebp", -0xc), ("ebp", -0xa), ("ebp", -0x10), ("ebp", -0xe)},
    "FUNC_001814": {("ebp", -0x4), ("ebp", -0x2)},
#   Same scan, same day, the rest of the render path:
#   FUNC_001546 / FUNC_001547 -- line-strip walkers: points (x,y) at
#   [ebp-0x4]/[ebp-0x2] and [ebp-0x8]/[ebp-0x6] from `sar eax,0x10 / mov
#   [..],ax', compared against 0x140/0x9e and passed by `lea' to FUNC_001561,
#   FUNC_001616_DrawLine and (1547) an `call edi' line callback; 1547 also
#   copies the whole point `mov eax,[ebp-0x8] / mov [ebp-0x4],eax'.
    "FUNC_001546": {("ebp", -0x4), ("ebp", -0x2), ("ebp", -0x8), ("ebp", -0x6)},
    "FUNC_001547": {("ebp", -0x8), ("ebp", -0x6)},
#   FUNC_001699 -- the point (x = ..+0xa0, y = 0x4f-..) at [ebp-0x18]/
#   [ebp-0x16], handed to FUNC_001700 as `lea eax,[ebp-0x18]' (103370),
#   which reads `movsx eax,word [esi]' / `[esi+0x2]'.
    "FUNC_001699": {("ebp", -0x18), ("ebp", -0x16)},
#   FUNC_001703 -- `mov ax,[DATA_007826] / mov [ebp-0x3],ax / mov al,
#   [DATA_007827] / mov [ebp-0x1],al / mov eax,[ebp-0x3]' (103940-103944), a
#   by-value code for FUNC_001692. Flipped as a scalar, the word landed on
#   (-1..0,a5): (0,a5) is the high byte of the CALLER'S SAVED a5, rewritten on
#   every call. Pass-through here; the word store and the dword read are
#   byte-swapped at the site (le_read_sites.tsv 103941/103944) so the dword
#   is the x86 value, top byte included (the saved frame pointer's low byte on
#   x86, its high byte here -- nonzero on both, which is all 1692 tests).
    "FUNC_001703": {("ebp", -0x3), ("ebp", -0x1)},
#   FUNC_001747 -- the text opcode's colour: `lea eax,[ebp-0x7]' is filled by
#   FUNC_001691 (108580-108582) in address order, then copied word+byte into
#   the render context at [ebp-0x94]/[ebp-0x92] (108584-108587), which already
#   passed through. The two source reads were flipped to (-5)/(-6): the text
#   was drawn in (b,?,g) -- the MAGENTA "FRONTIER" of the intro logo.
    "FUNC_001747": {("ebp", -0x7), ("ebp", -0x5)},
#   FUNC_001762 -- object tint: the block at [ebp-0x14] (FUNC_001692 fill via
#   lea, 110391-110393, then FUNC_001693 in place) is copied `mov ax,[ebp-
#   0x14] / mov [edi+0xf4],ax / mov al,[ebp-0x12] / mov [edi+0xf6],al'
#   (110423-110426) into the object's +0xf4..0xf6 tint bytes, which
#   FUNC_001693/1694 read byte-wise.
    "FUNC_001762": {("ebp", -0x14), ("ebp", -0x12)},
#   THE PLANET SURFACE PATH (2026-09-25), next rows of the same scan:
#   FUNC_000581 copies a 6-byte table record RAW -- `mov edi,[esi+ecx*2] /
#   mov [ebp-0x10],edi / mov di,[esi+ecx*2+0x4] / mov [ebp-0xc],di' (37973-
#   37976, repeated four times) -- and then reads bytes 2,3,4 of it as SIGNED
#   components (`movsx eax,byte [ebp-0xe]/[ebp-0xd]/[ebp-0xc]'). Whole-dword
#   and word copies keep address order, the flipped byte reads did not: every
#   component came from the wrong byte, which is the planet close-up breaking
#   into vertical "stalactites" where the reference draws a faceted sphere.
    "FUNC_000581": {("ebp", -0xe), ("ebp", -0xd), ("ebp", -0xc)},
#   FUNC_000576 -- `lea eax,[ebp-0x5]' to FUNC_000579 -> FUNC_000582, which
#   stores a BYTE through it (`mov [eax],bl'); the flipped read took (-2).
    "FUNC_000576": {("ebp", -0x5)},
#   FUNC_000561 -- three DrawLine calls on {x,y} points at [ebp-0x4]/-0x2 and
#   [ebp-0x8]/-0x6, the handler-point case above.
    "FUNC_000561": {("ebp", -0x4), ("ebp", -0x2), ("ebp", -0x8), ("ebp", -0x6)},
#   FUNC_000823 -- FUNC_000581's twin (the same 6-byte record copied raw into
#   [ebp-0x14] dword + [ebp-0x10] word, signed bytes read at -0x12/-0x11/
#   -0x10; 55007-55023 and three repeats).
    "FUNC_000823": {("ebp", -0x12), ("ebp", -0x11), ("ebp", -0x10)},
#   FUNC_000819 -- two single bytes, each with its OWN `lea' (54303/54305)
#   handed to a callee, and also set and read by name (54190-54196,
#   54339-54341): one address per byte, so no flip can be right.
    "FUNC_000819": {("ebp", -0x12), ("ebp", -0x11)},
#   THE REST OF scan_escaping_aggregates.py (2026-09-25), classified slot by
#   slot with ffe.asm evidence in docs\escaping_aggregates_review-2026-09-25.md
#   (59 functions: 24 AGGREGATE below, 35 VALUE where the flip is right).
#   Five groups: {x,y} points to DrawQuad/DrawLine/DrawCircle/scanner stalks
#   or written x-then-y by FUNC_001479; RGB triples read word+byte by
#   FUNC_001575; byte out-params FUNC_000570 writes one byte at a time; a word
#   in/out through a pointer (FUNC_000658 -> FUNC_000635); a _memcpy target.
#   EIGHT of them flipped a local onto the CALLER'S SAVED a5 at (0..3,a5):
#   000460/000506 (word write to (0,a5)), 000961 (byte write to (0,a5)),
#   000910 (write+read (2,a5)), 001024 (`dec byte' on (1,a5)), and reads in
#   000110/000714/000718 -- memory corruption of the caller's frame pointer on
#   every call, the prime suspect for the intermittent intro hang.
    "FUNC_000076": {("ebp", -0x6f), ("ebp", -0x6e), ("ebp", -0x6d)},
    "FUNC_000110": {("ebp", -0x2)},
    "FUNC_000120": {("ebp", -0x4), ("ebp", -0x2), ("ebp", -0x208), ("ebp", -0x207),
                    ("ebp", -0x30c), ("ebp", -0x30b)},
    "FUNC_000212": {("ebp", -0x4), ("ebp", -0x2), ("ebp", -0xc), ("ebp", -0xa),
                    ("ebp", -0x8), ("ebp", -0x6), ("ebp", -0x10), ("ebp", -0xe)},
    "FUNC_000213": {("ebp", -0x4), ("ebp", -0x2), ("ebp", -0xc), ("ebp", -0xa),
                    ("ebp", -0x8), ("ebp", -0x6), ("ebp", -0x10), ("ebp", -0xe)},
    "FUNC_000275": {("ebp", -0x8), ("ebp", -0x6)},
    "FUNC_000281": {("ebp", -0x4f), ("ebp", -0x4e), ("ebp", -0x4d)},
    "FUNC_000286": {("ebp", -0x4f), ("ebp", -0x4e), ("ebp", -0x4d)},
    "FUNC_000460": {("ebp", -0x2)},
    "FUNC_000506": {("ebp", -0x2)},
    "FUNC_000714": {("ebp", -0x2), ("ebp", -0x1)},
    "FUNC_000718": {("ebp", -0x2)},
    "FUNC_000727": {("ebp", -0x6)},
    "FUNC_000910": {("ebp", -0x1)},
    "FUNC_000961": {("ebp", -0x3), ("ebp", -0x2), ("ebp", -0x1)},
    "FUNC_000974": {("ebp", -0x37), ("ebp", -0x36), ("ebp", -0x35)},
    "FUNC_000998": {("ebp", -0x23), ("ebp", -0x21)},
    "FUNC_001024": {("ebp", -0x2)},
    "FUNC_001033": {("ebp", -0x5)},
    "FUNC_001066": {("ebp", -0x23), ("ebp", -0x22), ("ebp", -0x21)},
    "FUNC_001094": {("ebp", -0x27), ("ebp", -0x25)},
    "FUNC_001099": {("ebp", -0x34), ("ebp", -0x32)},
    "FUNC_001111": {("ebp", -0x4), ("ebp", -0x2)},
    "FUNC_001114": {("ebp", -0x4), ("ebp", -0x2), ("ebp", -0xc), ("ebp", -0xa),
                    ("ebp", -0x8), ("ebp", -0x6), ("ebp", -0x10), ("ebp", -0xe)},
}


def apply(fn):
    """Rewrite every PROVEN sub-word stack access to its big-endian
    displacement, in place, and return the list of rewrites for the header.

    Each entry is (ffe.asm line, original text, base, old disp, new disp, width).
    Operands left unproven are untouched, and lower.ea() then refuses them.
    """
    slots = _slots(fn)
    dword = {key for key, widths in slots.items() if 4 in widths}
    dword |= PROVEN_BY_CALLERS.get(fn.base, set())
    subword = PROVEN_SUBWORD.get(fn.base, set())
    notes = []

    # Is ebp a FRAME POINTER in this function at all? FUNC_001330_DPalBuild
    # saves ebp as a plain register (push ebx/esi/edi/ebp, no `mov ebp,esp`)
    # and later walks a word array through it -- [ebp+0] there is a POINTER
    # access, not a stack slot, and its displacement must pass through
    # unchanged (pointer-reached data follows the 'plain' convention). Only a
    # function that actually executes `mov ebp, esp` subjects its ebp
    # accesses to the endianness flip machinery.
    ebp_is_frame = any(
        isinstance(it, Insn) and it.mnem == "mov" and len(it.ops) == 2
        and getattr(it.ops[0], "name", None) == "ebp"
        and getattr(it.ops[1], "name", None) == "esp"
        for it in fn.items)

    for it in fn.items:
        if not isinstance(it, Insn):
            continue
        for i, o in enumerate(it.ops):
            if not (isinstance(o, Mem) and o.base in STACK_BASES
                    and o.index is None and o.sym is None):
                continue
            w = _width(it, o)
            if w is None or w == 4:
                continue
            if o.base == "ebp" and not ebp_is_frame:
                # ebp is a data pointer here, not a frame: the access follows
                # the pointer ('plain') convention and the displacement is
                # already right. `swapped` only records that it is accounted
                # for.
                it.ops[i] = replace(o, swapped=True)
                continue
            if (o.base, o.disp) in subword:
                # A PROVEN word/byte FIELD: the displacement is already the
                # right one on both machines; `swapped` only tells lower.ea()
                # the slot is accounted for.
                it.ops[i] = replace(o, swapped=True)
                continue
            # AN INCOMING ARGUMENT IS ALWAYS A PUSHED DWORD, so the
            # single-width rule below MUST NOT claim it. With ebp a frame
            # pointer, [ebp+8] and up are the cdecl argument slots, and x86
            # PUSH writes four bytes even when the disassembly spells it
            # `push byte +0x1d` -- the byte form is an ENCODING, not a width.
            # A byte read of such a slot therefore means "the low byte of a
            # pushed dword" and needs the endianness flip exactly like a slot
            # proven by an in-function dword access.
            #
            # Getting this wrong is not theoretical: FUNC_000927, the object
            # allocator, reads its flag argument with `mov al,[ebp+0x10]` and
            # never touches the slot at dword width, so the single-width rule
            # passed the displacement through and the 68k read the byte at
            # +0x10 -- the HIGH byte of 0x0000001d, i.e. ZERO. Every object it
            # allocated was flagged free, so all seven bodies of the star
            # system landed in the same slot and overwrote each other, the
            # starport never existed, the ship inherited a zero orientation
            # matrix from a blank station, and the 3D viewport rendered
            # nothing. Measured 2026-08-02: ALLOC n=7 with the same slot 114
            # every time. 75 functions read an incoming argument sub-word this
            # way, so this was silently wrong game-wide.
            #
            # k is the byte offset inside the pushed dword: arguments start at
            # +8 and are dword-sized, so k = (disp - 8) % 4 and the slot base
            # is disp - k. If a caller ever pushed a by-value struct the same
            # arithmetic still holds inside each pushed dword.
            # AN INCOMING ARGUMENT IS ALWAYS A PUSHED DWORD. With ebp a frame
            # pointer, [ebp+8] and up are the cdecl argument slots, and x86
            # PUSH writes four bytes even where the disassembly spells it
            # `push byte +0x1d` -- the byte form is an ENCODING, not a width.
            # A sub-word read of such a slot therefore means "the low byte (or
            # low word) of a pushed dword" and needs the same endianness flip
            # as a slot proven by an in-function dword access. k is the byte
            # offset inside the pushed dword: arguments start at +8 and are
            # dword-sized, so k = (disp - 8) % 4.
            #
            # This is held to the oracle: 5101/5101 records identical on the
            # emulated 68020 with the rule ON, which is what licenses it over
            # the 75 functions it moves -- including FUNC_001491..001494,
            # whose packed-shorts case the header above could not decide by
            # reading the function alone. The oracle decides it by execution.
            # THE BLANKET VERSION OF THE RULE ABOVE IS WRONG AND WAS TRIED
            # TWICE. Every incoming argument really is a pushed dword -- the
            # disassembly contains ZERO 16-bit pushes, checked -- but that is
            # not enough, because the header's other case is real: a FOUR-CHAR
            # code pushed as one dword and read byte by byte wants NO flip,
            # and FFE passes system codes exactly that way. With the blanket
            # rule on, the oracle stayed GREEN (5101/5101, so the 96 carried
            # functions are all genuine scalars) and the GAME STOPPED TAKING
            # THE SPLASH KEYS -- the counter-example lives in the ~70 uncarried
            # functions, i.e. exactly where the oracle cannot see it. That is
            # the whole argument for per-slot evidence: the oracle can license
            # a rule only over what it covers.
            #
            # So the correction stays in PROVEN_BY_CALLERS, one slot at a time
            # with the callers read. FUNC_000927's flag argument is there and
            # is oracle-green; the other 74 functions that read an incoming
            # argument sub-word are listed by the scan in LEFTOFF and each
            # needs its callers looked at before it moves.

            # (kept for the record) Turning the rule above on for
            # every incoming argument re-translated 75 functions at once and
            # the game stopped responding to the splash keys -- among the 96
            # oracle-carried functions it also moves FUNC_001491..001494,
            # which this module's own header says are the case nothing here
            # can decide (a packed pair of shorts and a by-value struct of two
            # shorts want different code). So the correction goes in through
            # PROVEN_BY_CALLERS, one slot at a time with the callers read, and
            # the blanket version waits for the oracle loop to be runnable
            # again (build\xlat currently holds more than the carried set, so
            # ffe_xlattest does not link).
            if (slots.get((o.base, o.disp)) == {w}
                    and not any((o.base, dd) in dword
                                for dd in range(o.disp - 3, o.disp + w))):
                # SINGLE-WIDTH slot: every access to this displacement in the
                # whole function uses THIS width and no proven dword slot
                # overlaps it. A word (or byte) written and read at one
                # offset round-trips identically on both machines -- there is
                # no wider access whose byte order could disagree -- so the
                # displacement passes through. (FUNC_000878's [ebp-2] word
                # local: six accesses, all 16-bit.) The same esp/ebp-aliasing
                # blind spot the dword proof already has applies here, no
                # more, no less.
                it.ops[i] = replace(o, swapped=True)
                continue
            # Find the proven dword slot this access falls inside. Searching for
            # the enclosing slot rather than assuming 4-byte alignment means a
            # frame that is not aligned the way we expect produces no match --
            # and therefore a refusal -- instead of a wrong displacement.
            for k in range(0, 4 - w + 1):
                if (o.base, o.disp - k) in dword:
                    new = (o.disp - k) + (4 - w - k)
                    # AUDIT (2026-09-25): a LOCAL may never be flipped onto
                    # the saved frame pointer / return address. The x86 dword
                    # at [ebp-3] spans [ebp+0], so mirroring its byte 0 lands
                    # on (0,a5) -- the high byte of the caller's saved a5 --
                    # and every call silently corrupted the caller's frame
                    # (eight functions measured: 000460, 000506, 000961,
                    # 000910, 001024 writing it, 000110/714/718 reading it).
                    # Such a slot is a byte aggregate, never a value: declare
                    # it in PROVEN_SUBWORD with its evidence instead.
                    if o.base == "ebp" and o.disp < 0 and new + w > 0:
                        raise TranslationError(
                            "ffe.asm:%d: %s -- the endianness flip would move "
                            "[ebp%+d] (%d-bit) onto [ebp%+d], i.e. into the "
                            "saved frame pointer. The slot is a byte aggregate "
                            "(declare its displacements in frame.py "
                            "PROVEN_SUBWORD with evidence); refusing rather "
                            "than corrupting the caller's frame."
                            % (it.line, it.text.strip(), o.disp, w * 8, new))
                    it.ops[i] = replace(o, disp=new, swapped=True)
                    notes.append((it.line, it.text, o.base, o.disp, new, w))
                    break

    return notes
