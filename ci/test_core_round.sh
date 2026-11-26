#!/usr/bin/env bash
# T239.5 D18: step 12 of a core lane, tests/coreapp/emu/core_round.sh, with a fake ue2emu (no emulator, no ROMs): the machine it starts (the core's profile,
# no C64, the .cor's app as the firmware, the .cor and the ROM stand-ins on the stick), the menu key it sends (the hotkey with RESTORE, the menu button for a
# core without one), the steps it reads from the console in order (a missing one is named), and tools/ue2emu_core.py firmware (the .cor's app in the ELF:
# the heap word may differ, nothing else).
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/.."
W="$(mktemp -d)"; trap 'rm -rf "${W:?}"' EXIT
fail() { echo "FAIL: $*"; exit 1; }
mkdir -p "$W/bin" "$W/roms"
# the fake emulator: `run --help` answers; a run checks the machine, keeps its control script and stick listing, writes one file per png line, prints the
# console of the scenario ($FAKE_LOG) and exits $FAKE_RC
cat > "$W/bin/ue2emu" <<'EOS'
#!/usr/bin/env bash
[ "$1 ${2:-}" != "run --help" ] || exit 0
a=" $* "
for w in " run " " --board c64u " " --trx-io=on " " --c64 none " " --headless "; do case "$a" in *"$w"*) ;; *) echo "bad ue2emu line, no$w: $*" >&2; exit 3 ;; esac; done
case "$a" in *" --cor-dir "*|*" --core-dir "*|*" --icap "*) echo "the round starts no host: $*" >&2; exit 3 ;; esac
prof="" fw="" stick="" ctl=""
while [ $# -gt 0 ]; do case "$1" in --core) prof="$2" ;; --firmware) fw="$2" ;; --usb-dir) stick="$2" ;; --script) ctl="$2" ;; esac; shift; done
[ -f "$prof" ] && [ -f "$fw" ] && [ -d "$stick" ] && [ -f "$ctl" ] || { echo "a file of the run is missing" >&2; exit 4; }
cp "$ctl" "$FAKE_DIR/ctl"; cp "$fw" "$FAKE_DIR/fw.elf"; echo "$prof" > "$FAKE_DIR/prof"
( cd "$stick" && find . -type f | sort ) > "$FAKE_DIR/stick"
while read -r cmd arg _; do [ "$cmd" != png ] || echo "picture $(basename "$arg")" > "$arg"; done < "$ctl"
cat "$FAKE_LOG"
exit "${FAKE_RC:-0}"
EOS
chmod +x "$W/bin/ue2emu"
printf 'rom\n' > "$W/roms/font.bin"
# a .cor and the ELF of its app: the app at 0x30000 with one heap record, its heap word set by the packer (0) where the link has the default (0x7A0000)
mk() {   # mk NAME MANIFEST_JSON [ELF_CHANGE]: $W/NAME.cor and $W/NAME.elf; ELF_CHANGE "other" changes a byte outside the heap word, "size" adds one
    python3 -I - "$W/$1" "$2" "${3:-}" <<'PY'
import hashlib, json, struct, sys
out, man, change = sys.argv[1], json.loads(sys.argv[2]), sys.argv[3]
app = bytearray(b"\x13\x00\x00\x00" + bytes(60) + b"TRXHEAP:" + struct.pack("<4I", 0, 0x30100, 0x800000, 0x1000) + bytes(40))
link = bytearray(app); link[72:76] = struct.pack("<I", 0x7A0000)
if change == "other":
    link[8] = 0x55
elif change == "size":
    link += b"\0"
man.update(app_load=0x30000, app_entry=0x30000, app_size=len(app), app_sha256=hashlib.sha256(app).hexdigest())
m = json.dumps(man).encode()
body = struct.pack("<II", 1, len(m)) + m + struct.pack("<II", 3, len(app)) + app
open(out + ".cor", "wb").write(b"TRXCOR01" + struct.pack("<I", len(body)) + body)
ph = struct.pack("<8I", 1, 0x1000, 0x30000, 0x30000, len(link), len(link), 5, 0x1000)
eh = b"\x7fELF\x01\x01\x01" + bytes(9) + struct.pack("<HHIIIIIHHHHHH", 2, 0xF3, 1, 0x30000, 52, 0, 0, 52, 32, 1, 40, 0, 0)
open(out + ".elf", "wb").write(eh + ph + bytes(0x1000 - len(eh) - len(ph)) + link)
PY
}
mk vc20 '{"name": "VIC-20"}'
mk ddt '{"name": "DDT Look Ma, No VIC"}'
mk nes '{"kind": "large", "id": "nes", "name": "Nintendo Entertainment System"}'
mk other '{"name": "VIC-20"}' other
mk size '{"name": "VIC-20"}' size

# tools/ue2emu_core.py firmware: the ELF with the .cor's app; another byte or another size is not the link of this app
python3 -I tools/ue2emu_core.py firmware "$W/vc20.cor" "$W/vc20.elf" "$W/fw.elf" > /dev/null || fail "firmware refused the link of the .cor's app"
python3 -I - "$W" <<'PY' || fail "the firmware's segment is not the .cor's app"
import struct, sys
w = sys.argv[1]
cor, fw = open(w + "/vc20.cor", "rb").read(), open(w + "/fw.elf", "rb").read()
n, = struct.unpack_from("<I", cor, 16); off = 20 + n
assert struct.unpack_from("<I", cor, off)[0] == 3
size, = struct.unpack_from("<I", cor, off + 4)
sys.exit(0 if fw[0x1000:0x1000 + size] == cor[off + 8:off + 8 + size] and fw[:0x1000] == open(w + "/vc20.elf", "rb").read()[:0x1000] else 1)
PY
for bad in other size; do
    if python3 -I tools/ue2emu_core.py firmware "$W/$bad.cor" "$W/$bad.elf" "$W/fw.elf" > /dev/null 2>&1; then fail "firmware took an ELF that is not the link ($bad)"; fi
done

say() { printf '%s\n' "$@" > "$W/log"; }
round() {   # round CORE NAME: the round of CORE on $W/NAME.cor, $W/NAME.elf; its output in $W/out
    rm -rf "$W/fake" "$W/o"; mkdir -p "$W/fake"
    FAKE_DIR="$W/fake" FAKE_LOG="$W/log" UE2EMU="$W/bin/ue2emu" UE2EMU_ROMS="$W/roms" tests/coreapp/emu/core_round.sh "$1" "$W/$2.cor" "$W/$2.elf" "$W/o" > "$W/out" 2>&1
}
id="[t+340 ms] phase core identified: [core] VC20 VIC-20, cor /Usb0/cores/vc20.cor"
up="[t+2590 ms] phase boot_start returned (running)"
say "$id" "$up" "[core] menu open 709 ms after the request" "[core] menu closed, machine running 12 ms after the close key"
round vc20 vc20 || { cat "$W/out"; fail "the VIC-20's round did not pass"; }
grep -q '^PASS vc20 alone: VC20 identified (VIC-20), started, the hotkey ctrl+RESTORE opened the menu and closed it' "$W/out" || { cat "$W/out"; fail "no PASS line"; }
[ "$(cat "$W/fake/prof")" = "$PWD/cores/vc20/app/ue2emu.toml" ] || fail "the round does not run the core's profile"
cmp -s "$W/fake/fw.elf" "$W/fw.elf" || fail "the firmware is not the .cor's app in its ELF"
grep -qx './cores/vc20.cor' "$W/fake/stick" && grep -qx './cores/vc20/roms/basic.bin' "$W/fake/stick" || { cat "$W/fake/stick"; fail "the stick lacks the .cor or a ROM stand-in"; }
[ "$(grep -c '^hold ctrl$' "$W/fake/ctl")" = 2 ] && [ "$(grep -c '^restore$' "$W/fake/ctl")" = 2 ] || { cat "$W/fake/ctl"; fail "the hotkey is not pressed twice"; }
grep -qF 'expect-console "phase core identified: [core] VC20 VIC-20," ' "$W/fake/ctl" || fail "the control script does not wait for the identification"
[ -s "$W/o/core-menu.png" ] && [ -s "$W/o/core-back.png" ] && [ ! -e "$W/o/work" ] || fail "the pictures are missing or the work folder stayed"
# a missing step is named: the close, the identification (another core's line), the start (the ROM page's line is shown)
say "$id" "$up" "[core] menu open 709 ms after the request"
if round vc20 vc20; then fail "a round without the close passed"; fi
grep -q '^FAIL vc20 alone: the hotkey ctrl+RESTORE did not close the menu' "$W/out" || { cat "$W/out"; fail "the missing close is not named"; }
say "[t+340 ms] phase core identified: [core] PET1 Commodore PET, cor /Usb0/cores/pet.cor" "$up" "[core] menu open 1 ms" "[core] menu closed, 1 ms"
if round vc20 vc20; then fail "another core's identification passed"; fi
grep -q '^FAIL vc20 alone: the core app did not identify VC20 (.*PET1 Commodore PET' "$W/out" || { cat "$W/out"; fail "the wrong identification is not named"; }
say "$id" "[coreapp] ROM files: ROM files of the VIC-20 in cores/vc20/roms:" "wrong content: kernal.bin" \
    '    1: expect-console "phase boot_start returned (running)": not in the console output within 120000 ms emulated'
# D20: a core held on its ROM page ends its round there - identified, then the app's own ROM page line, and the picture of the page
round vc20 vc20 || { cat "$W/out"; fail "a core held on its ROM page did not pass"; }
grep -q '^PASS vc20 alone: .* waits on its ROM page: .*wrong content: kernal.bin' "$W/out" && [ -s "$W/o/core-rompage.png" ] || { cat "$W/out"; fail "no PASS line or no picture of the ROM page"; }
# a ROM page without the identification before it, or after the core started, is no proof
say "[coreapp] ROM files: ROM files of the VIC-20 in cores/vc20/roms:" "wrong content: kernal.bin"
if round vc20 vc20; then fail "a ROM page without the core's identification passed"; fi
say "$id" "phase boot_start returned (running)" "[coreapp] ROM files: x"
if round vc20 vc20; then fail "a ROM page behind the core's start passed as a held core"; fi
# the emulator's report of the control line that failed quotes the line's text: it is no step
say "$id" "$up" "[core] menu open 709 ms after the request" '    1: expect-console "[core] menu closed": not in the console output within 30000 ms emulated'
if round vc20 vc20; then fail "the emulator's report of the missing close passed as the close"; fi
grep -q '^FAIL vc20 alone: the hotkey ctrl+RESTORE did not close the menu' "$W/out" || { cat "$W/out"; fail "the missing close behind the report is not named"; }
# the steps in order: a close before the open does not count
say "$id" "$up" "[core] menu closed, machine running 1 ms" "[core] menu open 1 ms"
if round vc20 vc20; then fail "a close before the open passed"; fi
# the emulator's exit counts
say "$id" "$up" "[core] menu open 709 ms after the request" "[core] menu closed, machine running 12 ms after the close key"
if FAKE_RC=1 round vc20 vc20; then fail "a run whose emulator exited 1 passed"; fi
# a core without a hotkey: the menu button; a core that waits for a program: its menu opens by itself and stays open (no key: its hotkeys are off until it runs)
say "[core] phase core identified: [core] DDTN DDT Look Ma, No VIC, cor x" "$up" "[core] menu open 1 ms" "[core] menu closed, 1 ms"
round ddt_novic ddt || { cat "$W/out"; fail "the DDT core's round did not pass"; }
[ "$(grep -c '^rocker up$' "$W/fake/ctl")" = 2 ] && ! grep -q '^hold' "$W/fake/ctl" || { cat "$W/fake/ctl"; fail "a core without a hotkey does not use the menu button"; }
say "phase core identified: [core] NES_ Nintendo Entertainment System, cor x" "phase boot_start returned (waiting for a program)" "[core] menu open 1 ms"
round nes nes || { cat "$W/out"; fail "the NES's round did not pass"; }
grep -q '^PASS nes alone: NES_ identified (Nintendo Entertainment System), waits for a program with its menu open' "$W/out" \
    || { cat "$W/out"; fail "no PASS line of a core that waits for a program"; }
! grep -q -E '^(hold|restore|rocker)' "$W/fake/ctl" || { cat "$W/fake/ctl"; fail "the round of a core that waits for a program presses a menu key"; }
say "phase core identified: [core] NES_ Nintendo Entertainment System, cor x" "phase boot_start returned (waiting for a program)"
if round nes nes; then fail "a core that waits for a program passed without its menu"; fi
grep -q '^FAIL nes alone: the menu did not open' "$W/out" || { cat "$W/out"; fail "the missing menu is not named"; }
# a .cor that is not the core's is refused before the emulator runs
if round ddt_novic vc20; then fail "the VIC-20's .cor passed as the DDT core's"; fi
[ ! -e "$W/fake/ctl" ] || fail "the emulator ran for a .cor that is not the core's"
echo "core round: ok"
