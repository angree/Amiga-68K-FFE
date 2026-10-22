#!/usr/bin/env python3
r"""ffedat.asm / ffebmp.asm -> vasm big-endian data sources.

The three rules, in order of how much they matter:

1. NOTHING here may change a single label's OFFSET. The x86 image is
   byte-packed; every emitted item is exactly as long as its NASM original,
   and no alignment padding is EVER inserted. The 68020 reads unaligned
   words and longs (slower, but correct), which is what makes the all-bytes
   fallback legal.
2. `dd` becomes `dc.l`: the assembler writes big-endian numbers and 68k
   relocations for symbols -- PLAN 5's invariant for every pointer table.
   A `dc.l` at an ODD offset would make vasm pad and shift the world, so an
   odd-offset dd is a FATAL error, not a warning.
3. `db` blobs are re-ordered per label using build\datamap\staticscan.tsv:
   a dword-only label's bytes are emitted in groups of four REVERSED, a
   word-only label's in pairs reversed, everything else raw. Emitted as
   dc.b always, so rule 1 cannot be broken. A typed label whose byte count
   does not divide evenly is left raw and WARNED about.

Usage: python tools\datamap\convert_static.py
Writes build\data-be\ffedat_be.s and build\data-be\ffebmp_be.s.
"""
import io
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
JJ = os.path.join(ROOT, "ref", "jjffe")
OUT = os.path.join(ROOT, "build", "data-be")
SCAN = os.path.join(ROOT, "build", "datamap", "staticscan.tsv")

_LABEL = re.compile(r"^([A-Za-z_]\w*):")
_FUNCNAME = re.compile(r"^FUNC_(\d+)(?:_\w+)?$")


def canon(sym):
    m = _FUNCNAME.match(sym)
    return "FUNC_" + m.group(1) if m else sym


def load_classes():
    """label -> (class, addr_taken). Class comes from staticscan.tsv, then
    tools\\datamap\\overrides.tsv on top -- the hand-curated answers for
    pointer-reached labels the scan cannot see into. Every override carries its
    evidence in the file; an override without evidence is a guess and gets
    reverted on sight."""
    cls = {}
    with io.open(SCAN, encoding="utf-8") as f:
        next(f)
        for l in f:
            p = l.rstrip("\n").split("\t")
            if len(p) >= 5:
                klass = p[2]
                # Mixed-width labels: the DOMINANT width plus the label's own
                # size decide the storage, because the two real idioms look
                # the same to a width tally alone:
                #   - a 2-byte label word-read 11 times and dword-read 4 is a
                #     WORD PAIR (x,y grabbed together)  -> word-only/be16,
                #     the dword sites become one SWAP in the MOV rules;
                #   - a 4-byte label dword-read 49 times with one word read
                #     is a DWORD var whose low word someone peeks -> be32,
                #     the word site gets the frame.py displacement flip.
                # The size guard matters: DATA_009181 is THREE bytes with a
                # dword access spilling into its neighbour -- only 'le'
                # storage (raw x86 bytes + explicit swaps) reproduces that,
                # so anything that fails the guard stays at the default.
                if klass == "mixed":
                    wc = {}
                    for w in p[3].split(","):
                        if w:
                            k, v = w.split(":")
                            wc[int(k)] = int(v)
                    size = int(p[1] or 0)
                    dw, ww, bw = wc.get(4, 0), wc.get(2, 0), wc.get(1, 0)
                    if dw >= max(ww, bw) and size and size % 4 == 0:
                        klass = "dword-only"
                    elif ww > max(dw, bw) and size and size % 2 == 0:
                        klass = "word-only"
                cls[p[0]] = (klass, int(p[4] or 0))
    import glob as _glob
    for ov in sorted(_glob.glob(os.path.join(
            os.path.dirname(os.path.abspath(__file__)), "overrides*.tsv"))):
        with io.open(ov, encoding="utf-8") as f:
            if os.path.basename(ov) == "overrides.tsv":
                next(f)                      # only the main file has a header
            for l in f:
                p = l.rstrip("\n").split("\t")
                if len(p) >= 2 and p[0]:
                    old = cls.get(p[0], ("raw", 0))
                    klass = p[1]
                    addr = old[1]
                    # `+ptr` forces the 'plain' storage: the label takes part
                    # in a byte-order-preserving copy idiom, so NO width may
                    # ever be swapped on it, exactly as if it were
                    # address-taken.
                    if klass.endswith("+ptr"):
                        klass = klass[:-4]
                        addr = max(addr, 1)
                    # `+le` forces the raw-LE storage even on an
                    # address-taken label: every reader (named or pointer)
                    # must then use x86-order accesses, which is the truth
                    # for byte-flag structs like the console-button array.
                    elif klass.endswith("+le"):
                        klass = klass[:-3]
                        addr = 0
                    cls[p[0]] = (klass, addr)
    return cls


