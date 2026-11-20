#!/usr/bin/env bash
# T239.1 D23: the kit the emulator suites need, made in the run from public sources (the repository must not hold it). A directory:
#   KIT/roms             roms/ of GideonZ/1541ultimate at the commit pinned below (git checks every byte through the commit id)
#   KIT/flash.bin        ue2emu install --update <this run's .ue2> --flash KIT/flash.bin --roms KIT/roms --yes --c64-roms
#   KIT/profiles/VIC-20.toml   docs/examples/core-vic20.toml of the emulator's pinned release, SHA-256 checked
#   KIT/cors/vc20.cor    this run's VIC-20 .cor (the host finds its partial by the SHA-256, so any .cor of the same core serves)
#
#   ci/emu_kit.sh KIT --update <file.ue2> --cor <vc20.cor> [--roms DIR] [--roms-tree ID] [--profile FILE --profile-sha256 HEX]
#     --roms DIR: use a restored copy of the ROM folder instead of fetching it (actions/cache of the workflow, keyed by the commit pinned below; the folder is
#                 checked against the git tree id of roms/ at that commit, so a cache entry that holds anything else is refused)
#     --roms-tree ID: tests only: another tree id
#     --profile FILE: tests only: a local profile, with its own hash
# env: UE2EMU_BIN or `ue2emu` on PATH (ci/ue2emu.sh). The emulator is the one of the emu image (ci/emu/Dockerfile).
# The installed flash.bin is the one the emulator's `install` makes; whether tests/hostapp/emu_host.sh expects exactly that one is not proven (D23).
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
GIDEON_REPO="https://github.com/GideonZ/1541ultimate.git"
GIDEON_COMMIT="700c7f2fab0b7d52da418d21f0c2e65e99725805"
GIDEON_ROMS_TREE="e308506696d7507486b7b0981eeacc4ae5bfe373"     # git tree id of roms/ at that commit (gh api repos/GideonZ/1541ultimate/git/trees/<commit>)
UE2_RAW="https://raw.githubusercontent.com/Jondalar/UE2-C64U-Emulator/4612884e7a633d52375277060f1a7e9bbf6552ba/docs/examples/core-vic20.toml"
UE2_PROFILE_SHA256="828d90974b921a440b2c83e1ef2fddac1c1d4de12d39efef1b6f77dac47cd24a"
kit="${1:?usage: ci/emu_kit.sh KIT --update FILE.ue2 --cor FILE.cor [--roms DIR]}"; shift
upd="" cor="" roms="" roms_tree="$GIDEON_ROMS_TREE" prof="" prof_sha="$UE2_PROFILE_SHA256"
while [ $# -gt 0 ]; do
  case "$1" in
    --update) upd="$2"; shift 2 ;; --cor) cor="$2"; shift 2 ;; --roms) roms="$2"; shift 2 ;; --roms-tree) roms_tree="$2"; shift 2 ;;
    --profile) prof="$2"; shift 2 ;; --profile-sha256) prof_sha="$2"; shift 2 ;;
    *) echo "emu_kit: unknown argument $1" >&2; exit 2 ;;
  esac
done
[ -f "$upd" ] && [ -f "$cor" ] || { echo "emu_kit: --update and --cor must name existing files (this run's .ue2 and VIC-20 .cor)" >&2; exit 1; }
sha256() { python3 -I -c 'import hashlib,sys; print(hashlib.sha256(open(sys.argv[1],"rb").read()).hexdigest())' "$1"; }
mkdir -p "$kit/profiles" "$kit/cors"
rm -rf "$kit/roms"
if [ -n "$roms" ]; then
  [ -d "$roms" ] || { echo "emu_kit: --roms $roms is no directory" >&2; exit 1; }
  cp -R "$roms" "$kit/roms"
else
  src="$(mktemp -d)"; trap 'rm -rf "${src:?}"' EXIT
  git -C "$src" init -q
  git -C "$src" fetch -q --depth 1 --filter=blob:none "$GIDEON_REPO" "$GIDEON_COMMIT"
  [ "$(git -C "$src" rev-parse 'FETCH_HEAD^{commit}')" = "$GIDEON_COMMIT" ] || { echo "emu_kit: the fetched commit is not $GIDEON_COMMIT" >&2; exit 1; }
  git -C "$src" -c advice.detachedHead=false checkout -q FETCH_HEAD -- roms
  cp -R "$src/roms" "$kit/roms"
fi
# the git tree id of a folder: the id git gives the folder's content (names, modes, bytes), made in a scratch repository. add -A -f takes ignored files too and
# core.attributesFile and core.excludesFile point at nothing, so a .gitignore or .gitattributes inside the folder (the pinned folder holds one at
# roms/c64rom/.gitignore) is just a file in the id: a planted one is an extra file and changes the id, and it hides and converts nothing.
# Only a name equal to .git (any case) is refused first, because git add skips it without a word.
tree_of() {
  if [ -n "$(find "$1" -iname '.git' -print -quit)" ]; then echo "emu_kit: the ROM folder holds a name .git" >&2; echo refused; return 0; fi
  local g; g="$(mktemp -d)"; git -C "$g" init -q
  git -C "$g" -c core.autocrlf=false -c core.safecrlf=false -c core.attributesFile=/dev/null -c core.excludesFile=/dev/null --work-tree="$1" add -A -f
  git -C "$g" write-tree; rm -rf "${g:?}"
}
[ "$(tree_of "$kit/roms")" = "$roms_tree" ] || { echo "emu_kit: the ROM folder is not the roms/ of $GIDEON_COMMIT (tree id $roms_tree)" >&2; exit 1; }
[ -n "$(ls -A "$kit/roms")" ] || { echo "emu_kit: the ROM folder is empty" >&2; exit 1; }
if [ -z "$prof" ]; then prof="$kit/profiles/.download"; curl -fsSL --retry 3 -o "$prof" "$UE2_RAW"; fi
[ "$(sha256 "$prof")" = "$prof_sha" ] || { echo "emu_kit: the VIC-20 profile does not match its SHA-256 $prof_sha" >&2; exit 1; }
cp "$prof" "$kit/profiles/VIC-20.toml"; rm -f "$kit/profiles/.download"
cp "$cor" "$kit/cors/vc20.cor"
rm -f "$kit/flash.bin"
"$HERE/ue2emu.sh" install --update "$upd" --flash "$kit/flash.bin" --roms "$kit/roms" --yes --c64-roms
[ -s "$kit/flash.bin" ] || { echo "emu_kit: ue2emu install left no flash.bin" >&2; exit 1; }
echo "emu_kit: $kit holds roms ($(find "$kit/roms" -type f | wc -l | tr -d ' ') files), flash.bin ($(wc -c < "$kit/flash.bin" | tr -d ' ') bytes), VIC-20 profile, vc20.cor"
