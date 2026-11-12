#!/bin/sh
# T182 step 2: the emulation-mode bench of the C64SCPU core: the 65C816 (E = 1) runs a test program of our own (tools/gen_c64scpu_emu.py; no ROM byte of any real machine) in the whole
# machine under Verilator. Three checks, each against a model that does not come from the machine under test:
#   - RAM: the result bytes of the program (a table, a 16 bit sum, shifts and rotates, the stack and JSR, (zp),Y, page crossings, and the 65C02 additions STZ, PHX/PHY/PLX/PLY,
#     INC A/DEC A, TSB/TRB, (zp), BIT #, BRA and XCE with E = 1) equal the bytes of the Python interpreter that ran the same program from the reset vector;
#   - cycles: the PHI2 cycles between the markers of the second pass of each routine equal the interpreter's (the documented cycle table);
#   - the 6510: the same routine R1 (documented NMOS opcodes only) on the step-1 machine (C64SCPU_CPU6510) takes the same PHI2 cycles as the 65816.
# Negative controls (each must FAIL its check, lines prefixed "ctl| "):
#   ctl_slow       the 65816 steps in every second PHI2 cycle (SCPU_MUT_SLOW): the cycles are not the model's;
#   ctl_emu_wrong  R1's sum loop has SBC for ADC, the expectation is the real program's: the RAM bytes are not the model's;
#   ctl_6510       the same program on the 6510 machine: R2's 65C02 opcodes are not the 6510's, the RAM bytes are not the model's;
#   ctl_ce2        the 65816's clock enable fires a second time 2 clk2x cycles after each one (SCPU_MUT_CE2): the assertion of tb_c64scpu.v (enables at least 3 clk2x cycles apart,
#                  the premise of the multicycle constraints in mono/build_mono.tcl) must fire. Every run of the bench prints the "ce|" line with the smallest gap it saw.
# Run from the repository root in trx64-sim-env:   sh cores/c64scpu/sim/run_emu.sh
# C64SCPU_BUILD (65816 machine, shared with run_base.sh), C64SCPU_BUILD_6510, C64SCPU_BUILD_SLOW, C64SCPU_BUILD_CE2 override the build directories.
set -u
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../.." && pwd)
b816=${C64SCPU_BUILD:-$here/build}
b6510=${C64SCPU_BUILD_6510:-$here/build_6510}
bslow=${C64SCPU_BUILD_SLOW:-$here/build_slow}
bce2=${C64SCPU_BUILD_CE2:-$here/build_ce2}
base="SID_REDIP NO_TURBO NO_REU TRX_SIM_ROMS SCPU_FLATMAP"
mk() { # mk <dir> <extra defines>; one build per directory: the nightly starts these benches at once (run 1188: four builds in build_map, a PCH race and an OOM kill)
  [ -x "$1/obj/tb_c64scpu" ] && return
  mkdir -p "$1"
  ( flock 9; [ -x "$1/obj/tb_c64scpu" ] || C64SCPU_SIM_DEFS="$base $2" sh "$here/build_machine.sh" "$1" ) 9>"$1.lock" || exit 1
}
mk "$b816" ""
mk "$b6510" "C64SCPU_CPU6510"
mk "$bslow" "SCPU_MUT_SLOW"
mk "$bce2" "SCPU_MUT_CE2"
work=${C64SCPU_EMU_WORK:-$here/emu_run}
rm -rf "$work"; mkdir -p "$work"

run() { # run <label> <build> <program> <expectation: yes|no>   -> $work/<label>/{log,expect.json}
  d="$work/$1"; mkdir -p "$d"; cp -a "$2/run/." "$d/"
  exp=$(python3 "$root/tools/gen_c64scpu_emu.py" "$3" "$d") || { echo "FAIL $1: generator"; exit 1; }
  if [ "$4" = yes ]; then (cd "$d" && "$2/obj/tb_c64scpu" +MS=20 "$exp") > "$d/log" 2>&1
  else (cd "$d" && "$2/obj/tb_c64scpu" +MS=20) > "$d/log" 2>&1; fi
}
ram() { grep -q "^PASS tb_c64scpu" "$work/$1/log" && echo PASS || echo FAIL; }
rc=0

run emu "$b816" emu yes
run ref6510 "$b6510" emu no
r=$(ram emu); c=$(python3 "$here/check_emu.py" "$work/emu/log" "$work/emu/expect.json" --same-as "$work/ref6510/log" > "$work/emu/cyc.log" 2>&1 && echo PASS || echo FAIL)
echo "ram| $(grep -c '^ok ' "$work/emu/log") RAM bytes equal the model's"
grep -E "^ce\|" "$work/emu/log"
grep -E "^FAIL" "$work/emu/log"
cat "$work/emu/cyc.log"
echo "$r+$c emu (RAM bytes $r, PHI2 cycles against the model and the 6510 $c)"
{ [ "$r" = PASS ] && [ "$c" = PASS ]; } || rc=1

# the 6510 reference must itself agree with the model on R1 (else the comparison above means nothing)
if python3 "$here/check_emu.py" "$work/ref6510/log" "$work/emu/expect.json" --no-r2 > "$work/ref6510/cyc.log" 2>&1; then
  echo "PASS the 6510 reference takes the model's cycles in R1"
else
  cat "$work/ref6510/cyc.log"; echo "FAIL the 6510 reference"; rc=1
fi

# negative controls
run ctl_slow "$bslow" emu yes
c=$(python3 "$here/check_emu.py" "$work/ctl_slow/log" "$work/ctl_slow/expect.json" > "$work/ctl_slow/cyc.log" 2>&1 && echo PASS || echo FAIL)
sed 's/^/ctl| /' "$work/ctl_slow/cyc.log"
if [ "$c" = PASS ]; then echo "ctl| control ctl_slow: it PASSED the cycle check, it must fail"; rc=1; else echo "ctl| control ctl_slow: fails as it must (cycles $c)"; fi

run ctl_emu_wrong "$b816" ctl_emu_wrong yes
r=$(ram ctl_emu_wrong)
grep -E "^FAIL" "$work/ctl_emu_wrong/log" | head -4 | sed 's/^/ctl| /'
if [ "$r" = PASS ]; then echo "ctl| control ctl_emu_wrong: it PASSED the RAM check, it must fail"; rc=1; else echo "ctl| control ctl_emu_wrong: fails as it must (RAM $r)"; fi

run ctl_6510 "$b6510" emu yes
r=$(ram ctl_6510)
grep -E "^FAIL" "$work/ctl_6510/log" | head -3 | sed 's/^/ctl| /'
if [ "$r" = PASS ]; then echo "ctl| control ctl_6510: the 6510 PASSED the RAM check of the 65C02 additions, it must fail"; rc=1; else echo "ctl| control ctl_6510: the 6510 fails as it must (RAM $r: no STZ, TSB, BRA, XCE)"; fi

run ctl_ce2 "$bce2" emu yes
grep -E "^(FAIL ce gap|ce\|)" "$work/ctl_ce2/log" | head -4 | sed 's/^/ctl| /'
if grep -q "^FAIL ce gap" "$work/ctl_ce2/log" && grep -q "^FAIL tb_c64scpu" "$work/ctl_ce2/log"; then echo "ctl| control ctl_ce2: the enable-spacing assertion fires as it must"; else echo "ctl| control ctl_ce2: the assertion did NOT fire, it must"; rc=1; fi

[ $rc = 0 ] && echo "PASS run_emu" || echo "FAIL run_emu"
exit $rc
