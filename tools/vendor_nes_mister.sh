#!/bin/sh
# T95 D12: copy the parts of MiSTer-devel/NES_MiSTer (https://github.com/MiSTer-devel/NES_MiSTer, GPL v3) that the NES core
# uses into cores/nes/rtl, unchanged, from the commit the spec was written against (docs/specs/T95-nes-core.md 5.6). The
# list is the machine (nes.v, ppu.sv, apu.sv, cart.sv), the 13 mapper files, the VHDL 6502 (t65/) and the constants package
# the savestate registers name. Out: the MiSTer framework and board parts - sdram.sv, ddram.sv, pll*, composite_*, video.sv,
# miracle.sv, keyboard.sv, zapper.sv, dsp.v, cheatcodes.sv, EEPROM_24C0x.sv (only mapper 16 uses it), the savestate framework
# (savestates.vhd, statemanager.vhd, bus_savestates.vhd, regs_savestates.sv is kept: it is only a package of constants),
# savestate_ui.sv, the .mif/.chr/.bin/.asm data files, NES.sv (the board top; the iNES header decode of cores/nes/trx/
# nes_regs.v is derived from its GameLoader, see NOTICE), sys/, Palette/, releases/.
# Usage: tools/vendor_nes_mister.sh <NES_MiSTer checkout>      (read only; nothing in it is run)
set -e
src=${1:?NES_MiSTer checkout}
here=$(cd "$(dirname "$0")/.." && pwd)
dst=$here/cores/nes/rtl
pin=49a0a66
have=$(git -C "$src" rev-parse --short=7 HEAD)
[ "$have" = "$pin" ] || { echo "checkout is at $have, the spec read $pin"; exit 1; }
[ -z "$(git -C "$src" status --porcelain)" ] || { echo "checkout is not clean"; exit 1; }
files="nes.v ppu.sv apu.sv cart.sv regs_savestates.sv
  mappers/FDS.sv mappers/JYCompany.sv mappers/MMC1.sv mappers/MMC2.sv mappers/MMC3.sv mappers/MMC5.sv mappers/Mapper99.sv
  mappers/Namco.sv mappers/Sachen.sv mappers/Sunsoft.sv mappers/VRC.sv mappers/generic.sv mappers/misc.sv
  t65/T65.vhd t65/T65_ALU.vhd t65/T65_MCode.vhd t65/T65_Pack.vhd"
mkdir -p "$dst"
find "$dst" -type f ! -name NOTICE -delete
for f in $files; do
  [ -f "$src/rtl/$f" ] || { echo "missing in the checkout: rtl/$f"; exit 1; }
  mkdir -p "$dst/$(dirname "$f")"
  cp "$src/rtl/$f" "$dst/$f"
done
cp "$src/COPYING" "$dst/COPYING"
(cd "$dst" && find . -type f ! -name NOTICE ! -name SHA256SUMS | sort | xargs shasum -a 256) > "$dst/SHA256SUMS"
echo "vendored $(echo $files | wc -w | tr -d ' ') files from $pin into cores/nes/rtl, SHA256SUMS written (NOTICE is ours: keep it)"
echo "next: python3 tools/nes_gen.py makes cores/nes/gen; it refuses an rtl/ that SHA256SUMS does not describe"
