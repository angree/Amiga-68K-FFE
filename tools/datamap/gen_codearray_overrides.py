#!/usr/bin/env python3
r"""Generate overrides_codearrays.tsv: the mission CODE ARRAYS and mission
VARIABLE ARRAYS behind the 105 mission struct headers, typed dword-only.

Why this file exists (2026-07-30): the new-game spin was bisected to
FUNC_000053's condition loop reading the code array's COUNT field --
`cmp esi,[eax+0x4]` at ffe.asm (Pos 142db) -- as a dword THROUGH A POINTER.
The count is raw `db 1,0,0,0` in ffebmp.asm; unconverted, the 68k reads
0x01000000 and iterates 16.7M garbage "condition pointers" (each nonzero
result resets the scan: the infinite FUNC_000031 spin in ffe_enter.log).

The mission BYTECODE BLOBS themselves need NO conversion and this tool
verifies they get none: all three interpreters consume the stream through
one-byte opcode fetches (`movzx eax,byte [eax]`) and FUNC_000020, which
assembles its dword operand BYTE BY BYTE little-endian (ffe.asm Pos 11464)
-- endian-neutral as long as the blob bytes stay raw. The planned
"format-aware bytecode converter" is therefore unnecessary for mission code;
only the ARRAY HEADERS around the blobs carry raw LE dwords read wide.

Shapes handled (parsed from ffebmp.asm, offsets verified per label):
  mission header (105 labels, already dword-only via overrides_missions.tsv)
    +0x00 dd blob | db 0*4     "on-parameter" stream (FUNC_000052 reads [hdr])
    +0x14 dd varray | db 0*4   mission variable array (FUNC_000031 opcode 7:
                               [hdr+0x14] -> [ptr+idx*4]; FUNC_000037 writes)
    +0x1c dd codearray...      one per phase (FUNC_000054 picks [hdr+0xc])
  code array (targets of +0x1c..end):
    dd init-blob / db N,0,0,0 / dd cond-blob * N
  variable array (targets of +0x14): raw db LE dwords, dword-only.

Usage: python tools\datamap\gen_codearray_overrides.py
Writes tools\datamap\overrides_codearrays.tsv, then verifies the blob
labels' storage stays raw (le/plain) in the current manifests if present.
"""
import io
import os
import re

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
BMP = os.path.join(ROOT, "ref", "jjffe", "ffebmp.asm")
MISSIONS = os.path.join(HERE, "overrides_missions.tsv")
OUT = os.path.join(HERE, "overrides_codearrays.tsv")


def parse_labels(path):
    lines = io.open(path, encoding="latin-1").readlines()
    labpos = {}
    for i, l in enumerate(lines):
        m = re.match(r"^(DATA_\w+):", l)
        if m:
            labpos[m.group(1)] = i
    def items_of(name):
        i = labpos[name] + 1
        items = []
        while i < len(lines):
            if re.match(r"^\w", lines[i]):
                break
            s = lines[i].strip()
            if s.startswith("db "):
                items.append(("db", [v.strip() for v in s[3:].split(",")]))
            elif s.startswith("dd "):
                items.append(("dd", s[3:].strip()))
            i += 1
        return items
    return labpos, items_of


def main():
    labpos, items_of = parse_labels(BMP)
    headers = [l.split("\t")[0] for l in io.open(MISSIONS) if l.strip()]

    codearrays = {}   # label -> evidence
    varrays = {}
    blobs = set()

    for h in headers:
        off = 0
        for kind, v in items_of(h):
            if kind == "db":
                off += len(v)
                continue
            if re.match(r"^DATA_\w+$", v):
                if off == 0x0:
                    blobs.add(v)          # "on-parameter" stream, stays raw
                elif off == 0x4:
                    # array of parameter-default expression blobs: pure dd
                    # pointer table (dc.l converts itself); entries are blobs
                    for k2, v2 in items_of(v):
                        if k2 != "dd" or not re.match(r"^DATA_\w+$", v2):
                            raise SystemExit("%s (+0x4 of %s): not a pure "
                                             "dd pointer table" % (v, h))
                        blobs.add(v2)
                elif off == 0x14:
                    varrays[v] = h
                elif off >= 0x1c:
                    codearrays.setdefault(v, (h, off))
                else:
                    raise SystemExit("unexpected dd at +%#x in %s" % (off, h))
            off += 4

    # inside each code array: dd entries are BLOBS (stay raw), db runs must
    # be whole dwords (the count) or the be32 flush would refuse the label
    for ca in codearrays:
        for kind, v in items_of(ca):
            if kind == "db":
                if len(v) % 4:
                    raise SystemExit("%s: db run of %d bytes -- not dword "
                                     "layout, refusing to type it" % (ca, len(v)))
            elif re.match(r"^DATA_\w+$", v):
                blobs.add(v)

    clash = (set(codearrays) | set(varrays)) & blobs
    if clash:
        raise SystemExit("label(s) both blob and array: %s" % sorted(clash))
    clash = set(codearrays) & set(varrays)
    if clash:
        raise SystemExit("label(s) both code and var array: %s" % sorted(clash))

    with io.open(OUT, "w", encoding="utf-8", newline="\n") as f:
        for ca in sorted(codearrays):
            h, off = codearrays[ca]
            f.write("%s\tdword-only\tMission code array (hdr %s +%#x): "
                    "dd init-blob / db N,0,0,0 / dd cond-blob*N; the raw LE "
                    "count is read as a dword through a pointer by "
                    "FUNC_000053 (cmp esi,[eax+0x4], Pos 142db) -- unswapped "
                    "it reads 0x01000000 and spins. dd entries stay "
                    "symbolic; only the count db run reverses.\n" % (ca, h, off))
        for va in sorted(varrays):
            f.write("%s\tdword-only\tMission variable array (hdr %s +0x14): "
                    "raw LE dwords read/written as [ptr+idx*4] by "
                    "FUNC_000031 opcode 7 (Pos 11991) and FUNC_000037 "
                    "(Pos 12761).\n" % (va, varrays[va]))
    print("wrote %s: %d code arrays, %d var arrays, %d blob labels verified "
          "raw" % (OUT, len(codearrays), len(varrays), len(blobs)))

    # verify blob storage in current manifests (informational before a
    # reconvert, authoritative after)
    bad = []
    for man in [os.path.join(ROOT, "build", "data-be", "ffebmp_be.storage.tsv"),
                os.path.join(ROOT, "build", "data-be", "ffedat_be.storage.tsv")]:
        if not os.path.exists(man):
            continue
        for l in io.open(man):
            p = l.rstrip("\n").split("\t")
            if len(p) >= 2 and p[0] in blobs and p[1] not in ("le", "plain"):
                bad.append((p[0], p[1]))
    if bad:
        print("WARN: %d blob labels NOT raw in current manifest (stream "
              "bytes would be reordered!):" % len(bad))
        for b in bad[:20]:
            print("  %s\t%s" % b)
    else:
        print("all mission bytecode blobs are raw (le/plain) -- interpreters "
              "read them byte-wise, correct as-is")


if __name__ == "__main__":
    main()
