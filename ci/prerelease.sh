#!/usr/bin/env bash
# T239.1 D1, D42, D43, gap 25: the last job of a push to main, t239.1 or a v* tag. Its pre-release of the builds repository holds what a release holds (D42,
# as v0.2.0): the bundle of tools/make_release_mc.sh - every .cor of the package (trx64u.cor and trx64g.cor as well when the package has them, D43), the update
# files .cfw and .ue2 (artifact `update`), the package's .txu, the .u2a apps the updater copies into <cores>/apps, INSTALL.md, ROMS.txt and SHA256SUMS - and
# the zip of that bundle. No report, log, bitstream or ELF goes in: the bit-<entry> releases hold those. publish_files of ci/lib.sh publishes it
# (ci/publish_release.py: a draft release of the builds repository, env BUILDS_TOKEN, published when every digest matches).
#
#   ci/prerelease.sh        env: CI_PACKAGE_IN (the directory of the package artifact), CI_UPDATE_IN (the directory of the update artifact),
#                           CI_STAGE (default ./prerelease-stage), PRERELEASE_NO_PUBLISH=1 (tests: stage and check, publish nothing)
# The bundle's id is the version on a v* tag (manifest.json "version" x.y.z), else the pre-release tag <branch>-<8 hex of the commit>.
# Checks before anything is published, the ones tools/release_mc.sh made for a release:
#   - tools/check_release_flash.py: the installer writes the flash of the package's .txu (the tested one), never c64_basic.bit;
#   - tools/release_check.py zip: the zip holds every file of the bundle;
#   - tools/check_public_tree.py --allow-trx --files on the bundle and the zip: only trx64u.cor and trx64g.cor may carry what the fourth exception allows.
# The stage is the bundle and the zip; its SHA256SUMS is the bundle's with a line for the zip added (the one inside the zip cannot name the zip).
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$HERE/lib.sh"
: "${CI_PACKAGE_IN:?CI_PACKAGE_IN: the directory of the package artifact}"
: "${CI_UPDATE_IN:?CI_UPDATE_IN: the directory of the update artifact}"
P="$CI_PACKAGE_IN"
stage="${CI_STAGE:-$PWD/prerelease-stage}"
die() { echo "prerelease: $*" >&2; exit 1; }
[ -d "$P" ] && [ -n "$(ls -A "$P")" ] || die "$P is empty: the package job left nothing"
# one <glob> <what>: the one file the glob names, else stop
one() { local m; m=( $1 ); [ "${#m[@]}" = 1 ] && [ -s "${m[0]}" ] || die "not exactly one $2 ($1)"; printf '%s\n' "${m[0]}"; }
cfw="$(one "$CI_UPDATE_IN/trx64host_update_*.cfw" ".cfw in the update artifact")"
ue2="$(one "$CI_UPDATE_IN/trx64host_update_*.ue2" ".ue2 in the update artifact")"
txu="$(one "$P/trx64host_update_*.txu" ".txu in the package")"
for f in c64_basic.bit hostapp.app manifest.json; do [ -s "$P/$f" ] || die "the package has no $f"; done
read -r bitc appc ver < <(python3 -I -c 'import json, sys
m = json.load(open(sys.argv[1]))
print(m["trx64_commit"][:8], (m.get("trxapps_commit") or m["trx64_commit"])[:8], m.get("version") or "-")' "$P/manifest.json")
if [[ "$ver" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]]; then id="$ver"; else id="$(branch_slug)-$(git -C "$ROOT" rev-parse --short=8 HEAD)"; fi
cors=(); for c in "$P"/*.cor; do [ -e "$c" ] && cors+=(--cor "$c"); done
[ "${#cors[@]}" -gt 0 ] || die "the package holds no .cor"
u2as=(); for u in "$P"/*.u2a; do [ -e "$u" ] && u2as+=(--u2a "$u"); done
W="$(mktemp -d)"; trap 'rm -rf "${W:?}"' EXIT
# --bit c64_basic.bit, as tools/release_mc.sh gave it: the host key every .cor is checked against, and the bitstream INSTALL.md describes
bash "$CI_CODE/tools/make_release_mc.sh" --id "$id" --bit "$P/c64_basic.bit" --app "$P/hostapp.app" --bit-commit "$bitc" --app-commit "$appc" \
  --update "$cfw" --update-ue2 "$ue2" --txu "$txu" ${u2as[@]+"${u2as[@]}"} "${cors[@]}" --out "$W" || die "tools/make_release_mc.sh made no bundle"
D="$W/trx64mc-$id"; ZIP="$W/trx64mc-$id.zip"
[ -d "$D" ] && [ -s "$ZIP" ] || die "no bundle $D or no zip $ZIP"
python3 "$CI_CODE/tools/check_release_flash.py" --txu "$txu" --reject "$P/c64_basic.bit" "$D"/trx64host_update_*.cfw "$D"/trx64host_update_*.ue2 \
  || die "the installer is not the flash of the package's .txu"
python3 "$CI_CODE/tools/release_check.py" zip "$D" "$ZIP" || die "the zip does not hold every file of the bundle"
python3 -I "$CI_CODE/tools/check_public_tree.py" --allow-trx --files "$D" "$ZIP" || die "private content outside trx64u.cor and trx64g.cor"
rm -rf "$stage"; mkdir -p "$stage"
cp "$D"/* "$ZIP" "$stage"/
(cd "$stage" && sha256sum -- "$(basename "$ZIP")" >> SHA256SUMS && sha256sum -c --quiet SHA256SUMS) || die "SHA256SUMS does not match the staged files"
echo "prerelease: $(ls "$stage" | wc -l | tr -d ' ') files staged in $stage ($(ls "$stage" | grep -c '\.cor$') .cor and trx64mc-$id.zip), every check passed"
[ -z "${PRERELEASE_NO_PUBLISH:-}" ] || { echo "prerelease: PRERELEASE_NO_PUBLISH set, nothing published"; exit 0; }
publish_files "$stage"
