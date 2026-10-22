#!/usr/bin/env python3
"""Read ref\\jjffe\\ffe.asm and hand back one function as an instruction list.

ffe.asm is 2.5 MB of NASM output from JJ's own disassembler, so the syntax it
uses is narrow and regular: one instruction per line, two tabs of indent,
function labels in column 0, local `JUMP_nnnnnn:` labels indented by one, and a
`; Pos = xxxx` trailer on every label. Nothing here tries to be a general NASM
parser -- it is a reader for the exact dialect ffedis.cpp emits.

The one rule that outranks everything else: **anything not understood raises**.
A translator that guesses produces a silent miscompile somewhere in 113,000
instructions, and that is the single failure mode this project cannot afford to
have. Every message carries the ffe.asm line number and the original text so the
refusal points straight at the source line that needs work.
"""
import re
from dataclasses import dataclass, field
from typing import List, Optional, Union


class TranslationError(Exception):
    """Anything the translator does not understand, at any stage."""


def fail(line, text, msg):
    raise TranslationError(f"ffe.asm:{line}: {msg}\n    {text.strip()}")


# ---- operands ---------------------------------------------------------------

REG32 = ("eax", "ecx", "edx", "ebx", "esp", "ebp", "esi", "edi")
REG16 = ("ax", "cx", "dx", "bx", "sp", "bp", "si", "di")
REG8 = ("al", "cl", "dl", "bl", "ah", "ch", "dh", "bh")

SIZE_KEYWORDS = {"byte": 1, "word": 2, "dword": 4, "qword": 8}
BRANCH_KEYWORDS = ("near", "short", "far")

_FUNC_SYM = re.compile(r"^(FUNC_\d+)(?:_.*)?$")


def canon(sym):
    """The one spelling of a game function's symbol: the bare FUNC_nnnnnn.

    ffe.asm spells 278 of the 1,621 functions with a name attached
    (`FUNC_001601_DrawHLine`) and the other 1,343 without. Both spellings are
    the SAME function, so both must produce the same 68k symbol -- otherwise
    which one you get depends on how the function was spelled on the command
    line, and a caller translated from ffe.asm (which always uses the long
    form) fails to link against a callee translated from a bare request.

    That is not hypothetical: FUNC_001601 was translated as `FUNC_001601` and
    FUNC_001600_DrawHLineClip then emitted `jsr FUNC_001601_DrawHLine`. The
    bare form wins because it is the one every function has.

    Anything that is not a FUNC_ symbol -- JUMP_ labels, DATA_ labels, the
    platform's own underscored names -- is returned untouched.
    """
    m = _FUNC_SYM.match(sym)
    return m.group(1) if m else sym


@dataclass(frozen=True)
class Reg:
    """A general-purpose register. `size` is 4/2/1; `high` marks ah/bh/ch/dh."""
    name: str
    size: int
    high: bool = False

    def __str__(self):
        return self.name


@dataclass(frozen=True)
class Imm:
    """An immediate. `value` is kept signed; NASM writes both `-0x1` and the
    equivalent `0xffffffff` and the two must land on the same number."""
    value: int
    size_hint: Optional[int] = None

    def __str__(self):
        return str(self.value)


@dataclass(frozen=True)
class Mem:
    """`[base + index*scale + sym + disp]`, with an optional size prefix.

    Every field is optional because ffe.asm uses every combination: plain
    `[ebp+0x8]`, absolute `[DATA_008835]`, and scaled `[eax*4+DATA_000012]` for
    the 35 jump tables."""
    size: Optional[int] = None
    base: Optional[str] = None
    index: Optional[str] = None
    scale: int = 1
    disp: int = 0
    sym: Optional[str] = None
    # Set by frame.apply() on a sub-word stack access whose slot it has PROVED
    # to be a 4-byte scalar: `disp` then already carries the big-endian
    # displacement rather than the x86 one. lower.ea() refuses every sub-word
    # stack access that does not carry this, which is what stops the translator
    # silently reading the wrong byte of a pushed longword. See frame.py.
    swapped: bool = False

    def __str__(self):
        parts = []
        if self.base:
            parts.append(self.base)
        if self.index:
            parts.append(f"{self.index}*{self.scale}")
        if self.sym:
            parts.append(self.sym)
        if self.disp or not parts:
            parts.append(hex(self.disp))
        return "[" + "+".join(parts) + "]"


@dataclass(frozen=True)
class Sym:
    """A bare label: a call/jump target, or an address used as an immediate."""
    name: str

    def __str__(self):
        return self.name


Operand = Union[Reg, Imm, Mem, Sym]


# ---- items ------------------------------------------------------------------

