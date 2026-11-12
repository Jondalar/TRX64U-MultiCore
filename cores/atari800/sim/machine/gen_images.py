#!/usr/bin/env python3
"""T88: the images of the whole-machine benches, all made here (no Atari code anywhere): the test OS from testos2.asm (assembled by
64tass), a "BASIC" of 8 KB, and cartridge images of 8 KB, 16 KB and 128 KB (AtariMax 1 Mbit), each a deterministic pattern.
Written as hex files, one byte per line, into the directory given.   gen_images.py <out dir> <testos2.rom>"""
import sys, os


def pat(seed, n):
    return bytes(((i * seed + (i >> 8) * 13 + (i >> 12) * 7 + seed) & 0xFF) for i in range(n))


def write_hex(path, data):
    with open(path, "w") as f:
        f.write("".join("%02X\n" % b for b in data))


out, rom = sys.argv[1], sys.argv[2]
os.makedirs(out, exist_ok=True)
os_img = open(rom, "rb").read()
assert len(os_img) == 16384
write_hex(os.path.join(out, "testos2.hex"), os_img)
write_hex(os.path.join(out, "basic.hex"), pat(11, 8192))
write_hex(os.path.join(out, "cart8.hex"), pat(7, 8192))
write_hex(os.path.join(out, "cart16.hex"), pat(5, 16384))
write_hex(os.path.join(out, "cartmax.hex"), pat(3, 131072))
