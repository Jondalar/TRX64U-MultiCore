#!/usr/bin/env python3
"""Print, for every image row that is not uniform, its colour runs (x, width, palette index)."""
import sys
from PIL import Image
PAL = [0x000000, 0xFFFFFF, 0x68372B, 0x70A4B2, 0x6F3D86, 0x588D43, 0x352879, 0xB8C76F,
       0x6F4F25, 0x433900, 0x9A6759, 0x444444, 0x6C6C6C, 0x9AD284, 0x6C5EB5, 0x959595]
im = Image.open(sys.argv[1]).convert("RGB")
w, h = im.size
px = im.load()
idx = {((c >> 16) & 255, (c >> 8) & 255, c & 255): i for i, c in enumerate(PAL)}
prev = None
for y in range(h):
    row = [idx.get(px[x, y], -1) for x in range(w)]
    if len(set(row)) == 1:
        continue
    runs, s = [], 0
    for x in range(1, w + 1):
        if x == w or row[x] != row[s]:
            runs.append((s, x - s, row[s]))
            s = x
    key = str(runs)
    if key != prev:
        print(y, " ".join(f"{x}+{n}:{c}" for x, n, c in runs))
    prev = key
