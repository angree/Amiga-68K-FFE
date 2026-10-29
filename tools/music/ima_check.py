# SPDX-License-Identifier: MIT
# Copyright (c) 2026 angree. See LICENSE at the repository root.
"""Decode IMA ADPCM WAVs written by ffe-music (independently of our C code),
report length / peak / RMS / clipping, and optionally write 16-bit PCM copies
for listening on a PC.
    python ima_check.py DIR [--pcm OUTDIR]"""
import os
import struct
import sys

IDX = [-1, -1, -1, -1, 2, 4, 6, 8] * 2
STEP = [7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45, 50, 55, 60, 66,
        73, 80, 88, 97, 107, 118, 130, 143, 157, 173, 190, 209, 230, 253, 279, 307, 337, 371, 408, 449,
        494, 544, 598, 658, 724, 796, 876, 963, 1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066, 2272,
        2499, 2749, 3024, 3327, 3660, 4026, 4428, 4871, 5358, 5894, 6484, 7132, 7845, 8630, 9493,
        10442, 11487, 12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767]


def decode(b):
    rate = struct.unpack_from("<I", b, 24)[0]
    align = struct.unpack_from("<H", b, 32)[0]
    p = b.index(b"data") + 8
    out = []
    while p + align <= len(b):
        blk = b[p:p + align]
        pred = struct.unpack_from("<h", blk, 0)[0]
        idx = blk[2]
        out.append(pred)
        for byte in blk[4:]:
            for code in (byte & 15, byte >> 4):
                step = STEP[idx]
                dq = step >> 3
                if code & 4: dq += step
                if code & 2: dq += step >> 1
                if code & 1: dq += step >> 2
                pred = max(-32768, min(32767, pred - dq if code & 8 else pred + dq))
                idx = max(0, min(88, idx + IDX[code]))
                out.append(pred)
        p += align
    return rate, out


d = sys.argv[1]
pcm_dir = sys.argv[sys.argv.index("--pcm") + 1] if "--pcm" in sys.argv else None
if pcm_dir:
    os.makedirs(pcm_dir, exist_ok=True)
for f in sorted(os.listdir(d)):
    if not f.endswith(".wav"):
        continue
    b = open(os.path.join(d, f), "rb").read()
    rate, s = decode(b)
    peak = max(abs(x) for x in s)
    rms = (sum(x * x for x in s[::7]) / len(s[::7])) ** 0.5
    clip = sum(1 for x in s if abs(x) >= 32767)
    print("%-14s %7d bytes %6.1f s %5d Hz peak %5d rms %5d clipped %d" % (f, len(b), len(s) / rate, rate, peak, rms, clip))
    if pcm_dir:
        data = struct.pack("<%dh" % len(s), *s)
        hdr = b"RIFF" + struct.pack("<I", 36 + len(data)) + b"WAVEfmt " + struct.pack("<IHHIIHH", 16, 1, 1, rate, rate * 2, 2, 16) + b"data" + struct.pack("<I", len(data))
        open(os.path.join(pcm_dir, f), "wb").write(hdr + data)
