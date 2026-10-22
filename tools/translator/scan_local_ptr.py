#!/usr/bin/env python3
"""LOCAL-POINTER SCAN: `lea reg,[ebp-N]` followed by a NARROW access through
that register (`mov ax,[reg]`), where the same register is also used for a
dword access. frame.py flips sub-word accesses written as [ebp-N]; it cannot
see the same slot reached through a pointer. FUNC_000585 (rotation damping)
was eight of these (2026-10-09).

    py tools\translator\scan_local_ptr.py   -> build\datamap\local_ptr_todo.tsv
Candidates only; lines already registered are dropped.
"""
import os, re, sys
ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
ASM = os.path.join(ROOT, "ref", "jjffe", "ffe.asm")
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from scan_base_offsets import registered, width, MEM, SUB, NOWRITE, R32

def main():
    done = registered()
    func, base, out, wide = None, {}, [], {}
    pend = []
    def flush():
        for (ln, f, r, slot, off, w, t) in pend:
            if wide.get((f, slot)) and ln not in done:
                out.append((ln, f, slot, off, w, {(2, 0): 2, (2, 2): -2, (1, 0): 3, (1, 1): 1, (1, 2): -1, (1, 3): -3}.get((w, off % 4), "?"), t))
    for ln, l in enumerate(open(ASM, encoding="latin-1"), 1):
        if l.startswith("FUNC_"):
            flush(); pend = []; wide = {}
            func = re.match(r"FUNC_\w+", l).group(0); base = {}
            continue
        if not l.startswith("\t") or func is None:
            continue
        t = l.strip().split(";")[0].strip()
        if not t or t.endswith(":"):
            continue
        mn = t.split()[0]; ops = t[len(mn):].strip()
        m = re.match(r"(e[a-ds][xi]),\[ebp([+-]0x[0-9a-f]+)\]$", ops) if mn == "lea" else None
        if m:
            base[m.group(1)] = int(m.group(2), 16)
            continue
        m2 = re.search(r"dword \[ebp([+-]0x[0-9a-f]+)\]|\[ebp([+-]0x[0-9a-f]+)\],e[a-ds][xi]$|e[a-ds][xi],\[ebp([+-]0x[0-9a-f]+)\]$", ops)
        if m2 and mn != "lea":
            wide[(func, int(next(g for g in m2.groups() if g), 16))] = True
        mm = MEM.search(t)
        if mm and mn != "lea" and mm.group(1) in base:
            off = int(mm.group(2), 16) if mm.group(2) else 0
            w = width(mn, ops.replace(mm.group(0), " "))
            slot = base[mm.group(1)] + (off & ~3)
            if w == 4:
                wide[(func, slot)] = True
            elif w:
                pend.append((ln, func, mm.group(1), slot, off, w, t))
        if mn == "call":
            for r in ("eax", "ecx", "edx"):
                base.pop(r, None)
        elif mn not in NOWRITE:
            d = ops.split(",")[0].strip(); d = SUB.get(d, d)
            base.pop(d, None)
    flush()
    p = os.path.join(ROOT, "build", "datamap", "local_ptr_todo.tsv")
    with open(p, "w", encoding="utf-8", newline="\n") as o:
        o.write("line\tfunc\tslot\toff\tw\tdelta\ttext\n")
        for r in out:
            o.write("%d\t%s\t%d\t%d\t%d\t%s\t%s\n" % r)
    print(len(out), "candidates ->", p)

if __name__ == "__main__":
    main()
