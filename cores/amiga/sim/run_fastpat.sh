#!/bin/sh
# T97 5.9: the A1200's fast RAM under the 68020 (TG68K), 5.5 MB = three Zorro II cards, with the AGA wide fetch (FMODE 3, 8 planes) running.
# The test ROM (gen_testrom.py MODE=fastpatdma) writes and checks patterns (long / byte / word / odd address, code run from each card)
# and then the slow-RAM window: the cards 2 and 3 are "stolen from the slow RAM", so a write at $C00000 must not reach $600000.
#   1. the A1200 core (its own amiga_core.v, app config chip 2M slow 1.5M fast 5.5M = MEM 0F, FAST 3): errors=0.
here=$(cd "$(dirname "$0")" && pwd)
mkdir -p "$here/build"
run() { # name, extra env...
  n=$1; shift
  env TG68K=1 FASTPAT=1 MODE=fastpatdma NOPIC=1 MEMNB=15 CHS_INIT=16 MEM_INIT=3 HDF_SECT=1 OUT_SUFFIX=$n "$@" sh "$here/run_l2.sh" > "$here/build/$n.log" 2>&1
  grep -E "^fastpat|^FAIL|^PASS" "$here/build/$n.log" | sed "s/^/$n| /"
}
run fp_a1200 FASTCFG=3 FRAMES=40
grep -q 'PASS tb_l2 errors=0' "$here/build/fp_a1200.log" || { echo "FAIL amiga_fastpat: the A1200 core"; exit 1; }
echo "PASS amiga_fastpat (5.5 MB fast RAM is clean beside AGA DMA; the chipset no longer serves the slow RAM the cards took"
