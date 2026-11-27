#!/usr/bin/env bash
# T192.2-S10 (no radio): the WiFi page's modal actions (access point list, manual entry, authentication list, password, static address) as jobs
# (host_core_flow_wifi.cc: hc_wifi_key) against a rig with no screen and a module model: the record of every scenario of the blocking call and of the stepped job equals the
# golden file captured before the flows were cut into steps.
#   tests/hostapp/run_hostwifi.sh [scratch dir] [--write-golden]
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
  echo "SKIPPED test_hostwifi: vendor/gideon/software is missing (tools/gideon_sources.sh)"
  exit 0
fi
INC=(${HJ_INC:+-I"$HJ_INC"} -I"$H" -I"$ROOT/app/video" -I"$ROOT/app/edit" -I"$HS/io/flash" -I"$HS/components" -I"$HS/infra" -I"$HS/system" -I"$HS/api" -I"$HS/filesystem" -I"$HS/io/c64")
srcs=("${HJ_WIFI:-$H/host_core_flow_wifi.cc}" "${HJ_FLOW:-$H/host_core_flow.cc}" "$H/txu.cc" "$H/guard.cc" "$H/flashfw.cc" "$H/cfw.cc" "$H/cor.cc" "$H/handover.cc" "$H/fwconfirm.cc" "$H/mediaui.cc" "$H/mediasniff.cc"
      "$H/fwknown.cc" "$ROOT/app/edit/lineedit.cc")
c++ -std=gnu++14 ${HJ_DEFS:-} -Wall -Wno-unused-function -Wno-sign-compare "${INC[@]}" -o "$T/test_hostwifi" "$ROOT/tests/hostapp/test_hostwifi.cc" "${srcs[@]}" "$T/sha256.o" 2>&1
"$T/test_hostwifi" "${HW_GOLDEN:-$ROOT/tests/hostapp/golden/hostwifi.txt}" $W

# the controls (the WiFi store is a persistence operation): a mutant of the flow has to go red
hw_mutant() {   # name, sed expression on host_core_flow_wifi.cc
  local name="$1" expr="$2" bad="$T/mutw_$1.cc"
  sed -e "$expr" "$H/host_core_flow_wifi.cc" > "$bad"
  cmp -s "$bad" "$H/host_core_flow_wifi.cc" && { echo "CONTROL $name: the sed changed nothing" >&2; exit 1; }
  c++ -std=gnu++14 -w "${INC[@]}" -o "$T/mutw_$name" "$ROOT/tests/hostapp/test_hostwifi.cc" "$bad" "${srcs[@]:1}" "$T/sha256.o" 2>&1
  if "$T/mutw_$name" "$ROOT/tests/hostapp/golden/hostwifi.txt" > "$T/mutw_$name.log" 2>&1; then echo "CONTROL $name: the bench PASSED against the mutant" >&2; exit 1; fi
  grep -q '^FAIL' "$T/mutw_$name.log" || { echo "CONTROL $name: it failed without a red check" >&2; exit 1; }
  echo "control $name: fails as it must ($(grep -c '^FAIL' "$T/mutw_$name.log") check(s) red)"
}
if [ -z "$W" ] && [ -z "${HJ_WIFI:-}" ] && [ "${TEST_TIER:-}" != fast ]; then
  hw_mutant hw_cancel_saves 's/if (!esub.saved) {/if (false) {/'
  hw_mutant hw_password_shown 's/e.len ? "\*\*\*\*\*\*\*\*" : ""/e.buf/'
fi
