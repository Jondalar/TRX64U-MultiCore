#!/usr/bin/env python3
"""T143: the pixel oracle of the sprite path. The expected picture of a sprite scene is computed from the tile art alone (indices 0..15), the palette the
bench's program wrote and the Neo Geo colour formula - not from anything the core or the DDR2 holds - and compared with the frame the simulated machine drew.

  tools/neogeo_sprites.py gen   --out DIR [--tile-base N] [--ntiles N] [--seed S]   DIR/mslug/201-c1.c1 + 201-c2.c2 (the tile art in the C ROM file format of MAME),
                                                                                    DIR/tiles.npy (the pixels), DIR/meta.txt. tools/neogeo_layout.py overlay --roms DIR
                                                                                    puts these files through MiSTer's loader (or the application's old layout)
  tools/neogeo_sprites.py check --dir DIR --pal PAL.txt FRAME.txt [--min PCT]        scene of cores/neogeo/sim/mkimg.py --dense --sheet: 19 sprites side by side,
                                                                                    14 tiles each (tile_base + 16 * s + t), palette 1 + s % 4. Prints the share of the
                                                                                    pixels that are as the oracle says, at the best offset. Exit 0 when >= --min (default 100)
  tools/neogeo_sprites.py probe --dir DIR --pal PAL.txt FRAME.txt                    which reading of the C ROM bytes explains the frame (the file format's variants)
  tools/neogeo_sprites.py selftest

The format of the C ROM files (one tile = 64 bytes in c1 and 64 in c2; c1 holds bit planes 0 and 1, c2 planes 2 and 3) is MAME's: the first 32 bytes of a file
are the right half of the tile (x 8..15), 16 rows of two bytes (plane 0, plane 1 in c1; plane 2, plane 3 in c2), the next 32 bytes the left half, and bit 0 of a byte
is the left pixel. That reading was found with `probe` against the core (the variants of halves, bit order and plane order, scored against the frame of the MiSTer
layout; the scene needs a LO ROM that maps the lines of a full size sprite to themselves, mkimg.py --sheet writes one) and is `VARIANT`; `selftest` and the bench's
control keep it from being a reading that only fits itself: the layout of the application's older writer gives another picture.
"""
import argparse
import itertools
import os
import sys

import numpy as np

# (structure, right half first, msb is the left pixel, c1 planes swapped, c2 planes swapped)
VARIANT = ("half", True, False, False, False)


def decode_tile(v, c1, c2):
    """64 bytes of c1 and of c2 -> 16 x 16 pixel indices"""
    struct, right_first, msb, sw1, sw2 = v
    out = np.zeros((16, 16), dtype=np.uint8)
    for half in range(2):
        x0 = (8 if half == 0 else 0) if right_first else (0 if half == 0 else 8)
        for row in range(16):
            o = (half * 32 + row * 2) if struct == "half" else (row * 4 + half * 2)
            pl = [c1[o + (1 if sw1 else 0)], c1[o + (0 if sw1 else 1)], c2[o + (1 if sw2 else 0)], c2[o + (0 if sw2 else 1)]]
            for i in range(8):
                bit = 7 - i if msb else i
                out[row, x0 + i] = sum(((pl[k] >> bit) & 1) << k for k in range(4))
    return out


def encode_tile(v, px):
    struct, right_first, msb, sw1, sw2 = v
    c1 = bytearray(64)
    c2 = bytearray(64)
    for half in range(2):
        x0 = (8 if half == 0 else 0) if right_first else (0 if half == 0 else 8)
        for row in range(16):
            o = (half * 32 + row * 2) if struct == "half" else (row * 4 + half * 2)
            pl = [0, 0, 0, 0]
            for i in range(8):
                bit = 7 - i if msb else i
                p = int(px[row, x0 + i])
                for k in range(4):
                    pl[k] |= ((p >> k) & 1) << bit
            c1[o + (1 if sw1 else 0)], c1[o + (0 if sw1 else 1)] = pl[0], pl[1]
            c2[o + (1 if sw2 else 0)], c2[o + (0 if sw2 else 1)] = pl[2], pl[3]
    return bytes(c1), bytes(c2)


