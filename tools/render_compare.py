#!/usr/bin/env python3
"""T192.2 P5 (S11, S12): the parity compare of two render bench outputs (tests/hostapp/render/run.sh <dir>), screen by screen.

A is the reference (the build before a change), B the build under test. Every file of A must be in B and equal: the PNG pictures pixel by pixel (the
count of differing pixels and the first one are reported), every other file (the dumps: texts, navigation, states) byte by byte. A file only B has is reported
too. The only skipped files are the ones named in --skip, each with the reason after a colon; a skip is printed. No region of a picture is masked: a mask that
hides a footer or a label would hide the thing the compare is for.

  tools/render_compare.py A B [--skip name:reason ...]      exit 1 when anything differs
  tools/render_compare.py --selftest                         plants a one-pixel change, a changed dump, a missing and an extra file: each must be reported;
                                                             two identical copies must pass (S12: the compare is deterministic)
"""
import os
import shutil
import struct
import sys
import tempfile
import zlib


def png_pixels(path):
    """(width, height, bytes per pixel, rows of raw pixel bytes) of an 8-bit RGB / RGBA PNG, not interlaced; None when it is no such PNG."""
    b = open(path, "rb").read()
    if b[:8] != b"\x89PNG\r\n\x1a\n":
        return None
    i, idat, hdr = 8, b"", None
    while i + 8 <= len(b):
        n, t = struct.unpack(">I4s", b[i:i + 8])
        d = b[i + 8:i + 8 + n]
        if t == b"IHDR":
            hdr = struct.unpack(">IIBBBBB", d)
        elif t == b"IDAT":
            idat += d
        i += 12 + n
    if not hdr:
        return None
    w, h, depth, ctype, _, _, inter = hdr
    if depth != 8 or ctype not in (2, 6) or inter:
        return None
    bpp = 3 if ctype == 2 else 4
    raw = zlib.decompress(idat)
    stride = w * bpp
    rows, prev = [], bytearray(stride)
    p = 0
    for _ in range(h):
        f = raw[p]
        line = bytearray(raw[p + 1:p + 1 + stride])
        p += 1 + stride
        for x in range(stride):
            a = line[x - bpp] if x >= bpp else 0
            up = prev[x]
            c = prev[x - bpp] if x >= bpp else 0
            if f == 1:
                line[x] = (line[x] + a) & 255
            elif f == 2:
                line[x] = (line[x] + up) & 255
            elif f == 3:
                line[x] = (line[x] + ((a + up) >> 1)) & 255
            elif f == 4:
                pa, pb, pc = abs(up - c), abs(a - c), abs(a + up - 2 * c)
                pr = a if pa <= pb and pa <= pc else (up if pb <= pc else c)
                line[x] = (line[x] + pr) & 255
        rows.append(line)
        prev = line
    return w, h, bpp, rows


def compare(a, b, skip):
    """The list of breaches (strings) between two output directories."""
    bad = []
    names_a = sorted(os.listdir(a))
    names_b = sorted(os.listdir(b))
    for n in names_a:
        if n in skip:
            continue
        pa, pb = os.path.join(a, n), os.path.join(b, n)
        if not os.path.exists(pb):
            bad.append("missing in B: " + n)
            continue
        if os.path.isdir(pa):
            continue
        da, db = open(pa, "rb").read(), open(pb, "rb").read()
        if da == db:
            continue
        if n.endswith(".png"):
            ia, ib = png_pixels(pa), png_pixels(pb)
            if ia and ib and ia[0] == ib[0] and ia[1] == ib[1]:
                w, h, bpp, ra = ia
                rb = ib[3]
                cnt, first = 0, None
                for y in range(h):
                    if ra[y] == rb[y]:
                        continue
                    for x in range(w):
                        if ra[y][x * bpp:x * bpp + bpp] != rb[y][x * bpp:x * bpp + bpp]:
                            cnt += 1
                            first = first or (x, y)
                if cnt:
                    bad.append("picture differs: %s: %d pixels, the first at %d,%d" % (n, cnt, first[0], first[1]))
                    continue
                bad.append("picture bytes differ, pixels equal (the encoder): " + n)
                continue
            bad.append("picture differs (not comparable pixel by pixel): " + n)
        else:
            bad.append("dump differs: " + n)
    for n in names_b:
        if n not in names_a and n not in skip:
            bad.append("only in B: " + n)
    return bad


