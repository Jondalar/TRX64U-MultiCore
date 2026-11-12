#!/bin/sh
# T159 S4c: the wiring from amiga_core's drv_led to the case LED and the bling lamp (see tb_motorwire.v). Built by gen.py from the shipped files.
set -e
here=$(cd "$(dirname "$0")" && pwd)
root=$here/../../../..
out=$here/build; mkdir -p "$out"
run() {  # name, mutation
  mkdir -p "$out/$1"
  python3 -I "$here/gen.py" "$root" "$out/$1" $2
  iverilog -g2012 -o "$out/$1/tb" "$here/tb_motorwire.v" "$out/$1/wire_top.v" "$out/$1/layer2_patched.v" "$out/$1/amiga_core_stub.v" \
    "$root/cores/amiga1200/mono/host/bling_io.v" "$root/cores/amiga1200/mono/host/case_leds.v" > "$out/$1/iverilog.log" 2>&1 || { grep -i error "$out/$1/iverilog.log" | head; return 1; }
  vvp -n "$out/$1/tb" > "$out/$1/log"
}
run base
cat "$out/base/log"
grep -q '^PASS tb_motorwire errors=0' "$out/base/log"
echo "PASS amiga1200_motorwire (drv_led -> drive_leds_n[0] -> case LED 1 + bling drive_act, drv_hd -> drive_leds_n[1] -> bling hd_act)"
