#!/usr/bin/env bash
# T152 S5/S6/S7 (D4, D6, D7): the flash protection before a large core (flashlock.cc) against a model of the W25Q128JV, including the one thing that must
# never happen - SRP together with SRL, the one-time program lock - which the model records as OTP. Each decision has a control: a mutant of the source under
# test has to go red, and the SRP controls are the ones that matter most.
#   tests/hostapp/run_flashlock.sh [scratch dir]          (run.sh calls it with its own)
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
H="$ROOT/host-app/software/hostapp"
T="${1:-}"
if [ -z "$T" ]; then T="$(mktemp -d)"; trap 'rm -rf "$T"' EXIT; fi
T="$T/flashlock"; mkdir -p "$T"
TST="$ROOT/tests/hostapp/test_flashlock.cc"
c++ -std=gnu++14 -Wall -I"$H" -o "$T/test_flashlock" "$TST" "$H/flashlock.cc" 2>&1
"$T/test_flashlock"
fl_mutant() {   # name, sed expression on flashlock.cc
  local name="$1" expr="$2" bad="$T/mut_$1.cc"
  sed -e "$expr" "$H/flashlock.cc" > "$bad"
  cmp -s "$bad" "$H/flashlock.cc" && { echo "CONTROL $name: the sed changed nothing" >&2; exit 1; }
  c++ -std=gnu++14 -w -I"$H" -o "$T/mut_$name" "$TST" "$bad" 2>&1
  if "$T/mut_$name" > "$T/mut_$name.log" 2>&1; then echo "CONTROL $name: the bench PASSED against the mutant" >&2; exit 1; fi
  grep -q '^FAIL' "$T/mut_$name.log" || { echo "CONTROL $name: it failed without a red check" >&2; exit 1; }
  echo "control $name: fails as it must ($(grep -c '^FAIL' "$T/mut_$name.log") check(s) red)"
}
# SRP (the one-time program lock): the mask is gone / the guard in front of SRL is gone. Each alone has to be caught by its own scenario.
fl_mutant fl_srp_mask_gone 's/    v \&= (uint8_t)~FL_SR1_SRP;/    (void)0;/'
fl_mutant fl_srp_guard_gone 's/    if (sr1 \& FL_SR1_SRP) {  /    if (false) {  /'
# volatile only
fl_mutant fl_nonvolatile_enable 's/#define CMD_WRITE_ENABLE_VOLATILE 0x50u/#define CMD_WRITE_ENABLE_VOLATILE 0x06u/'
# the read-back
fl_mutant fl_no_readback 's/    if (!flashlock_is_set(info->sr1, info->sr2)) {/    if (false) {/'
# the range and the register bits
fl_mutant fl_range_2mib 's/| FL_SR1_WANT));/| FL_SR1_TB | 0x10));/'
fl_mutant fl_cmp_kept 's/(cur2 \& ~FL_SR2_CMP)/cur2/'
fl_mutant fl_no_lockdown 's/| FL_SR2_SRL));/));/'
# which chip, and the setting
fl_mutant fl_any_chip 's/    if (info->jedec != FL_JEDEC_W25Q128) {/    if (false) {/'
fl_mutant fl_off_ignored 's/    if (!enabled) {/    if (false) {/'
# the read-only status (S6): a status read on any chip
fl_mutant fl_status_any_chip '/^bool flashlock_status/,/^}/s/^    if (info->jedec != FL_JEDEC_W25Q128) {$/    if (false) {/'
# an extra byte in a write frame (the board's controller clocks a byte in on every read of the data register, a write frame must not read)
fl_mutant fl_extra_byte_in_write 's/^    spi.send(v);$/    spi.send(v); spi.send(0);/'
echo "run_flashlock: ok"
