# SPDX-License-Identifier: MIT
# Copyright (c) 2026 angree. See LICENSE at the repository root.
"""Hand-scheduled 68k for the measured hot spots (2026-10-24, performance work).

The translator emits correct but literal code: one x86 instruction at a time,
memory operands re-read on every pass, shifts through d7. For the few inner
loops that the profiler (tools/perf/prof_report.py) shows dominating the
frame, this script replaces the EMITTED loop in build/xlat-full/*.s with a
register-only version that computes the same thing.

Every patch states, next to its code, why its result equals the original
(which registers and memory it leaves as the x86 would, and which values are
dead after it). The original text must match EXACTLY -- if the translator ever
emits something else, this script fails instead of patching blind. Idempotent;
run by build/build-amiga-local.sh before assembly. FFE_NO_SPEED=1 skips it, so
a build with and without the patches can be compared.
"""
import os
import sys

D = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "build", "xlat-full")
TAG = "; patch_speed.py: "

# --- Tex64 / Tex64Trans span loops ------------------------------------------
# ffe.asm:99279-99295 (FUNC_001650, opaque) and 99514-99533 (FUNC_001651,
# colour 0 transparent). Per pixel the x86 does
#     texel = [ [DATA_009235] + (esi sar 16) + ((edi sar 10) and -64) ]
#     (Trans: skip if texel == 0)  [eax] = [DATA_009252 + texel]
#     eax++  esi += [ebp-4]  edi += [ebp-8]  ebx--  until ebx == 0
# The replacement keeps [ebp-4], [ebp-8], [DATA_009235] and the colour table
# in registers (none of them is written inside the loop), takes esi sar 16 as
# SWAP+EXT, and keeps the shift count in a register. AND.W #-64 on the shifted
# edi is the 32-bit AND -64: the mask's upper word is all ones.
# Same state at the exit as the x86 loop: eax = end of span (d0), ebx = 0
# (d3), esi/edi advanced (a2/a3), memory identical. ecx/edx (d1/d2) differ
# but are dead: right after the loop eax is reloaded, and the next pass
# rewrites edx/ebx/esi/edi before reading them (ffe.asm:99188-99213,
# 99423-99448). The loop is only entered with ebx > 0 (`test ebx,ebx / jng`
# just before it). d4-d6/a4 are not translator registers; saved and restored.


def tex_loop(top, skip, trans):
    # the texel goes to d7, whose upper 24 bits are cleared once before the
    # loop and never written again, so it indexes the colour table as is
    store = ("\tmove.b\t(0,a4,d2.l),d7\n"
             "\tbeq.s\t%s\n"
             "\tmove.b\t(0,a1,d7.w),(a0)\n"
             "\txdef\t%s\n"
             "%s:\n"
             "\taddq.l\t#1,a0\n" % (skip, skip, skip)) if trans else (
             "\tmove.b\t(0,a4,d2.l),d7\n"
             "\tmove.b\t(0,a1,d7.w),(a0)+\n")
    return tex_fast(top, trans) + (
            "\tmovem.l\td4-d6/a4,-(sp)\n"
            "\tmove.l\t(-4,a5),d4\n"
            "\tmove.l\t(-8,a5),d5\n"
            "\tmovea.l\t(DATA_009235).l,a4\n"
            "\tlea\t(DATA_009252).l,a1\n"
            "\tmovea.l\td0,a0\n"
            "\tmoveq\t#10,d6\n"
            "\tmoveq\t#0,d7\n"
            "\txdef\t%s\n"
            "%s:\n"
            "\tmove.l\ta3,d2\n"
            "\tasr.l\td6,d2\n"
            "\tand.w\t#-64,d2\n"
            "\tmove.l\ta2,d1\n"
            "\tswap\td1\n"
            "\text.l\td1\n"
            "\tadd.l\td1,d2\n"
            "%s"
            "\tadda.l\td4,a2\n"
            "\tadda.l\td5,a3\n"
            "\tsubq.l\t#1,d3\n"
            "\tbne.s\t%s\n"
            "\tmove.l\ta0,d0\n"
            "\tmovem.l\t(sp)+,d4-d6/a4\n"
            "%s_done:\n" % (top, top, store, top, top))


