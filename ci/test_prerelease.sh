#!/usr/bin/env bash
# T239.1 D16: ci/prerelease.sh stages the package and the update files, refuses a TRX64 .cor, and passes a clean package (no publish). Unit and happy path.
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/.."
W="$(mktemp -d)"; trap 'rm -rf "${W:?}"' EXIT
fail() { echo "FAIL: $*"; exit 1; }
mkdir -p "$W/pkg" "$W/upd"
printf 'host bitstream stand-in\n' > "$W/pkg/c64_basic.bit"; printf '{}\n' > "$W/pkg/manifest.json"; printf 'a core\n' > "$W/pkg/vc20.cor"
printf 'update stand-in\n' > "$W/upd/trx64host_update_x.cfw"
run() { CI_PACKAGE_IN="$W/pkg" CI_UPDATE_IN="$W/upd" CI_STAGE="$W/stage" PRERELEASE_NO_PUBLISH=1 ci/prerelease.sh > "$W/out" 2>&1; }
run || { cat "$W/out"; fail "a clean package was refused"; }
for f in c64_basic.bit manifest.json vc20.cor trx64host_update_x.cfw; do [ -f "$W/stage/$f" ] || fail "$f is not staged"; done
printf 'x\n' > "$W/pkg/trx64u.cor"
if run; then fail "a package with trx64u.cor was accepted"; fi
grep -q "D16" "$W/out" || fail "the refusal does not name D16"
rm "$W/pkg/trx64u.cor"; printf 'x\n' > "$W/pkg/trx64g.cor"
if run; then fail "a package with trx64g.cor was accepted"; fi
rm "$W/pkg/trx64g.cor"; rm -rf "$W/pkg"/*
if run; then fail "an empty package was accepted"; fi
echo "prerelease staging: ok"
