#!/usr/bin/env python3
"""T217: the test programs of the program-start benches (cores/c64scpu/sim/run_prgrun.sh, cores/c128/sim/run_prgrun.sh), built here, nothing from a machine.
  prgrun_gen.py <scpu|c128> <outdir>      writes basic.prg and ml.prg and prints  KEY=value  lines (the marker addresses and values the bench checks)
BASIC: 10 POKE <m>,42 / 20 POKE <m+1>,PEEK(45 or 47): the second marker is the low byte of VARTAB after RUN, which BASIC computes from the program's own line links, so
it proves the relinked chain and the pointers the application wrote. ML: LDA #42 / STA <m+2> / RTS, started by SYS."""
import sys, os

core, out = sys.argv[1], sys.argv[2]
scpu = core == "scpu"
start = 0x0801 if scpu else 0x1C01
marker = 0xC000 if scpu else 0x0B00          # the C128 bench reads RAM block 0, $0B00 is the unused tape buffer
vartab = 45 if scpu else 47                    # TXTTAB $2B, VARTAB $2D (C64);  TXTTAB $2D, VARTAB $2F (C128)


def line(addr, num, body):
    nxt = addr + 2 + 2 + len(body) + 1
    return bytes([nxt & 255, nxt >> 8, num & 255, num >> 8]) + body + b"\0", nxt


POKE = 0x97
PEEK = 0xC2
l1, a2 = line(start, 10, bytes([POKE]) + b" %d,42" % marker)
l2, a3 = line(a2, 20, bytes([POKE]) + b" %d," % (marker + 1) + bytes([PEEK]) + b"(%d)" % vartab)
prog = l1 + l2 + b"\0\0"
end = a3 + 2
open(os.path.join(out, "basic.prg"), "wb").write(bytes([start & 255, start >> 8]) + prog)
ml_at = 0xC100 if scpu else 0x1300
ml = bytes([0xA9, 0x2A, 0x8D, (marker + 2) & 255, (marker + 2) >> 8, 0x60])
open(os.path.join(out, "ml.prg"), "wb").write(bytes([ml_at & 255, ml_at >> 8]) + ml)
print("MARK=%04x" % marker)
print("VAR_LO=%02x" % (end & 255))
print("MLMARK=%04x" % (marker + 2))
print("MLAT=%d" % ml_at)
print("TXT_LO=%02x TXT_HI=%02x" % (start & 255, start >> 8))
