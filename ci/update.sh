#!/usr/bin/env bash
# Stage `update`: the host update file of this run, from exactly the parts of this commit's CI package and
# added to the same package version (<branch>-<8 digit commit> and <branch>-latest of $MC_PKG),
# with its SHA256SUMS extended. Runs in u64-build-env:ci after `package`. Gideon's application is not built or
# packed here any more (T164, Alex 2026-11-01): the monolith updater trx64_update_* is TRX64-Ultimate's.
#   trx64host_update_<commit8>_<date>.cfw/.ue2  the MultiCore file: null_full.bit (the same host + the null core, cores/null,
#                                               T123: the flash holds no machine) + our boot application from stage
#                                               trxapps; tools/release_mc.sh attaches it to the release (T94 P6)
#
# Only for integ-m1, main and v* tags (ci/select.sh update); any other ref prints
# "update SKIPPED" and ends green at once. The decision is made here, never by a job-level `if:`
# (TRX64-Ultimate/docs/specs/T31-ci-pipeline.md, decision 2). UPDATE_FORCE=true (dispatch input `update`), BUNDLE_INPUT=true (dispatch input `bundle`)
# or "[bundle]" in the head commit message (T172) builds on any branch, into $MC_PKG as <branch>-<sha8>.
#
# Hook for the release tag: TRX_UPDATE_NAME replaces the file name base (tools/make_update.sh); the
# tag logic itself is not built here.
#
#   PACKAGE_TOKEN=... ci/update.sh
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/lib.sh"

if [ "$("$ROOT/ci/select.sh" update)" != yes ]; then
  echo "update SKIPPED: only integ-m1/main (and v* tags); put [bundle] in the head commit or dispatch with bundle = true to build on this branch"
  exit 0
fi
: "${PACKAGE_TOKEN:?PACKAGE_TOKEN is not set: the package cannot be read or extended}"
: "${GITHUB_SERVER_URL:?}" "${GITHUB_REPOSITORY:?}"

OWNER="${GITHUB_REPOSITORY%%/*}"
API="$GITHUB_SERVER_URL/api/packages/$OWNER/generic/$MC_PKG"
sha8="$(git -C "$ROOT" rev-parse --short=8 HEAD)"
ver="$(branch_slug)-$sha8"
pcurl() { curl -sf -H "Authorization: token $PACKAGE_TOKEN" "$@"; }

W="$ROOT/.update"
rm -rf "$W"; mkdir -p "$W/in" "$W/out"

echo "=== inputs: $MC_PKG/$ver"
pcurl -o "$W/in/manifest.json" "$API/$ver/manifest.json" || { echo "no package $MC_PKG/$ver: stage package did not publish it" >&2; exit 1; }
python3 - "$W/in/manifest.json" "$(git -C "$ROOT" rev-parse HEAD)" > "$W/in/files.txt" <<'PY'
import json, sys
m = json.load(open(sys.argv[1]))
if m["trx64_commit"] != sys.argv[2]:
    sys.exit("package commit %s is not the commit of this run %s" % (m["trx64_commit"], sys.argv[2]))
# the .txu (T152) is made by stage package from this run's null_full.bit + hostapp.app and published with the package: it must be listed, and the
# sweep below must leave it (2026-11-01, bundle t164-774497b1: it matched trx64host_update_<sha8>.txu and deleted it)
for f in ("c64_basic.bit", "null_full.bit", "hostapp.app", "trx64host_update_%s.txu" % sys.argv[2][:8]):
    if f not in m["files"]:
        sys.exit("package manifest lists no %s" % f)
    print(f, m["files"][f]["sha256"])
PY
while read -r f sum; do
  pcurl -o "$W/in/$f" "$API/$ver/$f"
  [ "$(sha256sum "$W/in/$f" | cut -d' ' -f1)" = "$sum" ] || { echo "$f: SHA-256 differs from the manifest" >&2; exit 1; }
done < "$W/in/files.txt"
echo "inputs match the manifest ($(cut -d' ' -f1 "$W/in/files.txt" | tr '\n' ' '))"
# T123: the null core's image is the same host as c64_basic.bit - one AXSS host key, or the .cor files (packed with
# c64_basic.bit's key) would be refused by the host the update file writes.
hk_ref="$(python3 "$ROOT/tools/cor_pack.py" hostkey "$W/in/c64_basic.bit")"
hk_null="$(python3 "$ROOT/tools/cor_pack.py" hostkey "$W/in/null_full.bit")"
[ -n "$hk_null" ] && [ "$hk_null" = "$hk_ref" ] \
  || { echo "null_full.bit carries host key '$hk_null', c64_basic.bit '$hk_ref': they are not one host" >&2; exit 1; }
echo "host key $hk_null in both images"

pub="$W/pub"; mkdir -p "$pub"

# The MultiCore host update file: the host with the null core in the partition (null_full.bit, T123) and OUR boot
# application (stage trxapps), which is what tools/release_mc.sh attaches to a release. It used to be built on a Mac in the same image,
# so a release needed a machine with that image; now it comes out of the run that built the parts.
echo "=== updater (host: the MultiCore boot application)"
rm -rf "$W/out-host"; mkdir -p "$W/out-host"
TRX_UPDATE_NAME="trx64host_update_${sha8}_$(date +%Y%m%d)" \
  build_update_host "$W/in/manifest.json" "$W/in/null_full.bit" "$W/in/hostapp.app" "$W/out-host"
DH="$(ls -d "$W"/out-host/trx64-*)"
cp "$DH"/trx64host_update_*.cfw "$DH"/trx64host_update_*.ue2 "$pub/"
ls "$pub"/trx64host_update_*.cfw "$pub"/trx64host_update_*.ue2 >/dev/null

# SHA256SUMS of the package version: its lines for earlier update files (another build date, a rerun)
# go, ours are added. Those earlier files are removed from both versions below.
pcurl -o "$W/in/SHA256SUMS" "$API/$ver/SHA256SUMS" || { echo "package $ver has no SHA256SUMS" >&2; exit 1; }
{ update_sums_without_ours < "$W/in/SHA256SUMS" || true; (cd "$pub" && sha256sum -- *); } > "$pub/SHA256SUMS"
cat "$pub/SHA256SUMS"

# stale update files of earlier builds of this commit (and the legacy trx64_update_* of the monolith line, no longer made here)
for v in "$ver" "$(branch_slug)-latest"; do
  pcurl "$GITHUB_SERVER_URL/api/v1/packages/$OWNER/generic/$MC_PKG/$v/files" 2>/dev/null \
    | update_file_names \
    | while read -r old; do
        [ -e "$pub/$old" ] || curl -s -o /dev/null -X DELETE -H "Authorization: token $PACKAGE_TOKEN" "$API/$v/$old" || true
      done || true
done

# T172 S5: the content of the bundle is checked before it is published - the .txu/.cfw/.ue2 carry this run's bitstream and app, the app shows
# this run's version, the two host images are one host (tools/check_bundle.py; its selftest is in ci/test_bundle.sh, lint)
python3 -I "$ROOT/tools/check_bundle.py" "$W/in" "$pub" || { echo "the bundle content check failed: nothing is published" >&2; exit 1; }

publish_files "$pub"
echo "=== update done in $(lap): $(cd "$pub" && ls trx64host_update_* | tr '\n' ' ') (the package keeps trx64host_update_${sha8}.txu)"
