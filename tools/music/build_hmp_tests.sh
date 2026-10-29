#!/bin/sh
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 angree. See LICENSE at the repository root.
#
# Build the host tools for amiga/music/hmp.c (WSL gcc) and check that hmp.c
# compiles for the Amiga. Run from PowerShell:
#   wsl -e sh /mnt/e/GITHUB/amiffe/tools/music/build_hmp_tests.sh
# then
#   /tmp/hmptest/hmptest_oracle /mnt/e/ffe/wtest/FFE/MUSIC/*.HMP
#
# hmptest_oracle links JJFFE's converter (ref/jjffe/hmp.cpp) in as a TEST
# ORACLE only. Its DWORD is "unsigned long", 8 bytes on a 64-bit host, so it
# is compiled through a generated wrapper that maps long to int after the
# system headers it includes are already in.
set -e
export PATH=/opt/amiga/bin:/usr/local/bin:/usr/bin:/bin
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../.." && pwd)
OUT=${OUT:-/tmp/hmptest}
mkdir -p "$OUT"
CF="-std=c99 -O1 -Wall -Wextra -g -I$ROOT/amiga/music"

gcc $CF -o "$OUT/hmp2mid" "$HERE/hmp2mid.c" "$ROOT/amiga/music/hmp.c"
gcc $CF -o "$OUT/hmptest" "$HERE/hmptest.c" "$ROOT/amiga/music/hmp.c"
gcc $CF -fsanitize=address,undefined -o "$OUT/hmptest_asan" "$HERE/hmptest.c" "$ROOT/amiga/music/hmp.c"

if [ -f "$ROOT/ref/jjffe/hmp.cpp" ]; then
	{
		echo '#include <memory.h>'
		echo '#include <malloc.h>'
		echo '#include <stdlib.h>'
		echo '#define long int'
		echo "#include \"$ROOT/ref/jjffe/hmp.cpp\""
	} > "$OUT/ref_hmp_wrap.cpp"
	g++ -O1 -w -c "$OUT/ref_hmp_wrap.cpp" -o "$OUT/ref_hmp.o"
	gcc $CF -DHMP_ORACLE -c "$HERE/hmptest.c" -o "$OUT/hmptest_oracle.o"
	gcc $CF -c "$ROOT/amiga/music/hmp.c" -o "$OUT/hmp.o"
	g++ -o "$OUT/hmptest_oracle" "$OUT/hmptest_oracle.o" "$OUT/hmp.o" "$OUT/ref_hmp.o"
fi

m68k-amigaos-gcc -mcpu=68020 -msoft-float -O1 -noixemul -Wall -Wextra \
	-c "$ROOT/amiga/music/hmp.c" -o "$OUT/hmp.68k.o"
echo "built in $OUT"
