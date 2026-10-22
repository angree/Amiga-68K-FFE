#!/usr/bin/env python3
"""Pair the model-request logs from the two machines and say where they diverge.

    python tools\\translator\\cmp_model1538.py [-n N]

Reads the reference's `build\\m0-pc\\run\\pc_objpos.log` and our
`winuae\\work\\ffe_enter.log`, both of which dump their rings at every
FUNC_001876 entry, and reports for scene render N (default 1):

  * how many requested model numbers fall outside DATA_004681's 466 entries,
    on each machine -- the headline number, because a request of 0xB1000000
    makes `mov eax,[eax*4+DATA_004681]` compute a nonsense address;
  * the return-address histogram, so the CALL SITE that emits the bad values
    is named rather than inferred;
  * the model sets, and the first position at which the two sequences differ.

Two things this deliberately does NOT do:

  * compare raw totals across runs. Measured 2026-08-05: two reference runs
    gave total=455 and total=389 at n=1 with an identical last-eight ring,
    because the count depends on how many intro frames elapse before the key
    is posted. Only sequence SHAPE and fractions are comparable.
  * assume a line is complete. Every dump carries `kept=` and `cap=`; a
    truncated sequence is reported, never silently used.
"""

import io
import os
import re
import sys
from collections import Counter

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
REF_LOG = os.path.join(ROOT, "build", "m0-pc", "run", "pc_objpos.log")
OUR_LOG = os.path.join(ROOT, "winuae", "work", "ffe_enter.log")
TABLE_ENTRIES = 466          # DATA_004681, ffebmp.asm:41814, 1864 bytes


def read_seq(path, prefix, n):
    """Return (numbers, capped) for scene render n, or (None, None)."""
    try:
        txt = io.open(path, encoding="utf-8", errors="replace").read()
    except IOError:
        return None, None
    pat = re.compile(prefix + r" n=%d p=(\d+) total=(\d+) kept=(\d+) cap=(\d+) ([-0-9,]*)" % n)
    chunks, capped = {}, False
    for m in pat.finditer(txt):
        chunks[int(m.group(1))] = [int(x) for x in m.group(5).split(",") if x.strip()]
        capped = capped or m.group(4) != "0"
    if not chunks:
        return None, None
    return [v for p in sorted(chunks) for v in chunks[p]], capped


def read_ret(path, prefix, n):
    """Return {retaddr: (total, out_of_range)} for scene render n."""
    try:
        txt = io.open(path, encoding="utf-8", errors="replace").read()
    except IOError:
        return {}
    out = {}
    for m in re.finditer(prefix + r" n=%d ([^\r\n]*)" % n, txt):
        for tok in m.group(1).split():
            bits = tok.split(":")
            if len(bits) == 3:
                try:
                    out[bits[0]] = (int(bits[1]), int(bits[2]))
                except ValueError:
                    pass
    return out


def bad(v):
    return v < 0 or v >= TABLE_ENTRIES


def report(name, seq, capped):
    if seq is None:
        print("  %-10s BRAK DANYCH -- czy ten bieg zostal wykonany?" % name)
        return
    n_bad = sum(1 for v in seq if bad(v))
    print("  %-10s %4d zadan, %4d poza zakresem (%.1f%%)%s"
          % (name, len(seq), n_bad, 100.0 * n_bad / max(1, len(seq)),
             "   [UWAGA: cap=1, sekwencja obcieta]" if capped else ""))
    if n_bad:
        print("             zle wartosci:", ", ".join(
            "0x%08X x%d" % (v & 0xFFFFFFFF, c)
            for v, c in Counter(v for v in seq if bad(v)).most_common(6)))


def main():
    n = 1
    if "-n" in sys.argv:
        n = int(sys.argv[sys.argv.index("-n") + 1])

    ref, ref_cap = read_seq(REF_LOG, "PC1538SEQ", n)
    our, our_cap = read_seq(OUR_LOG, "MODEL1538SEQ", n)

    print("=== render sceny n=%d ===" % n)
    print()
    print("ZADANIA MODELI POZA ZAKRESEM TABLICY (%d wpisow):" % TABLE_ENTRIES)
    report("referencja", ref, ref_cap)
    report("my", our, our_cap)
    if ref is None or our is None:
        return 1

    print()
    print("HISTOGRAM ADRESOW POWROTU  (adres: wszystkie / poza zakresem):")
    for label, hist in (("referencja", read_ret(REF_LOG, "PC1538RET", n)),
                        ("my", read_ret(OUR_LOG, "MODEL1538RET", n))):
        if not hist:
            print("  %-10s brak linii RET -- sonda adresu powrotu nie byla w tym biegu" % label)
            continue
        print("  %s:" % label)
        for addr, (tot, nbad) in sorted(hist.items(), key=lambda kv: -kv[1][0]):
            flag = "   <== ZRODLO ZLYCH WARTOSCI" if nbad else ""
            print("     %-12s %5d / %5d%s" % (addr, tot, nbad, flag))

    gr = set(v for v in ref if not bad(v))
    go = set(v for v in our if not bad(v))
    print()
    print("ZBIORY MODELI: ref=%d  my=%d  wspolne=%d" % (len(gr), len(go), len(gr & go)))
    print("  tylko w referencji:", sorted(gr - go))
    print("  tylko u nas       :", sorted(go - gr))

    d = next((i for i in range(min(len(ref), len(our))) if ref[i] != our[i]), None)
    print()
    if d is None:
        print("PIERWSZA ROZNICA: brak -- wspolny prefiks %d pozycji jest identyczny."
              % min(len(ref), len(our)))
    else:
        lo = max(0, d - 8)
        print("PIERWSZA ROZNICA na pozycji %d:" % d)
        print("  ref:", ref[lo:d + 8])
        print("  my :", our[lo:d + 8])
    return 0


if __name__ == "__main__":
    sys.exit(main())
