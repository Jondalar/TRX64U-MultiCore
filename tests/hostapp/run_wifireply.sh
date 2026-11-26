#!/usr/bin/env bash
# T184 D4: the WiFi command channel of the host app's driver (io/wifi/wifi_cmd.cc, wifi_reply.h, wifi_scan_result.h, wifi_connection_feedback.h) on the host:
# the reply table (slot + sequence), wifi_cmd.cc against stubs of the UART and the RTOS, the bounded scan decode, the connect feedback.
# The tests are the fork's (omnicron-c64/ultimate-community-edition f7f665ab, software/test/wifi_reply).
#   tests/hostapp/run_wifireply.sh [scratch dir]          (run.sh calls it with its own)
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
source "$ROOT/tests/hostgen.sh"   # T239.3 V-3b: CPATH holds hostio.h (IOBASE, U2P_IO_BASE) and basetab.h, generated from the TOML A of the host
HS="$ROOT/host-app/software"
TD="$ROOT/tests/hostapp/wifireply"
T="${1:-}"
if [ -z "$T" ]; then T="$(mktemp -d)"; trap 'rm -rf "$T"' EXIT; fi
T="$T/wifireply"; mkdir -p "$T"
W="$HS/io/wifi"
CXXF=(-std=c++11 -g -O0 -w -DRISCV=1 -DU64=2)
SAN=(-fsanitize=address,undefined -fno-omit-frame-pointer)
# like run.sh: the builder image cannot start ASan binaries (runtime not first in the library list); then the tests run unsanitized
_p="$(mktemp -d)"
if ! { echo 'int main(){return 0;}' | c++ "${SAN[@]}" -x c++ - -o "$_p/probe" 2>/dev/null && "$_p/probe" >/dev/null 2>&1; }; then SAN=(); fi
rm -rf "$_p"

# $1 = dir of mutated headers (first on the include path, may be empty), $2 = wifi_cmd.cc to build, $3 = out dir
build_all() {
    local mh="$1" cmdcc="$2" out="$3"; mkdir -p "$out"
    local INC=(-I"$mh" -I"$TD/stubs" -I"$W" -I"$HS/io/uart" -I"$HS/u64ctrl/main" -I"$HS/system" -I"$TD")
    c++ "${CXXF[@]}" "${INC[@]}" -o "$out/wifi_cmd" "$cmdcc" "$TD/wifi_cmd_test.cc" 2>&1 | head -20
    c++ -std=c++11 -g -w "${SAN[@]}" -I"$mh" -I"$TD/stubs" -I"$W" -I"$HS/u64ctrl/main" -o "$out/wifi_scan" "$TD/wifi_scan_test.cc" 2>&1 | head -20
    c++ -std=c++11 -g -w "${SAN[@]}" -I"$mh" -I"$W" -o "$out/wifi_feedback" "$TD/wifi_feedback_test.cc" 2>&1 | head -20
    cc -std=c99 -g -O0 -Wall -Wextra -Wno-unused-parameter -I"$mh" -I"$W" -o "$out/wifi_reply" "$TD/wifi_reply_test.c" 2>&1 | head -20
}
run_all() {   # exit status of the first test that is red
    local out="$1" rc=0
    "$out/wifi_feedback" || rc=1
    "$out/wifi_scan" || rc=1
    "$out/wifi_reply" || rc=1
    "$out/wifi_cmd" > "$out/wifi_cmd.log" || { grep '^FAIL' "$out/wifi_cmd.log"; rc=1; }
    tail -n 1 "$out/wifi_cmd.log"
    return $rc
}
mkdir -p "$T/none"
build_all "$T/none" "$W/wifi_cmd.cc" "$T/real"
run_all "$T/real" || { echo "run_wifireply: the tests are red" >&2; exit 1; }

echo "run_wifireply: ok"
