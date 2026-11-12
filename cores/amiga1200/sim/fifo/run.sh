#!/bin/sh
# T97 5.8: the A1200's block-RAM copy of paula_floppy_fifo equals the shared file cycle for cycle (tb_fifo.v).
set -e
here=$(cd "$(dirname "$0")" && pwd)
root=$here/../../../..
out=$here/build; mkdir -p "$out"
perl -pe 's/^module paula_floppy_fifo\b/module paula_floppy_fifo_ref/' "$root/cores/amiga/rtl/src/minimig-aga/paula_floppy_fifo.v" > "$out/fifo_ref.v"
iverilog -g2012 -o "$out/tb" "$here/tb_fifo.v" "$out/fifo_ref.v" "$root/cores/amiga1200/rtl/paula_floppy_fifo.v"
vvp -n "$out/tb" | tee "$out/fifo.log"
grep -q '^PASS tb_fifo' "$out/fifo.log"
echo "PASS amiga1200_fifo (copy = shared fifo)"
