#!/usr/bin/env python3
"""Build a THIRD PC executable: the frame-dumping reference plus a projection log.

Same sources and flags as build-pc-dump.py, with one more substitution: a COPY
of `ffe.asm` is generated into build\\pcprobe\\ with the projection renamed,

    GLOBAL FUNC_001479      ->  GLOBAL FUNC_001479_REAL
                                EXTERN FUNC_001479
    FUNC_001479:            ->  FUNC_001479_REAL:

so all seven `call FUNC_001479` sites bind to the C shim in
build\\pcprobe_proj.c, which logs the camera-space vertex and forwards to the
real routine. **Nothing under ref\\jjffe is touched** -- the copy is generated,
exactly as build-oracle.py generates its wrapper.

Why a rename rather than a NASM %define wrapper: `%define FUNC_001479 ...`
rewrites the label definition AND the call sites together, so the shim would
never be reached. Only a rename at the definition splits the two.

    python build\\build-pc-probe.py [--force]

produces build\\m0-pc-probe\\ffesdl-probe.exe, which writes `pc_proj.log` in its
working directory -- a ring of the most recent 4000 projections, so the file
holds the cockpit frame rather than the splash.
"""
import io
import os
import re
import subprocess
import sys
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))  # the repo copy this script lives in
SRC = os.path.join(ROOT, "ref", "jjffe")
GEN = os.path.join(ROOT, "build", "pcprobe")
OUT = os.path.join(ROOT, "build", "m0-pc-probe")
TOOLS = os.path.join(ROOT, "tools")

NASM = os.path.join(TOOLS, "nasm", "nasm.exe")
GCC = os.path.join(TOOLS, "mingw32", "bin", "gcc.exe")
GXX = os.path.join(TOOLS, "mingw32", "bin", "g++.exe")
SDL = os.path.join(TOOLS, "SDL-1.2.15")

C = ["ffecfg.c", "ffemisc.c"]
CXX = ["hmp.cpp",
       r"sdl\sdlsound.cpp", r"sdl\sdlinput.cpp", r"sdl\sdltimer.cpp",
       r"win32\w32dir.cpp"]

CFLAGS = [
    "-m32", "-O2", "-D__FFEWINSDL__", "-DFFE_PROBE",
    "-I" + SRC,
    "-I" + os.path.join(SDL, "include", "SDL"),
    "-fpermissive", "-w",
    "-fno-strict-aliasing",
]
CXXFLAGS = CFLAGS + ["-std=gnu++98"]

LIBS = ["-L" + os.path.join(SDL, "lib"),
        "-lmingw32", "-lSDL", "-lopengl32"]

OURS_C = [os.path.join(ROOT, "build", "sdlmain_shim.c"),
          os.path.join(ROOT, "build", "pcprobe_proj.c")]
OURS_CXX = [os.path.join(ROOT, "build", "pcdump_video.cpp")]

