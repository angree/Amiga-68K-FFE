# SPDX-License-Identifier: MIT
# Find the FUNC_001560 class: a local dword loaded WHOLE from a pointer
# dereference -- so the local is a byte-for-byte copy of a caller struct --
# and later read at sub-word width somewhere inside that dword.
#
# Why this matters. frame.py's default for a sub-word access to a slot proven
# dword is to FLIP the displacement, because a 32-bit scalar's halves swap
# places between x86 and 68k. That is right for a scalar and WRONG for a
# struct copy: `struct { short x, y; }` has x at +0 and y at +2 on both
# machines, because the load and the store are both longwords and the bytes
# are simply moved. Flipping turns every read of y into a read of x.
#
# It was not hypothetical. FUNC_001560 (the span adder) reads the vertex Y at
# [ebp-0x2]; flipped to [ebp-0x4] it returned the X, so the row bounds handed
# to FUNC_001613_DrawSpans were 0..319 where the row array holds 158 rows of
# 44 bytes. DrawSpans then bubble-sorted in place at ctx+44*319 = +14036, far
# past the 6952-byte record, and the emulated machine hard-reset with no CPU
# exception. Fixed by listing the slots in frame.py's PROVEN_SUBWORD.
#
# The scan is deliberately blunt: it reports candidates, it does not classify
# them. A sub-word read at the SAME displacement as the store is the ambiguous
# case (x86 low half vs 68k high half) and still needs a human. The evidence
# that settles it is usually in the callee the address is passed to, or in the
# constants: FUNC_001704 compares +0 against 0x140 (320, the screen width) and
# +2 against 0x9e (158, the viewport height), which names the fields outright.

import io
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
ASM = os.path.join(ROOT, "ref", "jjffe", "ffe.asm")

STORE = re.compile(r"^\s*mov \[(ebp|esp)([-+])(0x[0-9a-f]+)\],(e[a-z]{2})\s*$")
LOAD = re.compile(r"^\s*mov (e[a-z]{2}),\[e[a-z]{2}\]\s*$")
MEM = re.compile(r"\[(ebp|esp)([-+])(0x[0-9a-f]+)\]")
NARROW = re.compile(r"\bword\b|\bbyte\b|movsx|movzx|,\s*[a-d][xl]\b|\b[a-d][xl]\s*,")


def main():
    lines = io.open(ASM, encoding="latin-1").readlines()

    ranges, cur, start = {}, None, None
    for i, l in enumerate(lines):
        m = re.match(r"^(FUNC_\d{6})", l)
        if m:
            if cur:
                ranges[cur] = (start, i)
            cur, start = m.group(1), i
    ranges[cur] = (start, len(lines))

    n = 0
    for fn in sorted(ranges):
        s, e = ranges[fn]
        body = lines[s:e]
        stores = set()
        for j, l in enumerate(body):
            m = STORE.match(l)
            if not m or j == 0:
                continue
            p = LOAD.match(body[j - 1])
            if not p or p.group(1) != m.group(4):
                continue
            d = int(m.group(3), 16)
            stores.add((m.group(1), -d if m.group(2) == "-" else d))
        if not stores:
            continue
        bad = set()
        for l in body:
            if not NARROW.search(l):
                continue
            for m in MEM.finditer(l):
                d = int(m.group(3), 16)
                d = -d if m.group(2) == "-" else d
                b = m.group(1)
                for k in range(0, 4):
                    if (b, d - k) in stores:
                        bad.add((b, d))
                        break
        if bad:
            n += 1
            print("%-14s whole-dword locals %s ; sub-word reads inside %s"
                  % (fn, sorted(stores), sorted(bad)))
    print("total functions in this class: %d" % n)
    return 0


if __name__ == "__main__":
    sys.exit(main())
