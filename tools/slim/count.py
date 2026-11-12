#!/usr/bin/env python3
"""LUT / FF of one module, alone (SLIM-state-path.md §1): yosys synth_xilinx -flatten, about +/-20 % off
Vivado's number, good for a before/after of one file.

    tools/slim/count.py trx64/dma_win.v dma_win
    tools/slim/count.py <(git show main:host/freeze_resp.v) freeze_resp

yosys from the OSS CAD Suite (~/.cache/trx64-oss/oss-cad-suite/bin) must be on PATH; run it niced.
Counts LUT1-6, FD* flip-flops and the RAM32/64 (distributed) and RAMB (block) cells; a RAM64M is four LUTs
in Vivado's report.
"""
import os
import re
import subprocess
import sys
import tempfile

if len(sys.argv) != 3:
    sys.exit(__doc__)
src, top = sys.argv[1], sys.argv[2]
with tempfile.TemporaryDirectory() as tmp:
    stat = os.path.join(tmp, "stat.txt")
    subprocess.run(["yosys", "-q", "-p",
                    f"read_verilog -sv {src}; synth_xilinx -flatten -top {top}; tee -o {stat} stat"],
                   check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    text = open(stat).read()
cells = {m.group(2): int(m.group(1)) for m in re.finditer(r"^\s+(\d+)\s+(\w+)\s*$", text, re.M)}
lut = sum(v for k, v in cells.items() if re.fullmatch(r"LUT[1-6]", k))
ff = sum(v for k, v in cells.items() if k.startswith("FD"))
ram = {k: v for k, v in cells.items() if k.startswith("RAM")}
print(f"{top}: LUT {lut}, FF {ff}, RAM cells {ram}")