@dataclass
class Insn:
    line: int
    text: str
    mnem: str
    ops: List[Operand] = field(default_factory=list)


@dataclass
class Label:
    line: int
    name: str


@dataclass
class Table:
    """An embedded data label inside a function's `SECTION .data` block.
    Each label's run must be internally uniform and is carried IN PLACE:

      entries -- a jump table, nothing but `dd <symbol>`; the emitter writes
                 dc.l, which is PLAN 5's invariant at work -- the 68k
                 assembler puts 68k addresses there, so no x86 address ever
                 exists at run time.
      bytes_  -- a byte lookup blob, nothing but `db`; emitted as dc.b,
                 byte-identical, so the indexed byte reads next to it keep
                 their offsets (FUNC_000037's opcode-class table).

    A label mixing the two inside itself is still refused."""
    line: int
    name: str
    entries: List[str] = field(default_factory=list)
    bytes_: List[int] = field(default_factory=list)


@dataclass
class Function:
    name: str          # the label as it appears, e.g. FUNC_001601_DrawHLine
    base: str          # the FUNC_nnnnnn part, used for file and symbol names
    line: int
    items: List[Union[Insn, Label, Table]] = field(default_factory=list)
    padding: int = 0   # trailing alignment NOPs dropped; see _strip_padding()

    @property
    def insns(self):
        return [i for i in self.items if isinstance(i, Insn)]


# ---- number and operand parsing ---------------------------------------------

_NUM = re.compile(r"^[+-]?(0x[0-9a-fA-F]+|[0-9]+)$")
_IDENT = re.compile(r"^[A-Za-z_][A-Za-z0-9_]*$")


def parse_number(tok):
    """NASM integers as ffedis writes them: decimal, 0x hex, optional sign --
    plus the flat constant expressions the HAND-WRITTEN jjffe support code
    uses (`158*320`, `200-158` in FlipScreenLow): products and differences of
    plain numbers, evaluated left to right (NASM precedence is irrelevant at
    this depth -- the game has no mixed `a-b*c`). Returns None if `tok` is
    not a number at all."""
    tok = tok.strip()
    if not _NUM.match(tok):
        m = re.match(r"^(0x[0-9a-fA-F]+|[0-9]+)"
                     r"(\s*[*-]\s*(0x[0-9a-fA-F]+|[0-9]+))+$", tok)
        if not m:
            return None
        parts = re.findall(r"([*-]?)\s*(0x[0-9a-fA-F]+|[0-9]+)", tok)
        v = None
        for sign, num in parts:
            n = int(num, 16) if num.lower().startswith("0x") else int(num, 10)
            if v is None:
                v = n
            elif sign == "*":
                v *= n
            else:
                v -= n
        return v
    neg = tok.startswith("-")
    tok = tok.lstrip("+-")
    v = int(tok, 16) if tok.lower().startswith("0x") else int(tok, 10)
    return -v if neg else v


def as_signed32(v):
    v &= 0xFFFFFFFF
    return v - 0x100000000 if v & 0x80000000 else v


def parse_reg(tok):
    t = tok.lower()
    if t in REG32:
        return Reg(t, 4)
    if t in REG16:
        return Reg(t, 2)
    if t in REG8:
        return Reg(t, 1, high=t[1] == "h")
    return None


def parse_mem(inner, size, line, text):
    """`base + index*scale + symbol + disp`, terms in any order and any sign."""
    base = index = sym = None
    scale = 1
    disp = 0

    # Segment overrides: jjffe runs in the DOS4GW flat model where ES, DS, SS
    # and CS all alias the same 4 GB, so `[es:ecx]` is `[ecx]` -- 13 sites,
    # all leftovers of a rep-stos idiom the disassembly unrolled. FS/GS could
    # legitimately differ and stay refused.
    m = re.match(r"^(es|ds|ss|cs):\s*(.*)$", inner.strip(), re.I)
    if m:
        inner = m.group(2)
    elif re.match(r"^[fg]s:", inner.strip(), re.I):
        fail(line, text, "FS/GS segment override -- not the flat-model alias")

    for sign, term in re.findall(r"([+-]?)\s*([^+-]+)", inner):
        term = term.strip()
        if not term:
            continue
        neg = sign == "-"

        if "*" in term:
            a, b = (x.strip() for x in term.split("*", 1))
            r, s = (parse_reg(a), parse_number(b))
            if r is None or s is None:
                r, s = (parse_reg(b), parse_number(a))
            if r is None or s is None or s not in (1, 2, 4, 8):
                fail(line, text, f"memory term {term!r} is not register*scale")
            if index is not None:
                fail(line, text, "two scaled index registers in one operand")
            index, scale = r.name, s
            continue

        r = parse_reg(term)
        if r is not None:
            if neg:
                fail(line, text, f"negated register {term!r} in a memory operand")
            if base is None:
                base = r.name
            elif index is None:
                index = r.name
            else:
                fail(line, text, "three registers in one memory operand")
            continue

        n = parse_number(term)
        if n is not None:
            disp += -n if neg else n
            continue

        if _IDENT.match(term):
            if sym is not None:
                fail(line, text, "two symbols in one memory operand")
            if neg:
                fail(line, text, f"negated symbol {term!r} in a memory operand")
            sym = term
            continue

        fail(line, text, f"cannot parse memory term {term!r}")

    return Mem(size=size, base=base, index=index, scale=scale,
               disp=as_signed32(disp), sym=sym)