# FAST PATH (v3, 2026-10-24; the idea -- u as integer/fraction words joined by
# ADDX, the texture row in an address register -- is from the 3d_platformer
# engine's TC256PX loop, made EXACT here). With W = 32-bit x86 arithmetic:
#   esi sar 16           = the signed high word of esi.  Kept as two words,
#     lo += du.lo (carry into X), hi += du.hi + X: that IS the 32-bit add, so
#     hi is exactly esi's high word at every pixel, wrap-around included. The
#     fetch uses it as a sign-extended .W index: (a4,d1.w).
#   (edi sar 10) and -64 = (edi sar 16) * 64.  Tracked as a row pointer
#     a4 = texture + (edi sar 16)*64, advanced per pixel by (dv sar 16)*64,
#     plus 64 when the low-word add carries -- again the 32-bit add, split.
#     Unlike the u word this does not wrap at 16 bits, so it is used only when
#     edi cannot leave +-2^31 during the span: |edi| < 2^30, |dv| < 2^21 and
#     ebx <= 320 (2^21 * 320 < 2^30). Otherwise the v1 loop below runs.
# At the exit esi = hi:lo, edi = edi + ebx*dv (MULS.L, mod 2^32 like the
# x86's repeated adds), eax = end of span, ebx = 0 -- the v1 loop's state.
# TEXTURE DETAIL (2026-10-24, developer's request: "distant polygons become
# untextured in the colour of their texture -- it will look like Frontier 1").
# PROGDIR:amiffe.cfg tex= (ffe_tex_mode): 0 = the original (this block is a
# single test and falls through), 1 = flat when the texture is minified by 2+
# texels per pixel in u or v (i.e. far away), 2 = always flat. The colour is
# ffe_tex_flat_index (ffe_system.c: the texture's most frequent index) through
# THIS quad's colour table [DATA_009252], so lighting is kept; it answers -1
# for a transparent texture with holes, which then stays textured.
# The flat fill leaves the state the textured loop leaves where it matters:
# eax = end of span, ebx = 0, memory = the span written. esi/edi are not
# advanced: they are dead after the loop (see above), so nothing reads them.
# The C call may clobber d0/d1/a0/a1 only: d0 is saved, d1 (ecx) is dead.
def tex_flat(t, trans):
    return ("\txref\t_ffe_tex_mode\n"
            "\txref\t_ffe_tex_flat_index\n"
            "\tmove.w\t(_ffe_tex_mode).l,d7\n"
            "\tbeq\t{t}_tex\n"
            "\tcmpi.w\t#2,d7\n"
            "\tbeq.s\t{t}_flat\n"
            "\tmove.l\t(-4,a5),d7\n"
            "\tbpl.s\t{t}_f1\n"
            "\tneg.l\td7\n"
            "{t}_f1:\n"
            "\tcmp.l\t#$20000,d7\n"
            "\tbhs.s\t{t}_flat\n"
            "\tmove.l\t(-8,a5),d7\n"
            "\tbpl.s\t{t}_f2\n"
            "\tneg.l\td7\n"
            "{t}_f2:\n"
            "\tcmp.l\t#$20000,d7\n"
            "\tblo\t{t}_tex\n"
            "\txdef\t{t}_flat\n"
            "{t}_flat:\n"
            "\tmove.l\td0,-(sp)\n"
            "\tmove.l\t#" + ("1" if trans else "0") + ",-(sp)\n"
            "\tmove.l\t(DATA_009235).l,-(sp)\n"
            "\tjsr\t_ffe_tex_flat_index\n"
            "\taddq.l\t#8,sp\n"
            "\tmove.l\td0,d7\n"
            "\tmove.l\t(sp)+,d0\n"
            "\ttst.l\td7\n"
            "\tbmi\t{t}_tex\n"
            "\tlea\t(DATA_009252).l,a1\n"
            "\tmove.b\t(0,a1,d7.w),d7\n"
            "\tmovea.l\td0,a0\n"
            "{t}_fl:\n"
            "\tmove.b\td7,(a0)+\n"
            "\tsubq.l\t#1,d3\n"
            "\tbne.s\t{t}_fl\n"
            "\tmove.l\ta0,d0\n"
            "\tbra\t{t}_done\n")


