# Count `movea.l dN,a0` that reload a0 with what it already holds, within
# straight-line code (any label, branch target, call or write to a0/dN
# forgets). A measurement for deciding whether a peephole pass is worth it.
import os
import re
import sys

D = sys.argv[1]
only = set(sys.argv[2:])
tot = red = moveas = 0
for fn in sorted(os.listdir(D)):
    if not fn.endswith(".s") or (only and fn[:-2] not in only):
        continue
    a0 = None
    for line in open(os.path.join(D, fn), encoding="latin-1"):
        if not line.strip() or line.lstrip().startswith(";"):
            continue
        if not line.startswith("\t"):          # a label
            a0 = None
            continue
        parts = line.split(None, 1)
        op = parts[0]
        args = parts[1].split(";")[0].strip() if len(parts) > 1 else ""
        if op in ("xdef", "xref", "section", "even", "dc.l", "dc.w", "dc.b", "cnop"):
            continue
        tot += 1
        m = re.match(r"movea\.l\s+(d[0-7]),a0$", op + " " + args)
        if m:
            moveas += 1
            if a0 == m.group(1):
                red += 1
            a0 = m.group(1)
            continue
        if op.startswith(("jsr", "bsr", "jmp", "b", "db", "rts", "rte")) and not op.startswith(("bset", "bclr", "bchg", "btst", "bfext", "bfins", "bftst")):
            a0 = None
            continue
        dst = args.split(",")[-1] if "," in args else args
        if a0 is not None and (dst.strip() in (a0, "a0") or "(a0)+" in args or "-(a0)" in args
                               or op.startswith(("movem", "exg", "swap", "ext", "clr", "neg", "not", "addq", "subq", "lsl", "lsr", "asl", "asr", "rol", "ror", "roxl", "roxr", "s")) and a0 in args):
            a0 = None
print("instructions %d, movea dN,a0 %d (%.1f%%), redundant %d (%.1f%% of all)" % (
    tot, moveas, 100.0 * moveas / tot, red, 100.0 * red / tot))
