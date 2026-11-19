#!/usr/bin/env bash
# T135 D8/D9 and T132: the TRX64 updater (vendor/gideon update_trx.cc, built with --emu-test) run in ue2emu from Commodore's own firmware 1.1.0,
# the way a person starts it: file browser, USB0 or SD, Run Update. The emulator's SPI flash is a model with erase and program, the media are
# FAT images; what the updater left is read back from the flash image and the media images (emu_check.py), the screens are written as pictures.
# Not part of CI: the emulator is macOS only (../u64-emulator = UE2EMU_DIR) and so is hdiutil, which opens the media images.
#
#   tests/updater/emu_t135.sh --update <emu-test .ue2 of a MultiCore host package> --bit <its bitstream> --app <its application>
#        --cores "a.cor,b.cor,c.cor" --cores-src <dir holding them> --vendor <Commodore/Gideon .ue2> --out <dir>
#        [--old-updater <.ue2 built before T132>] [--tester-update <emu-test .ue2 of a package whose application is not the host>]
#        [--cor-pack <tools/cor_pack.py with origin/flash_id (branch t137-updater)>]
#        [--t240-control <emu-test .ue2 built from main before T240: BOOT_ENTRIES 16, BUNDLE_MAX 24, no core folders>]
#        [--title-control <emu-test .ue2 with the title as before T132: setup("  ** TRX64 Ultimate Updater **\n"), no title_in_colour>]
#
# --update is copied next to the cores; the file names of the cores are the ones the package's release bundle has. Every scenario starts from a
# fresh flash (the vendor firmware: tests/updater/make_flash.py) and fresh media. A scenario names what it proves and has a control that can fail.
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
U="${UE2EMU_DIR:-/path/to/Development/u64-emulator}"
BIN="$U/target/release/ue2emu"
upd="" bit="" app="" cores="" csrc="" vendor="" out="" old="" tester="" tctl="" t240ctl="" corpack="$HERE/../../tools/cor_pack.py"
while [[ $# -gt 0 ]]; do
  case "$1" in
    --update) upd="$2"; shift 2 ;; --bit) bit="$2"; shift 2 ;; --app) app="$2"; shift 2 ;; --cores) cores="$2"; shift 2 ;;
    --cores-src) csrc="$2"; shift 2 ;; --vendor) vendor="$2"; shift 2 ;; --out) out="$2"; shift 2 ;; --old-updater) old="$2"; shift 2 ;;
    --tester-update) tester="$2"; shift 2 ;; --cor-pack) corpack="$2"; shift 2 ;; --title-control) tctl="$2"; shift 2 ;; --t240-control) t240ctl="$2"; shift 2 ;;
    *) echo "emu_t135: unknown argument $1" >&2; exit 2 ;;
  esac
done
[[ -x "$BIN" && -f "$upd" && -f "$bit" && -f "$app" && -f "$vendor" && -n "$out" && -n "$cores" && -d "$csrc" ]] || { echo "emu_t135: missing argument or emulator at $BIN" >&2; exit 2; }
mkdir -p "$out"; out="$(cd "$out" && pwd)"
W="$(mktemp -d "${TMPDIR:-/tmp}/emu_t135.XXXXXX")"; trap 'rm -rf "$W"' EXIT
UPD_NAME="$(basename "$upd")"
python3 -I "$HERE/make_flash.py" vendor "$vendor" "$W/flash_vendor.bin" > "$out/flash_vendor.txt"
python3 -I "$HERE/make_flash.py" ours "$bit" "$app" "$W/flash_ours.bin" > "$out/flash_ours.txt"
fails=0