def parse_operand(tok, line, text):
    t = tok.strip()
    size = None

    # Strip the leading keywords NASM allows: a size override, and near/short
    # on control transfers. Both may appear, e.g. `jmp near [eax*4+DATA_x]`.
    while True:
        head = t.split(None, 1)
        if len(head) != 2:
            break
        kw = head[0].lower()
        if kw in SIZE_KEYWORDS:
            size = SIZE_KEYWORDS[kw]
        elif kw in BRANCH_KEYWORDS:
            pass
        else:
            break
        t = head[1].strip()

    if t.startswith("[") and t.endswith("]"):
        return parse_mem(t[1:-1], size, line, text)

    r = parse_reg(t)
    if r is not None:
        return r

    n = parse_number(t)
    if n is not None:
        return Imm(as_signed32(n), size)

    # NASM character constant: `cmp al,'y'` -- a one-byte immediate. Only the
    # single-character form appears in the source (jjffe's hand-written
    # splash-acceptance check).
    if len(t) == 3 and t[0] == t[2] == "'":
        return Imm(ord(t[1]), size)

    if _IDENT.match(t):
        return Sym(t)

    fail(line, text, f"cannot parse operand {tok.strip()!r}")


def split_operands(rest):
    """ffedis never puts a comma inside brackets, so a plain split is safe --
    but assert it rather than assume it."""
    return [p for p in rest.split(",")] if rest.strip() else []


# ---- the file ---------------------------------------------------------------

_LABEL = re.compile(r"^(\s*)([A-Za-z_][A-Za-z0-9_]*):")

# NASM local labels -- `.exclusive:` / `jz NEAR .noptrrefresh` -- appear only
# in jjffe's hand-written support functions (29 of them), never in the
# disassembled game code. They are MANGLED into ordinary unique labels
# (`LOC_001630_exclusive`) rather than passed through as vasm locals: the
# emitted file xdef's every JUMP_ label, and a vasm local's scope would end at
# each of those, silently splitting one x86 label region into several 68k
# ones. The mangle needs no scoping rules at all.
# The colon is OPTIONAL: NASM accepts `.notrans` as a label definition and
# jjffe's hand-written code uses both spellings. The first character may be
# a DIGIT (`.50hzloop`) -- the mangled LOC_ name stays a valid identifier
# either way, and the negative lookbehind in the reference pattern keeps
# `0.5`-style numerals from matching.
_LOCAL_LABEL = re.compile(r"^\s*\.(\w+):?\s*$")
_LOCAL_REF = re.compile(r"(?<![\w$])\.(\w+)\b")


def _loc(base, name):
    return "LOC_%s_%s" % (base[len("FUNC_"):], name)


def _strip_comment(s):
    return s.split(";", 1)[0].rstrip()


_SECT_DATA = re.compile(r"^\s*SECTION\s+\.data\s*$", re.I)
_SECT_TEXT = re.compile(r"^\s*SECTION\s+\.text\s*$", re.I)
_DD_SYM = re.compile(r"^dd\s+([A-Za-z_]\w*)\s*$")


