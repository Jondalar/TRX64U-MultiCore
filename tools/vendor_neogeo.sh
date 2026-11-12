#!/bin/sh
# T143 D1: copy the parts of MiSTer's NeoGeo core (https://github.com/MiSTer-devel/NeoGeo_MiSTer, furrtek's chip-level MVS/AES, GPL v2 or later in the
# furrtek files, FX68K GPL v3 or later, jt10 / jt49 GPL v3, T80 BSD-style) that the core uses into cores/neogeo/rtl, unchanged, from the commit
# the spec measured. The list is what the build reads: neogeo.sv (the upstream top, kept as the reference our trx/neo_core.v derives from; not
# built), the chip models (rtl/cells, rtl/io, rtl/video), the CPUs (rtl/cpu), jt10 and jt49 as their .qip files list them (hdl only; no manuals),
# (the memory modules mem/memcard.v and backup.v are replaced by cores/neogeo/trx/neo_memcard.v and neo_bk.v and not copied).
# Out: sys/ (MiSTer framework), rtl/pll, rtl/cd (Neo Geo CD, spec D4a), rtl/mem (dpram.v, ddram.sv, sdram.sv, sdram_mux.sv, memcard.v, backup.v: replaced by cores/neogeo/trx),
# the releases/ directory (bitstreams), the Quartus projects, DEV_NOTES.md and xml.md (read for the layout only, not copied) and every ROM list.
# No ROM, BIOS, game or romset file is copied.
# Usage: tools/vendor_neogeo.sh <NeoGeo_MiSTer checkout>      (read only; nothing in it is run)
set -e
src=${1:?NeoGeo_MiSTer checkout}
here=$(cd "$(dirname "$0")/.." && pwd)
dst=$here/cores/neogeo/rtl
pin=ffaab14
have=$(git -C "$src" rev-parse --short=7 HEAD)
[ "$have" = "$pin" ] || { echo "checkout is at $have, the spec read $pin"; exit 1; }
[ -z "$(git -C "$src" status --porcelain)" ] || { echo "checkout is not clean"; exit 1; }
mkdir -p "$dst"
find "$dst" -type f ! -name NOTICE -delete
n=0
copy() {
  [ -f "$src/$1" ] || { echo "missing in the checkout: $1"; exit 1; }
  o=${1#rtl/}
  mkdir -p "$dst/$(dirname "$o")"
  cp "$src/$1" "$dst/$o"
  n=$((n + 1))
}
copy neogeo.sv
copy LICENSE
for d in cells io video cpu/T80 cpu/FX68K; do
  for f in "$src/rtl/$d"/*; do
    [ -f "$f" ] || continue
    case "$f" in *.qip|*.sdc|*.txt) continue;; esac
    copy "rtl/$d/$(basename "$f")"
  done
done
copy rtl/cpu/cpu_68k.v
copy rtl/cpu/cpu_z80.v
copy rtl/cpu/FX68K/fx68k.txt
for f in $(sed -n 's|.*\.\./\.\./hdl/\([^ ]*\) \].*|\1|p' "$src/rtl/jt12/target/quartus/jt10.qip"); do copy "rtl/jt12/hdl/$f"; done
for f in $(sed -n 's|.*\.\./\.\./hdl/\([^ ]*\) \].*|\1|p' "$src/rtl/jt49/syn/quartus/jt49.qip"); do copy "rtl/jt49/hdl/$f"; done
copy rtl/jt12/LICENSE
copy rtl/jt49/LICENSE
(cd "$dst" && find . -type f ! -name NOTICE ! -name SHA256SUMS | sort | xargs shasum -a 256) > "$dst/SHA256SUMS"
echo "vendored $n files from $pin into cores/neogeo/rtl, SHA256SUMS written (NOTICE is ours: keep it)"
