#!/usr/bin/env python3
"""T182 step 5: the cycle check of run_mirror.sh (S5). Reads the "mark|n|cycle" lines of a bench log (tb_c64scpu.v: a completed 65816 write to 24-bit $00:0B00+n prints the PHI2
cycle it completed in) and the expectations of tools/gen_c64scpu_mirror.py. Two phases, each between a mark 0 and a mark 1: P1 the 1,024-store screen fill in the reset mode (mirrored),
P2 the same fill in mode 0x84 (nothing mirrored). The model is the VICE accounting (wait_buffer / write_stretch) run over the program's instruction cycles.
  check_mirror.py <log> <expect.json>      prints "cyc| ..." lines and one PASS or FAIL line; exit 1 on FAIL"""
import json
import re
import sys


def main():
    log, exp = sys.argv[1], json.load(open(sys.argv[2]))
    m = {0: [], 1: []}
    for ln in open(log):
        g = re.match(r"mark\|(\d)\|(\d+)", ln)
        if g and int(g.group(1)) in m:
            k, c = int(g.group(1)), int(g.group(2))
            # a mirrored store prints twice: when the 65816 completes it and when it is on the C64 bus (a few PHI2 cycles later); the first is the CPU's
            if not (m[k] and c - m[k][-1] <= 4):
                m[k].append(c)
    if len(m[0]) != 2 or len(m[1]) != 2:
        print("cyc| expected 2 phases, found %d starts and %d ends (the program stopped)" % (len(m[0]), len(m[1])))
        print("FAIL mirror cycles (phases)")
        return 1
    p1, p2 = m[1][0] - m[0][0], m[1][1] - m[0][1]
    bad = []

    def chk(tag, ok, text):
        print("cyc| %s %s: %s" % ("ok  " if ok else "FAIL", tag, text))
        if not ok:
            bad.append(tag)
    chk("S5 mirrored", abs(p1 - exp["p1"]) <= 0.03 * exp["p1"] + 3, "1,024 mirrored stores: %d PHI2 cycles, VICE model %d (+-3 %%)" % (p1, exp["p1"]))
    chk("S5 unmirrored", abs(p2 - exp["p2"]) <= 0.03 * exp["p2"] + 3, "1,024 unmirrored stores: %d PHI2 cycles, VICE model %d (+-3 %%)" % (p2, exp["p2"]))
    lines = open(log).read().splitlines()
    q = [re.match(r"mirq\|(\d+) .*, (\d+) landed", ln) for ln in lines]
    q = [x for x in q if x]
    f = [re.match(r"mir\|\d+ .*, (\d+) of them held off", ln) for ln in lines]
    f = [x for x in f if x]
    qn, qk, fn = (int(q[0].group(1)), int(q[0].group(2)), int(f[0].group(1))) if q and f else (0, -1, 0)
    chk("S5 retry reached", fn >= 1, "%d mirror writes held off by the VIC (aec low at the PHI2 fall) and driven again" % fn)
    chk("S5 retry lands", qn > 0 and qn == qk, "%d mirror writes accepted by the queue, %d landed on the C64 bus" % (qn, qk))
    chk("S5 ratio", p1 > 2 * p2, "mirrored / unmirrored = %.2f (the write buffer belongs to the mirrored write only)" % (p1 / max(p2, 1)))
    print(("FAIL" if bad else "PASS") + " mirror cycles" + (" (" + ", ".join(bad) + ")" if bad else ""))
    return 1 if bad else 0


sys.exit(main())
