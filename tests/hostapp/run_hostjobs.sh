#!/usr/bin/env bash
# T192.2-S3/S4: the MultiCore update as a job (host_core_flow.cc: hc_update_job / hc_update_run) against a rig with no screen and a fake flash: the record of every case
# (pages, notes, flash counts) of the blocking call and of the stepped job equals the golden file captured before the flow was cut into steps; a job that asks writes nothing
# until it is answered.
#   tests/hostapp/run_hostjobs.sh [scratch dir] [--write-golden]
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
source "$ROOT/tests/hostgen.sh"   # T239.3 V-3b: CPATH holds hostio.h (IOBASE, U2P_IO_BASE) and basetab.h, generated from the TOML A of the host
H="$ROOT/host-app/software/hostapp"
HS="$ROOT/host-app/software"
T=""; W=""
for a in "$@"; do case "$a" in --write-golden) W="--write-golden" ;; *) T="$a" ;; esac; done
if [ -z "$T" ]; then T="$(mktemp -d)"; trap 'rm -rf "$T"' EXIT; fi
[ -f "$T/sha256.o" ] || cc -c -w -I"$H" -o "$T/sha256.o" "$H/sha256.c"
if [ ! -f "$HS/io/flash/flash.h" ]; then
  echo "SKIPPED test_hostjobs: vendor/gideon/software is missing (tools/gideon_sources.sh)"
  exit 0
fi
mkdir -p "$T/txu"
python3 "$ROOT/tests/hostapp/mktxutest.py" "$T/txu"
INC=(-I"$H" -I"$ROOT/app/video" -I"$HS/io/flash" -I"$HS/components" -I"$HS/infra" -I"$HS/system" -I"$HS/api" -I"$HS/filesystem" -I"$HS/io/c64")
srcs=("${HJ_FLOW:-$H/host_core_flow.cc}" "$H/txu.cc" "$H/flashfw.cc" "$H/cfw.cc" "$H/cor.cc" "$H/handover.cc" "$H/fwconfirm.cc" "$H/mediaui.cc" "$H/mediasniff.cc" "$H/fwknown.cc")
c++ -std=gnu++14 ${HJ_DEFS:-} -Wall -Wno-unused-function -Wno-sign-compare "${INC[@]}" -o "$T/test_hostjobs" "$ROOT/tests/hostapp/test_hostjobs.cc" "${srcs[@]}" "$T/sha256.o" 2>&1
"$T/test_hostjobs" "$T/txu" "${HJ_GOLDEN:-$ROOT/tests/hostapp/golden/hostjobs_update.txt}" $W

# the controls: a mutant of the flow has to go red (the golden file and the S3/S4 checks)
hj_mutant() {   # name, sed expression on host_core_flow.cc
  local name="$1" expr="$2" bad="$T/mut_$1.cc"
  sed -e "$expr" "$H/host_core_flow.cc" > "$bad"
  cmp -s "$bad" "$H/host_core_flow.cc" && { echo "CONTROL $name: the sed changed nothing" >&2; exit 1; }
  c++ -std=gnu++14 -w "${INC[@]}" -o "$T/mut_$name" "$ROOT/tests/hostapp/test_hostjobs.cc" "$bad" "$H/txu.cc" "$H/flashfw.cc" "$H/cfw.cc" "$H/cor.cc" "$H/handover.cc" "$H/fwconfirm.cc" "$H/mediaui.cc" "$H/mediasniff.cc" "$H/fwknown.cc" "$T/sha256.o" 2>&1
  if "$T/mut_$name" "$T/txu" "$ROOT/tests/hostapp/golden/hostjobs_update.txt" > "$T/mut_$name.log" 2>&1; then echo "CONTROL $name: the bench PASSED against the mutant" >&2; exit 1; fi
  grep -q '^FAIL' "$T/mut_$name.log" || { echo "CONTROL $name: it failed without a red check" >&2; exit 1; }
  echo "control $name: fails as it must ($(grep -c '^FAIL' "$T/mut_$name.log") check(s) red)"
}
if [ -z "$W" ] && [ -z "${HJ_FLOW:-}" ] && [ "${TEST_TIER:-}" != fast ]; then
  hj_mutant hj_ask_string 's/"This replaces %s in the flash."/"This replaces %s in the flash!"/'
  hj_mutant hj_write_without_answer 's/    if (!asub.go) {/    if (false) {/'
  hj_mutant hj_refusal_runs 's/        UP_REFUSE(title, chk.why, "This package does not fit the host it would run on.", "Use the update that matches this system.");/        state = HCJ_RUNNING;/'
fi
