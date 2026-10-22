#!/usr/bin/env python3
"""x86 -> 68020 static translator, entry point.

    python tools\\translator\\translate.py FUNC_001521 [FUNC_001522 ...] -o build\\xlat

One .s per original function, vasm Motorola syntax, assembled with

    vasmm68k_mot -Fhunk -m68020 -no-opt

The pieces: parse.py reads ffe.asm, flags.py computes flag liveness, lower.py
selects instructions and proves the flag behaviour legal, emit.py writes the
file. PLAN 3 (translator design) and PLAN 4 (flags) are the specification; this
file is only the driver.

Nothing here is best-effort. Any instruction, addressing mode or control-flow
shape the translator has not been taught raises with the ffe.asm line number and
the original text, and nothing is written. Silent wrong output is the one
failure this whole apparatus exists to prevent -- a bad guess buried in 113,000
instructions costs a full emulator cycle to find.
"""
import argparse
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import emit                                        # noqa: E402
from lower import HANDLED                          # noqa: E402
from parse import TranslationError, read_function  # noqa: E402

ROOT = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                    "..", ".."))
DEFAULT_ASM = os.path.join(ROOT, "ref", "jjffe", "ffe.asm")


def main(argv=None):
    ap = argparse.ArgumentParser(
        description=__doc__.split("\n")[0],
        epilog="x86 instructions handled so far:\n" + HANDLED,
        formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("funcs", nargs="+", metavar="FUNC_nnnnnn",
                    help="function labels as they appear in ffe.asm")
    ap.add_argument("-o", "--out", required=True, metavar="DIR",
                    help="output directory for the generated .s files")
    ap.add_argument("--asm", default=DEFAULT_ASM,
                    help="path to ffe.asm (default: %(default)s)")
    ap.add_argument("--no-c-entry", action="store_true",
                    help="emit only the t-ABI body, no C-callable wrapper")
    args = ap.parse_args(argv)

    if not os.path.exists(args.asm):
        print("no such file: %s" % args.asm, file=sys.stderr)
        return 2
    os.makedirs(args.out, exist_ok=True)

    # ffe.asm is 2.5 MB, so it is read once per function rather than kept in
    # memory as a parsed whole; at three functions that is not worth caching,
    # and at 1,621 the full-file mode of PLAN 3.4 replaces this loop anyway.
    rc = 0
    for name in args.funcs:
        try:
            fn = read_function(args.asm, name)
            text = emit.emit(fn, os.path.relpath(args.asm, ROOT).replace("/", "\\"),
                             c_entry_wanted=not args.no_c_entry)
        except TranslationError as e:
            print("REFUSED %s: %s" % (name, e), file=sys.stderr)
            rc = 1
            continue

        path = os.path.join(args.out, fn.base + ".s")
        with open(path, "w", newline="\n", encoding="ascii") as f:
            f.write(text)
        print("%-14s %s:%-6d %2d x86 -> %2d 68k   %s"
              % (fn.name, "ffe.asm", fn.line, len(fn.insns),
                 sum(1 for l in text.split("\n")
                     if l.startswith("\t") and not l.lstrip().startswith((";", "xdef", "section"))),
                 path))
    return rc


if __name__ == "__main__":
    sys.exit(main())
