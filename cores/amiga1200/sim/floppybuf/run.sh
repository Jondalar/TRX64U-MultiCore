#!/bin/sh
# T97 5.11: the A1200's block-RAM copy of paula_floppy (sector buffer) equals the shared file cycle for cycle, driven by a DMA read
# of two sectors and a DMA write of a valid MFM sector with sd card read-back (tb_floppybuf.v).
# Also: lane_lint.sh (structural rule for the sector-buffer arrays, 5.11).
# The synthesised netlist is checked by synth_check.sh (Vivado, by hand).
set -e
here=$(cd "$(dirname "$0")" && pwd)
root=$here/../../../..
out=$here/build; mkdir -p "$out"
perl -pe 's/^module paula_floppy\b/module paula_floppy_ref/' "$root/cores/amiga/rtl/src/minimig-aga/paula_floppy.v" > "$out/pf_ref.v"
fifo=$root/cores/amiga1200/rtl/paula_floppy_fifo.v
copy=$root/cores/amiga1200/rtl/paula_floppy.v
sh "$here/lane_lint.sh" "$copy"
iverilog -g2012 -o "$out/tb" "$here/tb_floppybuf.v" "$out/pf_ref.v" "$copy" "$fifo"
vvp -n "$out/tb" | grep -v '^paula_floppy.v:' | tee "$out/floppybuf.log"
grep -q '^PASS tb_floppybuf' "$out/floppybuf.log"
echo "PASS amiga1200_floppybuf (copy = shared paula_floppy)"
