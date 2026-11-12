#!/usr/bin/env python3
"""T182: the 20 KiB system ROM image of the C64SCPU core (step 1 layout: C64 BASIC 8 KiB, KERNAL 8 KiB, character ROM 4 KiB, see tools/gen_c64scpu_test.py) from ROM
files that live OUTSIDE this repository, for the local real-ROM bench (cores/c64scpu/sim/run_real.sh). Never run in CI, the output is never committed.
  tools/c64scpu_rom_image.py <rom dir> <out file> [--mem]       --mem: write the bench's mem/c64scpu_rom.mem form (one 16 bit word per line) instead of the binary
File names (any one match per part, case-insensitive): basic*901226*, kernal*901227*, char*901225* or chargen*901225*."""
import fnmatch
import glob
import os
import sys

PARTS = [("basic", "basic*901226*", 8192), ("kernal", "kernal*901227*", 8192), ("chargen", "char*901225*", 4096)]


def find(d, pat):
    hits = [f for f in glob.glob(os.path.join(d, "*")) if fnmatch.fnmatch(os.path.basename(f).lower(), pat)]
    if len(hits) != 1:
        sys.exit("%s: %d files match %s" % (d, len(hits), pat))
    return hits[0]


def main():
    if len(sys.argv) < 3:
        sys.exit(__doc__)
    d, out = sys.argv[1], sys.argv[2]
    img = b""
    for name, pat, size in PARTS:
        data = open(find(d, pat), "rb").read()
        if len(data) != size:
            sys.exit("%s: %d bytes, expected %d" % (name, len(data), size))
        img += data
    assert len(img) == 0x5000
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