def split_items(rest):
    """Comma split that respects NASM quoted strings."""
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


def item_value(t):
    """A db/dw/dd item -> ('num', int) | ('sym', name) | ('str', bytes)."""
    t = t.strip()
    if t.startswith("'") and t.endswith("'") and len(t) >= 2:
        return ("str", t[1:-1].encode("latin-1"))
    if re.match(r"^-?0x[0-9a-fA-F]+$", t):
        return ("num", int(t, 16))
    if re.match(r"^-?\d+$", t):
        return ("num", int(t, 10))
    return ("sym", t)


def load_layouts():
    """tools/datamap/overrides_layout.tsv: label <tab> spec <tab> evidence, where
    spec is a comma list of  width@offset[*count]  (e.g. 4@0x0*9,2@0x98)."""
    path = os.path.join(os.path.dirname(os.path.abspath(__file__)), "overrides_layout.tsv")
    lay = {}
    if not os.path.exists(path):
        return lay
    for l in open(path, encoding="utf-8", errors="replace"):
        p = l.rstrip("\n").split("\t")
        if len(p) < 2 or not p[0].startswith("DATA_"):
            continue
        spans = []
        for t in p[1].split(","):
            w, rest = t.strip().split("@")
            off, _, cnt = rest.partition("*")
            spans.append((int(w), int(off, 0), int(cnt) if cnt else 1))
        lay[p[0]] = spans
    return lay


LAYOUTS = load_layouts()