# Every symbol renamed in the generated copy, each with a C shim of the same
# name in build\pcprobe_proj.c. 001479 is the projection; 001683 builds the
# render object and is where the camera-relative position is filled in.
HOOKS = ["FUNC_001479", "FUNC_001683", "FUNC_001684", "FUNC_001775",
         "FUNC_001052", "FUNC_001877", "FUNC_001741", "FUNC_001876",
         "FUNC_000568", "FUNC_000582", "FUNC_000575", "FUNC_000880",
         "FUNC_000878", "FUNC_000832", "FUNC_000829",
         "FUNC_000909", "FUNC_000869", "FUNC_000871", "FUNC_000881",
         "FUNC_001478", "FUNC_001470", "FUNC_001772",
         "FUNC_001538", "FUNC_001745",
         # PCPALMAP (2026-09-25): the colour codes DPal16toPal receives.
         "FUNC_001327_DPal16toPal", "FUNC_001326_DPal32to16",
         "FUNC_001623_BlitIndex", "FUNC_001808", "FUNC_001837", "FUNC_001805", "FUNC_001810", "FUNC_001709", "FUNC_000879", "FUNC_000581",
         # The seven model-interpreter opcode handlers that never execute on
         # the Amiga side (2026-08-05). Counted here so the null result there
         # can be paired with a live one here; FUNC_001697 is the important
         # one, being a caller of FUNC_001470 that we never reach.
         "FUNC_001698", "FUNC_001759", "FUNC_001815", "FUNC_001760",
         "FUNC_001763", "FUNC_001697", "FUNC_001765",
         # The four conditional-SKIP handlers. Their RETURN VALUE is where the
         # interpreter reads next, so `ret - a2' is the decision itself -- the
         # quantity that says whether a skip steps over the block holding
         # opcode 21.
         "FUNC_001752", "FUNC_001755", "FUNC_001756", "FUNC_001757",
         # the arithmetic all four delegate to, and the parameter decoder that
         # produces the condition they branch on.
         "FUNC_001753", "FUNC_001473",
         # and the SOLE writer of the fields the decoder reads.
         "FUNC_001758", "FUNC_001534",
         # EVERY writer of record offset +0x9c (2026-08-06). Grepping the
         # disassembly for `+0x9c],' gives ELEVEN write sites in NINE
         # functions, not the two the previous session named -- and forcing
         # the value inside 1536/1537 on the Amiga side left the field
         # unchanged, so those two are either dead or outvoted. Counted here
         # so the Amiga side's zero can be paired with something live.
         # FUNC_000829 and FUNC_001741 are hooked above already.
         "FUNC_000288", "FUNC_000562", "FUNC_000959", "FUNC_001066",
         "FUNC_001070", "FUNC_001536", "FUNC_001537", "FUNC_001770",
         # EVERY remaining entry of the model interpreter's dispatch table
         # DATA_007816 (ffedat.asm:25528-25560, 32 dwords), so the OPCODE
         # SEQUENCE can be recorded here and diffed against the Amiga side's.
         # Counts and ratios cannot answer "where did the two first disagree" --
         # our run ends on the renderer's TRAP 5 after a fraction of the
         # reference's dispatches -- and only the sequence can. A handler left
         # out would leave a hole in it that reads exactly like an opcode that
         # never ran, which is the thing being measured, so this is the whole
         # table minus the ones hooked above.
         "FUNC_001754", "FUNC_001702", "FUNC_001707", "FUNC_001711",
         "FUNC_001721", "FUNC_001696", "FUNC_001706", "FUNC_001710",
         "FUNC_001738", "FUNC_001747", "FUNC_001749", "FUNC_001737",
         "FUNC_001766", "FUNC_001771", "FUNC_001762", "FUNC_001748",
         "FUNC_001767", "FUNC_001874",
         # The string expansion the text opcode feeds its glyph loop. Spelled
         # with its NAME because that is how ffe.asm spells it -- `GLOBAL
         # FUNC_001344_StringExpandFFCode' (ffe.asm:1133) -- and the rename this
         # script performs is textual.
         "FUNC_001344_StringExpandFFCode",
         # The branch that decides whether a glyph is INTERPRETED at all:
         # FUNC_001747 recurses into FUNC_001772 only when this returns zero.
         # Nineteen lines, and its first act is a variable-count shift whose
         # count is an unbounded record field.
         "FUNC_001686",
         # ...and the function it tail-calls, which is where the cull is
         # actually decided and what FUNC_001747 branches on.
         "FUNC_001687",
         # 2026-10-15: the system orbit map (F6) -- SYSMAP twin probe
         "FUNC_000981", "FUNC_000986", "FUNC_001680", "FUNC_001657_Vec64FindMSB", "FUNC_001703", "FUNC_001691", "FUNC_001840", "FUNC_001838"]

# Data labels the shims need to read. `ffe.asm` exports exactly ONE of its 8,596
# DATA_ labels, so anything else has to be made GLOBAL in the generated copy --
# the same problem build-oracle.py solves with its own EXPORTS list, and the
# same solution. Reading them from C otherwise fails at link time with
# "undefined reference to `DATA_009019'".
# Functions that get a bare entry counter (see generate_asm). The planet-
# surface pipeline, for the Mars close-up culling diff (2026-09-25).
COUNTERS = [1644, 1645, 1646, 1647, 1650, 1651, 1652, 1653, 1707, 1708, 1709,
            1712, 1713, 1714, 1806, 1807, 1808, 1809, 1810, 1811, 1799, 1800,
            1801, 1802, 1803, 1804, 1805, 1814,
            # 2026-09-26 FRAMECNT: the model chain above the draw items
            1470, 1471, 1478, 1481, 1692, 1693, 1695, 1717, 1772, 1777,
            1778, 1788, 1837, 1838, 1840, 1879, 1710, 1711, 1738, 1791, 1792,
            # 2026-10-15: the sphere/orbit chain of the F6 orbit map
            1874, 1876, 1877, 1681, 1682, 1532, 1678, 1685, 1700, 1773, 1686, 1687, 1745, 1688, 1746, 1344, 1539,
            # ...and every model-opcode handler (DATA_007816), same frame
            1754, 1698, 1702, 1707, 1711, 1721, 1696, 1706, 1710, 1738, 1747, 1752, 1755, 1758, 1749, 1759, 1815, 1760, 1763, 1756, 1757, 1697, 1737, 1765, 1766, 1771, 1762, 1748, 1741, 1767]

# Draw-item functions (dispatched by FUNC_001788 with the item as arg1) whose
# items are logged during the capture frame -- twin of ITEMLOG in ffe_game.c.
ITEMLOG = [1791, 1792, 1799, 1800, 1801, 1802, 1803, 1804, 1805, 1806, 1807,
           1808, 1809, 1810, 1811, 1814]

