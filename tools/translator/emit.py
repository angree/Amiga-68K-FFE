#!/usr/bin/env python3
"""Turn a lowered function into a vasm Motorola-syntax source file.

Output shape, per PLAN 3.4 (harness mode: one .s per original function):

    FUNC_nnnnnn     the translated body, t-ABI (PLAN 3.3). This is what other
                    translated code calls, and it is a mechanical image of the
                    x86: same stack frame, same argument offsets, no prologue
                    of its own.
    _FUNC_nnnnnn    a C-ABI entry that wraps it. See the long comment in
                    c_entry() for why this is not optional.

Assemble with:  vasmm68k_mot -Fhunk -m68020 -no-opt
`-no-opt` matters: it stops vasm substituting instructions behind the
translator's back. The whole correctness argument is about which instruction is
emitted and what it does to the CCR, so the assembler must emit what it is told.
"""
import datetime
import textwrap

import flags as F
import frame
import lower as _lowmod
from lower import Line, label, lower
from parse import Insn, Label, Mem, Table, TranslationError, canon, fail

# The C ABI's callee-saved set on m68k, which is exactly what the t-ABI treats
# as scratch. PLAN 3.3 names this same list for the four hand-written entry
# wrappers the full port needs.
SAVED = "d2-d7/a2-a6"
SAVED_BYTES = 11 * 4


def has_standard_prologue(fn):
    ins = fn.insns
    return (len(ins) >= 2
            and ins[0].mnem == "push" and str(ins[0].ops[0]) == "ebp"
            and ins[1].mnem == "mov" and str(ins[1].ops[0]) == "ebp"
            and str(ins[1].ops[1]) == "esp")


def arg_count(fn):
    """Number of cdecl arguments, read off the frame accesses.

    With `push ebp; mov ebp,esp` in front, [ebp+0] is the saved ebp, [ebp+4] the
    return address and [ebp+8] the first argument -- and the 68k lands on the
    identical layout because JSR pushes a 4-byte return address just as CALL
    does. So the highest [ebp+d] with d >= 8 gives the argument count."""
    hi = 4
    for ins in fn.insns:
        for o in ins.ops:
            if isinstance(o, Mem) and o.base == "ebp" and o.disp >= 8:
                hi = max(hi, o.disp)
    return (hi - 8) // 4 + 1 if hi >= 8 else 0


def c_entry(fn, nargs):
    """The C-callable wrapper. Returns a list of Lines."""
    sym = "_" + fn.base
    disp = SAVED_BYTES + 4 * nargs
    L = [
        Line(comment="-" * 76),
        Line(comment="C-ABI entry point. The test harness and, later, the four"),
        Line(comment="platform->game call sites (PLAN 3.3) come in through here."),
        Line(comment=""),
        Line(comment="The body below is t-ABI: d2 holds edx and d7 is scratch, and both"),
        Line(comment="count as caller-saved. The m68k C ABI says the opposite -- d2-d7 and"),
        Line(comment="a2-a6 belong to the caller. That is not a theoretical clash: compiling"),
        Line(comment="the probe's own `for` loop with bebbo gcc -O1 puts the loop counters"),
        Line(comment="in d2/d3/d4 and keeps them live across the call, so calling the body"),
        Line(comment="directly from C would corrupt them. Hence this entry."),
        Line(comment=""),
        Line(comment="Arguments cannot simply stay where they are, because the MOVEM has"),
        Line(comment="moved sp by %d bytes and the JSR below will push a second return" % SAVED_BYTES),
        Line(comment="address. They are copied down instead. Every copy uses the SAME"),
        Line(comment="displacement: each push moves sp down 4 bytes and the next argument"),
        Line(comment="to fetch is 4 bytes lower, so the two cancel."),
        Line(comment="-" * 76),
        Line("xdef\t" + sym),
        Line(),
    ]
    L.append(label(sym))
    L.append(Line("movem.l\t%s,-(sp)" % SAVED))
    for k in range(nargs, 0, -1):
        L.append(Line("move.l\t(%d,sp),-(sp)" % disp, "arg %d" % k))
    L.append(Line("jsr\t" + fn.base))
    if nargs:
        L.append(Line("lea\t(%d,sp),sp" % (4 * nargs), "drop the copied arguments"))
    L.append(Line("movem.l\t(sp)+,%s" % SAVED))
    L.append(Line("rts"))
    L.append(Line())
    return L


