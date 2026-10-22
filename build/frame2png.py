#!/usr/bin/env python3
"""Turn an Amiga-side framebuffer dump into a PNG, on the host.

The Amiga probes write PROGDIR:ffe_frame.raw -- which lands in
I:\\GITHUB\\Amiga_Frontier_2_First_Encouters\\winuae\\work\\ffe_frame.raw because
that directory is the emulated Work: volume. This converts it to a real image.

Why not just screenshot the emulator: the user records the primary monitor, so
our window must never be raised there, and capturing a window parked on a
secondary monitor is unreliable -- one run photographed a browser that happened
to be in front and reported it as the emulated screen. Reading the actual index
buffer removes the emulator window from the evidence chain entirely.

Format written by dump_frame(): b"FFEFRM01", 768 palette bytes (256 x r,g,b in
the game's 6-bit VGA range, 0..63), then 320*200 colour indices.

Usage: python frame2png.py [in.raw] [out.png] [--scale N]
"""
import sys, os, struct, zlib

W, H = 320, 200
DEFAULT_IN = r"I:\GITHUB\Amiga_Frontier_2_First_Encouters\winuae\work\ffe_frame.raw"


def write_png(path, rgb_rows, w, h):
    """Minimal PNG writer -- no third-party dependency needed for 24-bit RGB."""
    raw = b"".join(b"\x00" + row for row in rgb_rows)

    def chunk(tag, data):
        c = struct.pack(">I", len(data)) + tag + data
        return c + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)

    hdr = struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0)   # 8-bit, truecolour
    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n")
        f.write(chunk(b"IHDR", hdr))
        f.write(chunk(b"IDAT", zlib.compress(raw, 9)))
        f.write(chunk(b"IEND", b""))


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    scale = 1
    for a in sys.argv[1:]:
        if a.startswith("--scale"):
            scale = int(a.split("=", 1)[1]) if "=" in a else 2

    src = args[0] if args else DEFAULT_IN
    dst = args[1] if len(args) > 1 else os.path.splitext(src)[0] + ".png"

    if not os.path.exists(src):
        print(f"no dump at {src}")
        return 1
    d = open(src, "rb").read()

    if d[:8] != b"FFEFRM01":
        print(f"bad magic {d[:8]!r} - not a frame dump")
        return 1
    want = 8 + 768 + W * H
    if len(d) != want:
        print(f"WARNING: {len(d)} bytes, expected {want}")

    pal = d[8:8 + 768]
    pix = d[8 + 768:8 + 768 + W * H]
    # The dumped palette is g_pal, which VideoSetPalValue stores ALREADY
    # SHIFTED to 8-bit (<< 2 of the game's 6-bit values). Detect the range:
    # any byte above 63 proves 8-bit, use as-is; an all-<=63 palette is the
    # old 6-bit form and gets the shift. The previous unconditional
    # min(63,v)<<2 crushed every mid-range 8-bit value to 252.
    is8 = any(b > 63 for b in pal)
    if is8:
        lut = [(pal[i * 3], pal[i * 3 + 1], pal[i * 3 + 2])
               for i in range(256)]
    else:
        lut = [(pal[i * 3] << 2, pal[i * 3 + 1] << 2, pal[i * 3 + 2] << 2)
               for i in range(256)]

    rows = []
    for y in range(H):
        line = bytearray()
        for x in range(W):
            r, g, b = lut[pix[y * W + x]]
            line += bytes((r, g, b)) * scale
        for _ in range(scale):
            rows.append(bytes(line))

    write_png(dst, rows, W * scale, H * scale)
    used = len({pix[i] for i in range(W * H)})
    print(f"{dst}  ({W*scale}x{H*scale}, {used} distinct colour indices used)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