class Emitter:
    def __init__(self, f):
        self.f = f
        self.offset = 0
        self.pend = []          # raw byte stream of the current label
        self.pend_label = None
        self.pend_class = "raw"
        self.warns = []
        self.odd_longs = 0
        # label -> (storage, size). Storage is what the TRANSLATOR must assume
        # about the bytes behind the label:
        #   be32  every 4-byte group is big-endian   (dc.l content, or db
        #         re-ordered by a dword-only class)
        #   be16  every 2-byte pair is big-endian
        #   le    bytes exactly as on the x86 -- byte reads are free, wider
        #         reads need an explicit byte-swap in the translated code
        #   plain like le, but the label is ADDRESS-TAKEN: computed-pointer
        #         accesses (which the translator cannot see) touch these bytes
        #         with no swap, so named accesses must not swap either --
        #         width-consistent code stays right, mixed-width code through
        #         both routes is the residual runtime-half risk
        #   split the label mixes dc.l content with raw bytes -- only accesses
        #         the translator can refuse
        self.storage = {}
        self._cur_kinds = set()
        self._label_start = 0
        self.pend_addr = 0

    def _close_label(self):
        if self.pend_label is None:
            return
        k = self._cur_kinds
        size = self.offset - self._label_start
        if not k:
            st = "le"                      # empty label: next label's alias
        elif len(k) > 1:
            st = "split"
        else:
            st = k.pop()
        # A be32/be16 label whose size is not a whole number of groups cannot
        # be group-swapped consistently -- DATA_008818 is THREE bytes with 36
        # dword reads spilling into the flag byte behind it, pure jjffe
        # sloppiness that only x86 byte order reproduces. Downgrade to 'le':
        # byte readers stay right and the MOV/PUSH swap shapes carry the rest.
        # (Initialised content is safe: the db path already fell back to raw
        # on the same divisibility test, and dd content is always 4-aligned.)
        if st == "be32" and size % 4:
            st = "le"
        elif st == "be16" and size % 2:
            st = "le"
        self.storage[self.pend_label] = (st, size)

    def flush(self):
        """Emit the current label's accumulated db bytes, re-ordered per its
        class. Always as dc.b, so offsets cannot move."""
        b = self.pend
        if not b:
            return
        cls = self.pend_class
        lay = LAYOUTS.get(self.pend_label)
        if lay:
            # RECORD LAYOUT (overrides_layout.tsv): a whole struct image that
            # is copied out by address (object templates). Only the listed
            # multi-byte fields are byte-reversed, everything else stays raw;
            # the label keeps its storage class (nobody reads it by name).
            base = self.offset - len(b) - self._label_start
            out = list(b)
            for w, off, cnt in lay:
                for i in range(cnt):
                    a = off + i * w - base
                    if 0 <= a and a + w <= len(out):
                        out[a:a + w] = reversed(out[a:a + w])
            # "+ptr" in the class override forces plain too: a layout label
            # whose fields are also written BY NAME at full width (DATA_007783,
            # 2026-10-16) must not have those writes swapped into x86 order.
            self._cur_kinds.add("plain" if self.pend_addr or (cls or "").endswith("+ptr") else "le")
            for i in range(0, len(out), 16):
                self.f.write("\tdc.b\t%s\n"
                             % ",".join(str(x) for x in out[i:i + 16]))
            self.pend = []
            return
        group = 4 if cls == "dword-only" else 2 if cls == "word-only" else 1
        if group > 1 and len(b) % group:
            self.warns.append("%s: %d db bytes not divisible by %d, left raw"
                              % (self.pend_label, len(b), group))
            group = 1
        self._cur_kinds.add({4: "be32", 2: "be16",
                             1: "plain" if self.pend_addr else "le"}[group])
        out = []
        for i in range(0, len(b), group):
            out.extend(reversed(b[i:i + group]))
        for i in range(0, len(out), 16):
            self.f.write("\tdc.b\t%s\n"
                         % ",".join(str(x) for x in out[i:i + 16]))
        self.pend = []

    def label(self, name):
        self.flush()
        self._close_label()
        self.pend_label = name
        self._cur_kinds = set()
        self._label_start = self.offset
        self.f.write("\txdef\t%s\n%s:\n" % (name, name))

    def set_class(self, cls, addr=0):
        self.pend_class = cls
        self.pend_addr = addr

    def bytes_(self, bb):
        self.pend.extend(bb)
        self.offset += len(bb)

    def longs(self, items, refs, defined):
        self.flush()
        if self.offset % 2:
            # Verified: vasm does NOT pad dc.l (a dc.b/dc.l/dc.b file
            # assembles to 6 bytes), so the layout holds; the 68020 -- and
            # LoadSeg running on it -- handles the unaligned longword and
            # its relocation. Counted for the report, not fatal.
            self.odd_longs += 1
        vals = []
        for it in items:
            k, v = item_value(it)
            if k == "num":
                vals.append(str(v) if -2**31 <= v < 2**31 else str(v - 2**32))
            elif k == "sym":
                s = canon(v)
                vals.append(s)
                if s not in defined:
                    refs.add(s)
            else:
                raise SystemExit("FATAL: string in dd near %s" % self.pend_label)
        for i in range(0, len(vals), 8):
            self.f.write("\tdc.l\t%s\n" % ",".join(vals[i:i + 8]))
        self.offset += 4 * len(items)
        self._cur_kinds.add("be32")

    def space(self, n):
        # Uninitialised bytes carry no order of their own -- the storage the
        # manifest declares is a CONVENTION the translated accesses must all
        # follow, chosen per the label's observed access class:
        #   dword-only / word-only -> be32/be16: every access is native-width,
        #     nothing swaps, nothing pays;
        #   address-taken -> plain (pointer readers get no correction, so
        #     named accesses must match);
        #   everything else -> le: byte accesses stay free and the MOV swap
        #     rules make the wider ones consistent, which is what reproduces
        #     the sloppy mixed-width jjffe variables (a byte-written,
        #     dword-read last-scancode is x86-legal, and 'le' keeps it so).
        self.flush()
        self.f.write("\tds.b\t%d\n" % n)
        self.offset += n
        if self.pend_class == "dword-only":
            self._cur_kinds.add("be32")
        elif self.pend_class == "word-only":
            self._cur_kinds.add("be16")
        else:
            self._cur_kinds.add("plain" if self.pend_addr else "le")


def collect_defined(paths):
    defined = set()
    for p in paths:
        for l in io.open(p, encoding="latin-1"):
            m = _LABEL.match(l)
            if m:
                defined.add(m.group(1))
    return defined


