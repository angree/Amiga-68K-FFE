#!/usr/bin/env python3
"""Region statistics for an FFE frame dump -- the first viewport probe.

The question this answers: is ANYTHING being written into the 3D viewport
(y < 158, DATA_007774 = the viewport height the game itself clips to), or is
the region uniformly one colour? A uniform region means the rasteriser never
ran or every primitive was rejected; a region with a handful of indices means
pixels ARE landing and the problem is colour/clip/placement.

Usage: python framestats.py [in.raw] [--rows]
"""
import sys, os
from collections import Counter

W, H, VIEW_H = 320, 200, 158
DEFAULT_IN = r"I:\GITHUB\Amiga_Frontier_2_First_Encouters\winuae\work\ffe_frame.raw"


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    src = args[0] if args else DEFAULT_IN
    show_rows = "--rows" in sys.argv

    if not os.path.exists(src):
        print(f"no dump at {src}")
        return 1
    d = open(src, "rb").read()
    if d[:8] != b"FFEFRM01":
        print(f"bad magic {d[:8]!r}")
        return 1
    pal = d[8:8 + 768]
    pix = d[8 + 768:8 + 768 + W * H]
    print(f"{src}  ({len(d)} bytes)")

    def region(name, y0, y1):
        c = Counter(pix[y0 * W:(y1) * W])
        tot = (y1 - y0) * W
        top = c.most_common(8)
        print(f"\n{name}  rows {y0}..{y1-1}  ({tot} px, {len(c)} distinct indices)")
        for idx, n in top:
            r, g, b = pal[idx * 3], pal[idx * 3 + 1], pal[idx * 3 + 2]
            print(f"   idx {idx:3d}  {n:6d} px  {100.0*n/tot:5.1f}%   rgb({r:3d},{g:3d},{b:3d})")
        return c

    cv = region("VIEWPORT (3D)", 0, VIEW_H)
    region("PANEL (2D cockpit)", VIEW_H, H)

    if len(cv) == 1:
        print("\n=> VIEWPORT IS UNIFORM: nothing at all is being drawn there.")
    else:
        print(f"\n=> viewport has {len(cv)} indices -- pixels ARE landing.")

    if show_rows:
        print("\nper-row distinct-index count (viewport):")
        for y in range(VIEW_H):
            row = pix[y * W:(y + 1) * W]
            c = Counter(row)
            mark = "" if len(c) == 1 else "  <-- non-uniform"
            print(f"  y={y:3d}  distinct={len(c):3d}  first={row[0]:3d}{mark}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
