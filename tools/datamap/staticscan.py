#!/usr/bin/env python3
"""Infer the access width of every static data label, from the code that reads it.

WHY THIS EXISTS -- the project's largest correctness risk, stated concretely:

    ffedat.asm:24820   db 0x40, 0x1, 0x0, 0x0

On x86 that is the 32-bit number 320. Copied byte-for-byte to a big-endian 68k it
becomes 1073807360. The disassembler emitted bytes because it could not tell a
number from a blob, and 1.46 MB of the game's data is in that state. Every one of
those bytes has to be classified before it can be converted, and a single
misclassified word is a bug that surfaces hours into a game.

WHAT THIS DOES: reads every reference to a DATA_ label in ffe.asm and records the
WIDTH the instruction used -- from an explicit `byte`/`word`/`dword` keyword when
there is one, otherwise from the size of the register operand. It then classifies
each label:

    byte-only    only ever touched 8 bits at a time -> endian-neutral, no work
    word-only    16-bit -> needs byte-swapping in pairs
    dword-only   32-bit -> needs 4-byte swapping
    mixed        touched at more than one width -> must be looked at by hand
    addr-only    only ever had its ADDRESS taken (lea / push) -> the real access
                 happens through a computed pointer and this scan cannot see it
    unreferenced not named anywhere in the code -> reached only via pointer
                 arithmetic, or dead

The last two categories are the residual risk the plan's M2 gate measures. This
script produces the static half of the answer; the runtime half (a memory-access
log from an instrumented PC run) is a separate tool.

Output: a summary to stdout and a full TSV to build\\datamap\\staticscan.tsv.
"""
import os, re, sys, collections

ROOT = r"I:\GITHUB\Amiga_Frontier_2_First_Encouters"
REF  = os.path.join(ROOT, "ref", "jjffe")
OUT  = os.path.join(ROOT, "build", "datamap")

CODE = os.path.join(REF, "ffe.asm")
DATA = [os.path.join(REF, "ffedat.asm"), os.path.join(REF, "ffebmp.asm")]

R32 = {"eax","ebx","ecx","edx","esi","edi","ebp","esp"}
R16 = {"ax","bx","cx","dx","si","di","bp","sp"}
R8  = {"al","bl","cl","dl","ah","bh","ch","dh"}

# DATA_\w+, not DATA_\d+: ffedat also names labels like DATA_KeyAccumTable,
# and a regex keyed on digits silently drops them from the scan -- which left
# the key accumulator table unclassified and therefore unconverted.
label_re = re.compile(r"^(DATA_\w+):")
# Capture the WHOLE name, not just the digits: the definition side keys on
# "DATA_007773" and a reference side keyed on "007773" silently matches nothing,
# which reports every label as unreferenced and looks like a real finding.
ref_re   = re.compile(r"\b(DATA_\w+)\b")
size_re  = re.compile(r"\b(byte|word|dword|qword)\b")


def in_brackets(s, pos):
    """Is position `pos` inside a [...] memory operand? A DATA_ name OUTSIDE
    brackets is the label's ADDRESS (`mov ebx,DATA_008804`, `push dword X`),
    not an access -- counting it as a dword access made every pointer-carried
    struct look directly dword-read and poisoned the classification."""
    return s.rfind("[", 0, pos) > s.rfind("]", 0, pos)


