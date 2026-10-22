#!/usr/bin/env python3
"""VIEWPORT DEBUG: inject a call COUNTER at the entry of named translated
functions, in place, inside build\\xlat-full.

Why not massgen's trace_funcs.txt: that rig calls _FfeEnterLog, which does a
fopen/fprintf/fclose per call. A rasteriser primitive runs thousands of times
per frame, so the log rig would turn a 15-second observation into an hour and
lie about the timing anyway. _FfeCount only bumps a slot in a table that
ffe_video.c prints (and resets) once every 128 presents.

Why in place rather than through massgen: massgen regenerates all 1,615 files
and forces a full re-assemble; this touches the dozen files under test and
leaves every other .o cached.

The payload is the one massgen uses, which is proven register-transparent:
MOVEM saves d0-d7/a0-a6 and touches no CCR, and translated bodies read their
arguments from the stack, so the injection is invisible to the callee. It is
placed after the BARE label (FUNC_nnnnnn:), i.e. on the t-ABI entry every
translated caller uses; the C-ABI wrapper falls through it as well.

Usage:
    python tools\\translator\\inject_count.py 1601 1604 1613 ...
    python tools\\translator\\inject_count.py --optrace 1772   (loop head)
    python tools\\translator\\inject_count.py --mark1712       (opcode-4 bisect)
    python tools\\translator\\inject_count.py --strip          (remove all)
"""
import io, os, re, sys

OUTDIR = os.path.join(os.path.dirname(os.path.dirname(os.path.dirname(
    os.path.abspath(__file__)))), "build", "xlat-full")

MARK = "\t; INJECT_COUNT\n"
MARK_OP = "\t; INJECT_OPTRACE\n"
MARK_MK = "\t; INJECT_MARK\n"

# --w9cforce's marker. Deliberately NOT spelled `; INJECT_' : the generic
# strip() skips SIX lines after every `; INJECT_' marker, and this payload is
# two, so sharing the prefix would have strip() eat two real instructions.
MARK_W9C = "\t; W9CFORCE"

# --w9cforce's sites: `mov [ebp-0x4],eax' straight after `call FUNC_001534',
# in the only two functions that write record offset +0x9c.
#
#   FUNC_001536  ffe.asm:91255 call / 91257 store / 91272 `mov ax,[ebp-0x4]'
#                              / 91273 `mov [ebx+0x9c],ax'
#   FUNC_001537  ffe.asm:91374 call / 91376 store / 91391 `mov dx,[ebp-0x4]'
#                              / 91392 `mov [eax+0x9c],dx'
#
# The local is written ONCE and read ONCE in each function (grep of `ebp-0x4'
# over 91242-91411 returns exactly those four lines), so forcing eax here moves
# the +0x9c field and NOTHING else. That is what makes this a clean falsifier
# rather than a global clock hack: biasing FUNC_001534's return would have
# moved every other consumer of the game clock at the same time.
W9CFORCE_SITES = {1536: 91257, 1537: 91376}

# --optrace's site, per function. The opcode trace does NOT go on the bare
# label: the question is which OPCODE kills the machine, so the line has to be
# written once per iteration of the interpreter's loop, at its head and ahead
# of the `call eax' that runs the handler. The label is named here rather than
# discovered, because a guessed loop head silently instruments the wrong loop.
#   FUNC_001772: JUMP_006896 is ffe.asm:111194, immediately before the opcode
#   fetch at 111195.
OPTRACE_SITE = {1772: "JUMP_006896"}

