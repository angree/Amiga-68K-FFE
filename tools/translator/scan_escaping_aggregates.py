#!/usr/bin/env python3
"""Scan: stack slots whose SUB-WORD accesses frame.apply() FLIPS as if the slot
were a scalar dword, while the same bytes also leave the function WHOLE in
address order -- `push dword [ebp+X]', a `lea' of an address inside the slot,
or `rep movs' from/to a `lea'd stack block.

That combination is the escaping-byte-aggregate miscompile (FUNC_001761,
001692/3/4/5, 001712, 001700, 001708, 001738, 001766, 001878, 001680 --
2026-08-02 and 2026-09-25): the flipped byte accesses land at mirrored
addresses while the whole-dword copy moves the bytes in memory order, so the
two views of the same bytes disagree. A flip is only right when every wider
view of the slot is a VALUE (a register load/store of the whole dword).

This does not decide anything. It RUNS frame.apply() (so the listing is exactly
what the translator emits) and prints each candidate with the evidence lines;
each one still needs its producer and consumers read before it goes into
PROVEN_SUBWORD. Output: build\\datamap\\escaping_aggregates.tsv

    py tools\\translator\\scan_escaping_aggregates.py
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
OUT = os.path.join(ROOT, "build", "datamap", "escaping_aggregates.tsv")


def main():
    lines = io.open(ASM, encoding="latin-1").readlines()

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
    rows = []
    for n in names:
        try:
            fn = parse.read_function(ASM, n)
        except TranslationError:
            continue
        # Escape evidence, collected BEFORE apply() rewrites displacements.
        escapes = {}   # disp -> list of (line, text)
        for it in fn.insns:
            for o in it.ops:
                if not (isinstance(o, Mem) and o.base == "ebp"
                        and o.index is None and o.sym is None):
                    continue
                if it.mnem == "lea":
                    escapes.setdefault(o.disp, []).append((it.line, "lea"))
                elif it.mnem == "push" and frame._width(it, o) == 4:
                    escapes.setdefault(o.disp, []).append((it.line, "push"))
        if not escapes:
            continue
        try:
            notes = frame.apply(fn)
        except Exception:
            continue
        for (line, text, base, old, new, w) in notes:
            if base != "ebp":
                continue
            # the flipped access sits inside the dword starting at old-k
            hit = []
            for k in range(0, 4):
                d = old - k
                if d in escapes:
                    hit += escapes[d]
            # a lea'd block that the access falls into (rep movs / callee fill)
            for d, ev in escapes.items():
                if any(t == "lea" for _, t in ev) and d <= old < d + 16 and d not in [old - k for k in range(4)]:
                    hit += [(l, "lea-block@%d" % d) for l, t in ev if t == "lea"]
            if hit:
                rows.append((fn.base, old, new, w, line, text.strip(),
                             ",".join("%d:%s" % h for h in hit[:4])))
    with io.open(OUT, "w", encoding="utf-8") as f:
        f.write("# func\tx86_disp\temitted_disp\twidth\tline\ttext\tescape_evidence\n")
        for r in rows:
            f.write("\t".join(str(x) for x in r) + "\n")
    funcs = sorted(set(r[0] for r in rows))
    print("%d flipped accesses in %d functions -> %s" % (len(rows), len(funcs), OUT))
    for fnb in funcs:
        print(fnb, sum(1 for r in rows if r[0] == fnb))


if __name__ == "__main__":
    main()