# a directory as a medium image: the emulator imports it as a stick and builds an MBR + FAT32 image in its work directory
mkimg() {   # name, dir -> $W/<name>.img
  mkdir -p "$W/mk_$1"
  "$BIN" run --board c64u --caps 24E40226 --headless --speed max --firmware "$vendor" --roms "$U/firmware/1541ultimate/roms" --c64-roms \
     --flash "$W/mk_$1/flash.bin" --usb-dir "$2" --usb-dir-work "$W/mk_$1/work" --script <(printf 'wait 1000\nquit\n') > /dev/null 2>&1
  cp "$W"/mk_$1/work/*/image-1.img "$W/$1.img"
}
dir_with() {   # name, files... -> directory $W/d_<name>
  mkdir -p "$W/d_$1"; local n="$1"; shift
  for f in "$@"; do cp "$f" "$W/d_$n/"; done
}
corefiles=(); IFS=',' read -r -a _c <<< "$cores"; for c in "${_c[@]}"; do corefiles+=("$csrc/$c"); done
dir_with empty
dir_with all "$upd" "${corefiles[@]}"
dir_with alone "$upd"
mkimg sd_empty "$W/d_empty"
mkimg sd_all "$W/d_all"

# navigation: Commodore's menu button, Disk file browser, root entry n (SD, Flash, Temp, USB0), file number m, Run Update
nav() {   # root downs, file downs
  printf 'wait 20000\nbutton\nwait 2000\nkey return\nwait 2500\n'
  for ((i = 0; i < $1; i++)); do echo "key down"; done
  printf 'wait 500\nkey return\nwait 3000\nkey return\nwait 2500\n'
  for ((i = 0; i < $2; i++)); do echo "key down"; done
  printf 'wait 500\nkey return\nwait 3000\nkey return\n'
}

run() {   # name, flash, script(stdin), emulator args...
  local name="$1" flash="$2"; shift 2
  local d="$out/$name"; rm -rf "$d"; mkdir -p "$d"
  cat > "$W/$name.ctl"
  cp "$flash" "$d/flash_before.bin"; cp "$flash" "$d/flash.bin"
  sed -e "s|@OUT@|$d|g" "$W/$name.ctl" > "$d/run.ctl"
  ( cd "$U" && nice -n 15 "$BIN" run --board c64u --caps 24E40226 --headless --speed max --firmware "$vendor" --roms "$U/firmware/1541ultimate/roms" \
      --c64-roms --flash "$d/flash.bin" --script "$d/run.ctl" --max-seconds 280 "$@" ) > "$d/emu.log" 2>&1 || { echo "emu_t135: $name: the emulator exited non-zero, see $d/emu.log" >&2; fails=$((fails + 1)); }
  cp "$d/flash.bin" "$d/flash_after.bin"; rm -f "$d/flash.bin"
}
check() {   # name, emu_check.py args
  local name="$1"; shift
  echo "== $name"
  if python3 -I "$HERE/emu_check.py" "$out/$name" "$@" --cor-pack "$corpack" | tee "$out/$name/check.txt"; then :; else fails=$((fails + 1)); fi
}
stick() {   # dir name -> a fresh copy of $W/d_<name> (the emulator writes the guest's changes back to the directory it shares)
  rm -rf "$W/st_$2"; cp -R "$W/d_$1" "$W/st_$2"; printf '%s' "$W/st_$2"
}
usbimg() { ls "$out/$1"/usbwork/*/image-1.img | head -1; }
corelist() { printf '%s' "$cores"; }

# S10 + S11 + T132 + D8a: both media; the two warranty questions first, then the medium (the one with the update file first), then the backup and the cores
cp "$W/sd_empty.img" "$W/s1_sd.img"
run s1_install "$W/flash_vendor.bin" --usb-dir "$(stick all s1_install)" --usb-dir-work "$out/s1_install/usbwork" --sd "$W/s1_sd.img" < <(nav 3 2; cat <<'EOF'
expect-c64 "Installed:" 90000
wait 400
png @OUT@/01_title_header.png
expect-c64 "leaving the warranty" 90000
wait 400
c64screen
png @OUT@/02_warranty1.png
key y
expect-c64 "really want" 20000
wait 400
png @OUT@/03_warranty2.png
key y
expect-c64 "the cores go" 60000
wait 400
png @OUT@/04_sd_or_usb.png
key right
key return
expect-c64 "Writing commodore" 120000
wait 400
png @OUT@/05_cores_progress.png
expect-c64 "Writing bitstream" 120000
wait 400
png @OUT@/06_flash_progress.png
expect-c64 "Update OK" 120000
wait 400
png @OUT@/07_result.png
quit
EOF
)
check s1_install installed --bit "$bit" --app "$app" --medium "$W/s1_sd.img" --medium "$(usbimg s1_install)" --cores-src "$csrc" --cores "$(corelist)"
# T132 bug 1: the title and the header lines measured on the C64 text screen of s1 (the dump above), not only photographed
centre() {   # name, centred|off
  echo "== $1 title centring ($2)"
  if python3 -I "$HERE/screen_centre.py" "$out/$1/emu.log" "$2" | tee "$out/$1/centre.txt"; then :; else fails=$((fails + 1)); fi
}
centre s1_install centred

