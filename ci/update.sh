#!/usr/bin/env bash
# Stage `update`: the host update file of this run, from exactly the parts of this commit's CI package (the artifact `package`, read from $CI_PACKAGE_IN), left in
# $ROOT/.update/out for the workflow to upload as the artifact `update`; ci/prerelease.sh adds it to the package's pre-release and writes the SHA256SUMS over both.
# Gideon's application is not built or packed here (T164, Alex 2026-11-01): the monolith updater trx64_update_* is TRX64-Ultimate's.
#   trx64host_update_<commit8>_<date>.cfw/.ue2  (a tag: trx64host_update_<x.y.z>.cfw/.ue2, ci/lib.sh file_id) the MultiCore file: null_full.bit (the same host + the null core,
#                                               cores/null, T123: the flash holds no machine) + our boot application from stage trxapps; tools/release_mc.sh attaches it to the release
#
# Only for integ-m1, integration, main and v* tags (ci/select.sh update); any other ref prints "update SKIPPED" and ends green at once. The decision is made here,
# never by a job-level `if:` (docs/specs/T31-ci-pipeline.md, decision 2). UPDATE_FORCE=true (ci/child.sh sets it when it starts this script; the dispatcher's ci/select.sh reads the dispatch choice `extra`
# of T239.1 D39 on GitHub), BUNDLE_INPUT=true (Gitea: the dispatch input `bundle`, until the switch) or "[bundle]" in the head commit message (T172) builds on any branch.
#
# The release tag: TRX_UPDATE_NAME replaces the file name base (tools/make_update.sh); on a tag it is trx64host_update_<x.y.z> without a date (ci/lib.sh file_id, Alex 2026-11-18).
#
#   CI_PACKAGE_IN=<the package artifact> ci/update.sh
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/lib.sh"

if [ "$("$ROOT/ci/select.sh" update)" != yes ]; then
  echo "update SKIPPED: only integ-m1/integration/main (and v* tags); put [bundle] in the head commit or dispatch with extra = update (Gitea: bundle = true) to build on this branch"
  exit 0
fi
: "${CI_PACKAGE_IN:?CI_PACKAGE_IN: the directory of the package artifact}"
fid="$(file_id)"     # the name part of the update files: the version on a tag, else the commit (ci/lib.sh)

W="$ROOT/.update"
rm -rf "$W"; mkdir -p "$W/in" "$W/out"

echo "=== inputs: $CI_PACKAGE_IN"
[ -f "$CI_PACKAGE_IN/manifest.json" ] || { echo "no manifest.json in $CI_PACKAGE_IN: stage package left no package" >&2; exit 1; }
cp "$CI_PACKAGE_IN/manifest.json" "$W/in/manifest.json"
python3 - "$W/in/manifest.json" "$(git -C "$ROOT" rev-parse HEAD)" "$fid" > "$W/in/files.txt" <<'PY'
import json, sys
m = json.load(open(sys.argv[1]))
if m["trx64_commit"] != sys.argv[2]:
    sys.exit("package commit %s is not the commit of this run %s" % (m["trx64_commit"], sys.argv[2]))
# the .txu (T152) is made by stage package from this run's null_full.bit + hostapp.app and sits in the package: it must be listed
for f in ("c64_basic.bit", "null_full.bit", "hostapp.app", "trx64host_update_%s.txu" % sys.argv[3]):
    if f not in m["files"]:
        sys.exit("package manifest lists no %s" % f)
    print(f, m["files"][f]["sha256"])
PY
while read -r f sum; do
  [ -f "$CI_PACKAGE_IN/$f" ] || { echo "$f is not in $CI_PACKAGE_IN" >&2; exit 1; }
  cp "$CI_PACKAGE_IN/$f" "$W/in/$f"
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

pub="$W/out"

# The MultiCore host update file: the host with the null core in the partition (null_full.bit, T123) and OUR boot
# application (stage trxapps), which is what tools/release_mc.sh attaches to a release. It used to be built on a Mac in the same image,
# so a release needed a machine with that image; now it comes out of the run that built the parts.
echo "=== updater (host: the MultiCore boot application)"
rm -rf "$W/out-host"; mkdir -p "$W/out-host"
# the name: a release (tag) the version alone, any other build the commit and the build date (ci/lib.sh file_id, Alex 2026-11-18)
uname="trx64host_update_$fid"
[ "$(ci_ref_type)" = tag ] || uname="${uname}_$(date +%Y%m%d)"
TRX_UPDATE_NAME="$uname" \
  build_update_host "$W/in/manifest.json" "$W/in/null_full.bit" "$W/in/hostapp.app" "$W/out-host"
DH="$(ls -d "$W"/out-host/trx64-*)"
cp "$DH"/trx64host_update_*.cfw "$DH"/trx64host_update_*.ue2 "$pub/"
ls "$pub"/trx64host_update_*.cfw "$pub"/trx64host_update_*.ue2 >/dev/null

# T172 S5: the content of the bundle is checked before it is handed on - the .txu/.cfw/.ue2 carry this run's bitstream and app, the app shows
# this run's version, the two host images are one host (tools/check_bundle.py; its selftest is in ci/test_bundle.sh, lint)
python3 -I "$ROOT/tools/check_bundle.py" "$W/in" "$pub" || { echo "the bundle content check failed: the update files are not handed on" >&2; rm -rf "${pub:?}"/*; exit 1; }

echo "=== update done in $(lap): $(cd "$pub" && ls trx64host_update_* | tr '\n' ' ') (the package keeps trx64host_update_${fid}.txu)"
