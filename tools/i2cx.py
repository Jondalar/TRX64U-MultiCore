#!/usr/bin/env python3
"""I2C by hand over JTAG, for exploring the board's buses (c64_basic,
register 10: the design lets go of the buses while bit 31 is set).

    tools/i2cx.py levels                     line levels and KBJOY_INT
    tools/i2cx.py scan [bus ...]             addresses that acknowledge
    tools/i2cx.py read BUS ADDR REG [N]      N bytes from register REG
    tools/i2cx.py write BUS ADDR REG BYTE ...

BUS is 18 (1.8 V), hd (HDMI) or 33 (3.3 V); ADDR is the 7-bit address, hex.
Each line change is one JTAG access, so the clock runs at a few hundred Hz;
I2C has no lower limit. Afterwards the design gets its buses back.
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import trxdbg  # noqa: E402

REG = trxdbg.REG_BASE + 10
BUSES = {"18": 0, "hd": 1, "33": 2}


class Bus:
    def __init__(self, d, bus):
        self.d, self.b = d, BUSES[bus]
        self.scl = self.sda = 0                 # 1 = pulled low

    def _set(self, scl_low, sda_low):
        self.scl, self.sda = scl_low, sda_low
        self.d.write(REG, 1 << 31 | (sda_low << (3 + self.b)) | (scl_low << self.b))

    def _sda(self):
        return self.d.read(REG) >> (3 + self.b) & 1

    def start(self):
        self._set(0, 0)
        self._set(0, 1)
        self._set(1, 1)

    def stop(self):
        self._set(1, 1)
        self._set(0, 1)
        self._set(0, 0)

    def byte_out(self, v):
        for i in range(7, -1, -1):
            low = 0 if v >> i & 1 else 1
            self._set(1, low)
            self._set(0, low)
            self._set(1, low)
        self._set(1, 0)
        self._set(0, 0)
        ack = self._sda() == 0
        self._set(1, 0)
        return ack

    def byte_in(self, last):
        v = 0
        for _ in range(8):
            self._set(1, 0)
            self._set(0, 0)
            v = v << 1 | self._sda()
            self._set(1, 0)
        self._set(1, 0 if last else 1)
        self._set(0, 0 if last else 1)
        self._set(1, 0 if last else 1)
        return v


def release(d):
    d.write(REG, 0)


def main(argv):
    if not argv:
        print(__doc__)
        return 1
    cmd, args = argv[0], argv[1:]
    d = trxdbg.Dbg()
    try:
        if cmd == "levels":
            d.write(REG, 1 << 31)
            v = d.read(REG)
            for name, b in BUSES.items():
                print(f"bus {name}: SCL {v >> b & 1} SDA {v >> (3 + b) & 1}")
            print(f"KBJOY_INT {v >> 6 & 1}")
        elif cmd == "scan":
            for name in args or BUSES:
                bus = Bus(d, name)
                found = []
                for a in range(0x08, 0x78):
                    bus.start()
                    if bus.byte_out(a << 1 | 1):
                        found.append(a)
                        bus.byte_in(True)
                    bus.stop()
                print(f"bus {name}: " + (" ".join(f"{a:02X}" for a in found) or "nothing"))
        elif cmd == "read":
            bus = Bus(d, args[0])
            a, r = int(args[1], 16), int(args[2], 16)
            n = int(args[3], 0) if len(args) > 3 else 1
            bus.start()
            ok = bus.byte_out(a << 1) and bus.byte_out(r)
            bus.start()
            ok = ok and bus.byte_out(a << 1 | 1)
            data = [bus.byte_in(i == n - 1) for i in range(n)] if ok else []
            bus.stop()
            print("no acknowledge" if not ok else " ".join(f"{b:02X}" for b in data))
        elif cmd == "write":
            bus = Bus(d, args[0])
            bus.start()
            ok = all(bus.byte_out(v) for v in [int(args[1], 16) << 1] + [int(x, 16) for x in args[2:]])
            bus.stop()
            print("ok" if ok else "no acknowledge")
        else:
            print(__doc__)
            return 1
    finally:
        release(d)
        d.close()
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
