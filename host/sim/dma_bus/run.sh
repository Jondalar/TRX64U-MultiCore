#!/bin/sh
# T16 Q8 in simulation: tb_dma_bus (GHDL), ultimate_sys with rvlite_bfm_dma_bus.vhd in
# place of the RISC-V: the app's DMA writes (C64_STOP, C64_DMA_MEMONLY, the window at
# 0x050000) and whether each byte becomes a C64 bus cycle (dma_bus). A MEMONLY write of
# $0000/$0001 must, or it never reaches the 6510 port.
# Needs ghdl on PATH (OSS CAD Suite). One process at a time, niced.
set -e
here=$(cd "$(dirname "$0")" && pwd)
design=$(cd "$here/../.." && pwd)
root=$(cd "$design/.." && pwd)
usb=$here/../usb
out=$here/build
mkdir -p "$out/tb"
G="nice -n 15 ghdl"
opts="--std=08 -fsynopsys -frelaxed -Wno-hide -Wno-shared -Wno-library"

"$root/tools/gideon_sources.sh"
python3 - "$design/gideon_vhdl.tcl" "$root/vendor/gideon/fpga" "$design" > "$out/files.txt" <<'PY'
import sys
tcl, g, design = sys.argv[1:]
body = open(tcl).read().split("\nset g ", 1)[1].split("set gvhdl [list", 1)[1].split("]", 1)[0]
for tok in body.replace("\\", " ").split():
    p = tok.replace("$g", g)
    print(p if p.startswith("/") else design + "/" + p)
PY

(cd "$out/tb" && $G -a $opts --work=unisim "$usb/unisim_sim.vhd" &&
  $G -i $opts "$usb/tl_string_util_pkg.vhd" $(cat "$out/files.txt") &&
  $G -i $opts "$here/tb_dma_bus_pkg.vhd" "$here/tb_dma_bus.vhd" "$here/rvlite_bfm_dma_bus.vhd" &&
  $G -m $opts tb_dma_bus && $G -a $opts "$here/rvlite_bfm_dma_bus.vhd" && $G -e $opts tb_dma_bus) > "$out/tb.log" 2>&1 ||
  { tail -20 "$out/tb.log"; exit 1; }
(cd "$out/tb" && $G -r $opts tb_dma_bus --stop-time=2ms > run.log 2>&1) || true
grep -E "BFM:|PASS|FAIL|error" "$out/tb/run.log" | sed 's/^.*(report note)://'
grep -q PASS "$out/tb/run.log"

