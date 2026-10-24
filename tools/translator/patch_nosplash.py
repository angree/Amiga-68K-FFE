"""Skip JJFFE's start-up notice ('Press "y" if you accept this...') in the
translated _asmmain and replace its text (2026-10-16, developer's decision: no
accept prompt; the attribution lives in README.md and the $VER string).

ref\\jjffe\\ffe.asm is never edited, so both cuts are made in emitted files:

1. build\\xlat-full\\_asmmain.s: right after `jsr FUNC_001527` (SysInit,
   ffe.asm:68290) a branch to LOC_ain_splashnoexit (ffe.asm:68365) jumps over
   the notice and its wait-for-'y' loop. Everything the notice set up (palette,
   pointer off, cleared buffers) is done again by _asmmain right after that
   label.
2. build\\xlat-full\\support_data.s: the nine pSplash strings (ffe.asm:
   68222-68232) are no longer shown, so their text is replaced with the port's
   own credits; the outdated warning and the dead eliteclub link go.

Idempotent; run by build\\build-amiga-local.sh before assembly. Prints what it
did and fails loudly if an anchor is not found."""
import os, re, sys

D = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..",
                 "build", "xlat-full")

# --- 1. skip the notice -----------------------------------------------------
P = os.path.join(D, "_asmmain.s")
MARK = "\tbra\tLOC_ain_splashnoexit\t; patch_nosplash.py: skip the JJFFE notice\n"
OLDMARK = "\tbra\tLOC_ain_splashnoexit\t; patch_nosplash.py: skip the JJFFE accept screen\n"

s = open(P, encoding="latin-1").read()
if MARK in s or OLDMARK in s:
    print("patch_nosplash: branch already applied")
else:
    anchor = "\tjsr\tFUNC_001527\n"
    if s.count(anchor) != 1 or "LOC_ain_splashnoexit:" not in s:
        sys.exit("patch_nosplash: anchors not found in %s -- refusing" % P)
    s = s.replace(anchor, anchor + MARK)
    open(P, "w", encoding="latin-1").write(s)
    print("patch_nosplash: branch applied")

# --- 2. replace the notice text ---------------------------------------------
TEXT = [
    "AmiFFE 0.1.3 - Amiga 68k port",
    "Frontier: First Encounters (c) Frontier Developments",
    "Based on JJFFE by John Jordan",
    "Unofficial fan project, not endorsed by Frontier",
    "Needs the original DOS game files",
    "", "", "", "",
]
P = os.path.join(D, "support_data.s")
s = open(P, encoding="latin-1").read()
for i, t in enumerate(TEXT, 1):
    pat = re.compile(r"(\npSplash%d:\n\tdc\.b\t)(?:'[^\n]*',0|0)\n" % i)
    if len(pat.findall(s)) != 1:
        sys.exit("patch_nosplash: pSplash%d not found in %s -- refusing" % (i, P))
    new = ("'%s',0\n" % t) if t else "0\n"
    s = pat.sub(lambda m: m.group(1) + new, s)
open(P, "w", encoding="latin-1").write(s)
print("patch_nosplash: notice text replaced")
