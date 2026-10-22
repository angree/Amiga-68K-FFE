#!/usr/bin/env python3
"""FIELD ATLAS -- every pointer-based memory access in ffe.asm, grouped into
fields, so mixed-width fields can be reviewed in bulk instead of being found
one at a time by measurement (developer's idea, 2026-10-09).

    py tools\\translator\\scan_field_atlas.py            -> build\\datamap\\field_atlas.tsv
                                                           build\\datamap\\field_atlas_todo.tsv

WHY. On the big-endian 68k a location written at one width and read at
another needs a displacement fix (lo_byte_sites.tsv) or a value swap
(le_read_sites.tsv) at the narrower access. For NAMED labels staticscan.tsv
already shows the widths; for fields reached through a register
(`[ebx+0xb0]`) nothing did, and that is where the bugs keep coming from
(FUNC_001508's orbit radius, 2026-10-09: found by hours of probes, visible
here in seconds).

WHAT IT DOES. Regex pass over ffe.asm (no translator involved):
  * every memory operand `[reg+disp]` with reg not ebp/esp and no symbol;
  * width from the size keyword or the register operand;
  * dword uses are classed ARITH (add/sub/cmp/imul/shift/test/inc/dec/neg,
    or a mov of an immediate) versus COPY (plain mov/push/xchg) -- a field
    that is only ever COPIED as a dword may be a byte aggregate, which wants
    NO flip, while an ARITH dword is certainly a scalar;
  * (function, register) pairs are tagged STRUCT=obj when the function uses
    that register with a displacement that only the 0x152-byte game object
    has (>= 0x60). Everything else is STRUCT=other.

A FIELD is mixed when a dword access at D coexists with a word/byte access
in D..D+3 (or a word at D with a byte at D/D+1) in the same STRUCT class.
The todo file lists every narrow access of a mixed field that is NOT already
in le_read_sites.tsv / lo_byte_sites.tsv, with the displacement delta a
SCALAR would need:
    word  in dword: low word  D   -> +2,   high word D+2 -> -2
    byte  in dword: D -> +3, D+1 -> +1, D+2 -> -1, D+3 -> -3
    byte  in word : D -> +1, D+1 -> -1

IT IS A CANDIDATE LIST, NOT A FIX LIST. The same displacement exists in
different structs, and a field written AND read narrow at the same offsets
(with the dword access being an unrelated copy) is correct as it stands.
Each row needs its writer looked at before it is registered -- exactly the
PROVEN_BY_CALLERS discipline. The columns `dword_arith` / `dword_copy` and
the per-field site counts are there to make that review fast.
"""
import collections
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
ASM = os.path.join(ROOT, "ref", "jjffe", "ffe.asm")
OUT = os.path.join(ROOT, "build", "datamap")
R32 = set("eax ebx ecx edx esi edi ebp esp".split())
R16 = set("ax bx cx dx si di bp sp".split())
R8 = set("al bl cl dl ah bh ch dh".split())
ARITH = set("add sub cmp imul mul idiv div shl shr sar sal test inc dec neg not "
            "and or xor adc sbb rol ror rcl rcr".split())
MEM = re.compile(r"\[(e[a-ds][xi])([+-]0x[0-9a-f]+)?\]")


def registered():
    got = set()
    for name in ("le_read_sites.tsv", "lo_byte_sites.tsv"):
        p = os.path.join(ROOT, "tools", "datamap", name)
        for l in open(p, encoding="utf-8", errors="replace"):
            m = re.match(r"(\d+)\t", l)
            if m:
                got.add(int(m.group(1)))
    return got


def parse():
    """Yield (func, line, text, reg, disp, width, is_arith)."""
    func = None
    for ln, l in enumerate(open(ASM, encoding="latin-1"), 1):
        if l.startswith("FUNC_"):
            m = re.match(r"(FUNC_\d+|FUNC_\w+)", l)
            func = m.group(1)
            continue
        if not l.startswith("\t") or func is None:
            continue
        t = l.strip()
        if not t or t.startswith(";"):
            continue
        mn = t.split()[0]
        if mn == "lea":
            continue
        m = MEM.search(t)
        if not m:
            continue
        disp = int(m.group(2), 16) if m.group(2) else 0
        rest = t[len(mn):].replace(m.group(0), " ")
        w = None
        if re.search(r"\bdword\b", rest):
            w = 4
        elif re.search(r"\bword\b", rest):
            w = 2
        elif re.search(r"\bbyte\b(?! [+-])", rest):
            w = 1
        else:
            ops = [o for o in re.split(r"[ ,]+", rest) if o]
            if any(o in R8 for o in ops):
                w = 1
            elif any(o in R16 for o in ops):
                w = 2
            elif any(o in R32 for o in ops):
                w = 4
            elif mn in ("push", "pop", "call", "jmp"):
                w = 4
        if w is None:
            continue
        if mn in ("movsx", "movzx"):
            # the size keyword names the SOURCE; already handled above
            pass
        arith = mn in ARITH or (mn == "mov" and re.search(r",\s*(0x[0-9a-f]+|byte [+-]0x)", t) is not None
                                and t.index(m.group(0)) < t.index(","))
        yield func, ln, t, m.group(1), disp, w, arith


