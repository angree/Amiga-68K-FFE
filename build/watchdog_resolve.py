#!/usr/bin/env python3
"""Resolve a Work:ffe_watchdog.log stack dump to function names.

    py build\\watchdog_resolve.py C:\\temp\\ffe\\watchdog_try4.log

The load base comes from the log's MAP line (three CODE anchors, each FUNC_
appears twice in the symmap -- t-ABI body and C wrapper -- so every candidate
base is tried and the one all three anchors agree on wins). Every longword at
every 2-byte offset of the dumped stack that falls inside the code hunk is
printed with its enclosing symbol: exec's switch frame puts the interrupted
PC near the top, the rest are return addresses of the call chain.
"""
import bisect
import collections
import re
import sys

SYMMAP = r"I:\GITHUB\Amiga_Frontier_2_First_Encouters\build\amiga-out\ffe_game.symmap"

log = open(sys.argv[1]).read().split("\n")
mp = [l for l in log if l.startswith("MAP ")][0]
anchors = {k: int(v, 16) for k, v in re.findall(r"(FUNC_\d+)=([0-9a-f]+)", mp)}
words = []
for l in log:
    m = re.match(r"S \d+ ([0-9a-f]{8})", l)
    if m:
        v = int(m.group(1), 16)
        words += [(v >> 24) & 0xff, (v >> 16) & 0xff, (v >> 8) & 0xff, v & 0xff]

syms = []
for l in open(SYMMAP, encoding="latin-1"):
    m = re.match(r"\s+0x([0-9a-f]{8})\s+(\S+)\s*$", l)
    if m:
        syms.append((int(m.group(1), 16), m.group(2)))
syms.sort()
keys = [a for a, n in syms]
byname = collections.defaultdict(list)
for a, n in syms:
    byname[n].append(a)

votes = collections.Counter()
for name, rt in anchors.items():
    for a in byname.get(name, []):
        votes[rt - a] += 1
base, nv = votes.most_common(1)[0]
print("base=%#x (anchors agreeing: %d/%d)" % (base, nv, len(anchors)))
lo, hi = keys[0] + base, keys[-1] + base
for off in range(0, len(words) - 3, 2):
    v = (words[off] << 24) | (words[off + 1] << 16) | (words[off + 2] << 8) | words[off + 3]
    if lo <= v <= hi:
        i = bisect.bisect_right(keys, v - base) - 1
        print("  sp+%03d %08x  %s+%#x" % (off, v, syms[i][1], v - base - syms[i][0]))
