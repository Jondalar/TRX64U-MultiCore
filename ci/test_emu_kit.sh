#!/usr/bin/env bash
# T239.1 D23, step 17: ci/emu_kit.sh makes the kit with a fake ue2emu (local ROM folder and profile, no network); a wrong profile hash and a missing input are refused.
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/.."
W="$(mktemp -d)"; trap 'rm -rf "${W:?}"' EXIT
fail() { echo "FAIL: $*"; exit 1; }
mkdir -p "$W/bin" "$W/roms"
cat > "$W/bin/ue2emu" <<'EOS'
#!/usr/bin/env bash
# the kit's run line: the machine of tests/hostapp/emu_host.sh (--board c64u --trx-io=on, no --caps), the C64 ROMs before boot, a script
a=" $* "
case "$a" in *" --caps "*) echo "the kit sets --caps: $*" >&2; exit 3 ;; esac
for w in " run " " --board c64u " " --trx-io=on " " --c64-roms "; do case "$a" in *"$w"*) ;; *) echo "bad ue2emu line, no$w: $*" >&2; exit 3 ;; esac; done
fw="" fl="" roms="" ctl=""
while [ $# -gt 0 ]; do case "$1" in --firmware) fw="$2" ;; --flash) fl="$2" ;; --roms) roms="$2" ;; --script) ctl="$2" ;; esac; shift; done
[ -f "$fw" ] && [ -d "$roms" ] && [ -f "$ctl" ] && [ -n "$fl" ] || exit 4
printf 'flash for %s\n' "$(basename "$fw")" > "$fl"
EOS
chmod +x "$W/bin/ue2emu"
printf 'rom\n' > "$W/roms/kernal.bin"; mkdir -p "$W/roms/sub"; printf '*.tmp\n' > "$W/roms/sub/.gitignore"; printf 'elf\n' > "$W/hostapp_emu.elf"; printf 'cor\n' > "$W/vc20.cor"; printf 'name = "vc20"\n' > "$W/p.toml"
sha="$(python3 -I -c 'import hashlib,sys; print(hashlib.sha256(open(sys.argv[1],"rb").read()).hexdigest())' "$W/p.toml")"
tree="$(g=$(mktemp -d); git -C "$g" init -q; git -C "$g" -c core.autocrlf=false -c core.excludesFile=/dev/null --work-tree="$W/roms" add -A -f; git -C "$g" write-tree; rm -rf "$g")"
run() { PATH="$W/bin:$PATH" UE2EMU_BIN="$W/bin/ue2emu" ci/emu_kit.sh "$W/kit" --firmware "$W/hostapp_emu.elf" --cor "$W/vc20.cor" --roms "$W/roms" --roms-tree "${2:-$tree}" --profile "$W/p.toml" --profile-sha256 "$1" > "$W/out" 2>&1; }
run "$sha" || { cat "$W/out"; fail "the kit was not made"; }
[ "$(cat "$W/kit/flash.bin")" = "flash for hostapp_emu.elf" ] || fail "flash.bin is not made by a run of this run's host image"
[ -f "$W/kit/roms/kernal.bin" ] && [ -f "$W/kit/profiles/VIC-20.toml" ] && [ -f "$W/kit/cors/vc20.cor" ] || fail "the kit lacks a part"
if run 0000000000000000000000000000000000000000000000000000000000000000; then fail "a profile with another hash was accepted"; fi
# a restored cache entry that is not the pinned folder (a planted file, the exploit) is refused; the pinned tree id is what the kit demands without the test option
printf 'planted\n' > "$W/roms/extra.bin"
if run "$sha"; then fail "a ROM folder that is not the pinned one was accepted"; fi
rm "$W/roms/extra.bin"; run "$sha" || { cat "$W/out"; fail "the pinned folder was refused"; }
# plants that a .gitignore or a .gitattributes inside the folder would hide from a plain add -A: both change the id and are refused (the folder has the pinned content otherwise)
printf '.gitignore\nextra.bin\n' > "$W/roms/.gitignore"; printf 'planted\n' > "$W/roms/extra.bin"
if run "$sha"; then fail "a ROM folder with a .gitignore and an ignored extra file was accepted"; fi
rm "$W/roms/.gitignore" "$W/roms/extra.bin"
printf '*.bin text\n' > "$W/roms/.gitattributes"; printf 'rom\r\n' > "$W/roms/kernal.bin"
if run "$sha"; then fail "a ROM folder with a .gitattributes and a converted kernal.bin was accepted"; fi
rm "$W/roms/.gitattributes"; printf 'rom\n' > "$W/roms/kernal.bin"; run "$sha" || { cat "$W/out"; fail "the pinned folder was refused after the plants"; }
# a junk .git file in either case (git add skips .git silently; on Linux it refuses .GIT by itself, so both names are planted) and a FIFO
# (git add skips it silently) are refused
for n in .git .GIT; do
  printf 'gitdir: x\n' > "$W/roms/sub/$n"
  if run "$sha"; then fail "a ROM folder with a planted $n file was accepted"; fi
  rm "$W/roms/sub/$n"
done
mkfifo "$W/roms/sub/pipe.bin"
if run "$sha"; then fail "a ROM folder with a FIFO was accepted"; fi
rm "$W/roms/sub/pipe.bin"
# an empty folder (git add skips it silently, so the tree id stays the pinned one) is refused
mkdir "$W/roms/sub/empty.d"
if run "$sha"; then fail "a ROM folder with an empty folder was accepted"; fi
rmdir "$W/roms/sub/empty.d"; run "$sha" || { cat "$W/out"; fail "the pinned folder was refused after the .git, FIFO and empty-folder plants"; }
rm "$W/hostapp_emu.elf"
if run "$sha"; then fail "a missing host image was accepted"; fi
echo "emulator kit: ok"