def tex_fast(top, trans):
    t = top
    if trans:
        px = ("\tmove.b\t(0,a4,d1.w),d7\n"
              "\tbeq.s\t%s_skip\n"
              "\tmove.b\t(0,a1,d7.w),(a0)\n"
              "%s_skip:\n"
              "\taddq.l\t#1,a0\n" % (t, t))
    else:
        px = ("\tmove.b\t(0,a4,d1.w),d7\n"
              "\tmove.b\t(0,a1,d7.w),(a0)+\n")
    return (TAG + "Tex64%s span loop: exact ADDX fast path + v1 fallback (see the script)\n"
            + tex_flat(t, trans) +
            "{t}_tex:\n"
            "\tmove.l\ta3,d7\n"
            "\tbpl.s\t{t}_g1\n"
            "\tneg.l\td7\n"
            "{t}_g1:\n"
            "\tcmp.l\t#$40000000,d7\n"
            "\tbhs\t{t}_slow\n"
            "\tmove.l\t(-8,a5),d7\n"
            "\tbpl.s\t{t}_g2\n"
            "\tneg.l\td7\n"
            "{t}_g2:\n"
            "\tcmp.l\t#$200000,d7\n"
            "\tbhs\t{t}_slow\n"
            "\tcmp.l\t#320,d3\n"
            "\tbhi\t{t}_slow\n"
            "\tmovem.l\td4-d6/a4/a6,-(sp)\n"
            "\tmove.l\t(-8,a5),d7\n"
            "\tmuls.l\td3,d7\n"
            "\tadd.l\ta3,d7\n"
            "\tmove.l\td7,-(sp)\n"           # edi at the end of the span
            "\tmove.l\ta3,d6\n"
            "\tswap\td6\n"
            "\text.l\td6\n"
            "\tlsl.l\t#6,d6\n"
            "\tmovea.l\t(DATA_009235).l,a4\n"
            "\tadda.l\td6,a4\n"              # row pointer
            "\tmove.w\ta3,d6\n"              # edi low word
            "\tmovea.l\td0,a0\n"
            "\tmove.l\t(-8,a5),d0\n"         # d0.w = dv low word
            "\tmove.l\td0,d7\n"
            "\tswap\td7\n"
            "\text.l\td7\n"
            "\tlsl.l\t#6,d7\n"
            "\tmovea.l\td7,a6\n"             # (dv sar 16) * 64
            "\tmove.l\ta2,d2\n"              # d2.w = esi low word
            "\tmove.l\td2,d1\n"
            "\tswap\td1\n"                   # d1.w = esi high word
            "\tmove.l\t(-4,a5),d4\n"         # d4.w = du low word
            "\tmove.l\td4,d5\n"
            "\tswap\td5\n"                   # d5.w = du high word
            "\tlea\t(DATA_009252).l,a1\n"
            "\tmoveq\t#0,d7\n"
            "\tsubq.w\t#1,d3\n"
            "\txdef\t{t}_px\n"
            "{t}_px:\n"
            + px.replace("%", "%%") +
            "\tadd.w\td4,d2\n"
            "\taddx.w\td5,d1\n"
            "\tadd.w\td0,d6\n"
            "\tbcc.s\t{t}_nc\n"
            "\tlea\t(64,a4),a4\n"
            "{t}_nc:\n"
            "\tadda.l\ta6,a4\n"
            "\tdbra\td3,{t}_px\n"
            "\tmoveq\t#0,d3\n"
            "\tswap\td1\n"
            "\tmove.w\td2,d1\n"
            "\tmovea.l\td1,a2\n"
            "\tmovea.l\t(sp)+,a3\n"
            "\tmove.l\ta0,d0\n"
            "\tmovem.l\t(sp)+,d4-d6/a4/a6\n"
            "\tbra\t{t}_done\n"
            "\txdef\t{t}_slow\n"
            "{t}_slow:\n").replace("{t}", t) % ("Trans" if trans else "")

