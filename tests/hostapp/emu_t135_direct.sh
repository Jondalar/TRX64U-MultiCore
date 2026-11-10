#!/usr/bin/env bash
# T135 S1-S7 of the direct boot in ue2emu, the host's whole flow: boot.cfg on the emulated flash, the real hostapp_emu.elf, a VIC-20 .cor on an emulated stick,
# the core's own application (coreapp.elf) for the way back. Not part of CI: the emulator lives in ../u64-emulator (UE2EMU_DIR), macOS only.
#
#   tests/hostapp/emu_t135_direct.sh --kit <dir> [--old-elf <hostapp_emu.elf of a build before the fire guard>] [--keep]
#
# --kit holds hostapp_emu.elf (a build of this tree with HOSTAPP_EXTRA=-DHOSTAPP_EMU_FLASH), coreapp.elf + coreapp.app (same build) and vc20.cor (and amiga1200.cor, which only fills the stick).
# --old-elf is the red case of S3 (Keyboard_C64 turned the fire button of port 1 into a RETURN, which the menu then replayed: the default started anyway); it must FAIL there.
# Prints one PASS or FAIL line per scenario; the exit code is the number of FAILs. Every assertion reads the console log the host wrote, or boot.cfg read back from the flash image
# (FAT at 0x580000, tests/updater/fatread.py), never a picture.
set -uo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
U="${UE2EMU_DIR:-$ROOT/../u64-emulator}"
BIN="$U/target/release/ue2emu"
kit="" old="" keep=0
while [[ $# -gt 0 ]]; do
  case "$1" in
    --kit) kit="$2"; shift 2 ;;
    --old-elf) old="$2"; shift 2 ;;
    --keep) keep=1; shift ;;
    *) echo "emu_t135_direct: unknown argument $1" >&2; exit 2 ;;
  esac
done
for f in hostapp_emu.elf coreapp.elf coreapp.app vc20.cor; do [[ -f "$kit/$f" ]] || { echo "emu_t135_direct: need --kit with $f" >&2; exit 2; }; done
[[ -x "$BIN" ]] || { echo "emu_t135_direct: no emulator at $BIN" >&2; exit 2; }
NEW="$kit/hostapp_emu.elf"
W="$(mktemp -d "${TMPDIR:-/tmp}/emu_t135.XXXXXX")"
[[ $keep = 1 ]] && echo "work directory: $W" || trap 'rm -rf "$W"' EXIT
cd "$W" || exit 2
mkdir -p cors profiles stick/cores/vc20/roms
# the .cor must carry the image whose ELF gives the symbols (a mismatch fires the fault hook on a wrong address, C64U_emu 2026-11-08)
python3 "$ROOT/tools/cor_pack.py" replace-app "$kit/vc20.cor" --app "$kit/coreapp.app" -o cors/vc20.cor > /dev/null || exit 2; cp cors/vc20.cor stick/cores/
[[ -f "$kit/amiga1200.cor" ]] && cp "$kit/amiga1200.cor" cors/ && cp "$kit/amiga1200.cor" stick/cores/
cp "$kit/coreapp.elf" profiles/; cp "$U/docs/examples/core-vic20.toml" profiles/VIC-20.toml
head -c 8192 /dev/zero > stick/cores/vc20/roms/basic.bin; head -c 8192 /dev/zero > stick/cores/vc20/roms/kernal.bin; head -c 4096 /dev/zero > stick/cores/vc20/roms/char.bin

