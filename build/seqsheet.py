"""Contact sheet of a frame series: py seqsheet.py <seqdir> <out.png> [from=0.5] [to=1.0] [n=12] [cols=4]
Frames are FFEFRM dumps (776-byte header incl. 256x3 palette, then 320x200)."""
import sys, glob, os
from PIL import Image, ImageDraw
d, out = sys.argv[1], sys.argv[2]
a = float(sys.argv[3]) if len(sys.argv) > 3 else 0.5
b = float(sys.argv[4]) if len(sys.argv) > 4 else 1.0
n = int(sys.argv[5]) if len(sys.argv) > 5 else 12
cols = int(sys.argv[6]) if len(sys.argv) > 6 else 4
fs = sorted(glob.glob(os.path.join(d, "f*.raw")))
pick = [fs[min(len(fs) - 1, int((len(fs) - 1) * (a + (b - a) * i / (n - 1))))] for i in range(n)]
rows = (n + cols - 1) // cols
sheet = Image.new("RGB", (cols * 322, rows * 212), (40, 40, 40))
dr = ImageDraw.Draw(sheet)
for i, f in enumerate(pick):
    raw = open(f, "rb").read()
    if len(raw) != 64776:
        continue
    pal = raw[8:776]
    if max(pal) <= 63:
        pal = bytes(min(255, v << 2) for v in pal)
    im = Image.frombytes("P", (320, 200), raw[776:])
    im.putpalette(pal)
    x, y = (i % cols) * 322, (i // cols) * 212
    sheet.paste(im.convert("RGB"), (x, y + 11))
    dr.text((x + 2, y), os.path.basename(f), fill=(255, 255, 0))
sheet.save(out)
print(out, len(fs), "frames")
