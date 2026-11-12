#!/usr/bin/env python3
"""T182 step 2: the cycle check of run_emu.sh. Reads the "mark|n|cycle" lines of a bench log (tb_c64scpu.v: every write to $0B00+n prints the PHI2 cycle it happened in) and
compares the PHI2 cycles between the markers of the second pass of each routine with the model's (tools/gen_c64scpu_emu.py).
  check_emu.py <log> <expect.json> [--no-r2] [--same-as <other log>]
--no-r2   the log is of the 6510 (R2 holds 65C02 opcodes it does not know): only routine R1 is compared
--same-as the R1 cycles must also equal the other log's (the 6510 run: the 816's PHI2 count equals the 6510's)
Prints "cyc| ..." lines and one PASS or FAIL line; exit 1 on FAIL."""
import json
import re
import sys


def marks(path):
    m = {}
    for ln in open(path):
        g = re.match(r"mark\|(\d)\|(\d+)", ln)
        if g:
            m.setdefault(int(g.group(1)), []).append(int(g.group(2)))
    return m


def delta(m, a, b):
    if len(m.get(a, [])) < 2 or len(m.get(b, [])) < 2:
        return None
    return m[b][1] - m[a][1]


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    log, exp = args[0], json.load(open(args[1]))
    m = marks(log)
    r1, r2 = delta(m, 0, 1), delta(m, 2, 3)
    bad = []
    print("cyc| R1 second pass: %s PHI2 cycles (model %d)" % (r1, exp["r1_cycles"]))
    if r1 != exp["r1_cycles"]:
        bad.append("R1")
    if "--no-r2" not in sys.argv:
        print("cyc| R2 second pass: %s PHI2 cycles (model %d)" % (r2, exp["r2_cycles"]))
        if r2 != exp["r2_cycles"]:
            bad.append("R2")
    if "--same-as" in sys.argv:
        other = sys.argv[sys.argv.index("--same-as") + 1]
        o1 = delta(marks(other), 0, 1)
        print("cyc| R1 second pass of %s: %s" % (other, o1))
        if o1 != r1:
            bad.append("R1 against the reference")
    if bad:
        print("FAIL check_emu: cycles differ in " + ", ".join(bad))
        sys.exit(1)
    print("PASS check_emu: the PHI2 cycles between the markers are the model's")


main()
