#!/usr/bin/env python3
r"""Extract the embedded .data blocks the translator REFUSES (byte blobs,
strings, named variables in jjffe's hand-written support code) into one vasm
file, build\xlat-full\support_data.s.

These blocks live inside functions, but their SYMBOLS (pSplash1, ptrIndex,
tmLastTime...) are referenced by TRANSLATED functions and must exist at link
time. Unlike ffedat, these are hand-written named variables accessed only by
name, so an `even` before each label is safe -- nothing does cross-label
arithmetic here.

Numeric dd values are emitted as dc.l (big-endian: these are ints the code
reads as ints). db runs stay bytes. `times N db X` becomes dcb.b.
"""
import io
import os
import re

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
ASM = os.path.join(ROOT, "ref", "jjffe", "ffe.asm")
OUT = os.path.join(ROOT, "build", "xlat-full", "support_data.s")

_SECT_DATA = re.compile(r"^\s*SECTION\s+\.data\s*$", re.I)
_SECT_TEXT = re.compile(r"^\s*SECTION\s+\.text\s*$", re.I)


def split_items(rest):
    items, cur, q = [], "", None
    for ch in rest:
        if q:
            cur += ch
            if ch == q:
                q = None
        elif ch in "'\"":
            q = ch
            cur += ch
        elif ch == ",":
            items.append(cur.strip())
            cur = ""
        else:
            cur += ch
    if cur.strip():
        items.append(cur.strip())
    return items


def to_dcb(items):
    """db items -> a dc.b operand string (vasm accepts 'strings' verbatim)."""
    out = []
    for it in items:
        if it.startswith("'") and it.endswith("'"):
            out.append(it)
        elif re.match(r"^-?0x[0-9a-fA-F]+$", it):
            out.append(str(int(it, 16)))
        else:
            out.append(it)
    return ",".join(out)


def db_len(items):
    n = 0
    for it in items:
        if it.startswith("'") and it.endswith("'"):
            n += len(it) - 2
        else:
            n += 1
    return n


def main():
    lines = io.open(ASM, encoding="latin-1").readlines()
    body = []
    n_blocks = 0
    n_syms = 0
    # label -> (set of content kinds, byte size) for the storage manifest the
    # translator reads; same meaning as convert_static's.
    storage = {}
    cur_label = None

    def note(kind, nbytes):
        if cur_label is not None:
            k, n = storage[cur_label]
            k.add(kind)
            storage[cur_label] = (k, n + nbytes)
    i = 0
    while i < len(lines):
        if not _SECT_DATA.match(lines[i].split(";")[0]):
            i += 1
            continue
        # Peek: a block that is ONLY labels + dd-symbol lines is a jump
        # table -- the translator carries those itself. Anything else lands
        # here.
        j = i + 1
        block = []
        while j < len(lines) and not _SECT_TEXT.match(lines[j].split(";")[0]):
            block.append((j + 1, lines[j].split(";")[0].rstrip()))
            j += 1
        pure = all(
            (not t.strip())
            or re.match(r"^\s*[A-Za-z_]\w*:\s*$", t)
            or re.match(r"^\s*dd\s+[A-Za-z_]\w*\s*$", t)
            for _, t in block)
        if pure:
            i = j + 1
            continue
        n_blocks += 1
        body.append("; ---- ffe.asm:%d ----" % (i + 1))
        for lineno, t in block:
            s = t.strip()
            if not s:
                continue
            # NAME: | NAME directive... | directive
            m = re.match(r"^([A-Za-z_]\w*):\s*(.*)$", s)
            label, rest = (m.group(1), m.group(2).strip()) if m else (None, s)
            if label is None:
                m2 = re.match(r"^([A-Za-z_]\w*)\s+(d[bd]|times)\s+(.*)$", s)
                if m2 and m2.group(1).lower() not in ("db", "dd", "dw",
                                                      "times", "resb"):
                    label, rest = m2.group(1), s[len(m2.group(1)):].strip()
            if label:
                body.append("\teven")
                body.append("\txdef\t_%s" % label if False else "\txdef\t%s" % label)
                body.append("%s:" % label)
                n_syms += 1
                cur_label = label
                storage[label] = (set(), 0)
                if not rest:
                    continue
                s = rest
            m = re.match(r"^dd\s+(.*)$", s)
            if m:
                items = []
                for x in split_items(m.group(1)):
                    if re.match(r"^JUMP_\d+$", x):
                        # A jump-table entry inside a MIXED block. Its JUMP_
                        # label is local to a function the translator REFUSED
                        # (that is why the block landed here), so the symbol
                        # does not exist -- and the only reader of this table
                        # is that same stubbed function. A zero of the same
                        # size keeps every offset honest.
                        items.append("0")
                    elif re.match(r"^-?0x", x):
                        items.append(str(int(x, 16)))
                    else:
                        items.append(x)
                body.append("\tdc.l\t%s" % ",".join(items))
                note("be32", 4 * len(items))
                continue
            m = re.match(r"^db\s+(.*)$", s)
            if m:
                its = split_items(m.group(1))
                body.append("\tdc.b\t%s" % to_dcb(its))
                note("le", db_len(its))
                continue
            m = re.match(r"^times\s+(\d+)\s+db\s+(\S+)$", s)
            if m:
                v = m.group(2)
                v = str(int(v, 16)) if re.match(r"^-?0x", v) else v
                body.append("\tdcb.b\t%s,%s" % (m.group(1), v))
                note("le", int(m.group(1)))
                continue
            raise SystemExit("FATAL: ffe.asm:%d unhandled support line: %r"
                             % (lineno, s))
        i = j + 1

    with io.open(OUT, "w", encoding="utf-8", newline="\n") as f:
        f.write("; GENERATED by tools/datamap/extract_support.py -- the\n"
                "; hand-written variables from ffe.asm's mixed embedded\n"
                "; .data blocks. `even` before each label is safe here:\n"
                "; everything is accessed by name only.\n")
        f.write("\tsection\tffesup,data\n\n")
        f.write("\n".join(body) + "\n")
        # VideoData1/VideoData2: x87 float constants (60000.0f and 500.0f)
        # that ffedis mis-disassembled as code. Raw little-endian bytes, as
        # the (stubbed) x87 functions would expect them.
        f.write("\n\teven\n\txdef\tVideoData1\nVideoData1:\n"
                "\tdc.b\t0,96,106,71\n"
                "\txdef\tVideoData2\nVideoData2:\n"
                "\tdc.b\t0,0,250,67\n")
    storage["VideoData1"] = ({"le"}, 4)
    storage["VideoData2"] = ({"le"}, 4)

    man = OUT[:-2] + ".storage.tsv"
    with io.open(man, "w", encoding="utf-8", newline="\n") as f:
        f.write("label\tstorage\tbytes\n")
        for lab, (kinds, size) in storage.items():
            st = ("le" if not kinds else
                  "split" if len(kinds) > 1 else next(iter(kinds)))
            f.write("%s\t%s\t%d\n" % (lab, st, size))
    print("%d blocks, %d symbols -> %s (+ storage manifest)" % (n_blocks, n_syms, OUT))


if __name__ == "__main__":
    main()
