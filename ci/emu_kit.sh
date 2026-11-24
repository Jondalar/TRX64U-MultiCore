#!/usr/bin/env bash
# T239.1 D23: the kit the emulator suites need, made in the run from public sources (the repository must not hold it). A directory:
#   KIT/roms             roms/ of GideonZ/1541ultimate at the commit pinned below (git checks every byte through the commit id)
#   KIT/flash.bin        an erased flash in which the emulator formats /flash and writes the C64 ROMs of KIT/roms before boot (ue2emu run --c64-roms),
#                        on the machine of tests/hostapp/emu_host.sh (--board c64u --trx-io=on, no --caps: the capability word picks the /flash layout).
#                        No updater: the scenarios load the apps as ELF and read neither bitstream nor application from the flash, and ue2emu's install
#                        runs a 100T (no --caps) with an S25FL128L (no W25Q), which our tester and emulator-test updaters both refuse (T239.1 §5, step 17)
#   KIT/profiles/VIC-20.toml   docs/examples/core-vic20.toml of the emulator's pinned release, SHA-256 checked
#   KIT/cors/vc20.cor    this run's VIC-20 .cor (the host finds its partial by the SHA-256, so any .cor of the same core serves)
#   With --core amiga1200 (T213.3 P5, the A1200's lane of the matrix) the kit holds its large .cor as KIT/cors/amiga1200.cor and
#   KIT/profiles/amiga1200.toml, docs/examples/core-amiga1200.toml of the same release (SHA-256 checked) with the line symbols = "coreapp.elf" after
#   its name, as the emulator's own host smoke test of that release writes it (scripts/smoke-s44-host.sh): the round reads the app's symbols
#
#   ci/emu_kit.sh KIT --firmware <hostapp_emu.elf> --cor <core.cor> [--core vc20|amiga1200] [--roms DIR] [--roms-tree ID] [--profile FILE --profile-sha256 HEX]
#     --roms DIR: use a restored copy of the ROM folder instead of fetching it (actions/cache of the workflow, keyed by the commit pinned below; the folder is
#                 checked against the git tree id of roms/ at that commit, so a cache entry that holds anything else is refused)
#     --roms-tree ID: tests only: another tree id
#     --profile FILE: tests only: a local profile, with its own hash
# env: UE2EMU_BIN or `ue2emu` on PATH (ci/ue2emu.sh). The emulator is the one of the emu image (ci/emu/Dockerfile).
# The run's script quits at once, so the flash holds what the emulator wrote before boot.
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
GIDEON_REPO="https://github.com/GideonZ/1541ultimate.git"
GIDEON_COMMIT="700c7f2fab0b7d52da418d21f0c2e65e99725805"
GIDEON_ROMS_TREE="e308506696d7507486b7b0981eeacc4ae5bfe373"     # git tree id of roms/ at that commit (gh api repos/GideonZ/1541ultimate/git/trees/<commit>)
UE2_EXAMPLES="https://raw.githubusercontent.com/Jondalar/UE2-C64U-Emulator/4612884e7a633d52375277060f1a7e9bbf6552ba/docs/examples"
UE2_PROFILE_SHA256="828d90974b921a440b2c83e1ef2fddac1c1d4de12d39efef1b6f77dac47cd24a"            # core-vic20.toml
UE2_A1200_SHA256="e4995bf9ecd4c17e3818c00e5e3d9c3d30196cff7bfa1d8989a6c15daf6a655d"              # core-amiga1200.toml
kit="${1:?usage: ci/emu_kit.sh KIT --firmware FILE.elf --cor FILE.cor [--roms DIR]}"; shift
fw="" cor="" core=vc20 roms="" roms_tree="$GIDEON_ROMS_TREE" prof="" prof_sha=""
while [ $# -gt 0 ]; do
  case "$1" in
    --firmware) fw="$2"; shift 2 ;; --cor) cor="$2"; shift 2 ;; --core) core="$2"; shift 2 ;; --roms) roms="$2"; shift 2 ;; --roms-tree) roms_tree="$2"; shift 2 ;;
    --profile) prof="$2"; shift 2 ;; --profile-sha256) prof_sha="$2"; shift 2 ;;
    *) echo "emu_kit: unknown argument $1" >&2; exit 2 ;;
  esac
