#!/usr/bin/env python3
"""Status of the ddr2_test design (designs/ddr2_test) over the USB-Blaster: calibration, rounds,
errors, the first error. The design's JTAG register map is in designs/ddr2_test/top.v.

    tools/ddr2_status.py                 print the status once
    tools/ddr2_status.py watch [secs]    print a line every few seconds (default 5) until ^C
    tools/ddr2_status.py clear           clear rounds, errors, commands and the first error
    tools/ddr2_status.py ctl [value]     read or set the tester control (reset value 0x1E):
                                         b0 LFSR order, b1 alternate the order every round,
                                         b2 pingpong pass, b3 write masks, b4 run
    tools/ddr2_status.py hold [clocks]   read or set the retention hold (clocks of 13.3 ns between
                                         the masked write pass and the read pass)
    tools/ddr2_status.py refi [clocks]   read or set the refresh interval (clocks of 13.3 ns; reset value 560 =
                                         7.47 us; 40 is refresh stress: a refresh every 0.53 us under load)
    tools/ddr2_status.py oewin [value]   read or set the DQ/DQS drive window (ddr2_lean reg 0xD: [1:0] cycles of
                                         drive before the data, [3:2] after it; reset 0x9 = 1 before, 2 after (board 2026-10-06); the silicon's
                                         tri-state latency against its data path is a guess, widen or narrow here)
    tools/ddr2_status.py lean            ddr2_lean's register block (taps, read delay, bitslip count,
                                         refresh counter, DQS capture of the last calibration read)

The design has to be loaded (tools/load.sh with designs/ddr2_test/build/ddr2_test.bit). The USB-Blaster
driver is that of tools/trxdbg.py.
"""
import sys
import time

import trxdbg

ID = 0xDD2A0002
STATUS_BITS = ["init done", "calibration done", "calibration ok", "controller up", "tester run",
               "errors seen", "first error kept", "ui reset"]


def read128(d, base):
    return sum(d.read(base + i) << (32 * i) for i in range(4))


def status(d):
    ident = d.read(0x00)
    if ident != ID:
        print(f"ID 0x{ident:08X}: this is not ddr2_test (0x{ID:08X})")
        return 1
    st = d.read(0x01)
    print("status 0x%02X: %s" % (st, ", ".join(n for i, n in enumerate(STATUS_BITS) if st >> i & 1) or "-"))
    c0, c1 = d.read(0x02), d.read(0x03)
    print("calibration: bitslip position %d, read delay %d, tap lane 0 %d, lane 1 %d (78 ps each)"
          % (c0 >> 24 & 15, c0 >> 16 & 7, c0 & 31, c0 >> 8 & 31))
    print("  window width lane 0 %d taps (%.2f ns)%s, lane 1 %d taps (%.2f ns)%s; positions with a window: %d"
          % (c1 & 63, (c1 & 63) * 0.078, " cut by the scan range" if c1 >> 16 & 1 else "",
             c1 >> 8 & 63, (c1 >> 8 & 63) * 0.078, " cut by the scan range" if c1 >> 17 & 1 else "", c1 >> 20 & 15))
    rounds, errors, cmds = d.read(0x04), d.read(0x05), d.read(0x11)
    pas = d.read(0x12)
    print("rounds %d, error words %d, commands %d; now pass %d, %s order" % (rounds, errors, cmds, pas & 15,
                                                                         "LFSR" if pas >> 4 & 1 else "sequential"))
    if st >> 6 & 1:
        burst = d.read(0x06)
        print("first error: round %d, pass %d (2 = read, 3 = pingpong), burst 0x%X (byte address 0x%X)"
              % (d.read(0x07), d.read(0x08), burst, burst * 16))
        exp, act = read128(d, 0x09), read128(d, 0x0D)
        print("  expected %032X\n  actual   %032X\n  xor      %032X" % (exp, act, exp ^ act))
    return 0


def lean(d):
    names = {0: "status", 1: "ctrl", 3: "tap (lane 1 [12:8], lane 0 [4:0])", 4: "read delay (lane 1 [6:4], lane 0 [2:0])",
             5: "bitslip pulses (lane 1 [7:4], lane 0 [3:0])", 6: "DQS capture lane 0 words 0-3", 7: "DQS capture lane 0 words 4-7",
             8: "DQS capture lane 1 words 0-3", 9: "DQS capture lane 1 words 4-7", 11: "refresh interval (clocks)",
             12: "refresh commands issued", 13: "DQ/DQS drive window (late [3:2], early [1:0] cycles)"}
    for a, n in names.items():
        print(f"0x{0x100 + a:03X} {n:44s} 0x{d.read(0x100 + a):08X}")
    return 0


def main(argv):
    d = trxdbg.Dbg()
    try:
        cmd = argv[0] if argv else "status"
        if cmd == "status":
            return status(d)
        if cmd == "watch":
            period = float(argv[1]) if len(argv) > 1 else 5.0
            while True:
                rounds, errors = d.read(0x04), d.read(0x05)
                print("%s rounds %d errors %d commands %d" % (time.strftime("%H:%M:%S"), rounds, errors, d.read(0x11)), flush=True)
                time.sleep(period)
        if cmd == "clear":
            d.write(0x22, 0)
            return 0
        if cmd == "ctl":
            if len(argv) > 1:
                d.write(0x20, int(argv[1], 0))
            print("ctl 0x%02X" % d.read(0x20))
            return 0
        if cmd == "hold":
            if len(argv) > 1:
                d.write(0x21, int(argv[1], 0))
            print("hold %d clocks" % d.read(0x21))
            return 0
        if cmd == "refi":
            if len(argv) > 1:
                d.write(0x10B, int(argv[1], 0))
            print("refresh interval %d clocks" % (d.read(0x10B) & 0xFFFF))
            return 0
        if cmd == "oewin":
            if len(argv) > 1:
                d.write(0x10D, int(argv[1], 0))
            print("drive window 0x%X" % (d.read(0x10D) & 0xF))
            return 0
        if cmd == "lean":
            return lean(d)
        print(__doc__)
        return 1
    finally:
        d.close()


if __name__ == "__main__":
    try:
        sys.exit(main(sys.argv[1:]))
    except KeyboardInterrupt:
        sys.exit(0)
