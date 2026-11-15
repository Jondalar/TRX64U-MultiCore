#!/bin/sh
# T216 S7: the SuperPET's six Waterloo ROMs (sim/media, Gitea only) run the 6809 to its menu: banner on row 0, 80 columns, CRTC R12 bit 5 (the Waterloo character set).
set -e
cd "$(dirname "$0")/.."
iverilog -g2012 -I sim -o /tmp/tb_pet_wl.vvp sim/tb_pet_wl.v rtl/trx/pet_core.v rtl/trx/pet_regs.v rtl/trx/pet_win.v rtl/trx/pet_mem.v rtl/trx/pet_vid.v \
  rtl/trx/pet_pixw.v sim/pet_ieee_stub.v rtl/trx/pet_keys.v rtl/trx/pet_padkeys.v rtl/trx/pet_audio.v rtl/trx/pet_tape.v rtl/src/cpu6502.v rtl/src/pia6520.v rtl/src/via6522.v rtl/src/pet2001io.v \
  rtl/trx/pet_spet.v rtl/src/mc6809/mc6809is.v
out=$(timeout 3000 vvp /tmp/tb_pet_wl.vvp +ms=${WL_MS:-250} +media=${WL_MEDIA:-sim/media})
echo "$out" | head -14
echo "$out" | grep -q '^PASS tb_pet_wl'
