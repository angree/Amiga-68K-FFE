import sys, math
def rd(fn):
    out=[]
    for l in open(fn):
        p=l.split()
        if p[0]!='D': continue
        v=list(map(int,p[3:9])); rel=[(v[2*k+1]<<32)|(v[2*k]&0xffffffff) for k in range(3)]
        P=[int(x)/2**31 for x in p[10:19]]; S=[int(x)/2**31 for x in p[20:29]]
        r=math.sqrt(sum(x*x for x in rel)) or 1
        # player direction in station frame (S applied as columns and rows)
        cs=[sum(S[3*j+k]*rel[j] for j in range(3))/r for k in range(3)]
        # player forward axis (P row 2 / col 2) in station frame
        out.append((int(p[1]),r,cs,P,S))
    return out
for fn in sys.argv[1:]:
    d=rd(fn); print('==',fn,len(d))
    for i in range(0,len(d),max(1,len(d)//14)):
        c,r,cs,P,S=d[i]
        print('%11d dist=%10.0f dir=%s Pz=%s'%(c,r,['%.2f'%x for x in cs],['%.2f'%x for x in P[6:9]]))
    c,r,cs,P,S=d[-1]; print('%11d dist=%10.0f dir=%s LAST'%(c,r,['%.2f'%x for x in cs]))
