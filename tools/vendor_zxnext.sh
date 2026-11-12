#!/bin/sh
# T116 D1/Q9: copy the parts of the ZX Spectrum Next FPGA source (https://gitlab.com/SpectrumNext/ZX_Spectrum_Next_FPGA, GPL v3 or later,
# cores/zxnext/src) that the core uses into cores/zxnext/rtl, unchanged, from the commit the spec measured (docs/specs/T116-zx-next-core.md
# section 5). The list is what the measurement read: zxnext.vhd and the directories audio, cpu, device, rom, serial, misc, video (the files
# directly in video: layer2, lores, sprites, tilemap, zxula, zxula_timing; not video/hdmi, video/vga). Out: the board tops and pin files
# (zxnext_top_issue*.vhd, *.xdc, *.ucf), pll/, input/ (PS/2 keyboard and mouse blocks, replaced by the key translator, T116 section 4), the
# flash-boot blocks (flashboot*.vhd, anti-brick, section 4), uart_old.vhd, and ram/ (dpram, dpram2, sdpram, tdpram: written again by
# cores/zxnext/ip/zxn_ip.vhd because the converter cannot read them; the five Xilinx IP cores were never in the source tree).
# No ROM and no NextZXOS content is copied. rom/bootrom.vhd is the original's own 8 KB boot ROM (machine generated VHDL data, part of the
# GPL v3 source tree; cores/zxnext/rtl/NOTICE says what it is and what it is not).
# Usage: tools/vendor_zxnext.sh <ZX_Spectrum_Next_FPGA checkout>      (read only; nothing in it is run)
set -e
src=${1:?ZX_Spectrum_Next_FPGA checkout}
here=$(cd "$(dirname "$0")/.." && pwd)
dst=$here/cores/zxnext/rtl
pin=b047011
have=$(git -C "$src" rev-parse --short=7 HEAD)
[ "$have" = "$pin" ] || { echo "checkout is at $have, the spec read $pin"; exit 1; }
[ -z "$(git -C "$src" status --porcelain)" ] || { echo "checkout is not clean"; exit 1; }
base=$src/cores/zxnext/src
mkdir -p "$dst"
find "$dst" -type f ! -name NOTICE -delete
n=0
copy() {
  [ -f "$base/$1" ] || { echo "missing in the checkout: $1"; exit 1; }
  mkdir -p "$dst/$(dirname "$1")"
  cp "$base/$1" "$dst/$1"
  n=$((n + 1))
}
copy zxnext.vhd
for d in audio audio/i2s cpu device rom serial misc video; do
  for f in "$base/$d"/*.vhd; do
    b=$(basename "$f" .vhd)
    case "$b" in flashboot|flashboot_256|flashboot_old|uart_old) continue;; esac
    copy "$d/$b.vhd"
  done
done
cp "$src/cores/zxnext/COPYING" "$dst/COPYING"
(cd "$dst" && find . -type f ! -name NOTICE ! -name SHA256SUMS | sort | xargs shasum -a 256) > "$dst/SHA256SUMS"
echo "vendored $n files from $pin into cores/zxnext/rtl, SHA256SUMS written (NOTICE is ours: keep it)"
echo "next: python3 tools/zxnext_gen.py makes cores/zxnext/gen; it refuses an rtl/ that SHA256SUMS does not describe"
