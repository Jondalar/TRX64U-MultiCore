#!/usr/bin/env python3
# T257 fault 2: the picture of the ruler program vdc_il_ruler.asm (the same lines, built the same way), for tb_c128_vdcil.sv.
#   python3 vdcil_model.py gen <dir>      writes <dir>/lines_<m>.hex, m = 0..3 (the mode letters A..D): 480 lines, one line of 160 hex
#                                         digits each (byte 0 first), picture line p as the VDC RAM holds it (p even at $4000 + 80 (p / 2),
#                                         p odd at $8B00 + 80 (p / 2), Busted Boulders' layout)
#   python3 vdcil_model.py png <ppm> <png> converts a dump of the bench to PNG (needs PIL; for looking only)
# Written for this project (TREX).
import os
import sys

GLYPHS = [[0x18, 0x3c, 0x66, 0x66, 0x7e, 0x66, 0x66, 0x00], [0x7c, 0x66, 0x66, 0x7c, 0x66, 0x66, 0x7c, 0x00],
          [0x3c, 0x66, 0x60, 0x60, 0x60, 0x66, 0x3c, 0x00], [0x78, 0x6c, 0x66, 0x66, 0x66, 0x6c, 0x78, 0x00]]


def line_bytes(p, mode):
    """the 80 bytes of picture line p (0..479) with the letter of mode (0..3): vdc_il_ruler.asm `build` and `setmode`"""
    b = [0] * 80
    if p in (0, 479):
        b = [0xff] * 80
    else:
        b[0] |= 0x80
        b[79] |= 0x01
        if p % 48 < 2:
            for i in range(10):
                b[i] = 0xff
                b[70 + i] = 0xff
        if 96 <= p < 224:
            for i in range(12):
                b[(2 if p % 2 == 0 else 66) + i] = 0xff
        x0 = p & 63
        for j in range(10):
            for x in (x0 + 64 * j, x0 + 64 * j + 1):
                if x < 640:
                    b[x >> 3] |= 0x80 >> (x & 7)
        for k in range(9):
            b[38 - k] = 0x7e if (p >> k) & 1 else 0x00
    if 16 <= p < 48:
        g = GLYPHS[mode][(p - 16) >> 2]
        for k in range(4):
            v = 0
            if g & (0x80 >> (2 * k)):
                v |= 0xf0
            if g & (0x40 >> (2 * k)):
                v |= 0x0f
            b[72 + k] = v
    return b


def gen(d):
    os.makedirs(d, exist_ok=True)
    for m in range(4):
        with open(os.path.join(d, 'lines_%d.hex' % m), 'w') as f:
            for p in range(480):
                f.write(''.join('%02x' % v for v in line_bytes(p, m)) + '\n')


def png(src, dst):
    from PIL import Image
    with open(src, 'rb') as f:
        data = f.read()
    hdr = data.split(b'\n', 3)
    w, h = (int(v) for v in hdr[1].split())
    Image.frombytes('RGB', (w, h), hdr[3]).save(dst)


if __name__ == '__main__':
    if len(sys.argv) == 3 and sys.argv[1] == 'gen':
        gen(sys.argv[2])
    elif len(sys.argv) == 4 and sys.argv[1] == 'png':
        png(sys.argv[2], sys.argv[3])
    else:
        sys.exit('usage: vdcil_model.py gen <dir> | png <ppm> <png>')
