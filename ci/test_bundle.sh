#!/usr/bin/env bash
# T172 bundle path, on fake inputs: (1) the version a package carries is stamped into the host app image at package time, whatever the cache held;
# (2) the update stage's sweep and SHA256SUMS filter leave trx64host_update_<sha8>.txu alone. Negative controls: the unstamped image shows the old
# version, and the old prefix-only pattern would delete the .txu. (3) the content check of the bundle, with its negative controls.
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

# (2) the sweep: what the update stage would delete / drop from SHA256SUMS
sha8=774497b1
printf '[{"name":"trx64host_update_%s_20261101.cfw"},{"name":"trx64host_update_%s_20261101.ue2"},{"name":"trx64host_update_%s.txu"},{"name":"c64_basic.bit"}]' $sha8 $sha8 $sha8 > "$W/files.json"
got="$(update_file_names < "$W/files.json" | tr '\n' ' ')"
case "$got" in *.txu*) fail "the sweep names the .txu: $got";; esac
[ "$got" = "trx64host_update_${sha8}_20261101.cfw trx64host_update_${sha8}_20261101.ue2 " ] || fail "sweep list: $got"
printf '%s  c64_basic.bit\n%s  trx64host_update_%s.txu\n%s  trx64host_update_%s_20261031.cfw\n' aa bb $sha8 cc $sha8 > "$W/sums"
update_sums_without_ours < "$W/sums" | grep -q "trx64host_update_$sha8.txu" || fail "the .txu line left SHA256SUMS"
update_sums_without_ours < "$W/sums" | grep -q '_20261031.cfw' && fail "an old .cfw line stayed"
# negative control: the old prefix-only pattern does hit the .txu
grep -vE ' (trx64_update_|trx64host_update_)' "$W/sums" | grep -q '\.txu' && fail "control is blind: the old pattern kept the .txu"
echo "sweep ok: .txu kept, old .cfw dropped; the old pattern would have dropped the .txu"

# (3) S5: the content check of the bundle (tools/check_bundle.py): a good fixture passes, each fault (a wrong bitstream in the .cfw or .txu, a wrong
# app, an old version in the app, a missing file, two different hosts) turns it red
python3 -I tools/check_bundle.py selftest
echo "bundle test ok"
