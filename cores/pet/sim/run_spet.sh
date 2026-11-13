#!/bin/sh
# T134 Q13: the SuperPET board (6809, the $9000 bank window, the Waterloo ROM area, dongle, ACIA) with a synthetic program: mode 0 CPU switch 6809, mode 1 PROG (the 6502 hands over).
set -e
cd "$(dirname "$0")/.."
iverilog -g2012 -o /tmp/tb_pet_spet.vvp sim/tb_pet_spet.v rtl/trx/pet_core.v rtl/trx/pet_regs.v rtl/trx/pet_win.v rtl/trx/pet_mem.v rtl/trx/pet_vid.v \
  rtl/trx/pet_pixw.v sim/pet_ieee_stub.v rtl/trx/pet_keys.v rtl/trx/pet_audio.v rtl/trx/pet_tape.v rtl/src/cpu6502.v rtl/src/pia6520.v rtl/src/via6522.v rtl/src/pet2001io.v \
  rtl/trx/pet_spet.v rtl/src/mc6809/mc6809is.v
for m in 0 1; do
  out=$(timeout 1200 vvp /tmp/tb_pet_spet.vvp +mode=$m)
  echo "$out" | tail -8
  echo "$out" | grep -q "^PASS tb_pet_spet mode $m"
done