def main():
    os.makedirs(OUT, exist_ok=True)
    reg_done = registered()
    rows = list(parse())
    # (func, reg) -> obj?  the 0x152 object is the only struct with large displacements
    maxd = collections.defaultdict(int)
    seen = collections.defaultdict(set)
    for f, ln, t, r, d, w, a in rows:
        maxd[(f, r)] = max(maxd[(f, r)], d)
        seen[(f, r)].add(d)
    OBJ_SIG = {0x86, 0x87, 0x56, 0x57, 0x118, 0x14c, 0xc6, 0xa0}   # game object (0x152 bytes)
    CTX_SIG = {0x134, 0x138, 0x13c, 0x148, 0x154, 0x144, 0x140}    # render context (FUNC_001683)
    def cls(f, r):
        if not (0x60 <= maxd[(f, r)] <= 0x15c):
            return "other"
        o, c = bool(seen[(f, r)] & OBJ_SIG), bool(seen[(f, r)] & CTX_SIG)
        if o and not c:
            return "obj"
        if c and not o:
            return "ctx"
        return "big"        # large struct, not identified (or both signatures)

    fields = collections.defaultdict(lambda: collections.defaultdict(list))  # (cls, disp) -> width -> rows
    for row in rows:
        f, ln, t, r, d, w, a = row
        fields[(cls(f, r), d)][w].append(row)

    with open(os.path.join(OUT, "field_atlas.tsv"), "w", encoding="utf-8", newline="\n") as o:
        o.write("struct\tdisp\tdword\tdword_arith\tword\tbyte\n")
        for (c, d) in sorted(fields, key=lambda k: (k[0], k[1])):
            fw = fields[(c, d)]
            o.write("%s\t0x%x\t%d\t%d\t%d\t%d\n" % (
                c, d, len(fw[4]), sum(1 for x in fw[4] if x[6]), len(fw[2]), len(fw[1])))

    todo = []
    for (c, d), fw in fields.items():
        if d < 0:
            continue
        # byte/word inside a dword at D
        if fw[4]:
            n4 = len(fw[4]); a4 = sum(1 for x in fw[4] if x[6])
            for dd, delta2, delta1 in ((d, 2, 3), (d + 1, None, 1), (d + 2, -2, -1), (d + 3, None, -3)):
                sub = fields.get((c, dd))
                if not sub:
                    continue
                if dd != d and sub[4]:
                    continue        # its own dword field starts here: overlapping layouts, not a sub-field
                if delta2 is not None:
                    for x in sub[2]:
                        todo.append((c, d, 4, n4, a4, dd, 2, delta2, x))
                for x in sub[1]:
                    todo.append((c, d, 4, n4, a4, dd, 1, delta1, x))
        # byte inside a word at D (only where no dword covers it)
        if fw[2] and not any(fields.get((c, d - k), {}).get(4) for k in range(0, 4) if (c, d - k) in fields):
            n2 = len(fw[2]); a2 = sum(1 for x in fw[2] if x[6])
            for dd, delta1 in ((d, 1), (d + 1, -1)):
                sub = fields.get((c, dd))
                if not sub or (dd != d and sub[2]):
                    continue
                for x in sub[1]:
                    todo.append((c, d, 2, n2, a2, dd, 1, delta1, x))

    todo = [t for t in todo if t[8][1] not in reg_done]
    todo.sort(key=lambda t: (t[0], t[1], t[5], t[8][1]))
    with open(os.path.join(OUT, "field_atlas_todo.tsv"), "w", encoding="utf-8", newline="\n") as o:
        o.write("struct\tfield\tfield_w\twide_sites\twide_arith\tat\tw\tdelta\tline\tfunc\ttext\n")
        for c, d, fwid, n, a, dd, w, delta, x in todo:
            o.write("%s\t0x%x\t%d\t%d\t%d\t0x%x\t%d\t%+d\t%d\t%s\t%s\n" % (
                c, d, fwid, n, a, dd, w, delta, x[1], x[0], x[2]))

    by = collections.Counter((t[0], t[2]) for t in todo)
    print("pointer accesses: %d   fields: %d   already-registered lines: %d" % (len(rows), len(fields), len(reg_done)))
    for k in sorted(by):
        print("  todo struct=%-5s inside a %d-byte field: %d narrow sites" % (k[0], k[1], by[k]))
    objf = sorted(set((t[1]) for t in todo if t[0] == "obj" and t[2] == 4))
    print("obj dword fields with ARITH dword use and unregistered narrow access (%d): %s" % (
        len(objf), " ".join("0x%x" % d for d in objf)))
    print("wrote", os.path.join(OUT, "field_atlas.tsv"), "and field_atlas_todo.tsv")


if __name__ == "__main__":
    sys.exit(main())
