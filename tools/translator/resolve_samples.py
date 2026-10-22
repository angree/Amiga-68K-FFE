# SPDX-License-Identifier: MIT
# FIRST-LIGHT DEBUG: turn the VBlank PC sampler's raw dump into a symbol
# histogram -- i.e. name the function a silent run is spinning in.
#
# Inputs, all produced by an ordinary harness run:
#   winuae\work\ffe_samples.log   the ring of interrupted PCs (FfeSampDump)
#   winuae\work\ffe_enter.log     its first line is
#                                 "MAP FUNC_000286=<runtime> ..." -- three
#                                 known symbols, which pin the load base
#                                 exactly; guessing it is not necessary.
#   build\amiga-out\ffe_game.symmap   ld -Map output: "0xADDR  symbol" lines,
#                                 link-time addresses relative to hunk 0.
#
# The base is (runtime address of a known symbol) - (its link address); all
# three anchors must agree or the script says so rather than reporting a
# plausible-looking wrong answer.
#
# Debug only -- delete with the rest of the rig.

import io
import os
import re
import sys
from collections import Counter

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
WORK = os.path.join(ROOT, "winuae", "work")
SYMMAP = os.path.join(ROOT, "build", "amiga-out", "ffe_game.symmap")
SAMPLES = os.path.join(WORK, "ffe_samples.log")
ENTER = os.path.join(WORK, "ffe_enter.log")


def load_symbols(path):
    """[(link_addr, name)] sorted, from an ld -Map file."""
    syms = []
    pat = re.compile(r"^\s+0x([0-9a-fA-F]{8,16})\s+(\S+)\s*$")
    for line in io.open(path, encoding="latin-1", errors="replace"):
        m = pat.match(line.rstrip("\n"))
        if not m:
            continue
        name = m.group(2)
        # ld also lists section/fill rows and absolute assignments; keep only
        # things that look like symbols
        if name.startswith(".") or name.startswith("0x"):
            continue
        syms.append((int(m.group(1), 16), name))
    syms.sort()
    # collapse duplicates at the same address (the bare + wrapper entry points
    # of a translated function share one address in some builds)
    out = []
    for a, n in syms:
        if out and out[-1][0] == a:
            continue
        out.append((a, n))
    return out


def find_base(enter_path, syms):
    """Runtime load base, from the MAP line's three anchors."""
    byname = {}
    for a, n in syms:
        byname.setdefault(n, a)
    if not os.path.exists(enter_path):
        return None, "no ffe_enter.log -- cannot pin the load base"
    first = io.open(enter_path, encoding="latin-1", errors="replace").readline()
    if not first.startswith("MAP "):
        return None, "ffe_enter.log has no MAP line (trace not armed?)"
    bases = []
    for tok in first.split()[1:]:
        if "=" not in tok:
            continue
        nm, val = tok.split("=", 1)
        if nm not in byname:
            return None, "MAP names %s, which the symmap does not" % nm
        bases.append((int(val, 16) - byname[nm], nm))
    if not bases:
        return None, "MAP line carried no anchors"
    if len({b for b, _ in bases}) != 1:
        return None, "anchors disagree: %r" % (bases,)
    return bases[0][0], None


def resolve(pc, syms):
    """(name, offset) for a runtime-relative address, or (None, pc)."""
    lo, hi = 0, len(syms) - 1
    if not syms or pc < syms[0][0]:
        return None, pc
    while lo < hi:
        mid = (lo + hi + 1) // 2
        if syms[mid][0] <= pc:
            lo = mid
        else:
            hi = mid - 1
    return syms[lo][1], pc - syms[lo][0]


def addr_mode(argv):
    """--addr <hex> ... : resolve bare runtime addresses (the RET census line
    in ffe_game.log, or any ret= field in ffe_enter.log) to symbol+offset."""
    syms = load_symbols(SYMMAP)
    base, err = find_base(ENTER, syms)
    if err:
        print("BASE UNKNOWN: %s" % err)
        return 3
    print("load base: 0x%08x" % base)
    for tok in argv:
        # accept "10143d94" and "10143d94=1234" (the census's addr=count form)
        cnt = None
        if "=" in tok:
            tok, cnt = tok.split("=", 1)
        try:
            pc = int(tok, 16)
        except ValueError:
            continue
        nm, off = resolve(pc - base, syms)
        print("  %08x  %-22s +0x%-6x %s" % (pc, nm if nm else "<unknown>", off,
                                            ("n=" + cnt) if cnt else ""))
    return 0


def main():
    if "--addr" in sys.argv:
        i = sys.argv.index("--addr")
        return addr_mode(sys.argv[i + 1:])
    if not os.path.exists(SAMPLES):
        print("NO SAMPLE DUMP: %s does not exist." % SAMPLES)
        print("The sampler writes it only after 3000 in-user-mode VBlank")
        print("samples while armed. If the run was long enough, the spin is")
        print("NOT in user mode -- check that, do not re-run blindly.")
        return 2

    syms = load_symbols(SYMMAP)
    print("symmap: %d symbols from %s" % (len(syms), SYMMAP))
    base, err = find_base(ENTER, syms)
    if err:
        print("BASE UNKNOWN: %s" % err)
        return 3
    print("load base: 0x%08x" % base)

    pcs = []
    for line in io.open(SAMPLES, encoding="latin-1", errors="replace"):
        for tok in re.findall(r"[0-9a-fA-F]{6,8}", line):
            pcs.append(int(tok, 16))
    print("samples: %d" % len(pcs))
    if not pcs:
        return 4

    hist = Counter()
    outside = 0
    for pc in pcs:
        rel = pc - base
        if rel < 0 or rel > 0x400000:
            outside += 1
            hist["<outside the program: 0x%08x>" % (pc & 0xFFFF0000)] += 1
            continue
        nm, off = resolve(rel, syms)
        hist[nm if nm else "<below the first symbol>"] += 1

    print("outside the program image: %d of %d" % (outside, len(pcs)))
    print("")
    print("%-8s %-7s %s" % ("samples", "share", "symbol"))
    for nm, c in hist.most_common(25):
        print("%-8d %-6.1f%% %s" % (c, 100.0 * c / len(pcs), nm))

    # the hottest symbol's offsets, so a spin inside a big function is placed
    top = hist.most_common(1)[0][0]
    offs = Counter()
    for pc in pcs:
        rel = pc - base
        if 0 <= rel <= 0x400000:
            nm, off = resolve(rel, syms)
            if nm == top:
                offs[off] += 1
    if offs:
        print("")
        print("hottest offsets inside %s:" % top)
        for off, c in offs.most_common(12):
            print("   +0x%-6x %d" % (off, c))
    return 0


if __name__ == "__main__":
    sys.exit(main())
