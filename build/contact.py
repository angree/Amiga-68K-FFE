#!/usr/bin/env python3
"""Contact sheet of FFEFRM01 dumps: pick frames from a directory (or a list)
and tile them into one PNG with the file name under each tile, so a whole
intro can be LOOKED at in one Read.

    py build\\contact.py <dir> <out.png> [--every K] [--cols C] [--from A] [--to B]
"""
import os
import sys
from PIL import Image, ImageDraw


def load(path):
    d = open(path, "rb").read()
    if d[:8] != b"FFEFRM01" or len(d) != 8 + 768 + 64000:
        return None
    pal = d[8:776]
    # 6-bit VGA values -> 8 bit (same as frame2png: v*4, clamp)
    p8 = bytes(min(255, v * 4) for v in pal)
    im = Image.frombytes("P", (320, 200), d[776:])
    im.putpalette(p8)
    return im.convert("RGB")


def main():
    a = sys.argv[1:]
    src, out = a[0], a[1]
    opt = {"--every": 1, "--cols": 4, "--from": 0, "--to": 10 ** 9}
    i = 2
    while i < len(a):
        opt[a[i]] = int(a[i + 1])
        i += 2
    names = sorted(n for n in os.listdir(src) if n.endswith(".raw"))
    sel = []
    for n in names:
        num = int("".join(c for c in n if c.isdigit()) or 0)
        if opt["--from"] <= num <= opt["--to"]:
            sel.append(n)
    sel = sel[:: opt["--every"]]
    cols = opt["--cols"]
    rows = (len(sel) + cols - 1) // cols
    W, H = 320, 212
    sheet = Image.new("RGB", (cols * W, max(1, rows) * H), (40, 40, 40))
    dr = ImageDraw.Draw(sheet)
    for k, n in enumerate(sel):
        im = load(os.path.join(src, n))
        x, y = (k % cols) * W, (k // cols) * H
        if im is not None:
            sheet.paste(im, (x, y))
        dr.text((x + 2, y + 200), n, fill=(255, 255, 0))
    sheet.save(out)
    print("%s: %d frames, %dx%d" % (out, len(sel), sheet.size[0], sheet.size[1]))


if __name__ == "__main__":
    main()
