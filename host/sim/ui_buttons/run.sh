#!/bin/sh
# T195 S1/S2: the debounce bench (host/sim/tb_ui_buttons.v through ci/sim_iv.sh), then the wiring: top.v feeds UI_BUTTON through ui_buttons into ultimate_sys,
# ultimate_sys drives ITU buttons 5/6/7 (left+middle = Menu on bit 6, rocker Freeze bit 5, right = bit 7) and holds the C64 reset for as long as the right
# button is down, pins.xdc has P17 / R18 / N18 with LVCMOS33 and the pull-up.
set -e
here=$(cd "$(dirname "$0")" && pwd)
root=$here/../../..
"$root/ci/sim_iv.sh" tb_ui_buttons.v | tee /dev/stderr | grep -q '^PASS tb_ui_buttons errors=0'
t=$root/host/top.v; u=$root/host/ultimate_sys.vhd; x=$root/host/pins.xdc
wiring() { # top ultimate_sys xdc
  grep -q 'ui_buttons ubtn (.clk(cpu_clk), .tick_1ms(drv_t1k), .pin_n(UI_BUTTON), .btn(ui_btn));' "$1" && grep -q '\.ui_btn(ui_btn)' "$1" &&
  grep -q 'input  wire \[2:0\] UI_BUTTON' "$1" &&
  grep -q "buttons(0) <= '1' when btn_freeze /= 0 else '0';" "$2" &&
  grep -q "buttons(1) <= '1' when btn_menu /= 0 or ui_btn(0) = '1' or ui_btn(1) = '1' else '0';" "$2" &&
  grep -q 'buttons(2) <= ui_btn(2);' "$2" &&
  grep -q "or esp_reset /= 0 or ui_btn(2) = '1' else '1';" "$2" &&
  grep -q 'PACKAGE_PIN P17 \[get_ports {UI_BUTTON\[0\]}\]' "$3" && grep -q 'PACKAGE_PIN R18 \[get_ports {UI_BUTTON\[1\]}\]' "$3" &&
  grep -q 'PACKAGE_PIN N18 \[get_ports {UI_BUTTON\[2\]}\]' "$3" && grep -q 'IOSTANDARD LVCMOS33 \[get_ports {UI_BUTTON\[\*\]}\]' "$3" &&
  grep -q 'PULLTYPE PULLUP \[get_ports {UI_BUTTON\[\*\]}\]' "$3"
}
wiring "$t" "$u" "$x" || { echo "FAIL: the host does not wire the board buttons"; exit 1; }
echo "PASS host_ui_buttons (debounce 20 ms both ways, wiring top.v / ultimate_sys.vhd / pins.xdc)"
