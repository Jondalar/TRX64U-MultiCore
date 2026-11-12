#!/usr/bin/env python3
"""Turn the stock C64 ROM images in roms/ into the $readmemh file the
simulations of host preload (sim only: the bitstream holds no
ROM, the Ultimate app writes them from /roms, see c64_mem.v).

  rom2mem.py <dir>   -> <dir>/c64_rom.mem

c64_rom.mem: 16-bit words for the ROM block of c64_mem.v, byte offsets from
0x10000 as icepi's c64_top maps them (char 0x10000, kernal 0x12000,
basic 0x14000; unused bytes 0xFF). Low byte = even address.
"""
import os
import sys

here = os.path.dirname(os.path.abspath(__file__))
roms = os.path.join(here, "..", "roms")
if len(sys.argv) != 2:
    sys.exit(__doc__)
out = sys.argv[1]


def load(name, size):
    data = open(os.path.join(roms, name), "rb").read()
    if len(data) != size:
        sys.exit(f"{name}: {len(data)} bytes, expected {size}")
    return data


img = bytearray(b"\xff" * 0x8000)
for off, name, size in ((0x0000, "characters.901225-01.bin", 0x1000),
                        (0x2000, "kernal.901227-03.bin", 0x2000),
                        (0x4000, "basic.901226-01.bin", 0x2000)):
    img[off:off + size] = load(name, size)

os.makedirs(out, exist_ok=True)
path = os.path.join(out, "c64_rom.mem")
if os.path.islink(path):        # an old run directory linked it from the design
    os.remove(path)
with open(path, "w") as f:
    for i in range(0, len(img), 2):
        f.write(f"{img[i + 1]:02x}{img[i]:02x}\n")
