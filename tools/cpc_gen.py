#!/usr/bin/env python3
"""T92 D3: cores/cpc/gen/u765.sv = cores/cpc/rtl/u765/u765.sv (MiSTer-devel/Amstrad_MiSTer, GPL v2 or later, Gyorgy Szombathelyi) plus two changes, counted.
(1) Mechanical, Verilator needs it: the function SECTOR_SIZE declares `reg [15:0] logical_size = <expression of its arguments>;`.
In a static function the initialiser runs once at time 0, which Verilator rejects ("static variable initializer dependent on function I/O")
and Vivado may take either way; the intent is one evaluation per call, so the declaration is split from the assignment. The benches and the
synthesis read gen/, never rtl/ (one artifact for both).
(2) T92 #17: a new image event (mount/eject) releases the global i_scan_lock when its drive's running scan held it (see OLD2 below).
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
# T92 #17: the image scan owns the global i_scan_lock from state 1 -> 2 until its end (state 2/3 -> 0). A mount or eject of the drive that owns it
# restarts or cancels that scan without releasing the lock, and every later scan then waits in state 1 for ever (RQM low). The new image event of a
# drive in scan state 2 or 3 releases the lock it holds (state 1 does not hold it). Benches: cores/cpc/sim tb_fdcscan.v (fdcscan2).
OLD2 = "\t\t\timage_scan_state[i] <= {1'b0, |img_size};\n"
NEW2 = OLD2 + "\t\t\tif (image_scan_state[i] >= 2'd2) i_scan_lock <= 0; // T92 #17: this drive's running scan held the lock\n"
HDR = ("// T92 D3: cores/cpc/rtl/u765/u765.sv with two changes made by tools/cpc_gen.py (the initialiser of `logical_size` in SECTOR_SIZE is an\n"
       "// assignment per call; T92 #17: a new image event releases the scan lock its drive's running scan held). Everything else, the licence header\n"
       "// included, is the upstream file. Do not edit.\n")


def make():
    s = open(SRC).read()
    if s.count(OLD) != 1:
        raise SystemExit("cpc_gen: expected one declaration with an initialiser in SECTOR_SIZE, found %d (upstream changed?)" % s.count(OLD))
    if s.count(OLD2) != 1:
        raise SystemExit("cpc_gen: expected one image_scan_state assignment in the mount block, found %d (upstream changed?)" % s.count(OLD2))
    return HDR + s.replace(OLD, NEW).replace(OLD2, NEW2)


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
