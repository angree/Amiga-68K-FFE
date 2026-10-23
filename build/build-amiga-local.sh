#!/bin/bash
# LOCAL-DISK wrapper around build-amiga.sh (2026-09-25).
#
# WHY: the I: drive is a NAS shared with several other projects' builds; with it
# loaded, one ffe_game build took 45 minutes -- nearly all of it the linker
# reading ~1,500 objects and writing a 3.5 MB binary + map over SMB. This mirrors
# the INPUTS into a WSL-local directory (rsync -a: mtimes preserved, so vasm's
# incremental check keeps working), builds there, and copies the results back.
#
#   wsl -e bash /mnt/c/temp/ffe/build-local.sh [target]    (launcher on C:, survives a dropped /mnt/i)
#   Everything local lives under C:	empfe (developer's choice, 2026-09-25): TEMPORARY, keep it small.
#
# Results: build/amiga-out/<target>(.symmap) on I: as before, and the binary is
# deployed to BOTH winuae/work (I:) and the local Work: mirror used by
# ffe-020-aga-alllocal.uae.
set -e
# (2026-10-09) the working copy now lives on C:	empmiffe; FFE_SRC overrides
SRC=${FFE_SRC:-/mnt/e/GITHUB/amiffe}
LOC=${FFE_LOC:-/mnt/e/ffe/mirror}
LWORK=${FFE_LWORK:-/mnt/e/ffe/work}
TARGET=${1:-ffe_game}
mkdir -p "$LOC/build" "$LOC/winuae/work" "$LWORK"
t0=$(date +%s)
# CHECKSUM sync WITHOUT preserving times (-rc, no -t): the NAS clock is not
# the local clock, so copying I:'s mtimes made a freshly re-injected .s look
# OLDER than the .o built from its previous version -- vasm skipped it and the
# binary kept the old code (measured 2026-09-25: a probe injected into
# FUNC_001623 never ran while the counter it replaced still counted). Files
# rsync actually transfers get mtime = now, which is exactly what the
# incremental `-nt' check in build-amiga.sh needs.
# no JJFFE accept screen (developer, 2026-10-16): patch the emitted _asmmain
python3 "$SRC/tools/translator/patch_nosplash.py" || exit 1
mkdir -p "$LOC/amiga" "$LOC/build/xlat-full" "$LOC/build/data-be"
rsync -rc --delete "$SRC/amiga/" "$LOC/amiga/"
rsync -rc --delete "$SRC/build/xlat-full/" "$LOC/build/xlat-full/"
rsync -rc "$SRC/build/data-be/"*.s "$LOC/build/data-be/"
cp -p "$SRC/build/build-amiga.sh" "$LOC/build/build-amiga.sh"
if [ ! -d "$LOC/build/amiga-out/xlat-full" ]; then
	echo "=== first run: seeding local objects from I: ==="
	mkdir -p "$LOC/build/amiga-out"
	rsync -a "$SRC/build/amiga-out/xlat-full/" "$LOC/build/amiga-out/xlat-full/"
fi
echo "mirror synced in $(( $(date +%s) - t0 )) s"
FFE_ROOT="$LOC" FFE_DEPLOY="$LWORK" bash "$LOC/build/build-amiga.sh" "$TARGET"
cp "$LOC/build/amiga-out/$TARGET" "$SRC/build/amiga-out/$TARGET"
[ -f "$LOC/build/amiga-out/$TARGET.symmap" ] && cp "$LOC/build/amiga-out/$TARGET.symmap" "$SRC/build/amiga-out/$TARGET.symmap"
cp "$LOC/build/amiga-out/$TARGET" "$SRC/winuae/work/$TARGET"
md5sum "$LOC/build/amiga-out/$TARGET"
echo "total $(( $(date +%s) - t0 )) s"
