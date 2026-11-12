#!/usr/bin/env python3
"""T92 D3: cores/cpc/gen/u765.sv = cores/cpc/rtl/u765/u765.sv (MiSTer-devel/Amstrad_MiSTer, GPL v2 or later, Gyorgy Szombathelyi) plus the one
mechanical change that Verilator needs, counted: the function SECTOR_SIZE declares `reg [15:0] logical_size = <expression of its arguments>;`.
In a static function the initialiser runs once at time 0, which Verilator rejects ("static variable initializer dependent on function I/O")
and Vivado may take either way; the intent is one evaluation per call, so the declaration is split from the assignment. The benches and the
synthesis read gen/, never rtl/ (one artifact for both).
  tools/cpc_gen.py          (re)make cores/cpc/gen/u765.sv
  tools/cpc_gen.py --check  gen/u765.sv is what rtl/u765/u765.sv gives (the lint stage runs this)
"""
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = os.path.join(ROOT, "cores/cpc/rtl/u765/u765.sv")
DST = os.path.join(ROOT, "cores/cpc/gen/u765.sv")
OLD = "\t\treg [15:0] logical_size = (16'h80 << (n[3] ? 4'h8 : n[2:0]));\n"
NEW = "\t\treg [15:0] logical_size;\n\t\tlogical_size = (16'h80 << (n[3] ? 4'h8 : n[2:0]));\n"
HDR = ("// T92 D3: cores/cpc/rtl/u765/u765.sv with one change made by tools/cpc_gen.py (the initialiser of `logical_size` in SECTOR_SIZE is an\n"
       "// assignment per call). Everything else, the licence header included, is the upstream file. Do not edit.\n")


def make():
    s = open(SRC).read()
    if s.count(OLD) != 1:
        raise SystemExit("cpc_gen: expected one declaration with an initialiser in SECTOR_SIZE, found %d (upstream changed?)" % s.count(OLD))
    return HDR + s.replace(OLD, NEW)


def main(argv):
    if argv[1:] == ["--check"]:
        if not os.path.exists(DST) or open(DST).read() != make():
            print("cpc_gen: cores/cpc/gen/u765.sv is not what tools/cpc_gen.py makes of cores/cpc/rtl/u765/u765.sv (run tools/cpc_gen.py)")
            return 1
        print("cpc_gen: u765.sv matches")
        return 0
    os.makedirs(os.path.dirname(DST), exist_ok=True)
    open(DST, "w").write(make())
    print("cpc_gen: wrote", DST)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
