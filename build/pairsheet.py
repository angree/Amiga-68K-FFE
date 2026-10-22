"""Pair two frame series by GAME CLOCK (index.txt: 'fNNNNN clock') and make
side-by-side sheets (PC left, Amiga right).
py pairsheet.py <pcseq> <amseq> <out_prefix> [n_pairs=24] [per_sheet=6]"""
import sys, os
from PIL import Image, ImageDraw

def load_index(d):
    out = []
    for l in open(os.path.join(d, "index.txt")):
        p = l.split()
        if len(p) >= 2 and os.path.exists(os.path.join(d, p[0] + ".raw")):
            c = int(p[1])
            if c == 0:
                continue
            if out and c < out[-1][0]:
                break          # FIRST loop only: the intro clock restarts
            out.append((c, p[0]))
    return out

def load(d, f):
    raw = open(os.path.join(d, f + ".raw"), "rb").read()
    pal = raw[8:776]
    if max(pal) <= 63:
        pal = bytes(min(255, v << 2) for v in pal)
    im = Image.frombytes("P", (320, 200), raw[776:])
    im.putpalette(pal)
    return im.convert("RGB")

pcd, amd, outp = sys.argv[1:4]
n = int(sys.argv[4]) if len(sys.argv) > 4 else 24
per = int(sys.argv[5]) if len(sys.argv) > 5 else 6
pc, am = load_index(pcd), load_index(amd)
lo = max(pc[0][0], am[0][0]); hi = min(pc[-1][0], am[-1][0])
if len(sys.argv) > 7:
    lo, hi = int(sys.argv[6]), int(sys.argv[7])
pairs = []
for i in range(n):
    t = lo + (hi - lo) * i // (n - 1)
    a = min(pc, key=lambda x: abs(x[0] - t)); b = min(am, key=lambda x: abs(x[0] - t))
    pairs.append((t, a, b))
for s in range(0, n, per):
    chunk = pairs[s:s + per]
    sheet = Image.new("RGB", (644, len(chunk) * 212), (40, 40, 40))
    dr = ImageDraw.Draw(sheet)
    for k, (t, a, b) in enumerate(chunk):
        y = k * 212
        sheet.paste(load(pcd, a[1]), (0, y + 11)); sheet.paste(load(amd, b[1]), (324, y + 11))
        dr.text((2, y), "PC %s clk=%d" % (a[1], a[0]), fill=(255, 255, 0))
        dr.text((326, y), "AM %s clk=%d" % (b[1], b[0]), fill=(255, 255, 0))
    sheet.save("%s-%d.png" % (outp, s // per))
print(len(pairs), "pairs, clock range", lo, hi)
