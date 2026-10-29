#!/bin/sh
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 angree. See LICENSE at the repository root.
#
# Builds and runs the gm_synth checks. Run inside WSL:
#   wsl -e bash /mnt/e/GITHUB/amiffe/tools/music/build_gm_tests.sh [outdir]
#
#  1. host gmtest (gcc) -> renders the demo WAVs, determinism check, benchmark
#  2. m68k compile of gm_synth.c with the project's binding flags
#     (-mcpu=68020 -msoft-float -O1; never -O2, never -m68040)
#  3. nm check: the object must not reference ANY float/double helper --
#     on a 68020 without FPU each one is a libgcc soft-float call, and the
#     synth's contract (midi_synth.h) is integer-only at run time
#  4. the 68k assembly is kept next to the object for cycle counting
#
# The bank (tools/music/ffe_gm.bnk) is built separately, on Windows:
#   python tools\music\build_gmbank.py

set -e
export PATH=/opt/amiga/bin:/usr/local/bin:/usr/bin:/bin

HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../.." && pwd)
SRC=$ROOT/amiga/music
OUT=${1:-/mnt/e/ffe/gmsrc/out}
mkdir -p "$OUT"

echo "== host build"
gcc -O1 -Wall -Wextra -std=gnu99 -I"$SRC" -o "$OUT/gmtest" "$HERE/gmtest.c" "$SRC/gm_synth.c" -lm

echo "== m68k compile (68020, soft-float, -O1)"
m68k-amigaos-gcc -mcpu=68020 -msoft-float -O1 -noixemul -Wall -c "$SRC/gm_synth.c" -o "$OUT/gm_synth.o"
m68k-amigaos-gcc -mcpu=68020 -msoft-float -O1 -noixemul -Wall -S "$SRC/gm_synth.c" -o "$OUT/gm_synth.s"
m68k-amigaos-nm "$OUT/gm_synth.o" > "$OUT/gm_synth.nm"
echo "undefined symbols:"
grep ' U ' "$OUT/gm_synth.nm" || true
if grep -E ' U ___?(add|sub|mul|div|neg|cmp|unord|eq|ne|lt|le|gt|ge)(s|d|x|t)f[23]| U ___?(fix|float|extend|trunc)[a-z]*(sf|df|xf)|ieee|IEEE' "$OUT/gm_synth.nm"; then
	echo "FAIL: gm_synth.o references floating-point helpers"
	exit 1
fi
if grep -E ' U ___?(mul|div|mod|udiv|umod)di3' "$OUT/gm_synth.nm"; then
	echo "FAIL: gm_synth.o references 64-bit arithmetic helpers"
	exit 1
fi
echo "OK: no float or 64-bit helpers referenced"
m68k-amigaos-size "$OUT/gm_synth.o" || true

echo "== host run"
"$OUT/gmtest" "$HERE/ffe_gm.bnk" "$OUT"