# S11: No at the first question; nothing is written, the updater asks the ESP32 for a reboot (Commodore's own start-up is the menu)
cp "$W/sd_empty.img" "$W/s2_sd.img"
run s2_no_first "$W/flash_vendor.bin" --usb-dir "$(stick all s2_no_first)" --usb-dir-work "$out/s2_no_first/usbwork" --sd "$W/s2_sd.img" < <(nav 3 2; cat <<'EOF'
expect-c64 "leaving the warranty" 120000
wait 400
png @OUT@/01_warranty1.png
key n
expect-c64 "Back to the menu" 20000
expect-c64 "CMD_MACHINE_REBOOT" 20000
wait 400
png @OUT@/02_cancel_menu.png
quit
EOF
)
check s2_no_first unchanged
# what the ESP32's reboot brings: the flash is the old firmware, so its menu (a second emulator boot on the flash the run left)
run s2_after_reboot "$out/s2_no_first/flash_after.bin" < <(printf 'wait 20000\nbutton\nwait 3000\npng @OUT@/01_menu_after_cancel.png\nquit\n')

# S11: Yes, then No at the second question
cp "$W/sd_empty.img" "$W/s3_sd.img"
run s3_no_second "$W/flash_vendor.bin" --usb-dir "$(stick all s3_no_second)" --usb-dir-work "$out/s3_no_second/usbwork" --sd "$W/s3_sd.img" < <(nav 3 2; cat <<'EOF'
expect-c64 "leaving the warranty" 120000
key y
expect-c64 "really want" 20000
wait 400
png @OUT@/01_warranty2_no.png
key n
expect-c64 "CMD_MACHINE_REBOOT" 20000
wait 400
png @OUT@/02_cancel_menu.png
quit
EOF
)
check s3_no_second unchanged

# a stick and no SD card: no question where the cores go
run s5_usb_only "$W/flash_vendor.bin" --usb-dir "$(stick all s5_usb_only)" --usb-dir-work "$out/s5_usb_only/usbwork" < <(nav 3 2; cat <<'EOF'
expect-c64 "leaving the warranty" 150000
wait 400
png @OUT@/01_warranty1_usb_only.png
key y
expect-c64 "really want" 20000
key y
expect-c64 "Update OK" 150000
wait 400
png @OUT@/02_result.png
quit
EOF
)
check s5_usb_only installed --bit "$bit" --app "$app" --medium "$(usbimg s5_usb_only)" --cores-src "$csrc" --cores "$(corelist)"

# T132 bug 2: the SD card and no stick - the update file, the cores and the backup all on the SD card
cp "$W/sd_all.img" "$W/s6_sd.img"
run s6_sd_only "$W/flash_vendor.bin" --sd "$W/s6_sd.img" < <(nav 0 2; cat <<'EOF'
expect-c64 "leaving the warranty" 150000
wait 400
png @OUT@/01_warranty1_sd_only.png
key y
expect-c64 "really want" 20000
key y
expect-c64 "Backup in /SD" 150000
wait 400
png @OUT@/02_backup_on_sd.png
expect-c64 "Update OK" 150000
wait 400
png @OUT@/03_result.png
quit
EOF
)
check s6_sd_only installed --bit "$bit" --app "$app" --medium "$W/s6_sd.img" --cores-src "$csrc" --cores "$(corelist)"

# control for s6: the updater before the fix (no SD driver linked) on the same SD card
if [[ -n "$old" ]]; then
  dir_with old_sd "$old"; mkimg sd_old "$W/d_old_sd"
  cp "$W/sd_old.img" "$W/s7_sd.img"
  run s7_old_updater_sd "$W/flash_vendor.bin" --sd "$W/s7_sd.img" < <(nav 0 0; cat <<'EOF'
expect-c64 "About to update" 120000
key y
expect-c64 "No USB/SD with" 150000
wait 400
png @OUT@/01_old_updater_sd_refused.png
quit
EOF
)
  check s7_old_updater_sd unchanged
fi

# the update file alone on the stick: no firmware files beside it, the backup is the only entry
run s8_alone "$W/flash_vendor.bin" --usb-dir "$(stick alone s8_alone)" --usb-dir-work "$out/s8_alone/usbwork" < <(nav 3 0; cat <<'EOF'
expect-c64 "leaving the warranty" 150000
wait 400
png @OUT@/01_alone.png
key y
expect-c64 "really want" 20000
key y
expect-c64 "Update OK" 120000
quit
EOF
)
check s8_alone installed --bit "$bit" --app "$app" --medium "$(usbimg s8_alone)" --cores-src "$csrc" --cores ""

