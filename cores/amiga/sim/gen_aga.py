#!/usr/bin/env python3
"""A1200 stripes: a hand-assembled 68000 ROM + chip RAM image for the AGA frame bench (tb_aga.sv).
  gen_aga.py outdir FMODE [HIRES]
rom.hex  : sets a 256 entry palette (colour i: R = i & 15, G = i >> 4, so (R,G) names the index), a copper list with
           8 bitplanes, DIW 320 x 256, FMODE, and starts the display
chip.hex : 8 planes of 40 byte lines at $10000 + plane * $4000; byte column c of every line has the colour index
           (c * 37 + 11) & 255, so each of the 8 bytes of a 64-bit group differs"""
import struct, sys
out = sys.argv[1]; fmode = int(sys.argv[2], 0); hires = len(sys.argv) > 3 and sys.argv[3] == '1'
ddfs = int(sys.argv[4], 0) if len(sys.argv) > 4 else 0x38; ddfe = int(sys.argv[5], 0) if len(sys.argv) > 5 else 0xD0
STR = 0x6000
npl = int(sys.argv[6]) if len(sys.argv) > 6 else 8
scr = int(sys.argv[7], 0) if len(sys.argv) > 7 else 0
spr = int(sys.argv[8]) if len(sys.argv) > 8 else 0      # 1: sprite 0, 64 px wide pattern, at $50000
code = bytearray()
def w(x): code.extend(struct.pack(">H", x & 0xFFFF))
def l(x): code.extend(struct.pack(">I", x & 0xFFFFFFFF))
def mw(imm, addr): w(0x33FC); w(imm); l(addr)
def mb(imm, addr): w(0x13FC); w(imm & 0xFF); l(addr)
def ml(imm, addr): w(0x23FC); l(imm); l(addr)
C = 0xDFF000
code.extend(struct.pack(">II", 0x1000, 0xF80008))
mw(0x7FFF, C + 0x09A); mw(0x7FFF, C + 0x096)
mb(3, 0xBFE201); mb(2, 0xBFE001)
for bank in range(8):
    mw(bank << 13, C + 0x106)                       # BPLCON3: colour bank, LOCT = 0 (high nibbles)
    for j in range(32):
        i = bank * 32 + j
        mw(((i & 15) << 8) | ((i >> 4) << 4), C + 0x180 + 2 * j)
mw(0, C + 0x106)
cop = [(0x0100, 0x0200 | ((npl & 7) << 12) | (0x10 if npl > 7 else 0) | (0x8000 if hires else 0)), (0x0102, scr), (0x0104, 0), (0x0108, 0 if hires else 40), (0x010A, 0 if hires else 40), (0x01FC, fmode)]
for k in range(npl):
    p = 0x10000 + k * STR
    cop += [(0x00E0 + 4 * k, p >> 16), (0x00E2 + 4 * k, p & 0xFFFF)]
cop += ([(0x0120, 5), (0x0122, 0)] if spr else [])
cop += [(0x008E, 0x2C81), (0x0090, 0x2CC1), (0x0092, ddfs), (0x0094, ddfe), (0xFFFF, 0xFFFE)]
a = 0x1800
for r, v in cop:
    mw(r, a); mw(v, a + 2); a += 4
ml(0x1800, C + 0x080); mw(0, C + 0x088); mw(0x83A0 if spr else 0x8380, C + 0x096)
loop = len(code)
w(0x5279); l(0x0100); w(0x6000); w((loop - len(code)) & 0xFFFF)
rom = bytearray(524288); rom[:len(code)] = code
open(out + "/rom.hex", "w").write("".join("%04x\n" % ((rom[i] << 8) | rom[i + 1]) for i in range(0, len(rom), 2)))
chip = bytearray(0x42000 - 0x10000 + 0x10000)
for k in range(8):
    for y in range(256):
        for c in range(80):
            idx = (c * 37 + 11) & 255
            chip[k * STR + y * 80 + c] = 0xFF if (idx >> k) & 1 else 0x00
if spr:
    o = 0x50000 - 0x10000
    chip[o:o+8] = struct.pack(">HHHH", 0x5060, 0x9000, 0, 0)
    for y in range(0x40):
        chip[o+8+16*y:o+8+16*y+8] = b"\xff" * 8
open(out + "/chip.hex", "w").write("".join("%04x\n" % ((chip[i] << 8) | chip[i + 1]) for i in range(0, len(chip), 2)))
