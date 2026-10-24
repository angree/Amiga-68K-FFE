# SPDX-License-Identifier: MIT
# Copyright (c) 2026 angree. See LICENSE at the repository root.
"""Read a profiler log (PROGDIR:ffe_prof.log, 2026-10-24) against the symbol
map of the SAME build and print the frame rate and a function histogram.

    python prof_report.py E:/ffe/prof/base1      (reads base1.log + base1.symmap)
    python prof_report.py E:/ffe/prof/base1 -n 60 --by-file
    python prof_report.py E:/ffe/prof/base1 --by-func    (JUMP_/LOC_ folded)

The load base comes from the three CODE anchors on the MAP line, which must
agree (BSS is a separate hunk; a data anchor would give a wrong base).
Samples outside our code hunk are counted as 'outside' (OS, other tasks).
"""
import bisect
import re
import sys
from collections import Counter


def load_symmap(path):
    syms, objs = [], []
    cur_obj = None
    sec_pat = re.compile(r"^ (?:code|\.text)\s+0x([0-9a-f]+)\s+0x([0-9a-f]+)\s+(\S+)")
    sym_pat = re.compile(r"^\s+0x([0-9a-fA-F]{8,16})\s+([A-Za-z_.$][\w.$]*)\s*$")
    for line in open(path, encoding="latin-1"):
        m = sec_pat.match(line)
        if m:
            objs.append((int(m.group(1), 16), int(m.group(2), 16), m.group(3)))
            continue
        m = sym_pat.match(line.rstrip("\n"))
        if m:
            syms.append((int(m.group(1), 16), m.group(2)))
    syms.sort()
    objs.sort()
    return syms, objs


def main():
    base = sys.argv[1]
    top = int(sys.argv[sys.argv.index("-n") + 1]) if "-n" in sys.argv else 40
    by_file = "--by-file" in sys.argv
    syms, objs = load_symmap(base + ".symmap")
    if "--by-func" in sys.argv:   # fold JUMP_/LOC_ labels into their function
        syms = [(a, n) for a, n in syms if not re.match(r"(JUMP|LOC)_", n)]
    names = {n: a for a, n in load_symmap(base + ".symmap")[0]}
    lines = open(base + ".log", encoding="latin-1").read().split("\n")
    m = re.findall(r"(FUNC_\d+)=([0-9a-f]+)", lines[0])
    bases = set(int(v, 16) - names[k] for k, v in m)
    if len(bases) != 1:
        sys.exit("anchors disagree: %s" % [hex(b) for b in bases])
    load = bases.pop()
    i = lines.index("SAMPLES")
    for l in lines[1:i]:
        print(l)
    pcs = [int(l, 16) - load for l in lines[i + 1:] if l.strip()]
    hi = max(a for a, _ in syms)
    addrs = [a for a, _ in syms]
    c = Counter()
    for pc in pcs:
        if pc < 0 or pc > hi + 0x10000:
            c["(outside our code)"] += 1
            continue
        if by_file:
            k = bisect.bisect_right([o[0] for o in objs], pc) - 1
            c[objs[k][2].split("/")[-1] if k >= 0 else "?"] += 1
        else:
            k = bisect.bisect_right(addrs, pc) - 1
            c[syms[k][1] if k >= 0 else "?"] += 1
    n = len(pcs)
    print("samples %d, load base %x" % (n, load))
    acc = 0
    for name, k in c.most_common(top):
        acc += k
        print("%6d %5.1f%% %5.1f%%  %s" % (k, 100.0 * k / n, 100.0 * acc / n, name))


if __name__ == "__main__":
    main()
