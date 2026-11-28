#!/bin/sh
# T175 / T260 co-check (spec S2 + the register assumptions of the application): the step words the diag APPLICATION (cores/diag/app/lib, engine v2) compiles out of every shipped example test
# (cores/diag/sd/tests/*.toml, pins by name against the table of the model engine = cores/diag/rtl/pins.toml) run on the real probe engine v2 (cores/diag/rtl) in iverilog behind its register
# block, wired as tb_probe.v wires it, against a model of the plugged peripherals (models/<id>.hex: the ten joystick pins, the paddle comparators, PADDLE_DIS = pin 67); the engine's STATUS,
# the FIFO words (popped at every wait, as the application does) and the PCs it waited at are written to a file and decoded by the application's own runner (cores/diag/app/tests/diag_co.cc replay
# -> diag_run_file), and the result file it writes is compared with what the model must have produced (models/<id>.expect, check.py). So the compile (op codes, field layout, us -> clocks,
# loop body, the 5 word SAMPLE), the register protocol (ABORT, CLEAR, CAP RESET, STEP_ADDR = 0, the word write order, RUN, STATUS, PC at WAIT_USER, GO, FIFO pop on byte 3) and the decode
# meet the RTL here, not a model.
set -e
here=$(cd "$(dirname "$0")/.." && pwd)
root=$here/../../..
out=$here/build/co; mkdir -p "$out"
co=$here/co
lib=$root/cores/diag/app/lib
INC="-DTEST_APP_DIAG=1 -I $lib -I $out/tab/drv -I $root/coreapp/src -I $root/coreapp/cores -I $root/app/loaders -I $root/app/video -I $root/app/lib/cbm1541 -I $root/host-app/software/hostapp -I $root/host-app/software/io/c64 -I $root/tests/coreapp"
rtl=$root/cores/diag/rtl
TESTS_ALL="pins paddle stick2 cd32"
rm -rf "$out"/*
# the driver table the application names its registers through (the same function the app's link and tests/coreapp/run.sh use)
ROOT=$root; . "$root/tests/coreapp/diag_drv.sh"
diag_drv_dir "$out/tab" || { echo "FAIL co: the driver table was not generated"; exit 1; }

# flow TAG APP PROBE REGS TESTSDIR [extra -I]: build the application side and the engine side, run the four example tests; the log is $out/TAG/log; returns 1 when anything is red
flow() {
  tag=$1; app=$2; probe=$3; regs=$4; tests=$5; incx=$6
  d=$out/$tag; mkdir -p "$d"; : > "$d/log"
  if ! { c++ -w -std=gnu++11 -DVIDEOMODE_NO_BOARD=1 $incx $INC -o "$d/diag_co" "$root/cores/diag/app/tests/diag_co.cc" $app &&
         iverilog -g2012 -I "$rtl" -o "$d/tb.vvp" "$here/tb_co.v" "$regs" "$probe" "$rtl/diag_pinrom.v"; } > "$d/build.log" 2>&1; then
    echo "FAIL $tag: build" >> "$d/log"; cat "$d/build.log" >> "$d/log"; return 1
  fi
  # the four tests run side by side (one iverilog process each; cd32 alone is about 25 million clocks, the others a tenth of it), each with its own log
  for t in $TESTS_ALL; do
    (
      l="$d/$t.log"; : > "$l"
      "$d/diag_co" capture "$tests/$t.toml" "$d/$t.steps.hex" >> "$l" 2>&1 || { echo "FAIL $tag: $t capture" >> "$l"; exit 1; }
      vvp -n "$d/tb.vvp" +steps="$d/$t.steps.hex" +model="$co/models/$t.hex" +out="$d/$t.run.hex" 2>&1 | grep -v 'WARNING\|\$finish' >> "$l" || true
      if [ ! -s "$d/$t.run.hex" ] || grep -q '^FAIL' "$l"; then echo "FAIL $tag: $t engine run" >> "$l"; exit 1; fi
      "$d/diag_co" replay "$tests/$t.toml" "$d/$t.run.hex" > "$d/$t.res.txt" 2>> "$l" || { echo "FAIL $tag: $t decode: $(tail -1 "$d/$t.res.txt")" >> "$l"; exit 1; }
      python3 "$co/check.py" "$d/$t.res.txt" "$co/models/$t.expect" >> "$l" 2>&1 || { echo "FAIL $tag: $t result differs from the model" >> "$l"; exit 1; }
      echo "ok $tag: $t" >> "$l"
    ) &
  done
  wait
  for t in $TESTS_ALL; do cat "$d/$t.log" >> "$d/log"; done
  grep -q '^FAIL' "$d/log" && return 1
  return 0
}

flow base "$lib/diagtest.cc $lib/dtv2.cc $lib/panel.cc $lib/pdnames.cc" "$rtl/probe.v" "$rtl/diag_regs.v" "$root/cores/diag/sd/tests" || true
cat "$out/base/log" | grep '^ok \|^FAIL\|^check\|^tb_co\|^co-check'
for t in $TESTS_ALL; do grep -q "^ok base: $t" "$out/base/log" || { echo "FAIL co: $t does not pass on the engine"; exit 1; }; done

echo "PASS co (the application's compile and decode meet engine v2 on all four example tests)"
