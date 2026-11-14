#!/bin/sh
# T88: copy the parts of Mark Watson's Atari800 core (https://github.com/MiSTer-devel/Atari800_MiSTer) that the
# Atari 800 XL core uses into cores/atari800/rtl, unchanged. Only the files of the list below are taken, only from
# the commit the spec was written against, and only when every file still has the SHA-256 it had when it was read
# (docs/specs/T88-atari800-core.md D1/D2): the licence reading of that section is of THESE bytes, and Watson's terms
# are non-commercial (see cores/atari800/rtl/NOTICE).
# Usage: tools/vendor_atari800.sh <Atari800_MiSTer checkout>
set -e
src=${1:?Atari800_MiSTer checkout}
here=$(cd "$(dirname "$0")/.." && pwd)
dst=$here/cores/atari800/rtl
pin=1440e41

have=$(git -C "$src" rev-parse --short=7 HEAD)
[ "$have" = "$pin" ] || { echo "checkout is at $have, the spec read $pin"; exit 1; }
[ -z "$(git -C "$src" status --porcelain)" ] || { echo "checkout is not clean"; exit 1; }

# a8core: the machine. Out by decision: cpu.vhd and cpu_65xx.vhd (ours, D1/§5), vbxe* (Q7), pokeymax (Q8),
# ultime.vhdl, ps2_to_atari* (D6: our matrix), the 5200 files, internalromram.vhd (ROMs come from the app, D4).
a8core="address_decoder.vhdl antic.vhdl antic_counter.vhdl antic_dma_clock.vhdl atari800core.vhd
  atari800core_simple_sdram.vhd cart_logic.vhd covox.vhd enable_divider.vhdl freezer_logic.vhd gtia.vhdl
  gtia_palette.vhdl gtia_player.vhdl gtia_priority.vhdl irq_glue.vhdl pbi_rom.vhdl pia.vhdl pokey.vhdl
  pokey_countdown_timer.vhdl pokey_keyboard_scanner.vhdl pokey_mixer_simple.vhdl pokey_noise_filter.vhdl
  pokey_poly_17_9.vhdl pokey_poly_4.vhdl pokey_poly_5.vhdl pot_from_signed.vhdl reg_file.vhdl shared_enable.vhdl
  simple_counter.vhdl wide_delay_line.vhdl"
# components: the generic blocks those files instantiate. Out: large_bram.vhdl (VBXE), ps2_keyboard.vhdl (D6),
# sdram_statemachine.vhdl (the host's DDR2 path instead, D4).
components="complete_address_decoder.vhdl delay_line.vhdl generic_ram_infer.vhdl latch_delay_line.vhdl
  mult_infer.vhdl simple_low_pass_filter.vhdl synchronizer.vhdl syncreset_enable_divider.vhd"
# sioemu: the disk side with its FIFOs (D1). Out: tape_handler.vhdl and fifo_tape.vhd unless Q6 asks for tape.
sioemu="fifo_receive.vhd fifo_transmit.vhd sio_handler.vhdl"

mkdir -p "$dst/a8core" "$dst/components" "$dst/sioemu"
copy() { # copy <subdirectory> <file list>
  for f in $2; do
    [ -f "$src/rtl/common/$1/$f" ] || { echo "missing in the checkout: $1/$f"; exit 1; }
    cp "$src/rtl/common/$1/$f" "$dst/$1/$f"
  done
}
copy a8core "$a8core"
copy components "$components"
copy sioemu "$sioemu"
cp "$src/LICENSE" "$dst/LICENSE" 2>/dev/null || true
echo "vendored $(find "$dst" -name '*.vhd*' | wc -l | tr -d ' ') files from $pin into cores/atari800/rtl"
echo "next: tools/vendor_atari800.sh --hash writes the SHA-256 list the spec pins"
if [ "${2:-}" = --hash ]; then (cd "$dst" && find . -name '*.vhd*' | sort | xargs shasum -a 256) ; fi