def data_label_sizes():
    """Byte extent of every DATA_ label, plus which region it lives in.

    The region matters more than the total. ffebmp.asm is mostly bitmap pixel
    data -- byte arrays, endian-neutral, no work at all -- but it also contains
    the 3D model bytecode, which is 16-BIT and therefore does need swapping.
    That region is delimited by JJ's own comments: "MODEL DATA START" at
    ffebmp.asm:10542, running to the ship object table at 41814. Lumping the two
    together turns a mostly-safe megabyte into an alarming unknown.
    """
    sizes, order, region = {}, [], {}
    for path in DATA:
        base = os.path.basename(path)
        cur, count = None, 0
        model_from = model_to = None
        if base == "ffebmp.asm":
            for ln, line in enumerate(open(path, errors="replace"), 1):
                if "MODEL DATA START" in line:
                    model_from = ln
                if "Ship object table" in line:
                    model_to = ln
                    break
        for lineno, line in enumerate(open(path, errors="replace"), 1):
            s = line.split(";")[0].strip()
            if not s:
                continue
            m = label_re.match(s)
            if m:
                if cur is not None:
                    sizes[cur] = count
                cur = m.group(1); count = 0; order.append(cur)
                if base == "ffedat.asm":
                    region[cur] = "ffedat (game structures)"
                elif model_from and model_to and model_from <= lineno <= model_to:
                    region[cur] = "ffebmp MODEL bytecode (16-bit!)"
                else:
                    region[cur] = "ffebmp bitmaps/tables"
                s = s.split(":", 1)[1].strip()
                if not s:
                    continue
            if cur is None:
                continue
            p = s.split(None, 1)
            d = p[0].lower()
            arg = p[1] if len(p) > 1 else ""
            n = len([x for x in arg.split(",") if x.strip()])
            if   d == "db":   count += n
            elif d == "dw":   count += 2 * n
            elif d == "dd":   count += 4 * n
            elif d == "times":
                m2 = re.match(r"^(\d+)\s+(db|dw|dd)\b", arg)
                if m2:
                    count += int(m2.group(1)) * {"db": 1, "dw": 2,
                                                 "dd": 4}[m2.group(2)]
            elif d == "resb":
                try: count += int(arg.strip(), 0)
                except ValueError: pass
            elif d == "resw":
                try: count += 2 * int(arg.strip(), 0)
                except ValueError: pass
            elif d == "resd":
                try: count += 4 * int(arg.strip(), 0)
                except ValueError: pass
        if cur is not None:
            sizes[cur] = count
    return sizes, order, region


def width_of(line, s):
    """Width in bytes this instruction touches, or 0 for address-taken only."""
    low = s.lower()
    op = low.split()[0] if low.split() else ""
    if op == "lea":
        return 0                       # address computation, never a load
    m = size_re.search(low)
    if m:
        return {"byte": 1, "word": 2, "dword": 4, "qword": 8}[m.group(1)]
    if op in ("movsx", "movzx"):
        # `movsx eax, byte [X]` has the keyword; without one the source is
        # narrower than the destination but unstated -- flag it rather than guess
        return -1
    # otherwise take it from the widest register operand present -- OUTSIDE the
    # brackets. A base or index register inside [..] says nothing about the
    # access width: `mov al,[eax+DATA_007688]` is a BYTE read, and counting the
    # eax made the scan classify the 256-byte scancode->ASCII table as
    # dword-only, which the converter then byte-swapped in groups of four. That
    # turned the splash's 'y' into 'u' at runtime. Caught 2026-07-29.
    low = re.sub(r"\[[^\]]*\]", "[]", low)
    toks = set(re.findall(r"\b([a-z]{2,3})\b", low))
    if toks & R32: return 4
    if toks & R16: return 2
    if toks & R8:  return 1
    if op == "push": return 4
    return -1


