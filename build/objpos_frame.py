#!/usr/bin/env python3
"""List the render objects (ctx position + one matrix element) seen by
FUNC_001478 in the FRAME1478 capture frame, both machines, in call order.

    py build\\objpos_frame.py
"""
import collections
import re

PC = r"I:\GITHUB\Amiga_Frontier_2_First_Encouters\build\m0-pc\run\pc_frame1478.log"
AM = r"C:\temp\ffe\work\ffe_frame1478.log"


def load(path):
    objs = collections.OrderedDict()
    for line in open(path):
        hit = re.search(r"o=(-?\d+),(-?\d+),(-?\d+) m4=(-?\d+)", line)
        if hit:
            key = tuple(int(v) for v in hit.groups())
            objs[key] = objs.get(key, 0) + 1
    return objs


for tag, path in (("PC", PC), ("AMIGA", AM)):
    print(tag)
    for key, n in load(path).items():
        print("   o=(%d,%d,%d) m4=%d  vertices=%d" % (key + (n,)))
