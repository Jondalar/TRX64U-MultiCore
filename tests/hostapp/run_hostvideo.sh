#!/usr/bin/env bash
# T164 WP17 (S10, S11, S12, Q7): the host application's picture (hostvideo.h/.cc) against a fake screen, a fake mode-request gateware and a fake host.cfg
# (test_hostvideo.cc). Controls only for what is stored (hv_saved_store, T174 D5.4).
#   tests/hostapp/run_hostvideo.sh [scratch dir]          (run.sh calls it with its own)
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
source "$ROOT/tests/hostgen.sh"   # T239.3 V-3b: CPATH holds hostio.h (IOBASE, U2P_IO_BASE) and basetab.h, generated from the TOML A of the host
H="$ROOT/host-app/software/hostapp"
V="$ROOT/app/video"
T="${1:-}"
if [ -z "$T" ]; then T="$(mktemp -d)"; trap 'rm -rf "$T"' EXIT; fi
T="$T/hostvideo"; mkdir -p "$T"
TEST="$ROOT/tests/hostapp/test_hostvideo.cc"
build() {   # out, hostvideo.h dir, hostvideo.cc
    c++ -std=gnu++11 -w -DVIDEOMODE_NO_BOARD=1 -I"$2" -I"$H" -I"$V" -o "$1" "$TEST" "$3" "$V/videomode.cc" "$V/edid.cc" 2>&1 | head -20
}
build "$T/test_hostvideo" "$H" "$H/hostvideo.cc"
"$T/test_hostvideo" | grep -v '^\[video\]' | tail -4
"$T/test_hostvideo" | grep -q ' 0 failed' || { echo "run_hostvideo: the test is red" >&2; exit 1; }

hv_mutant() {   # name, file (hostvideo.h or hostvideo.cc), sed expression
    local name="$1" file="$2" expr="$3" d="$T/mut_$1"
    mkdir -p "$d"
    cp "$H/hostvideo.h" "$H/hostvideo.cc" "$d/"
    sed -e "$expr" "$H/$file" > "$d/$file"
    cmp -s "$d/$file" "$H/$file" && { echo "CONTROL $name: the edit changed nothing" >&2; exit 1; }
    c++ -std=gnu++11 -w -DVIDEOMODE_NO_BOARD=1 -I"$d" -I"$H" -I"$V" -o "$d/t" "$TEST" "$d/hostvideo.cc" "$V/videomode.cc" "$V/edid.cc" 2>&1 | head -20
    if "$d/t" > "$d/log" 2>&1; then echo "CONTROL $name: the test PASSED against the mutant" >&2; exit 1; fi
    grep -q '^FAIL' "$d/log" || { echo "CONTROL $name: it failed without a red check" >&2; exit 1; }
    echo "control $name: fails as it must ($(grep -c '^FAIL' "$d/log") check(s) red)"
}
# Q7: a key keeps the picture but does not store it
hv_mutant q7_keep_not_stored hostvideo.cc 's/^    hv_saved_store(st.def_hdmi, st.def_sys);/    (void)0;/'
# Q7: the choice is stored when it is made, before the key
hv_mutant q7_stored_early hostvideo.cc 's/        st.pend_def = def;/        st.pend_def = def; hv_saved_store(def, st.def_sys);/'
# picsave: a key keeps the picture but the file gets nothing (the reboot finds nothing)
hv_mutant picsave_keep_not_stored hostvideo.cc 's/^    hv_saved_store(st.def_hdmi, st.def_sys);/    (void)0;/'
echo "run_hostvideo: ok"
