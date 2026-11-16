#!/bin/sh
# T208: the Metal Dust benches of the C64SCPU core (spec T208 section 2 S1-S3). The whole machine with the SuperCPU map under Verilator; synthetic SuperCPU ROMs of our own
# (tools/gen_c64scpu_mdust.py, no ROM byte of a game, a crack or a CMD ROM) in the DDR2 model. The runs:
#   menu20   S3  the crack menu's raster IRQ at line $F3 (handler shape of the spec), 30 frames at 20 MHz and 30 at 1 MHz are not mixed: menu20 stays at 20 MHz
#   menu1    S3  the same, switched to 1 MHz (system speed register) after 30 frames
#   scroll   S1  optimisation mode $D075 (mirror $40-$7F), a dense stream of mirrored writes with $D077 windows, badlines and sprite DMA: the VIC's own copy of the RAM against the 65816's
#   hud      S2  the same stream with the game's raster chain (IRQ at $D1: 22 I/O writes), the clk cycle of each write counted from the IRQ line's rising edge; no chain may take more than writes + 1 PHI2 cycles, the cold first one included
#   hudx     S2  the same with the scroll's bank-2 source on the cache index of the stage's code: the code is evicted every frame (asserted: at most writes + 1 per chain, held by the next-line prefetch of scpu_cache.v)
#   smc      S2  the next-line prefetch of the code cache against self-modifying code (60 code lines that store into the next line while it is fetched, 8 passes; tools/gen_c64scpu_mdust.py smcbench)
#   smc_nokill (control) the same bench on a machine built with SCPU_MUT_NOKILL (guard off): must FAIL
#   tb_scpu_cache (iverilog, scpu_cache.v alone): races no CPU program can time to the clock - a CPU write in the clock a prefetch starts (SCPU_MUT_NOPGOWR must FAIL), a byte write while a killed line is invalidated (SCPU_MUT_NOINVDEFER must FAIL)
#   stars / stars60  T208 (BA hold of I/O writes): the crack menu's 3-line raster IRQ chain (tools/gen_c64scpu_mdust.py starsq30 / starsq60: the dispatcher short enough that the handler's I/O starts before the VIC's BA window) and a nested CIA music IRQ against the VICE accounting: on a badline no I/O access completes while the VIC has the bus, and the chain runs through every frame (the VIC drops a register write that arrives while AEC is low)
#   stz      S2  24 back-to-back stz $D0xx at 20 MHz, cold then warm, against a port of the VICE accounting (check_mdust.py vice_model)
# Run from the repository root in trx64-sim-env:   sh cores/c64scpu/sim/run_mdust.sh
# C64SCPU_MAP_BUILD (the machine, shared with run_map.sh), C64SCPU_MDUST_WORK the run directory, MDUST_ONLY=<label> runs one.
set -u
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../.." && pwd)
base="SID_REDIP NO_TURBO NO_REU TRX_SIM_ROMS"
bm=${C64SCPU_MAP_BUILD:-$here/build_map}
mk() { # mk <dir> <extra defines>; one build per directory (the nightly starts these benches at once)
  [ -x "$1/obj/tb_c64scpu" ] && return
  mkdir -p "$1"
  ( flock 9; [ -x "$1/obj/tb_c64scpu" ] || C64SCPU_SIM_DEFS="$base $2" sh "$here/build_machine.sh" "$1" ) 9>"$1.lock" || exit 1
}
mk "$bm" ""
work=${C64SCPU_MDUST_WORK:-$here/mdust_run}
rm -rf "$work"; mkdir -p "$work"
rc=0

run() { # run <label> <generator kind> <build> <tb args>
  d="$work/$1"; mkdir -p "$d"; cp -a "$3/run/." "$d/"
  python3 -I "$root/tools/gen_c64scpu_mdust.py" "$d" "$2" > /dev/null || { echo "FAIL $1: generator"; exit 1; }
  b=$3; shift 3
  (cd "$d" && "$b/obj/tb_c64scpu" "$@") > "$d/log" 2>&1
}

for spec in "menu20 menu20 +MS=1400 +IOW" "menu1 menu1 +MS=2400 +IOW" "scroll scroll +MS=120 +MIRCHK" "hud hud +MS=200 +MIRCHK +IOW" "hudx hudx +MS=200 +MIRCHK +IOW" "stz stz +MS=20 +IOW" "smc smc +MS=40" "stars starsq30 +MS=200 +IOW" "stars60 starsq60 +MS=200 +IOW"; do
  set -- $spec
  label=$1; kind=$2; shift 2
  [ -z "${MDUST_ONLY:-}" ] || [ "$MDUST_ONLY" = "$label" ] || continue
  run "$label" "$kind" "$bm" "$@"
  python3 -I "$here/check_mdust.py" "$label" "$work/$label/log" || rc=1
done

# S5 controls and the unit bench of the cache alone (tb_scpu_cache.v: the races no CPU program can time to the clock). Skipped with MDUST_ONLY.
if [ -z "${MDUST_ONLY:-}" ]; then
  mk "${bm}_nokill" "SCPU_MUT_NOKILL"                       # the write-to-prefetched-line guard is off: the smc bench must FAIL
  run smc_nokill smc "${bm}_nokill" +MS=40
  if python3 -I "$here/check_mdust.py" smc "$work/smc_nokill/log" > "$work/smc_nokill/verdict" 2>&1; then echo "mdust| control smc_nokill: it PASSED, it must fail"; rc=1
  else grep -E "^mdust\| smc (FAIL|ok)" "$work/smc_nokill/verdict" | sed 's/^/ctl| /'; echo "mdust| control smc_nokill (SCPU_MUT_NOKILL): fails as it must"; fi
  ut() { # ut <label> <define or empty> <must pass: 1|0>
    iverilog -g2012 ${2:+-D$2} -o "$work/tb_scpu_cache_$1" "$here/tb_scpu_cache.v" "$root/cores/c64scpu/scpu_cache.v" || { echo "FAIL tb_scpu_cache $1: iverilog"; rc=1; return; }
    vvp "$work/tb_scpu_cache_$1" > "$work/tb_scpu_cache_$1.log" 2>&1
    if grep -q "^PASS tb_scpu_cache" "$work/tb_scpu_cache_$1.log"; then v=1; else v=0; fi
    pre=""; [ "$3" = 1 ] || pre="ctl| "                    # a control's own FAIL lines are not the bench's verdict (ci/sims.sh verdict_of skips "ctl| " lines)
    grep -E "^(ok|FAIL|PASS)" "$work/tb_scpu_cache_$1.log" | sed "s/^/${pre}tb_scpu_cache $1| /"
    if [ "$v" = "$3" ]; then echo "mdust| tb_scpu_cache $1: $( [ "$3" = 1 ] && echo PASS || echo 'fails as it must')"; else echo "mdust| tb_scpu_cache $1: wrong verdict"; rc=1; fi
  }
  ut main "" 1
  ut nopgowr SCPU_MUT_NOPGOWR 0
  ut noinvdefer SCPU_MUT_NOINVDEFER 0
fi

[ $rc = 0 ] && echo "PASS run_mdust" || echo "FAIL run_mdust"
exit $rc
