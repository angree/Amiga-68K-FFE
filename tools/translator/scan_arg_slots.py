#!/usr/bin/env python3
"""Find unremapped sub-word ARGUMENT slots -- the frame.py bug class where a
narrow (byte/word) read of an incoming cdecl argument, at a displacement no
in-function dword access proves, is passed through UNCHANGED because
frame.apply()'s "every access to this slot uses one width" rule is correct
for a LOCAL and wrong for an ARGUMENT (a pushed dword whose byte order the
CALLER decides, not the callee).

METHOD. This does not reimplement frame.py's rule; it RUNS frame.apply() on
every parsed function (via the real frame.py, imported unmodified) and reads
back what happened to every [ebp+N], N>=8, narrow memory operand:

  * operand ends up swapped=True with its displacement UNCHANGED and the slot
    is NOT literally present in frame.PROVEN_SUBWORD[fn.base]
        -> the single-width-passthrough branch fired with no proof: this is
           the silent bug this scanner exists to find.
  * operand ends up swapped=True with a DIFFERENT displacement
        -> the enclosing-dword search matched (an in-function dword access,
           or an existing frame.PROVEN_BY_CALLERS entry, proved the slot and
           frame.py already flips it correctly). Not reported.
  * operand ends up swapped=False (untouched)
        -> no proof anywhere and multiple widths seen at that displacement:
           lower.ea() refuses this instruction outright, so the function does
           not translate at all. That failure is already VISIBLE (a refusal),
           not silent, so it is reported separately (refused_multiwidth.tsv-
           worthy, printed as a count only) rather than mixed into the TODO
           list this script's caller asked for.

RULE FOR "ARGUMENT SLOT": base is 'ebp', the function actually establishes
ebp as a frame pointer (`mov ebp, esp` appears -- the exact test frame.apply()
itself uses; otherwise ebp is a data pointer, e.g. FUNC_001330_DPalBuild, and
[ebp+N] is not a cdecl argument at all), no index, no symbol, and disp >= 8
(above the saved ebp at +0 and the return address at +4).

Functions the parser itself refuses (embedded data blocks it does not carry,
unmodelled instructions, etc.) cannot be scanned at all; their count is
reported separately and they are NOT silently treated as zero.

Never modifies ref\\jjffe\\ffe.asm or tools\\translator\\frame.py.
"""
import io
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
ROOT = os.path.dirname(os.path.dirname(HERE))

import parse
import frame
from parse import Insn, Mem, TranslationError

ASM = os.path.join(ROOT, "ref", "jjffe", "ffe.asm")
OUT = os.path.join(ROOT, "build", "datamap", "arg_slots_todo.tsv")


def ebp_is_frame(fn):
    """The exact test frame.apply() uses: does this function ever execute
    `mov ebp, esp`? If not, ebp is a plain register / data pointer here and
    [ebp+N] is not a cdecl argument slot."""
    return any(
        isinstance(it, Insn) and it.mnem == "mov" and len(it.ops) == 2
        and getattr(it.ops[0], "name", None) == "ebp"
        and getattr(it.ops[1], "name", None) == "esp"
        for it in fn.items)


def main():
    lines = io.open(ASM, encoding="latin-1").readlines()

    # read_function re-reads the 2.5 MB file per call; feed it a cache, same
    # trick massgen.py uses.
    class _F:
        def readlines(self):
            return lines

        def __enter__(self):
            return self

        def __exit__(self, *a):
            return False

    parse.open = lambda *a, **k: _F()

    names = [m.group(1) for m in
             (re.match(r"^(FUNC_\d+(?:_\w+)?):", l) for l in lines) if m]
    print("function labels found in ffe.asm: %d" % len(names), file=sys.stderr)

    parse_failed = []
    no_frame = 0
    scanned = 0
    refused_multiwidth = 0  # candidates left swapped=False -- a visible refusal, not silent
    todo = []  # (fn_base, disp, sorted(widths), count, first_line)

    for n in names:
        try:
            fn = parse.read_function(ASM, n)
        except TranslationError as e:
            parse_failed.append((n, str(e).split("\n")[0][:120]))
            continue

        scanned += 1
        if not ebp_is_frame(fn):
            no_frame += 1
            continue

        # Collect the candidate operands BEFORE apply() mutates them, so we
        # can compare before/after per operand.
        candidates = []  # (Insn, op_index, orig_disp, width, line)
        for it in fn.insns:
            for i, o in enumerate(it.ops):
                if not (isinstance(o, Mem) and o.base == "ebp"
                        and o.index is None and o.sym is None):
                    continue
                if o.disp < 8:
                    continue
                w = frame._width(it, o)
                if w is None or w not in (1, 2):
                    # "narrower than 4 bytes" means byte/word only. A qword
                    # access (fld/fstp qword, x87 double arguments -- e.g.
                    # FUNC_002110's fprem helper) is WIDER than 4, a different
                    # problem entirely, and out of scope here.
                    continue
                candidates.append((it, i, o.disp, w, it.line))

        if not candidates:
            continue

        frame.apply(fn)  # real frame.py, unmodified, mutates fn.items in place

        subword = frame.PROVEN_SUBWORD.get(fn.base, set())

        per_slot = {}  # disp -> {"widths": set, "count": int, "line": int}
        for it, i, orig_disp, w, line in candidates:
            o2 = it.ops[i]
            if not o2.swapped:
                refused_multiwidth += 1
                continue
            if o2.disp != orig_disp:
                continue  # enclosing-dword search matched: already handled correctly
            if ("ebp", orig_disp) in subword:
                continue  # intentional passthrough, already accounted for
            d = per_slot.setdefault(orig_disp,
                                     {"widths": set(), "count": 0, "line": line})
            d["widths"].add(w)
            d["count"] += 1
            d["line"] = min(d["line"], line)

        for disp, info in per_slot.items():
            todo.append((fn.base, disp, sorted(info["widths"]), info["count"],
                          info["line"]))

    todo.sort(key=lambda r: (r[0], r[1]))

    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    with io.open(OUT, "w", encoding="utf-8", newline="\n") as f:
        f.write("function\tebp_disp_hex\twidths\tcount\tfirst_line\n")
        for fn_base, disp, widths, count, line in todo:
            wtxt = ",".join(str(w) for w in widths)
            f.write("%s\t%s\t%s\t%d\t%d\n" %
                    (fn_base, hex(disp), wtxt, count, line))

    funcs = sorted({r[0] for r in todo})
    print("functions with parse.read_function TranslationError: %d"
          % len(parse_failed), file=sys.stderr)
    print("functions scanned (parsed OK): %d" % scanned, file=sys.stderr)
    print("  of which ebp is not a frame pointer (skipped): %d" % no_frame,
          file=sys.stderr)
    print("candidate narrow-ebp-arg operands left UNSWAPPED (refused "
          "elsewhere, not silent): %d" % refused_multiwidth, file=sys.stderr)
    print("TODO slots: %d   TODO functions: %d" % (len(todo), len(funcs)),
          file=sys.stderr)
    print("wrote %s" % OUT, file=sys.stderr)


if __name__ == "__main__":
    main()