def rgb565(p):
    """neogeo.sv's DAC as tb_neo.v's rgb565_of has it: the dark bit, 6 bit per channel to 8, then RGB565"""
    def ch(v4, lsb, hi):
        v = (v4 << 2) | (lsb << 1) | hi
        v -= (p >> 15) & 1
        if v < 0:
            return 0
        return v
    r6 = ch((p >> 8) & 15, (p >> 14) & 1, (p >> 11) & 1)
    g6 = ch((p >> 4) & 15, (p >> 13) & 1, (p >> 7) & 1)
    b6 = ch(p & 15, (p >> 12) & 1, (p >> 3) & 1)
    r8 = ((r6 & 63) << 2) | ((r6 >> 3) & 3) if r6 < 64 else 0
    g8 = ((g6 & 63) << 2) | ((g6 >> 3) & 3) if g6 < 64 else 0
    b8 = ((b6 & 63) << 2) | ((b6 >> 3) & 3) if b6 < 64 else 0
    return ((r8 >> 3) << 11) | ((g8 >> 2) << 5) | (b8 >> 3)


def load_pal(path):
    pal = {}
    bd = 0
    for l in open(path):
        t = l.split()
        if not t:
            continue
        if t[0] == "backdrop":
            bd = int(t[1], 16)
        else:
            pal[(int(t[0]), int(t[1]))] = int(t[2], 16)
    return pal, bd


def load_frame(path):
    px = np.array([int(l, 16) for l in open(path) if l.strip()], dtype=np.uint32)
    if len(px) != 320 * 224:
        sys.exit("%s: %d pixels, not 320 x 224" % (path, len(px)))
    return px.reshape(224, 320)


def cmd_gen(a):
    rng = np.random.default_rng(a.seed)
    n = a.ntiles
    # art that tells a wrong byte from a right one: every pixel random, a seventh transparent
    t = rng.integers(1, 16, size=(n, 16, 16), dtype=np.uint8)
    t[rng.integers(0, 7, size=(n, 16, 16)) == 0] = 0
    size = (a.tile_base + n) * 64
    c1 = bytearray(size)
    c2 = bytearray(size)
    for i in range(n):
        e1, e2 = encode_tile(VARIANT, t[i])
        o = (a.tile_base + i) * 64
        c1[o:o + 64] = e1
        c2[o:o + 64] = e2
    d = os.path.join(a.out, "mslug")
    os.makedirs(d, exist_ok=True)
    open(os.path.join(d, "201-c1.c1"), "wb").write(bytes(c1))
    open(os.path.join(d, "201-c2.c2"), "wb").write(bytes(c2))
    np.save(os.path.join(a.out, "tiles.npy"), t)
    open(os.path.join(a.out, "meta.txt"), "w").write("tile_base %d\nntiles %d\n" % (a.tile_base, n))


def meta(d):
    m = dict(l.split() for l in open(os.path.join(d, "meta.txt")))
    return int(m["tile_base"]), int(m["ntiles"])


def expected(tiles, pal, bd, nspr=19, nrows=14):
    """the 304 x 224 picture of the scene: sprite s, row t shows tile 16 * s + t with palette 1 + s % 4 (index 0 = backdrop)"""
    img = np.zeros((nrows * 16, nspr * 16), dtype=np.int64)
    bd565 = rgb565(bd)
    for s in range(nspr):
        lut = np.array([bd565] + [rgb565(pal[(1 + s % 4, c)]) for c in range(1, 16)], dtype=np.int64)
        for t in range(nrows):
            img[t * 16:(t + 1) * 16, s * 16:(s + 1) * 16] = lut[tiles[16 * s + t]]
    return img