# our own firmware installed (the bitstream carries our stamp), T135 D9a: both warranty questions as well, but no cores folder, no boot.cfg
run s9_ours_installed "$W/flash_ours.bin" --usb-dir "$(stick all s9_ours_installed)" --usb-dir-work "$out/s9_ours_installed/usbwork" < <(nav 3 2; cat <<'EOF'
expect-c64 "leaving the warranty" 120000
wait 400
png @OUT@/01_warranty1_over_ours.png
key y
expect-c64 "really want" 20000
wait 400
png @OUT@/02_warranty2_over_ours.png
key y
expect-c64 "Update OK" 150000
wait 400
png @OUT@/02_result.png
quit
EOF
)
check s9_ours_installed written --bit "$bit" --app "$app"
run s10_ours_cancel "$W/flash_ours.bin" --usb-dir "$(stick all s10_ours_cancel)" --usb-dir-work "$out/s10_ours_cancel/usbwork" < <(nav 3 2; cat <<'EOF'
expect-c64 "leaving the warranty" 120000
key y
expect-c64 "really want" 20000
key n
expect-c64 "CMD_MACHINE_REBOOT" 20000
wait 400
png @OUT@/01_cancel_menu.png
quit
EOF
)
check s10_ours_cancel unchanged

# a package whose application is not the host (the tester package): over the vendor firmware it keeps its one question as well
if [[ -n "$tester" ]]; then
  dir_with tester "$tester"
  run s11_tester_package "$W/flash_vendor.bin" --usb-dir "$(stick tester s11_tester_package)" --usb-dir-work "$out/s11_tester_package/usbwork" < <(nav 3 0; cat <<'EOF'
expect-c64 "About to update" 120000
wait 400
png @OUT@/01_one_question.png
key n
expect-c64 "CMD_MACHINE_REBOOT" 20000
quit
EOF
)
  check s11_tester_package unchanged
fi

# control for the centring: the updater with the title as it was before the fix, same path to the same screen; the measurement must call it off centre
if [[ -n "$tctl" ]]; then
  rm -rf "$W/d_tctl"; cp -R "$W/d_all" "$W/d_tctl"; cp "$tctl" "$W/d_tctl/$UPD_NAME"
  run s12_title_control "$W/flash_vendor.bin" --usb-dir "$(stick tctl s12_title_control)" --usb-dir-work "$out/s12_title_control/usbwork" --sd "$W/sd_empty.img" < <(nav 3 2; cat <<'EOF'
expect-c64 "leaving the warranty" 150000
wait 400
c64screen
png @OUT@/01_title_unfixed.png
quit
EOF
)
  centre s12_title_control off
fi


# ---- T240: the boot menu takes every core found (up to 32), the bundle scan keeps every file, every .cor gets its folder -------------------
# Dummy files (a .cor here is any file the updater copies; its menu name is the file name). The names sort before the update file's, so the
# update file is the last in the file browser: its file number is the number of files beside it.
GEN="$W/t240gen"; mkdir -p "$GEN"
python3 -I - "$GEN" <<'PY'
import os, sys
d = sys.argv[1]
def mk(name):
    open(os.path.join(d, name), "wb").write(("T240 %s " % name).encode().ljust(2048, b"."))
for i in range(1, 34): mk("dummy%02d.cor" % i)
for i in range(1, 5): mk("app%d.u2a" % i)
for i in range(1, 4): mk("commodore_x%d.cfw" % i)
PY
t240_dir() {   # name, cor count, cfw count -> $W/d_<name> with the update file, the .cor files, 4 .u2a and the .cfw files
  local n="$1" nc="$2" nf="$3"; mkdir -p "$W/d_$n"; cp "$upd" "$W/d_$n/"
  for ((i = 1; i <= nc; i++)); do cp "$GEN/$(printf 'dummy%02d.cor' $i)" "$W/d_$n/"; done
  for i in 1 2 3 4; do cp "$GEN/app$i.u2a" "$W/d_$n/"; done
  for ((i = 1; i <= nf; i++)); do cp "$GEN/commodore_x$i.cfw" "$W/d_$n/"; done
}
t240_run() {   # name, cor count, cfw count: the stick alone, no SD card; the install runs to the end
  local n="$1" nc="$2" nf="$3"
  t240_dir "$n" "$nc" "$nf"
  # The counts are read from the final screen (Boot menu / not added / Core folders): the copy phase's lines last only milliseconds
  # before the updater clears the screen for the flash phase, so an expect on them misses now and then.
  run "$n" "$W/flash_vendor.bin" --usb-dir "$(stick "$n" "$n")" --usb-dir-work "$out/$n/usbwork" < <(nav 3 $((nc + 4 + nf)); cat <<'EOF'
expect-c64 "leaving the warranty" 150000
key y
expect-c64 "really want" 20000
key y
expect-c64 "Update OK" 350000
wait 400
c64screen
png @OUT@/02_result.png
quit
EOF
)
}

