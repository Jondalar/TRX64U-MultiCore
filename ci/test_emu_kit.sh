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
run() { PATH="$W/bin:$PATH" UE2EMU_BIN="$W/bin/ue2emu" ci/emu_kit.sh "$W/kit" --update "$W/x.ue2" --cor "$W/vc20.cor" --roms "$W/roms" --profile "$W/p.toml" --profile-sha256 "$1" > "$W/out" 2>&1; }
run "$sha" || { cat "$W/out"; fail "the kit was not made"; }
[ "$(cat "$W/kit/flash.bin")" = "flash from x.ue2" ] || fail "flash.bin is not made from this run's .ue2"
[ -f "$W/kit/roms/kernal.bin" ] && [ -f "$W/kit/profiles/VIC-20.toml" ] && [ -f "$W/kit/cors/vc20.cor" ] || fail "the kit lacks a part"
if run 0000000000000000000000000000000000000000000000000000000000000000; then fail "a profile with another hash was accepted"; fi
rm "$W/x.ue2"
if run "$sha"; then fail "a missing .ue2 was accepted"; fi
echo "emulator kit: ok"
