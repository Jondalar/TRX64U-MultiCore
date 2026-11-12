#!/usr/bin/env python3
"""netfix.py <in.v> <out.v>: GHDL's netlist of a design with a black box (the Verilog 6805) declares the box's output wires twice and assigns
them to themselves (assign x = x;), which iverilog refuses or turns into a loop. One declaration each, no self-assignment."""
import re, sys
seen, out = set(), []
for line in open(sys.argv[1]):
    if line.startswith("module "):
        seen = set()
    if re.match(r"^  assign (\w+) = \1;", line):
        continue
    m = re.match(r"^  wire (?:\[[^\]]*\] )?(\w+);\s*$", line)
    if m:
        if m.group(1) in seen:
            continue
        seen.add(m.group(1))
    out.append(line)
open(sys.argv[2], "w").write("".join(out))