# --- median-cut histogram scans (DPal) ---------------------------------------
# FUNC_001329, ffe.asm:82873-82917: for b = edx .. [ebp-0x18]-1 over the cells
# at ecx, every NON-ZERO cell counts ([ebp-0x1c]++) and widens the box at eax
# (r = [ebp-4], g = ebx, b = edx: min/max+1 words at +0..+0xa). Most cells are
# zero, and the emitted loop paid MOVEA/CMPI/compare-with-memory for each.
# Replacement: TST.B (a0)+ with the remaining count in d7; the box update is
# the x86's, compare for compare (directions noted). Exit state as the x86:
# edx = [ebp-0x18], ecx = one past the row, eax/ebx unchanged, the count and
# the box in memory identical. edi only ever holds a MOVSX'd box word here and
# is reloaded before every use, so it is dead after the loop. Entered only
# with edx < [ebp-0x18] (the `jnl` before it).
P1329 = """\txdef\tJUMP_005230
""" + TAG + """median-cut box scan, register-only (see the script)
\tmovem.l\td4-d5,-(sp)
\tmovea.l\td1,a0
\tmovea.l\td0,a1
\tmove.l\t(-24,a5),d7
\tsub.l\td2,d7
JUMP_005230:
PS1329_scan:
\ttst.b\t(a0)+
\tbne.s\tPS1329_hit
PS1329_next:
\tsubq.l\t#1,d7
\tbne.s\tPS1329_scan
\tmove.l\t(-24,a5),d2
\tmove.l\ta0,d1
\tmovem.l\t(sp)+,d4-d5
\tbra\tJUMP_005237
PS1329_hit:
\taddq.l\t#1,(-28,a5)
\tmove.l\t(-24,a5),d2
\tsub.l\td7,d2
\tmove.l\t(-4,a5),d4
\tmove.w\t(a1),d5
\text.l\td5
\tcmp.l\td4,d5
\tble.s\tPS1329_a
\tmove.w\td4,(a1)
PS1329_a:
\tmove.w\t(2,a1),d5
\text.l\td5
\tcmp.l\td4,d5
\tbgt.s\tPS1329_b
\tmove.w\td4,d5
\taddq.w\t#1,d5
\tmove.w\td5,(2,a1)
PS1329_b:
\tmove.w\t(4,a1),d5
\text.l\td5
\tcmp.l\td5,d3
\tbge.s\tPS1329_c
\tmove.w\td3,(4,a1)
PS1329_c:
\tmove.w\t(6,a1),d5
\text.l\td5
\tcmp.l\td5,d3
\tblt.s\tPS1329_d
\tmove.w\td3,d5
\taddq.w\t#1,d5
\tmove.w\td5,(6,a1)
PS1329_d:
\tmove.w\t(8,a1),d5
\text.l\td5
\tcmp.l\td5,d2
\tbge.s\tPS1329_e
\tmove.w\td2,(8,a1)
PS1329_e:
\tmove.w\t(10,a1),d5
\text.l\td5
\tcmp.l\td5,d2
\tblt.s\tPS1329_next
\tmove.w\td2,d5
\taddq.w\t#1,d5
\tmove.w\td5,(10,a1)
\tbra.s\tPS1329_next
"""

