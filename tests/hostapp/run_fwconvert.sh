#!/usr/bin/env bash
# T137 S1: the .cfw/.ue2 -> .cor conversion (fwconvert.cc), the table of official firmware (fwknown.cc, known_fw.h) and the manifest's origin (cor.cc):
# packages from mkconvtest.py (the real writer tools/cor_pack.py), a RAM disk for the medium.
#   tests/hostapp/run_fwconvert.sh [scratch dir]          (run.sh calls it with its own)
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
source "$ROOT/tests/hostgen.sh"   # T239.3 V-3b: CPATH holds hostio.h (IOBASE, U2P_IO_BASE) and basetab.h, generated from the TOML A of the host
H="$ROOT/host-app/software/hostapp"
T="${1:-}"
if [ -z "$T" ]; then T="$(mktemp -d)"; trap 'rm -rf "$T"' EXIT; fi
[ -f "$T/sha256.o" ] || cc -c -w -I"$H" -o "$T/sha256.o" "$H/sha256.c"
mkdir -p "$T/fwt"
python3 "$ROOT/tests/hostapp/mkconvtest.py" "$T/fwt"
FWCT="$ROOT/tests/hostapp/test_fwconvert.cc"
# the sources the bench links
srcs=("$H/fwconvert.cc" "$H/fwknown.cc" "$H/cfw.cc" "$H/cor.cc" "$H/handover.cc")
c++ -std=gnu++14 -Wall -Wno-unused-function -I"$H" -o "$T/test_fwconvert" "$FWCT" "${srcs[@]}" "$T/sha256.o" 2>&1
"$T/test_fwconvert" "$T/fwt"
python3 "$ROOT/tools/cor_pack.py" check "$T/fwt/converted.cor" > /dev/null          # the packer's own reader takes what the host wrote
# what the firmware's printf cannot do (seen in ue2emu 2026-10-29: "%.8s" printed the whole string, "%08x" printed capitals, "%%" printed two): not in the files that
# make text a reader parses or a person reads
if grep -nE '%%|%\.[0-9*]+s|%0[0-9]x' "$H/fwconvert.cc" "$H/fwknown.cc" | grep -vE '^[^:]+:[0-9]+:[[:space:]]*//'; then echo "run_fwconvert: a format the firmware's printf gets wrong (above)" >&2; exit 1; fi
echo "run_fwconvert: ok"