# emu NAME ELF FLASH_TEMPLATE STICK  (script NAME.ctl; flash after the run: NAME-flash.bin, log NAME.log)
emu() {
  cp "$3" "$1-flash.bin"; rm -rf "usbwork-$1"
  nice -n 15 timeout 400 "$BIN" run --board c64u --trx-io=on --icap --cor-dir cors --core-dir profiles --firmware "$2" --roms "$U/firmware/1541ultimate/roms" \
    --flash "$1-flash.bin" --usb-dir "$4" --usb-dir-work "usbwork-$1" --headless --speed max --script "$1.ctl" --max-seconds 380 > "$1.log" 2>&1
}
cfg() { python3 -I - "$1" "$ROOT/tests/updater" <<'PY'     # boot.cfg of a flash image, printed
import sys
sys.path.insert(0, sys.argv[2])
from fatread import Fat
f = Fat(open(sys.argv[1], "rb").read()[0x580000:])
e = f.find("boot.cfg")
sys.stdout.write(f.read(e).decode() if e else "NO BOOT.CFG\n")
PY
}
setdirect() { python3 -I - "$1" "$2" "$ROOT/tests/updater" <<'PY'     # boot.cfg direct=<v> in place (same length, the file's own cluster)
import sys
sys.path.insert(0, sys.argv[3])
from fatread import Fat
raw = bytearray(open(sys.argv[1], "rb").read())
f = Fat(bytes(raw[0x580000:]))
e = f.find("boot.cfg")
o = 0x580000 + (f.datastart + (e["cl"] - 2) * f.spc) * f.bps
i = raw.index(b"direct=", o) + 7
raw[i] = ord(sys.argv[2])
open(sys.argv[1], "wb").write(raw)
PY
}
fails=0
pass() { echo "PASS $1"; }
fail() { echo "FAIL $1"; fails=$((fails + 1)); }
has() { grep -aq -- "$2" "$1"; }
count() { grep -ac -- "$2" "$1"; }
jumped() { has "$1" '\[host\] step 4: Jump' || has "$1" 'direct boot: jump'; }
check() {   # check NAME  cond...   (a command; PASS when it succeeds)
  local n=$1; shift
  if "$@"; then pass "$n"; else fail "$n"; fi
}

# --- prep: the browser sets the VIC-20 as default (boot.cfg is written by the host itself), direct=1
printf 'wait 15000\nkey space\nwait 800\nkey f3\nwait 1500\nkey down\nwait 500\nkey down\nwait 500\nkey return\nwait 2000\nkey down\nwait 500\nkey return\nwait 2000\n' > prep.ctl
printf 'key down\nkey down\nkey down\nkey down\nwait 500\nkey a\nwait 8000\nkey down\nwait 500\nkey a\nwait 8000\nquit\n' >> prep.ctl
emu prep "$NEW" "$U/run/flash.bin" stick
cfg prep-flash.bin > prep.cfg
if grep -q '^direct=1' prep.cfg && grep -q '^default=0' prep.cfg && grep -q '^core=/USB0/cores/vc20.cor|VIC-20|' prep.cfg; then echo "prep ok: $(grep '^core=' prep.cfg | cut -c1-60)"; else echo "FAIL prep: boot.cfg is not direct=1 with the VIC-20 as default"; cat prep.cfg; exit 1; fi
P=prep-flash.bin

