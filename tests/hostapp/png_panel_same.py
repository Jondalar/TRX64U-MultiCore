#!/usr/bin/env python3
"""png_panel_same.py A.png B.png [W H]: exit 0 when the top-left W x H pixels (default 960 x 540, the host's own panel) of two PNGs are equal.
The emulated machine behind the panel (its blinking cursor) is outside. Standard library only (8-bit, not interlaced)."""
import struct, sys, zlib


def load(path):
    d = open(path, "rb").read()
    pos, idat, w = 8, b"", 0
    while pos < len(d):
        n, t = struct.unpack(">I4s", d[pos:pos + 8])
        c = d[pos + 8:pos + 8 + n]
        if t == b"IHDR":
            w, h, depth, ct, _, _, il = struct.unpack(">IIBBBBB", c)
            assert depth == 8 and il == 0 and ct in (2, 6), "unsupported PNG"
            bpp = 3 if ct == 2 else 4
        elif t == b"IDAT":
            idat += c
        pos += 12 + n
    raw, stride, rows, prev = zlib.decompress(idat), w * bpp, [], bytearray(w * bpp)
    for y in range(h):
        f, line = raw[y * (stride + 1)], bytearray(raw[y * (stride + 1) + 1:(y + 1) * (stride + 1)])
        for i in range(stride):
            a = line[i - bpp] if i >= bpp else 0
            b = prev[i]
            c2 = prev[i - bpp] if i >= bpp else 0
            if f == 1: line[i] = (line[i] + a) & 255
            elif f == 2: line[i] = (line[i] + b) & 255
            elif f == 3: line[i] = (line[i] + (a + b) // 2) & 255
            elif f == 4:
                p = a + b - c2
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c2)
                line[i] = (line[i] + (a if pa <= pb and pa <= pc else b if pb <= pc else c2)) & 255
        rows.append(bytes(line)); prev = line
    return rows, bpp


a, b = sys.argv[1], sys.argv[2]
W, H = (int(sys.argv[3]), int(sys.argv[4])) if len(sys.argv) > 4 else (960, 540)
ra, bpp = load(a); rb, _ = load(b)
sys.exit(0 if all(ra[y][:W * bpp] == rb[y][:W * bpp] for y in range(H)) else 1)