# --mark1712's sites: a PROGRESS BISECT through the model interpreter's opcode
# 4, which is where the opcode trace put the hard reset (last line
# `OPT p=0x10282430 w=0x0004 op 4 FUNC_001711', reproducible over three runs).
#
# FUNC_001711 (ffe.asm:104862-104892) only unpacks six fields and calls
# FUNC_001712, so the body under suspicion is FUNC_001712 -- ffe.asm:104896-
# 105478, with 21 calls out of it. A marker before and after every one of them
# means the LAST `MARK1712' line in the log names the call the machine died
# inside; a marker on FUNC_001711's entry and its single exit separates a death
# in the wrapper from one in the body.
#
# The numbering is parity-coded so a log line reads without a table: 1 and 2 are
# FUNC_001711's entry and exit, 10 is FUNC_001712's entry, and from 11 up the
# ODD site is immediately BEFORE call k and the EVEN one immediately AFTER it,
# k in ffe.asm source order.
#
# EVERY call is instrumented, not only the ones named in the render chain:
# FUNC_001692 (104936) and FUNC_001691 (105356) are calls too, and leaving a
# gap in a bisect is how the answer comes back "between marker 24 and marker 25"
# with nothing to say what is in between.
MARK1712_CALLS = [
    104917,     # call near [DATA_007721]   FUNC_001481
    104936,     # call FUNC_001692
    104954,     # call near [DATA_007727]   FUNC_001471
    104977,     # call near [DATA_007727]   FUNC_001471
    105000,     # call near [DATA_007727]   FUNC_001471
    105021,     # call near [DATA_007727]   FUNC_001471
    105071,     # call FUNC_001695
    105210,     # call FUNC_001717
    105214,     # call FUNC_001717
    105220,     # call FUNC_001717
    105226,     # call FUNC_001717
    105243,     # call FUNC_001777
    105247,     # call FUNC_001778
    105322,     # call near [DATA_007629]   FUNC_001326_DPal32to16
    105347,     # call FUNC_001693
    105356,     # call FUNC_001691
    105391,     # call FUNC_001777
    105395,     # call FUNC_001778
    105415,     # call near [DATA_007629]   FUNC_001326_DPal32to16
    105453,     # call FUNC_001713
    105466,     # call FUNC_001714
]

# (site, kind, ffe.asm line). kind is `entry' (the bare t-ABI label), `before'
# (ahead of the block emitted for that line) or `after' (behind it).
MARK1712_SITES = {
    1711: [(1, "entry", 0),
           (2, "before", 104892)],          # the single RET
    1712: [(10, "entry", 0)] +
          [s for k, ln in enumerate(MARK1712_CALLS)
             for s in ((11 + 2 * k, "before", ln), (12 + 2 * k, "after", ln))],
}


def payload(num, fn="_FfeCount"):
    return (MARK +
            "\tmovem.l\td0-d7/a0-a6,-(sp)\n"
            "\tmove.l\t#%d,-(sp)\n"
            "\tjsr\t%s\n"
            "\taddq.l\t#4,sp\n"
            "\tmovem.l\t(sp)+,d0-d7/a0-a6\n" % (num, fn))


def payload_op(fn="_FfeOpTrace"):
    """The opcode-trace payload: same six-line shape as payload(), but it
    pushes d0 instead of a function number.

    At FUNC_001772's loop head d0 IS the x86 eax (`regs: eax=d0` in the
    generated header) and eax holds the bytecode stream pointer -- the fetch
    two instructions later is `movea.l d0,a0 / move.w (a0),d7'. MOVEM stores
    the registers without altering them, so d0 still holds the pointer when it
    is pushed. Register- and CCR-transparent for exactly the reasons the
    counter payload is, and the flags are dead at that point anyway."""
    return (MARK_OP +
            "\tmovem.l\td0-d7/a0-a6,-(sp)\n"
            "\tmove.l\td0,-(sp)\n"
            "\tjsr\t%s\n"
            "\taddq.l\t#4,sp\n"
            "\tmovem.l\t(sp)+,d0-d7/a0-a6\n" % fn)


