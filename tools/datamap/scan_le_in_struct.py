#!/usr/bin/env python3
"""LE LABELS INSIDE A POINTER-WRITTEN STRUCT -- candidates for the
DATA_008971 / DATA_008984 bug class (2026-10-15).

    py tools\\datamap\\scan_le_in_struct.py  -> build\\datamap\\le_in_struct_todo.tsv

A label stored 'le' (raw x86 order) is consistent only while EVERY access to
its bytes swaps -- i.e. goes by name through the MOV rules. When an EARLIER
label's address is handed out (push dword DATA_x / mov reg,DATA_x / lea), the
callee writes the whole struct through the pointer in NATIVE order, and a
later named wide read of the le member reverses it. Typical trigger: a struct
member whose label size is not a multiple of 4 (DATA_008971: 5 bytes), so the
converter downgraded a dword-only label to le.

For each le label with a named 2/4-byte access, report the nearest earlier
address-taken label within WINDOW bytes (same file, memory order). A
CANDIDATE LIST: read the writer before registering anything.
"""
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
ASM = os.path.join(ROOT, "ref", "jjffe", "ffe.asm")
STOR = os.path.join(ROOT, "build", "data-be", "ffedat_be.storage.tsv")
OUT = os.path.join(ROOT, "build", "datamap", "le_in_struct_todo.tsv")
WINDOW = 0x200

def main():
    labs, addr, info = [], 0, {}
    for l in open(STOR, encoding="utf-8"):
        p = l.rstrip("\n").split("\t")
        if len(p) < 3 or p[0] == "label":
            continue
        n = int(p[2])
        info[p[0]] = (addr, n, p[1])
        labs.append(p[0])
        addr += n
    named = {}      # label -> {width: [lines]}
    taken = {}      # label -> first line taking its address
    W = {"dword": 4, "word": 2, "byte": 1}
    R32 = set("eax ebx ecx edx esi edi ebp esp".split())
    R16 = set("ax bx cx dx si di bp".split())
    for ln, l in enumerate(open(ASM, encoding="latin-1"), 1):
        t = l.split(";")[0].strip()
        if not t or t.endswith(":"):
            continue
        for m in re.finditer(r"\[(?:(dword|word|byte)\s+)?(DATA_\d+)([+-]0x[0-9a-f]+)?\]", t):
            pass
        m = re.search(r"(?:(dword|word|byte)\s+)?\[(DATA_\d+)\]", t)
        if m:
            lab = m.group(2)
            w = W.get(m.group(1))
            if w is None:
                ops = re.split(r"[ ,]+", t)
                w = 4 if any(o in R32 for o in ops) else 2 if any(o in R16 for o in ops) else 1
            if t.startswith("movsx") or t.startswith("movzx"):
                w = 2 if "word" in t else 1
            named.setdefault(lab, {}).setdefault(w, []).append(ln)
        else:
            m2 = re.search(r"(?:push dword |mov e[a-ds][xi],|lea e[a-ds][xi],\[)(DATA_\d+)\b", t)
            if m2:
                taken.setdefault(m2.group(1), ln)
    rows = []
    starts = [info[x][0] for x in labs]
    for i, lab in enumerate(labs):
        a, n, k = info[lab]
        if k != "le" or lab not in named:
            continue
        wide = {w: v for w, v in named[lab].items() if w > 1}
        if not wide:
            continue
        j = i - 1
        hit = None
        while j >= 0 and a - info[labs[j]][0] <= WINDOW:
            if labs[j] in taken:
                hit = labs[j]
                break
            j -= 1
        if hit:
            rows.append((a - info[hit][0], lab, n, hit, taken[hit],
                         ",".join("%d:%d" % (w, len(v)) for w, v in sorted(wide.items())),
                         ",".join(str(x) for v in wide.values() for x in v[:4])))
    rows.sort()
    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    with open(OUT, "w", encoding="utf-8", newline="\n") as o:
        o.write("dist\tlabel\tsize\tbase_taken\tbase_line\tnamed_wide\tlines\n")
        for r in rows:
            o.write("0x%x\t%s\t%d\t%s\t%d\t%s\t%s\n" % r)
    print("candidates:", len(rows), "->", OUT)

if __name__ == "__main__":
    sys.exit(main())
