#!/usr/bin/env python3
"""Diff the Amiga log against the x86 oracle, record by record.

    python tools\\translator\\compare.py [amiga.log] [-n N]

Left side : build\\oracle\\vectors-x86.tsv, produced by calling the ORIGINAL x86
            functions (tools\\oracle\\build-oracle.py, then oracle.exe --tsv).
Right side: winuae\\work\\ffe_xlattest.log, produced by the translated 68k code
            running under WinUAE (amiga\\probes\\ffe_xlattest.c).

Both files are name + decimal arguments + decimal result, one call per line, in
the same order. Nothing is compared as bytes: the whole port is a little-endian
to big-endian move, and a byte-level comparison would fail on byte order alone
and tell us nothing about the translation.

Exit status is 0 only if every record matches and the trailer count adds up.
"""
import argparse
import os
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                    "..", ".."))
ORACLE = os.path.join(ROOT, "build", "oracle", "vectors-x86.tsv")
AMIGA = os.path.join(ROOT, "winuae", "work", "ffe_xlattest.log")


def load(path):
    """Returns (records, trailer_count). Line endings differ by design -- the
    oracle is written by a Windows build in text mode and the Amiga writes bare
    LF -- so they are stripped, not compared."""
    if not os.path.exists(path):
        return None, None
    records, trailer = [], None
    with open(path, "r", errors="replace") as f:
        for raw in f:
            line = raw.rstrip("\r\n")
            if not line:
                continue
            if line.startswith("#"):
                w = line.split()
                if len(w) >= 3 and w[1] == "records":
                    trailer = int(w[2])
                continue
            records.append(line.split("\t"))
    return records, trailer


def describe(rec):
    if len(rec) == 4:
        return "%s(%s, %s)" % (rec[0], rec[1], rec[2])
    if len(rec) == 3:
        return "%s(%s)" % (rec[0], rec[1])
    return " ".join(rec)


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0],
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("amiga", nargs="?", default=AMIGA,
                    help="the Amiga log (default: %(default)s)")
    ap.add_argument("--oracle", default=ORACLE,
                    help="the x86 reference TSV (default: %(default)s)")
    ap.add_argument("-n", "--max", type=int, default=20,
                    help="how many mismatches to print (default: %(default)s)")
    args = ap.parse_args(argv)

    want, _ = load(args.oracle)
    if want is None:
        print("no oracle at %s -- run tools\\oracle\\build-oracle.py, then "
              "build\\oracle\\oracle.exe --tsv > %s" % (args.oracle, args.oracle),
              file=sys.stderr)
        return 2
    got, trailer = load(args.amiga)
    if got is None:
        print("no Amiga log at %s -- run the probe under WinUAE first"
              % args.amiga, file=sys.stderr)
        return 2

    print("x86 oracle : %-6d records  %s" % (len(want), args.oracle))
    print("68k amiga  : %-6d records  %s" % (len(got), args.amiga))

    bad = 0

    # A short log is a truncated run, not a translation failure, and saying so
    # is the difference between "the port is broken" and "the emulator was
    # stopped too early".
    if trailer is None:
        print("\nWARNING: no `# records N` trailer -- the log is truncated or "
              "the probe never finished.")
        bad += 1
    elif trailer != len(got):
        print("\nWARNING: trailer says %d records, %d were read -- log corrupt."
              % (trailer, len(got)))
        bad += 1

    if len(got) != len(want):
        print("\nRECORD COUNT DIFFERS: oracle %d, amiga %d" % (len(want), len(got)))
        bad += 1

    shown = 0
    diffs = 0
    for k in range(min(len(want), len(got))):
        w, g = want[k], got[k]
        if w == g:
            continue
        diffs += 1
        if shown < args.max:
            shown += 1
            if w[:-1] == g[:-1]:
                print("\nline %d  %s" % (k + 1, describe(w)))
                print("    x86 wants %s" % w[-1])
                print("    68k gives %s" % g[-1])
            else:
                print("\nline %d  records do not even line up:" % (k + 1))
                print("    x86 %s" % "\t".join(w))
                print("    68k %s" % "\t".join(g))
    if diffs > shown:
        print("\n... and %d more mismatch(es)" % (diffs - shown))

    print()
    if bad == 0 and diffs == 0:
        print("IDENTICAL: %d records, the translated 68k matches the original "
              "x86 on every one." % len(want))
        return 0
    if diffs == 0:
        # Every record that exists agrees; the log is simply not all there.
        # Saying so is the difference between "the port is broken" and "the
        # emulator was stopped too early".
        print("INCOMPLETE: the %d record(s) present all match, but the log is "
              "short or has no trailer." % len(got))
        return 1
    print("MISMATCH: %d record(s) differ out of %d." % (diffs, len(want)))
    return 1


if __name__ == "__main__":
    sys.exit(main())
