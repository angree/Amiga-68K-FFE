#!/usr/bin/env python3
"""ADDRESS-TAKEN labels whose accesses are all one width -- the DATA_007235 class.

The static scan types a label from the accesses that NAME it. A label whose
address is loaded into a register instead (`mov edx,DATA_007235`) is reported
`addr-only`, and convert_static.py then stores it 'plain' -- raw x86 bytes,
no swap on any width. That default is SAFE only when the label really is
byte-addressed. It is WRONG when every access through the register is a dword:
the 68k then reads each dword byte-reversed, silently.

That is not hypothetical. DATA_007235 (the camera position) and DATA_007236
(the view rotation matrix) are addr-only, are read exclusively as dwords, and
being left raw made the 68k see an eye 6,440x too far away and a rotation
matrix 2^24 too small -- every projected vertex collapsed onto the view centre
and the 3D viewport rendered nothing at all (measured 2026-08-01: DrawQuad
receiving four identical vertices, DrawHLine never called).

This tool finds the rest of that class. For each `mov reg,DATA_nnn` /
`lea reg,[DATA_nnn]` site it propagates the register inside the function --
copies, constant advances, scaled indices -- and tallies the width of every
memory access made through it. A label is REPORTED when

  * its scan class is addr-only (or the label is unreferenced), AND
  * every access seen is the same width (4 or 2), AND
  * its size is a whole number of those units, AND
  * the address never ESCAPES (pushed to a call, stored to memory) inside the
    scanning window -- an escaped pointer can be read at any width elsewhere,
    so those are listed separately as UNPROVEN rather than recommended.

Nothing is changed automatically: each hit still needs its evidence line in
tools\\datamap\\overrides.tsv, because the recommendation is only as good as
the propagation, and a wrong storage class is a silent miscompile.

Usage: python tools\\datamap\\addrscan.py [--all]
"""
import io, os, re, sys
from collections import defaultdict

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
ASM = os.path.join(ROOT, "ref", "jjffe", "ffe.asm")
SCAN = os.path.join(ROOT, "build", "datamap", "staticscan.tsv")
OVR = os.path.join(os.path.dirname(os.path.abspath(__file__)), "overrides.tsv")

R32 = r"(?:eax|ebx|ecx|edx|esi|edi|ebp)"
_FUNC = re.compile(r"^(FUNC_\d{6})(?:_\w+)?:")
_TAKE = re.compile(r"^\s*(?:mov|lea)\s+(%s),\s*\[?(DATA_\d+)\]?\s*$" % R32)

BYTE_RE = re.compile(r"\bbyte\b|\b[a-d][lh]\b")
WORD_RE = re.compile(r"\bword\b|\b[a-d]x\b|\b(?:si|di)\b(?!:)")


def main():
    show_all = "--all" in sys.argv

    cls, size = {}, {}
    with io.open(SCAN, encoding="utf-8") as f:
        next(f)
        for l in f:
            p = l.rstrip("\n").split("\t")
            if len(p) >= 3:
                size[p[0]] = int(p[1] or 0)
                cls[p[0]] = p[2]

    already = set()
    with io.open(OVR, encoding="utf-8") as f:
        for l in f:
            p = l.split("\t")
            if p and p[0].startswith("DATA_"):
                already.add(p[0])

    lines = io.open(ASM, encoding="latin-1").read().split("\n")
    fstart = [(i, m.group(1)) for i, l in enumerate(lines)
              for m in [_FUNC.match(l)] if m]
    fstart.append((len(lines), None))

    widths = defaultdict(set)          # label -> {1,2,4}
    escaped = set()
    sites = defaultdict(list)

    for k in range(len(fstart) - 1):
        i0, i1 = fstart[k][0], fstart[k + 1][0]
        live = {}                      # reg -> label
        for i in range(i0, i1):
            s = lines[i].split(";")[0].strip()
            if not s:
                continue
            m = _TAKE.match(lines[i].split(";")[0])
            if m:
                live[m.group(1)] = m.group(2)
                sites[m.group(2)].append(i + 1)
                continue
            m = re.match(r"mov\s+(%s),(%s)$" % (R32, R32), s)
            if m:
                if m.group(2) in live: live[m.group(1)] = live[m.group(2)]
                else: live.pop(m.group(1), None)
                continue
            # the pointer leaves our sight: pushed, stored, or handed to a call
            m = re.match(r"push\s+(%s)$" % R32, s)
            if m and m.group(1) in live:
                escaped.add(live[m.group(1)]); continue
            m = re.match(r"mov\s+\[[^\]]*\],\s*(%s)$" % R32, s)
            if m and m.group(1) in live:
                escaped.add(live[m.group(1)]); continue
            if re.match(r"(?:rep\s+)?movs[bwd]", s):
                # a string move THROUGH esi/edi: width is in the mnemonic
                for r in ("esi", "edi"):
                    if r in live:
                        widths[live[r]].add({"b": 1, "w": 2, "d": 4}[s.rstrip()[-1]])
                continue
            m = re.search(r"\[(%s)(?:[^\]]*)?\]" % R32, s)
            if m and m.group(1) in live:
                lab = live[m.group(1)]
                if BYTE_RE.search(s):   widths[lab].add(1)
                elif WORD_RE.search(s): widths[lab].add(2)
                else:                   widths[lab].add(4)
            # any reload kills the register
            m = re.match(r"(?:mov|movsx|movzx|lea|pop|xor|and|or)\s+(%s)\b" % R32, s)
            if m and "[" in s and m.group(1) in live:
                live.pop(m.group(1), None)

    rec, unproven = [], []
    for lab, w in sorted(widths.items()):
        if lab in already:
            continue
        if cls.get(lab) not in ("addr-only", "unreferenced", None) and not show_all:
            continue
        if len(w) != 1:
            continue
        unit = w.pop()
        if unit == 1 or not size.get(lab) or size[lab] % unit:
            continue
        row = (lab, size[lab], unit, sites[lab][:3])
        (unproven if lab in escaped else rec).append(row)

    print("RECOMMENDED -- addr-only, single access width, pointer never escapes")
    print("%-14s %7s %5s  %s" % ("label", "bytes", "width", "address-taken at ffe.asm"))
    for lab, sz, unit, st in rec:
        print("%-14s %7d %5d  %s" % (lab, sz, unit, ", ".join(str(x) for x in st)))
    print("\nUNPROVEN -- same shape, but the pointer is passed on; audit by hand")
    for lab, sz, unit, st in unproven:
        print("%-14s %7d %5d  %s" % (lab, sz, unit, ", ".join(str(x) for x in st)))
    print("\n%d recommended, %d unproven" % (len(rec), len(unproven)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
