#!/usr/bin/env python3
"""bin -> the bench's hex image (65,536 lines of two hex digits).
   mkimage.py IN.bin OUT.hex [--load ADDR] [--reset ADDR] [--irq ADDR] [--nmi ADDR]
IN.bin is placed at ADDR (default 0) in a zeroed 64 KB; the vectors are patched when given."""
import sys

a = sys.argv[1:]
src, dst = a[0], a[1]
opt = {"--load": 0, "--reset": None, "--irq": None, "--nmi": None}
i = 2
while i < len(a):
    opt[a[i]] = int(a[i + 1], 0)
    i += 2
mem = bytearray(65536)
d = open(src, "rb").read()
mem[opt["--load"]:opt["--load"] + len(d)] = d
for k, v in (("--nmi", 0xFFFA), ("--reset", 0xFFFC), ("--irq", 0xFFFE)):
    if opt[k] is not None:
        mem[v] = opt[k] & 255
        mem[v + 1] = opt[k] >> 8
open(dst, "w").write("".join("%02X\n" % b for b in mem))
