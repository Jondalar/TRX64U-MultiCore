#!/bin/sh
# T175 co-check (spec S2 + the register assumptions of the application): the step words the APPLICATION compiles out of every shipped example test (cores/diag/sd/tests/*.toml)
# run on the real probe engine (cores/diag/rtl) in iverilog behind its register block, against a model of the plugged peripherals (models/<id>.hex); the engine's STATUS,
# FIFO words and the PCs it waited at are written to a file and decoded by the application's own runner (tests/coreapp/diag_co.cc replay -> diag_run_file), and the result file it
# writes is compared with what the model must have produced (models/<id>.expect, check.py). So the compile (op codes, field layout, us -> clocks, loop body, PADCTL bits),
# the register protocol (ABORT, CLEAR, STEP_ADDR = 0, the word write order, RUN, STATUS, PC at WAIT_USER, GO, FIFO pop on byte 3) and the decode meet the RTL here, not a model.
set -e
here=$(cd "$(dirname "$0")/.." && pwd)
root=$here/../../..
out=$here/build/co; mkdir -p "$out"
co=$here/co
INC="-I $root/coreapp/src -I $root/coreapp/cores -I $root/app/loaders -I $root/app/video -I $root/app/lib/cbm1541 -I $root/host-app/software/hostapp -I $root/host-app/software/io/c64 -I $root/tests/coreapp"
rtl=$root/cores/diag/rtl
TESTS_ALL="pins paddle stick2 cd32"
rm -rf "$out"/*

# flow TAG APP PROBE REGS TESTSDIR [extra -I]: build the application side and the engine side, run the four example tests; the log is $out/TAG/log; returns 1 when anything is red
flow() {
  tag=$1; app=$2; probe=$3; regs=$4; tests=$5; incx=$6
  d=$out/$tag; mkdir -p "$d"; : > "$d/log"
  if ! { c++ -w -std=gnu++11 -DVIDEOMODE_NO_BOARD=1 $incx $INC -o "$d/diag_co" "$root/tests/coreapp/diag_co.cc" "$app" &&
         iverilog -g2012 -o "$d/tb.vvp" "$here/tb_co.v" "$regs" "$probe"; } > "$d/build.log" 2>&1; then
    echo "FAIL $tag: build" >> "$d/log"; cat "$d/build.log" >> "$d/log"; return 1
  fi
  rc=0
  for t in $TESTS_ALL; do
    "$d/diag_co" capture "$tests/$t.toml" "$d/$t.steps.hex" >> "$d/log" 2>&1 || { echo "FAIL $tag: $t capture" >> "$d/log"; rc=1; continue; }
    vvp -n "$d/tb.vvp" +steps="$d/$t.steps.hex" +model="$co/models/$t.hex" +out="$d/$t.run.hex" 2>&1 | grep -v 'WARNING\|\$finish' >> "$d/log" || true
    if [ ! -s "$d/$t.run.hex" ] || grep -q '^FAIL' "$d/log"; then echo "FAIL $tag: $t engine run" >> "$d/log"; rc=1; continue; fi
    "$d/diag_co" replay "$tests/$t.toml" "$d/$t.run.hex" > "$d/$t.res.txt" 2>> "$d/log" || { echo "FAIL $tag: $t decode: $(tail -1 "$d/$t.res.txt")" >> "$d/log"; rc=1; continue; }
    python3 "$co/check.py" "$d/$t.res.txt" "$co/models/$t.expect" >> "$d/log" 2>&1 || { echo "FAIL $tag: $t result differs from the model" >> "$d/log"; rc=1; continue; }
    echo "ok $tag: $t" >> "$d/log"
  done
  return $rc
}

flow base "$root/coreapp/src/diagtest.cc" "$rtl/probe.v" "$rtl/diag_regs.v" "$root/cores/diag/sd/tests" || true
cat "$out/base/log" | grep '^ok \|^FAIL\|^check\|^tb_co\|^co-check'
for t in $TESTS_ALL; do grep -q "^ok base: $t" "$out/base/log" || { echo "FAIL co: $t does not pass on the engine"; exit 1; }; done

echo "PASS co (the application's compile and decode meet the engine on all four example tests)"
