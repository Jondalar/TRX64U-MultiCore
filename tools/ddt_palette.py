#!/usr/bin/env python3
"""T168: the Mandelbrot palette of DDT's mandelbrUlt as RGB565 for the frame buffer.

His palette_yuv_256.hex holds {Y[7:0], U[9:0], V[9:0]} words for a PAL modulator, and every line names the RGB colour it was made from
(`// 01 RGB 001155 Y ...`). This tool reads the words (the data) and the colours, checks that the words ARE those colours (Y = 16 + 0.2526 *
(0.299 R + 0.587 G + 0.114 B), U = 0.436 B - ..., V = ..., every word within 2 counts of the conversion (the words are truncated, not rounded) of its colour) and writes the colours as RGB565, one
4 digit hex word a line, for $readmemh.

  tools/ddt_palette.py            write cores/ddt_mandel/mem/palette_rgb565_256.hex
  tools/ddt_palette.py --check    compare the file with what the tool makes (lint) and run the YUV check
"""
import os, re, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = os.path.join(ROOT, "cores", "ddt_mandel", "rtl", "palette_yuv_256.hex")
OUT = os.path.join(ROOT, "cores", "ddt_mandel", "mem", "palette_rgb565_256.hex")
PAT = re.compile(r"([0-9A-Fa-f]+)\s*//\s*([0-9A-Fa-f]+)\s*RGB\s*([0-9A-Fa-f]{6})")


def read():
    rows = []
    for line in open(SRC, encoding="ascii"):
        m = PAT.match(line)
        if not m:
            raise SystemExit("palette line not understood: %r" % line)
        w = int(m.group(1), 16)
        idx = int(m.group(2), 16)
        rgb = int(m.group(3), 16)
        y, u, v = w >> 20, (w >> 10) & 0x3FF, w & 0x3FF
        u -= 1024 if u >= 512 else 0
        v -= 1024 if v >= 512 else 0
        rows.append((idx, (rgb >> 16, (rgb >> 8) & 255, rgb & 255), (y, u, v)))
    if [r[0] for r in rows] != list(range(256)):
        raise SystemExit("palette: entries are not 00..FF in order")
    return rows


def yuv(rgb):
    r, g, b = rgb
    yl = 0.299 * r + 0.587 * g + 0.114 * b
    return 16 + yl * 64.0 / 255.0, 0.492 * (b - yl), 0.877 * (r - yl)


def rgb565(rgb):
    r, g, b = rgb
    return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)


def main():
    rows = read()
    worst = 0.0
    for idx, rgb, word in rows:
        for a, b in zip(yuv(rgb), word):
            worst = max(worst, abs(a - b))
    if worst > 2.0:
        raise SystemExit("palette: the YUV words are not the comment colours (worst difference %.2f)" % worst)
    text = "".join("%04X\n" % rgb565(rgb) for _, rgb, _ in rows)
    if "--check" in sys.argv:
        if not os.path.exists(OUT) or open(OUT).read() != text:
            raise SystemExit("palette: %s is not what this tool makes" % os.path.relpath(OUT, ROOT))
        print("palette ok: 256 colours, YUV words within %.2f of their colours" % worst)
        return
    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    open(OUT, "w").write(text)
    print("written %s (YUV words within %.2f of their colours)" % (os.path.relpath(OUT, ROOT), worst))


main()
