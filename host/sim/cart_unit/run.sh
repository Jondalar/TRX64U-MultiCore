#!/bin/sh
# Cartridge unit bench (tb_cart_unit.v) under iverilog: cart_slot_c64ref (the C64 reference core's cartridge slot) with the
# descriptor-driven model (the app's descriptors, tools/cart_descriptors), cart_fill, trx_io. One process, niced.
set -e
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../.." && pwd)
C=$root/vendor/icepi
D=$root/host
out=$here/build
mkdir -p "$out"
python3 "$root/tools/gen_cart_descriptors.py" --verilog "$out/desc_app.vh"
nice -n 15 iverilog -g2012 -I "$out" -o "$out/tb_cart_unit.vvp" -s tb_cart_unit "$here/tb_cart_unit.v" \
  $D/../cores/c64ref/cart_slot_c64ref.v $D/cart_fill.v $D/../cores/common/trx_io.v \
  $C/c64_cartridge.v $C/c64_flash_amd.v $C/c64_eeprom_93c86.v $C/c64_action_replay/c64_action_replay_pla.v
nice -n 15 vvp -n "$out/tb_cart_unit.vvp"
