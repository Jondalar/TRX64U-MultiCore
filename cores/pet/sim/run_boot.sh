#!/bin/sh
# T134 S2/S3: boots the hand-written test ROM of tb_pet_boot.v in the PET machine (iverilog).
set -e
d="$(cd "$(dirname "$0")" && pwd)"
cd "$d/.."
iverilog -g2012 -o /tmp/tb_pet_boot.vvp sim/tb_pet_boot.v rtl/trx/pet_core.v rtl/trx/pet_regs.v rtl/trx/pet_win.v rtl/trx/pet_mem.v rtl/trx/pet_vid.v \
  rtl/trx/pet_pixw.v sim/pet_ieee_stub.v rtl/trx/pet_keys.v rtl/trx/pet_padkeys.v rtl/trx/pet_audio.v rtl/trx/pet_tape.v rtl/src/cpu6502.v rtl/src/pia6520.v rtl/src/via6522.v rtl/src/pet2001io.v rtl/trx/pet_spet.v rtl/src/mc6809/mc6809is.v
out=$(timeout 1200 vvp /tmp/tb_pet_boot.vvp)
echo "$out" | tail -8
echo "$out" | grep -q '^PASS tb_pet_boot'

sh "$d/run_models.sh"

sh "$d/run_chrwin.sh"
