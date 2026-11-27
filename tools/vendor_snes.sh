#!/bin/sh
# T142: copy the parts of MiSTer-devel/SNES_MiSTer (GPL v3, srg320's SNES) that the core uses into cores/snes/rtl, unchanged, from the
# pinned commit. In: the machine (SNES, CPU/65C816, PPU, SMP/SPC700, DSP, SWRAM, CEGen) and the cartridge-side blocks of the plain cartridge
# mapper (DSP_LHRomMap with S-RTC, OBC-1 and the DSP-n core DSPn.vhd). Out: the MiSTer shell (SNES.sv, main.v, sdram/ddram, hps_ext, uart, savestates,
# cheatcodes, lightgun, sni, pll), the Altera megafunction wrappers (bram.vhd, mlab.vhd, cx4cache.vhd, SA1MULT.vhd, SPC7110_FIFO.vhd,
# pll*: tool output for Altera devices, written again in cores/snes/ip/snes_ip.vhd), the coprocessors SA-1, SuperFX, CX4, SPC7110, SDD-1, BSX,
# Sufami, MSU-1 (T142 section 4), and every .mif (dumps of chip ROMs: DSP-1B, CX4 data, BSX; never in this repository).
# Usage: tools/vendor_snes.sh <SNES_MiSTer checkout>      (read only; nothing in it is run)
set -e
src=${1:?SNES_MiSTer checkout}
here=$(cd "$(dirname "$0")/.." && pwd)
dst=$here/cores/snes/rtl
pin=bdcd33c5
have=$(git -C "$src" rev-parse --short=8 HEAD)
[ "$have" = "$pin" ] || { echo "checkout is at $have, the spec read $pin"; exit 1; }
[ -z "$(git -C "$src" status --porcelain)" ] || { echo "checkout is not clean"; exit 1; }
mkdir -p "$dst"
find "$dst" -type f ! -name NOTICE -delete
n=0
copy() {
  [ -f "$src/rtl/$1" ] || { echo "missing in the checkout: rtl/$1"; exit 1; }
  mkdir -p "$dst/$(dirname "$1")"
  cp "$src/rtl/$1" "$dst/$1"
  n=$((n + 1))
}
for f in CEGen CPU DSP DSP_PKG PPU PPU_PKG SMP SNES SWRAM; do copy $f.vhd; done
for f in ALU AddSubBCD AddrGen BCDAdder MCode P65816_pkg P65C816; do copy 65C816/$f.vhd; done
for f in AddSub ALU AddrGen BCDAdj MCode MulDiv SPC700 SPC700_pkg; do copy SPC700/$f.vhd; done
for f in chip/RTC4513 chip/SRTC chip/DSP/DSP_LHRomMap chip/DSP/DSPn chip/DSP/OBC1; do copy $f.vhd; done
# The S-SMP's 64 byte boot ROM (SMP.vhd, IPLROM) is a dump of Nintendo's chip ROM. The rule of this repository (no ROM in a source tree, a .cor or a package)
# applies to this tree (T142 D9): it is cut out here. Alex allowed the bytes in the application (Q3, 2026-10-29), which writes its spc_ipl table
# (cores/snes/app/lib/snes.cc) into a small RAM at the start (tools/snes_gen.py makes the RAM and its port). Nothing else is changed.
python3 - "$dst/SMP.vhd" <<'EOF'
import re, sys
p = sys.argv[1]
t = open(p).read()
pat = re.compile(r'constant\s+IPLROM: IplRom_t := \((?:\s*x"[0-9a-fA-F]{2}",?){64}\s*\);')
t, n = pat.subn('constant  IPLROM: IplRom_t := (others => x"00");   -- the 64 byte boot ROM is not in this repository: tools/vendor_snes.sh cut it out (T142 D9)', t)
if n != 1:
    raise SystemExit("SMP.vhd: the 64 byte IPLROM constant was not found exactly once")
open(p, "w").write(t)
EOF
cp "$src/LICENSE" "$dst/COPYING"
(cd "$dst" && find . -type f ! -name NOTICE ! -name SHA256SUMS | sort | xargs shasum -a 256) > "$dst/SHA256SUMS"
echo "vendored $n files from $pin into cores/snes/rtl, SHA256SUMS written (NOTICE is ours: keep it)"
