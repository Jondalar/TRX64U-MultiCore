#!/bin/sh
# T97 5.11: the A1200's registered-read FIFO (trx_afifo_rr, block RAM) equals the shared trx_afifo on everything a consumer sees
# (full, empty, rdata while not empty), at three clock ratios and two depths.
set -e
here=$(cd "$(dirname "$0")" && pwd)
root=$here/../../../..
out=$here/build; mkdir -p "$out"
rr=$root/cores/amiga1200/rtl/trx_afifo_rr.v
ref=$root/cores/amiga/rtl/trx/trx_afifo.v
run() {   # tag, source, -P options
  tag=$1; src=$2; shift 2
  iverilog -g2012 "$@" -o "$out/tb_$tag" "$here/tb_afifo.v" "$ref" "$src"
  vvp -n "$out/tb_$tag" | tee "$out/afifo_$tag.log"
}
run a "$rr" -Ptb_afifo.WP=10 -Ptb_afifo.RP=7;  grep -q '^PASS tb_afifo' "$out/afifo_a.log"
run b "$rr" -Ptb_afifo.WP=7 -Ptb_afifo.RP=10;   grep -q '^PASS tb_afifo' "$out/afifo_b.log"
run c "$rr" -Ptb_afifo.WP=10 -Ptb_afifo.RP=10 -Ptb_afifo.AW=3 -Ptb_afifo.W=128; grep -q '^PASS tb_afifo' "$out/afifo_c.log"
echo "PASS amiga1200_afifo (registered-read FIFO = shared FIFO)"
