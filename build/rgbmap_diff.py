#!/usr/bin/env python3
"""RGBMAP (Amiga) against PCRGBMAP (reference), per PRODUCER function.

    py build\\rgbmap_diff.py [amiga ffe_dump.log] [pc pc_objpos.log]

Both sides record the low 24 bits of the by-value RGB dword handed to
FUNC_001326_DPal32to16 (x86 value form: r = bits 7..0, g = 15..8, b = 23..16),
keyed by return address. Return addresses are resolved to the enclosing FUNC_:
Amiga via the run's ANCHOR line + build\\amiga-out\\ffe_game.symmap, the PC via
`nm` of the probe exe plus the image relocation delta, which is found from
FUNC_001579 (its DPal32to16 call returns at +0xf, ffe.asm:93613-93615).
Values print as rgb nibble triples (the game uses 4-bit channels, so 0x070707
prints 777); anything above 0xf in a channel prints in full.
"""
import bisect
import collections
import re
import subprocess
import sys

ROOT = r"I:\GITHUB\Amiga_Frontier_2_First_Encouters"
AM = sys.argv[1] if len(sys.argv) > 1 else r"C:\temp\ffe\work\ffe_dump.log"
PC = sys.argv[2] if len(sys.argv) > 2 else ROOT + r"\build\m0-pc\run\pc_objpos.log"


def rgb(v):
    r, g, b = v & 0xff, (v >> 8) & 0xff, (v >> 16) & 0xff
    if max(r, g, b) <= 0xf:
        return "%x%x%x" % (r, g, b)
    return "(%x,%x,%x)" % (r, g, b)


def last_block(lines, head, row):
    s = max(i for i, l in enumerate(lines) if l.startswith(head))
    out = []
    for l in lines[s + 1:]:
        m = re.match(row, l)
        if not m:
            break
        out.append((int(m.group(1), 16), int(m.group(2), 16), int(m.group(3))))
    return lines[s], out


def amiga():
    syms = []
    for l in open(ROOT + r"\build\amiga-out\ffe_game.symmap", encoding="latin-1"):
        m = re.match(r"\s+0x([0-9a-f]{8})\s+(FUNC_\S+)\s*$", l)
        if m:
            syms.append((int(m.group(1), 16), m.group(2)))
    syms.sort()
    keys = [a for a, n in syms]
    lines = open(AM, encoding="latin-1").read().split("\n")
    anc = [l for l in lines if l.startswith("ANCHOR")][0]
    rt = int(re.search(r"FUNC_001772=([0-9a-f]+)", anc).group(1), 16)
    base = rt - min(a for a, n in syms if n == "FUNC_001772")
    head, rows = last_block(lines, "RGBMAP calls",
                            r"RGBMAP1 ret=(\w+) val=(\w+) n=(\d+)")
    per = collections.defaultdict(collections.Counter)
    for ret, val, n in rows:
        per[syms[bisect.bisect_right(keys, ret - base) - 1][1]][val] += n
    return head, per


def pc():
    nm = subprocess.run([ROOT + r"\tools\mingw32\bin\nm.exe",
                         ROOT + r"\build\m0-pc\run\ffesdl-probe.exe"],
                        capture_output=True, text=True).stdout
    syms = []
    for l in nm.split("\n"):
        p = l.split()
        if len(p) == 3 and p[1] in "Tt" and p[2].startswith("FUNC_"):
            syms.append((int(p[0], 16), re.sub(r"(FUNC_\d+).*", r"\1", p[2])))
    syms.sort()
    keys = [a for a, n in syms]
    lines = open(PC, encoding="latin-1").read().split("\n")
    head, rows = last_block(lines, "PCRGBMAP calls",
                            r"PCRGBMAP1 ret=(\w+) val=(\w+) n=(\d+)")
    f1579 = [a for a, n in syms if n == "FUNC_001579"][0]
    deltas = collections.Counter(ret - (f1579 + 0xf) for ret, v, n in rows
                                 if (ret - (f1579 + 0xf)) % 0x1000 == 0)
    delta = deltas.most_common(1)[0][0] if deltas else 0
    per = collections.defaultdict(collections.Counter)
    for ret, val, n in rows:
        per[syms[bisect.bisect_right(keys, ret - delta) - 1][1]][val] += n
    return head + " delta=%#x" % delta, per


ha, pa = amiga()
hp, pp = pc()
print("AMIGA", ha)
print("PC   ", hp)
for fn in sorted(set(pa) | set(pp)):
    for tag, per in (("pc ", pp), ("ami", pa)):
        c = per.get(fn)
        if not c:
            print("%-12s %s  --" % (fn, tag))
            continue
        tot = sum(c.values())
        print("%-12s %s n=%-8d %s" % (fn, tag, tot, " ".join(
            "%s:%d" % (rgb(v), n) for v, n in c.most_common(12))))
