#!/usr/bin/env bash
# T239.1 D42: ci/prerelease.sh refuses what it cannot make a release bundle from - an empty package, no update artifact, a package without its .txu,
# c64_basic.bit, hostapp.app or manifest.json, two .cfw. Unit only: the bundle itself (tools/make_release_mc.sh and the checks of the release) is proven
# with the artifacts of a real run (T239.1 §5, step 17); tools/test_make_release_mc.py tests the bundle script.
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/.."
W="$(mktemp -d)"; trap 'rm -rf "${W:?}"' EXIT
fail() { echo "FAIL: $*"; exit 1; }
fill() {
  local f
  rm -rf "$W/pkg" "$W/upd"; mkdir -p "$W/pkg" "$W/upd"
  for f in c64_basic.bit hostapp.app vc20.cor trx64host_update_x.txu; do printf 'stand-in\n' > "$W/pkg/$f"; done
  printf '{"trx64_commit": "0123456789abcdef", "version": "0.0.0-dev+01234567"}\n' > "$W/pkg/manifest.json"
  printf 'stand-in\n' > "$W/upd/trx64host_update_x.cfw"; printf 'stand-in\n' > "$W/upd/trx64host_update_x.ue2"
}
run() { CI_PACKAGE_IN="$W/pkg" CI_STAGE="$W/stage" PRERELEASE_NO_PUBLISH=1 "$@" ci/prerelease.sh > "$W/out" 2>&1; }
expect() { # expect <text the refusal must name> <what>
  if run env CI_UPDATE_IN="$W/upd"; then fail "$2 was accepted"; fi
  grep -q -- "$1" "$W/out" || { cat "$W/out"; fail "the refusal of $2 does not say '$1'"; }
}
fill; rm -rf "$W/pkg"/*; expect "is empty" "an empty package"
fill; if run env -u CI_UPDATE_IN; then fail "a run without the update artifact was accepted"; fi
grep -q "CI_UPDATE_IN" "$W/out" || fail "the refusal does not name CI_UPDATE_IN"
fill; rm "$W/pkg/trx64host_update_x.txu"; expect "exactly one .txu" "a package without its .txu"
fill; printf 'x\n' > "$W/upd/trx64host_update_y.cfw"; expect "exactly one .cfw" "two .cfw"
for f in c64_basic.bit hostapp.app manifest.json; do fill; rm "$W/pkg/$f"; expect "has no $f" "a package without $f"; done
fill; rm "$W/pkg/vc20.cor"; expect "no .cor" "a package without a .cor"
echo "prerelease refusals: ok"
