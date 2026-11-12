#!/bin/sh
# T164 WP13 / T152 S8: the ESP32 boot control of the host (host/esp_ctl.v, from TRX64-Ultimate t26-esp-ram): pins high-Z until armed, uart_dma boot/enable
# reach the pins as the app's modes OFF 3 / BOOT 1 / RUN 0 demand, the ESP32 model enters the ROM loader only when BOOT is high as DIS falls, polarity bits, a
# reset frees the pins within one clock, status/ID bytes. Then the wiring: top.v tri-states the pins on oe and feeds uart_dma's bits, ultimate_sys.vhd decodes
# 0x060910-0x06091F and takes boot/enable from uart_dma, pins.xdc has L13 / M13.
set -e
here=$(cd "$(dirname "$0")" && pwd)
root=$here/../../..
out=$here/build; mkdir -p "$out"
f=$root/host/esp_ctl.v
iverilog -g2012 -Wall -o "$out/tb_esp_ctl" "$here/tb_esp_ctl.v" "$f"
vvp -n "$out/tb_esp_ctl" | tee "$out/esp_ctl.log"
grep -q '^PASS tb_esp_ctl errors=0' "$out/esp_ctl.log"
# the wiring in top.v, ultimate_sys.vhd and pins.xdc
t=$root/host/top.v; u=$root/host/ultimate_sys.vhd; x=$root/host/pins.xdc
wiring() { # top ultimate_sys xdc
  grep -q 'assign WIFI_BOOT = esp_oe ? esp_boot_o : 1.bz;' "$1" && grep -q 'assign WIFI_DIS  = esp_oe ? esp_dis_o  : 1.bz;' "$1" &&
  grep -q '\.boot_i(wifi_boot_u), \.enable_i(wifi_enable_u)' "$1" && grep -q '\.wifi_boot(wifi_boot_u), \.wifi_enable(wifi_enable_u)' "$1" &&
  grep -q '\.esp_re(esp_re)' "$1" && grep -q 'output wire       WIFI_BOOT, WIFI_DIS' "$1" &&
  grep -q 'boot        => wifi_boot,' "$2" && grep -q 'enable      => wifi_enable,' "$2" &&
  grep -q 'a(23 downto 4) = X"06091" then' "$2" && grep -q 'region <= r_esp;' "$2" && grep -q 'fixed_data <= esp_rdata;' "$2" &&
  grep -q 'PACKAGE_PIN L13 \[get_ports WIFI_BOOT\]' "$3" && grep -q 'PACKAGE_PIN M13 \[get_ports WIFI_DIS\]' "$3"
}
wiring "$t" "$u" "$x" || { echo "FAIL: the host does not wire esp_ctl"; exit 1; }
echo "PASS host_esp_ctl (high-Z until armed, modes OFF/BOOT/RUN, ROM-loader sequence, polarity, reset frees the pins, ID)"
