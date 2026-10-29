#!/bin/sh
# Build ffe-music (amiga/tools/ffe-music.c) for the host and for the Amiga.
#   sh build_render.sh [OUTDIR]       host binary + Amiga binary into OUTDIR
# GM support is compiled in when amiga/music/gm_synth.c exists.
set -e
R=$(cd "$(dirname "$0")/../.." && pwd)
O=${1:-/tmp/ffemusic}
mkdir -p "$O"
M="$R/amiga/music"
SRC="$R/amiga/tools/ffe-music.c $M/hmp.c $M/opl2.c $M/adlib_midi.c $M/ima_enc.c"
DEF=""
if [ -f "$M/gm_synth.c" ]; then SRC="$SRC $M/gm_synth.c"; DEF="-DFFE_HAVE_GM"; fi
gcc -O2 -Wall $DEF -o "$O/ffe-music" $SRC
echo "host: $O/ffe-music"
export PATH=/opt/amiga/bin:$PATH
m68k-amigaos-gcc -mcpu=68020 -msoft-float -O1 -noixemul -Wall $DEF -o "$O/ffe-music.amiga" $SRC "$R/amiga/ffeapi/ffe_progdir.c"
ls -l "$O/ffe-music.amiga"
if m68k-amigaos-nm "$O/ffe-music.amiga" 2>/dev/null | grep -qE '__(add|sub|mul|div)(s|d)f3'; then
	echo "FATAL: float helpers in ffe-music"; exit 1
fi
echo "amiga: $O/ffe-music.amiga (no float helpers)"
