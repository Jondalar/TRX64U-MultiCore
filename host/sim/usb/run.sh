#!/bin/sh
# USB in simulation (GHDL, e.g. from the OSS CAD Suite, on PATH):
#  1. ultimate_sys with every VHDL file build.tcl reads, elaborated as built
#     (the real RISC-V);
#  2. tb_usb: the same with rvlite_bfm.vhd in place of the RISC-V and the ULPI
#     pins on ulpi_phy_model, once per PHY product ID (both branches of
#     nano_minimal's power-up);
#  3. tb_usb_phy (Icarus): usb_phy.v's meter, resets and data turnaround.
# Output under build/.
set -e
here=$(cd "$(dirname "$0")" && pwd)
design=$(cd "$here/../.." && pwd)
root=$(cd "$design/.." && pwd)
out=$here/build
mkdir -p "$out/elab" "$out/tb"
G="ghdl"
opts="--std=08 -fsynopsys -frelaxed -Wno-hide -Wno-shared -Wno-library"

# the VHDL list of build.tcl, in its order
"$root/tools/gideon_sources.sh"
python3 - "$design/gideon_vhdl.tcl" "$root/vendor/gideon/fpga" "$design" > "$out/files.txt" <<'EOF'
import re, sys
tcl, g, design = sys.argv[1:]
body = open(tcl).read().split("\nset g ", 1)[1].split("set gvhdl [list", 1)[1].split("]", 1)[0]
for tok in body.replace("\\", " ").split():
    p = tok.replace("$g", g)
    print(p if p.startswith("/") else design + "/" + p)
EOF

# ghdl -m sorts the files; the tb then analyzes rvlite_bfm last, so that
# ultimate_sys binds it in place of the real RISC-V
for d in elab tb; do
  (cd "$out/$d" && $G -a $opts --work=unisim "$here/unisim_sim.vhd" &&
    $G -i $opts "$here/tl_string_util_pkg.vhd" \
      $(cat "$out/files.txt"))
done
(cd "$out/elab" && $G -m $opts ultimate_sys) > "$out/elab.log" 2>&1 ||
  { tail -20 "$out/elab.log"; exit 1; }
echo "elaborated ultimate_sys"

(cd "$out/tb" && $G -i $opts "$here/tb_usb_pkg.vhd" "$here/ulpi_phy_model.vhd" "$here/tb_usb.vhd" "$here/rvlite_bfm.vhd" &&
  $G -m $opts tb_usb && $G -a $opts "$here/rvlite_bfm.vhd" && $G -e $opts tb_usb) > "$out/tb.log" 2>&1 ||
  { tail -20 "$out/tb.log"; exit 1; }

python3 "$root/vendor/gideon/tools/parse_nano.py" "$root/vendor/gideon/software/io/usb/nano_minimal.nan" \
  "$out/tb/nano_minimal.b" > "$out/tb/nano_minimal.lst"
python3 -c "import sys; [print('%02X' % b) for b in open(sys.argv[1], 'rb').read()]" \
  "$out/tb/nano_minimal.b" > "$out/tb/nano_minimal.hex"

status=0
for p in 00000110 00000111; do
  (cd "$out/tb" && $G -r $opts tb_usb -gg_product_low=$p --stop-time=3ms > "run_$p.log" 2>&1) || true
  grep -E "PHY:|BFM:|PASS|FAIL|error" "$out/tb/run_$p.log" | sed 's/^.*(report note)://'
  grep -q PASS "$out/tb/run_$p.log" || status=1
done

iverilog -g2012 -o "$out/tb_usb_phy" "$here/tb_usb_phy.v" "$design/usb_phy.v" && vvp -n "$out/tb_usb_phy" | tee "$out/tb_usb_phy.log"
grep -q PASS "$out/tb_usb_phy.log" || status=1
exit $status