def _read_embedded_block(lines, j0, lineno, raw):
    """`SECTION .data` ... `SECTION .text` inside a function. Accepts ONLY
    labels followed by `dd <symbol>` runs -- the jump tables -- and returns
    (index just past SECTION .text, [Table, ...]). Anything else in the block
    refuses the whole function: a partially-carried block would silently drop
    data out of the program, which is the one failure mode worse than a
    refusal."""
    tables = []
    cur = None
    j = j0
    while j < len(lines):
        t = _strip_comment(lines[j]).strip()
        if not t:
            j += 1
            continue
        if _SECT_TEXT.match(t):
            if not tables or any(not tb.entries and not tb.bytes_
                                 for tb in tables):
                fail(lineno, raw,
                     "embedded .data block with a label but no content")
            return j + 1, tables
        m = re.match(r"^([A-Za-z_]\w*):\s*$", t)
        if m:
            cur = Table(j + 1, m.group(1), [])
            tables.append(cur)
            j += 1
            continue
        m = _DD_SYM.match(t)
        if m and cur is not None and not cur.bytes_:
            cur.entries.append(m.group(1))
            j += 1
            continue
        m = re.match(r"^db\s+(.+)$", t)
        if m and cur is not None and (not cur.entries
                                      or len(m.group(1).split(",")) < 4):
            # Either a pure byte-lookup label, or a SHORT db run TRAILING a
            # jump table (`dd JUMP_x / db 0xeb,0x48` in FUNC_000718/809):
            # under 4 bytes it cannot be another entry -- it is the stray
            # tail of the disassembler cutting a jmp-short. Kept and emitted
            # after the dc.l entries, so sizes stay honest.
            for item in m.group(1).split(","):
                item = item.strip()
                if re.match(r"^-?0x[0-9a-fA-F]+$", item):
                    cur.bytes_.append(int(item, 16) & 0xFF)
                elif re.match(r"^-?\d+$", item):
                    cur.bytes_.append(int(item) & 0xFF)
                else:
                    fail(j + 1, lines[j],
                         "non-numeric db item in an embedded data label")
            j += 1
            continue
        # Not a pure jump-table block. If it contains NO jump table at all
        # (no `dd JUMP_x` anywhere), it is hand-written variables and blobs
        # -- extract_support.py carries every one of those into
        # support_data.s, so the block is SKIPPED here rather than refused;
        # the symbols resolve at link time. A block that MIXES a jump table
        # with other data still refuses the whole function: skipping it
        # would leave the table's entries as zeros in support_data and the
        # dispatch would jump into nowhere at run time.
        k = j
        has_table = False
        while k < len(lines) and not _SECT_TEXT.match(_strip_comment(lines[k])):
            if re.search(r"\bdd\s+JUMP_\d+", _strip_comment(lines[k])):
                has_table = True
                break
            k += 1
        if has_table or any(tb.entries for tb in tables):
            fail(j + 1, lines[j],
                 "embedded .data block MIXES a jump table with other data -- "
                 "refused whole: the support extractor zeroes table entries, "
                 "so skipping here would make the dispatch jump into nowhere")
        while j < len(lines) and not _SECT_TEXT.match(_strip_comment(lines[j])):
            j += 1
        if j >= len(lines):
            fail(lineno, raw, "SECTION .data with no SECTION .text before EOF")
        return j + 1, []
    fail(lineno, raw, "SECTION .data with no SECTION .text before EOF")


