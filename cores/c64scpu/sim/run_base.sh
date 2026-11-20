#!/bin/sh
# T182 step 1: the boot bench of the C64SCPU core (the C64 with its 6510 still in): the whole machine under Verilator boots a synthetic system ROM of our own
# (tools/gen_c64scpu_test.py; no ROM byte of any real machine) and must show a C64 power-on screen with READY. in two places:
#   - RAM: the six screen codes of READY. at $04C8 (what the 6510 wrote through the DDR2 RAM path, read back from the DDR2 model);
#   - the picture: the VIC's own pixels of a frame (tb +PIXDUMP), read back as text through the character ROM (cores/c64scpu/sim/check_screen.py).
# Run from the repository root in trx64-sim-env (Verilator, GHDL). Each program ends with one PASS or FAIL line; a program whose name starts with ctl_ is a
# negative control: it must FAIL its check, and its lines are printed with the prefix "ctl| ".
#   sh cores/c64scpu/sim/run_base.sh [program ...]        default: boot ctl_nobasic ctl_nochars
# C64SCPU_MS overrides the simulated time (default 100 ms: five VIC frames), C64SCPU_BUILD the build directory, C64SCPU_SIM_DEFS the feature defines.
set -u
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../.." && pwd)
build=${C64SCPU_BUILD:-$here/build}
progs=${*:-"boot ctl_nobasic ctl_nochars"}
# the lock of run_emu.sh: both build this folder with the same defines, and the stage runs them side by side (run 2535: two
# Verilator builds in one obj folder, a link error)
( flock 9; [ -x "$build/obj/tb_c64scpu" ] || sh "$here/build_machine.sh" "$build" ) 9>"$build.lock" || exit 1
rc=0
for p in $progs; do
  run="$build/run_$p"
  rm -rf "$run"; mkdir -p "$run"
  cp -a "$build/run/." "$run/"
  exp=$(python3 "$root/tools/gen_c64scpu_test.py" "$p" "$run") || { echo "FAIL $p: generator"; rc=1; continue; }
  (cd "$run" && "$build/obj/tb_c64scpu" +MS="${C64SCPU_MS:-100}" +PIXDUMP=3 +SCREEN "$exp") > "$run/log" 2>&1
  python3 "$here/check_screen.py" "$run/pixdump.txt" "$run/expect.json" > "$run/pic.log" 2>&1
  ram=FAIL; grep -q "^PASS tb_c64scpu" "$run/log" && ram=PASS
  pic=FAIL; grep -q "^PASS check_screen" "$run/pic.log" && pic=PASS
  case $p in ctl_*) pre="ctl| " ;; *) pre="" ;; esac
  grep -E "^(cycles|ok |FAIL|PASS|romc|ce\\|)" "$run/log" | sed "s/^/$pre/"
  grep -E "^(pic\||PASS|FAIL)" "$run/pic.log" | sed "s/^/$pre/"
  if [ -n "$pre" ]; then
    if [ "$ram" = PASS ] && [ "$pic" = PASS ]; then echo "ctl| control $p: it PASSED both checks, it must fail one"; rc=1; else echo "ctl| control $p: fails as it must (RAM $ram, picture $pic)"; fi
  else
    echo "$ram+$pic $p (RAM check $ram, picture check $pic)"
    [ "$ram" = PASS ] && [ "$pic" = PASS ] || rc=1
  fi
done
[ $rc = 0 ] && echo "PASS run_base" || echo "FAIL run_base"
exit $rc