# FUNC_001330, ffe.asm:83233-83273: esi = 0..0xfff over the cells at
# [esp+0x18]; a non-zero cell runs the body (unchanged, emitted code), a zero
# cell only advances. Only the HEAD is replaced: a fast scan to the next
# non-zero cell, which then enters the emitted body with the x86's state
# (eax = [esp+0x18] = that cell, esi = its index). After the last cell
# eax = the last cell examined, [esp+0x18] one past it, esi = 0x1000, exactly
# what the x86 leaves when its loop falls out.
P1330 = """\txdef\tJUMP_005265
JUMP_005265:
""" + TAG + """histogram scan head, register-only (see the script)
\tmovea.l\t(24,sp),a0
\tmove.l\t#4096,d7
\tsub.l\ta2,d7
PS1330_scan:
\ttst.b\t(a0)+
\tbne.s\tPS1330_hit
\tsubq.l\t#1,d7
\tbne.s\tPS1330_scan
\tmove.l\ta0,(24,sp)
\tsubq.l\t#1,a0
\tmove.l\ta0,d0
\tmovea.l\t#4096,a2
\tbra\tJUMP_005267
PS1330_hit:
\tsubq.l\t#1,a0
\tmove.l\ta0,(24,sp)
\tmove.l\ta0,d0
\tmovea.l\t#4096,a2
\tsuba.l\td7,a2
"""


# Timer50Hz, ffe.asm:85574-85581: every 50 Hz tick, each of the 256 key
# accumulators that is non-zero is incremented (eax = 255 down to 0). The
# entries are independent, so the order does not matter; at the exit the x86
# leaves eax = -1 (the `dec eax / jnl` falls out there) and the flags are
# rewritten by the `cmp` that follows. a0/d7 are translator scratch.
P50HZ = """
LOC_50Hz_no9160:
""" + TAG + """key accumulator loop, register-only (see the script)
\tlea\t(DATA_KeyAccumTable+1024).l,a0
\tmove.w\t#255,d7
P50HZ_loop:
\ttst.l\t-(a0)
\tbeq.s\tP50HZ_next
\taddq.l\t#1,(a0)
P50HZ_next:
\tdbra\td7,P50HZ_loop
\tmoveq\t#-1,d0
"""

# PLANET DETAIL (2026-10-24). The planet is an icosahedron split 4-fold per
# level by FUNC_001868..1873. Each splitter stops a patch whose doubled screen
# area is < 150 -- but only above level 8 (`cmp <level>,byte +0x8 / jng
# subdivide`, ffe.asm 121768 / 122703 / 123970 / 125561 / 126818 / 128073),
# so a planet a few tens of pixels across is still drawn as hundreds of
# textured triangles (measured: ~140 of 220 ms per frame in the intro's
# small-Earth scene on the 68060 target). The 8 becomes the variable
# ffe_planet_lod (ffe_system.c, amiffe.cfg planetlod=): 8 is the original
# and gives the same flags as the immediate, so the picture is unchanged;
# 0 lets small patches stop from level 1 on. Same compare order as the x86
# (level minus threshold), so the `jng` after it is untouched; d7 is scratch.
def lod(fn, line, old, new):
    return (fn, "; ffe.asm:%d " % line, old + "\n",
            TAG + "planet detail threshold from ffe_planet_lod (was 8)\n"
            "\txref\t_ffe_planet_lod\n" + new + "\n",
            [" ".join(old.split())])


