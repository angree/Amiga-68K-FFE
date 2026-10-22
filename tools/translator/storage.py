#!/usr/bin/env python3
r"""The storage manifest -- the runtime half of the data-typing pipeline.

`frame.py` answers "what byte order does this STACK slot hold" for sub-word
stack accesses. This module answers the same question for STATIC data: for
every label the converters emit, what byte order stands behind it, so that
`lower.ea()` can either prove an access right, correct its displacement, or
refuse it loudly. Found the hard way on 2026-07-29: the scancode->ASCII table
converted as dwords turned the splash's 'y' into 'u', and a byte-written,
dword-read last-scancode variable read as 0x15000000 and sent an index 350 MB
out of its table -- the nondeterministic-boot bug.

Storage kinds, written by tools\datamap\convert_static.py and
tools\datamap\extract_support.py next to the data they describe:

    be32   every aligned 4-byte group behind the label is big-endian
           (dc.l content, or a dword-only-classified blob swapped in 4s).
           dword accesses are plain; sub-word accesses need the frame.py
           displacement flip; RUNTIME-indexed sub-word accesses are refused.
    be16   pairs are big-endian; word accesses plain, byte accesses flipped.
    le     bytes exactly as on the x86. Byte accesses (indexed included) are
           plain; word/dword accesses need an explicit byte swap, which only
           the MOV rules implement -- everything else refuses.
    plain  the label is ADDRESS-TAKEN and its bytes are in x86 order:
           computed-pointer readers get no correction, so named accesses must
           not be corrected either. Width-consistent code is right through
           both routes; a mixed-width field shared between a pointer and a
           name is the residual risk only the runtime access log can retire.
    split  the label mixes dc.l content and raw bytes; nothing is provable.

A label absent from every manifest gets None, and callers treat that as
'plain' -- the exact pre-manifest behaviour, so nothing silently changes for
symbols this pipeline does not describe.
"""
import io
import os

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

MANIFESTS = [
    os.path.join(ROOT, "build", "data-be", "ffedat_be.storage.tsv"),
    os.path.join(ROOT, "build", "data-be", "ffebmp_be.storage.tsv"),
    os.path.join(ROOT, "build", "xlat-full", "support_data.storage.tsv"),
]

_map = None
_layout = None      # label -> (file_idx, ordinal); files are contiguous runs


def _load():
    global _map, _layout
    _map = {}
    _layout = {}
    _files = []
    for fi, p in enumerate(MANIFESTS):
        if not os.path.exists(p):
            _files.append([])
            continue
        run = []
        with io.open(p, encoding="utf-8") as f:
            next(f)
            for l in f:
                parts = l.rstrip("\n").split("\t")
                if len(parts) >= 3:
                    _map[parts[0]] = (parts[1], int(parts[2]))
                    run.append(parts[0])
        _files.append(run)
    # ffedat/ffebmp are emitted with NO padding ever, so file order IS memory
    # layout and a displacement can be walked forward across labels. The
    # support file inserts `even` between labels, so walking it is NOT valid
    # -- its labels are islands (file_idx None marks that).
    for fi, run in enumerate(_files):
        island = "support" in os.path.basename(MANIFESTS[fi])
        for i, lab in enumerate(run):
            _layout[lab] = (None if island else fi, i)
    _files_g[0] = _files


_files_g = [None]


def kind(sym):
    """(storage, size_in_bytes) for a converted label, or None if no manifest
    describes it (callers treat None as 'plain')."""
    if _map is None:
        _load()
    return _map.get(sym)


_sites = None

SITES_TSV = os.path.join(ROOT, "tools", "datamap", "le_read_sites.tsv")


LOBYTE_TSV = os.path.join(os.path.dirname(os.path.dirname(
    os.path.dirname(os.path.abspath(__file__)))), "tools", "datamap",
    "lo_byte_sites.tsv")
_lobyte = None


