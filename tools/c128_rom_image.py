#!/usr/bin/env python3
"""T113: the 72 KiB system ROM image (spec 5.5 layout) from ROM files that live OUTSIDE this repository (VICE's C128 directory, or the files of a real machine),
for the local real-ROM bench (cores/c128/sim/run_realrom.sh) and for the application's ROM loader tests by hand. Never run in CI, the output is never committed.
  tools/c128_rom_image.py <rom dir> <out file> [--mem]       --mem: write the bench's mem/c128_rom.mem form (one 16 bit word per line) instead of the binary
File names (VICE): basic64-901226-01.bin, kernal64-901227-03.bin (C64 BASIC and KERNAL, 8 KiB each), kernal-318020-05.bin (C128 editor / Z80 BIOS / KERNAL,
16 KiB), basiclo-318018-04.bin + basichi-318019-04.bin (C128 BASIC, 16 KiB each), chargen-315079-01.bin (8 KiB: the C64's and the C128's character set)."""
import os
import sys

PARTS = [("basic64-901226-01.bin", 8192), ("kernal64-901227-03.bin", 8192), ("kernal-318020-05.bin", 16384),
         ("basiclo-318018-04.bin", 16384), ("basichi-318019-04.bin", 16384), ("chargen-315079-01.bin", 8192)]


def main():
    if len(sys.argv) < 3:
        sys.exit(__doc__)
    d, out = sys.argv[1], sys.argv[2]
    img = b""
    for name, size in PARTS:
        data = open(os.path.join(d, name), "rb").read()
        if len(data) != size:
            sys.exit("%s: %d bytes, expected %d" % (name, len(data), size))
        img += data
    assert len(img) == 0x12000
    if "--mem" in sys.argv:
        os.makedirs(os.path.dirname(out) or ".", exist_ok=True)
        with open(out, "w") as f:
            for i in range(0, len(img), 2):
                f.write("%02x%02x\n" % (img[i + 1], img[i]))
    else:
        open(out, "wb").write(img)
    print("wrote %s (%d bytes)" % (out, len(img)))


if __name__ == "__main__":
    main()