def write_png(path, w, h, px):
    raw = b"".join(b"\x00" + b"".join(bytes(px(x, y)) for x in range(w)) for y in range(h))

    def chunk(t, d):
        c = struct.pack(">I", len(d)) + t + d
        return c + struct.pack(">I", zlib.crc32(t + d) & 0xFFFFFFFF)
    open(path, "wb").write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0)) + chunk(b"IDAT", zlib.compress(raw)) + chunk(b"IEND", b""))


def selftest():
    rc = 0
    t = tempfile.mkdtemp()
    try:
        a, b = os.path.join(t, "a"), os.path.join(t, "b")
        os.makedirs(a)
        write_png(os.path.join(a, "home.png"), 40, 30, lambda x, y: (x * 5 & 255, y * 7 & 255, 90))
        open(os.path.join(a, "home.txt"), "w").write("Boot menu\nUP/DOWN select\n")
        shutil.copytree(a, b)
        bad = compare(a, b, set())
        print("selftest: two identical outputs: %s" % ("pass" if not bad else "RED " + str(bad)))
        rc |= 1 if bad else 0
        # one pixel
        px = lambda x, y: (x * 5 & 255, y * 7 & 255, 90 if (x, y) != (17, 11) else 91)
        write_png(os.path.join(b, "home.png"), 40, 30, px)
        bad = compare(a, b, set())
        ok = any("1 pixels, the first at 17,11" in x for x in bad)
        print("selftest: one pixel changed: %s" % ("reported" if ok else "NOT reported " + str(bad)))
        rc |= 0 if ok else 1
        shutil.copy(os.path.join(a, "home.png"), os.path.join(b, "home.png"))
        # a dump (one footer word)
        open(os.path.join(b, "home.txt"), "w").write("Boot menu\nUP/DOWN selekt\n")
        bad = compare(a, b, set())
        ok = any(x.startswith("dump differs") for x in bad)
        print("selftest: one word of a dump changed: %s" % ("reported" if ok else "NOT reported"))
        rc |= 0 if ok else 1
        shutil.copy(os.path.join(a, "home.txt"), os.path.join(b, "home.txt"))
        # a missing and an extra file
        os.rename(os.path.join(b, "home.txt"), os.path.join(b, "other.txt"))
        bad = compare(a, b, set())
        ok = any(x.startswith("missing in B") for x in bad) and any(x.startswith("only in B") for x in bad)
        print("selftest: a renamed file (missing and extra): %s" % ("reported" if ok else "NOT reported"))
        rc |= 0 if ok else 1
        # a skip is a named file, not a region
        bad = compare(a, b, {"home.txt", "other.txt"})
        print("selftest: a named skip: %s" % ("pass" if not bad else "RED " + str(bad)))
        rc |= 1 if bad else 0
    finally:
        shutil.rmtree(t)
    return rc


def main():
    args = sys.argv[1:]
    if args[:1] == ["--selftest"]:
        return selftest()
    skip = {}
    dirs = []
    i = 0
    while i < len(args):
        if args[i] == "--skip" and i + 1 < len(args):
            n, _, why = args[i + 1].partition(":")
            skip[n] = why
            i += 2
        else:
            dirs.append(args[i])
            i += 1
    if len(dirs) != 2:
        print(__doc__)
        return 2
    for n, why in sorted(skip.items()):
        print("skipped %s (%s)" % (n, why or "no reason given"))
    bad = compare(dirs[0], dirs[1], set(skip))
    for x in bad:
        print(x)
    if bad:
        return 1
    print("render_compare: %d files equal" % len([n for n in os.listdir(dirs[0]) if n not in skip]))
    return 0


sys.exit(main())