def frame_note(swaps):
    """The rewrites frame.apply() made, spelled out in the emitted file.

    This is not decoration. Every line here is a displacement that DIFFERS from
    the x86 original, so anyone diffing the two by eye would otherwise read it
    as a translator bug -- and the whole point of the rewrite is that reading it
    as the same displacement is what the bug actually was."""
    if not swaps:
        return []
    L = [
        Line(comment=""),
        Line(comment="BIG-ENDIAN STACK DISPLACEMENTS (frame.py). Each line below is a"),
        Line(comment="SUB-WORD access to a stack slot that this function also touches at"),
        Line(comment="full dword width, which proves the slot holds a 4-byte scalar. A"),
        Line(comment="scalar is stored the opposite way round on the two machines, so the"),
        Line(comment="byte or word the x86 names at one displacement lives at another"),
        Line(comment="here: offset k inside the slot becomes 4-width-k."),
        Line(comment=""),
        Line(comment="The displacement in the emitted code is therefore DELIBERATELY not"),
        Line(comment="the one in the ffe.asm comment above it. Reading those two as the"),
        Line(comment="same number is exactly the bug the oracle caught in"),
        Line(comment="FUNC_001612_DrawCircle (ffe.asm:96611, `mov al,[ebp+0x10]')."),
        Line(comment=""),
    ]
    for line, text, base, old, new, w in swaps:
        L.append(Line(comment="    ffe.asm:%-7d %-34s  [%s%+d] -> [%s%+d]  %d-bit"
                              % (line, text, base, old, base, new, w * 8)))
    L.append(Line(comment=""))
    return L


def header(fn, live_after, nargs, source, swaps=()):
    reg = "  ".join("%s=%s" % (k, v) for k, v in [
        ("eax", "d0"), ("ecx", "d1"), ("edx", "d2"), ("ebx", "d3"),
        ("esi", "a2"), ("edi", "a3"), ("ebp", "a5"), ("esp", "sp")])

    L = [
        Line(comment="GENERATED by tools\\translator\\translate.py -- do not edit."),
        Line(comment="Regenerate with:"),
        Line(comment="    python tools\\translator\\translate.py %s -o build\\xlat" % fn.base),
        Line(comment=""),
        Line(comment="source : %s:%d   %s" % (source, fn.line, fn.name)),
        Line(comment="date   : %s" % datetime.date.today().isoformat()),
        Line(comment="regs   : %s" % reg),
        Line(comment="scratch: a0 a1 d7        arguments: %d" % nargs),
    ] + ([
        Line(comment="padding: %d trailing alignment NOP(s) after the final RET were"
                     % fn.padding),
        Line(comment="         dropped -- see parse._strip_padding(), which proves them"),
        Line(comment="         unreachable before discarding anything."),
    ] if fn.padding else []) + [
        Line(comment=""),
        Line(comment="FLAG LIVENESS (PLAN 4.2), computed backwards over this function's"),
        Line(comment="CFG with the flags dead at RET. The column is the set still live"),
        Line(comment="AFTER each instruction, and it is what licenses every shortcut taken"),
        Line(comment="below -- MOVEQ instead of OR, PEA instead of MOVE, LSR clearing V"),
        Line(comment="where x86 SHR sets OF. Where a shortcut would have disturbed a live"),
        Line(comment="flag the translator refuses to emit at all."),
        Line(comment=""),
        Line(comment="    live-after   ffe.asm   instruction"),
    ]
    for k, it in enumerate(fn.items):
        if isinstance(it, Insn):
            L.append(Line(comment="    %-12s %-9d %s"
                                  % (F.show(live_after[k]), it.line, it.text)))
        else:
            L.append(Line(comment="    %-12s %-9d %s:" % ("", it.line, it.name)))
    L.append(Line(comment=""))
    return L + frame_note(swaps)


