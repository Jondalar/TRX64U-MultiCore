#!/usr/bin/env bash
# T197 P3 / S3: Unzip in the host's File Browser through hc_zip_job (host_core_flow_zip.cc), the loader of coreapp/src and the Mac build of apps/unzip + apps/miniz
# (the fixtures are tests/coreapp/gen_unzip.py's). No negative control: nothing here writes the flash.
#   tests/hostapp/run_hostzip.sh [scratch dir]
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
source "$ROOT/tests/hostgen.sh"   # T239.3 V-3b: CPATH holds hostio.h (IOBASE, U2P_IO_BASE) and basetab.h, generated from the TOML A of the host
H="$ROOT/host-app/software/hostapp"
HS="$ROOT/host-app/software"
SRC="$ROOT/coreapp/src"
T="${1:-}"
if [ -z "$T" ]; then T="$(mktemp -d)"; trap 'rm -rf "$T"' EXIT; fi
if [ ! -f "$HS/io/flash/flash.h" ]; then
  echo "SKIPPED test_hostzip: vendor/gideon/software is missing (tools/gideon_sources.sh)"
  exit 0
fi
mkdir -p "$T/fx" "$T/o"
python3 -I "$ROOT/tests/coreapp/gen_unzip.py" "$T/fx" "$ROOT" > /dev/null
INC=(-I"$H" -I"$SRC" -I"$ROOT/app/video" -I"$ROOT/tests/coreapp" -I"$ROOT/app/sdk" -I"$ROOT/apps/unzip/src" -I"$ROOT/apps/miniz/src" -I"$ROOT/apps/miniz" -I"$ROOT/apps/unzip"
     -I"$HS/io/flash" -I"$HS/components" -I"$HS/infra" -I"$HS/system" -I"$HS/api" -I"$HS/filesystem" -I"$HS/io/c64")
[ -f "$T/sha256.o" ] || cc -c -w -I"$H" -o "$T/sha256.o" "$H/sha256.c"
objs=()
for f in "$ROOT/apps/unzip/src/unzip.c" "$ROOT/apps/miniz/src/miniz.c" "$ROOT/apps/miniz/src/miniz_tinfl.c" "$ROOT/apps/miniz/src/miniz_zip.c" "$ROOT/app/sdk/u2p_compat.c"; do
  [ -f "$f" ] || f="$(find "$ROOT/apps/miniz" "$ROOT/apps/unzip" "$ROOT/app/sdk" -name "$(basename "$f")" | head -1)"
  o="$T/o/$(basename "$f").o"
  cc -std=gnu99 -w -c "${INC[@]}" "$f" -o "$o"
  objs+=("$o")
done
srcs=("$H/host_core_flow_zip.cc" "$H/host_core_flow.cc" "$H/txu.cc" "$H/guard.cc" "$H/flashfw.cc" "$H/cfw.cc" "$H/cor.cc" "$H/handover.cc" "$H/fwconfirm.cc" "$H/mediaui.cc" "$H/mediasniff.cc" "$H/fwknown.cc"
      "$SRC/appindex.cc" "$SRC/libload.cc" "$SRC/libscan.cc" "$SRC/libmenu.cc" "$SRC/descrec.cc" "$SRC/overlay.cc" "$SRC/ops.cc" "$SRC/corefolder.cc"
      "$H/hostlibslot.cc" "$ROOT/apps/miniz/miniz_lib.cc" "$ROOT/apps/unzip/glue.cc")
c++ -std=gnu++14 -Wall -Wno-unused-function -Wno-sign-compare -Wno-inconsistent-missing-override -fno-exceptions "${INC[@]}" -o "$T/test_hostzip" "$ROOT/tests/hostapp/test_hostzip.cc" "${srcs[@]}" "${objs[@]}" "$T/sha256.o" 2>&1
"$T/test_hostzip" "$T/fx"
