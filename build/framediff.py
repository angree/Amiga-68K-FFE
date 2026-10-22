#!/usr/bin/env python3
"""Compare two FFEFRM01 frame dumps -- ours against a reference, or one run
against another -- and say WHERE and HOW they differ, per region.

Format (written identically by ffe_video.c, ffe_apitest.c and ffe_drawtest.c):
    "FFEFRM01"  8 bytes
    palette     256 x (r,g,b), 6-bit VGA range
    indices     320*200 colour indices

Why per-region: the 3D viewport is rows 0..157 and the 2D cockpit panel rows
158..199, and they fail for completely different reasons. A single "N pixels
differ" number hides which one moved.

Why also compare the PALETTE: a frame can be pixel-identical and still look
wrong if the palette differs, and the reverse -- identical palettes with
different indices -- means the geometry moved, not the colours. Separating the
two is the first question worth answering.

Usage:
    python build\\framediff.py A.raw B.raw
    python build\\framediff.py A.raw B.raw --rows 0 157     (viewport only)

NOTE for the PC-vs-Amiga comparison specifically: the two builds are NOT at the
same simulated time while the FUNC_000152 clock-levelling shortcut is in
FfeEnterLog -- it skips the 43,207-unit module-time catch-up that the PC
reference runs. Remove that shortcut before reading anything into a diff, or
every object in the scene will be in a different place for that one reason.
"""
import io
import os
import sys
from collections import Counter

W, H = 320, 200
VIEW_ROWS = (0, 157)
PANEL_ROWS = (158, 199)


def load(path):
    b = io.open(path, "rb").read()
    if len(b) < 8 + 768 + W * H:
        raise SystemExit("%s: too short (%d bytes)" % (path, len(b)))
    if b[:8] != b"FFEFRM01":
        raise SystemExit("%s: not an FFEFRM01 dump" % path)
    pal = b[8:8 + 768]
    idx = b[8 + 768:8 + 768 + W * H]
    return pal, idx


def region(idx, r0, r1):
    return idx[r0 * W:(r1 + 1) * W]


def report(name, a, b):
    n = len(a)
    diff = [i for i in range(n) if a[i] != b[i]]
    print("%-22s %6d px, %6d differ (%.1f%%)"
          % (name, n, len(diff), 100.0 * len(diff) / n if n else 0))
    if not diff:
        return
    rows = Counter(i // W for i in diff)
    print("   rows touched: %d   worst: %s"
          % (len(rows), ", ".join("r%d=%d" % (r, c)
                                  for r, c in rows.most_common(5))))
    pairs = Counter((a[i], b[i]) for i in diff)
    print("   index changes: %s"
          % ", ".join("%d->%d x%d" % (x, y, c) for (x, y), c in pairs.most_common(6)))


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    if len(args) != 2:
        print(__doc__)
        return 2
    rows = None
    if "--rows" in sys.argv:
        i = sys.argv.index("--rows")
        rows = (int(sys.argv[i + 1]), int(sys.argv[i + 2]))

    pa, ia = load(args[0])
    pb, ib = load(args[1])
    print("A: %s" % os.path.abspath(args[0]))
    print("B: %s" % os.path.abspath(args[1]))
    print("")

    if pa == pb:
        print("palette: IDENTICAL")
    else:
        d = [i for i in range(256) if pa[3 * i:3 * i + 3] != pb[3 * i:3 * i + 3]]
        print("palette: %d of 256 entries differ -- e.g. %s"
              % (len(d), ", ".join(
                  "idx %d %s vs %s" % (i, tuple(pa[3 * i:3 * i + 3]),
                                       tuple(pb[3 * i:3 * i + 3]))
                  for i in d[:4])))
    print("")

    if rows:
        report("rows %d..%d" % rows, region(ia, *rows), region(ib, *rows))
    else:
        report("VIEWPORT (3D)", region(ia, *VIEW_ROWS), region(ib, *VIEW_ROWS))
        report("PANEL (2D cockpit)", region(ia, *PANEL_ROWS), region(ib, *PANEL_ROWS))
    return 0


if __name__ == "__main__":
    sys.exit(main())
