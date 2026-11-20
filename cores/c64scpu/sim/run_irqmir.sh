#!/bin/sh
# T241 S2: where does the C64SCPU core deviate at software 1 MHz when the mirror mode is "mirror everything" ($D077)? The whole machine with the SuperCPU map under Verilator; a synthetic SuperCPU
# ROM of our own (tools/gen_c64scpu_irqmir.py, no ROM byte) in the DDR2 model at 0x03080000. One run, four phases (see the generator): A0 / A1 in the reset mirror mode (masked / CIA 1 timer A IRQs
# with the PAL KERNAL's period), B0 / B1 after `SEI; STA $D07E; STA $D077; STA $D07F` as on the board. The probe (tb_irqmir.vh) logs per PHI2 cycle; check_irqmir.py compares the masked phases with the
# model of the C64's cycles (6502 / 65816 bus cycles + RDY stalls at BA low), counts the IRQs taken against the timer's underflows and prints the first deviating pass.
# No negative controls: this is a diagnosis, not a regression bench (T241 §2 S2).
# Run from the repository root in trx64-sim-env:   sh cores/c64scpu/sim/run_irqmir.sh
# C64SCPU_MAP_BUILD (the machine, shared with run_map.sh), C64SCPU_IRQMIR_WORK the run directory, C64SCPU_IRQMIR_LOG a copy of the full log.
set -u
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../.." && pwd)
base="SID_REDIP NO_TURBO NO_REU TRX_SIM_ROMS"
bm=${C64SCPU_MAP_BUILD:-$here/build_map}
mk() { # mk <dir> <extra defines>
  [ -x "$1/obj/tb_c64scpu" ] && return
  mkdir -p "$1"
  ( flock 9; [ -x "$1/obj/tb_c64scpu" ] || C64SCPU_SIM_DEFS="$base $2" sh "$here/build_machine.sh" "$1" ) 9>"$1.lock" || exit 1
}
mk "$bm" ""
work=${C64SCPU_IRQMIR_WORK:-$here/irqmir_run}
rm -rf "$work"; mkdir -p "$work"
rc=0
# two programs: simple (PHA/PLA/JSR/INC) and kernal (the shape of the KERNAL's IRQ and of BASIC's arithmetic: RMW in zero page, (zp),Y, zp,X, CIA / colour RAM accesses)
for prog in ${IRQMIR_PROGS:-simple kernal}; do
  echo "=== program $prog"
  d="$work/$prog"; mkdir -p "$d"; cp -a "$bm/run/." "$d/"
  ms=$(IRQMIR_PROG=$prog python3 "$root/tools/gen_c64scpu_irqmir.py" "$d") || { echo "FAIL generator"; exit 1; }
  (cd "$d" && "$bm/obj/tb_c64scpu" $ms +IRQMIR) > "$d/log" 2>&1
  [ -n "${C64SCPU_IRQMIR_LOG:-}" ] && cp "$d/log" "$C64SCPU_IRQMIR_LOG.$prog"
  grep -E "^(phs|phe|sum|holdwhy|irqlost|val|fin|scpu|cycles|mir|ce\||FAIL|PASS)" "$d/log" | head -60
  python3 "$here/check_irqmir.py" "$d/log" "$d/expect.json" || rc=1
done
exit $rc
