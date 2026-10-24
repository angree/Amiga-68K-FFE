# SPDX-License-Identifier: MIT
# Copyright (c) 2026 angree. See LICENSE at the repository root.
"""Compare the frame sequences of two equivalence runs (winuae/harness/eq_run.ps1).

    python eq_compare.py E:/ffe/eq/A E:/ffe/eq/B

Frames are matched by present number (fNNNNN.raw); with the fixed clock step
the same number is the same game time in both runs. Prints how many common
frames are byte-identical and the first ones that are not.
"""
import hashlib
import os
import sys


def frames(d):
    return {f: hashlib.md5(open(os.path.join(d, f), "rb").read()).hexdigest()
            for f in os.listdir(d) if f.startswith("f") and f.endswith(".raw")}


a, b = frames(sys.argv[1]), frames(sys.argv[2])
common = sorted(set(a) & set(b))
diff = [f for f in common if a[f] != b[f]]
print("frames: %d in A, %d in B, %d common, %d identical, %d different" % (
    len(a), len(b), len(common), len(common) - len(diff), len(diff)))
if common:
    print("common range %s .. %s" % (common[0], common[-1]))
for f in diff[:10]:
    print("DIFFERENT", f)
sys.exit(1 if diff or not common else 0)
