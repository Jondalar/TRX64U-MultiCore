#!/usr/bin/env python3
"""T116: the Next core's bench programs (cores/zxnext/sim/prog/*.s, ours, assembled by tools/zxnext_asm.py) as SD card images the boot ROM starts them
from: TBBLUE.FW = header sector + the program, loaded at 0x6000 (tools/zxnext_img.py make_fw). Nothing else is on the card, except what a bench asks
for: `--fill LBA` writes a block of known bytes at that LBA (byte i of block n is (7 n + 13 i) mod 251), `--flip OFFSET` flips bit 0 of that byte of the
program (the negative control of the boot bench).

  tools/zxnext_progs.py build <name> <out.img> [--fill LBA ...] [--flip OFFSET] [--size-mb N] [--define NAME=VALUE ...]
  tools/zxnext_progs.py check          every program assembles, fits the 8 KB of the display RAM it is started in, and makes an image
  tools/zxnext_progs.py pattern LBA    the bytes --fill puts at LBA (hex, 512 bytes), for the benches' own expectations
"""
import glob
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)
import zxnext_asm  # noqa: E402
import zxnext_img  # noqa: E402

PROG = os.path.join(ROOT, "cores/zxnext/sim/prog")
LOAD = 0x6000
MAXLEN = 0x2000                          # 0x6000-0x7FFF: the second half of bank 5, the part of the display RAM the boot ROM leaves alone


def pattern(lba):
    return bytes((7 * lba + 13 * i) % 251 for i in range(512))


def assemble(name, defs=None):
    text = open(os.path.join(PROG, name + ".s")).read()
    data, org, _ = zxnext_asm.assemble(text, defs)
    if org != LOAD:
        raise SystemExit("zxnext_progs: %s starts at %04X, the boot ROM runs it at %04X" % (name, org, LOAD))
    if len(data) > MAXLEN:
        raise SystemExit("zxnext_progs: %s is %d bytes, at most %d fit" % (name, len(data), MAXLEN))
    return data


def build(name, out, fills=(), flip=None, size_mb=8, defs=None):
    data = bytearray(assemble(name, defs))
    if flip is not None:
        data[flip] ^= 1
    img = bytearray(zxnext_img.make_fat16({"TBBLUE.FW": zxnext_img.make_fw(bytes(data))}, size_mb * 2048))
    for lba in fills:
        img[lba * 512:(lba + 1) * 512] = pattern(lba)
    open(out, "wb").write(img)
    return len(data)


def main(argv):
    if len(argv) < 2:
        print(__doc__)
        return 1
    if argv[1] == "check":
        names = sorted(os.path.splitext(os.path.basename(p))[0] for p in glob.glob(os.path.join(PROG, "*.s")))
        if not names:
            print("zxnext_progs: no programs")
            return 1
        for n in names:
            d = assemble(n, {"SPEED": 0, "MODE": 0, "NPAGES": 6, "CLIPX2": 255, "CLIPY2": 191, "TIMING": 3, "POLLS": 5000})
            fw = zxnext_img.make_fw(d)
            img = zxnext_img.make_fat16({"TBBLUE.FW": fw})
            assert zxnext_img.read_fat16(img)["TBBLUE.FW"] == fw
            print("zxnext_progs: %-10s %5d bytes at %04X, TBBLUE.FW %d bytes" % (n, len(d), LOAD, len(fw)))
        return 0
    if argv[1] == "pattern":
        print(pattern(int(argv[2])).hex())
        return 0
    if argv[1] == "build":
        name, out = argv[2], argv[3]
        fills, flip, size, defs = [], None, 8, {}
        i = 4
        while i < len(argv):
            if argv[i] == "--fill":
                fills.append(int(argv[i + 1]))
            elif argv[i] == "--flip":
                flip = int(argv[i + 1])
            elif argv[i] == "--size-mb":
                size = int(argv[i + 1])
            elif argv[i] == "--define":
                k, _, v = argv[i + 1].partition("=")
                defs[k] = int(v, 0)
            else:
                print("unknown option " + argv[i])
                return 1
            i += 2
        n = build(name, out, fills, flip, size, defs)
        print("zxnext_progs: %s -> %s (%d bytes of program)" % (name, out, n))
        return 0
    print(__doc__)
    return 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))
