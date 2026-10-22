#!/usr/bin/env python3
"""Which addrscan candidates are INITIALISED, i.e. where the storage class
actually changes the bytes in the image.

addrscan.py lists address-taken labels whose every access is one width. For a
BSS label (`resb`) the class only changes what the TRANSLATOR assumes at named
access sites, which is a code change and needs its own argument. For an
INITIALISED label the class decides the byte order of real data, and leaving a
dword table in x86 order is the fault already proven three times over:
DATA_007235 (camera), DATA_007236 (view matrix), DATA_007265 (ship identity
matrix), DATA_007121 (star-mass thresholds), DATA_007133 (handcoded systems).

This prints the initialised ones with their leading bytes and the disassembly's
own comment, which is usually the whole evidence needed.

Usage: python tools\\datamap\\addrscan_init.py
"""
import io, os, re, subprocess, sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
DAT = os.path.join(ROOT, "ref", "jjffe", "ffedat.asm")
OVR = os.path.join(os.path.dirname(os.path.abspath(__file__)), "overrides.tsv")


def main():
    out = subprocess.run([sys.executable,
                          os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                       "addrscan.py")],
                         capture_output=True, text=True).stdout
    cand = {}
    for l in out.split("\n"):
        m = re.match(r"^(DATA_\d+)\s+(\d+)\s+(\d+)\s", l)
        if m:
            cand[m.group(1)] = (int(m.group(2)), int(m.group(3)))
        if l.startswith("UNPROVEN"):
            break                      # only the RECOMMENDED block

    already = set()
    for l in io.open(OVR, encoding="utf-8"):
        p = l.split("\t")
        if p and p[0].startswith("DATA_"):
            already.add(p[0])

    lines = io.open(DAT, encoding="latin-1").read().split("\n")
    at = {}
    for i, l in enumerate(lines):
        m = re.match(r"^(DATA_\d+):(.*)", l)
        if m:
            at[m.group(1)] = (i, m.group(2).strip())

    print("%-14s %6s %5s  %s" % ("label", "bytes", "width", "first bytes / comment"))
    n = 0
    for lab, (size, unit) in sorted(cand.items()):
        if lab in already or lab not in at:
            continue
        i, comment = at[lab]
        body = [l.strip() for l in lines[i + 1:i + 3]]
        if not body or body[0].startswith("resb"):
            continue                   # BSS: the bytes are zeros, class is code-only
        n += 1
        print("%-14s %6d %5d  %s" % (lab, size, unit, comment))
        for b in body[:2]:
            print("%34s%s" % ("", b))
    print("\n%d INITIALISED candidates left (of %d recommended)" % (n, len(cand)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