EXPORTS = ["DATA_008818", "DATA_008820", "DATA_009188", "DATA_009189", "DATA_007711", "DATA_008861", "DATA_008703", "DATA_008704", "DATA_009133", "DATA_008812", "DATA_009019", "DATA_009020", "DATA_008814", "DATA_008870",
           "DATA_008804", "DATA_008843", "DATA_008844", "DATA_009127", "DATA_009200",
           # the seed of the x10: [DATA_008804] -> DATA_009132 -> >>10 ->
           # DATA_009129[0] -> FUNC_001473 mode 0x80 -> FUNC_001758's operand.
           "DATA_009132",
           # the sub-model descent cull's running pool offset, read by the
           # FUNC_001745 gate shim (2026-09-02).
           "DATA_007823", "DATA_009201", "DATA_007826", "DATA_007827", "DATA_009262", "DATA_009263", "DATA_009267", "DATA_007849"]


def generate_asm():
    """Write build\\pcprobe\\ffe.asm -- ffe.asm with the projection renamed.

    Refuses rather than guesses: if either edit does not match exactly once,
    the copy is not written. A silently unhooked build would look like "the
    reference agrees with us", which is the one answer that must never be
    produced by accident.
    """
    os.makedirs(GEN, exist_ok=True)
    src = os.path.join(SRC, "ffe.asm")
    dst = os.path.join(GEN, "ffe.asm")
    text = io.open(src, encoding="latin-1").read()

    for hook in HOOKS:
        n_global = len(re.findall(r"^GLOBAL %s\s*$" % hook, text, re.M))
        n_label = len(re.findall(r"^%s:" % hook, text, re.M))
        if n_global != 1 or n_label != 1:
            print("REFUSING: expected exactly one GLOBAL and one label for %s, "
                  "found %d and %d" % (hook, n_global, n_label))
            return None
        text = re.sub(r"^GLOBAL %s\s*$" % hook,
                      "GLOBAL %s_REAL\nEXTERN %s" % (hook, hook), text,
                      count=1, flags=re.M)
        text = re.sub(r"^%s:" % hook, "%s_REAL:" % hook, text, count=1,
                      flags=re.M)
        print("hooked %s" % hook)

    # ENTRY COUNTERS (2026-09-25): `inc dword [PCCNT_nnnnnn]' as the first
    # instruction of each listed function -- no C shim, no signature needed.
    # INC writes flags, which is harmless at a function entry (no x86 code in
    # this disassembly carries flags across a CALL). The counters are GLOBAL
    # dwords the C side reads (PCWINCNT in pcprobe_proj.c).
    counted = []
    for num in dict.fromkeys(COUNTERS):   # de-duplicated: a repeat would redefine PCCNT_
        pat = r"^(FUNC_%06d(?:_\w+)?):[^\n]*\n" % num
        if len(re.findall(pat, text, re.M)) != 1:
            print("REFUSING: counter label FUNC_%06d not found exactly once" % num)
            return None
        extra = ""
        if num in ITEMLOG:
            # ITEMLOG (2026-09-26): hand arg1 (the draw item) to C. pushad +
            # pushfd make the call invisible to the function body; after them
            # the return address is at [esp+36] and arg1 at [esp+40].
            extra = ("\t\tpushad\n\t\tpushfd\n\t\tpush dword [esp+40]\n\t\tpush dword %d\n"
                     "\t\tcall _pc_item_log\n\t\tadd esp,byte +0x8\n\t\tpopfd\n\t\tpopad\n" % num)
        text = re.sub(pat, lambda m: m.group(0) + "\t\tinc dword [PCCNT_%06d]\n" % num + extra,
                      text, count=1, flags=re.M)
        counted.append(num)
    if counted:
        # one contiguous table (PCCNT_TAB, numbers in PCCNT_NUMS, length in
        # PCCNT_N), so the C side needs no per-function extern list.
        text += ("\nEXTERN _pc_item_log\nSECTION .data\nGLOBAL PCCNT_N\nGLOBAL PCCNT_NUMS\nGLOBAL PCCNT_TAB\n"
                 "PCCNT_N: dd %d\nPCCNT_NUMS: dd %s\nPCCNT_TAB:\n"
                 % (len(counted), ",".join(str(n) for n in counted))) + "".join(
            "GLOBAL PCCNT_%06d\nPCCNT_%06d: dd 0\n" % (n, n) for n in counted)
        print("counted %d functions" % len(counted))

    # The GLOBAL directives go at the very top, before any SECTION, which is
    # where NASM accepts a forward declaration of a label defined later.
    if EXPORTS:
        text = "".join("GLOBAL %s\n" % e for e in EXPORTS) + text
        print("exported %s" % ", ".join(EXPORTS))

    old = io.open(dst, encoding="latin-1").read() if os.path.exists(dst) else None
    if old != text:
        io.open(dst, "w", encoding="latin-1", newline="").write(text)
        print("wrote %s (%d bytes)" % (dst, len(text)))
    else:
        print("[skip] %s unchanged" % dst)
    return dst


