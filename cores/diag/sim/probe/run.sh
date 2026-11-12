#!/bin/sh
# T175 S1: the probe engine of the Port Diagnostics core (cores/diag/rtl/probe.v, diag_regs.v) through its register window, against a loop-back model
# (cores/diag/sim/tb_probe.v): exact WAIT, timestamps, the pin pulse measured by the TB's own clock counter, EDGES at the window's last countable clock, time-to-flip of a
# modelled comparator, FIFO order and overflow, LOOP, WAIT_USER / GO, safety (END / ABORT / error release), the live pin register, the register map.
set -e
here=$(cd "$(dirname "$0")/.." && pwd)
root=$here/../../..
out=$here/build/probe; mkdir -p "$out"
rtl=$root/cores/diag/rtl
vv() { # name probe.v defines...
  n=$1; p=$2; shift 2
  iverilog -g2012 -o "$out/$n.vvp" "$@" "$here/tb_probe.v" "${REGS:-$rtl/diag_regs.v}" "$p" && vvp -n "$out/$n.vvp" > "$out/$n.log" || true
}
rm -f "$out"/*.log
vv base "$rtl/probe.v"
grep "^PASS\|^FAIL" "$out/base.log"
grep -q '^PASS tb_probe errors=0' "$out/base.log" || { cat "$out/base.log"; echo "FAIL run.sh: the engine bench fails"; exit 1; }
echo "PASS probe (engine bench passes)"