done
[ -f "$fw" ] && [ -f "$cor" ] || { echo "emu_kit: --firmware and --cor must name existing files (this run's hostapp_emu.elf and the core's .cor)" >&2; exit 1; }
case "$core" in
  vc20)      prof_src=core-vic20.toml prof_out=VIC-20.toml; [ -n "$prof_sha" ] || prof_sha="$UE2_PROFILE_SHA256" ;;
  amiga1200) prof_src=core-amiga1200.toml prof_out=amiga1200.toml; [ -n "$prof_sha" ] || prof_sha="$UE2_A1200_SHA256" ;;
  *) echo "emu_kit: --core is vc20 or amiga1200, not $core" >&2; exit 2 ;;
esac
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
# core.attributesFile and core.excludesFile point at nothing. A .gitignore or .gitattributes inside the folder (the pinned folder holds one at
# roms/c64rom/.gitignore) is an entry of the id like any file: whatever a planted one would hide or convert, it is itself an extra entry, so the id changes.
# Refused first, because git add skips them without a word: a name equal to .git (any case), an empty folder, and an entry that is not a file, a folder
# or a link (a FIFO, a socket, a device). The pinned folder is a git checkout copied with cp -R, so it never holds an empty folder.
tree_of() {
  if [ -n "$(find "$1" \( -iname '.git' -o \( -type d -empty \) -o ! \( -type f -o -type d -o -type l \) \) -print -quit)" ]; then
    echo "emu_kit: the ROM folder holds a name .git, an empty folder or an entry that is no file, folder or link" >&2; echo refused; return 0
  fi
  local g; g="$(mktemp -d)"; git -C "$g" init -q
  git -C "$g" -c core.autocrlf=false -c core.safecrlf=false -c core.attributesFile=/dev/null -c core.excludesFile=/dev/null --work-tree="$1" add -A -f
  git -C "$g" write-tree; rm -rf "${g:?}"
}
[ "$(tree_of "$kit/roms")" = "$roms_tree" ] || { echo "emu_kit: the ROM folder is not the roms/ of $GIDEON_COMMIT (tree id $roms_tree)" >&2; exit 1; }
[ -n "$(ls -A "$kit/roms")" ] || { echo "emu_kit: the ROM folder is empty" >&2; exit 1; }
if [ -z "$prof" ]; then prof="$kit/profiles/.download"; curl -fsSL --retry 3 -o "$prof" "$UE2_EXAMPLES/$prof_src"; fi
[ "$(sha256 "$prof")" = "$prof_sha" ] || { echo "emu_kit: the profile $prof_src does not match its SHA-256 $prof_sha" >&2; exit 1; }
if [ "$core" = vc20 ]; then cp "$prof" "$kit/profiles/$prof_out"
else awk '{ print } /^name = "amiga1200"$/ { print "symbols = \"coreapp.elf\"" }' "$prof" > "$kit/profiles/$prof_out"   # awk: a newline in a sed replacement is GNU only
  grep -q '^symbols = "coreapp.elf"' "$kit/profiles/$prof_out" || { echo "emu_kit: $prof_src has no line name = \"amiga1200\" to put the symbols after" >&2; exit 1; }
fi
rm -f "$kit/profiles/.download"
cp "$cor" "$kit/cors/$core.cor"
rm -f "$kit/flash.bin"
printf 'quit\n' > "$kit/.flash.ctl"
"$HERE/ue2emu.sh" run --board c64u --trx-io=on --firmware "$fw" --roms "$kit/roms" --c64-roms --flash "$kit/flash.bin" --headless --speed max \
  --script "$kit/.flash.ctl" --max-seconds 60
rm -f "$kit/.flash.ctl"
[ -s "$kit/flash.bin" ] || { echo "emu_kit: ue2emu run --c64-roms left no flash.bin" >&2; exit 1; }
echo "emu_kit: $kit holds roms ($(find "$kit/roms" -type f | wc -l | tr -d ' ') files), flash.bin ($(wc -c < "$kit/flash.bin" | tr -d ' ') bytes), profiles/$prof_out, cors/$core.cor"