def lo_byte_site(line):
    """True if this ffe.asm LINE reads the LOW BYTE of a 16-bit field through
    a POINTER, so its displacement must move by +1 on the 68k.

    NOT the same as le_site(). There is nothing to byte-swap in a single byte:
    x86 puts a word's low half at offset +0 and the 68k puts it at +1, so
    `mov al,[eax]` on a word field has to become `move.b (1,a0),d0`. The
    operand carries no symbol, so the storage manifest cannot see it and the
    site has to be named by line, exactly as le_read_sites.tsv does.

    Found in FUNC_001330_DPalBuild (ffe.asm:83332-83355), which builds every
    palette entry the 3D view uses: it reads the low byte of six word-sized
    colour-box bounds. Those bounds hold 0..0x10, so the HIGH byte -- which is
    what the 68k read instead -- is always ZERO. All three channels were
    computed from zero and every palette entry came out R=G=B, which is
    exactly why the 3D viewport was grey while the 2D console was in full
    colour (measured: 10 distinct indices in the viewport, every one of them
    a pure grey).

    GENERALISED 2026-08-03. The original list held one shape only -- a BYTE
    inside a WORD, delta +1 -- and returned a bool. A second shape turned up in
    FUNC_000832, which packs the player's system id into ONE dword field at
    [DATA_008804+0x27c] through three differently-sized accesses:

        mov dx,[ebx+0x27c]  /  mov [ebx+0x27c],ax     a WORD in a DWORD  -> +2
        mov dl,[ebx+0x27f]  /  mov [ebx+0x27f],al     BYTE 3 of a DWORD  -> -3

    so the table now carries an optional explicit delta in column 2. A row
    without one still means +1, which is why the six FUNC_001330 sites and every
    object file built from them are unchanged.

    Returns the displacement delta (an int) or 0 when the line is not listed.
    Callers must treat any non-zero value as "shift the displacement", not as a
    flag -- the old `if lo_byte_site(...)` test is still correct because 0 is
    falsy, but it can no longer assume the shift is +1.
    """
    global _lobyte
    if _lobyte is None:
        _lobyte = {}
        if os.path.exists(LOBYTE_TSV):
            for l in io.open(LOBYTE_TSV, encoding="utf-8"):
                if l.startswith("#") or not l.strip():
                    continue
                cols = l.split("\t")
                num = int(cols[0])
                delta = 1
                if len(cols) > 1:
                    c = cols[1].strip()
                    # Column 2 is the delta only when it is a signed integer;
                    # the original two-column rows put free text there.
                    try:
                        delta = int(c)
                    except ValueError:
                        delta = 1
                _lobyte[num] = delta
    return _lobyte.get(line, 0)


def le_site(line):
    """Width (2 or 4) if this ffe.asm LINE is a registered pointer-based
    access to LE static data that must byte-swap at the site, else None.
    The site list exists because pointer reads carry no symbol for the
    manifest to type, and because the model descriptor mixes widths on one
    field (see the header of le_read_sites.tsv). The caller must check the
    instruction's actual width against the returned one and fail loudly on
    mismatch -- the guard against the list drifting from the disassembly."""
    global _sites
    if _sites is None:
        _sites = {}
        if os.path.exists(SITES_TSV):
            for l in io.open(SITES_TSV, encoding="utf-8"):
                if l.startswith("#") or not l.strip():
                    continue
                p = l.rstrip("\n").split("\t")
                if len(p) >= 2:
                    # `4p' = a dword over a big-endian WORD PAIR reached through
                    # a pointer: one SWAP of the halves, no byte rotates (the
                    # pointer-site twin of the manifest's be16 kind).
                    if p[1].strip() == "4p":
                        _sites[int(p[0])] = 4
                        _pair_sites.add(int(p[0]))
                    else:
                        _sites[int(p[0])] = int(p[1])
    return _sites.get(line)


_pair_sites = set()


def le_site_kind(line):
    """'be16' for a `4p' site, 'le' for any other registered site, else None."""
    if le_site(line) is None:
        return None
    return "be16" if line in _pair_sites else "le"