def convert(src, dst, section, classes, defined):
    refs = set()
    body = io.StringIO()
    em = Emitter(body)
    nline = 0
    for raw in io.open(src, encoding="latin-1"):
        nline += 1
        code = raw.split(";", 1)[0].rstrip()
        if not code.strip():
            continue
        m = _LABEL.match(code)
        if m:
            name = m.group(1)
            em.label(name)
            em.set_class(*classes.get(name, ("raw", 0)))
            rest = code[m.end():].strip()
            if not rest:
                continue
            code = "\t" + rest       # label + directive on one line
        t = code.strip()
        m = re.match(r"^(db|dw|dd)\s+(.*)$", t)
        if m:
            kind, rest = m.group(1), m.group(2)
            items = split_items(rest)
            if kind == "dd":
                em.longs(items, refs, defined)
            else:
                bb = []
                for it in items:
                    k, v = item_value(it)
                    if k == "str":
                        bb.extend(v)
                    elif k == "num":
                        n = v & (0xFF if kind == "db" else 0xFFFF)
                        if kind == "db":
                            bb.append(n)
                        else:
                            bb.extend([n & 0xFF, n >> 8])   # LE pair; the
                            # class-based flush re-orders word-only labels
                    else:
                        raise SystemExit("FATAL: symbol in %s at %s:%d"
                                         % (kind, src, nline))
                em.bytes_(bb)
            continue
        m = re.match(r"^resb\s+(\d+)$", t)
        if m:
            em.space(int(m.group(1)))
            continue
        m = re.match(r"^times\s+(\d+)\s+db\s+(\S+)$", t)
        if m:
            k, v = item_value(m.group(2))
            em.bytes_([v & 0xFF] * int(m.group(1)))
            continue
        m = re.match(r"^times\s+(\d+)\s+dd\s+(\S+)$", t)
        if m:
            k, v = item_value(m.group(2))
            if k != "num":
                raise SystemExit("FATAL: times dd with a symbol at %s:%d"
                                 % (src, nline))
            # Appended in LITTLE-endian byte order like every other pend
            # byte: the class-based flush re-orders dword-only labels to
            # big-endian, and raw labels stay byte-identical to the x86.
            one = [v & 0xFF, (v >> 8) & 0xFF,
                   (v >> 16) & 0xFF, (v >> 24) & 0xFF]
            em.bytes_(one * int(m.group(1)))
            continue
        m = re.match(r"^(GLOBAL|EXTERN|SECTION|ALIGN|BITS|CPU|%\w+)\b", t, re.I)
        if m:
            continue
        raise SystemExit("FATAL: %s:%d unhandled line: %r" % (src, nline, t))
    em.flush()
    em._close_label()

    # The storage manifest: what byte order the translator may assume behind
    # every label, plus its size -- the "runtime half" of the data-typing
    # pipeline reads this. Zero-size labels alias the NEXT label's bytes and
    # inherit its storage.
    order = [l for l in em.storage]
    for i in range(len(order) - 2, -1, -1):
        st, sz = em.storage[order[i]]
        if sz == 0:
            em.storage[order[i]] = (em.storage[order[i + 1]][0], 0)
    man = dst[:-2] + ".storage.tsv"
    with io.open(man, "w", encoding="utf-8", newline="\n") as f:
        f.write("label\tstorage\tbytes\n")
        for l in order:
            st, sz = em.storage[l]
            f.write("%s\t%s\t%d\n" % (l, st, sz))

    with io.open(dst, "w", encoding="utf-8", newline="\n") as f:
        f.write("; GENERATED by tools/datamap/convert_static.py from %s\n"
                "; Big-endian data image, offsets byte-identical to the x86.\n"
                % os.path.basename(src))
        for r in sorted(refs):
            f.write("\txref\t%s\n" % r)
        f.write("\tsection\t%s,data\n\n" % section)
        f.write(body.getvalue())
    for w in em.warns[:20]:
        print("WARN:", w)
    if len(em.warns) > 20:
        print("... %d warnings total" % len(em.warns))
    print("%s: %d bytes, %d external refs, %d odd-offset dc.l -> %s"
          % (os.path.basename(src), em.offset, len(refs), em.odd_longs, dst))


def main():
    os.makedirs(OUT, exist_ok=True)
    classes = load_classes()
    dat = os.path.join(JJ, "ffedat.asm")
    bmp = os.path.join(JJ, "ffebmp.asm")
    defined = collect_defined([dat, bmp])
    convert(dat, os.path.join(OUT, "ffedat_be.s"), "ffedat", classes, defined)
    convert(bmp, os.path.join(OUT, "ffebmp_be.s"), "ffebmp", classes, defined)


if __name__ == "__main__":
    main()