def best_offset(frame, exp):
    """(matching pixels, compared pixels, ox, oy) at the offset that matches most: the picture is the same wherever the machine puts the sprite window"""
    best = (-1, 1, 0, 0)
    eh, ew = exp.shape
    for oy in range(-16, 33):
        for ox in range(0, 320 - ew + 1):
            y0, y1 = max(0, oy), min(224, oy + eh)
            if y1 - y0 < 160:
                continue
            f = frame[y0:y1, ox:ox + ew]
            e = exp[y0 - oy:y1 - oy]
            m = int((f == e).sum())
            if m > best[0]:
                best = (m, f.size, ox, oy)
    return best


def cmd_check(a):
    tb, n = meta(a.dir)
    tiles = np.load(os.path.join(a.dir, "tiles.npy"))
    pal, bd = load_pal(a.pal)
    # tile number of sprite s row t is tile_base + 16 s + t: tiles.npy starts at tile_base
    exp = expected(tiles, pal, bd)
    m, k, ox, oy = best_offset(load_frame(a.frame), exp)
    pct = 100.0 * m / k
    print("sprites: %d of %d pixels as the oracle says (%.2f %%) at offset x %d y %d" % (m, k, pct, ox, oy))
    return 0 if pct >= a.min else 1


def cmd_probe(a):
    tb, n = meta(a.dir)
    pal, bd = load_pal(a.pal)
    c1 = open(os.path.join(a.dir, "mslug", "201-c1.c1"), "rb").read()
    c2 = open(os.path.join(a.dir, "mslug", "201-c2.c2"), "rb").read()
    fr = load_frame(a.frame)
    res = []
    for v in itertools.product(("half", "row"), (True, False), (True, False), (False, True), (False, True)):
        tiles = np.array([decode_tile(v, c1[(tb + i) * 64:(tb + i + 1) * 64], c2[(tb + i) * 64:(tb + i + 1) * 64]) for i in range(n)])
        m, k, ox, oy = best_offset(fr, expected(tiles, pal, bd))
        res.append((m / k, v, ox, oy))
    res.sort(reverse=True)
    for r in res[:4]:
        print("%.4f %s offset %d,%d" % r)


def cmd_selftest(_):
    rng = np.random.default_rng(3)
    ok = True
    for v in itertools.product(("half", "row"), (True, False), (True, False), (False, True), (False, True)):
        t = rng.integers(0, 16, size=(16, 16), dtype=np.uint8)
        c1, c2 = encode_tile(v, t)
        ok &= bool((decode_tile(v, c1, c2) == t).all())
    print("  %s encode/decode are inverses in all 32 readings" % ("ok  " if ok else "FAIL"))
    # the colour formula against the testbench's own function (the fixed word of mkimg: 0x0F24 -> 0xF108)
    c = rgb565(0x0F24) == 0xF108
    print("  %s the backdrop word 0F24 is 565 F108 (the bench's own check)" % ("ok  " if c else "FAIL"))
    ok &= c
    print("selftest", "PASS" if ok else "FAIL")
    return 0 if ok else 1


def main():
    ap = argparse.ArgumentParser()
    sub = ap.add_subparsers(dest="cmd", required=True)
    g = sub.add_parser("gen")
    g.add_argument("--out", required=True)
    g.add_argument("--tile-base", type=lambda x: int(x, 0), default=0x100)
    g.add_argument("--ntiles", type=int, default=19 * 16)
    g.add_argument("--seed", type=int, default=143)
    for nm in ("check", "probe"):
        p = sub.add_parser(nm)
        p.add_argument("--dir", required=True)
        p.add_argument("--pal", required=True)
        p.add_argument("--min", type=float, default=100.0)
        p.add_argument("frame")
    sub.add_parser("selftest")
    a = ap.parse_args()
    if a.cmd == "selftest":
        sys.exit(cmd_selftest(a))
    r = {"gen": cmd_gen, "check": cmd_check, "probe": cmd_probe}[a.cmd](a)
    sys.exit(r or 0)


if __name__ == "__main__":
    main()
