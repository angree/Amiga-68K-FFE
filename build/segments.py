"""py segments.py <seqdir> <out.png> [cols=4]: split a frame series into runs of
the same screen (a new run starts when >5% of the 320x158 viewport+panel pixels
change), keep the LAST frame of each run that lasts >= 2 frames."""
import sys, glob, os
from PIL import Image, ImageDraw
def load(f):
    raw=open(f,'rb').read(); pal=raw[8:776]
    if max(pal)<=63: pal=bytes(min(255,v<<2) for v in pal)
    im=Image.frombytes('P',(320,200),raw[776:]); im.putpalette(pal); return im.convert('RGB')
fs=sorted(glob.glob(os.path.join(sys.argv[1],'f*.raw')))
segs=[]; prev=None; run=[]
for f in fs:
    px=open(f,'rb').read()[776:776+320*190]
    if prev is not None and sum(a!=b for a,b in zip(px[::7],prev[::7])) > 0.05*len(px[::7]):
        segs.append(run); run=[]
    run.append(f); prev=px
segs.append(run)
keep=[r[-1] for r in segs if len(r)>=2]
cols=int(sys.argv[3]) if len(sys.argv)>3 else 4
rows=(len(keep)+cols-1)//cols
sh=Image.new('RGB',(cols*322,rows*212),(40,40,40)); dr=ImageDraw.Draw(sh)
for i,f in enumerate(keep):
    x,y=(i%cols)*322,(i//cols)*212
    sh.paste(load(f),(x,y+11)); dr.text((x+2,y),'%d %s'%(i,os.path.basename(f)),fill=(255,255,0))
sh.save(sys.argv[2]); print(len(keep),'screens')
