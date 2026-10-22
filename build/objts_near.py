"""py objts_near.py <objts.log> : per snapshot, player (slot) position and the
nearest objects (model t, distance, relative vector in player's frame-free
world coords, their matrix)."""
import sys, math
snaps=[]; cur=None
for l in open(sys.argv[1], errors='replace'):
    p=l.split()
    if not p: continue
    if p[0]=='T':
        cur={'clk':p[1],'slot':int(p[3].split('=')[1]),'o':{}}; snaps.append(cur); last=None
    elif p[0]=='O':
        i=int(p[1]); t=int(p[3].split('=')[1]); v=list(map(int,p[4:10]))
        pos=[(v[2*k+1]<<32)|(v[2*k]&0xffffffff) for k in range(3)]
        cur['o'][i]={'t':t,'pos':pos}; last=i
    elif p[0]=='M' and last is not None:
        cur['o'][last]['m']=list(map(int,p[1:10]))
for s in snaps[int(sys.argv[2]) if len(sys.argv)>2 else 0:]:
    pl=s['o'].get(s['slot'])
    if not pl: continue
    near=[]
    for i,o in s['o'].items():
        if i==s['slot']: continue
        d=[o['pos'][k]-pl['pos'][k] for k in range(3)]
        near.append((math.sqrt(sum(x*x for x in d)),i,o['t'],d,o.get('m')))
    near.sort()
    print(s['clk'],'player t=%d'%pl['t'],'m=',pl.get('m'))
    for n in near[:3]:
        print('   %12.0f idx=%d t=%d d=%s m=%s'%(n[0],n[1],n[2],n[3],n[4]))
