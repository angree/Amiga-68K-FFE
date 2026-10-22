#!/usr/bin/env python3
"""Link the oracle against the game's own object file.

ffe.obj carries all 1,621 FUNC_ symbols as GLOBAL, so calling the original x86
code directly is just a link away -- but the object also has EXTERN references
to the whole platform layer (the 40 ffeapi functions plus five helpers). None of
them are reached when we call a leaf arithmetic function, yet the linker still
demands they resolve. So this generates a stub for every unresolved symbol the
linker complains about, and links again. Any stub that is actually CALLED aborts
loudly rather than returning a plausible zero -- a silent stub would turn a
missing dependency into a wrong answer, which is the one failure mode this whole
apparatus exists to prevent.

THE GAME'S DATA IS NOT GLOBAL, AND ref\\ IS NOT TOUCHED TO FIX THAT. Of the
8,596 DATA_ labels exactly one (DATA_008804) is declared GLOBAL, so a function
that reads game state -- FUNC_001601_DrawHLine reads the back-buffer descriptor
DATA_007773 -- cannot be set up from C against the stock object. The fix is a
three-line NASM wrapper, generated below, that adds the GLOBALs and then
%includes ffe.asm unchanged. Same code, same data, two more exported symbols,
and ref\\jjffe stays pristine. The wrapper object is used for the ORACLE ONLY:
build-pc-ref.py keeps assembling its own ffe.obj, so the reference build the
user plays is unaffected.
"""
import os, re, subprocess, sys, time

ROOT  = r"I:\GITHUB\Amiga_Frontier_2_First_Encouters"
TOOLS = os.path.join(ROOT, "tools")
OUT   = os.path.join(ROOT, "build", "oracle")
M0    = os.path.join(ROOT, "build", "m0-pc")
JJ    = os.path.join(ROOT, "ref", "jjffe")
GCC   = os.path.join(TOOLS, "mingw32", "bin", "gcc.exe")
NASM  = os.path.join(TOOLS, "nasm", "nasm.exe")

WRAP_ASM = os.path.join(OUT, "ffe_oracle.asm")
WRAP_OBJ = os.path.join(OUT, "ffe_oracle.obj")

OBJS = [WRAP_OBJ, os.path.join(M0, "ffebmp.obj")]
SRC  = os.path.join(TOOLS, "oracle", "oracle.c")
EXE  = os.path.join(OUT, "oracle.exe")
STUB = os.path.join(OUT, "stubs.c")

