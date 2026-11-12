#!/usr/bin/env python3
"""c128-vicglitch: pictures of the VIC's pixel stream (tb_c128 +PIXDUMP: "frame y x colour" per pixel) and a pixel diff of two dumps.
  render.py <pixdump.txt> <out prefix>                 one PNG per frame: <prefix>_f<frame>.png
  render.py <a.txt> <b.txt> --diff                      per frame number present in both: pixels that differ, first differing (y, x, colours a/b)"""
import struct
import sys
import zlib

PAL = [(0, 0, 0), (255, 255, 255), (136, 57, 50), (103, 182, 189), (139, 63, 150), (85, 160, 73), (64, 49, 141), (191, 206, 114),
       (139, 84, 41), (87, 66, 0), (184, 105, 98), (80, 80, 80), (120, 120, 120), (148, 224, 137), (120, 105, 196), (159, 159, 159)]


def load(path):
    frames = {}
    for ln in open(path):
        p = ln.split()
        if len(p) != 4:
            continue
        f, y, x, c = map(int, p)
        frames.setdefault(f, {})[(y, x)] = c
    return frames


def png(path, w, h, rows):
    raw = b"".join(b"\x00" + bytes(r) for r in rows)

    def chunk(t, d):
        c = struct.pack(">I", len(d)) + t + d
        return c + struct.pack(">I", zlib.crc32(t + d) & 0xFFFFFFFF)
    open(path, "wb").write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0)) + chunk(b"IDAT", zlib.compress(raw, 6)) + chunk(b"IEND", b""))


def main():
    if "--diff" in sys.argv:
        a, b = load(sys.argv[1]), load(sys.argv[2])
        for f in sorted(set(a) & set(b)):
            d = sorted((k for k in a[f] if k in b[f] and a[f][k] != b[f][k]))
            print("frame %d: %d pixels of %d differ" % (f, len(d), len(a[f])), ("first (y %d, x %d) %d vs %d" % (d[0][0], d[0][1], a[f][d[0]], b[f][d[0]])) if d else "")
        return
    fr = load(sys.argv[1])
    for f, px in sorted(fr.items()):
        ys = sorted({k[0] for k in px})
        xs = sorted({k[1] for k in px})
        y0, y1, x0, x1 = ys[0], ys[-1], xs[0], xs[-1]
        rows = []
        for y in range(y0, y1 + 1):
            r = []
            for x in range(x0, x1 + 1):
                r += PAL[px.get((y, x), 0) & 15]
            rows.append(r)
        out = "%s_f%d.png" % (sys.argv[2], f)
        png(out, (x1 - x0 + 1), (y1 - y0 + 1), rows)
        print(out, x1 - x0 + 1, "x", y1 - y0 + 1)


if __name__ == "__main__":
    main()
