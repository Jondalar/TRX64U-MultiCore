#!/usr/bin/env bash
# One multi-core release, from a finished CI pre-release of the builds repository to a GitHub release of this repository (T94 P5; T239.1 S12).
#
#   tools/release_mc.sh <pre-release tag> [options]
#       --publish                 also create the GitHub release v<x.y.z> (without it: build and check only)
#       --app <hostapp.app>       the host boot app that goes into the update file
#                                 [default: the hostapp.app of the package itself, built by stage
#                                 trxapps of the same run; only an override needs this option]
#       --app-commit <sha>        the commit that app was built from [default: its own gitinfo string]
#       --out <dir>               where the bundle is written [default: dist]
#       --null-full <bit>         the run's null_full.bit (host + null core), only when the package has no
#                                 trx64host_update_* (a branch package): the update file is built from it
#       --keep                    keep the downloaded package (default: it stays in <out>/pkg-<tag>)
#
# <pre-release tag> is the pre-release the pipeline published in the builds repository (MC_BUILDS_REPO of ci/pkg.env), <branch>-<8 hex of the commit>,
# e.g. main-51e89821. `latest` takes the newest main-<sha8> one (tools/builds.sh latest). No token is needed to read it.
#
# What it does, in this order:
#   1. downloads the pre-release's .cor, the bitstream and manifest.json (never the reports), each file checked against the SHA-256
#      GitHub and SHA256SUMS list (tools/builds.sh);
#   2. tools/check_public_tree.py --files on them: no ROM byte of the private list (T239.1 D16: a CI pre-release carries no ROM and no TRX64 .cor);
#   3. packs trx64u.cor and trx64g.cor from release/imports/*.pin (D22; tools/pack_import_cor.sh, the one Gitea read, needs GITHUB_SERVER_URL,
#      GITHUB_REPOSITORY and PACKAGE_TOKEN for the registry of the TRX64-Ultimate package) into the package, before it is promoted;
#   4. reads the host key out of the bitstream and the bitstream's commit out of the manifest;
#   5. checks every .cor against that key and the contract version - a bundle for another host stops the release;
#   6. tools/make_release_mc.sh: the update file (.cfw/.ue2), INSTALL.md, SHA256SUMS, and a zip that holds
#      every file of the bundle;
#   7. tools/check_public_tree.py --allow-trx --files on the bundle and the zip: only the two TRX64 .cor may carry what the fourth exception allows;
#   8. with --publish: gh release create v<x.y.z> --verify-tag in $MC_SRC_REPO, as a draft with every file, then published. The tag v<x.y.z> is made by
#      Alex alone (D28) and has to name the commit the package was built from; a missing tag stops the release.
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/.." && pwd)"
source "$ROOT/ci/pkg.env"
OUT="$ROOT/dist"
PUBLISH=0
APP=""
APP_COMMIT=""
VER="${1:-}"
[ -n "$VER" ] || { sed -n '2,13p' "$0"; exit 2; }
shift
[ "$VER" != latest ] || VER="$("$HERE/builds.sh" latest)"
while [ $# -gt 0 ]; do
    case "$1" in
        --publish) PUBLISH=1; shift;;
        --app) APP="$2"; shift 2;;
        --app-commit) APP_COMMIT="$2"; shift 2;;
        --out) OUT="$2"; shift 2;;
        --null-full) NULL_FULL="$2"; shift 2;;
        --keep) shift;;
        -h|--help) sed -n '2,13p' "$0"; exit 0;;
        *) echo "unknown option $1" >&2; exit 2;;
    esac
done

[ -z "$APP" ] || [ -f "$APP" ] || { echo "no host app at $APP" >&2; exit 1; }

PKG="$OUT/pkg-$VER"
mkdir -p "$PKG"