# Data labels the oracle has to reach and ffe.asm does not export. Keep this
# list as short as the tests need: every entry is a symbol the linker will then
# resolve for anything, so a typo here becomes a link error rather than a wrong
# answer, but a long list is still a long list.
EXPORTS = [
    "DATA_007773",   # back-buffer descriptor {ptr,x,y,320,200,jump}, ffedat:24815
    "DATA_009226",   # the 64000-byte back buffer it points at, ffedat:28944
    "DATA_007683",   # the GUI hot-rect table, ffedat:23317: 2580 bytes = 129
                     # entries of {x0,y0,x1,y1,id}, terminated by an x0 of -1.
                     # FUNC_001461_GuiAddHotRect2 walks it and appends.
    "DATA_008861",   # `resb 4`, ffedat:28212. The id FUNC_000402/000427 compare
    "DATA_008890",   # their argument against, and the two values they return
    "DATA_009105",   # when it matches. All three are plain 4-byte BSS words.
    "DATA_008568",   # `resb 400`, ffedat:27625 -- 100 ints indexed as
                     # [eax*4+DATA_008568] by FUNC_000093/000094. The
                     # suppressed-base indexed form, read and written.
    # -- the label-address / movzx batch --------------------------------------
    "DATA_008561",   # `resb 24`, ffedat:27611 -- FUNC_000038 treats this as an
                     # array of 36-byte records; record 0 spills into the resb
                     # neighbours, which is how the original lays it out too.
    "DATA_008661",   # `resb 20`, ffedat:27811 -- the FileInfo struct
                     # FUNC_000178 rep-movsd's 24 bytes into (the copy runs 4
                     # bytes into DATA_008662, faithfully on both machines).
    "DATA_007129",   # 1024 initialised bytes, a word-in-dword table read as
                     # [eax*4+DATA_007129] by FUNC_000896. The harness writes
                     # the slots it reads, so the vectors do not depend on the
                     # not-yet-converted static content.
    "DATA_007751",   # 1151 initialised bytes, the char-advance-width table
                     # FUNC_001597 reads bytewise. Same harness-fills-it rule.
    "DATA_007830",   # dd + dd + dd + resb 4, ffedat -- the four words
    "DATA_007831",   # FUNC_001785 writes: an int, an int, a POINTER (printed
    "DATA_007832",   # as an offset from DATA_009255, never as a value) and
    "DATA_009255",   # the int that pointer aims at.
    # -- the base+symbol batch ------------------------------------------------
    "DATA_007770",   # 4096 initialised bytes; FUNC_001327 reads it as
    "DATA_009139",   # [edx+DATA_007770]. resb 4096, read as [edx+DATA_009139].
    "DATA_009140",   # `resb 1` -- FUNC_001327's [eax+DATA_009140] runs into
                     # the resb neighbours by design; the harness keeps the
                     # index under 8 and fills those bytes itself.
    # -- repne scasb ----------------------------------------------------------
    "DATA_008919",   # resb 22 and resb 26: the two commander-name buffers
    "DATA_008920",   # FUNC_000211 strcpy's into via REPNE SCASB + REP MOVS.
    "DATA_007977",   # 6144 initialised bytes: the sqrt word table FUNC_001467
                     # reads at [eax*2+DATA_007977]. Harness fills it whole.
    "DATA_008669",   # 4 bytes: the scramble-key seed FUNC_000168 adds to
                     # 0x12345678. Harness sets it per vector.
    # -- the jump-table carrier FUNC_000021 reads these game variables -------
    "DATA_008911", "DATA_008888", "DATA_008922", "DATA_008885",
    "DATA_008804",   # only its first dword is read (the div arm)
    "DATA_008807", "DATA_008874", "DATA_008862", "DATA_008870",
    "DATA_008892", "DATA_008923", "DATA_008924", "DATA_008925",
    "DATA_008926",
    # -- the LEA symbol+scaled-index carriers --------------------------------
    "DATA_009004",   # resb 529: the 24-byte-record list FUNC_000517 deletes
                     # from (lea eax,[eax*8+DATA], data-register path).
    "DATA_008659",   # FileInfo count and the 1536-byte FileInfo array
    "DATA_008665",   # FUNC_000199 appends to (lea edi,[eax*8+DATA], the
                     # flag-exact address-register path).
    "DATA_007687",   # the scancode map and the key-state byte table
    "DATA_007691",   # FUNC_001406_KeybGetKeyState reads; harness-filled.
    # -- the star-system lookup ----------------------------------------------
    "DATA_008912",   # the two star temperatures FUNC_000871 hands back,
    "DATA_008913",   # swapped or not depending on which of the two star
                     # classes it decides is the primary. Harness-filled, so
                     # the vectors do not depend on live game state.
]

CFLAGS = ["-m32", "-O1", "-Wall", "-static-libgcc"]


def build_wrapper():
    """nasm the GLOBAL-adding wrapper around ffe.asm. ~60 s, so it is skipped
    when nothing it depends on has moved."""
    text = ("; GENERATED by tools\\oracle\\build-oracle.py -- do not edit.\n"
            "; The oracle's copy of the game object: identical code and data,\n"
            "; plus the exports below. ref\\jjffe is never modified; ffe.asm and\n"
            "; ffedat.asm are reached through nasm's -i include path.\n"
            + "".join("GLOBAL %s\n" % s for s in EXPORTS)
            + '%include "ffe.asm"\n')

    old = None
    if os.path.exists(WRAP_ASM):
        with open(WRAP_ASM) as f:
            old = f.read()
    if old != text:
        with open(WRAP_ASM, "w") as f:
            f.write(text)

    deps = [WRAP_ASM, os.path.join(JJ, "ffe.asm"), os.path.join(JJ, "ffedat.asm")]
    if os.path.exists(WRAP_OBJ):
        t = os.path.getmtime(WRAP_OBJ)
        if all(os.path.getmtime(d) <= t for d in deps):
            print("[skip] %s (up to date)" % os.path.basename(WRAP_OBJ))
            return True

    print("nasm %s (this is the ~60 s step) ..." % os.path.basename(WRAP_ASM))
    t0 = time.time()
    p = subprocess.run([NASM, "-f", "win32", "-i", JJ + os.sep,
                        WRAP_ASM, "-o", WRAP_OBJ],
                       capture_output=True, text=True, errors="replace")
    if p.returncode != 0:
        print("nasm failed:")
        print((p.stderr or p.stdout)[:3000])
        return False
    print("  ok, %.0f s -> %s" % (time.time() - t0, WRAP_OBJ))
    return True


