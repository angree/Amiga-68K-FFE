#!/usr/bin/env python3
"""Every WIDE read of the MODEL BYTECODE STREAM, which is raw little-endian.

The stream stays in x86 byte order and cannot be swapped in memory: the
dispatcher FUNC_001772 (ffe.asm:111186) fetches each opcode as a 16-bit word

    movzx edx,word [eax] / add eax,2 / and edx,0x1f / call [edx*4+DATA_007816]

while the 32 handlers behind that table read their operands BYTE-wise
(`mov al,[ebx+0x4]`, `cmp byte [ebx],0x7f`, `movsx eax,byte [esi]`). No byte
order in memory satisfies both readers, so the data stays raw and every WIDE
read is swapped at the site -- the mechanism already used for the GUI hot-area
walker (tools\\datamap\\le_read_sites.tsv).

This finds the sites to audit. Within each function it propagates the stream
pointer: the incoming argument (a handler's [ebp+0xc], the dispatcher's own
cursor), any register copied from it, and any register that only has a
constant added to it. It then reports every 16- or 32-bit memory read through
one of those registers. BYTE reads are listed separately -- they are correct
as they stand and must NOT be swapped.

Usage: python tools\\datamap\\streamscan.py [--bytes]
"""
import io, os, re, sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
ASM = os.path.join(ROOT, "ref", "jjffe", "ffe.asm")

# the 32 entries of DATA_007816 (ffedat.asm:25528) plus the sub-interpreters
# they hand the cursor to
HANDLERS = """1754 1698 1702 1707 1711 1721 1696 1706 1710 1738 1747 1752 1755
1758 1749 1759 1815 1760 1763 1756 1757 1697 1737 1765 1766 1771 1762 1748
1741 1767 1874 1772 1788 1790 1791 1813 1814""".split()

# Functions whose CURSOR is arg0, not arg1. The 32 table handlers are called
# (state, cursor, opcode) by FUNC_001772:111197-111201, so their cursor is
# arg1; the second-level ones are entered with the cursor first -- 1814 is
# installed as a callback (`mov dword [edi],FUNC_001814`, ffe.asm:110772) and
# opens with `mov ebx,[ebp+0x8]` before walking pairs at +0xc.
ARG0_CURSOR = {"1788", "1790", "1791", "1813", "1814"}

R32 = r"(?:eax|ebx|ecx|edx|esi|edi|ebp)"
_FUNC = re.compile(r"^(FUNC_\d{6})(?:_\w+)?:")


def load():
    lines = io.open(ASM, encoding="latin-1").read().split("\n")
    starts = []
    for i, l in enumerate(lines):
        m = _FUNC.match(l)
        if m:
            starts.append((i, m.group(1)))
    return lines, starts


def body(lines, starts, name):
    for k, (i, n) in enumerate(starts):
        if n == name:
            j = starts[k + 1][0] if k + 1 < len(starts) else len(lines)
            return i, lines[i:j]
    return None, []


def scan(lines, starts, name, argslot):
    i0, b = body(lines, starts, name)
    if not b:
        return [], []
    live = set()
    wide, byt = [], []
    for off, l in enumerate(b):
        s = l.split(";")[0].strip()
        if not s:
            continue
        ln = i0 + off + 1
        # seed: the argument that carries the cursor
        m = re.match(r"mov\s+(%s),\[ebp\+%s\]" % (R32, argslot), s)
        if m:
            live.add(m.group(1)); continue
        # copies and constant advances keep the cursor
        m = re.match(r"mov\s+(%s),(%s)$" % (R32, R32), s)
        if m:
            if m.group(2) in live: live.add(m.group(1))
            else: live.discard(m.group(1))
            continue
        # `lea r,[cursor+disp]` still points into the stream; `lea r,[r+r*8]`
        # is index arithmetic and must KILL the cursor, or every table read
        # that follows is reported as a stream read (12 false hits in 1747).
        m = re.match(r"lea\s+(%s),\[(%s)((?:\s*[+-]\s*(?:byte\s*)?[+-]?"
                     r"(?:0x[0-9a-f]+|\d+))?)\]" % (R32, R32), s)
        if m:
            if m.group(2) in live: live.add(m.group(1))
            else: live.discard(m.group(1))
            continue
        m = re.match(r"lea\s+(%s),\[" % R32, s)
        if m:
            live.discard(m.group(1))      # any scaled/complex lea
            continue
        if re.match(r"(add|sub)\s+(%s),\s*(byte\s*)?[+-]?(0x[0-9a-f]+|\d+)" % R32, s):
            continue                      # advancing the cursor keeps it live
        # any other write to a register kills it
        m = re.match(r"(?:mov|movsx|movzx|lea|xor|and|or|shl|shr|sar|imul|pop)"
                     r"\s+(%s)\b" % R32, s)
        if m and m.group(1) in live and "[" in s:
            pass                          # a load THROUGH the cursor: report first
        # report reads through a live cursor
        m = re.search(r"\[(%s)(?:[+*][^\]]*)?\]" % R32, s)
        if m and m.group(1) in live and not re.match(r"mov\s+\[", s):
            if re.search(r"\bbyte\b|,\s*[a-d][lh]\b|\b[a-d][lh],", s):
                byt.append((ln, s))
            elif re.search(r"\bword\b|\b[a-d]x,|,\s*[a-d]x\b|\b(?:si|di),|,\s*(?:si|di)\b", s):
                wide.append((ln, s, 2))
            elif re.search(r"\bdword\b|%s,\s*\[" % R32, s):
                wide.append((ln, s, 4))
        if m and m.group(1) in live and re.match(r"mov\s+\[", s):
            continue
        # a register loaded from memory is no longer the cursor
        m = re.match(r"(?:mov|movsx|movzx)\s+(%s),\s*\[" % R32, s)
        if m:
            live.discard(m.group(1))
    return wide, byt


def main():
    lines, starts = load()
    show_bytes = "--bytes" in sys.argv
    tw = tb = 0
    for h in HANDLERS:
        name = "FUNC_%06d" % int(h)
        # A table handler is called (state, cursor, opcode) so its cursor is
        # arg1 = [ebp+0xc]; the sub-interpreters the handlers hand the cursor
        # to (1788/1790/1791/1813/1814) take it as arg0 = [ebp+0x8]. Scan
        # both slots and merge -- a function that never reads the other slot
        # contributes nothing, so the union is safe.
        slot = r"0x8" if h in ARG0_CURSOR else r"0xc"
        w1, b1 = scan(lines, starts, name, slot)
        w2, b2 = [], []
        seen = set()
        wide, byt = [], []
        for x in w1 + w2:
            if x[0] not in seen: seen.add(x[0]); wide.append(x)
        seen = set()
        for x in b1 + b2:
            if x[0] not in seen: seen.add(x[0]); byt.append(x)
        wide.sort(); byt.sort()
        tw += len(wide); tb += len(byt)
        if wide or (show_bytes and byt):
            print("\n=== %s   wide=%d byte=%d" % (name, len(wide), len(byt)))
            for ln, s, w in wide:
                print("  WIDE%d  ffe.asm:%d  %s" % (w, ln, s))
            if show_bytes:
                for ln, s in byt:
                    print("  byte   ffe.asm:%d  %s" % (ln, s))
    print("\n%d wide reads of the stream, %d byte reads (byte reads need NO swap)"
          % (tw, tb))
    return 0


if __name__ == "__main__":
    sys.exit(main())
