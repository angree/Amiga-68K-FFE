#!/usr/bin/env python3
"""ADDR-ONLY TABLE SCAN: static labels whose address is taken but which are
never read by name, so staticscan cannot type them and convert_static leaves
their bytes in x86 order ('plain'). When the code then reads them as dwords
or words through the pointer, every value is byte-reversed -- DATA_006169,
the 33 x 16-byte commodity table, gave the stock market its nonsense prices
(2026-10-09).

    py tools\datamap\scan_addr_only.py  -> build\datamap\addr_only_review.tsv

For each addr-only label that has no override it prints the size, the
manifest kind and a SHAPE guess from the bytes alone:
  dword?  most 4-byte groups look like small little-endian numbers
          (high bytes 00 00 or ff ff)
  word?   most 2-byte groups have a 00/ff high byte, but not the dword shape
  text    mostly printable
  bytes   none of the above
A guess, not a verdict: read the accessing code before adding an override.
"""
import os, re, sys
ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, os.path.join(ROOT, "tools", "translator"))
import storage

def labels(path):
    cur, data = None, {}
    order = []
    for l in open(path, encoding="latin-1"):
        m = re.match(r"(DATA_\d+):", l)
        if m:
            cur = m.group(1); data[cur] = bytearray(); order.append(cur)
            l = l[m.end():]
        if cur is None:
            continue
        t = l.split(";")[0].strip()
        if t.startswith("db "):
            for tok in re.findall(r"'(?:[^']*)'|0x[0-9a-fA-F]+|\d+", t[3:]):
                if tok.startswith("'"):
                    data[cur] += tok[1:-1].encode("latin-1")
                else:
                    data[cur].append(int(tok, 0) & 0xff)
        elif t.startswith(("dd ", "dw ", "resb")):
            data[cur] += b"\x01\x02\x03\x04" if not t.startswith("resb") else b""
            if t.startswith("dd "):
                data.setdefault(cur + "#dd", True)
    return order, data

def shape(b):
    n = len(b)
    if n == 0:
        return "empty"
    pr = sum(1 for x in b if 32 <= x < 127 or x in (0, 10, 13)) / n
    if n % 4 == 0 and n >= 8:
        g = [b[i:i + 4] for i in range(0, n, 4)]
        ok = sum(1 for x in g if (x[2] == 0 and x[3] == 0) or (x[2] == 0xff and x[3] == 0xff)
                 or (x[3] in (0, 0xff) and x[2] < 0x40)) / len(g)
        nz = sum(1 for x in g if any(x)) / len(g)
        if ok > 0.85 and nz > 0.2:
            return "dword?"
    if n % 2 == 0 and n >= 4:
        g = [b[i:i + 2] for i in range(0, n, 2)]
        ok = sum(1 for x in g if x[1] in (0, 0xff)) / len(g)
        nz = sum(1 for x in g if any(x)) / len(g)
        if ok > 0.85 and nz > 0.2:
            return "word?"
    if pr > 0.9 and any(65 <= x < 123 for x in b):
        return "text"
    return "bytes"

def main():
    scan = {}
    for l in open(os.path.join(ROOT, "build", "datamap", "staticscan.tsv"), encoding="utf-8"):
        p = l.rstrip("\n").split("\t")
        scan[p[0]] = p
    ov = set()
    for name in ("overrides.tsv", "overrides_layout.tsv", "overrides_codearrays.tsv", "overrides_missions.tsv"):
        fp = os.path.join(ROOT, "tools", "datamap", name)
        if os.path.exists(fp):
            for l in open(fp, encoding="utf-8", errors="replace"):
                m = re.match(r"(DATA_\d+)\t", l)
                if m:
                    ov.add(m.group(1))
    order, data = labels(os.path.join(ROOT, "ref", "jjffe", "ffedat.asm"))
    out = []
    for lab in order:
        p = scan.get(lab)
        if not p or p[2] != "addr-only" or lab in ov:
            continue
        if data.get(lab + "#dd"):
            continue          # holds symbolic dd pointers: converted already
        b = bytes(data[lab])
        k = storage.kind(lab)
        out.append((lab, len(b), k[0] if k else "?", shape(b), p[4], b[:16].hex(" ")))
    fp = os.path.join(ROOT, "build", "datamap", "addr_only_review.tsv")
    with open(fp, "w", encoding="utf-8", newline="\n") as o:
        o.write("label\tbytes\tmanifest\tshape\taddr_refs\tfirst16\n")
        for r in out:
            o.write("%s\t%d\t%s\t%s\t%s\t%s\n" % r)
    import collections
    print(collections.Counter((r[2], r[3]) for r in out))
    print("wrote", fp)

if __name__ == "__main__":
    main()
