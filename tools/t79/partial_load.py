#!/usr/bin/env python3
"""T79 PoC: shift a partial bitstream into the running FPGA over JTAG (CFG_IN only: no
JPROGRAM, no JSTART), so the static part (the host) keeps running.  partial_load.py FILE.bit

Run with the JTAG venv (pyftdi):
    ~/.cache/1541ultimate-tools/jtag-venv/bin/python partial_load.py FILE.bit
Recovered from the 1541ultimate session transcript (2026-10-12); it lived only in a job temp dir.
Before a swap on a running host: tools/trxdbg.sh decouple on (T79.1 section 7), afterwards off."""
import os, sys, time
sys.path.insert(0, os.path.expanduser("~/Development/1541ultimate-tools/tooling"))
import u64ii_jtag as jt
from u64ii_bscan import Blaster

data = open(sys.argv[1], "rb").read()
assert b"\xaa\x99\x55\x66" in data[:4096], "no sync word"
cable = Blaster.open()
tap = jt.Tap(cable)
tap.reset()
idc = tap.idcode()
assert idc & 0x0FFFFFFF == 0x0362C093, hex(idc)
t = time.monotonic()
tap.ir(jt.IR_CFG_IN)
# the USB-Blaster shifts LSB first only: reverse each byte's bits (= MSB first of the original)
rev = bytes(int(f"{b:08b}"[::-1], 2) for b in data)
m = tap.m
m.tms(0b001, 3)                                   # Select-DR, Capture-DR, Shift-DR
m.bytes_out(rev[:-1])
m.bits_out(rev[-1], 7)
m.tms(0b011, 3, tdi=(rev[-1] >> 7) & 1)           # last bit, Update, Idle
m.flush()
tap.idle(2000)
cap = tap.ir_capture()
tap.reset()
print(f"partial {sys.argv[1]}: {len(data)} bytes in {time.monotonic() - t:.1f} s, IR capture 0x{cap:02X} "
      f"(INIT {bool(cap & jt.IR_CAPTURE_INIT)}, DONE {bool(cap & jt.IR_CAPTURE_DONE)})")
