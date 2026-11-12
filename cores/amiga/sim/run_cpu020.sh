#!/bin/sh
# T97 step 2: the Amiga 1200 core's CPU is a 68020. A test ROM (MODE=cpu020 in gen_testrom.py - NOT a label, it picks the ROM)
# runs EXTB.L, MULU.L (32 bit) and BFEXTU, which a 68000 answers with the illegal-instruction exception.
#   the A1200 build (TG68K kernel behind cores/amiga1200/rtl/tg68k_020.v, AGA + 2 MB + sector HDF size as core_sources.tcl
#      generics) must reach the end of the ROM with the right results and still show the picture.
set -e
here=$(cd "$(dirname "$0")" && pwd)
sh "$here/../../amiga1200/tools/check_wrapper_fork.sh"
mkdir -p "$here/build"
TG68K=1 C020=1 MODE=cpu020 OUT_SUFFIX=tg020 CHS_INIT=16 MEM_INIT=3 HDF_SECT=1 sh "$here/run_l2.sh" > "$here/build/cpu020_tg68k.log" 2>&1 || { tail -30 "$here/build/cpu020_tg68k.log"; echo "FAIL build tg68k"; exit 1; }
grep -E "^cpu020|FAIL|PASS|first yellow" "$here/build/cpu020_tg68k.log"
grep -q 'PASS tb_l2 errors=0' "$here/build/cpu020_tg68k.log" || { echo "FAIL amiga_cpu020: the TG68K build"; exit 1; }
grep -q 'cpu020: EXTB.L ffffff80, MULU.L 24101100, BFEXTU 00000080, flag 020f' "$here/build/cpu020_tg68k.log" || { echo "FAIL amiga_cpu020: results"; exit 1; }
echo "PASS amiga_cpu020 (the TG68K build runs 68020 code)"
