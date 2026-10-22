#!/usr/bin/env python3
"""Which 'le' blobs are really 16-BIT LITTLE-ENDIAN data?

The model-bytecode interpreter FUNC_001772 (ffe.asm:111186) fetches its stream
with `movzx edx,word [eax]` / `add eax,2` / `and edx,0x1f` -- SIXTEEN-BIT
words, not bytes. Every 16-bit value the game stores little-endian therefore
has to be byte-swapped for the 68k, or the opcode index comes out of the wrong
byte. The static scan cannot see these labels at all (they are reached only
through pointer tables), so they defaulted to 'le' = raw x86 bytes: 1.22 MB of
ffebmp is in that class.

The measurement, and why it discriminates: signed 16-bit quantities in a model
stream are small -- coordinates, counts, shades. A small LE value keeps its
interesting bits in the FIRST byte of the pair and 0x00 (or 0xFF, for small
negatives) in the SECOND. So a genuine 16-bit LE blob shows a strong ASYMMETRY:
odd-offset bytes are dominated by 0x00/0xFF while even-offset bytes are not.
A byte array -- a bitmap, a palette, a string -- has no such asymmetry, and
neither does 32-bit data (which puts two quiet bytes in each group of four,
at offsets 2 and 3, so its odd/even split is much weaker).

Reported per label:
    hi   = fraction of odd-offset bytes that are 0x00 or 0xFF
    lo   = the same fraction for even-offset bytes
    skew = hi - lo         (the discriminator; >= 0.35 is a strong 16-bit hit)

Usage: python tools\\datamap\\wordscan.py [ffebmp|ffedat] [--min-bytes N]
       python tools\\datamap\\wordscan.py --at 0x1234    (byte offset lookup)
"""
import io, os, re, sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
BE = os.path.join(ROOT, "build", "data-be")

_LABEL = re.compile(r"^(DATA_\d+):")


def read_blobs(path):
    """label -> (offset, list-of-bytes-or-None). None marks a dc.l entry, whose
    four bytes are already big-endian and must not be counted."""
    out, cur, off = [], None, 0
    for line in io.open(path, encoding="utf-8"):
        m = _LABEL.match(line)
        if m:
            cur = [m.group(1), off, []]
            out.append(cur)
            continue
        s = line.strip()
        if s.startswith("dc.b"):
            vals = [int(v) & 0xFF for v in s[4:].split(",")]
            if cur: cur[2].extend(vals)
            off += len(vals)
        elif s.startswith("dc.l"):
            n = len(s[4:].split(","))
            if cur: cur[2].extend([None] * 4 * n)
            off += 4 * n
        elif s.startswith("ds.b"):
            n = int(s[4:].strip())
            if cur: cur[2].extend([None] * n)
            off += n
    return out


def stats(b):
    """(hi, lo, n) over the byte pairs that are actually initialised."""
    hi = lo = nhi = nlo = 0
    for i, v in enumerate(b):
        if v is None:
            continue
        if i & 1:
            nhi += 1
            if v == 0 or v == 255: hi += 1
        else:
            nlo += 1
            if v == 0 or v == 255: lo += 1
    if nhi < 8 or nlo < 8:
        return None
    return (hi / float(nhi), lo / float(nlo), nhi + nlo)


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    which = args[0] if args else "ffebmp"
    minb = 64
    at = None
    for a in sys.argv[1:]:
        if a.startswith("--min-bytes"):
            minb = int(a.split("=", 1)[1])
        if a.startswith("--at"):
            at = int(a.split("=", 1)[1], 0)

    path = os.path.join(BE, which + "_be.s")
    blobs = read_blobs(path)

    if at is not None:
        prev = None
        for name, off, b in blobs:
            if off > at:
                break
            prev = (name, off, len(b))
        print("offset %#x is inside %s (starts %#x, %d bytes, +%#x)"
              % (at, prev[0], prev[1], prev[2], at - prev[1]))
        return 0

    # storage class, so only the labels left raw are judged
    st = {}
    with io.open(os.path.join(BE, which + "_be.storage.tsv"), encoding="utf-8") as f:
        next(f)
        for l in f:
            p = l.rstrip("\n").split("\t")
            if len(p) >= 2: st[p[0]] = p[1]

    rows = []
    for name, off, b in blobs:
        if st.get(name) != "le" or len(b) < minb:
            continue
        s = stats(b)
        if s is None:
            continue
        rows.append((s[0] - s[1], name, off, s))

    rows.sort(reverse=True)
    tot16 = sum(r[3][2] for r in rows if r[0] >= 0.35)
    totle = sum(r[3][2] for r in rows)
    print("%-14s %8s %8s %6s %6s %6s" % ("label", "offset", "bytes", "hi", "lo", "skew"))
    for skew, name, off, s in rows[:25]:
        print("%-14s %8x %8d %6.2f %6.2f %6.2f" % (name, off, s[2], s[0], s[1], skew))
    print("...")
    for skew, name, off, s in rows[-10:]:
        print("%-14s %8x %8d %6.2f %6.2f %6.2f" % (name, off, s[2], s[0], s[1], skew))
    print("\n%d 'le' labels measured, %d bytes total; %d bytes (%.1f%%) show the "
          "16-bit signature (skew >= 0.35)"
          % (len(rows), totle, tot16, 100.0 * tot16 / max(1, totle)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
