#!/usr/bin/env bash
# T152 D8/S9: the inspection of a .cfw/.ue2 for an ESP32 image set (espinspect.cc). Files from mkesptest.py (the format of the vendor updater's embedded images),
# the real vendor files when they are on this machine (~/Downloads/update_v3.14.ue2 = V1.8, ~/Downloads/c64u_v1.1.0-2/c64u_v1.1.0.ue2 = V1.11; read only, never
# copied, never committed).
#   tests/hostapp/run_espinspect.sh [scratch dir]          (run.sh calls it with its own)
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
source "$ROOT/tests/hostgen.sh"   # T239.3 V-3b: CPATH holds hostio.h (IOBASE, U2P_IO_BASE) and basetab.h, generated from the TOML A of the host
H="$ROOT/host-app/software/hostapp"
T="${1:-}"
if [ -z "$T" ]; then T="$(mktemp -d)"; trap 'rm -rf "$T"' EXIT; fi
[ -f "$T/sha256.o" ] || cc -c -w -I"$H" -o "$T/sha256.o" "$H/sha256.c"
mkdir -p "$T/esp"
python3 "$ROOT/tests/hostapp/mkesptest.py" "$T/esp"
real=()
[ -f "$HOME/Downloads/update_v3.14.ue2" ] && real+=("$HOME/Downloads/update_v3.14.ue2" "V1.8")
[ -f "$HOME/Downloads/c64u_v1.1.0-2/c64u_v1.1.0.ue2" ] && real+=("$HOME/Downloads/c64u_v1.1.0-2/c64u_v1.1.0.ue2" "V1.11")
[ "${#real[@]}" -gt 0 ] || echo "real vendor files: not on this machine, skipped"
TST="$ROOT/tests/hostapp/test_espinspect.cc"
c++ -std=gnu++14 -Wall -I"$H" -o "$T/test_espinspect" "$TST" "$H/espinspect.cc" "$T/sha256.o" 2>&1
"$T/test_espinspect" "$T/esp" ${real[@]+"${real[@]}"}
echo "run_espinspect: ok"
