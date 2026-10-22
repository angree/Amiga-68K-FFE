#!/usr/bin/env python3
"""BASE+OFFSET SCAN -- accesses through a register that was loaded with the
ADDRESS of a named label (`mov ebx,DATA_008804` ... `mov ax,[ebx+0x8e]`).

    py tools\\translator\\scan_base_offsets.py   -> build\\datamap\\base_offsets_todo.tsv

WHY. Storage width is decided per LABEL (staticscan.tsv), but such an access
names only the base label, so the label actually hit -- base + offset, a
LATER label -- is invisible to every per-label check. FUNC_000929 restoring
the thrust words through `DATA_008804 + off` read the wrong half of six
dwords exactly this way (2026-10-09). Unlike the object-field atlas the
target here is KNOWN, so the check is mechanical:

  * label addresses = running sum of the `bytes` column of staticscan.tsv
    (file order = memory order inside one section);
  * inside a function a register holds a base from `mov reg,DATA_x` until it
    is written again (eax/ecx/edx also die at a call);
  * a NARROW access (1 or 2 bytes) landing inside a label whose class is
    dword-only / word-only at a sub-offset is a candidate, with the delta a
    scalar needs (same table as scan_field_atlas.py);
  * a WIDE access landing on byte-only labels is listed too (class `wide`):
    it is usually an aggregate copy and wants no fix, but it is the other
    half of the same disagreement.

Lines already in le_read_sites.tsv / lo_byte_sites.tsv are dropped.
A CANDIDATE LIST: look at the writer before registering anything.
"""
import bisect
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
ASM = os.path.join(ROOT, "ref", "jjffe", "ffe.asm")
SCAN = os.path.join(ROOT, "build", "datamap", "staticscan.tsv")
OUT = os.path.join(ROOT, "build", "datamap", "base_offsets_todo.tsv")
R32 = "eax ebx ecx edx esi edi".split()
SUB = {"ax": "eax", "al": "eax", "ah": "eax", "bx": "ebx", "bl": "ebx", "bh": "ebx",
       "cx": "ecx", "cl": "ecx", "ch": "ecx", "dx": "edx", "dl": "edx", "dh": "edx",
       "si": "esi", "di": "edi"}
R16 = set("ax bx cx dx si di".split())
R8 = set("al bl cl dl ah bh ch dh".split())
MEM = re.compile(r"\[(e[a-ds][xi])([+-]0x[0-9a-f]+)?\]")
NOWRITE = set("cmp test push call jmp".split())


def registered():
    got = set()
    for name in ("le_read_sites.tsv", "lo_byte_sites.tsv"):
        for l in open(os.path.join(ROOT, "tools", "datamap", name), encoding="utf-8", errors="replace"):
            m = re.match(r"(\d+)\t", l)
            if m:
                got.add(int(m.group(1)))
    return got


def labels():
    addr, names, starts, info = 0, [], [], {}
    for l in open(SCAN, encoding="utf-8"):
        p = l.rstrip("\n").split("\t")
        if p[0] == "label":
            continue
        n = int(p[1])
        names.append(p[0]); starts.append(addr); info[p[0]] = (addr, n, p[2], p[3])
        addr += n
    return names, starts, info


def width(mn, rest):
    if re.search(r"\bdword\b", rest):
        return 4
    if re.search(r"\bword\b", rest):
        return 2
    if re.search(r"\bbyte\b(?! [+-])", rest):
        return 1
    ops = [o for o in re.split(r"[ ,]+", rest) if o]
    if any(o in R8 for o in ops):
        return 1
    if any(o in R16 for o in ops):
        return 2
    if any(o in R32 for o in ops) or mn in ("push", "pop"):
        return 4
    return None


def main():
    done = registered()
    names, starts, info = labels()
    base = {}
    func = None
    rows = []
    for ln, l in enumerate(open(ASM, encoding="latin-1"), 1):
        if l.startswith("FUNC_"):
            func = re.match(r"FUNC_\w+", l).group(0)
            base = {}
            continue
        if not l.startswith("\t") or func is None:
            continue
        t = l.strip()
        if not t or t.startswith(";") or t.endswith(":"):
            continue
        t = t.split(";")[0].strip()
        mn = t.split()[0]
        ops = t[len(mn):].strip()
        m = re.match(r"(e[a-ds][xi]),(DATA_\d+)$", ops) if mn == "mov" else None
        if m and m.group(2) in info:
            base[m.group(1)] = m.group(2)
            continue
        mm = MEM.search(t)
        if mm and mn != "lea" and mm.group(1) in base:
            off = int(mm.group(2), 16) if mm.group(2) else 0
            w = width(mn, ops.replace(mm.group(0), " "))
            b = base[mm.group(1)]
            a = info[b][0] + off
            i = bisect.bisect_right(starts, a) - 1
            if w and 0 <= i < len(names):
                tgt = names[i]
                ta, tn, tc, tw = info[tgt]
                sub = a - ta
                rows.append((ln, func, b, off, w, tgt, sub, tn, tc, tw, t))
        # register writes
        if mn == "call":
            for r in ("eax", "ecx", "edx"):
                base.pop(r, None)
        elif mn not in NOWRITE:
            d = ops.split(",")[0].strip()
            d = SUB.get(d, d)
            if d in base:
                del base[d]
            if mn in ("pop",) and d in base:
                del base[d]
            if mn in ("xchg",):
                for r in R32:
                    if re.search(r"\b%s\b" % r, ops):
                        base.pop(r, None)
            if mn in ("mul", "imul", "div", "idiv", "cdq"):
                base.pop("eax", None); base.pop("edx", None)
    todo = []
    for ln, func, b, off, w, tgt, sub, tn, tc, tw, t in rows:
        if ln in done:
            continue
        kind = None
        delta = ""
        if w < 4 and tc == "dword-only" and tn >= 4:
            k = sub % 4
            delta = {(2, 0): 2, (2, 2): -2, (1, 0): 3, (1, 1): 1, (1, 2): -1, (1, 3): -3}.get((w, k), "?")
            kind = "narrow-in-dword"
        elif w == 1 and tc == "word-only":
            delta = 1 if sub % 2 == 0 else -1
            kind = "byte-in-word"
        elif w == 4 and tc in ("byte-only", "word-only"):
            kind = "wide"
        elif w == 2 and tc == "byte-only":
            kind = "wide"
        elif tc == "mixed":
            kind = "mixed-target"
        if kind:
            todo.append((kind, ln, func, b, off, w, tgt, sub, tn, tc, tw, delta, t))
    todo.sort()
    with open(OUT, "w", encoding="utf-8", newline="\n") as o:
        o.write("kind\tline\tfunc\tbase\toff\tw\ttarget\tsub\ttarget_bytes\ttarget_class\ttarget_widths\tdelta\ttext\n")
        for r in todo:
            o.write("%s\t%d\t%s\t%s\t0x%x\t%d\t%s\t%d\t%d\t%s\t%s\t%s\t%s\n" % r)
    import collections
    c = collections.Counter(r[0] for r in todo)
    print("base+offset accesses: %d   candidates: %s" % (len(rows), dict(c)))
    print("wrote", OUT)


if __name__ == "__main__":
    sys.exit(main())
