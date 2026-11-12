#!/usr/bin/env python3
"""T83: put the VIC-20's ROMs into the VC-20 core over JTAG and reset it.

    tools/vc20_load.sh CHAR.bin BASIC.bin KERNAL.bin [peek ADDR [LEN]]

The VC-20 core has no ROM in its bitstream. Its memories are at their VIC-20 addresses in the JTAG
byte window (the same 64 KB as the C64's RAM): CHAR (4096 bytes) at $8000, BASIC (8192) at $C000,
KERNAL (8192) at $E000, RAM at $0000/$1000, colour RAM at $9400. After the three images are in, the
machine is reset by the machine control register (0x100009 bit 0), which is what the host's
core_reset_req carries; the app's machine:reset/machine:reboot are NOT used (they would write the
C64's ROMs into the VC-20). Read back and compare are done for the first 64 bytes of each image.
"""
import sys
import time

import trxdbg


def main(argv):
    if len(argv) < 3:
        print(__doc__)
        return 2
    char, basic, kernal = (open(p, "rb").read() for p in argv[:3])
    if len(char) != 4096 or len(basic) != 8192 or len(kernal) != 8192:
        print(f"sizes: CHAR {len(char)} (4096), BASIC {len(basic)} (8192), KERNAL {len(kernal)} (8192)")
        return 2
    d = trxdbg.Dbg()
    try:
        for name, base, data in (("CHAR", 0x8000, char), ("BASIC", 0xC000, basic), ("KERNAL", 0xE000, kernal)):
            d.write_bytes(base, data)
            back = bytes(d.read(base + i) & 0xFF for i in range(64))
            print(f"{name} at ${base:04X}: {len(data)} bytes, first 64 read back {'ok' if back == data[:64] else 'DIFFERENT'}")
        ctl = d.read(trxdbg.MACHINE) & 0x1F
        d.write(trxdbg.MACHINE, ctl | 1)
        time.sleep(0.05)
        d.write(trxdbg.MACHINE, ctl & ~1)
        print("reset released")
        names = ["id", "status", "frames", "cpuaddr", "pixel enables", "lines", "cpu addr changes"]
        for rnd in range(2):
            time.sleep(0.3)
            w = [d.trx_read(4 * i) for i in range(7)]
            print("core words: " + ", ".join(f"{n} {v:#x}" for n, v in zip(names, w)))
        print("status b0 = in reset, b2 = the host asks for reset (the app's C64_RESET!), b7 = left reset once")
        if len(argv) > 3 and argv[3] == "peek":
            a = int(argv[4], 16)
            n = int(argv[5], 0) if len(argv) > 5 else 16
            print(f"{a:04X}: " + " ".join(f"{d.read(a + i) & 0xFF:02X}" for i in range(n)))
    finally:
        d.close()
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
