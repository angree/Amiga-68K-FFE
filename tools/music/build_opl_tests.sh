#!/bin/bash
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 angree. See LICENSE at the repository root.
#
# Build and run the OPL2 / AdLib-MIDI tests. Run inside WSL:
#   wsl -e bash /mnt/e/GITHUB/amiffe/tools/music/build_opl_tests.sh [OUTDIR] [MELODIC.BNK] [DRUM.BNK]
#
# 1. host opltest: table check, the listening WAVs into OUTDIR, the benchmark
# 2. the Amiga compile of opl2.c and adlib_midi.c (bebbo gcc, 68020, soft
#    float, -O1) and an nm check that neither object references a float or
#    double helper (__mulsf3, __adddf3, __floatsisf, __fixdfsi, ...)
set -e
export PATH=/opt/amiga/bin:/usr/local/bin:/usr/bin:/bin

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
OUT="${1:-/tmp/opltest}"
MEL="${2:-/mnt/e/ffe/wtest/FFE/MELODIC.BNK}"
DRM="${3:-/mnt/e/ffe/wtest/FFE/DRUM.BNK}"
BIN=/tmp/opltest-bin
mkdir -p "$OUT" "$BIN"

echo "== host build"
gcc -O2 -Wall -Wextra -o "$BIN/opltest" "$ROOT/tools/music/opltest.c" -lm

echo "== tables"
"$BIN/opltest" --check-tables

echo "== listening tests -> $OUT"
"$BIN/opltest" "$MEL" "$DRM" "$OUT"

echo "== benchmark (host)"
"$BIN/opltest" "$MEL" "$DRM" --bench

echo "== amiga compile"
fail=0
for f in opl2 adlib_midi; do
	m68k-amigaos-gcc -mcpu=68020 -msoft-float -O1 -noixemul -Wall \
		-c "$ROOT/amiga/music/$f.c" -o "$BIN/$f.o"
	m68k-amigaos-gcc -mcpu=68020 -msoft-float -O1 -noixemul -Wall \
		-S "$ROOT/amiga/music/$f.c" -o "$BIN/$f.s"
	und="$(m68k-amigaos-nm "$BIN/$f.o" | awk '$1 == "U" { print $2 }' | tr '\n' ' ')"
	echo "$f.o undefined: $und"
	if m68k-amigaos-nm "$BIN/$f.o" | grep -E '__[a-z]+(sf|df|xf)[0-9]?$|__float|__fix|__extend|__trunc' ; then
		echo "FAIL: $f.o references a float helper"
		fail=1
	fi
done
[ $fail = 0 ] && echo "amiga objects: no float helpers"
echo "asm listings: $BIN/opl2.s $BIN/adlib_midi.s"
exit $fail
