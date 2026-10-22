#!/usr/bin/env python3
"""Build a SECOND PC reference executable that writes FFEFRM01 frame dumps.

Same sources, same flags and the same output layout as build-pc-ref.py, with
exactly one substitution: `sdl\\sdlvideo.cpp` is replaced by
`build\\pcdump_video.cpp`, which #includes the stock file with `VideoBlit`
renamed and re-exports its own that also dumps the back buffer. See the header
of that file for why the hook goes there.

Why a separate script instead of a flag in build-pc-ref.py: build-pc-ref.py
produces the executable the developer actually plays, and it must keep doing
exactly that. Nothing here writes into ref\\jjffe or into build\\m0-pc.

    python build\\build-pc-dump.py [--force]

produces build\\m0-pc-dump\\ffesdl-dump.exe, which drops `ffe_frame.raw` in its
working directory every 128 presents -- the same format the Amiga build writes,
so `python build\\framediff.py <pc>.raw <amiga>.raw` compares them directly.

BEFORE COMPARING: the Amiga build must have FFE_LEVEL_CLOCKS = 0 in
amiga\\probes\\ffe_game.c (it does since 2026-08-02). While that shortcut was
on, the Amiga started the universe unsimulated and no frame could match.
"""
import os
import subprocess
import sys
import time

ROOT = r"I:\GITHUB\Amiga_Frontier_2_First_Encouters"
SRC = os.path.join(ROOT, "ref", "jjffe")
OUT = os.path.join(ROOT, "build", "m0-pc-dump")
TOOLS = os.path.join(ROOT, "tools")

NASM = os.path.join(TOOLS, "nasm", "nasm.exe")
GCC = os.path.join(TOOLS, "mingw32", "bin", "gcc.exe")
GXX = os.path.join(TOOLS, "mingw32", "bin", "g++.exe")
SDL = os.path.join(TOOLS, "SDL-1.2.15")

ASM = ["ffe.asm", "ffebmp.asm"]
C = ["ffecfg.c", "ffemisc.c"]
# sdl\sdlvideo.cpp is DELIBERATELY ABSENT -- pcdump_video.cpp includes it.
CXX = ["hmp.cpp",
       r"sdl\sdlsound.cpp", r"sdl\sdlinput.cpp", r"sdl\sdltimer.cpp",
       r"win32\w32dir.cpp"]

CFLAGS = [
    "-m32", "-O2", "-D__FFEWINSDL__",
    "-I" + SRC,
    "-I" + os.path.join(SDL, "include", "SDL"),
    "-fpermissive", "-w",
    "-fno-strict-aliasing",
]
CXXFLAGS = CFLAGS + ["-std=gnu++98"]

LIBS = ["-L" + os.path.join(SDL, "lib"),
        "-lmingw32", "-lSDL", "-lopengl32"]

OURS_C = [os.path.join(ROOT, "build", "sdlmain_shim.c")]
OURS_CXX = [os.path.join(ROOT, "build", "pcdump_video.cpp")]


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

    objs, failed = [], []

    for a in ASM:
        o = os.path.join(OUT, os.path.splitext(a)[0] + ".obj")
        deps = [os.path.join(SRC, a)] + ([os.path.join(SRC, "ffedat.asm")]
                                         if a == "ffe.asm" else [])
        if not stale(o, *deps):
            print("[skip] %s" % a)
            objs.append(o)
            continue
        ok = run([NASM, "-f", "win32", "-i", SRC + os.sep,
                  os.path.join(SRC, a), "-o", o], a)
        (objs if ok else failed).append(o if ok else a)

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
        # depends on the stock file it includes, too
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

    exe = os.path.join(OUT, "ffesdl-dump.exe")
    if not run([GXX, "-m32", "-mconsole",
                "-static-libgcc", "-static-libstdc++", "-o", exe] + objs + LIBS,
               "link ffesdl-dump.exe"):
        return 1

    print("\nBUILT: %s  (%.2f MB)" % (exe, os.path.getsize(exe) / 1024.0 / 1024))
    print("It writes ffe_frame.raw in its working directory every 128 presents.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
