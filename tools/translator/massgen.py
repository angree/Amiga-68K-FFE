#!/usr/bin/env python3
r"""Mass translation for the full-game link.

Translates EVERY function label in ffe.asm (plus _asmmain) into
build\xlat-full\, choosing per function whether a C-ABI entry can be
generated (has_standard_prologue -- same policy the survey uses). Functions
the translator refuses become entries in stubs.s: both the bare t-ABI label
and the _-wrapper jump to _FfeStubHit with the function's number, so the
FIRST refused function the running game actually needs announces itself by
name instead of crashing anonymously.

This tool exists for the run test. The oracle harness keeps using
build\xlat\ and translate.py exactly as before.
"""
import io
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
ROOT = os.path.dirname(os.path.dirname(HERE))

import parse
import emit
from parse import TranslationError

ASM = os.path.join(ROOT, "ref", "jjffe", "ffe.asm")
OUTDIR = os.path.join(ROOT, "build", "xlat-full")


def main():
    lines = io.open(ASM, encoding="latin-1").readlines()

    # read_function re-reads the 2.5 MB file per call; feed it the cache.
    class _F:
        def readlines(self):
            return lines

        def __enter__(self):
            return self

        def __exit__(self, *a):
            return False

    parse.open = lambda *a, **k: _F()

    names = [m.group(1) for m in
             (re.match(r"^(FUNC_\d+(?:_\w+)?):", l) for l in lines) if m]
    # jjffe's hand-written functions, outside the FUNC_nnnnnn pattern.
    # Deliberately absent: _SetJmp/_LongJmp (x86 register-image saves --
    # C shims in the game main instead), NullKill (a deliberate `db 0xff`
    # crash -- C shim), VideoData1/VideoData2 (x87 float constants that
    # ffedis mis-disassembled as code -- raw bytes in support_data.s).
    # FUNC_CheckInputWrap is also absent: its word reads of bare [esp+4]
    # slots have no in-function dword proof for frame.py, and it is a
    # 15-line saturating add -- a C shim in ffe_game.c instead.
    names += ["_asmmain", "FUNC_extravideo", "FUNC_FindFORNew",
              "FUNC_SetupCombatVelocity",
              "FUNC_CalcKeyInput", "FUNC_CalcKeyInputNoPause",
              "Timer50Hz", "_BufferBlit", "_BufferMaskedBlit",
              "_BufferReverseBlit", "_strncpyfill"]

    # x87 functions carried as C SHIMS in ffe_game.c (legal: the t-ABI caller
    # assumes less than the C ABI provides). No stub may be emitted for these
    # or the link sees the symbol twice.
    #   FUNC_000833: 3D point distance -- integer normalisation + one sqrt +
    #   one truncating imul; IEEE double sqrt of an integer matches the x87's
    #   bit for bit, and sqrt(k) is never exactly n+1/2, so the fistp
    #   round-to-nearest equals the shim's floor(x+0.5).
    #   FUNC_001877: the planet limb / atmosphere-halo renderer -- 97 x87 ops
    #   plus a SECTION .data island inside the body. Ported by hand in
    #   amiga\probes\ffe_planet.c; see build\datamap\func1877_analysis.md.
    names = [n for n in names if n not in ("FUNC_000833", "FUNC_001877",
                                        "FUNC_000112", "FUNC_001486",
                                        "FUNC_000118", "FUNC_000120",
                                        # model-variable decoders, C in ffe_timers.c (2026-10-09)
                                        "FUNC_001473", "FUNC_001474")]

    os.makedirs(OUTDIR, exist_ok=True)

    # BISECTION BREAKPOINTS: build\xlat-full\force_stub.txt names functions
    # to stub REGARDLESS of translatability. A forced stub either gets HIT
    # (everything on the path before it ran clean -- the run log names it) or
    # the run hangs first (the fault is earlier). Delete the file to disarm.
    force = set()
    exclude = set()
    fpath = os.path.join(OUTDIR, "force_stub.txt")
    if os.path.exists(fpath):
        for l in io.open(fpath):
            l = l.strip()
            if not l:
                continue
            if l.startswith("!"):
                exclude.add(l[1:])     # no translation, NO stub either --
                                       # a hand-written .s provides the symbol
            else:
                force.add(l)
        if force:
            print("FORCED stubs (bisection):", " ".join(sorted(force)))
        if exclude:
            print("EXCLUDED (hand .s provides):", " ".join(sorted(exclude)))

    # ENTRY TRACING: build\xlat-full\trace_funcs.txt names functions whose
    # bare-label entry gets an injected, register-transparent call to
    # _FfeEnterLog(number) (C, in ffe_game.c). MOVEM saves everything and
    # touches no CCR; translated bodies take args from the stack only, so
    # the injection is invisible to them. Debug only -- delete the file.
    tracefn = set()
    tpath = os.path.join(OUTDIR, "trace_funcs.txt")
    if os.path.exists(tpath):
        tracefn = {parse.canon(l.strip()) for l in io.open(tpath) if l.strip()}
        print("ENTRY-TRACED:", " ".join(sorted(tracefn)))

    def inject_trace(text, base):
        num = int(base.split("_")[1])
        pay = ("\tmovem.l\td0-d7/a0-a6,-(sp)\n"
               "\tmove.l\t#%d,-(sp)\n"
               "\tjsr\t_FfeEnterLog\n"
               "\taddq.l\t#4,sp\n"
               "\tmovem.l\t(sp)+,d0-d7/a0-a6\n" % num)
        lines_ = text.split("\n")
        for k, l in enumerate(lines_):
            if l == base + ":":
                lines_[k] = l + "\n" + pay.rstrip("\n")
                break
        return "\n".join(lines_)

    ok, refused = [], []
    for n in names:
        if parse.canon(n) in exclude or n in exclude:
            continue
        if parse.canon(n) in force or n in force:
            refused.append((n, "FORCED stub for runtime bisection"))
            continue
        try:
            fn = parse.read_function(ASM, n)
            text = emit.emit(fn, "ffe.asm",
                            c_entry_wanted=emit.has_standard_prologue(fn))
            if fn.base in tracefn:
                text = inject_trace(text, fn.base)
            path = os.path.join(OUTDIR, fn.base + ".s")
            with io.open(path, "w", encoding="utf-8", newline="\n") as f:
                f.write(text)
            ok.append(fn.base)
        except TranslationError as e:
            refused.append((n, str(e).split("\n")[0][:100]))
        except Exception as e:  # noqa: BLE001 -- a mass run must not stop
            refused.append((n, "PYERR %s: %s" % (type(e).__name__, e)))

    # stubs.s -- every refused function, loudly.
    out = ["; GENERATED by massgen.py -- stubs for functions the translator",
           "; refuses. A stub that RUNS calls _FfeStubHit(number) and never",
           "; returns, so the first one the game needs names itself.",
           "\tsection\tcode,code",
           "\txref\t_FfeStubHit", ""]
    for n, why in refused:
        base = parse.canon(n) if n.startswith("FUNC_") else n
        m = re.match(r"^FUNC_(\d+)$", base)
        num = int(m.group(1)) if m else 0
        out.append("; %s -- %s" % (n, why))
        out.append("\txdef\t%s" % base)
        out.append("\txdef\t_%s" % base)
        out.append("%s:" % base)
        out.append("_%s:" % base)
        out.append("\tmove.l\t#%d,-(sp)" % num)
        out.append("\tjsr\t_FfeStubHit")
        out.append("")
    with io.open(os.path.join(OUTDIR, "stubs.s"), "w", encoding="utf-8",
                 newline="\n") as f:
        f.write("\n".join(out) + "\n")

    with io.open(os.path.join(OUTDIR, "refused.txt"), "w",
                 encoding="utf-8", newline="\n") as f:
        for n, why in refused:
            f.write("%s\t%s\n" % (n, why))

    print("translated: %d   refused (stubbed): %d   -> %s"
          % (len(ok), len(refused), OUTDIR))


if __name__ == "__main__":
    main()
