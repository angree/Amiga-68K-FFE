# SPDX-License-Identifier: MIT
# Copyright (c) 2026 angree. See LICENSE at the repository root.
"""Workbench icons for the AmiFFE release (2026-10-23).

Writes classic OS 2.x/3.x DiskObjects (.info) in the 4 Workbench
pens (0 grey, 1 black, 2 white, 3 blue), drawn for square pixels. Classic
icons are what both Workbench 3.1 and AROS Wanderer read without any icon
library extension, so one set serves every system the game runs on.

    python make_icons.py OUTDIR [--preview PNGDIR]

OUTDIR gets AmiFFE.info (drawer), AmiFFE/ffe_game.info, AmiFFE/ffe-prefs.info
and AmiFFE/README.md.info, laid out as the release archive is.
"""
import os
import struct
import sys

from PIL import Image, ImageDraw

GREY, BLACK, WHITE, BLUE = 0, 1, 2, 3
WB_PAL = [(0xAA, 0xAA, 0xAA), (0, 0, 0), (0xFF, 0xFF, 0xFF), (0x66, 0x88, 0xBB)]

WBDRAWER, WBTOOL, WBPROJECT = 2, 3, 4

# DEPTH 8 although only pens 0-3 are used (2026-10-23): Workbench 3.1 draws an
# icon's planes through a mask and leaves the screen's other planes as they
# were, so a 2-plane icon on an 8-plane Workbench (measured: 640x512x8 here)
# came out mixed with the window backdrop. Planes 2-7 are written as zeros;
# on a shallower screen the extra planes are simply not drawn.
DEPTH = 8
NO_ICON_POSITION = 0x80000000


def canvas(w, h):
    im = Image.new("P", (w, h), GREY)
    im.putpalette([c for rgb in WB_PAL for c in rgb] + [0] * (768 - 12))
    return im, ImageDraw.Draw(im)


