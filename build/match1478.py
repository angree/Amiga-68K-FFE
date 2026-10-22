import re, sys
ref = {}
for ln in open(r"I:\GITHUB\Amiga_Frontier_2_First_Encouters\build\m0-pc\run\pc_objpos.log", errors="ignore"):
    if ln.startswith("PC1478D"):
        m = re.search(r"idx=(-?\d+) desc=([0-9a-f ]+?) flag=(\d) out=\((-?\d+),(-?\d+),(-?\d+)\) ctxpos=\((-?\d+),(-?\d+),(-?\d+)\)", ln)
        if m:
            key = (int(m.group(1)), m.group(2).strip())
            ref.setdefault(key, []).append((int(m.group(3)), tuple(int(m.group(i)) for i in (4,5,6)), tuple(int(m.group(i)) for i in (7,8,9))))
crash = open(r"I:\GITHUB\Amiga_Frontier_2_First_Encouters\winuae\work\ffe_crash.log", errors="ignore").read()
print("PC keys:", len(ref))
for m in re.finditer(r"\[(\d+)\] idx=(-?\d+) obj=([0-9a-f]+) flag=(\d+) desc=([0-9a-f ]+?) out=\((-?\d+),(-?\d+),(-?\d+)\)", crash):
    idx = int(m.group(2)); desc = m.group(5).strip(); ours = tuple(int(m.group(i)) for i in (6,7,8))
    print("\nAMIGA [%s] idx=%d desc=%s flag=%s obj=%s out=%s hex=(%x,%x,%x)" % (m.group(1), idx, desc, m.group(4), m.group(3), ours, *(v & 0xffffffff for v in ours)))
    hits = ref.get((idx, desc))
    if not hits:
        same_desc = [(k, v) for k, v in ref.items() if k[1] == desc]
        print("   PC: NO entry with this key; same descriptor at other idx:", [(k[0], v[0][1]) for k, v in same_desc][:6])
        continue
    for flag, out, ctx in hits:
        tag = "IDENTICAL" if out == ours else "DIFFERS"
        print("   PC : flag=%d out=%s hex=(%x,%x,%x) ctxpos=%s  -> %s" % (flag, out, *(v & 0xffffffff for v in out), ctx, tag))
