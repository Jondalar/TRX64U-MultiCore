#!/usr/bin/env python3
"""T218: the bench firmware of the DSP-n coprocessor (sim `dsp`), written for this project: 2048 program words (3 bytes, low byte first) and 1024 data words
(2 bytes, low byte first) = the 8192 byte file the application takes. Word 0: LDI DR, $1234 (type 11, ID in bits 21:6, DST 6 = DR); word 1: JP 1 (type 10, BRCH $100 =
always, NA 1). Not a chip dump. Usage: snes_dsp_fw.py <out.bin>"""
import sys
prog = [0] * 2048
prog[0] = (3 << 22) | (0x1234 << 6) | 6
prog[1] = (2 << 22) | (0x100 << 13) | (1 << 2)
out = bytearray()
for w in prog:
    out += bytes([w & 255, (w >> 8) & 255, (w >> 16) & 255])
for i in range(1024):
    out += bytes([i & 255, (i >> 8) & 255])
assert len(out) == 8192
open(sys.argv[1], "wb").write(out)