def emit(fn, source, c_entry_wanted=True):
    # ORDER MATTERS. arg_count() reads the [ebp+d] displacements, and
    # frame.apply() is about to change some of them, so the argument count is
    # taken off the ORIGINAL frame first. Getting this backwards would make a
    # function whose only sub-word argument is the last one report one argument
    # too few, and the C-ABI wrapper would then copy too little.
    nargs = arg_count(fn)
    swaps = frame.apply(fn)
    live_after = F.liveness(fn)

    if c_entry_wanted and not has_standard_prologue(fn):
        raise TranslationError(
            "%s (%s:%d) has no `push ebp; mov ebp,esp` prologue, so its "
            "argument count cannot be read off the frame and a C-ABI entry "
            "cannot be generated. Translate it with --no-c-entry."
            % (fn.name, source, fn.line))

    out = header(fn, live_after, nargs, source, swaps)
    out.append(Line("section\tcode,code"))
    out.append(Line())

    if c_entry_wanted:
        out += c_entry(fn, nargs)

    out.append(Line(comment="-" * 76))
    out.append(Line(comment="Translated body (t-ABI). One x86 instruction per block; the"))
    out.append(Line(comment="ffe.asm line number is on every one of them."))
    out.append(Line(comment="-" * 76))
    out.append(Line("xdef\t" + fn.base))
    out.append(Line())
    out.append(label(fn.base))

    ctx = None
    _lowmod.LOCAL_TABLES = {t.name for t in fn.items
                            if isinstance(t, Table)}
    for k, it in enumerate(fn.items):
        if isinstance(it, Label):
            out.append(Line())
            # LOC_ labels are mangled NASM locals (see parse._LOCAL_LABEL):
            # they exist only inside this one function, so exporting them
            # would be noise.
            if not it.name.startswith("LOC_"):
                out.append(Line("xdef\t" + it.name))
            out.append(label(it.name))
            continue

        if isinstance(it, Table):
            # An embedded jump table, emitted in place -- data in the code
            # section, unreachable as code because it sits after an
            # unconditional transfer (the CFG pass refuses any layout where
            # that does not hold). dc.l is PLAN 5's invariant at work: the
            # assembler puts 68k label addresses in the slots, so no x86
            # address ever exists at run time.
            #
            # ALIGNMENT IS NOT OPTIONAL. An embedded BYTE blob of odd length
            # leaves everything after it odd-aligned: the next jump table's
            # dc.l relocations land on odd addresses (vasm warning 3023,
            # which the build treated as non-fatal) and every later branch
            # label in the function is odd -- Guru 80000003 the moment one
            # is taken. Caught live in FUNC_001478 (fault address = branch
            # target = label+odd, 2026-07-30). Every consumer reaches these
            # tables by LABEL, never by offset from the surrounding code, so
            # an `even` before each table and after each blob is invisible
            # to the program and mandatory for the machine.
            out.append(Line())
            if it.bytes_ and not it.entries:
                # A byte lookup blob living next to its jump table
                # (FUNC_000037's opcode-class table): byte-identical dc.b,
                # so the indexed byte reads keep their offsets. Storage is
                # x86 order by construction and only byte reads touch it.
                out.append(Line(comment="embedded byte table, ffe.asm:%d -- "
                                        "%d bytes" % (it.line,
                                                      len(it.bytes_))))
                out.append(Line("even"))
                out.append(label(it.name))
                for o in range(0, len(it.bytes_), 16):
                    out.append(Line("dc.b\t" + ",".join(
                        str(b) for b in it.bytes_[o:o + 16])))
                out.append(Line("even"))
                continue
            out.append(Line(comment="embedded jump table, ffe.asm:%d -- %d "
                                    "entries" % (it.line, len(it.entries))))
            out.append(Line("even"))
            out.append(label(it.name))
            for e in it.entries:
                out.append(Line("dc.l\t" + canon(e)))
            if it.bytes_:
                # the stray disassembler tail after the table (see parse)
                out.append(Line("dc.b\t" + ",".join(
                    str(b) for b in it.bytes_)))
                out.append(Line("even"))
            continue

        lw = lower(ctx, it, live_after[k])
        out.append(Line())
        out.append(Line(comment="ffe.asm:%d   %s   [live after: %s]"
                                % (it.line, it.text, F.show(live_after[k]))))
        for para in (lw.why.split("\n") if lw.why else []):
            for line in textwrap.wrap(para, 74) or [""]:
                out.append(Line(comment=line))
        out += lw.lines

    out.append(Line())
    text = "\n".join(l.render() for l in out) + "\n"

    # The emitted source is ASCII by construction. Assert it rather than trust
    # it: a stray non-ASCII character in a generated file turns into mojibake
    # the moment the file crosses to the Amiga side or through a cp1252 editor.
    text.encode("ascii")
    return text
