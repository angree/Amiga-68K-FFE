# SPDX-License-Identifier: MIT
# Copyright (c) 2026 angree. See LICENSE at the repository root.
"""Turn grabscr's in-guest screenshots (GRAB files) into PNGs.
    python grab2png.py in.raw [more.raw ...]     -> in.png next to each"""
import sys
from PIL import Image

for p in sys.argv[1:]:
    b = open(p, "rb").read()
    if b[:4] != b"GRAB":
        print("not a GRAB file:", p)
        continue
    w, h = (b[4] << 8) | b[5], (b[6] << 8) | b[7]
    im = Image.frombytes("P", (w, h), b[8 + 768:8 + 768 + w * h])
    im.putpalette(b[8:8 + 768])
    out = p.rsplit(".", 1)[0] + ".png"
    im.convert("RGB").save(out)
    print(out, w, h)
