#!/usr/bin/env bash
# T172 bundle path, on fake inputs: (1) the version a package carries is stamped into the host app image at package time, whatever the cache held;
# (2) the content check of the bundle, with its negative controls. Negative control of (1): the unstamped image shows the old version.
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/.."
source ci/lib.sh
W="$(mktemp -d)"; trap 'rm -rf "${W:?}"' EXIT
fail() { echo "FAIL: $*"; exit 1; }

# (1) the real gitinfo rule of the apps, compiled into a probe, stamped like ci/package.sh does
H=host-app/target/u64ii/riscv/hostapp
mkdir -p "$W/out"
make -s -C "$H" gitinfo OUTPUT="$W/out" >/dev/null 2>&1 || fail "make gitinfo"
cat > "$W/probe.cc" <<'EOF'
#include <cstdio>
#include "gitinfo.h"
int main() { printf("%s\n", APP_VERSION_STRING); return 0; }
EOF
cc -O2 -c -I"$W/out" -o "$W/gitinfo.o" "$W/out/gitinfo.c" && c++ -O2 -I"$W/out" -o "$W/probe" "$W/probe.cc" "$W/gitinfo.o"
built="$("$W/probe")"
[ "$built" = "$(ci/version.sh hostapp)" ] || fail "the build-time string '$built' is not ci/version.sh hostapp"
cp "$W/probe" "$W/cached"                        # "the cache entry": built by an earlier commit, version old
python3 -I tools/stamp_version.py stamp "$W/probe" --version "t164@774497b1" >/dev/null
[ "$(uname)" != Darwin ] || codesign -f -s - "$W/probe" 2>/dev/null   # a patched Mach-O loses its signature; the RISC-V image has none
[ "$("$W/probe")" = "t164@774497b1" ] || fail "stamped image still shows '$("$W/probe")'"
[ "$("$W/cached")" = "$built" ] || fail "the cached image changed"
echo "stamp ok: cached image shows '$built', the package copy '$("$W/probe")'"
python3 -I tools/stamp_version.py selftest

# (2) S5: the content check of the bundle (tools/check_bundle.py): a good fixture passes, each fault (a wrong bitstream in the .cfw or .txu, a wrong
# app, an old version in the app, a missing file, two different hosts) turns it red
python3 -I tools/check_bundle.py selftest
echo "bundle test ok"