def payload_mark(site, fn="_FfeMark1712"):
    """The progress-marker payload, returned as LINES rather than one string.

    Same six-line shape as payload() and stack-neutral for the same reasons, so
    it is legal between a call's argument pushes and the JSR itself: MOVEM saves
    d0-d7/a0-a6 without touching the CCR, the number is pushed and dropped
    again, and sp comes back exactly where it was. The one flag-visible step is
    the `move.l #site,-(sp)', and every site this is placed at reports
    `[live after: -----]' in the generated file -- x86 CALL neither reads nor
    writes a flag, so nothing is live on either side of one.

    Lines, because the sites are inserted at computed indices and several of
    them land in one file; splicing lists keeps that arithmetic honest."""
    return [MARK_MK.rstrip("\n"),
            "\tmovem.l\td0-d7/a0-a6,-(sp)",
            "\tmove.l\t#%d,-(sp)" % site,
            "\tjsr\t%s" % fn,
            "\taddq.l\t#4,sp",
            "\tmovem.l\t(sp)+,d0-d7/a0-a6"]


def strip(text):
    """Remove a previously injected block (marker + the 5 instructions).

    Matches EVERY `; INJECT_' marker, not only INJECT_COUNT: the opcode trace
    and the hand-placed INJECT_SHIFT payload have the same six-line shape, and
    `--strip' has to leave the file clean -- a leftover block is a jsr into a
    debug rig that is supposed to be gone."""
    lines = text.split("\n")
    out, i = [], 0
    while i < len(lines):
        if lines[i].strip().startswith("; INJECT_"):
            i += 6          # marker + movem + move + jsr + addq + movem
            continue
        out.append(lines[i])
        i += 1
    return "\n".join(out)


def strip_op(text):
    """Remove ONLY the opcode-trace block, leaving any counter payload alone.

    FUNC_001772.s carries both -- the FfeCode1772 counter on the bare label and
    this on the loop head -- and re-injecting one must not drop the other."""
    lines = text.split("\n")
    out, i = [], 0
    while i < len(lines):
        if lines[i].strip() == "; INJECT_OPTRACE":
            i += 6
            continue
        out.append(lines[i])
        i += 1
    return "\n".join(out)


def strip_mark(text):
    """Remove ONLY the progress-marker blocks, leaving any other payload alone
    -- the same courtesy strip_op() extends to the counter."""
    lines = text.split("\n")
    out, i = [], 0
    while i < len(lines):
        if lines[i].strip() == "; INJECT_MARK":
            i += 6
            continue
        out.append(lines[i])
        i += 1
    return "\n".join(out)


def payload_tri():
    """--tri1876's payload: hand the game's FRAME POINTER to the C probe.

    Same six-line, stack-neutral shape as payload() and payload_mark(), so
    strip() removes it like any other. MOVEM saves a5 and does not change it,
    so the `move.l a5,-(sp)' after it pushes the live frame pointer; the probe
    reads the locals off it at the offsets the EMITTED 68k actually uses.

    Passing a5 rather than the values themselves is deliberate: pushing twelve
    longs here would be twelve chances to name the wrong offset in assembly,
    where nothing checks it. In C the offsets sit next to the comment that says
    which ffe.asm local each one is."""
    return ["\t; INJECT_TRI",
            "\tmovem.l\td0-d7/a0-a6,-(sp)",
            "\tmove.l\ta5,-(sp)",
            "\tjsr\t_FfeTri1876",
            "\taddq.l\t#4,sp",
            "\tmovem.l\t(sp)+,d0-d7/a0-a6"]