def main():
    os.makedirs(OUT, exist_ok=True)
    sizes, order, region = data_label_sizes()
    print(f"data labels defined: {len(sizes)}")

    widths  = collections.defaultdict(collections.Counter)
    addronly = collections.Counter()
    unknown  = collections.Counter()
    examples = collections.defaultdict(list)

    for ln, line in enumerate(open(CODE, errors="replace"), 1):
        s = line.split(";")[0].strip()
        if "DATA_" not in s:
            continue
        if label_re.match(s):
            continue
        for m in ref_re.finditer(s):
            lab = m.group(1)
            w = 0 if not in_brackets(s, m.start()) else width_of(ln, s)
            if w == 0:
                addronly[lab] += 1
            elif w < 0:
                unknown[lab] += 1
                if len(examples[lab]) < 2:
                    examples[lab].append(f"{ln}: {s}")
            else:
                widths[lab][w] += 1
                if len(examples[lab]) < 2:
                    examples[lab].append(f"{ln}: {s}")

    klass = {}
    for lab in sizes:
        w = widths.get(lab)
        if w:
            ws = set(w)
            if len(ws) == 1:
                klass[lab] = {1: "byte-only", 2: "word-only", 4: "dword-only",
                              8: "qword-only"}[ws.pop()]
            else:
                klass[lab] = "mixed"
        elif addronly.get(lab):
            klass[lab] = "addr-only"
        elif unknown.get(lab):
            klass[lab] = "unknown-width"
        else:
            klass[lab] = "unreferenced"

    tally = collections.Counter(klass.values())
    bytes_by = collections.Counter()
    for lab, k in klass.items():
        bytes_by[k] += sizes.get(lab, 0)

    total_bytes = sum(sizes.values())
    print()
    print(f"{'class':16s} {'labels':>8s} {'bytes':>12s}   {'% of data':>9s}")
    for k in ("byte-only", "word-only", "dword-only", "qword-only", "mixed",
              "addr-only", "unknown-width", "unreferenced"):
        if tally[k] or bytes_by[k]:
            print(f"{k:16s} {tally[k]:8d} {bytes_by[k]:12d}   "
                  f"{100.0*bytes_by[k]/total_bytes:8.2f}%")
    print(f"{'TOTAL':16s} {len(sizes):8d} {total_bytes:12d}")

    safe = bytes_by["byte-only"]
    typed = bytes_by["word-only"] + bytes_by["dword-only"]
    risky = bytes_by["mixed"] + bytes_by["unknown-width"]
    blind = bytes_by["addr-only"] + bytes_by["unreferenced"]
    print()
    print(f"endian-neutral (byte-only)     : {safe:9d} B  {100.0*safe/total_bytes:5.1f}%")
    print(f"typed by this scan             : {typed:9d} B  {100.0*typed/total_bytes:5.1f}%")
    print(f"needs a human                  : {risky:9d} B  {100.0*risky/total_bytes:5.1f}%")
    print(f"INVISIBLE to static analysis   : {blind:9d} B  {100.0*blind/total_bytes:5.1f}%")
    print("  (address-taken or never named: reached through computed pointers,")
    print("   so only a runtime access log can classify these)")

    # The breakdown that actually guides the work: where the unclassified bytes
    # sit. A megabyte of unreferenced bitmap pixels is not the same problem as a
    # kilobyte of unreferenced game structure.
    print()
    print("=== unclassified bytes, by region ===")
    per = collections.defaultdict(collections.Counter)
    for lab, k in klass.items():
        per[region.get(lab, "?")][k] += sizes.get(lab, 0)
    for reg in sorted(per):
        c = per[reg]
        tot = sum(c.values())
        invis = c["addr-only"] + c["unreferenced"]
        print(f"  {reg}")
        print(f"      total {tot:9d} B | typed {c['byte-only']+c['word-only']+c['dword-only']:8d}"
              f" | mixed {c['mixed']:7d} | invisible {invis:9d}"
              f" ({100.0*invis/tot if tot else 0:5.1f}%)")

    tsv = os.path.join(OUT, "staticscan.tsv")
    with open(tsv, "w") as f:
        f.write("label\tbytes\tclass\twidths\taddr\texample\n")
        for lab in order:
            w = widths.get(lab, {})
            ws = ",".join(f"{k}:{v}" for k, v in sorted(w.items()))
            ex = examples.get(lab, [""])[0].replace("\t", " ")
            f.write(f"{lab}\t{sizes.get(lab,0)}\t{klass.get(lab,'?')}\t{ws}"
                    f"\t{addronly.get(lab,0)}\t{ex}\n")
    print(f"\nfull table -> {tsv}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
