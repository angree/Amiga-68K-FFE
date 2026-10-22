"""Side-by-side sheet for build\\SCEN\\{pc,am}-keyN: the LAST frame of each run
(both runs sit in the landed/spawned cockpit by then), PC left, Amiga right.
py build\\scen_sheet.py <out.png> [keys=1,2,3,4,5]"""
import sys, os, glob
from PIL import Image, ImageDraw

def load(f):
    raw = open(f, "rb").read()
    pal = raw[8:776]
    if max(pal) <= 63:
        pal = bytes(min(255, v << 2) for v in pal)
    im = Image.frombytes("P", (320, 200), raw[776:])
    im.putpalette(pal)
    return im.convert("RGB")

base = r"C:\temp\amiffe\build\SCEN"
keys = (sys.argv[2] if len(sys.argv) > 2 else "1,2,3,4,5").split(",")
sheet = Image.new("RGB", (644, 212 * len(keys)), (40, 40, 40))
dr = ImageDraw.Draw(sheet)
for r, k in enumerate(keys):
    for c, side in enumerate(("pc", "am")):
        dirs = sorted(glob.glob(os.path.join(base, "%s-key%s-*" % (side, k))), key=os.path.getmtime)
        fs = sorted(glob.glob(os.path.join(dirs[-1], "f*.raw"))) if dirs else []
        if not fs:
            dr.text((c * 324 + 4, r * 212 + 4), "%s key%s: NO FRAMES" % (side, k), fill=(255, 0, 0))
            continue
        sheet.paste(load(fs[-1]), (c * 324, r * 212 + 11))
        dr.text((c * 324 + 2, r * 212), "%s key%s %s" % (side, k, os.path.basename(fs[-1])), fill=(255, 255, 0))
sheet.save(sys.argv[1])
