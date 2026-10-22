import re
def load_amiga():
    rows=[]
    for ln in open(r"winuae/work/ffe_dump.log", errors="ignore"):
        m=re.search(r"a1=(-?\d+) a2=(-?\d+) a3=(-?\d+) pool=(-?\d+) esi=(-?\d+) cull=(\d)", ln)
        if m and ln.strip().startswith("a1="): rows.append(tuple(int(x) for x in m.groups()))
    return rows
def load_pc():
    rows=[]
    for ln in open(r"build/m0-pc/run/pc_objpos.log", errors="ignore"):
        if not ln.startswith("PC1745"): continue
        m=re.search(r"a1=(-?\d+) a2=(-?\d+) a3=(-?\d+) pool=(-?\d+) esi=(-?\d+) cull=(\d)", ln)
        if m: rows.append(tuple(int(x) for x in m.groups()))
    return rows
a=load_amiga(); p=load_pc()
print("AMIGA gate tuples:", len(a), " PC:", len(p))
print("\nAMIGA (a1,a2,a3,pool,esi,cull):")
for r in a: print("  ", r, "  <-- DRAWS (cull=0)" if r[5]==0 else "  culls")
print("\nPC:")
for r in p: print("  ", r, "  <-- DRAWS" if r[5]==0 else "  culls")
# match by (a1,a3): does the reference see the same a1/a3 and decide differently?
pa={(r[0],r[2]):r for r in p}
print("\n=== our DRAWS whose (a1,a3) the reference CULLS or never sees ===")
for r in a:
    if r[5]!=0: continue
    key=(r[0],r[2]); pr=pa.get(key)
    if pr is None: print("   ours draws", r[:5], " -- reference has NO (a1,a3)=",key)
    elif pr[5]==1: print("   ours draws", r[:5], " but reference CULLS", pr[:5]," -- INPUTS a1/a3 EQUAL, pool/esi differ" if (r[3]!=pr[3]) else " -- SAME inputs, gate bug")
