#!/usr/bin/env python3
"""REU round trip on a running c64_basic, over JTAG (tools/trxdbg.py).

Assembles reu_roundtrip.asm with the repository's 64tass (tools/64tass.sh),
puts it at $C000, types SYS49152 through the keyboard buffer, then checks that
the page came back from the REU and that the DDR2 holds it where the REU lives
(word 0x2000000 + bank 1 * 32K words = 0x2008000; the bottom 64 MB belong to
the RISC-V).
"""
import os
import random
import subprocess
import sys
import tempfile
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, os.path.join(ROOT, "tools"))
import trxdbg  # noqa: E402

REU_BANK1_WORD = 0x2008000


def assemble(source):
    with tempfile.TemporaryDirectory() as tmp:
        out = os.path.join(tmp, "out.bin")
        subprocess.run([os.path.join(ROOT, "tools", "64tass.sh"), "-q", "-b", "-o", out, source],
                       check=True)
        return open(out, "rb").read()


def type_text(d, text):
    petscii = [ord(c) if c != "\n" else 0x0D for c in text.upper()]
    for i, b in enumerate(petscii):
        d.write(0x0277 + i, b)
    d.write(0x00C6, len(petscii))


def main():
    code = assemble(os.path.join(HERE, "reu_roundtrip.asm"))
    d = trxdbg.Dbg()
    try:
        seed = random.randrange(1, 256)
        d.write(0x0002, seed)
        for i, b in enumerate(code):
            d.write(0xC000 + i, b)
        type_text(d, "SYS49152\n")
        time.sleep(1.0)
        src = [d.read(0xC100 + i) & 0xFF for i in range(256)]
        back = [d.read(0xC200 + i) & 0xFF for i in range(256)]
        ddr = [d.ddr_read(REU_BANK1_WORD + i) for i in range(128)]
    finally:
        d.close()
    ddr_bytes = [b for w in ddr for b in (w & 0xFF, w >> 8)]
    want = [(seed + i) & 0xFF for i in range(256)]
    ok = src == want and back == src and ddr_bytes == src
    print(f"seed {seed:02X}: source ok: {src == want}, fetched back ok: {back == src}, "
          f"DDR2 holds it: {ddr_bytes == src}")
    print("PASS" if ok else "FAIL")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
