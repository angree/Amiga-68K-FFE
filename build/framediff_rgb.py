#!/usr/bin/env python3
"""Compare two FFEFRM01 dumps by COLOUR, not by palette index.

Why this exists alongside framediff.py: two runs of the SAME PC reference
disagree on 81.8% of the viewport by index and on 0.0% of it by picture. The
dynamic half of the palette (slots 128..255) is allocated in whatever order the
scene happens to request colours, so an identical image comes out as
`153->154 x22382` -- a total mismatch that means nothing. Indices are still the
right unit for the 2D panel, which uses the fixed low half; they are the wrong
unit for the 3D viewport.

    python build\\framediff_rgb.py A.raw B.raw [--rows 0 157]
"""
import sys

W, H = 320, 200
VIEW_LAST = 157


def load(path):
    b = open(path, "rb").read()
    if b[:8] != b"FFEFRM01":
        raise SystemExit("%s is not an FFEFRM01 dump" % path)
    pal = [tuple(b[8 + 3 * i:8 + 3 * i + 3]) for i in range(256)]
    return pal, b[776:776 + W * H]


def main():
    a, b = sys.argv[1], sys.argv[2]
    r0, r1 = 0, H - 1
    if "--rows" in sys.argv:
        k = sys.argv.index("--rows")
        r0, r1 = int(sys.argv[k + 1]), int(sys.argv[k + 2])

    pa, ia = load(a)
    pb, ib = load(b)
    print("A: %s\nB: %s\n" % (a, b))

    for label, lo, hi in (("VIEWPORT (3D)", 0, VIEW_LAST),
                          ("PANEL (2D)", VIEW_LAST + 1, H - 1)):
        lo, hi = max(lo, r0), min(hi, r1)
        if lo > hi:
            continue
        n = diff = 0
        near = 0
        rows = {}
        from collections import Counter
        changes = Counter()
        for y in range(lo, hi + 1):
            for x in range(W):
                ca, cb = pa[ia[y * W + x]], pb[ib[y * W + x]]
                n += 1
                if ca == cb:
                    continue
                diff += 1
                rows[y] = rows.get(y, 0) + 1
                # "near" = every channel within 2 of the 0..63 VGA range, i.e.
                # the same colour rounded into a different palette slot.
                if max(abs(ca[i] - cb[i]) for i in range(3)) <= 2:
                    near += 1
                changes[(ca, cb)] += 1
        pct = 100.0 * diff / n if n else 0.0
        print("%-16s %6d px, %6d differ (%.1f%%), of which %d are near-identical"
              % (label, n, diff, pct, near))
        if diff:
            worst = sorted(rows.items(), key=lambda kv: -kv[1])[:5]
            print("   rows touched: %d   worst: %s"
                  % (len(rows), ", ".join("r%d=%d" % (y - lo, c) for y, c in worst)))
            print("   colour changes: %s"
                  % ", ".join("%s->%s x%d" % (k[0], k[1], v)
                              for k, v in changes.most_common(6)))
        print()


if __name__ == "__main__":
    main()
