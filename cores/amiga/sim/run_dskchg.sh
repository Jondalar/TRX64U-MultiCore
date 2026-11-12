#!/bin/sh
# T85 (disk change): /DSKCHG of paula_floppy.v as Kickstart's poll reads it (tb_dskchg.v), on the A500's copy (cores/amiga/rtl/a500) and on the A1200's copy.
# run_dskchg.sh [a500|a1200|all] (T100 D10): a500 = the A500's copy, a1200 = the A1200's copy.
set -e
which=${1:-all}
case "$which" in a500|a1200|all) ;; *) echo "usage: run_dskchg.sh [a500|a1200|all]"; exit 2 ;; esac
here=$(cd "$(dirname "$0")" && pwd)
root=$here/../../..
out=$here/build/dskchg; mkdir -p "$out"
sh "$root/cores/amiga1200/tools/check_wrapper_fork.sh" > /dev/null
m=$root/cores/amiga/rtl/src/minimig-aga
a=$root/cores/amiga1200/rtl
run() {  # name paula_floppy.v fifo
  iverilog -g2012 -o "$out/$1" "$here/tb_dskchg.v" "$2" "$3" && vvp -n "$out/$1" | grep -v '^paula_floppy.v:'
}
if [ "$which" != a1200 ]; then
run a500 "$root/cores/amiga/rtl/a500/paula_floppy.v" "$root/cores/amiga/rtl/a500/paula_floppy_fifo.v" | tee "$out/a500.log"
grep -q '^PASS tb_dskchg' "$out/a500.log"
fi
if [ "$which" != a500 ]; then
run a1200 "$a/paula_floppy.v" "$a/paula_floppy_fifo.v" | tee "$out/a1200.log"
grep -q '^PASS tb_dskchg' "$out/a1200.log"
fi
echo "PASS amiga_dskchg ($which)"
