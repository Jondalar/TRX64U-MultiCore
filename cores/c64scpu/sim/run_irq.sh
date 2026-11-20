#!/bin/sh
# T182 step 6: the interrupt / vector bench S9 and the native-mode part of S3 of the C64SCPU core, spec T182 section 2 and 5.8.
# The whole machine with the SuperCPU map under Verilator; a synthetic SuperCPU ROM of our own (tools/gen_c64scpu_irq.py, no ROM byte) in the DDR2 model. One run of 900 ms of machine time:
#   S9  per phase the handler count after 100,000 PHI2 cycles (CIA 1 / CIA 2 timer A every 4,000 cycles = 25 +- 1, VIC raster IRQ 5..6) in emulation mode (vector from the ROM's $FFFE / $FFFA:
#       hwenable + KERNAL visible = reroute), with hwenable off (vector from the SRAM of bank 1, no reroute) and in native mode ($FFEE / $FFEA); ten RESTORE pulses = exactly 10 NMIs ($FFEA),
#       BRK x3 ($FFE6) and COP x2 ($FFE4) = exactly 3 and 2. The verdict bytes ($0320+n = $AA) and the exact counts are +EXPECT (C64 RAM), the raw counts are printed ("dump|").
#   S3  native mode from SuperRAM $02:0000: 16-bit ADC/SBC, MVN $02 -> $03, the stack at $0FFF, [dp],Y in two banks, XBA, ADC long,X, JSL/RTL, STA long,X, an 8-bit ADC after SEP: the block at
#       $03:0000 is +DEXPECT (absolute DDR2 bytes, computed in the generator by hand from the instructions, not read from the machine).
#   T241 emulation mode, hwenable off: a VIC raster IRQ acknowledged with ASL $D019 (1 MHz), INC $D019 (1 MHz), ASL $D019 (20 MHz): 2..3 handler runs in 40,000 PHI2 cycles each
#       (verdicts $0327-$0329); ASL $0B02 on RAM writes three times with its STA, the native INC $0B03 twice with its STA (check_irq.py counts the tb's "mark|n|...|frame" lines).
#   T241 S2 emulation mode, hwenable off, software 1 MHz, display off: phase 10, an IRQ that rises in the high half of the last cycle of `INC $D012` (the raster compare is set to the line the
#       INC runs on by its modify cycle, the VIC matches in the last cycle) is taken after the NEXT instruction (verdict $032A; the handler sees the first instruction after the INC done and the
#       second not). Phase 11: RTI takes 6 cycles in emulation mode, phase 12: 7 in native mode (check_irq.py: the PHI2 cycles between the tb's marks 0 and 1 around loops of 20 and 10 RTI,
#       difference 240 / 230). Phases 10 and 11 fail on the logic before T241 S2 (the IRQ right after the INC; the old RTI of 5 cycles: 230); phase 12 passes on it as well: it is a guard of the native
#       RTI. The sources of the expected values are in the docstring of tools/gen_c64scpu_irq.py. The raw bytes of every phase ($0330-$033F, "raw" of expect.json) are the "dump|" line.
# Negative controls (the mutated machine must FAIL, lines prefixed "ctl| "):
#   ctl_noreroute SCPU_MUT_NOREROUTE   the vector pull always reads the SRAM of bank 0 (empty): every rerouted phase fails
#   ctl_emuonly   SCPU_MUT_EMUONLY     XCE is fetched as NOP: the 65816 stays in emulation mode, the native phases and the S3 block fail
# Run from the repository root in trx64-sim-env:   sh cores/c64scpu/sim/run_irq.sh
# C64SCPU_MAP_BUILD (the machine, shared with run_map.sh), C64SCPU_IRQ_WORK the run directory.
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
work=${C64SCPU_IRQ_WORK:-$here/irq_run}
rm -rf "$work"; mkdir -p "$work"
rc=0

run() { # run <label> <build>
  d="$work/$1"; mkdir -p "$d"; cp -a "$2/run/." "$d/"
  exp=$(python3 "$root/tools/gen_c64scpu_irq.py" "$d") || { echo "FAIL $1: generator"; exit 1; }
  (cd "$d" && "$2/obj/tb_c64scpu" +MS=900 +RESTOREHOOK +DUMP=0330 $exp) > "$d/log" 2>&1
}
ok() { grep -q "^PASS tb_c64scpu" "$work/$1/log" && python3 "$here/check_irq.py" "$work/$1/log" "$work/$1/expect.json" > /dev/null && echo PASS || echo FAIL; }
report() { # report <label> <prefix>
  d="$work/$1"
  grep -E "^(dump\||FAIL)" "$d/log" | head -12 | sed "s/^/$2/"
  echo "$2$(grep -c '^ok   ram' "$d/log") C64 RAM bytes (verdicts, exact counts) and $(grep -c '^ok   ddr' "$d/log") SuperRAM bytes of the S3 block equal the model"
  python3 "$here/check_irq.py" "$d/log" "$d/expect.json" | sed "s/^/$2/"
}

run irq "$bm"
report irq ""
r=$(ok irq); echo "$r irq"
[ "$r" = PASS ] || rc=1

ctl() { # ctl <label> <define>
  mk "${bm}_$1" "$2"
  run "ctl_$1" "${bm}_$1"
  report "ctl_$1" "ctl| "
  if [ "$(ok ctl_$1)" = PASS ]; then echo "ctl| control ctl_$1: it PASSED, it must fail"; rc=1; else echo "ctl| control ctl_$1: fails as it must"; fi
}
ctl noreroute SCPU_MUT_NOREROUTE
ctl emuonly SCPU_MUT_EMUONLY

[ $rc = 0 ] && echo "PASS run_irq" || echo "FAIL run_irq"
exit $rc