def link(extra):
    cmd = [GCC] + CFLAGS + ["-o", EXE, SRC] + extra + OBJS
    return subprocess.run(cmd, capture_output=True, text=True, errors="replace")


def missing_from(err):
    """Undefined symbols, as ld names them. On win32 the object's `_foo`
    becomes `foo` in ld's message about a C-visible name, so strip one
    leading underscore when generating the stub's C name."""
    names = set()
    for m in re.finditer(r"undefined reference to [`'\"]([A-Za-z_][\w@]*)", err):
        names.add(m.group(1))
    return names


def main():
    os.makedirs(OUT, exist_ok=True)
    for t, n in ((NASM, "nasm"), (GCC, "gcc")):
        if not os.path.exists(t):
            print(f"MISSING TOOL: {n} at {t}")
            return 1
    if not build_wrapper():
        return 1
    for o in OBJS:
        if not os.path.exists(o):
            print(f"missing {o} -- run build\\build-pc-ref.py first")
            return 1

    p = link([])
    if p.returncode == 0:
        print("linked with no stubs needed")
    else:
        names = missing_from(p.stderr)
        # A FUNC_ symbol must come from ffe.obj. If one is unresolved the cause
        # is a naming problem (the game declares them without the leading
        # underscore MinGW adds to C symbols), and stubbing it would replace the
        # very code under test with an abort -- or, worse, with a return value.
        game = sorted(n for n in names if n.lstrip("_").startswith("FUNC_"))
        if game:
            print("REFUSING to stub the game's own functions -- these must resolve "
                  "from ffe.obj, so this is a symbol-naming problem, not a missing "
                  "dependency:")
            for n in game:
                print("   ", n)
            print('Declare them with an explicit asm name: '
                  'extern int FUNC_00nnnn(...) __asm__("FUNC_00nnnn");')
            return 1
        if not names:
            print("link failed for a reason that is not an undefined symbol:")
            print(p.stderr[:3000])
            return 1
        print(f"generating {len(names)} stub(s): {', '.join(sorted(names))}")
        with open(STUB, "w") as f:
            f.write('/* GENERATED by build-oracle.py -- do not edit.\n'
                    ' * One abort-on-call stub per platform symbol ffe.obj references.\n'
                    ' * Aborting is deliberate: a stub that quietly returned 0 would let a\n'
                    ' * test that secretly depends on the platform layer report a clean pass.\n'
                    ' */\n#include <stdio.h>\n#include <stdlib.h>\n\n')
            f.write('static void hit(const char *n)\n{\n'
                    '    fprintf(stderr, "\\nORACLE: %s was actually called -- this test is not a leaf\\n", n);\n'
                    '    exit(97);\n}\n\n')
            for n in sorted(names):
                cname = n[1:] if n.startswith("_") else n
                f.write(f'int {cname}(void);\nint {cname}(void) {{ hit("{cname}"); return 0; }}\n')
        p = link([STUB])
        if p.returncode != 0:
            print("still unresolved after stubbing:")
            print(p.stderr[:3000])
            return 1
        print(f"linked with stubs -> {EXE}")

    r = subprocess.run([EXE], capture_output=True, text=True, errors="replace")
    print()
    print(r.stdout)
    if r.stderr.strip():
        print("stderr:", r.stderr[:2000])
    return r.returncode


if __name__ == "__main__":
    sys.exit(main())
