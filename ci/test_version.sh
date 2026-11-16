#!/usr/bin/env bash
# T221 S1: ci/version.sh on fake refs. A branch, an integration branch and main show X.Y.Z-dev+<sha8>, a tag shows X.Y.Z; every component reads its own
# version file; a version file that is not SemVer is refused (negative controls: the old forms `<branch>@<sha8>` and `X.Y.Z+<sha8>` must not come back).
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/.."
fail() { echo "FAIL: $*"; exit 1; }
sha8="$(git rev-parse --short=8 HEAD)"
want() { [ "$1" = "$2" ] || fail "$3: got '$1', want '$2'"; }
b="$(python3 -I tools/semver.py get bundle.toml)"; h="$(python3 -I tools/semver.py get host-app/version.toml)"
c="$(python3 -I tools/semver.py get coreapp/version.toml)"; g="$(python3 -I tools/semver.py get contract/version.toml)"
for ref in t221-x integ-m1 main; do
  want "$(GITHUB_REF_NAME=$ref ci/version.sh)" "$b-dev+$sha8" "bundle on $ref"
  want "$(GITHUB_REF_NAME=$ref ci/version.sh hostapp)" "$h-dev+$sha8" "hostapp on $ref"
  want "$(GITHUB_REF_NAME=$ref ci/version.sh coreapp)" "$c-dev+$sha8" "coreapp on $ref"
  want "$(GITHUB_REF_NAME=$ref ci/version.sh host)" "$g-dev+$sha8" "host on $ref"
done
want "$(GITHUB_REF_NAME=v$b GITHUB_REF_TYPE=tag ci/version.sh)" "$b" "bundle on a tag"
want "$(GITHUB_REF_NAME=v$b GITHUB_REF_TYPE=tag ci/version.sh hostapp)" "$h" "hostapp on a tag"
want "$(GITHUB_REF_TYPE=tag ci/version.sh with 0.9.0)" "0.9.0" "a core on a tag"
want "$(ci/version.sh with 0.9.0)" "0.9.0-dev+$sha8" "a core on a branch"
want "$(TRX_VERSION_SHA8=deadbeef ci/version.sh with 1.4.0)" "1.4.0-dev+deadbeef" "the sha override"
# negative controls
for bad in "$(ci/version.sh)"; do
  case "$bad" in *@*) fail "the old branch@sha form is back: $bad";; esac
  case "$bad" in *-dev+*) ;; *) fail "no -dev pre-release in '$bad'";; esac
done
ci/version.sh with 8f1dc184 >/dev/null 2>&1 && fail "a commit hash was taken as a version"
ci/version.sh with 1.4 >/dev/null 2>&1 && fail "1.4 was taken as a version"
python3 -I tools/semver.py selftest
python3 -I tools/check_versions.py >/dev/null
echo "version ok: bundle $(ci/version.sh), tag form $b"
