#!/usr/bin/env python3
"""T143: read the Neo Geo core's ROM space out of the board's DDR2 over JTAG (read only) and say which layout it holds.

Uses `tools/trxdbg.sh ddrdump <byte address> <length>` only: 32 bit reads through the host's JTAG window, no write to the DDR2, no reset, no register of the
core. (ddrdump moves the host's 16 MB window select, the register every JTAG read of the DDR2 moves; nothing else.) The expected bytes come from the same ROM
files the board loaded (tools/neogeo_layout.py): the MiSTer layout (what the core was written for) and the application's.

  tools/neogeo_ddrcheck.py --roms DIR --c-start 4              read the board, compare, print a verdict per sample
  tools/neogeo_ddrcheck.py --roms DIR --c-start 4 --dry        only print the commands and the expected bytes (run the commands by hand)

--c-start is the "C ROM from N MB" of the application's log line "[neogeo] mslug: ROM space .. MB, C ROM from N MB".
"""
import argparse
import os
import re
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import neogeo_layout as nl  # noqa: E402

HERE = os.path.dirname(os.path.abspath(__file__))
# (region, offset in the region, length)
SAMPLES = [("bios", 0x0000, 16), ("bios", 0x1F000, 16),
           ("sfix", 0x0040, 32), ("sfix", 0x4000, 32),
           ("s", 0x0040, 32), ("s", 0x4000, 32),
           ("p", 0x0200, 16),
           ("c", 0x000000, 32), ("c", 0x001000, 32), ("c", 0x123450, 32), ("c", 0x3FFFE0, 32),
           ("c", 0x800000 + 0x001000, 32), ("c", 0xBFFFE0 + 0x40, 32)]


def dump(addr, n):
    out = subprocess.run([os.path.join(HERE, "trxdbg.sh"), "ddrdump", "0x%X" % addr, str(n)], capture_output=True, text=True, timeout=300)
    if out.returncode:
        sys.exit("ddrdump failed: " + out.stderr.strip())
    b = bytearray()
    for line in out.stdout.splitlines():
        m = re.match(r"\s*[0-9A-Fa-f]+:\s*((?:[0-9A-Fa-f]{2}\s*)+)", line)
        if m:
            b += bytes(int(x, 16) for x in m.group(1).split())
    return bytes(b[:n])


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--roms", required=True)
    ap.add_argument("--c-start", type=int, required=True)
    ap.add_argument("--dry", action="store_true")
    a = ap.parse_args()
    lay = {k: nl.build(a.roms, k, c_start=a.c_start)[0] for k in ("mister", "app")}
    wins = {"mister": 0, "app": 0}
    for name, off, n in SAMPLES:
        base = nl.ROM_BASE + lay["mister"][name][0] + off
        exp = {k: lay[k][name][1][off:off + n] for k in lay}
        hx = lambda b: " ".join("%02X" % x for x in b)
        print("%-5s +0x%06X  DDR2 0x%08X  tools/trxdbg.sh ddrdump 0x%X %d" % (name, off, base, base, n))
        print("      mister: %s" % hx(exp["mister"]))
        if exp["app"] != exp["mister"]:
            print("      app   : %s" % hx(exp["app"]))
        if a.dry:
            continue
        got = dump(base, n)
        print("      board : %s" % hx(got))
        tag = []
        for k in ("mister", "app"):
            if got == exp[k]:
                tag.append(k)
                if exp["app"] != exp["mister"]:
                    wins[k] += 1
        print("      -> %s" % ("matches " + " and ".join(tag) if tag else "MATCHES NEITHER"))
    if not a.dry:
        print("\ndistinguishing samples (the layouts differ there): mister %d, app %d" % (wins["mister"], wins["app"]))


if __name__ == "__main__":
    main()
