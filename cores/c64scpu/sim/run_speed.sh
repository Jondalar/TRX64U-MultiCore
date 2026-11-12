#!/bin/sh
# T182 step 4: the speed and register benches S4, S10, S11 of the C64SCPU core, spec T182 section 2 and 5.6.
# The whole machine with the SuperCPU map under Verilator; a synthetic SuperCPU ROM of our own (tools/gen_c64scpu_speed.py) in the DDR2 model at 0x03080000.
#   speed  six timed phases (SRAM loop at 20 MHz with the display on and off, at 1 MHz on and off; 1,000 I/O reads with the display off and on); the PHI2 cycles between
#          the markers (cores/c64scpu/sim/check_speed.py) against the model computed from the loop's cycle count: S4 (speed switch) and S10 (BA / badlines vs SRAM and I/O)
#   regs   the readbacks of $D0B0-$D0BF in four register states against the values read from scpu64mem.c:634-821 (absolute SRAM bytes in the DDR2): S11
# Negative controls (the mutated machine must FAIL the same check, lines prefixed "ctl| "):
#   ctl_nofast  SCPU_MUT_NOFAST, speed   the clock enable is always PHI2: S4 fails
#   ctl_nosync  SCPU_MUT_NOSYNC, speed   I/O cycles do not wait for PHI2: the I/O loop is too short, S4 fails
#   ctl_lateaddr SCPU_MUT_LATEADDR, speed  the I/O address goes onto the C64 bus whenever the class is known, also after the PHI2 rise: the bus check of S4 fails
#   ctl_bastall SCPU_MUT_BASTALL, speed  BA stops the CPU at 20 MHz too: S10 fails
#   ctl_nohwgate SCPU_MUT_NOHWGATE, regs the registers that need hwenable ignore it: S11 fails
# Run from the repository root in trx64-sim-env:   sh cores/c64scpu/sim/run_speed.sh
# C64SCPU_MAP_BUILD (the machine, shared with run_map.sh), C64SCPU_SPEED_WORK the run directory.
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
work=${C64SCPU_SPEED_WORK:-$here/speed_run}
rm -rf "$work"; mkdir -p "$work"
rc=0

run() { # run <label> <build> <program> <ms>
  d="$work/$1"; mkdir -p "$d"; cp -a "$2/run/." "$d/"
  exp=$(python3 "$root/tools/gen_c64scpu_speed.py" "$3" "$d") || { echo "FAIL $1: generator"; exit 1; }
  (cd "$d" && "$2/obj/tb_c64scpu" +MS="$4" +BUSCHK "$exp") > "$d/log" 2>&1
}
chk() { python3 "$here/check_speed.py" "$work/$1/log" "$work/$1/expect.json"; }

run speed "$bm" speed 150
chk speed || rc=1
grep -E "^(scpu\||ce\||FAIL)" "$work/speed/log" | head -4
grep -q "^PASS tb_c64scpu" "$work/speed/log" || { echo "FAIL speed: the bench itself (program did not finish or the enable spacing broke)"; rc=1; }

run regs "$bm" regs 25
r=$(grep -q "^PASS tb_c64scpu" "$work/regs/log" && echo PASS || echo FAIL)
echo "regs| $(grep -c '^ok ' "$work/regs/log") register readbacks equal the values read from scpu64mem.c"
grep -E "^FAIL" "$work/regs/log" | head -6
echo "$r regs (S11)"
[ "$r" = PASS ] || rc=1

ctl() { # ctl <label> <define> <program> <ms>
  mk "${bm}_$1" "$2"
  run "ctl_$1" "${bm}_$1" "$3" "$4"
  if [ "$3" = speed ]; then
    chk "ctl_$1" | sed 's/^/ctl| /'; python3 "$here/check_speed.py" "$work/ctl_$1/log" "$work/ctl_$1/expect.json" > /dev/null 2>&1 && r=PASS || r=FAIL
  else
    grep -E "^FAIL" "$work/ctl_$1/log" | head -3 | sed 's/^/ctl| /'
    grep -q "^PASS tb_c64scpu" "$work/ctl_$1/log" && r=PASS || r=FAIL
  fi
  if [ "$r" = PASS ]; then echo "ctl| control ctl_$1: it PASSED, it must fail"; rc=1; else echo "ctl| control ctl_$1: fails as it must"; fi
}
ctl nofast SCPU_MUT_NOFAST speed 200
ctl nosync SCPU_MUT_NOSYNC speed 150
ctl bastall SCPU_MUT_BASTALL speed 150
ctl lateaddr SCPU_MUT_LATEADDR speed 150
ctl nohwgate SCPU_MUT_NOHWGATE regs 25

[ $rc = 0 ] && echo "PASS run_speed" || echo "FAIL run_speed"
exit $rc
