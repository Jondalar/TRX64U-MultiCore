#!/usr/bin/env bash
# T184 D3 S3: the core application reads the host's "WiFi settings" from /Flash/trx/host.cfg (hostwifi.cc, test_hostwifi.cc) against the real ConfigManager.
# Controls: the loader does nothing -> the host's static address is lost (DHCP default); the loader writes nothing is checked by the test (FakeFs writes).
#   tests/coreapp/run_hostwifi.sh [scratch dir]
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
HS="$ROOT/host-app/software"
source "$ROOT/tests/hostgen.sh"   # T239.3 V-3b: CPATH holds hostio.h and basetab.h, generated from the TOML A of the host
T="${1:-}"
if [ -z "$T" ]; then T="$(mktemp -d)"; trap 'rm -rf "$T"' EXIT; fi
T="$T/hostwifi"; mkdir -p "$T"
INC=(-I"$ROOT/tests/hostapp/cfgstub" -I"$ROOT/tests/hostapp/stubfs" -I"$ROOT/tests/coreapp" -I"$ROOT/coreapp/src" -I"$HS/components" -I"$HS/filesystem" -I"$HS/filemanager" -I"$HS/io/flash"
     -I"$HS/infra" -I"$HS/system" -I"$HS/userinterface")
TEST="$ROOT/tests/coreapp/test_hostwifi.cc"
srcs=("$HS/components/config.cc" "$HS/components/mystring.cc" "$ROOT/coreapp/src/hostwifi.cc" "$ROOT/coreapp/src/cfgfile.cc")
c++ -std=gnu++14 -w -g -DU64=2 -DRUNS_ON_PC "${INC[@]}" -o "$T/test_hostwifi" "$TEST" "${srcs[@]}" 2>&1 | head -30
"$T/test_hostwifi" | tail -3
"$T/test_hostwifi" | grep -q ' 0 failed' || { echo "run_hostwifi: the test is red" >&2; exit 1; }

hw_mutant() {   # name, source (full path), sed expression
    local name="$1" src="$2" expr="$3" bad="$T/mut_$1_$(basename "$2")" list=()
    sed -e "$expr" "$src" > "$bad"
    cmp -s "$bad" "$src" && { echo "CONTROL $name: the edit changed nothing" >&2; exit 1; }
    for f in "${srcs[@]}"; do if [ "$f" = "$src" ]; then list+=("$bad"); else list+=("$f"); fi; done
    c++ -std=gnu++14 -w -g -DU64=2 -DRUNS_ON_PC "${INC[@]}" -I"$ROOT/coreapp/src" -o "$T/mut_$name" "$TEST" "${list[@]}" 2>&1 | head -20
    if "$T/mut_$name" > "$T/mut_$name.log" 2>&1; then echo "CONTROL $name: the test PASSED against the mutant" >&2; exit 1; fi
    grep -q '^FAIL' "$T/mut_$name.log" || { echo "CONTROL $name: it failed without a red check" >&2; exit 1; }
    echo "control $name: fails as it must ($(grep -c '^FAIL' "$T/mut_$name.log") check(s) red)"
}
W="$ROOT/coreapp/src/hostwifi.cc"
hw_mutant loader_disabled "$W" 's/    cfgfile_parse(text, len, item, &c);/    (void)text; (void)len;/'
hw_mutant radio_taken "$W" 's/    if (!known) {/    if (false) {/'
hw_mutant not_effectuated "$W" 's/        s->effectuate();/        (void)0;/'
hw_mutant other_store "$W" 's/!cfgfile_label_equal(store, "WiFi settings") || //'
echo "run_hostwifi: ok"