# S1 + S3: 20 .cor and 4 .u2a. The SD card already holds cores/dummy01/roms/keep.bin: the install goes onto it, the file must stay.
mkdir -p "$W/d_sd_keep/cores/dummy01/roms"; printf 'T240-KEEP' > "$W/d_sd_keep/cores/dummy01/roms/keep.bin"
mkimg sd_keep "$W/d_sd_keep"
cp "$W/sd_keep.img" "$W/s13_sd.img"
t240_dir s13_t240_20 20 0
run s13_t240_20 "$W/flash_vendor.bin" --usb-dir "$(stick s13_t240_20 s13_t240_20)" --usb-dir-work "$out/s13_t240_20/usbwork" --sd "$W/s13_sd.img" < <(nav 3 24; cat <<'EOF'
expect-c64 "leaving the warranty" 150000
key y
expect-c64 "really want" 20000
key y
expect-c64 "the cores go" 60000
key right
key return
expect-c64 "Update OK" 350000
wait 400
c64screen
png @OUT@/02_result.png
quit
EOF
)
check s13_t240_20 t240 --medium "$W/s13_sd.img" --medium "$(usbimg s13_t240_20)" --src "$GEN" --n-cor 20 --n-app 4 --expect-lines 21 --folders yes \
  --keep "dummy01/roms/keep.bin=T240-KEEP" --log-has "Core folders: 21"

# S2: the same, and three firmware files of another vendor in the folder: 20 + 4 + 3 = 27 files, every core still found (the .cfw files are menu entries too)
t240_run s14_t240_cfw 20 3
check s14_t240_cfw t240 --medium "$(usbimg s14_t240_cfw)" --src "$GEN" --n-cor 20 --n-app 4 --n-cfw 3 --expect-lines 24 --folders yes --log-has "Core folders: 21"

# S1: 33 .cor: the menu holds 32 (the backup and 31 of them), the screen says that 2 were not added; the folders are made for all 33
t240_run s15_t240_33 33 0
check s15_t240_33 t240 --medium "$(usbimg s15_t240_33)" --src "$GEN" --n-cor 33 --n-app 4 --expect-lines 32 --folders yes --log-has "2 core(s) not added" --log-has "Core folders: 34"

# control for s13-s15: the updater of main before T240 (16 entries, 24 files, no folders) on the 33-core stick; the T240 checks must call it wrong
if [[ -n "$t240ctl" ]]; then
  t240_dir s16_t240_ctl 33 0; cp "$t240ctl" "$W/d_s16_t240_ctl/$UPD_NAME"
  run s16_t240_ctl "$W/flash_vendor.bin" --usb-dir "$(stick s16_t240_ctl s16_t240_ctl)" --usb-dir-work "$out/s16_t240_ctl/usbwork" < <(nav 3 37; cat <<'EOF'
expect-c64 "leaving the warranty" 150000
key y
expect-c64 "really want" 20000
key y
expect-c64 "Update OK" 200000
wait 400
c64screen
png @OUT@/01_result.png
quit
EOF
)
  echo "== s16_t240_ctl (the old updater: 16 entries and no folders is what is expected here)"
  if python3 -I "$HERE/emu_check.py" "$out/s16_t240_ctl" t240 --medium "$(usbimg s16_t240_ctl)" --src "$GEN" --n-cor 33 --expect-lines 16 --folders no --menu-only | tee "$out/s16_t240_ctl/check.txt"; then :; else fails=$((fails + 1)); fi
  echo "== s16_t240_ctl against the T240 expectation (must FAIL)"
  if python3 -I "$HERE/emu_check.py" "$out/s16_t240_ctl" t240 --medium "$(usbimg s16_t240_ctl)" --src "$GEN" --n-cor 33 --expect-lines 32 --folders yes --menu-only > "$out/s16_t240_ctl/check_t240.txt" 2>&1; then
    echo "emu_t135: the T240 checks passed on the old updater: they cannot fail" >&2; fails=$((fails + 1))
  else echo "  ok   the T240 checks fail on the old updater, as they must"; fi
fi

echo "emu_t135: $fails scenario(s) failed"
exit $((fails ? 1 : 0))
