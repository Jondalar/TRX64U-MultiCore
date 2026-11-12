#!/bin/sh
# T91 D2: copy the parts of MiSTer-devel/ZX-Spectrum_MiSTer (https://github.com/MiSTer-devel/ZX-Spectrum_MiSTer) that the ZX
# Spectrum core uses into cores/zx/rtl, unchanged but for tools/zx_ula.patch (T119 5.15: applied to ula.sv after the copy), from the commit the spec was written against (docs/specs/T91 header,
# 33774ad of 2026-10-12): the ULA (rtl/ula.sv, GPL v3), the AY (rtl/ym2149.sv, BSD wording) and the Z80 (rtl/T80, BSD wording;
# only the six files T80pa needs). Out: everything else of the repository, above all rtl/keyboard.sv (non-commercial clause),
# tape.sv, tzxplayer, snap_loader, turbosound, the disk, SDRAM and sound extras, sys/ and every ROM.
# Usage: tools/vendor_zx_mister.sh <ZX-Spectrum_MiSTer checkout>      (read only; nothing in it is run)
set -e
src=${1:?ZX-Spectrum_MiSTer checkout}
here=$(cd "$(dirname "$0")/.." && pwd)
dst=$here/cores/zx/rtl
pin=33774ad
have=$(git -C "$src" rev-parse --short=7 HEAD)
[ "$have" = "$pin" ] || { echo "checkout is at $have, the spec read $pin"; exit 1; }
[ -z "$(git -C "$src" status --porcelain)" ] || { echo "checkout is not clean"; exit 1; }
files="ula.sv ym2149.sv T80/T80.vhd T80/T80_ALU.vhd T80/T80_MCode.vhd T80/T80_Pack.vhd T80/T80_Reg.vhd T80/T80pa.vhd"
mkdir -p "$dst"
find "$dst" -type f ! -name NOTICE ! -name COPYING -delete
for f in $files; do
  [ -f "$src/rtl/$f" ] || { echo "missing in the checkout: rtl/$f"; exit 1; }
  mkdir -p "$dst/$(dirname "$f")"
  cp "$src/rtl/$f" "$dst/$f"
done
patch -s -p1 -d "$here" < "$here/tools/zx_ula.patch"
(cd "$dst" && find . -type f ! -name NOTICE ! -name COPYING ! -name SHA256SUMS | sort | xargs shasum -a 256) > "$dst/SHA256SUMS"
echo "vendored $(echo $files | wc -w | tr -d ' ') files from $pin into cores/zx/rtl, ula.sv patched, SHA256SUMS written (of the patched ula.sv; NOTICE and COPYING are ours: keep them)"
