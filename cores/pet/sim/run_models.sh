#!/bin/sh
# T134 S3/S6: the 8032 80 column picture + CRTC start address + business keys (mode 0), the 8096 bank switching (mode 1), the 8 KiB RAM size (mode 2).
set -e
cd "$(dirname "$0")/.."
iverilog -g2012 -o /tmp/tb_pet_models.vvp sim/tb_pet_models.v rtl/trx/pet_core.v rtl/trx/pet_regs.v rtl/trx/pet_win.v rtl/trx/pet_mem.v rtl/trx/pet_vid.v \
  rtl/trx/pet_pixw.v sim/pet_ieee_stub.v rtl/trx/pet_keys.v rtl/trx/pet_audio.v rtl/trx/pet_tape.v rtl/src/cpu6502.v rtl/src/pia6520.v rtl/src/via6522.v rtl/src/pet2001io.v rtl/trx/pet_spet.v rtl/src/mc6809/mc6809is.v
for m in 0 1 2; do
  out=$(timeout 1200 vvp /tmp/tb_pet_models.vvp +mode=$m)
  echo "$out" | tail -8
  echo "$out" | grep -q "^PASS tb_pet_models mode $m"
done