def main_tri1876(args, do_strip):
    """--tri1876: record the triangle determinant FUNC_001876 is about to
    divide by, at ffe.asm:130812.

    The site is JUMP_008027, the label both paths into the divide converge on,
    so every value recorded is exactly what the divide will use -- including
    the clamp at ffe.asm:130806 when that path ran. Injecting BEFORE the block
    for ffe.asm:130808 (`mov ecx,ebx', the first instruction after the label)
    is what puts it there.

    Takes no function arguments: the site is fixed, because a hand-typed one
    would silently instrument a different divide."""
    if args:
        print("--tri1876 takes no arguments; its site is fixed (JUMP_008027)")
        return 1
    base = "FUNC_001876"
    path = os.path.join(OUTDIR, base + ".s")
    if not os.path.exists(path):
        print("MISSING %s -- uruchom najpierw massgen" % base)
        return 1
    text = io.open(path, encoding="utf-8").read()
    had = "; INJECT_TRI" in text
    lines = [l for l in _strip_kind(text, "; INJECT_TRI").split("\n")]
    if not do_strip:
        try:
            s, _e = block_bounds(lines, 130808, base)
        except ValueError as exc:
            print(str(exc))
            return 1
        # The label really has to be just above, or the probe is not on the
        # path into the divide at all.
        above = [l for l in lines[max(0, s - 4):s] if l.strip()]
        if not any(l.strip() == "JUMP_008027:" for l in above):
            print("%s: brak JUMP_008027: tuz nad ffe.asm:130808 (%r) -- "
                  "wstrzykniecie odmowione" % (base, above))
            return 1
        lines = lines[:s] + payload_tri() + lines[s:]
    with io.open(path, "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(lines))
    print("%s %s (JUMP_008027, ffe.asm:130812)"
          % ("USUNIETO z" if do_strip else "WSTRZYKNIETO do", base)
          + ("" if do_strip or not had else "  [bylo juz -- podmienione]"))
    return 0


def _strip_kind(text, marker):
    """Remove only the six-line blocks introduced by one marker."""
    lines = text.split("\n")
    out, i = [], 0
    while i < len(lines):
        if lines[i].strip() == marker.strip():
            i += 6
            continue
        out.append(lines[i])
        i += 1
    return "\n".join(out)


def main_w9cforce(args, do_strip):
    """--w9cforce N: THE TIMING FALSIFIER, run on OUR side instead of the
    reference's.

    THE QUESTION. The model interpreter never dispatches opcode 21 or opcode 1,
    where the reference dispatches them 24 and 3 times, and everything upstream
    is measured identical. The skip opcodes branch on fields seeded by record
    offset +0x9c, which holds the low word of FUNC_001534's return -- the game
    clock >> 10. It reads 9-11 here against 1 on the reference, because our
    model setup happens at a later game clock. WHETHER THAT CAUSES THE OPCODE
    DIVERGENCE IS UNPROVEN.

    WHY NOT ON THE REFERENCE. The handoff proposed slowing the reference down
    until its own model setup lands at our clock. That is a whole-program
    perturbation with no way to confirm it landed on the intended value. This
    does the opposite and is exact: force OUR field to the reference's value and
    see whether opcode 21 starts firing.

    WHAT MUST MOVE, pre-registered so a dud run cannot be read as a result:
      W9CSWEEP first= must change from 9 to N. If it does not, the injection did
      not take and the run proves NOTHING -- a falsifier that cannot move its
      own measured quantity is not a test.

    WHAT IS THEN UNDER TEST:
      CNT 1697 (opcode 21) is 0 here and 24 on the reference. If it becomes
      non-zero, the clock sample CAUSES the divergence and our translator is
      exonerated downstream; the real work becomes why our model setup runs so
      much later. If it stays 0, the clock is NOT the cause and the hunt goes
      back upstream -- which is worth just as much, because it retires a
      hypothesis that currently blocks everything below it.

    This is an EXPERIMENT, never a fix: it writes a constant into game state.
    Strip it with `--w9cforce --strip' before any run whose numbers are meant to
    describe the port."""
    if do_strip:
        val = None
    elif len(args) != 1:
        print("--w9cforce needs exactly one value (the forced +0x9c word), "
              "or --strip")
        return 1
    else:
        val = int(args[0])

    n_done = 0
    for num in sorted(W9CFORCE_SITES):
        base = "FUNC_%06d" % num
        path = os.path.join(OUTDIR, base + ".s")
        if not os.path.exists(path):
            print("MISSING %s (refused or never generated)" % base)
            continue
        text = io.open(path, encoding="utf-8").read()
        had = MARK_W9C.strip() in text
        # The payload is marker + ONE instruction, so removing it skips two
        # lines -- not the six the generic strip() uses for its own payload.
        lines, i, src = [], 0, text.split("\n")
        while i < len(src):
            if src[i].strip() == MARK_W9C.strip():
                i += 2
                continue
            lines.append(src[i])
            i += 1
        if not do_strip:
            s, _ = block_bounds(lines, W9CFORCE_SITES[num], base)
            # The store is `move.l d0,(-4,a5)'. Forcing d0 immediately ahead of
            # it is legal because the block reports `[live after: -----]' and
            # the block before it does too, so the flags MOVE writes are dead
            # on both sides of the splice.
            if "move.l\td0,(-4,a5)" not in "\n".join(lines[s:s + 12]):
                print("UNEXPECTED SHAPE at %s ffe.asm:%d -- refusing"
                      % (base, W9CFORCE_SITES[num]))
                return 1
            lines[s:s] = [MARK_W9C.strip(), "\tmove.l\t#%d,d0" % val]
        with io.open(path, "w", encoding="utf-8", newline="\n") as f:
            f.write("\n".join(lines))
        if do_strip:
            if had:
                print("stripped W9CFORCE from %s" % base)
                n_done += 1
        else:
            print("W9CFORCE %s ffe.asm:%d  [ebp-0x4] := %d%s"
                  % (base, W9CFORCE_SITES[num], val,
                     "  [bylo juz -- podmienione]" if had else ""))
            n_done += 1
    print("%d file(s) rewritten" % n_done)
    return 0


def block_bounds(lines, ffe_line, base):
    """[start, end) of the block translate.py emitted for one ffe.asm line.

    A block runs from its `; ffe.asm:N   ...' header to the blank line that
    separates it from the next -- the generator emits one x86 instruction per
    block and never a blank inside one. EXACTLY one header must match or this
    raises: a marker spliced into the wrong block is a JSR from a place nobody
    is looking at, which is worse than no marker at all."""
    pat = re.compile(r"^; ffe\.asm:%d\s" % ffe_line)
    hits = [k for k, l in enumerate(lines) if pat.match(l)]
    if len(hits) != 1:
        raise ValueError("%s: %d block header(s) for ffe.asm:%d, expected 1"
                         % (base, len(hits), ffe_line))
    s = hits[0]
    e = s + 1
    while e < len(lines) and lines[e].strip() != "":
        e += 1
    return s, e


def main_mark1712(args, do_strip):
    """--mark1712: inject (or strip) the opcode-4 progress bisect.

    Takes no function arguments -- the site table names the two files, because
    a bisect with a hand-typed function list is one typo away from silently
    instrumenting half of it."""
    if args:
        print("--mark1712 takes no arguments; its sites are fixed in "
              "MARK1712_SITES")
        return 1
    n_done = 0
    for num in sorted(MARK1712_SITES):
        base = "FUNC_%06d" % num
        path = os.path.join(OUTDIR, base + ".s")
        if not os.path.exists(path):
            print("MISSING %s (refused or never generated)" % base)
            continue
        text = io.open(path, encoding="utf-8").read()
        had = "; INJECT_MARK" in text
        lines = strip_mark(text).split("\n")
        n_here = 0
        if not do_strip:
            ins = {}
            for site, kind, ffe_line in MARK1712_SITES[num]:
                if kind == "entry":
                    label = base + ":"
                    hits = [k for k, l in enumerate(lines) if l == label]
                    if len(hits) != 1:
                        print("%s: %d bare label(s), expected 1"
                              % (base, len(hits)))
                        return 1
                    at = hits[0] + 1
                else:
                    try:
                        s, e = block_bounds(lines, ffe_line, base)
                    except ValueError as exc:
                        print(str(exc))
                        return 1
                    at = s if kind == "before" else e
                    if kind == "after":
                        # The block really has to END in the call, or `after'
                        # is not after the call at all. Comments outnumber
                        # instructions in these files, so filter them out.
                        body = [l for l in lines[s:e]
                                if l.strip() and not l.lstrip().startswith(";")]
                        if not body or "jsr" not in body[-1]:
                            print("%s: ffe.asm:%d does not end in a JSR (%r)"
                                  % (base, ffe_line, body[-1:] or None))
                            return 1
                ins.setdefault(at, []).extend(payload_mark(site))
                n_here += 1
            out = []
            for k, l in enumerate(lines):
                out.extend(ins.get(k, []))
                out.append(l)
            out.extend(ins.get(len(lines), []))
            lines = out
        with io.open(path, "w", encoding="utf-8", newline="\n") as f:
            f.write("\n".join(lines))
        if do_strip:
            if had:
                print("stripped %s (mark1712)" % base)
                n_done += 1
        else:
            print("marked   %s with %d site(s)" % (base, n_here))
            n_done += 1
    print("%d file(s) rewritten" % n_done)
    return 0


def main_optrace(args, do_strip):
    """--optrace: inject (or strip) the per-opcode trace at a loop head.

    Kept apart from main()'s bare-label path because the two differ in both
    the site and the argument, and because this one must NOT run the general
    strip() -- that would take the counter payload with it."""
    n_done = 0
    # `--strip --optrace' with no arguments means every file that CAN carry
    # one, which is the site table -- not every .s in the directory, which
    # would print 1,600 refusals.
    if do_strip and not args:
        args = sorted(OPTRACE_SITE)
    for a in args:
        num = int(a)
        base = "FUNC_%06d" % num
        path = os.path.join(OUTDIR, base + ".s")
        if num not in OPTRACE_SITE:
            print("NO OPTRACE SITE for %s -- add its loop-head label to "
                  "OPTRACE_SITE instead of guessing one" % base)
            continue
        if not os.path.exists(path):
            print("MISSING %s (refused or never generated)" % base)
            continue
        text = io.open(path, encoding="utf-8").read()
        had = "; INJECT_OPTRACE" in text
        text = strip_op(text)
        if not do_strip:
            label = OPTRACE_SITE[num] + ":"
            lines = text.split("\n")
            for k, l in enumerate(lines):
                if l == label:
                    lines[k] = l + "\n" + payload_op().rstrip("\n")
                    break
            else:
                print("NO %s in %s" % (label, base))
                continue
            text = "\n".join(lines)
        with io.open(path, "w", encoding="utf-8", newline="\n") as f:
            f.write(text)
        if do_strip:
            if had:
                print("stripped %s (optrace)" % base)
                n_done += 1
        else:
            print("optraced %s at %s" % (base, OPTRACE_SITE[num]))
            n_done += 1
    print("%d file(s) rewritten" % n_done)
    return 0


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    do_strip = "--strip" in sys.argv
    # --args: sample the first cdecl arguments instead of only counting
    # --ret: tally the traced function's RETURN ADDRESS, i.e. name its call
    # sites. Written for "which of FUNC_001820's call sites eats the
    # renderer's 3000-node pool"; the census prints through dump_clock.
    # --args8: eight cdecl dwords of the FIRST call per function (the level
    # the recursive terrain handlers stop on is argument 7).
    # --colour: the FUNC_001791 colour word only, one guarded dereference
    # --colourarg: arg1 verbatim, NO dereference (FUNC_001326_DPal32to16's
    # argument is the packed RGB dword itself, not a pointer)
    # --arena: the node-arena base at the fatal moment, flushed immediately
    # --alloc: the node allocator's cursor at entry, flushed immediately
    # --idx: FUNC_001471's signed-byte arena index, triggered on the anomaly
    # --nodelist: is FUNC_001788's node list empty?
    # --gatecall: independent check that FUNC_001780 runs (own counter)
    # --root: the root list sampled from inside the render
    # --vtxlog: the projected vertices of a drawn polygon
    # --projlog: the projection's camera-space input (x, y, z)
    # --veclog: the transform's input vector and matrix
    # --typelog: which vertex-type handler FUNC_001478 dispatches to
    # --objpos: both halves of the 64-bit object position FUNC_001683 reads
    # --rendpos: the render object's position triple, read from ARG2
    # --gatefield: the word at arg2+0xa, the FUNC_000880 planet gate
    # --sysgen: the system record FUNC_000878 is handed to generate the bodies
    # --packsys: the sources FUNC_000832 packs the system id from
    # --sysid: the first three cdecl dwords, for tracing the system id down
    # --dispatch: the module event dispatcher, filtered to event 0
    # --renderpath: a MONOTONIC progress counter through the render entry --
    # how far in the code gets before the machine resets. No stack walk, no
    # reset in dump_clock, nine slots fixed in ffe_rp_id[] (ffe_game.c).
    # --arena1745: the same counter plus FUNC_001745's own bound expression,
    # watching for a NEGATIVE watermark+size, which passes its signed check
    # --optrace: one line per opcode at the model interpreter's LOOP HEAD --
    # the last line before the hard reset names the handler that never
    # returned. Own site and own argument, hence main_optrace() below; it also
    # coexists with the FfeCode1772 counter already on the bare label.
    # --mark1712: progress markers through FUNC_001711/FUNC_001712, the opcode
    # the trace above died in. Fixed site table, own payload argument (a site
    # number, not a function number), and it coexists with anything else those
    # two files might carry -- hence main_mark1712() below.
    if "--optrace" in sys.argv:
        return main_optrace(args, do_strip)
    if "--mark1712" in sys.argv:
        return main_mark1712(args, do_strip)
    if "--tri1876" in sys.argv:
        return main_tri1876(args, do_strip)
    # --w9cforce N: force the +0x9c clock-sample word to N, to test whether the
    # opcode-21 divergence is the clock. Own site table, own payload shape and
    # its own marker; see main_w9cforce() for what must move and what is under
    # test. An EXPERIMENT, not a fix -- strip it before measuring the port.
    if "--w9cforce" in sys.argv:
        return main_w9cforce(args, do_strip)

    # --str1344: the string ID FUNC_001747's text opcode asks
    # FUNC_001344_StringExpandFFCode for, plus the buffer it hands it, so the
    # NEXT call can read back what the previous one produced. The reference
    # enters five glyph models per text opcode where we enter one, and this is
    # the first quantity on that path that both machines can be compared on.
    # --shift1686: the variable shift count inside FUNC_001686, the branch that
    # decides whether a glyph is interpreted at all. Its count is an unbounded
    # record field, so a value over 31 is the difference between x86 and an
    # unmasked 68k -- the class the cl mask closed this morning.
    # --cull1687: the position triple and the shifted size at FUNC_001687, the
    # function that actually returns the cull decision FUNC_001747 branches on.
    # --node1470: the index, the caller context position and the local
    # offset at FUNC_001470, which makes the position triple as a SUM of
    # the last two. Splits "which of the three is already wrong".
    fn = ("_FfeP36" if "--p36" in sys.argv else
          "_FfeP581" if "--p581" in sys.argv else
          "_FfeP879" if "--p879" in sys.argv else
          "_FfeFrame1478" if "--frame1478" in sys.argv else
          "_FfeFrameHalo" if "--framehalo" in sys.argv else
          "_FfeClip1837" if "--clip1837" in sys.argv else
          "_FfeWinCount" if "--wincount" in sys.argv else
          "_FfeTri1808" if "--tri1808" in sys.argv else
          "_FfeBlitMap" if "--blitmap" in sys.argv else
          "_FfePalMap" if "--palmap" in sys.argv else
          "_FfeRgbMap" if "--rgbmap" in sys.argv else
          "_FfeGate1745" if "--gate1745" in sys.argv else
          "_FfeVtxAnom" if "--vtxanom" in sys.argv else
          "_FfeObjRing" if "--objring" in sys.argv else
          "_FfeColCensus" if "--colcensus" in sys.argv else
          "_FfeNode1470" if "--node1470" in sys.argv else
          "_FfeCull1687" if "--cull1687" in sys.argv else
          "_FfeShift1686" if "--shift1686" in sys.argv else
          "_FfeStr1344" if "--str1344" in sys.argv else
          "_FfeFunc1534" if "--func1534" in sys.argv else
          "_FfeWrite1758" if "--write1758" in sys.argv else
          "_FfeCond1473" if "--cond1473" in sys.argv else
          "_FfeSkip1753" if "--skip1753" in sys.argv else
          "_FfeArena1745" if "--arena1745" in sys.argv else
          "_FfeRenderPath" if "--renderpath" in sys.argv else
          "_FfeModel1538" if "--model1538" in sys.argv else
          "_FfeCode1772" if "--code1772" in sys.argv else
          "_FfeTick1470" if "--tick1470" in sys.argv else
          "_FfeVtx1478" if "--vtx1478" in sys.argv else
          "_FfeScene1876" if "--scene1876" in sys.argv else
          "_FfeGen881" if "--gen881" in sys.argv else
          "_FfeBodyList" if "--bodylist" in sys.argv else
          "_FfeDispatch" if "--dispatch" in sys.argv else
          "_FfeSysId" if "--sysid" in sys.argv else
          "_FfeCr1838" if "--cr1838" in sys.argv else
          "_FfeSrf1840" if "--srf1840" in sys.argv else
          "_FfeSh1691" if "--sh1691" in sys.argv else
          "_FfeLin1703" if "--lin1703" in sys.argv else
          "_FfeTxt1747" if "--txt1747" in sys.argv else
          "_FfeMsb1657" if "--msb1657" in sys.argv else
          "_FfeObj1680" if "--obj1680" in sys.argv else
          "_FfeOrb986" if "--orb986" in sys.argv else
          "_FfeSysMap" if "--sysmap" in sys.argv else
          "_FfePackSys" if "--packsys" in sys.argv else
          "_FfeSysGen" if "--sysgen" in sys.argv else
          "_FfeGateField" if "--gatefield" in sys.argv else
          "_FfeRendPos" if "--rendpos" in sys.argv else
          "_FfeObjPos" if "--objpos" in sys.argv else
          "_FfeTypeLog" if "--typelog" in sys.argv else
          "_FfeVecLog" if "--veclog" in sys.argv else
          "_FfeProjLog" if "--projlog" in sys.argv else
          "_FfeVtxLog" if "--vtxlog" in sys.argv else
          "_FfeRootNow" if "--root" in sys.argv else
          "_FfeGateCall" if "--gatecall" in sys.argv else
          "_FfeNodeList" if "--nodelist" in sys.argv else
          "_FfeIdxNow" if "--idx" in sys.argv else
          "_FfeAllocNow" if "--alloc" in sys.argv else
          "_FfeArenaNow" if "--arena" in sys.argv else
          "_FfeColourArg" if "--colourarg" in sys.argv else
          "_FfeColourLog" if "--colour" in sys.argv else
          "_FfeArg8Log" if "--args8" in sys.argv else
          "_FfeRetCensus" if "--ret" in sys.argv else
          "_FfeObjLog" if "--obj" in sys.argv else
          "_FfeVtxTabLog" if "--vtx" in sys.argv else
          "_FfeStateLog" if "--state" in sys.argv else
          "_FfeArgLog" if "--args" in sys.argv else "_FfeCount")

    if do_strip and not args:
        args = [re.match(r"FUNC_(\d{6})\.s$", f).group(1)
                for f in os.listdir(OUTDIR) if re.match(r"FUNC_\d{6}\.s$", f)]

    n_done = 0
    for a in args:
        num = int(a)
        base = "FUNC_%06d" % num
        path = os.path.join(OUTDIR, base + ".s")
        if not os.path.exists(path):
            print("MISSING %s (refused or never generated)" % base)
            continue
        text = io.open(path, encoding="utf-8").read()
        had = "; INJECT_COUNT" in text
        text = strip(text)
        if not do_strip:
            lines = text.split("\n")
            for k, l in enumerate(lines):
                if l == base + ":":
                    lines[k] = l + "\n" + payload(num, fn).rstrip("\n")
                    break
            else:
                print("NO BARE LABEL in %s" % base)
                continue
            text = "\n".join(lines)
        with io.open(path, "w", encoding="utf-8", newline="\n") as f:
            f.write(text)
        if do_strip:
            if had:
                print("stripped %s" % base)
                n_done += 1
        else:
            print("counted  %s" % base)
            n_done += 1
    print("%d file(s) rewritten" % n_done)
    return 0


if __name__ == "__main__":
    sys.exit(main())

