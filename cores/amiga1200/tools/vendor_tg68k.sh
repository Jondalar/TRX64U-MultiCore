#!/bin/sh
# T97 step 2: copy the TG68K 68020 kernel from a NanoMig checkout into cores/amiga1200 (the Amiga 1200
# core's own tree - cores/amiga is read by the OCS core and must not change).
# Usage: cores/amiga1200/tools/vendor_tg68k.sh <NanoMig checkout>     (taken from commit a19604a, see rtl/NOTICE)
#
# rtl/tg68k/*.vhd  the kernel and its two packages. ONE mechanical change: the kernel's entity is renamed
#                  TG68KdotC_Kernel -> TG68KdotC_Kernel_core, because rtl/tg68k_020.v takes the old name (the
#                  wrapper instantiates that name) and forces the CPU to 68020. TG68K.vhd (the 68000 bus top)
#                  is not taken: nothing instantiates it.
# sim/tg68k/       sim/TG68KdotC_Kernel.v of NanoMig, byte for byte: the tool-generated Verilog translation of
#                  the same kernel that Verilator needs (no VHDL there). Simulation only, never read by Vivado.
set -e
src=${1:?NanoMig checkout}
here=$(cd "$(dirname "$0")/.." && pwd)
for f in TG68K_ALU.vhd TG68K_Pack.vhd TG68KdotC_Kernel.vhd; do [ -f "$src/src/tg68k/$f" ] || { echo "missing $src/src/tg68k/$f"; exit 1; }; done
cp "$src/src/tg68k/TG68K_ALU.vhd" "$src/src/tg68k/TG68K_Pack.vhd" "$here/rtl/tg68k/"
perl -pe 's/\bTG68KdotC_Kernel\b/TG68KdotC_Kernel_core/g' "$src/src/tg68k/TG68KdotC_Kernel.vhd" > "$here/rtl/tg68k/TG68KdotC_Kernel.vhd"
cp "$src/sim/TG68KdotC_Kernel.v" "$here/sim/tg68k/TG68KdotC_Kernel.v"
echo "NanoMig $(git -C "$src" rev-parse --short HEAD 2>/dev/null)"
