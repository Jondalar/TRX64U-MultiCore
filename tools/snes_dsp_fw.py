#!/usr/bin/env python3
"""T218: the bench firmware of the DSP-n coprocessor (sim `dsp`), written for this project, not a chip dump: 2048 program words (3 bytes, low byte first) and 1024 data
words (2 bytes, low byte first) = the 8192 byte file the application takes. Data word i = (0x1234 + i * 0x9E37) & 0xFFFF. The program puts four values into DR, each only
after the 65816 has taken the one before (JRQM loops):
  data[5]       LDI RP,5;    OP DR <- ROM          (the data ROM through RP, an index above 0)
  data[1023]    LDI RP,1023; OP DR <- ROM          (the last data word: the end of the stream, 8190..8191)
  data[0]       LDI RP,0;    OP DR <- ROM          (the first data word: the byte after the 6144 program bytes, fw_sel and the word counter's wrap)
  $BEEF         JP 2046; word 2046 LDI DR,$BEEF; word 2047 JRQM 2047   (program words past 2 and the last one, 6141..6143)
Encodings: LDI = 11 | imm16 << 6 | dst (RP 5, DR 6); OP = 00 ... SRC 6 (data ROM) DST 6 (DR) = 0x66; JP = 10 | BRCH << 13 | NA << 2, BRCH 0x100 always, 0x0BE while RQM.
Usage: snes_dsp_fw.py <out.bin>   or   snes_dsp_fw.py --expect   (the eight bytes the 65816 program leaves at $7E0200.., low byte first, hex)"""
import sys


def d(i):
    return (0x1234 + i * 0x9E37) & 0xFFFF


def ldi(imm, dst):
    return (3 << 22) | (imm << 6) | dst


def jp(brch, na):
    return (2 << 22) | (brch << 13) | (na << 2)


OP_ROM_DR = 0x66
JRQM, JMP = 0x0BE, 0x100
prog = [0] * 2048
prog[0:9] = [ldi(5, 5), OP_ROM_DR, jp(JRQM, 2), ldi(1023, 5), OP_ROM_DR, jp(JRQM, 5), ldi(0, 5), OP_ROM_DR, jp(JRQM, 8)]
prog[9] = jp(JMP, 2046)
prog[2046] = ldi(0xBEEF, 6)
prog[2047] = jp(JRQM, 2047)
vals = [d(5), d(1023), d(0), 0xBEEF]
if sys.argv[1] == "--expect":
    print(" ".join("%02x %02x" % (v & 255, v >> 8) for v in vals))
    sys.exit(0)
out = bytearray()
for w in prog:
    out += bytes([w & 255, (w >> 8) & 255, (w >> 16) & 255])
for i in range(1024):
    out += bytes([d(i) & 255, d(i) >> 8])
assert len(out) == 8192
open(sys.argv[1], "wb").write(out)