LOD_PATCHES = [
    lod("FUNC_001868.s", 121768, "\tcmpa.l\t#8,a2", "\tcmpa.l\t(_ffe_planet_lod).l,a2"),
    lod("FUNC_001869.s", 122703, "\tcmp.l\t#8,d0", "\tcmp.l\t(_ffe_planet_lod).l,d0"),
    lod("FUNC_001870.s", 123970, "\tcmpi.l\t#8,(32,a5)", "\tmove.l\t(32,a5),d7\n\tcmp.l\t(_ffe_planet_lod).l,d7"),
    lod("FUNC_001871.s", 125561, "\tcmpi.l\t#8,(32,a5)", "\tmove.l\t(32,a5),d7\n\tcmp.l\t(_ffe_planet_lod).l,d7"),
    lod("FUNC_001872.s", 126818, "\tcmpi.l\t#8,(32,a5)", "\tmove.l\t(32,a5),d7\n\tcmp.l\t(_ffe_planet_lod).l,d7"),
    lod("FUNC_001873.s", 128073, "\tcmpi.l\t#8,(32,a5)", "\tmove.l\t(32,a5),d7\n\tcmp.l\t(_ffe_planet_lod).l,d7"),
]

PATCHES = LOD_PATCHES + [
    ("Timer50Hz.s", "\nLOC_50Hz_no9160:\n", "\tbge\tLOC_50Hz_accumloop\n", P50HZ,
     ["move.l #255,d0", "lea (DATA_KeyAccumTable).l,a0", "cmpi.l #0,(0,a0,d0.l*4)",
      "beq LOC_50Hz_accumloopend", "lea (DATA_KeyAccumTable).l,a0",
      "addq.l #1,(0,a0,d0.l*4)", "subq.l #1,d0", "bge LOC_50Hz_accumloop"]),
    # file, first line of the emitted loop, last line, replacement
    ("FUNC_001650.s", "\txdef\tJUMP_006272\nJUMP_006272:\n", "\tbne\tJUMP_006272\n",
     tex_loop("JUMP_006272", None, False)),
    ("FUNC_001651.s", "\txdef\tJUMP_006288\nJUMP_006288:\n", "\tbne\tJUMP_006288\n",
     tex_loop("JUMP_006288", "JUMP_006289", True)),
    ("FUNC_001329.s", "\txdef\tJUMP_005230\nJUMP_005230:\n", "\tblt\tJUMP_005230\n", P1329),
    ("FUNC_001330.s", "\txdef\tJUMP_005265\nJUMP_005265:\n", "\tbeq\tJUMP_005266\n", P1330,
     ["move.l (24,sp),d0", "movea.l d0,a0", "cmpi.b #$00,(a0)", "beq JUMP_005266"]),
]


def main():
    off = os.environ.get("FFE_NO_SPEED") == "1"
    for patch in PATCHES:
        fn, first, last, new = patch[:4]
        expect = patch[4] if len(patch) > 4 else None
        p = os.path.join(D, fn)
        keep = p + ".prespeed"          # the translator's own output, kept once
        if off:
            if os.path.exists(keep):
                open(p, "wb").write(open(keep, "rb").read())
                print("patch_speed: %s restored (FFE_NO_SPEED=1)" % fn)
            continue
        s = open(p, encoding="latin-1").read()
        if not os.path.exists(keep) and new not in s:
            open(keep, "wb").write(open(p, "rb").read())
        if new not in s and os.path.exists(keep):
            s = open(keep, encoding="latin-1").read()   # patch the translator's text, not an older patch
        if new in s:
            print("patch_speed: %s already patched" % fn)
            continue
        a = s.find(first)
        b = s.find(last, a)
        if a < 0 or b < 0 or s.count(first) != 1:
            sys.exit("patch_speed: anchor not found in %s - translator output changed, refusing" % fn)
        b += len(last)
        old = s[a:b]
        if expect is not None:
            got = [" ".join(l.split()) for l in old.split("\n")
                   if l.startswith("\t") and not l.startswith("\txdef")]
            if got != expect:
                sys.exit("patch_speed: %s region is %s, expected %s - refusing" % (fn, got, expect))
        s = s[:a] + new + s[b:]
        open(p, "w", encoding="latin-1", newline="\n").write(s)
        print("patch_speed: %s patched (%d emitted lines -> %d)" % (
            fn, old.count("\n"), new.count("\n")))


if __name__ == "__main__":
    main()
