#!/usr/bin/env bash
# T239.1 D23: ci/emu_kit.sh makes the kit with a fake ue2emu (local ROM folder and profile, no network); a wrong profile hash and a missing input are refused.
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/.."
W="$(mktemp -d)"; trap 'rm -rf "${W:?}"' EXIT
fail() { echo "FAIL: $*"; exit 1; }
mkdir -p "$W/bin" "$W/roms"
cat > "$W/bin/ue2emu" <<'EOS'
#!/usr/bin/env bash
# the arguments of D23's install line, in this order
[ "$1" = install ] && [ "$2" = --update ] && [ "$4" = --flash ] && [ "$6" = --roms ] && [ "$8" = --yes ] && [ "$9" = --c64-roms ] || { echo "bad ue2emu line: $*" >&2; exit 3; }
[ -f "$3" ] && [ -d "$7" ] || exit 4
printf 'flash from %s\n' "$(basename "$3")" > "$5"
EOS
chmod +x "$W/bin/ue2emu"
printf 'rom\n' > "$W/roms/kernal.bin"; printf 'ue2\n' > "$W/x.ue2"; printf 'cor\n' > "$W/vc20.cor"; printf 'name = "vc20"\n' > "$W/p.toml"
sha="$(python3 -I -c 'import hashlib,sys; print(hashlib.sha256(open(sys.argv[1],"rb").read()).hexdigest())' "$W/p.toml")"
tree="$(g=$(mktemp -d); git -C "$g" init -q; git -C "$g" -c core.autocrlf=false -c core.excludesFile=/dev/null --work-tree="$W/roms" add -A -f; git -C "$g" write-tree; rm -rf "$g")"
run() { PATH="$W/bin:$PATH" UE2EMU_BIN="$W/bin/ue2emu" ci/emu_kit.sh "$W/kit" --update "$W/x.ue2" --cor "$W/vc20.cor" --roms "$W/roms" --roms-tree "${2:-$tree}" --profile "$W/p.toml" --profile-sha256 "$1" > "$W/out" 2>&1; }
run "$sha" || { cat "$W/out"; fail "the kit was not made"; }
[ "$(cat "$W/kit/flash.bin")" = "flash from x.ue2" ] || fail "flash.bin is not made from this run's .ue2"
[ -f "$W/kit/roms/kernal.bin" ] && [ -f "$W/kit/profiles/VIC-20.toml" ] && [ -f "$W/kit/cors/vc20.cor" ] || fail "the kit lacks a part"
if run 0000000000000000000000000000000000000000000000000000000000000000; then fail "a profile with another hash was accepted"; fi
# a restored cache entry that is not the pinned folder (a planted file, the exploit) is refused; the pinned tree id is what the kit demands without the test option
printf 'planted\n' > "$W/roms/extra.bin"
if run "$sha"; then fail "a ROM folder that is not the pinned one was accepted"; fi
rm "$W/roms/extra.bin"; run "$sha" || { cat "$W/out"; fail "the pinned folder was refused"; }
# plants that a .gitignore or a .gitattributes inside the folder would hide from a plain add -A: both are refused (the folder has the pinned content otherwise)
printf '.gitignore\nextra.bin\n' > "$W/roms/.gitignore"; printf 'planted\n' > "$W/roms/extra.bin"
if run "$sha"; then fail "a ROM folder with a .gitignore and an ignored extra file was accepted"; fi
rm "$W/roms/.gitignore" "$W/roms/extra.bin"
printf '*.bin text\n' > "$W/roms/.gitattributes"; printf 'rom\r\n' > "$W/roms/kernal.bin"
if run "$sha"; then fail "a ROM folder with a .gitattributes and a converted kernal.bin was accepted"; fi
rm "$W/roms/.gitattributes"; printf 'rom\n' > "$W/roms/kernal.bin"; run "$sha" || { cat "$W/out"; fail "the pinned folder was refused after the plants"; }
rm "$W/x.ue2"
if run "$sha"; then fail "a missing .ue2 was accepted"; fi
echo "emulator kit: ok"