def read_function(path, want):
    """Isolate one function. `want` matches either the bare FUNC_nnnnnn or the
    named form ffe.asm actually uses (FUNC_001601_DrawHLine)."""
    with open(path, "r", errors="replace") as f:
        lines = f.readlines()

    start = None
    label = None
    for i, raw in enumerate(lines):
        m = _LABEL.match(raw)
        if m and not m.group(1):                   # column 0 == function label
            name = m.group(2)
            if name == want or name.startswith(want + "_"):
                start, label = i, name
                break
    if start is None:
        raise TranslationError(f"{path}: no function label matching {want!r}")

    # `base` is the emitted symbol and comes from the LABEL, not from `want`:
    # asking for FUNC_001601 and asking for FUNC_001601_DrawHLine must produce
    # the same object, or the two spellings fail to link against each other.
    fn = Function(name=label, base=canon(label), line=start + 1)

    skip_to = -1
    for i in range(start + 1, len(lines)):
        if i < skip_to:
            continue
        raw = lines[i]
        lineno = i + 1
        code = _strip_comment(raw)

        if not code.strip():
            continue

        if _SECT_DATA.match(code):
            skip_to, tables = _read_embedded_block(lines, i + 1, lineno, raw)
            fn.items.extend(tables)
            continue

        if _SECT_TEXT.match(code):
            # A redundant `SECTION .text` with no .data before it (the source
            # restates the section after some functions). Already in text --
            # a no-op.
            continue

        m = _LABEL.match(code)
        if m:
            if not m.group(1):                     # next function starts here
                break
            fn.items.append(Label(lineno, m.group(2)))
            trailing = code[m.end():].strip()
            if trailing:
                fail(lineno, raw, "instruction on the same line as a label")
            continue

        m = _LOCAL_LABEL.match(code)
        if m:
            fn.items.append(Label(lineno, _loc(fn.base, m.group(1))))
            continue
        code = _LOCAL_REF.sub(lambda mm: _loc(fn.base, mm.group(1)), code)

        # SECTION switches (the 35 jump tables live in .data blocks embedded in
        # ffe.asm) and any other directive: refuse, do not skip. Skipping one
        # would silently drop a jump table out of the translated program.
        #
        # This is tested BEFORE the indentation check, and deliberately: ffedis
        # writes `SECTION .data` in column 0, so the indentation check used to
        # win the race and report "unindented line that is not a label" -- true,
        # but useless. A survey of all 1,615 functions found 43 refusals wearing
        # that message when the real cause was an embedded data block every time.
        word = code.strip().split(None, 1)[0]
        if word.upper() in ("SECTION", "GLOBAL", "EXTERN", "DB", "DW", "DD",
                            "RESB", "RESW", "RESD", "TIMES", "ALIGN", "%INCLUDE"):
            fail(lineno, raw, f"assembler directive {word!r} inside a function "
                              "-- embedded data is not handled yet (the 35 jump "
                              "tables, PLAN 5)")

        # Unindented instruction lines exist -- jjffe's hand-written support
        # code has a handful (`push dword DATA_002308` at column 0). A column-0
        # LABEL was already taken as the next function above, and directives
        # were refused above, so what reaches here is an instruction whose
        # author did not indent it; the mnemonic dispatch below judges it like
        # any other line.

        parts = code.strip().split(None, 1)
        mnem = parts[0].lower()
        rest = parts[1] if len(parts) > 1 else ""
        if "[" in rest and "," in rest:
            # cheap guard on the split-on-comma assumption above
            depth = 0
            for ch in rest:
                if ch == "[":
                    depth += 1
                elif ch == "]":
                    depth -= 1
                elif ch == "," and depth:
                    fail(lineno, raw, "comma inside brackets -- operand split "
                                      "assumption broken")
        ops = [parse_operand(o, lineno, raw) for o in split_operands(rest)]
        fn.items.append(Insn(lineno, code.strip(), mnem, ops))

    if not fn.items:
        raise TranslationError(f"{path}: {label} is empty")
    _strip_padding(fn)
    return fn


def _strip_padding(fn):
    """Drop the alignment NOPs ffedis writes after the final RET.

    THIS IS THE ONE PLACE ANYTHING IS DISCARDED, and it is fenced in three ways
    because the rule everywhere else is that nothing is silently dropped.

    The measurement that produced it: a dry run over all 1,615 functions found
    173 refused with "control falls off the end of the function", and in all 173
    the last instruction is a `nop`. They are not code -- ffedis pads each
    function out to an alignment boundary, and the CFG then sees a NOP that
    falls through into nothing.

    Discarded only when all three hold, so that no reachable instruction can
    ever be removed:

      1. the run is at the very END of the item list and is nothing but `nop`;
      2. the instruction in front of it is `ret` or an unconditional `jmp`, so
         control cannot fall into the run;
      3. no Label sits inside the run, so nothing can branch into it either.

    If any of those fails the NOPs stay, `lower()` has no rule for `nop`, and
    the translator refuses -- which is the correct answer for a NOP that is
    actually reachable.

    `fn.padding` records how many went, so the emitted file can say so rather
    than leaving a reader to wonder where the tail of the function went.
    """
    items = fn.items
    k = len(items)
    while k > 0 and isinstance(items[k - 1], Insn) and items[k - 1].mnem == "nop":
        k -= 1
    if 0 < k < len(items):
        prev = items[k - 1]
        if isinstance(prev, Insn) and prev.mnem in ("ret", "jmp"):
            fn.padding = len(items) - k
            del items[k:]

    # The SECOND padding shape (FUNC_001877): ffedis disassembles the pad
    # bytes before an embedded SECTION .data block as junk instructions
    # (`add [eax],al` is 00 00). The CFG would see them falling INTO the
    # table and refuse. Dropped under the same three fences as the NOP run:
    # the run sits immediately before a Table, the instruction in front of
    # it is `ret` or an unconditional `jmp` (control cannot fall in), and no
    # Label sits inside the run (nothing can branch in -- branches only
    # target labels in this disassembly).
    k = 0
    while k < len(items):
        if not isinstance(items[k], Table):
            k += 1
            continue
        j = k
        while j > 0 and isinstance(items[j - 1], Insn) \
                and items[j - 1].mnem not in ("ret", "jmp"):
            j -= 1
        if (j < k and j > 0 and isinstance(items[j - 1], Insn)
                and items[j - 1].mnem in ("ret", "jmp")):
            fn.padding += k - j
            del items[j:k]
            k = j
        k += 1
