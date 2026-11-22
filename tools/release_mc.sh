#!/usr/bin/env bash
# One multi-core release, taken over from a finished CI pre-release of the builds repository into a GitHub release of this repository (T94 P5; T239.1 S12, D47).
#
#   tools/release_mc.sh <pre-release tag> [options]
#       --publish                 also create the GitHub release v<x.y.z> (without it: download and check only)
#       --out <dir>               where the pre-release is downloaded to <dir>/pkg-<tag> [default: dist]
#
# <pre-release tag> is the pre-release the pipeline published in the builds repository (MC_BUILDS_REPO of ci/pkg.env), <branch>-<8 hex of the commit>,
# e.g. main-51e89821, or v0.2.1-<sha8> for the build of the tag v0.2.1. `latest` takes the newest main-<sha8> one (tools/builds.sh latest). The download needs
# no token; the notes, bundle.toml and the tag are read with gh api, which needs a gh login.
# Since D42, D43 and D47 the pre-release is the tester bundle itself: every .cor (trx64u.cor and trx64g.cor included), the update files, the .u2a apps,
# INSTALL.md, ROMS.txt, SHA256SUMS and the zip of all. Nothing is packed, rebuilt or rewritten here; the release holds the bytes of the pre-release.
#
# What it does, in this order:
#   1. downloads every asset of the pre-release, each file checked against the SHA-256 GitHub and SHA256SUMS list (tools/builds.sh), then all of them
#      against SHA256SUMS (the zip's line included);
#   2. tools/check_public_tree.py --allow-trx --files on every file: no ROM byte of the private list outside the two TRX64 .cor (the fourth exception);
#   3. reads the host key out of the .txu (tools/txu_pack.py inspect) and checks every .cor against it and the contract version (tools/cor_pack.py check, whose
#      default contract is the one of this tree; a large core carries no host key and passes on its own checks) - a bundle for another host stops the release;
#   4. the version x.y.z is the version of the .txu without its -dev+<sha8> (ci/version.sh: the build of a tag is x.y.z, any other build x.y.z-dev+<sha8>,
#      bundle.toml names x.y.z) and has to equal the bundle.toml of the commit. With --publish the .txu has to carry the plain x.y.z: a release is made from the
#      pre-release of its tag build (S12), a dev build is only checked. The commit is the one in the notes of the pre-release, which has to start with the
#      <sha8> of its tag and with the commit field of the .txu. The tag v<x.y.z> is made by Alex alone (D28): the tool checks that it exists in $MC_SRC_REPO and names that commit, and
#      stops otherwise (this stop is also the end of a dry run before the tag exists);
#   5. without --publish: prints the files and what would be published; with --publish: gh release create v<x.y.z> --verify-tag in $MC_SRC_REPO, as a
#      draft with every file of the pre-release and no other file of the download folder (INSTALL.md is the notes), then published once the draft holds them all.
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/.." && pwd)"
source "$ROOT/ci/pkg.env"
OUT="$ROOT/dist"
PUBLISH=0
VER="${1:-}"
[ -n "$VER" ] || { sed -n '2,5p' "$0"; exit 2; }
case "$VER" in -h|--help) sed -n '2,5p' "$0"; exit 0;; esac
shift
[ "$VER" != latest ] || VER="$("$HERE/builds.sh" latest)"
while [ $# -gt 0 ]; do
    case "$1" in
        --publish) PUBLISH=1; shift;;
        --out) OUT="$2"; shift 2;;
        -h|--help) sed -n '2,5p' "$0"; exit 0;;
        *) echo "unknown option $1" >&2; exit 2;;
    esac
done
die() { echo "$*" >&2; exit 1; }

PKG="$OUT/pkg-$VER"
mkdir -p "$PKG"

echo ">>> pre-release $VER"
files=$("$HERE/builds.sh" files "$VER") || die "pre-release $VER not found"
[ -n "$files" ] || die "pre-release $VER holds no file"
for f in $files; do
    printf '%s' "$f" | grep -Eq '^[A-Za-z0-9][A-Za-z0-9._+-]{0,99}$' || die "$f: GitHub would rename this asset: release stopped"
    [ -s "$PKG/$f" ] || "$HERE/builds.sh" fetch "$VER" "$f" "$PKG/$f" >/dev/null 2>&1 || die "cannot fetch $f"
done
[ -s "$PKG/SHA256SUMS" ] || die "pre-release $VER has no SHA256SUMS"
(cd "$PKG" && shasum -a 256 -c --quiet SHA256SUMS) || die "SHA256SUMS does not match the downloaded files: release stopped"
for f in $files; do
    [ "$f" = SHA256SUMS ] || grep -q "  $f\$" "$PKG/SHA256SUMS" || die "$f is not listed in SHA256SUMS: release stopped"
done
echo "    $(echo "$files" | wc -w | tr -d ' ') files downloaded, SHA256SUMS ok"
# only the files of the pre-release count: a file left in the download folder by an earlier run is neither checked nor published
assets=()
for f in $files; do assets+=("$PKG/$f"); done

python3 -I "$ROOT/tools/check_public_tree.py" --allow-trx --files "${assets[@]}" || die "pre-release $VER holds private content outside the TRX64 .cor: release stopped"

