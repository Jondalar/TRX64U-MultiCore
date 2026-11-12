#!/usr/bin/env python3
"""Decode the BLING_KEYB edge recorder of c64_basic (0x300000, register 11)
as UART, 8N1, LSB first; the recorder keeps the first 1024 changes after
loading the bitstream.

    ~/.cache/1541ultimate-tools/jtag-venv/bin/python tools/bling_uart.py [baud]
"""
import os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import trxdbg  # noqa: E402
SYS_HZ = 31.5278e6
BAUD = float(sys.argv[1]) if len(sys.argv) > 1 else 250000
d = trxdbg.Dbg()
try:
    n = d.read(trxdbg.REG_BASE + 11)
    words = [d.read(0x300000 + i) for i in range(min(n, 1024))]
finally:
    d.close()
ed = [(w >> 31, (w & 0x7FFFFFFF) / SYS_HZ) for w in words]
bit = 1.0 / BAUD
def level(t):
    lv = 1
    for l, te in ed:
        if te > t: break
        lv = l
    return lv
out, i, last = [], 0, None
while i < len(ed):
    l, t = ed[i]
    if l == 0:                                    # start bit edge
        v = 0
        for b in range(8):
            v |= level(t + (1.5 + b) * bit) << b
        stop = level(t + 9.5 * bit)
        gap = (t - last) * 1000 if last is not None else 0
        out.append((gap, v, stop))
        last = t
        tend = t + 10 * bit
        while i < len(ed) and ed[i][1] < tend - bit / 2: i += 1
        continue
    i += 1
print(f"{n} edges, {len(out)} bytes")
for gap, v, stop in out:
    print(f"{gap:8.1f} ms  {v:02X}{'' if stop else '  framing!'}")
