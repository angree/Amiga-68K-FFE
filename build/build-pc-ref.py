#!/usr/bin/env python3
"""M0: build the JJFFE reference implementation for Windows (32-bit, SDL 1.2).

This is the reference the whole port is verified against. It builds the
__FFEWINSDL__ variant -- the file list is taken from ref\\jjffe\\jjffesdl.dsp:

    ffe.asm (which %includes ffedat.asm), ffebmp.asm      -> nasm -f win32
    ffecfg.c, ffemisc.c                                    -> gcc
    hmp.cpp, sdl\\sdl{video,sound,input,timer}.cpp,
    win32\\w32dir.cpp                                      -> g++

Nothing is ever written into ref\\jjffe -- nasm reads ffedat.asm through -i
and every output goes to build\\m0-pc\\.

The Linux makefile's lnxconv step is deliberately absent: it exists only to
rewrite Win32 underscore symbols into ELF form, and we are targeting Win32.
"""
import os, subprocess, sys, time

ROOT = r"I:\GITHUB\Amiga_Frontier_2_First_Encouters"
SRC  = os.path.join(ROOT, "ref", "jjffe")
OUT  = os.path.join(ROOT, "build", "m0-pc")
TOOLS = os.path.join(ROOT, "tools")

NASM = os.path.join(TOOLS, "nasm", "nasm.exe")
GCC  = os.path.join(TOOLS, "mingw32", "bin", "gcc.exe")
GXX  = os.path.join(TOOLS, "mingw32", "bin", "g++.exe")
SDL  = os.path.join(TOOLS, "SDL-1.2.15")

ASM = ["ffe.asm", "ffebmp.asm"]
C   = ["ffecfg.c", "ffemisc.c"]
CXX = ["hmp.cpp",
       r"sdl\sdlvideo.cpp", r"sdl\sdlsound.cpp",
       r"sdl\sdlinput.cpp", r"sdl\sdltimer.cpp",
       r"win32\w32dir.cpp"]

CFLAGS = [
    "-m32", "-O2", "-D__FFEWINSDL__",
    "-I" + SRC,
    "-I" + os.path.join(SDL, "include", "SDL"),
    # 2005-era sources through a 2026 compiler
    "-fpermissive", "-w",
    "-fno-strict-aliasing",      # the asm and C share memory freely
]
CXXFLAGS = CFLAGS + ["-std=gnu++98"]

# -lSDLmain is deliberately absent: the stock library needs MSVCRT's _iob, which
# UCRT does not have. build\sdlmain_shim.c replaces it. See that file for detail.
LIBS = ["-L" + os.path.join(SDL, "lib"),
        "-lmingw32", "-lSDL", "-lopengl32"]

# our own sources for the reference build (MIT), compiled alongside JJFFE's
OURS_C = [os.path.join(ROOT, "build", "sdlmain_shim.c")]


def stale(out, *deps):
    """True when `out` must be rebuilt. I: is a network share, so skipping a
    63-second nasm run on an unchanged ffe.asm is worth the four lines."""
    if "--force" in sys.argv or not os.path.exists(out):
        return True
    t = os.path.getmtime(out)
    return any(os.path.getmtime(d) > t for d in deps if os.path.exists(d))


def run(cmd, what):
    t = time.time()
    p = subprocess.run(cmd, capture_output=True, text=True, errors="replace")
    dt = time.time() - t
    ok = p.returncode == 0
    print(f"[{'ok ' if ok else 'FAIL'}] {what}  ({dt:.1f}s)")
    if not ok:
        err = (p.stderr or p.stdout)
        lines = [l for l in err.splitlines() if l.strip()]
        for l in lines[:40]:
            print("      " + l)
        if len(lines) > 40:
            print(f"      ... {len(lines)-40} more lines")
    return ok, p


def main():
    os.makedirs(OUT, exist_ok=True)
    for t, n in ((NASM, "nasm"), (GCC, "gcc"), (GXX, "g++")):
        if not os.path.exists(t):
            print(f"MISSING TOOL: {n} at {t}")
            return 1

    objs, failed = [], []

    for a in ASM:
        o = os.path.join(OUT, os.path.splitext(a)[0] + ".obj")
        # ffe.asm textually %includes ffedat.asm (ffe.asm:1790)
        deps = [os.path.join(SRC, a)] + ([os.path.join(SRC, "ffedat.asm")]
                                         if a == "ffe.asm" else [])
        if not stale(o, *deps):
            print(f"[skip] {a}")
            objs.append(o); continue
        ok, _ = run([NASM, "-f", "win32", "-i", SRC + os.sep,
                     os.path.join(SRC, a), "-o", o], a)
        (objs if ok else failed).append(o if ok else a)

    for f in C:
        o = os.path.join(OUT, os.path.basename(f)[:-2] + ".o")
        if not stale(o, os.path.join(SRC, f)):
            print(f"[skip] {f}"); objs.append(o); continue
        ok, _ = run([GCC] + CFLAGS + ["-c", os.path.join(SRC, f), "-o", o], f)
        (objs if ok else failed).append(o if ok else f)

    for f in CXX:
        o = os.path.join(OUT, os.path.basename(f)[:-4] + ".o")
        if not stale(o, os.path.join(SRC, f)):
            print(f"[skip] {f}"); objs.append(o); continue
        ok, _ = run([GXX] + CXXFLAGS + ["-c", os.path.join(SRC, f), "-o", o], f)
        (objs if ok else failed).append(o if ok else f)

    for f in OURS_C:
        o = os.path.join(OUT, os.path.basename(f)[:-2] + ".o")
        if not stale(o, f):
            print(f"[skip] {os.path.basename(f)}"); objs.append(o); continue
        ok, _ = run([GCC] + CFLAGS + ["-c", f, "-o", o], os.path.basename(f))
        (objs if ok else failed).append(o if ok else f)

    if failed:
        print(f"\n{len(failed)} translation unit(s) failed: {', '.join(map(str, failed))}")
        return 1

    # Without these the exe needs libstdc++-6.dll / libgcc_s_dw2-1.dll from the
    # MinGW bin dir and dies with 0xC0000135 (STATUS_DLL_NOT_FOUND). Full -static
    # is not an option: the SDL 1.2 mingw package ships only the import library
    # (libSDL.dll.a), so ld would demand a libSDL.a that does not exist. SDL.dll
    # therefore stays a runtime dependency and is copied next to the exe.
    exe = os.path.join(OUT, "ffesdl.exe")
    ok, _ = run([GXX, "-m32", "-mconsole",
                 "-static-libgcc", "-static-libstdc++", "-o", exe] + objs + LIBS,
                "link ffesdl.exe")
    if not ok:
        return 1

    print(f"\nBUILT: {exe}  ({os.path.getsize(exe)/1024/1024:.2f} MB)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
