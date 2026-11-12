#!/usr/bin/env python3
"""T108: the boot disk of the disk bench, made here: bootdisk.asm (assembled by 64tass) as sectors 1-3 of a 720-sector,
128-byte ATR, sector n >= 4 filled with n & 0xFF.   gen_disk.py <bootdisk.bin> <out.atr>"""
import sys

code = open(sys.argv[1], "rb").read()
assert len(code) == 384, len(code)
sectors = 720
body = bytearray(code)
for n in range(4, sectors + 1):
    body += bytes([n & 0xFF]) * 128
paras = len(body) // 16
hdr = bytes([0x96, 0x02, paras & 0xFF, (paras >> 8) & 0xFF, 128, 0, (paras >> 16) & 0xFF]) + bytes(9)
open(sys.argv[2], "wb").write(hdr + body)
print("%s: %d sectors, %d bytes" % (sys.argv[2], sectors, 16 + len(body)))