# BYTES INSIDE A BIG-ENDIAN 32-BIT RECORD THAT STARTS AT AN EARLIER LABEL.
#
# resolve() walks FORWARD only, so it can map DATA_007691+0x48 onto a later
# label but never a later label back onto the record that encloses it. When the
# record base is an EARLIER label the manifest cannot describe the access at
# all -- the same hole DATA_007811 fell through. This table closes it by naming
# the member label and its x86 byte offset within the record explicitly.
#
# Each entry must cite its evidence. An entry without one is a guess.
#
#   DATA_008804 is the game clock: `resb 1' at ffedat.asm:28926 followed by
#   DATA_008805 (`resb 2') and DATA_008806 (`resb 1') -- FOUR bytes over THREE
#   labels. It is written and read only as a dword: 29 accesses to DATA_008804,
#   all `mov eax,[DATA_008804]', plus the single initialiser `mov dword
#   [DATA_008804],0x80000000' (ffe.asm:14928), and the Amiga platform layer
#   declares it `long game_clock[2]' (ffe_video.c) -- a native 68k longword, so
#   the record is stored BIG-ENDIAN on our machine and the dword accesses need
#   no correction.
#
#   The six accesses below are the only ones that reach INTO it, and every one
#   is a read -- there is no byte write anywhere to either member label:
#       test byte [DATA_008806],0x80   ffe.asm:45992, 46073, 46200
#       test byte [DATA_008806],0x8    ffe.asm:49352
#       test byte [DATA_008805],0x80   ffe.asm:20569
#       test byte [DATA_008805],0x1    ffe.asm:72528
#   On x86 DATA_008806 is the record's TOP byte, so `,0x80' tests BIT 31 -- and
#   bit 31 is exactly the bit the initialiser sets, which is what proves the
#   four bytes are one 32-bit quantity rather than adjacent flags. Big-endian,
#   bits 24..31 live at offset +0, so the access must move to the other end.
#   Left uncorrected it tested bit 7 of a counter, i.e. a flag that flips every
#   128 ticks where the original has a stable one.
BE32_MEMBER = {
    "DATA_008805": ("DATA_008804", 1),   # spans x86 bytes +1 and +2
    "DATA_008806": ("DATA_008804", 3),   # the top byte on x86
    # DATA_008958 (3 bytes) + DATA_008959 (1) are ONE dword, the hyperspace
    # target system id, written whole (mov [ebx+0x27c],eax, ffe.asm:56630) and
    # its top byte read by name (mov al,[DATA_008959], 58581: shr 2 / and 0x3f
    # = the star index in bits 26..31). Stored plain = native big-endian.
    "DATA_008959": ("DATA_008958", 3),
}


def be32_member(sym):
    """(record base label, x86 byte offset) when `sym' names a byte inside a
    big-endian 32-bit record that begins at an EARLIER label, else None."""
    return BE32_MEMBER.get(sym)


def resolve(sym, disp):
    """The label actually COVERING byte sym+disp, and the displacement local
    to it. ffedis loves addressing one label through an earlier one
    ([DATA_007691+0x48] runs three labels deep), and it is the covering
    label's storage that decides how the access translates.

    Returns (label, local_disp) or None when: the symbol is unknown to the
    manifests, the displacement is negative (walking backward would need the
    whole reverse layout and no access has needed it), or the walk would
    leave a support-file island (those have alignment padding between labels,
    so file order is not memory layout there)."""
    if _map is None:
        _load()
    if sym not in _map:
        return None
    if disp < 0:
        return None if disp else (sym, 0)
    lab = sym
    local = disp
    while True:
        st, size = _map[lab]
        if local < size or (local == 0 and size == 0):
            return (lab, local)
        fi, i = _layout[lab]
        if fi is None:
            return None                      # support island: cannot walk
        run = _files_g[0][fi]
        if i + 1 >= len(run):
            return None                      # runs off the end of the file
        if size == 0 and local == 0:
            return (lab, 0)
        local -= size
        lab = run[i + 1]
