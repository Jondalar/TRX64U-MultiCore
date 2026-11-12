#!/bin/sh
# T138: the A1200's Zorro III fast RAM, one board of 32 MB (FAST_CFG 4), under the 68020 (TG68K). The test ROM (gen_testrom.py MODE=z3ram) runs
# AutoConfig as the OS does (descriptor at $E80000, base to register 0x44), writes long patterns at +0, +4, every power of two up to +16 MB and the last
# longword, mixed widths at the end and at the 8 / 16 MB edges, and runs code from the board; the bench reads the DDR2 model at byte 0x6000000+.
#   the A1200 core: all checks pass, the DDR2 holds the first / middle / last longword at 0x6000000 / 0x7000000 / 0x7FFFFFC.
here=$(cd "$(dirname "$0")" && pwd)
mkdir -p "$here/build"
run() { # name, extra env...
  n=$1; shift
  env TG68K=1 Z3=1 FASTCFG=4 MODE=z3ram NOPIC=1 MEMNB=3 CHS_INIT=16 MEM_INIT=3 HDF_SECT=1 DDRLOG2=23 FRAMES=40 OUT_SUFFIX=$n "$@" sh "$here/run_l2.sh" > "$here/build/$n.log" 2>&1
  grep -E "^z3|^FAIL|^PASS|^ok|ok  :" "$here/build/$n.log" | sed "s/^/$n| /"
}
run z3_a1200
grep -q 'PASS tb_l2 errors=0' "$here/build/z3_a1200.log" || { echo "FAIL amiga_z3: the A1200 core"; exit 1; }
echo "PASS amiga_z3 (32 MB Zorro III board configures, holds data at DDR2 0x6000000, all address bits count)"
