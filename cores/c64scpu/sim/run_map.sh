#!/bin/sh
# T182 step 3d: the memory-map benches S1 (boot with a ROM) and S6 (SuperRAM tags, the $F6/$F7 alias, the ROM window) of the C64SCPU core, spec T182 section 2.
# The whole machine with the SuperCPU map (no SCPU_FLATMAP) under Verilator; the SuperCPU ROM (tools/gen_c64scpu_map.py, our own code, 128 KiB) sits in the DDR2 model at 0x03080000;
# the check is +DEXPECT: absolute DDR2 bytes (the SRAM at 0x03000000 + a, the SuperRAM at 0x02000000 + a) the program left, computed in the generator, not read from the machine.
#   s1       the CPU takes its reset vector from the ROM (boot map), stores $A5 at $00:2000, copies a tail to SRAM, writes $D07E, $D0B6 and runs from SRAM
#   s1_held  the same ROM with +ROMVALID=0: the CPU must not run (nothing is stored)
#   s6       244 bank tags, hwenable gate, the $F6/$F7 alias, the 128 KiB ROM repeated in $F8-$FF, writes there ignored
# Negative controls (the mutated machine must FAIL the same check, lines prefixed "ctl| "):
#   ctl_nobootmap  SCPU_MUT_NOBOOTMAP, s1      ctl_novalid  SCPU_MUT_NOVALID, s1_held (ROM_VALID ignored: the CPU runs)
#   ctl_noalias    SCPU_MUT_NOALIAS, s6        ctl_addr23   SCPU_MUT_ADDR23, s6
# Run from the repository root in trx64-sim-env:   sh cores/c64scpu/sim/run_map.sh
# C64SCPU_MAP_BUILD (the machine), C64SCPU_MAP_BUILD_<MUT> override the build directories, C64SCPU_MAP_WORK the run directory.
set -u
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../.." && pwd)
base="SID_REDIP NO_TURBO NO_REU TRX_SIM_ROMS"
bm=${C64SCPU_MAP_BUILD:-$here/build_map}
mk() { # mk <dir> <extra defines>; one build per directory: the nightly starts these benches at once (run 1188: four builds in build_map, a PCH race and an OOM kill)
  [ -x "$1/obj/tb_c64scpu" ] && return
  mkdir -p "$1"
  ( flock 9; [ -x "$1/obj/tb_c64scpu" ] || C64SCPU_SIM_DEFS="$base $2" sh "$here/build_machine.sh" "$1" ) 9>"$1.lock" || exit 1
}
mk "$bm" ""
work=${C64SCPU_MAP_WORK:-$here/map_run}
rm -rf "$work"; mkdir -p "$work"
rc=0

run() { # run <label> <build> <program> <ROMVALID> <ms>
  d="$work/$1"; mkdir -p "$d"; cp -a "$2/run/." "$d/"
  exp=$(python3 "$root/tools/gen_c64scpu_map.py" "$3" "$d") || { echo "FAIL $1: generator"; exit 1; }
  (cd "$d" && "$2/obj/tb_c64scpu" +MS="$5" +ROMVALID="$4" "$exp") > "$d/log" 2>&1
}
ok() { grep -q "^PASS tb_c64scpu" "$work/$1/log" && echo PASS || echo FAIL; }
report() { # report <label> <prefix>
  grep -E "^(scpu\||ce\||FAIL)" "$work/$1/log" | head -6 | sed "s/^/$2/"
  echo "$2$(grep -c '^ok ' "$work/$1/log") DDR2 bytes equal the model's"
}

for t in "s1 s1 1 25" "s1_held s1_held 0 25" "s6 s6 1 100"; do
  set -- $t
  run "$1" "$bm" "$2" "$3" "$4"
  r=$(ok "$1"); report "$1" ""
  echo "$r $1"
  [ "$r" = PASS ] || rc=1
done

ctl() { # ctl <label> <define> <program> <ROMVALID> <ms>
  mk "${bm}_$1" "$2"
  run "ctl_$1" "${bm}_$1" "$3" "$4" "$5"
  r=$(ok "ctl_$1"); report "ctl_$1" "ctl| "
  if [ "$r" = PASS ]; then echo "ctl| control ctl_$1: it PASSED, it must fail"; rc=1; else echo "ctl| control ctl_$1: fails as it must"; fi
}
ctl nobootmap SCPU_MUT_NOBOOTMAP s1 1 25
ctl novalid SCPU_MUT_NOVALID s1_held 0 25
ctl noalias SCPU_MUT_NOALIAS s6 1 100
ctl addr23 SCPU_MUT_ADDR23 s6 1 100

[ $rc = 0 ] && echo "PASS run_map" || echo "FAIL run_map"
exit $rc
