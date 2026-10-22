import re
# Amiga MODREL: "MODREL n=N set=r1,r2,..."
amiga=set()
for ln in open(r"winuae/work/ffe_dump.log", errors="ignore"):
    if ln.startswith("MODREL"):
        m=re.search(r"set=([\-0-9,]*)", ln)
        if m: amiga=set(int(x) for x in m.group(1).split(",") if x)
# PC1772REL: one line per distinct rel
pc=set()
for ln in open(r"build/m0-pc/run/pc_objpos.log", errors="ignore"):
    if ln.startswith("PC1772REL"):
        m=re.search(r"rel=(-?\d+)", ln)
        if m: pc.add(int(m.group(1)))
print("amiga sub-model offsets:", len(amiga))
print("pc    sub-model offsets:", len(pc))
only_ours = sorted(amiga - pc)
print("\nOURS ONLY (%d) -- descents the reference never makes:" % len(only_ours))
# DATA_004681[176] base is ffebmp-relative; a rel maps to a ffebmp offset via the model 176 pointer.
# The crashing model DATA_004560 is at ffebmp 0x3eec8; model 176's pointer offset within ffebmp is unknown here,
# so just print the rels and flag any that, combined with a plausible base, land near 0x3eec8.
for r in only_ours[:40]:
    print("   rel=%d (0x%x)" % (r, r & 0xffffffff))
print("\nPC ONLY (%d):" % len(pc - amiga), sorted(pc - amiga)[:20])
print("SHARED:", len(amiga & pc))
