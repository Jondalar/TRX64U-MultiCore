#!/bin/sh
# T89 S12 (the mouse half): the AppleMouse card on its own (mouse/tb_mouse.v): GHDL turns applemouse.vhd + pia6821.vhd into a netlist, iverilog runs it with
# the Verilog 6805 (jt6805) and a stand-in MCU ROM of ours.
set -e
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../.." && pwd)
out="$here/build/mouse"; mkdir -p "$out"
m="$root/cores/apple2/rtl/mouse"
cp "$m/jt6805/6805.uc" "$out/"
gen() { # name applemouse.vhd
  d="$out/$1"; rm -rf "$d"; mkdir -p "$d"
  ( cd "$d" && ghdl -a --std=08 -fsynopsys -frelaxed -Wno-hide -Wno-binding "$m/pia6821.vhd" "$2" > ghdl_a.log 2>&1 \
      && ghdl --synth --std=08 -fsynopsys -frelaxed --out=verilog applemouse > am0.v 2> ghdl_s.log ) || { tail -n 20 "$d"/ghdl_*.log; exit 1; }
  python3 "$here/mouse/netfix.py" "$d/am0.v" "$d/am.v"
}
sim() { # name mcu-image echo
  d="$out/$1"
  python3 "$here/mouse/mk6805.py" "$2" "$d/mcu.hex"
  iverilog -g2012 -DMCU_HEX="\"$d/mcu.hex\"" -DECHO=$3 -I "$m/jt6805" -o "$d/t.vvp" "$here/mouse/tb_mouse.v" "$d/am.v" "$m"/jt6805/jt6805*.v "$m/jt6805/jtframe_6805mcu.v"
  ( cd "$out" && nice -n 15 vvp -n "$d/t.vvp" > "$d/run.log" 2>&1 || true )
}
gen base "$m/applemouse.vhd"
sim base echo 1; sed 's/^/  /' "$out/base/run.log"; grep -q "^PASS tb_mouse" "$out/base/run.log"
sim base count 0; sed 's/^/  /' "$out/base/run.log"; grep -q "^PASS tb_mouse" "$out/base/run.log"
echo "PASS mouse"
