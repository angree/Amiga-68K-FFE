#!/usr/bin/env python3
"""Resolve the LAST PALMAP block of an Amiga ffe_dump.log to caller functions.

    py build\\palmap.py [ffe_dump.log] [ffe_game.symmap]

The load base comes from the run's own ANCHOR line (FUNC_001772's runtime
address) against the symmap -- each FUNC_ appears twice there (t-ABI body and
C wrapper), and the body is the lower one. Codes are summed per caller so the
table can be set beside PCPALMAP's.
"""
import bisect
import collections
import re
import sys

LOG = sys.argv[1] if len(sys.argv) > 1 else r"C:\temp\ffe\work\ffe_dump.log"
MAP = sys.argv[2] if len(sys.argv) > 2 else \
    r"I:\GITHUB\Amiga_Frontier_2_First_Encouters\build\amiga-out\ffe_game.symmap"

syms = []
for l in open(MAP, encoding="latin-1"):
    m = re.match(r"\s+0x([0-9a-f]{8})\s+(FUNC_\S+)\s*$", l)
    if m:
        syms.append((int(m.group(1), 16), m.group(2)))
syms.sort()
keys = [a for a, n in syms]
lines = open(LOG, encoding="latin-1").read().split("\n")
anc = [l for l in lines if l.startswith("ANCHOR")][0]
rt = int(re.search(r"FUNC_001772=([0-9a-f]+)", anc).group(1), 16)
base = rt - min(a for a, n in syms if n == "FUNC_001772")
start = max(i for i, l in enumerate(lines) if l.startswith("PALMAP calls"))
print(lines[start])
per = collections.defaultdict(collections.Counter)
for l in lines[start + 1:]:
    m = re.match(r"PALMAP1 ret=(\w+) code=(\w+) n=(\d+)", l)
    if not m:
        break
    a = int(m.group(1), 16) - base
    fn = syms[bisect.bisect_right(keys, a) - 1][1]
    per[fn][m.group(2)] += int(m.group(3))
for fn in sorted(per):
    tot = sum(per[fn].values())
    print("%-12s n=%-8d %s" % (fn, tot, " ".join(
        "%s:%d" % (c, n) for c, n in per[fn].most_common(16))))
