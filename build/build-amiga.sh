#!/bin/bash
# Cross-build the Amiga 68k side. Run from WSL:
#   wsl -e bash /mnt/i/GITHUB/Amiga_Frontier_2_First_Encouters/build/build-amiga.sh [target]
#
# WSL1 cannot RUN 32-bit x86 binaries, which is why the PC reference lives on
# native Windows -- but it cross-compiles to m68k perfectly well, because the
# bebbo toolchain is a 64-bit Linux binary emitting Amiga hunks.
#
# HARD RULES, each one paid for in the OpenTTD port:
#   -mcpu=68020 -msoft-float, never -m68040 and never -mhard-float. bebbo maps
#   -m68040 onto the 68020+68881 multilib and -msoft-float does NOT undo it,
#   giving a binary whose C library needs an FPU. -print-multi-directory lies
#   about this; the only honest check is `-Wl,-t | grep libm881`, which this
#   script performs on every link.
#   Never -O2 anywhere. -O1 is the ceiling.
set -e
export PATH=/opt/amiga/bin:/usr/local/bin:/usr/bin:/bin

ROOT=${FFE_ROOT:-/mnt/i/GITHUB/Amiga_Frontier_2_First_Encouters}
AMIGA=$ROOT/amiga
OUT=$ROOT/build/amiga-out
DEPLOY=${FFE_DEPLOY:-$ROOT/winuae/work}
NDK=/opt/amiga/m68k-amigaos/ndk-include

CC=m68k-amigaos-gcc
VASM=vasmm68k_mot

# cgx-include carries the CyberGraphX/Picasso96 headers (proto/cybergraphics.h
# and friends). The bebbo NDK does not ship them, and amiga_gfx.c needs them even
# for an AGA-only build: the RTG backend is compiled in and chosen at runtime.
CFLAGS="-mcpu=68020 -msoft-float -O1 -Wall -I$AMIGA -I$AMIGA/cgx-include -I$NDK"

TARGET=${1:-ffe_agatest}
mkdir -p "$OUT"

# The display prefs tool (amiga/tools/ffe-prefs.c): plain C on Intuition and
# GadTools, nothing of the platform layer.
if [ "$TARGET" = "ffe-prefs" ]; then
	$CC $CFLAGS -noixemul -o "$OUT/ffe-prefs" "$AMIGA/tools/ffe-prefs.c"
	ls -l "$OUT/ffe-prefs"
	mkdir -p "$DEPLOY"
	cp "$OUT/ffe-prefs" "$DEPLOY/ffe-prefs"
	echo "deployed -> $DEPLOY/ffe-prefs"
	exit 0
fi

# Targets that sit on the FFE platform layer (amiga/ffeapi/*.c: the surface the
# game itself calls) rather than talking to amiga_gfx.c directly. Everything
# else links exactly what it linked before.
#
# The source list is globbed, not enumerated: several people add files to
# amiga/ffeapi/ independently, and a hand-maintained list here turns this script
# into a merge conflict every time. Anything that appears in that directory is
# compiled in.
FFEAPI_OBJS=""
EXTRA_OBJS=""
case "$TARGET" in
ffe_apitest|ffe_sndtest|ffe_dirtest|ffe_drawtest|ffe_game)
	FFEAPI_SRCS="$(cd "$AMIGA/ffeapi" && ls *.c 2>/dev/null | tr '\n' ' ')"
	;;
*)
	FFEAPI_SRCS=""
	;;
esac

# Paula audio comes from the OpenTTD port and lives one level up, so it is not
# caught by the glob above.
#
# Every target that globs the platform layer needs this, not just the ones that
# make a sound: the glob pulls in ffe_sound.c, so leaving amiga_audio.o out
# fails the link on AmigaAudio_Play and friends. Keep this list identical to the
# FFEAPI_SRCS list above.
case "$TARGET" in
ffe_apitest|ffe_sndtest|ffe_dirtest|ffe_drawtest|ffe_game)
	echo "=== amiga_audio.c ==="
	$CC $CFLAGS -c "$AMIGA/amiga_audio.c" -o "$OUT/amiga_audio.o"
	EXTRA_OBJS="$OUT/amiga_audio.o"
	;;
esac

