#!/bin/sh
# T134 S10: cassette 1 and the CB2 sound (tb_pet_tape.v, iverilog).
set -e
d="$(cd "$(dirname "$0")" && pwd)"
cd "$d/.."
iverilog -g2012 -o /tmp/tb_pet_tape.vvp sim/tb_pet_tape.v sim/ddr2c_model.v rtl/trx/pet_core.v rtl/trx/pet_regs.v rtl/trx/pet_win.v rtl/trx/pet_mem.v rtl/trx/pet_vid.v \
  rtl/trx/pet_pixw.v sim/pet_ieee_stub.v rtl/trx/pet_keys.v rtl/trx/pet_audio.v rtl/trx/pet_tape.v rtl/src/cpu6502.v rtl/src/pia6520.v rtl/src/via6522.v rtl/src/pet2001io.v rtl/trx/pet_spet.v rtl/src/mc6809/mc6809is.v
out=$(timeout 1200 vvp /tmp/tb_pet_tape.vvp)
echo "$out" | tail -12
echo "$out" | grep -q '^PASS tb_pet_tape'
