#!/usr/bin/env python3
"""Show the last byte the C64U keyboard sent (register 2 bits 23:16) each time
it changes, for SECONDS seconds; used to make the keyboard's code table.

    ~/.cache/1541ultimate-tools/jtag-venv/bin/python tools/bling_watch.py 60
"""
import os, sys, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import trxdbg
d = trxdbg.Dbg(); prev = None; t0 = time.time(); seq = []
try:
    while time.time() - t0 < float(sys.argv[1]):
        v = d.read(trxdbg.REG_BASE + 2) >> 16 & 0xFF
        if v != prev:
            seq.append(v); print(f"{time.time()-t0:6.1f} {v:02X}", flush=True); prev = v
finally:
    d.close()
print("downs:", " ".join(f"{v:02X}" for v in seq if v < 0x80 and v != 0x55))
