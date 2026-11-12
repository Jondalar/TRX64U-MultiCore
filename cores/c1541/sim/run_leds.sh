#!/bin/sh
# T159: the 1541 cores' LED outputs. Usage: sh cores/c1541/sim/run_leds.sh <c128|c264|vc20>
# 1. drive_leds_map in iverilog over all input combinations (tb_drive_leds_map.v)
# 2. check_wiring.py: the core's layer 2 routes the drive's leds_n through the map and nothing else drives drive_leds_n; the host top it runs in reads [0]/[1] as the contract says
set -e
core=$1
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../.." && pwd)
out="$here/build/$core"; mkdir -p "$out"
map="$root/cores/c1541/drive_leds_map.v"
w="$root/cores/$core/layer2_$core.v"
case "$core" in
  c128) top="$root/cores/c128/mono/top.v" ;;
  c264) top="$root/cores/c264/mono/host/top.v" ;;
  *)    top="$root/host/top.v" ;;
esac
tb() { # name map.v
  iverilog -g2012 -o "$out/$1.vvp" -s tb_drive_leds_map "$here/tb_drive_leds_map.v" "$2"
  nice -n 15 vvp -n "$out/$1.vvp" > "$out/$1.log"
}
chk() { python3 "$here/check_wiring.py" "$1" "$top"; }
echo "== map"; tb map "$map"; grep -E "^(PASS|FAIL)" "$out/map.log"; grep -q "^PASS" "$out/map.log"
echo "== wiring $core"; chk "$w"
echo "PASS leds $core (map passes, wiring passes)"
