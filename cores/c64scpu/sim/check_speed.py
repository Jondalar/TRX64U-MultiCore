#!/usr/bin/env python3
"""T182 step 4: the cycle checks of run_speed.sh (S4, S10). Reads the "mark|n|cycle" lines of a bench log (tb_c64scpu.v: a completed 65816 write to 24-bit $00:0B00+n prints the PHI2
cycle it completed in) and the expectations of tools/gen_c64scpu_speed.py. Seven phases P0..P6, each between a mark 0 and a mark 1 (P0 is the first I/O loop, before any raster wait; P1..P6 below):
  P1 fast SRAM loop, display on     P2 fast SRAM loop, display off     P3 1 MHz, display off     P4 1 MHz, display on
  P5 fast, 1,000 x LDA $D020, display off     P6 fast, 1,000 x LDA $D012, display on
  check_speed.py <log> <expect.json> [--only s4|s10]
Prints "cyc| ..." lines and one PASS or FAIL line; exit 1 on FAIL.
S4   P2 (20 MHz) takes (cycles + 4) x PHI2 / 20 MHz PHI2 cycles (+-1.5 %), P3 (1 MHz) takes cycles + 4 (+-1 %), the ratio P3 / P2 is between 17 and 20.3 (spec S4),
     1,000 I/O reads (P5) take at least 1,000 PHI2 cycles (every I/O access is a PHI2 cycle, scpu64cpu.c:178-191) and at most IO_MAX (measured, VICE: 1,000); P0 the same reads first of all, so that a machine whose I/O waits for no PHI2 fails by its count, not by a hang; the 250 STX $D020 of P7 show their bytes on the C64 bus at the PHI2 fall in order, and the address / R/W is the same at the PHI2 rise and fall of every cycle (a 6510's bus cycle).
S10  P1 and P2 differ by at most 2 % (BA and badlines do not stretch SRAM code at 20 MHz), P4 is at least 3 % longer than P3 (they do at 1 MHz), the I/O loop with the display on
     (P6) is at least 3 % longer than with it off (P5): BA holds I/O."""
import json
import re
import sys

IO_MIN = 1000       # 1,000 I/O reads take at least 1,000 PHI2 cycles (VICE: exactly 1,000 for this loop, scpu64cpu.c:178-191)
IO_MAX = 1020       # measured 1,006 (spec 5.6): the address of 6 accesses in 1,000 is on the bus after the PHI2 rise, those take the next cycle (VICE 0); 1,020 = 1.4 % margin
IO_FIRST_MAX = 1500 # P0 runs with the display as the boot left it (badlines hold I/O: P6 shows +8 %)


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    only = sys.argv[sys.argv.index("--only") + 1] if "--only" in sys.argv else None
    if only:
        args = [a for a in args if a != only]
    log, exp = args[0], json.load(open(args[1]))
    m = {0: [], 1: []}
    bw, bus = [], None
    for ln in open(log):
        g = re.match(r"bw\|([0-9a-f]{4})\|([0-9a-f]{2})", ln)
        if g:
            bw.append(int(g.group(2), 16))
        g = re.match(r"bus\|(\d+) PHI2 cycles looked at, (\d+) with", ln)
        if g:
            bus = (int(g.group(1)), int(g.group(2)))
        g = re.match(r"mark\|(\d)\|(\d+)\|frame", ln)          # the 65816's own write line; the bus-side line is its mirrored copy (step 5), the same write twice
        if g and int(g.group(1)) in m:
            m[int(g.group(1))].append(int(g.group(2)))
    bad = []

    def chk(tag, ok, text):
        print("cyc| %s %s: %s" % ("ok  " if ok else "FAIL", tag, text))
        if not ok:
            bad.append(tag)
    np0 = min(len(m[0]), len(m[1]))
    if np0 >= 1:
        p0 = m[1][0] - m[0][0]
        chk("S4 I/O first", IO_MIN <= p0 <= IO_FIRST_MAX, "P0, 1,000 x LDA $D020 before the first raster wait: %d PHI2 cycles (%d .. %d)" % (p0, IO_MIN, IO_FIRST_MAX))
    if np0 != 7 or len(m[0]) != 7:
        print("cyc| expected 7 phases, found %d starts and %d ends (the program stopped)" % (len(m[0]), len(m[1])))
        print("FAIL speed (" + ", ".join(bad + ["phases"]) + ")")
        return 1
    d = [m[1][i] - m[0][i] for i in range(1, 7)]
    n = exp["cpu_cycles"] + 4
    fast = n * exp["phi2"] / 20e6
    print("cyc| P1..P6 = %s PHI2 cycles; model: 20 MHz loop %.1f, 1 MHz loop %d" % (d, fast, n))
    if only in (None, "s4"):
        chk("S4 20 MHz", abs(d[1] - fast) <= 0.015 * fast + 1, "P2 %d against %.1f" % (d[1], fast))
        chk("S4 1 MHz", abs(d[2] - n) <= 0.01 * n + 1, "P3 %d against %d" % (d[2], n))
        chk("S4 ratio", 17.0 <= d[2] / d[1] <= 20.3 + 0.3, "P3 / P2 = %.2f (17 .. 20.3)" % (d[2] / d[1]))
        chk("S4 I/O", IO_MIN <= d[4] <= IO_MAX, "1,000 x LDA $D020 take %d PHI2 cycles (%d .. %d)" % (d[4], IO_MIN, IO_MAX))
    if only in (None, "s4"):
        nw = exp.get("io_writes", 250)
        want = list(range(nw, 0, -1))
        chk("S4 bus data", bw[-nw:] == want and len(bw) >= nw, "250 x STX $D020 at 20 MHz: the last %d bytes on the bus at the PHI2 fall are %s (%d writes seen)" % (nw, "X = 250 .. 1 in order" if bw[-nw:] == want else "WRONG, first %s" % bw[-nw:][:6], len(bw)))
        chk("S4 bus phase", bus is not None and bus[1] == 0, "address and R/W the same at the PHI2 rise and fall in %s PHI2 cycles, %s that changed inside the high phase (a 6510's bus cycle)" % (bus[0] if bus else "?", bus[1] if bus else "?"))
    if only in (None, "s10"):
        chk("S10 SRAM, BA", abs(d[0] - d[1]) <= 0.02 * d[1], "display on %d, off %d" % (d[0], d[1]))
        chk("S10 1 MHz stretched", d[3] >= 1.03 * d[2], "display on %d, off %d" % (d[3], d[2]))
        chk("S10 I/O held by BA", d[5] >= 1.03 * d[4], "display on %d, off %d" % (d[5], d[4]))
    print(("FAIL" if bad else "PASS") + " speed" + (" (" + ", ".join(bad) + ")" if bad else ""))
    return 1 if bad else 0


sys.exit(main())
