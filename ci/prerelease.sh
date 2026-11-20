#!/usr/bin/env bash
# T239.1 D1, D16, gap 25: the last job of a push to main, t239.1 or a v* tag. It stages what the run made (the package, the update files), refuses what a CI
# pre-release must not hold, writes the SHA256SUMS of everything staged (the package's own was made before the update files existed), and only then publishes it
# (publish_files of ci/lib.sh: ci/publish_release.py, the draft release of the builds repository, env BUILDS_TOKEN).
#
#   ci/prerelease.sh        env: CI_PACKAGE_IN (the directory of the package artifact), CI_UPDATE_IN (the update artifact; optional: no update, no files),
#                           CI_STAGE (default ./prerelease-stage), PRERELEASE_NO_PUBLISH=1 (tests: stage and check, publish nothing)
#   - no trx64u.cor and no trx64g.cor (D16: TRX64 is built by TRX64-Ultimate; the hand-made release packs both on the Mac, D22)
#   - tools/check_public_tree.py --files on the staged directory WITHOUT --allow-trx: no ROM byte of the private list in any file or archive member
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/.." && pwd)"
: "${CI_PACKAGE_IN:?CI_PACKAGE_IN: the directory of the package artifact}"
stage="${CI_STAGE:-$PWD/prerelease-stage}"
[ -d "$CI_PACKAGE_IN" ] && [ -n "$(ls -A "$CI_PACKAGE_IN")" ] || { echo "prerelease: $CI_PACKAGE_IN is empty: the package job left nothing" >&2; exit 1; }
rm -rf "$stage"; mkdir -p "$stage"
cp -R "$CI_PACKAGE_IN"/. "$stage"/
if [ -n "${CI_UPDATE_IN:-}" ] && [ -d "$CI_UPDATE_IN" ]; then cp -R "$CI_UPDATE_IN"/. "$stage"/; fi
for f in trx64u.cor trx64g.cor; do
  [ ! -e "$stage/$f" ] || { echo "prerelease: $f is in the package: a CI pre-release carries no TRX64 (T239.1 D16)" >&2; exit 1; }
done
python3 -I "$ROOT/tools/check_public_tree.py" --files "$stage"
rm -f "$stage/SHA256SUMS"
(cd "$stage" && sums="$(sha256sum -- *)" && printf '%s\n' "$sums" > SHA256SUMS)       # (a directory among the files makes sha256sum fail)
echo "prerelease: $(find "$stage" -type f | wc -l | tr -d ' ') files staged in $stage, no TRX64 .cor, no private ROM content"
[ -z "${PRERELEASE_NO_PUBLISH:-}" ] || { echo "prerelease: PRERELEASE_NO_PUBLISH set, nothing published"; exit 0; }
source "$HERE/lib.sh"
publish_files "$stage"
