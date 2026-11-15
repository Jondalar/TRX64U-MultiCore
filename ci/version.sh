#!/usr/bin/env bash
# The version string of a build (T221 D1-D9, replacing T172 D2/D4): every deliverable has its own SemVer in its own file, and the string a display shows is
#   a release (tag v*)            X.Y.Z                 (D2/D4: a release is the tested build with its version strings rewritten, tools/release_promote.py)
#   any other build               X.Y.Z-dev+<sha8>      (D3: branch, integration and main builds alike)
#
#   ci/version.sh [bundle]        bundle.toml            the bundle: manifest.json, the .txu and the package (D17)
#   ci/version.sh hostapp         host-app/version.toml  the host menu
#   ci/version.sh coreapp         coreapp/version.toml   the generic core application
#   ci/version.sh host            contract/version.toml  the host gateware
#   ci/version.sh with <X.Y.Z>    a given base (a core's core.toml version) in the form of this build
# The kind of build is read from GITHUB_REF_TYPE (tag) else from the checkout: a checkout of a tag has no branch, so `git describe --exact-match`.
set -eu
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
what="${1:-bundle}"
# no python here: the build image of the apps (ci/trxapps.sh, rules.mk gitinfo) calls this script, and tools/check_versions.py is the strict check of the files
semver_re='^(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)$'
read_version() { sed -n 's/^[[:space:]]*version[[:space:]]*=[[:space:]]*"\([^"]*\)".*$/\1/p' "$1" | head -1; }
case "$what" in
  bundle) base="$(read_version "$ROOT/bundle.toml")";;
  hostapp) base="$(read_version "$ROOT/host-app/version.toml")";;
  coreapp) base="$(read_version "$ROOT/coreapp/version.toml")";;
  host) base="$(read_version "$ROOT/contract/version.toml")";;
  with) base="${2:?ci/version.sh with <X.Y.Z>}";;
  *) echo "ci/version.sh: unknown component '$what'" >&2; exit 2;;
esac
printf '%s' "$base" | grep -Eq "$semver_re" || { echo "ci/version.sh: '$base' is not x.y.z ($what)" >&2; exit 1; }
if [ "${GITHUB_REF_TYPE:-}" = tag ]; then printf '%s\n' "$base"; exit 0; fi
sha8="${TRX_VERSION_SHA8:-$(git -C "$ROOT" rev-parse --short=8 HEAD)}"
printf '%s-dev+%s\n' "$base" "$sha8"