s1() {
  printf 'wait 25000\nquit\n' > s1.ctl; emu s1 "$NEW" $P stick
  check "S1 no key: decision START, direct boot jump, no menu build (decision at $(grep -a 'decision START' s1.log | head -1 | sed 's/^\[t+\([0-9]*\) ms.*/\1/') ms, jump at $(grep -a 'direct boot: jump' s1.log | head -1 | sed 's/^\[t+\([0-9]*\) ms.*/\1/') ms)" \
    bash -c "grep -aq 'direct boot: decision START' s1.log && grep -aq '\[host\] direct boot: VIC-20' s1.log && grep -aq 'direct boot: jump' s1.log && ! grep -aq 'phase Building the menu' s1.log"
}
s2() {
  printf 'hold cbm\nwait 3000\nrelease cbm\nwait 25000\nquit\n' > s2.ctl; emu s2 "$NEW" $P stick
  check "S2 C= held: menu built, nothing starts (control: the S1 run of the same flash does start)" \
    bash -c "grep -aq 'decision Menu: C= was held' s2.log && grep -aq 'phase Building the menu' s2.log && ! grep -aq 'direct boot: jump' s2.log && ! grep -aq 'step 4: Jump' s2.log && ! grep -aq 'VIC-20 released' s2.log && grep -aq 'direct boot: jump' s1.log"
}
s3() {
  printf 'joy-hold 1 fire\nwait 3000\njoy-release 1\nwait 25000\nquit\n' > s3.ctl; emu s3 "$NEW" $P stick
  check "S3 fire port 1 held: menu built, nothing starts after the release (FireGuard)" \
    bash -c "grep -aq 'decision Menu: fire on port 1 was held' s3.log && grep -aq 'phase Building the menu' s3.log && ! grep -aq 'step 4: Jump' s3.log && ! grep -aq 'VIC-20 released' s3.log"
}
s3old() {
  [[ -n "$old" && -f "$old" ]] || { fail "S3 control: --old-elf missing, the red case did not run"; return; }
  cp s3.ctl s3old.ctl; emu s3old "$old" $P stick
  check "S3 control: the elf before the fire guard starts the default after the release (red as it must be)" \
    bash -c "grep -aq 'decision Menu: fire on port 1 was held' s3old.log && grep -aq 'step 4: Jump' s3old.log"
}
s4() {
  rm -rf st4; cp -R stick st4; rm st4/cores/vc20.cor
  printf 'wait 40000\nquit\n' > s4.ctl; emu s4 "$NEW" $P st4
  check "S4 default missing: reason on the row, no jump, one decision (no restart loop)" \
    bash -c "grep -aq 'decision Default core: file missing' s4.log && ! grep -aq 'step 4: Jump' s4.log && ! grep -aq 'direct boot: jump' s4.log && [ \$(grep -ac 'direct boot: decision begins' s4.log) = 1 ] && grep -aq 'phase Building the menu' s4.log"
  rm -rf st4c; cp -R stick st4c; touch -t 202001010000 st4c/cores/vc20.cor
  printf 'wait 40000\nquit\n' > s4c.ctl; emu s4c "$NEW" $P st4c
  check "S4 default changed (same bytes, other time stamp): reason, no jump" \
    bash -c "grep -aq 'decision Default core: file changed' s4c.log && ! grep -aq 'step 4: Jump' s4c.log && ! grep -aq 'direct boot: jump' s4c.log"
  rm -rf st4r; cp -R stick st4r; head -c 4096 /dev/urandom >> st4r/cores/vc20.cor
  printf 'wait 40000\nquit\n' > s4r.ctl; emu s4r "$NEW" $P st4r
  check "S4 default refused (file grown, no longer a bundle): reason, no jump" \
    bash -c "grep -aq 'decision Default core: refused' s4r.log && ! grep -aq 'step 4: Jump' s4r.log && ! grep -aq 'direct boot: jump' s4r.log"
}
s5() {
  # the core runs (S1), its hotkey (CTRL+RESTORE) opens the core's menu, F8, the last entry "Return to host menu": IPROG WBSTAR 0 with the marker
  printf 'wait 30000\nhold ctrl\nwait 300\nrestore\nwait 300\nrelease ctrl\nwait 2000\nkey f8\nwait 1500\n' > s5.ctl
  for _ in $(seq 15); do printf 'key down\nwait 300\n' >> s5.ctl; done
  printf 'key return\nwait 40000\nquit\n' >> s5.ctl
  emu s5 "$NEW" $P stick
  check "S5 return to host with the marker: menu stays, the default is not started again (control: S1, same flash, marker 0, starts)" \
    bash -c "grep -aq 'icap: IPROG WBSTAR 0: the host bitstream, .* the menu marker set' s5.log && grep -aq 'decision Menu requested by the core (.*marker 1' s5.log && [ \$(grep -ac 'direct boot: jump' s5.log) = 1 ] && [ \$(grep -ac 'direct boot: decision START' s5.log) = 1 ] && grep -aq 'marker 0, C= 0, fire 0' s1.log"
}
s6() {
  cp $P p6.bin; setdirect p6.bin 0; cfg p6.bin | grep -q '^direct=0' || { fail "S6 setup: direct=0 not patched"; return; }
  printf 'wait 40000\nquit\n' > s6.ctl; emu s6 "$NEW" p6.bin stick
  check "S6 direct=0: decision \"Direct boot is off\", menu built, countdown ends in the start " \
    bash -c "grep -aq 'decision Direct boot is off' s6.log && grep -aq 'phase Building the menu' s6.log && ! grep -aq 'direct boot: jump' s6.log && grep -aq 'step 4: Jump' s6.log"
}
s7() {
  printf 'hold cbm\nwait 3000\nrelease cbm\nwait 15000\nkey f8\nwait 1500\nkey return\nwait 2000\nquit\n' > s7.ctl; emu s7 "$NEW" $P stick
  cfg s7-flash.bin > s7.cfg
  check "S7 F8 \"Direct boot\" row toggles: boot.cfg read back from the flash image says direct=0 (was direct=1)" \
    bash -c "grep -q '^direct=0' s7.cfg && grep -q '^direct=1' prep.cfg"
  printf 'wait 40000\nquit\n' > s7b.ctl; emu s7b "$NEW" s7-flash.bin stick
  check "S7 then power-on: as S6 (Direct boot is off, menu, the countdown starts the core)" \
    bash -c "grep -aq 'decision Direct boot is off' s7b.log && ! grep -aq 'direct boot: jump' s7b.log && grep -aq 'step 4: Jump' s7b.log"
}
s1; s2; s3; s3old; s4; s5; s6; s7
echo "emu_t135_direct: $fails FAIL"
exit $fails