# Translated game code: everything tools\translator\translate.py has written to
# build\xlat. Globbed for the same reason the platform layer is - the set grows
# one function at a time and a list here would be stale by the next commit.
#
# -m68020, and -no-opt for a reason that is not cosmetic: the translator's
# correctness argument is about WHICH instruction is emitted and what it leaves
# in the CCR. An assembler that substitutes an equivalent-looking instruction
# behind its back would invalidate the argument, so it is told not to.
XLAT_OBJS=""
case "$TARGET" in
ffe_game)
	# The FULL translation set (massgen.py) plus the big-endian data image.
	# ~1,500 vasm runs; each is skipped when its .o is newer than its .s, so
	# only the first build pays the whole bill.
	XLATF="$ROOT/build/xlat-full"
	XOUT="$OUT/xlat-full"
	mkdir -p "$XOUT"
	shopt -s nullglob
	srcs=("$XLATF"/*.s)
	shopt -u nullglob
	if [ ${#srcs[@]} -eq 0 ]; then
		echo "FATAL: no .s in $XLATF - run python tools\\translator\\massgen.py"
		exit 1
	fi
	echo "=== full translated 68k (vasm, ${#srcs[@]} file(s), incremental) ==="
	n=0
	for s in "${srcs[@]}"; do
		obj="$XOUT/$(basename "${s%.s}").o"
		if [ ! -f "$obj" ] || [ "$s" -nt "$obj" ]; then
			$VASM -Fhunk -m68020 -no-opt -quiet -o "$obj" "$s" || exit 1
			n=$((n+1))
		fi
		XLAT_OBJS="$XLAT_OBJS $obj"
	done
	echo "    ($n assembled, $(( ${#srcs[@]} - n )) up to date)"
	for d in ffedat_be ffebmp_be; do
		src="$ROOT/build/data-be/$d.s"
		obj="$OUT/$d.o"
		if [ ! -f "$obj" ] || [ "$src" -nt "$obj" ]; then
			echo "=== $d.s (vasm) ==="
			$VASM -Fhunk -m68020 -no-opt -quiet -o "$obj" "$src" || exit 1
		fi
		XLAT_OBJS="$XLAT_OBJS $obj"
	done
	# crash-catcher (tc_TrapCode): captures the faulting PC into globals and
	# redirects the task to the C reporter -- every Guru names its PC in
	# PROGDIR:ffe_crash.log instead of dying anonymously.
	src="$ROOT/amiga/probes/ffe_trap.s"
	obj="$OUT/ffe_trap.o"
	if [ ! -f "$obj" ] || [ "$src" -nt "$obj" ]; then
		echo "=== ffe_trap.s (vasm) ==="
		$VASM -Fhunk -m68020 -no-opt -quiet -o "$obj" "$src" || exit 1
	fi
	XLAT_OBJS="$XLAT_OBJS $obj"
	# _SetJmp/_LongJmp for real: the game's per-frame error escape. While
	# these were stubs (LongJmp -> exit(11)) the first landed frame killed
	# the process, which read as a hang. See the header in ffe_setjmp.s.
	src="$ROOT/amiga/probes/ffe_setjmp.s"
	obj="$OUT/ffe_setjmp.o"
	if [ ! -f "$obj" ] || [ "$src" -nt "$obj" ]; then
		echo "=== ffe_setjmp.s (vasm) ==="
		$VASM -Fhunk -m68020 -no-opt -quiet -o "$obj" "$src" || exit 1
	fi
	XLAT_OBJS="$XLAT_OBJS $obj"
	;;
ffe_xlattest)
	echo "=== ffe_pfloat.c (FUNC_001486 shim, oracle side) ==="
	$CC $CFLAGS -c "$AMIGA/probes/ffe_pfloat.c" -o "$OUT/ffe_pfloat.o"
	EXTRA_OBJS="$EXTRA_OBJS $OUT/ffe_pfloat.o"
	;&
ffe_xlattest|ffe_drawtest)
	XLAT="$ROOT/build/xlat"
	shopt -s nullglob
	srcs=("$XLAT"/*.s)
	shopt -u nullglob
	if [ ${#srcs[@]} -eq 0 ]; then
		echo "FATAL: no .s in $XLAT - run"
		echo "  python tools\\translator\\translate.py FUNC_001521 FUNC_001522 FUNC_001523 -o build\\xlat"
		exit 1
	fi
	echo "=== translated 68k (vasm, ${#srcs[@]} file(s)) ==="
	for s in "${srcs[@]}"; do
		obj="$OUT/$(basename "${s%.s}").o"
		$VASM -Fhunk -m68020 -no-opt -o "$obj" "$s"
		XLAT_OBJS="$XLAT_OBJS $obj"
	done
	;;
esac

echo "=== c2p (vasm, Motorola syntax - includes both Kalms routines) ==="
# c2p_glue.s does `include "c2p_rect.s"` / `include "c2p1x1_6_c5_bm_040.s"` by
# bare name, so vasm must be run with the c2p directory on its include path.
#
# -m68020, NOT -m68040, even though Kalms' file names say 040 and his comments
# say "optimized for 040+". Those routines are SCHEDULED for the 040 but use
# only base 68000/68020 instructions -- verified: add addq and andi bcc cmp cmpi
# eor exg jsr lea lsl lsr move movem moveq mulu or rts sub subq swap tst, with no
# move16, no cinv/cpush, no FPU. Assembling at -m68020 makes vasm REFUSE anything
# outside the 020 set instead of silently accepting it, which is the whole point:
# the release must run on a formal 68020 without an FPU, because that is what the
# FPGA accelerators (Vampire and friends) present.
$VASM -Fhunk -m68020 -no-opt -I"$NDK" -I"$AMIGA/c2p" \
      -o "$OUT/c2p_glue.o" "$AMIGA/c2p/c2p_glue.s"

# SOFT-FLOAT DOUBLE ARITHMETIC (2026-08-02). Without this the toolchain
# resolves __divdf3 and friends from libc's mathieee*.library stubs, which
# jump through library bases and died at PC=00fca0c0 the first time the
# planet-limb shim divided two doubles. -O0 for the same reason fp_conv.c
# gives: correctness first on a toolchain with a miscompilation history,
# and these are small leaf routines.
echo "=== fp_soft.c (soft-float doubles) ==="
$CC -mcpu=68020 -msoft-float -noixemul -O0 -c "$AMIGA/fp_soft.c" -o "$OUT/fp_soft.o"

echo "=== amiga_gfx.c ==="
$CC $CFLAGS -c "$AMIGA/amiga_gfx.c" -o "$OUT/amiga_gfx.o"

if [ -n "$FFEAPI_SRCS" ]; then
	echo "=== ffeapi (platform layer) ==="
	for src in $FFEAPI_SRCS; do
		obj="$OUT/${src%.c}.o"
		$CC $CFLAGS -c "$AMIGA/ffeapi/$src" -o "$obj"
		FFEAPI_OBJS="$FFEAPI_OBJS $obj"
	done
fi

echo "=== $TARGET.c ==="
$CC $CFLAGS -c "$AMIGA/probes/$TARGET.c" -o "$OUT/$TARGET.o"

# ffe_planet.c is the hand port of FUNC_001877 (the planet limb / atmosphere
# halo), which the translator refuses: 97 x87 instructions and a SECTION .data
# island inside the function body. It provides the BARE symbol FUNC_001877, so
# massgen.py must not also emit a stub for it - the names filter there and this
# line are one change, not two.
case "$TARGET" in
ffe_game)
	echo "=== ffe_planet.c (FUNC_001877 shim) ==="
	$CC $CFLAGS -c "$AMIGA/probes/ffe_planet.c" -o "$OUT/ffe_planet.o"
	EXTRA_OBJS="$EXTRA_OBJS $OUT/ffe_planet.o"
	echo "=== ffe_x87.c (FUNC_000112 shim) ==="
	$CC $CFLAGS -c "$AMIGA/probes/ffe_x87.c" -o "$OUT/ffe_x87.o"
	EXTRA_OBJS="$EXTRA_OBJS $OUT/ffe_x87.o"
	echo "=== ffe_pfloat.c (FUNC_001486 shim) ==="
	$CC $CFLAGS -c "$AMIGA/probes/ffe_pfloat.c" -o "$OUT/ffe_pfloat.o"
	EXTRA_OBJS="$EXTRA_OBJS $OUT/ffe_pfloat.o"
	echo "=== ffe_timers.c (FUNC_000118/120 shims) ==="
	$CC $CFLAGS -c "$AMIGA/probes/ffe_timers.c" -o "$OUT/ffe_timers.o"
	EXTRA_OBJS="$EXTRA_OBJS $OUT/ffe_timers.o"
	;;
esac

echo "=== link ==="
# -lm is the SOFT-FLOAT libm (the x87-shim sqrt in ffe_game.c); the libm881
# check below still proves no FPU library sneaked in.
$CC -mcpu=68020 -msoft-float -o "$OUT/$TARGET" \
    "$OUT/$TARGET.o" $FFEAPI_OBJS $EXTRA_OBJS $XLAT_OBJS "$OUT/amiga_gfx.o" "$OUT/c2p_glue.o" "$OUT/fp_soft.o" \
    -lm -Wl,-t -Wl,-Map -Wl,"$OUT/$TARGET.symmap" \
    > "$OUT/$TARGET.linkmap" 2>&1 || { tail -30 "$OUT/$TARGET.linkmap"; exit 1; }

if grep -q libm881 "$OUT/$TARGET.linkmap"; then
	echo "FATAL: libm881 pulled in - this build needs an FPU. Check the flags."
	grep libm881 "$OUT/$TARGET.linkmap"
	exit 1
fi
echo "soft-float verified (no libm881 in the link)"

ls -l "$OUT/$TARGET"
mkdir -p "$DEPLOY"
cp "$OUT/$TARGET" "$DEPLOY/$TARGET"
echo "deployed -> $DEPLOY/$TARGET"
