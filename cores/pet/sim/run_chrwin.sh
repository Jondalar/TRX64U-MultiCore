#!/bin/sh
# T216 §5: the app's window reads and writes all 4 KiB of the character ROM (rom_sel 2).
set -e
cd "$(dirname "$0")/.."
iverilog -g2012 -o /tmp/tb_pet_chrwin.vvp sim/tb_pet_chrwin.v rtl/trx/pet_win.v rtl/trx/pet_mem.v
out=$(timeout 300 vvp /tmp/tb_pet_chrwin.vvp)
echo "$out" | tail -8
echo "$out" | grep -q '^PASS tb_pet_chrwin'
