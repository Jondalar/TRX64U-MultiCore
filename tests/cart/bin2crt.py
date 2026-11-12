#!/usr/bin/env python3
"""A raw cartridge dump (8 KiB banks, ROML at $8000) as a CRT of the given type.

    bin2crt.py IN.bin OUT.crt CRT_TYPE [--name NAME] [--exrom 0 --game 1] [--skip-ff]

Bank n of the dump is CHIP packet bank n at $8000 (VICE's C64MegaCart .bin
attach, c64megacart.c:399-433). --skip-ff leaves banks that are all $FF out of
the file (erased flash; the app fills them with $FF).
"""
import argparse
import struct

ap = argparse.ArgumentParser()
ap.add_argument("inp")
ap.add_argument("out")
ap.add_argument("crt_type", type=int)
ap.add_argument("--name", default="")
ap.add_argument("--exrom", type=int, default=0)
ap.add_argument("--game", type=int, default=1)
ap.add_argument("--skip-ff", action="store_true")
a = ap.parse_args()
data = open(a.inp, "rb").read()
assert len(data) % 0x2000 == 0, "not whole 8 KiB banks"
h = bytearray(b"C64 CARTRIDGE   ")
h += struct.pack(">IHHBBB", 0x40, 0x0100, a.crt_type, a.exrom, a.game, 0) + bytes(5)
h += (a.name or a.inp.rsplit("/", 1)[-1]).upper().encode()[:32].ljust(32, b"\0")
n = 0
for b in range(len(data) // 0x2000):
    chunk = data[b * 0x2000:(b + 1) * 0x2000]
    if a.skip_ff and chunk == b"\xff" * 0x2000:
        continue
    h += b"CHIP" + struct.pack(">IHHHH", 0x2010, 2, b, 0x8000, 0x2000) + chunk
    n += 1
open(a.out, "wb").write(h)
print("%s: CRT type %d, %d banks, %d bytes" % (a.out, a.crt_type, n, len(h)))