def planes(im):
    """Image -> plane 0 then plane 1, rows padded to 16 bits."""
    w, h = im.size
    rowbytes = ((w + 15) // 16) * 2
    px = im.load()
    out = b""
    for plane in range(DEPTH):
        for y in range(h):
            row = bytearray(rowbytes)
            for x in range(w):
                if (px[x, y] >> plane) & 1:
                    row[x >> 3] |= 0x80 >> (x & 7)
            out += bytes(row)
    return out


def image_struct(im):
    w, h = im.size
    # LeftEdge, TopEdge, Width, Height, Depth, ImageData, PlanePick, PlaneOnOff, NextImage
    return struct.pack(">hhhhhIBBI", 0, 0, w, h, DEPTH, 1, (1 << DEPTH) - 1, 0, 0) + planes(im)


def string(s):
    b = s.encode("latin-1") + b"\0"
    return struct.pack(">I", len(b)) + b


def diskobject(kind, normal, selected=None, default_tool=None, tooltypes=(),
               stack=0, drawer=None, pos=None):
    w, h = normal.size
    flags = 0x0004 | (0x0002 if selected else 0x0000)   # GADGIMAGE | GADGHIMAGE / GADGHCOMP
    gadget = struct.pack(">IhhhhHHHIIIiIHI",
                         0, 0, 0, w, h, flags,
                         0x0003,          # RELVERIFY | GADGIMMEDIATE
                         0x0001,          # BOOLGADGET
                         1, 1 if selected else 0, 0, 0, 0, 0,
                         1)               # UserData: WB_DISKREVISION 1 (OS 2.x+)
    head = struct.pack(">HH", 0xE310, 1) + gadget
    head += struct.pack(">BBIIIIIII", kind, 0,
                        1 if default_tool else 0,
                        1 if tooltypes else 0,
                        pos[0] if pos else NO_ICON_POSITION,
                        pos[1] if pos else NO_ICON_POSITION,
                        1 if drawer else 0,
                        0, stack)
    body = b""
    if drawer:
        x, y, ww, wh = drawer
        # struct NewWindow (48 bytes) + dd_CurrentX/Y
        body += struct.pack(">hhhhBBIIIIIIIhhHHH", x, y, ww, wh, 0xFF, 0xFF,
                            0, 0, 0, 0, 0, 0, 0, 90, 40, 0xFFFF, 0xFFFF, 1)
        body += struct.pack(">ii", 0, 0)
    body += image_struct(normal)
    if selected:
        body += image_struct(selected)
    if default_tool:
        body += string(default_tool)
    if tooltypes:
        body += struct.pack(">I", (len(tooltypes) + 1) * 4)
        for t in tooltypes:
            body += string(t)
    if drawer:
        body += struct.pack(">IH", 1, 1)   # DrawerData2: show icons, view by icon
    return head + body


# ---------------------------------------------------------------- pictures
# Drawn for SQUARE pixels (hires interlace, productivity and RTG modes, AROS),
# which is what Workbench 3.x setups and MagicWB-style icons use. Pen 0 is
# drawn TRANSPARENT by Workbench (the window backdrop shows through), so
# nothing that must be seen is left in pen 0.
S = 4   # supersampling: shapes are drawn at 4x and reduced by majority


def big(w, h):
    im = Image.new("P", (w * S, h * S), GREY)
    return im, ImageDraw.Draw(im)


def reduce(bigim):
    w, h = bigim.size[0] // S, bigim.size[1] // S
    im, _ = canvas(w, h)
    src, dst = bigim.load(), im.load()
    for y in range(h):
        for x in range(w):
            n = [0, 0, 0, 0]
            for yy in range(S):
                for xx in range(S):
                    n[src[x * S + xx, y * S + yy]] += 1
            # an outline pen wins ties, so thin black lines survive
            dst[x, y] = max(range(4), key=lambda i: (n[i] + (1 if i == BLACK else 0), i))
    return im


def P(*pts):
    return [(x * S, y * S) for x, y in pts]


def R(x0, y0, x1, y1):
    return (x0 * S, y0 * S, x1 * S + S - 1, y1 * S + S - 1)


FONT = {   # 5x7
    "F": ["#####", "#....", "#....", "####.", "#....", "#....", "#...."],
    "E": ["#####", "#....", "#....", "####.", "#....", "#....", "#####"],
}


def text(d, x, y, s, pen):
    for ch in s:
        for r, row in enumerate(FONT[ch]):
            for c, p in enumerate(row):
                if p == "#":
                    d.rectangle(R(x + c, y + r, x + c, y + r), fill=pen)
        x += 7


def game_icon(sel=False):
    im, d = big(52, 44)
    d.rounded_rectangle(R(0, 0, 51, 43), radius=3 * S, fill=BLACK)            # space
    for x, y in ((6, 5), (20, 3), (44, 6), (34, 14), (4, 22), (48, 38), (14, 36)):
        d.rectangle(R(x, y, x, y), fill=WHITE)
    # planet in the lower right, lit from the upper left: a blue crescent
    d.ellipse(R(20, 18, 64, 62), fill=BLUE)
    d.ellipse(R(26, 23, 72, 69), fill=BLACK)
    d.arc(R(20, 18, 64, 62), 185, 265, fill=WHITE, width=S)
    # a delta-wing ship climbing to the upper right
    top, pan = (WHITE, BLUE) if not sel else (BLUE, WHITE)
    d.polygon(P((4, 38), (42, 6), (34, 24), (18, 36)), fill=top)
    d.polygon(P((18, 36), (34, 24), (42, 6), (28, 34)), fill=pan)
    d.line(P((13, 31), (25, 21)), fill=BLACK, width=S)                        # canopy
    d.rectangle(R(1, 38, 3, 39), fill=BLUE)                                   # engines
    d.rectangle(R(4, 40, 6, 41), fill=BLUE)
    return reduce(im)


def prefs_icon(sel=False):
    im, d = big(46, 40)
    bezel = WHITE if not sel else BLUE
    d.rectangle(R(2, 1, 43, 30), fill=bezel, outline=BLACK, width=S)           # monitor
    d.rectangle(R(6, 4, 39, 26), fill=BLUE if not sel else WHITE, outline=BLACK, width=S)
    for y, knob in ((8, 30), (14, 16), (20, 24)):                             # sliders
        d.rectangle(R(10, y, 35, y), fill=WHITE if not sel else BLACK)
        d.rectangle(R(knob - 2, y - 2, knob + 2, y + 2), fill=WHITE, outline=BLACK, width=S)
    d.rectangle(R(17, 31, 28, 34), fill=BLACK)                                # stand
    d.rectangle(R(9, 35, 36, 37), fill=BLACK)
    return reduce(im)


def drawer_icon(sel=False):
    im, d = big(56, 40)
    d.polygon(P((2, 8), (10, 1), (54, 1), (46, 8)), fill=WHITE, outline=BLACK)   # top
    d.polygon(P((46, 8), (54, 1), (54, 31), (46, 38)), fill=BLACK)               # side
    d.rectangle(R(2, 8, 46, 38), fill=BLUE, outline=BLACK, width=S)              # cabinet
    if sel:                                                                      # pulled out
        d.rectangle(R(5, 11, 43, 15), fill=BLACK)
        d.rectangle(R(4, 15, 44, 37), fill=WHITE, outline=BLACK, width=S)
    else:
        d.rectangle(R(5, 12, 43, 35), fill=BLUE, outline=BLACK, width=S)         # drawer front
    d.rectangle(R(12, 16, 36, 26), fill=WHITE, outline=BLACK, width=S)           # label
    text(d, 14, 18, "FFE", BLACK)
    d.rectangle(R(19, 29, 29, 31), fill=BLACK)                                   # handle
    return reduce(im)


def readme_icon(sel=False):
    im, d = big(32, 38)
    paper = WHITE if not sel else BLUE
    d.polygon(P((3, 1), (22, 1), (29, 8), (29, 36), (3, 36)), fill=paper, outline=BLACK)
    d.polygon(P((22, 1), (22, 8), (29, 8)), fill=BLUE if not sel else WHITE, outline=BLACK)
    for y in (6, 11, 16, 21, 26, 31):
        d.rectangle(R(7, y, 18 if y == 6 else 25, y), fill=BLACK if not sel else WHITE)
    return reduce(im)


ICONS = [
    # path in the archive, kind, picture, extras (pos = place in the drawer window)
    ("AmiFFE.info", WBDRAWER, drawer_icon, dict(drawer=(80, 50, 330, 120))),
    ("AmiFFE/ffe_game.info", WBTOOL, game_icon, dict(stack=65536, pos=(12, 8))),
    ("AmiFFE/ffe-prefs.info", WBTOOL, prefs_icon, dict(stack=16384, pos=(92, 10))),
    ("AmiFFE/README.md.info", WBPROJECT, readme_icon,
     dict(default_tool="SYS:Utilities/MultiView", pos=(170, 8))),
]


def main():
    out = sys.argv[1]
    preview = sys.argv[sys.argv.index("--preview") + 1] if "--preview" in sys.argv else None
    sheet = []
    for path, kind, pic, extra in ICONS:
        n, s = pic(False), pic(True)
        data = diskobject(kind, n, s, **extra)
        dst = os.path.join(out, path)
        os.makedirs(os.path.dirname(dst) or ".", exist_ok=True)
        with open(dst, "wb") as f:
            f.write(data)
        print("%-26s %5d bytes  %dx%d" % (path, len(data), n.size[0], n.size[1]))
        sheet += [n, s]
    if preview:
        W = sum(i.size[0] for i in sheet) + 8 * len(sheet)
        H = max(i.size[1] for i in sheet)
        im = Image.new("RGB", (W, H), (0x99, 0x77, 0x55))   # a backdrop: pen 0 is transparent
        x = 0
        for i in sheet:
            rgb = i.convert("RGB")
            m = i.point(lambda v: 0 if v == GREY else 255, "L")
            im.paste(rgb, (x, 0), m)
            x += i.size[0] + 8
        os.makedirs(preview, exist_ok=True)
        im.resize((W * 4, H * 4), Image.NEAREST).save(os.path.join(preview, "icons.png"))


if __name__ == "__main__":
    main()
