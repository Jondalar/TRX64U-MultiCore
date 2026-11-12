#!/bin/sh
# T92 D2: copy the parts of MiSTer-devel/Amstrad_MiSTer (https://github.com/MiSTer-devel/Amstrad_MiSTer) that the CPC core uses into
# cores/cpc/rtl, unchanged, from the commit the spec was written against (docs/specs/T92 header, 6c2c39b of 2026-06-03): the Gate Array
# (rtl/GA40010: ga40010, video, the clocked casgen_sync and syncgen_sync - the variants the Quartus project builds - and rslatch), the CRTC
# (UM6845R), the PPI (i8255), the MMU (Amstrad_MMU), the ink table (color_mix) and the FDC (u765/u765.sv). The Z80 and the AY are the
# Spectrum core's (cores/zx/rtl, T91 D1: shared, never copied). Out: Amstrad.sv (the board top), Amstrad_motherboard.v (our cores/cpc/trx/
# cpc_board.v is written from it), hid.sv and the mice, crt_filter, sdram, tzxplayer, progressbar, playcity, dandanator, sys/, every ROM.
# Usage: tools/vendor_amstrad_mister.sh <Amstrad_MiSTer checkout>      (read only; nothing in it is run)
set -e
src=${1:?Amstrad_MiSTer checkout}
here=$(cd "$(dirname "$0")/.." && pwd)
dst=$here/cores/cpc/rtl
pin=6c2c39b
have=$(git -C "$src" rev-parse --short=7 HEAD)
[ "$have" = "$pin" ] || { echo "checkout is at $have, the spec read $pin"; exit 1; }
[ -z "$(git -C "$src" status --porcelain)" ] || { echo "checkout is not clean"; exit 1; }
files="GA40010/ga40010.sv GA40010/video.sv GA40010/casgen_sync.v GA40010/syncgen_sync.v GA40010/rslatch.v UM6845R.v i8255.v Amstrad_MMU.v color_mix.sv u765/u765.sv"
mkdir -p "$dst"
find "$dst" -type f ! -name NOTICE ! -name COPYING -delete
for f in $files; do
  [ -f "$src/rtl/$f" ] || { echo "missing in the checkout: rtl/$f"; exit 1; }
  mkdir -p "$dst/$(dirname "$f")"
  cp "$src/rtl/$f" "$dst/$f"
done
(cd "$dst" && find . -type f ! -name NOTICE ! -name COPYING ! -name SHA256SUMS | sort | xargs shasum -a 256) > "$dst/SHA256SUMS"
echo "vendored $(echo $files | wc -w | tr -d ' ') files from $pin into cores/cpc/rtl, SHA256SUMS written (NOTICE and COPYING are ours: keep them)"