echo ">>> pre-release $VER"
files=$("$HERE/builds.sh" files "$VER" | python3 -c 'import sys
want = [n for n in sys.stdin.read().split()
        if n.endswith(".cor") or n.startswith("trx64host_update_") or n.endswith("_mono.bit")
        or n in ("c64_basic.bit", "manifest.json", "hostapp.app", "coreapp.bin", "unzip.u2a", "miniz.u2a", "trex_crt.u2a", "softboot.u2a")
        or n.startswith("coreapp_") and n.endswith(".bin")]
print(" ".join(want))') || { echo "pre-release $VER not found" >&2; exit 1; }
[ -n "$files" ] || { echo "pre-release $VER holds no .cor" >&2; exit 1; }
for f in $files; do
    case "$f" in trx64u.cor|trx64g.cor) echo "pre-release $VER holds $f: a CI pre-release carries no TRX64 .cor (T239.1 D16)" >&2; exit 1;; esac
done
fetched=()
for f in $files; do
    [ -s "$PKG/$f" ] || "$HERE/builds.sh" fetch "$VER" "$f" "$PKG/$f" || { echo "cannot fetch $f" >&2; exit 1; }
    fetched+=("$PKG/$f")
done
# D16: what a CI pre-release carries holds no ROM; checked on the downloaded files themselves, before anything is added to them
python3 -I "$ROOT/tools/check_public_tree.py" --files "${fetched[@]}" || { echo "pre-release $VER holds private content: release stopped" >&2; exit 1; }
[ -s "$PKG/c64_basic.bit" ] || { echo "the package has no bitstream" >&2; exit 1; }
[ -s "$PKG/manifest.json" ] || { echo "the package has no manifest" >&2; exit 1; }
# T221 D4: the release is the tested build with its version strings rewritten (x.y.z-dev+<sha8> -> x.y.z in the apps, the .cor, the .txu, the manifest);
# the marker keeps a second run over the same downloaded package from promoting it twice. The package of a tag (since 0.2.0 the release is the CI build of
# tag v<x.y.z>, T221) carries x.y.z already and its update files are named after it (ci/lib.sh file_id): nothing to promote, the bundle takes that name.
REL="$(python3 -c 'import json, re, sys
v = json.load(open(sys.argv[1]))["version"]
print(v if re.match(r"^[0-9]+[.][0-9]+[.][0-9]+$", v) else "")' "$PKG/manifest.json")"
# D22: the two TRX64 .cor are packed here, from the pins and the package's own coreapp_<id>.bin (else coreapp.bin), in the version form of the package (a tag build:
# x.y.z; else x.y.z-dev+<sha8>, which the promotion below turns into x.y.z like every other .cor). A second run keeps the ones of the first.
pack_imports() {
    local pin id app
    for pin in "$ROOT"/release/imports/*.pin; do
        [ -e "$pin" ] || continue
        id="$(basename "$pin" .pin)"
        if grep -q '^PIPELINE=false' "$pin"; then echo "    $id: PIPELINE=false in the pin, not packed"; continue; fi
        [ ! -e "$PKG/$id.cor" ] || { echo "    $id.cor: already in the package, kept"; continue; }
        app="$PKG/coreapp.bin"; [ ! -f "$PKG/coreapp_$id.bin" ] || app="$PKG/coreapp_$id.bin"
        [ -s "$app" ] || { echo "the package has no coreapp.bin for $id.cor" >&2; exit 1; }
        ${REL:+env GITHUB_REF_TYPE=tag} bash "$ROOT/tools/pack_import_cor.sh" --pin "$pin" --app-bin "$app" --app-commit "$(python3 -I -c 'import json, sys
print(json.load(open(sys.argv[1])).get("trxapps_commit", "")[:8])' "$PKG/manifest.json")" --out "$PKG" > /dev/null || { echo "FAIL packing $id.cor" >&2; exit 1; }
        echo "    $id.cor packed from $(basename "$pin")"
    done
}
pack_imports
if [ -n "$REL" ]; then
    echo "    a tag build: version $REL, nothing to promote"
else
    [ -f "$PKG/.promoted" ] || { python3 "$ROOT/tools/release_promote.py" "$PKG" && touch "$PKG/.promoted"; } || { echo "the package could not be promoted to a release" >&2; exit 1; }
    REL="$VER"
fi
# The host app of this very run (T94 P6). Before stage trxapps existed it came from a local result/
# directory, so a release could carry an application nobody had built from the commit it names.
if [ -z "$APP" ]; then
    [ -s "$PKG/hostapp.app" ] || { echo "package $VER has no hostapp.app: it was built before stage trxapps. Pass --app." >&2; exit 1; }
    APP="$PKG/hostapp.app"
    [ -n "$APP_COMMIT" ] || APP_COMMIT=$(python3 -c 'import json, sys
m = json.load(open(sys.argv[1]))
print(m.get("trxapps_commit", "")[:8])' "$PKG/manifest.json")
fi

BIT_COMMIT=$(python3 -c 'import json, sys; print(json.load(open(sys.argv[1]))["trx64_commit"][:8])' "$PKG/manifest.json")
HOST_KEY=$(python3 "$ROOT/tools/cor_pack.py" hostkey "$PKG/c64_basic.bit" | tr -d '\r\n ' | cut -c1-8)
[ -n "$HOST_KEY" ] || { echo "the bitstream names no host key" >&2; exit 1; }
if [ -z "$APP_COMMIT" ]; then
    # the app carries the commit it was built from (make gitinfo), usually seven characters
    APP_COMMIT=$(strings "$APP" | grep -oE '^[0-9a-f]{7,8}$' | head -1 || true)
    if [ -n "$APP_COMMIT" ]; then
        APP_COMMIT=$(git -C "$ROOT" rev-parse --short=8 "$APP_COMMIT" 2>/dev/null || echo "$APP_COMMIT")
    else
        echo "    the app names no commit: taking HEAD, which is right only if it was built from it" >&2
        APP_COMMIT=$(git -C "$ROOT" rev-parse --short=8 HEAD)
    fi
fi
echo "    host key $HOST_KEY, bitstream $BIT_COMMIT, app $APP_COMMIT"

# T115 7: a large core is one full bitstream + the core application, packed here from the package's flat build (the
# pipeline builds <id>_mono.bit, not the .cor); it carries no host key, a full bitstream does not sit under the host
for b in "$PKG"/*_mono.bit; do
    [ -e "$b" ] || continue
    id=$(basename "$b" _mono.bit)
    # build_only: a flat build the Gitea package carries for the board, never a release has it (no core since 2026-11-18; c64scpu was one)
    [ "$(python3 "$ROOT/tools/core_toml.py" get "$id" build_only)" = "true" ] && { echo "    $id is build_only: left out"; continue; }
    [ -s "$PKG/coreapp.bin" ] || { echo "FAIL $id: the package has no coreapp.bin for the large .cor" >&2; exit 1; }
    bash "$ROOT/tools/pack_large_cor.sh" --id "$id" --name "$(python3 "$ROOT/tools/core_toml.py" get "$id" name)" \
        --version "$(python3 "$ROOT/tools/core_toml.py" get "$id" version)" \
        --short "$(python3 "$ROOT/tools/core_toml.py" get "$id" short)" --bit "$b" --app-bin "$PKG/coreapp.bin" \
        --bit-commit "$BIT_COMMIT" --app-commit "$APP_COMMIT" --out "$PKG/large" > /dev/null || { echo "FAIL packing $id" >&2; exit 1; }
done
CORARGS=()
n=0
for c in "$PKG"/large/*.cor; do
    [ -e "$c" ] || continue
    CORARGS+=(--cor "$c")
done
for c in "$PKG"/*.cor; do
    # build_only holds for the package's own .cor too: the CI packs every flat build for the board, a release never has a build_only one
    [ "$(python3 "$ROOT/tools/core_toml.py" get "$(basename "$c" .cor)" build_only 2>/dev/null || true)" = "true" ] \
        && { echo "    $(basename "$c") is build_only: left out"; continue; }
    python3 "$ROOT/tools/cor_pack.py" check "$c" --host-key "$HOST_KEY" > /dev/null \
        || { echo "FAIL $(basename "$c") does not belong to host $HOST_KEY" >&2; exit 1; }
    CORARGS+=(--cor "$c")
    n=$((n + 1))
done
echo "    $n .cor checked against host $HOST_KEY and contract $(cat "$ROOT/contract/VERSION")"

# The update file the pipeline built for this very package (ci/update.sh, null_full.bit = host + null core). Without it
# the release stops: building one here took c64_basic.bit (host + the build-only c64ref, T115), and release
# t164-c83a83c2 shipped that, with no picture in the host menu (2026-11-02). Give --null-full <bit> to build from the
# flash image by hand.
UPDARGS=()
UCFW="$(ls "$PKG"/trx64host_update_*.cfw 2>/dev/null | head -1 || true)"
UUE2="$(ls "$PKG"/trx64host_update_*.ue2 2>/dev/null | head -1 || true)"
if [ -n "$UCFW" ] && [ -n "$UUE2" ]; then
    UPDARGS=(--update "$UCFW" --update-ue2 "$UUE2")
elif [ -n "${NULL_FULL:-}" ]; then
    [ "$(python3 "$ROOT/tools/cor_pack.py" hostkey "$NULL_FULL" | tr -d '\r\n ' | cut -c1-8)" = "$HOST_KEY" ] \
        || { echo "$NULL_FULL is not the host of package $VER" >&2; exit 1; }
    echo "    no trx64host_update_* in package $VER: building it here from $NULL_FULL (needs the u64-build-env image)"
else
    echo "no trx64host_update_* in package $VER: give --null-full <null_full.bit of this run>; c64_basic.bit is never a flash image" >&2
    exit 1
fi

# The tested .txu ships too (Alex, 2026-11-05): a board already on a MultiCore host installs it from F3 (T152); the host starts a .cfw as a core.
TXU="$(ls "$PKG"/trx64host_update_*.txu 2>/dev/null | head -1 || true)"
[ -n "$TXU" ] || { echo "package $VER has no trx64host_update_*.txu: nothing tested to compare the installer with" >&2; exit 1; }

# T180 D9: Unzip ships with the bundle, one copy for every core (/SD/cores/apps/unzip.u2a, INSTALL.md says so)
[ -s "$PKG/unzip.u2a" ] || { echo "package $VER has no unzip.u2a" >&2; exit 1; }
# T197 P5: the zip library Unzip loads rides beside it (the updater copies every .u2a beside the update file into <cores>/apps)
[ -s "$PKG/miniz.u2a" ] || { echo "package $VER has no miniz.u2a (the zip library unzip.u2a needs)" >&2; exit 1; }
# T228 R21: the cartridge tools and Run as Softboot ship like Unzip (ci/trxapps.sh builds them, ci/package.sh copies them into the package)
[ -s "$PKG/trex_crt.u2a" ] || { echo "package $VER has no trex_crt.u2a (Dump, Inspect, Flash Cartridge)" >&2; exit 1; }
[ -s "$PKG/softboot.u2a" ] || { echo "package $VER has no softboot.u2a (Run as Softboot)" >&2; exit 1; }

bash "$ROOT/tools/make_release_mc.sh" --u2a "$PKG/unzip.u2a" --u2a "$PKG/miniz.u2a" --u2a "$PKG/trex_crt.u2a" --u2a "$PKG/softboot.u2a" --txu "$TXU" --id "$REL" --bit "${NULL_FULL:-$PKG/c64_basic.bit}" --app "$APP" \
    --bit-commit "$BIT_COMMIT" --app-commit "$APP_COMMIT" --out "$OUT" ${UPDARGS[@]+"${UPDARGS[@]}"} "${CORARGS[@]}"

D="$OUT/trx64mc-$REL"

# The installer must write the flash the board test ran: bitstream and app byte-equal to the package's .txu, never c64_basic.bit
# (release t164-c83a83c2, 2026-11-02). The check fails on purpose once per file (probe); no .txu, no release.
python3 "$ROOT/tools/check_release_flash.py" --txu "$TXU" --reject "$PKG/c64_basic.bit" "$D"/trx64host_update_*.cfw "$D"/trx64host_update_*.ue2 \
    || { echo "the installer is not the tested flash: release stopped" >&2; exit 1; }
ZIP="$OUT/trx64mc-$REL.zip"
[ -s "$ZIP" ] || { echo "no zip was built" >&2; exit 1; }
# the zip has to hold every file of the bundle: a release whose archive is short of one is worse than none
python3 "$ROOT/tools/release_check.py" zip "$D" "$ZIP" || exit 1

# T239.1 D16, D22: the bundle is the one place a ROM-bearing file may be, and only inside the two TRX64 .cor (the fourth exception of the maintainer's rules)
python3 -I "$ROOT/tools/check_public_tree.py" --allow-trx --files "$D" "$ZIP" || { echo "the bundle holds private content outside the TRX64 .cor: release stopped" >&2; exit 1; }

if [ "$PUBLISH" = 1 ]; then
    # D28: the release is made for a tag that only Alex creates; --verify-tag makes gh refuse to make one. The tag has to be the commit the package was built from.
    TAGVER="$(python3 -I -c 'import json, re, sys
v = json.load(open(sys.argv[1]))["version"]
print(v if re.match(r"^[0-9]+[.][0-9]+[.][0-9]+$", v) else "")' "$PKG/manifest.json")"
    [ -n "$TAGVER" ] || { echo "the package version is no x.y.z: nothing to publish" >&2; exit 1; }
    for f in "$D"/* "$ZIP"; do
        printf '%s' "$(basename "$f")" | grep -Eq '^[A-Za-z0-9][A-Za-z0-9._+-]{0,99}$' || { echo "$(basename "$f"): GitHub would rename this asset: release stopped" >&2; exit 1; }
        [ -f "$f" ] || { echo "$f is no plain file: release stopped" >&2; exit 1; }
    done
    TAGSHA="$(gh api "repos/$MC_SRC_REPO/commits/v$TAGVER" --jq .sha 2>/dev/null || true)"
    [ -n "$TAGSHA" ] || { echo "no tag v$TAGVER in $MC_SRC_REPO: Alex creates the tag (D28), then run this again" >&2; exit 1; }
    case "$TAGSHA" in "$(python3 -I -c 'import json, sys; print(json.load(open(sys.argv[1]))["trx64_commit"])' "$PKG/manifest.json")"*) ;;
        *) echo "tag v$TAGVER is $TAGSHA, the package was built from another commit: release stopped" >&2; exit 1;; esac
    echo ">>> release v$TAGVER in $MC_SRC_REPO"
    gh release create "v$TAGVER" --repo "$MC_SRC_REPO" --verify-tag --draft --title "TRX64 MultiCore $TAGVER" --notes-file "$D/INSTALL.md" "$D"/* "$ZIP"
    want=$(( $(ls "$D" | wc -l) + 1 ))
    have="$(gh release view "v$TAGVER" --repo "$MC_SRC_REPO" --json assets --jq '.assets | length')"
    [ "$have" = "$want" ] || { echo "the draft holds $have assets, $want were attached: left as a draft" >&2; exit 1; }
    gh release edit "v$TAGVER" --repo "$MC_SRC_REPO" --draft=false
    gh release view "v$TAGVER" --repo "$MC_SRC_REPO" --json url,isImmutable --jq '">>> \(.url)  immutable: \(.isImmutable)"'
else
    echo ">>> built, not published: $D"
    echo "    publish it with: tools/release_mc.sh $VER --publish"
fi