def stale(out, *deps):
    if "--force" in sys.argv or not os.path.exists(out):
        return True
    t = os.path.getmtime(out)
    return any(os.path.getmtime(d) > t for d in deps if os.path.exists(d))


def run(cmd, what):
    t = time.time()
    p = subprocess.run(cmd, capture_output=True, text=True, errors="replace")
    dt = time.time() - t
    ok = p.returncode == 0
    print("[%s] %s  (%.1fs)" % ("ok " if ok else "FAIL", what, dt))
    if not ok:
        err = p.stderr or p.stdout
        lines = [l for l in err.splitlines() if l.strip()]
        for l in lines[:40]:
            print("      " + l)
        if len(lines) > 40:
            print("      ... %d more lines" % (len(lines) - 40))
    return ok


def main():
    os.makedirs(OUT, exist_ok=True)
    for t, n in ((NASM, "nasm"), (GCC, "gcc"), (GXX, "g++")):
        if not os.path.exists(t):
            print("MISSING TOOL: %s at %s" % (n, t))
            return 1

    gen_asm = generate_asm()
    if gen_asm is None:
        return 1

    objs, failed = [], []

    # The generated ffe.asm still %includes ffedat.asm, so it is assembled with
    # the STOCK directory on the include path.
    o = os.path.join(OUT, "ffe.obj")
    if stale(o, gen_asm, os.path.join(SRC, "ffedat.asm")):
        ok = run([NASM, "-f", "win32", "-i", SRC + os.sep, gen_asm, "-o", o],
                 "ffe.asm (hooked)")
        (objs if ok else failed).append(o if ok else "ffe.asm")
    else:
        print("[skip] ffe.asm (hooked)")
        objs.append(o)

    o = os.path.join(OUT, "ffebmp.obj")
    if stale(o, os.path.join(SRC, "ffebmp.asm")):
        ok = run([NASM, "-f", "win32", "-i", SRC + os.sep,
                  os.path.join(SRC, "ffebmp.asm"), "-o", o], "ffebmp.asm")
        (objs if ok else failed).append(o if ok else "ffebmp.asm")
    else:
        print("[skip] ffebmp.asm")
        objs.append(o)

    for f in C:
        o = os.path.join(OUT, os.path.basename(f)[:-2] + ".o")
        if not stale(o, os.path.join(SRC, f)):
            print("[skip] %s" % f)
            objs.append(o)
            continue
        ok = run([GCC] + CFLAGS + ["-c", os.path.join(SRC, f), "-o", o], f)
        (objs if ok else failed).append(o if ok else f)

    for f in CXX:
        o = os.path.join(OUT, os.path.basename(f)[:-4] + ".o")
        if not stale(o, os.path.join(SRC, f)):
            print("[skip] %s" % f)
            objs.append(o)
            continue
        ok = run([GXX] + CXXFLAGS + ["-c", os.path.join(SRC, f), "-o", o], f)
        (objs if ok else failed).append(o if ok else f)

    for f in OURS_C:
        o = os.path.join(OUT, os.path.basename(f)[:-2] + ".o")
        if not stale(o, f):
            print("[skip] %s" % os.path.basename(f))
            objs.append(o)
            continue
        ok = run([GCC] + CFLAGS + ["-c", f, "-o", o], os.path.basename(f))
        (objs if ok else failed).append(o if ok else f)

    for f in OURS_CXX:
        o = os.path.join(OUT, os.path.basename(f)[:-4] + ".o")
        if not stale(o, f, os.path.join(SRC, "sdl", "sdlvideo.cpp")):
            print("[skip] %s" % os.path.basename(f))
            objs.append(o)
            continue
        ok = run([GXX] + CXXFLAGS + ["-c", f, "-o", o], os.path.basename(f))
        (objs if ok else failed).append(o if ok else f)

    if failed:
        print("\n%d translation unit(s) failed: %s"
              % (len(failed), ", ".join(map(str, failed))))
        return 1

    exe = os.path.join(OUT, "ffesdl-probe.exe")
    if not run([GXX, "-m32", "-mconsole",
                "-static-libgcc", "-static-libstdc++", "-o", exe] + objs + LIBS,
               "link ffesdl-probe.exe"):
        return 1

    print("\nBUILT: %s  (%.2f MB)" % (exe, os.path.getsize(exe) / 1024.0 / 1024))
    print("It writes ffe_frame.raw and pc_proj.log in its working directory.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
