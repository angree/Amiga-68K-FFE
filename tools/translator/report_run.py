# SPDX-License-Identifier: MIT
# VIEWPORT DEBUG: one command that reads a finished harness run and says what
# it measured -- so the post-run step is not five ad-hoc PowerShell lines that
# each risk hitting the file while the emulator still holds it.
#
# Prints, in order:
#   the run boundaries and every "first light" marker (stale-binary check)
#   the per-window dump lines that matter: presents / LJ / POOL16 / NODE72
#   LONGJMP evidence
#   CNT (call counters) and A8 (first-call arguments)
#   the RET caller census, WITH the addresses resolved to symbol+offset
#
# Read-only. Debug rig; delete with the rest of it.

import io
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
LOG = os.path.join(ROOT, "winuae", "work", "ffe_dump.log")

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import resolve_samples as rs


def read_retry(path, tries=40):
    """The emulator holds the log open; a denied read is not an empty file."""
    last = None
    for _ in range(tries):
        try:
            return io.open(path, encoding="latin-1", errors="replace").read()
        except Exception as exc:          # PermissionError on the SMB share
            last = exc
    raise SystemExit("could not read %s: %s" % (path, last))


def main():
    text = read_retry(LOG)
    lines = text.split("\n")
    print("%s  (%d lines)" % (LOG, len(lines)))

    want = re.compile(r"first light|LONGJMP|^LJ |^presents=|^CNT|^A8 |^RET ")
    keep = [l for l in lines if want.search(l)]
    print("")
    for l in keep[-60:]:
        print("  " + l)

    # resolve the last RET census
    ret = [l for l in lines if l.startswith("RET total=")]
    if not ret:
        print("\nno RET census in this run (was --ret injected and rebuilt?)")
        return 0
    print("\n=== caller census, resolved (last window) ===")
    syms = rs.load_symbols(rs.SYMMAP)
    base, err = rs.find_base(rs.ENTER, syms)
    if err:
        print("BASE UNKNOWN: %s" % err)
        return 3
    toks = ret[-1].split()[2:]
    rows = []
    for t in toks:
        if "=" not in t:
            continue
        a, c = t.split("=", 1)
        try:
            pc, n = int(a, 16), int(c)
        except ValueError:
            continue
        nm, off = rs.resolve(pc - base, syms)
        rows.append((n, nm or "<unknown>", off, pc))
    rows.sort(reverse=True)
    total = sum(r[0] for r in rows)
    print("  %-8s %-7s %-22s %s" % ("calls", "share", "caller", "site"))
    for n, nm, off, pc in rows:
        print("  %-8d %-6.1f%% %-22s +0x%-6x (%08x)"
              % (n, 100.0 * n / total if total else 0, nm, off, pc))
    print("  total counted: %d" % total)
    return 0


if __name__ == "__main__":
    sys.exit(main())