TXU=""
for a in "${assets[@]}"; do case "$(basename "$a")" in trx64host_update_*.txu) TXU="$a"; break;; esac; done
[ -n "$TXU" ] || die "pre-release $VER has no trx64host_update_*.txu: no host to check the .cor against"
INSPECT="$(python3 "$ROOT/tools/txu_pack.py" inspect "$TXU")"
HOST_KEY="$(printf '%s\n' "$INSPECT" | sed -n 's/^ *host key \([0-9a-f]*\).*/\1/p' | head -1)"
[ -n "$HOST_KEY" ] || die "the .txu names no host key"
TXU_VER="$(printf '%s\n' "$INSPECT" | sed -n '1s/.*  version \([^ ]*\)  commit .*/\1/p')"
TXU_COMMIT="$(printf '%s\n' "$INSPECT" | sed -n '1s/.*  commit \([0-9a-f]*\).*/\1/p')"
[ -n "$TXU_COMMIT" ] || die "the .txu names no commit"
n=0
for c in "${assets[@]}"; do
    case "$c" in *.cor) ;; *) continue;; esac
    python3 "$ROOT/tools/cor_pack.py" check "$c" --host-key "$HOST_KEY" > /dev/null || die "FAIL $(basename "$c") does not belong to host $HOST_KEY (or to contract $(cat "$ROOT/contract/VERSION"))"
    n=$((n + 1))
done
[ "$n" -gt 0 ] || die "pre-release $VER holds no .cor"
echo "    $n .cor checked against host $HOST_KEY and contract $(cat "$ROOT/contract/VERSION")"

# version and commit: the .txu names x.y.z-dev+<sha8> (a build of main) or x.y.z (a build of a tag); the notes of the pre-release name the full commit
REL="${TXU_VER%%-dev+*}"
printf '%s' "$REL" | grep -Eq '^(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)$' || die "the .txu version '$TXU_VER' is no x.y.z[-dev+<sha8>]: nothing to release"
gh auth status >/dev/null 2>&1 || die "gh is not logged in (gh auth login): the notes, bundle.toml and the tag are read with gh api"
COMMIT="$(gh api "repos/$MC_BUILDS_REPO/releases/tags/$VER" --jq .body 2>/dev/null | sed -n 's/^Release bundle of commit `\([0-9a-f]\{40\}\)`.*/\1/p' | head -1 || true)"
[ -n "$COMMIT" ] || die "the notes of pre-release $VER name no commit"
case "$VER" in *-"${COMMIT:0:8}") ;; *) die "pre-release $VER is not named after commit ${COMMIT:0:8}: release stopped";; esac
case "$COMMIT" in "$TXU_COMMIT"*) ;; *) die "the .txu names commit $TXU_COMMIT, the pre-release $COMMIT: release stopped";; esac
BTOML="$(gh api -H 'Accept: application/vnd.github.raw' "repos/$MC_SRC_REPO/contents/bundle.toml?ref=$COMMIT" 2>/dev/null | sed -n 's/^[[:space:]]*version[[:space:]]*=[[:space:]]*"\([^"]*\)".*$/\1/p' | head -1 || true)"
[ "$BTOML" = "$REL" ] || die "the .txu names version $REL, bundle.toml of $COMMIT names '${BTOML:-nothing}': release stopped"
echo "    version $REL ($TXU_VER), commit $COMMIT, host key $HOST_KEY"
[ "$PUBLISH" != 1 ] || [ "$TXU_VER" = "$REL" ] || die "pre-release $VER is a dev build ($TXU_VER): a release is made from the pre-release of the tag build v$REL (S12): release stopped"

echo ">>> would publish v$REL in $MC_SRC_REPO from $PKG:"
printf '    %s\n' $files
if [ "$PUBLISH" != 1 ]; then
    echo "    publish it with: tools/release_mc.sh $VER --publish"
fi
# D28: the release is made for a tag that only Alex creates; --verify-tag makes gh refuse to make one. The tag has to be the commit of the pre-release.
TAGSHA="$(gh api "repos/$MC_SRC_REPO/commits/v$REL" --jq .sha 2>/dev/null || true)"
[ -n "$TAGSHA" ] || die "no tag v$REL in $MC_SRC_REPO: Alex creates the tag (D28), then run this again"
[ "$TAGSHA" = "$COMMIT" ] || die "tag v$REL is $TAGSHA, the pre-release was built from $COMMIT: release stopped"
echo "    tag v$REL names $COMMIT"
[ "$PUBLISH" = 1 ] || exit 0

echo ">>> release v$REL in $MC_SRC_REPO"
[ -s "$PKG/INSTALL.md" ] || die "pre-release $VER has no INSTALL.md (the notes)"
gh release create "v$REL" --repo "$MC_SRC_REPO" --verify-tag --draft --title "TRX64 MultiCore $REL" --notes-file "$PKG/INSTALL.md" "${assets[@]}"
want="${#assets[@]}"
have="$(gh release view "v$REL" --repo "$MC_SRC_REPO" --json assets --jq '.assets | length')"
[ "$have" = "$want" ] || die "the draft holds $have assets, $want were attached: left as a draft"
gh release edit "v$REL" --repo "$MC_SRC_REPO" --draft=false
gh release view "v$REL" --repo "$MC_SRC_REPO" --json url,isImmutable --jq '">>> \(.url)  immutable: \(.isImmutable)"'
