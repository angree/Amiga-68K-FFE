"""py dock_geom.py <objts.log> : per snapshot the player (slot) vs the station
(model 72): parents, distance, and the direction of the player in the
station's own frame (angle off its z axis)."""
import sys, math
def s64(lo, hi): return (hi << 32) | (lo & 0xffffffff)
snaps=[]; cur=None; last=None
for l in open(sys.argv[1], errors='replace'):
    p=l.split()
    if not p: continue
    if p[0]=='T': cur={'clk':int(p[1].split('=')[1])&0xffffffff,'slot':int(p[3].split('=')[1]),'o':{}}; snaps.append(cur)
    elif p[0]=='O': last=int(p[1]); cur['o'][last]={'t':int(p[3].split('=')[1])}
    elif p[0]=='R': v=list(map(int,p[3:9])); cur['o'][last]['par']=int(p[1].split('=')[1]); cur['o'][last]['rel']=[s64(v[0],v[1]),s64(v[2],v[3]),s64(v[4],v[5])]
    elif p[0]=='M': cur['o'][last]['m']=[int(x)/2147483648 for x in p[1:10]]
for s in snaps:
    pl=s['o'].get(s['slot']); st=[(i,o) for i,o in s['o'].items() if o['t']==72]
    if not pl or not st: continue
    si,so=st[0]
    line='%d player par=%d rel=%s' % (s['clk'], pl['par'], pl['rel'])
    if pl['par']==si:
        d=pl['rel']; r=math.sqrt(sum(x*x for x in d)) or 1
        m=so['m']
        # station frame coordinates (rows or columns -- print both)
        a=[sum(m[3*k+j]*d[j] for j in range(3))/r for k in range(3)]
        b=[sum(m[3*j+k]*d[j] for j in range(3))/r for k in range(3)]
        line+='  dist=%.0f rows=%s cols=%s' % (r, ['%.3f'%x for x in a], ['%.3f'%x for x in b])
    print(line)
