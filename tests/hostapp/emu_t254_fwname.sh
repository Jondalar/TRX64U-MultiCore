#!/usr/bin/env bash
# T254 S1-S5, S7, S8: a foreign firmware shows the name and version it gives itself, through the real host application in ue2emu. The stick's cores/ holds
# synthetic firmware files (tests/hostapp/mkconvtest.py's vendor layout, no vendor bytes): a Gideon-like .cfw whose application says
# "Software version:   3.15", a TRX64-Ultimate-like .ue2 ("TRX64 Ultimate", "0.3.1"), a .cfw without the line, a .cor packaged before T254
# (manifest version "bit 2026/10/25") whose application says "1.2.1", and a Commodore-like .cfw ("1.2.1"). The walk: F3 > USB0 > cores, each
# row's panel (PNG), S on the .cfw (the question, PNG, cancelled), A (boot.cfg), P (the .cor's manifest), the packaged row's panel (PNG), A on the
# .ue2 and the Commodore-like .cfw, then on f_swap.cfw and g_swap.cor (a .cfw and a packaged .cor whose application has no version line in
# this run: the host names them by the .bit header, as every host did before T254), F1 (PNG).
# Run 2 (T254 D7, D8): the same flash; f_swap.cfw and g_swap.cor now say "3.14" and "1.1.0". C= held at power-on (the menu, not the
# default: T135 S2), F3 > USB0 > cores, A on both: their entries take "Gideon 3.14" and "Commodore 1.1.0"; F1 (PNG: "packaged firmware").
#
#   tests/hostapp/emu_t254_fwname.sh <images dir with hostapp_emu.elf> <out dir>
set -uo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
source "$ROOT/ci/lib.sh"
img="${1:?usage: emu_t254_fwname.sh <images dir> <out dir>}"; out="${2:?usage: emu_t254_fwname.sh <images dir> <out dir>}"
UE2EMU="${UE2EMU:-$ROOT/ci/ue2emu.sh}"
kit="$(ue2emu_kit)" || { echo "no emulator kit" >&2; exit 77; }
mkdir -p "$out"; out="$(cd "$out" && pwd -P)"; img="$(cd "$img" && pwd -P)"
w="$out/work"; rm -rf "$w"; mkdir -p "$w/cors" "$w/profiles" "$w/stick/cores" "$w/run2"
python3 -I - "$ROOT" "$w/stick/cores" "$w/run2" <<'PY' || exit 1
import os, sys
root, d, d2 = sys.argv[1], sys.argv[2], sys.argv[3]
sys.path.insert(0, os.path.join(root, "tools"))
sys.path.insert(0, os.path.join(root, "tests", "hostapp"))
import cor_pack as C, test_cor_pack as TC, mkconvtest as M
GID = b"u64_mk2_artix;UserID=0XFFFFFFFF;Version=2024.1"

def app_with(n, *texts):
    """An application record of n bytes with each (offset from the end, text) written into its data."""
    rec = bytearray(M.app_record(n, 5))
    for back, t in texts:
        at = len(rec) - back
        rec[at:at + len(t)] = t
    return bytes(rec)

bit_g = TC.full_bit(frames=2500, axss=None, name=GID)
bit_t = TC.full_bit(frames=2500)
open(d + "/a_gideon.cfw", "wb").write(M.vendor_image(bit_g, app_with(200000, (90000, b"Software version:   3.15\n   Build date:")), True))
open(d + "/b_trx.ue2", "wb").write(M.vendor_image(bit_t, app_with(200000, (150000, b"on TRX64 Ultimate hardware"), (60000, b"Software version:   0.3.1\n")), False))
open(d + "/c_plain.cfw", "wb").write(M.vendor_image(bit_g, app_with(200000), True))
open(d + "/e_c121.cfw", "wb").write(M.vendor_image(bit_g, app_with(200000, (70000, b"Software version:   1.2.1\n")), True))
old_app = app_with(200000, (80000, b"Software version:   1.2.1\n"))[12:]
open(d + "/d_old.cor", "wb").write(C.pack_large("fw-0a1b2c3d", "u64_mk2_artix 2026/10/25 12:00:00", "bit 2026/10/25", bit_g, old_app, bytes(2048), origin="foreign"))
# T254 D7, D8: f_swap.cfw and g_swap.cor without the version line for run 1 (named by the .bit header, as before T254), with it in run2/
for dd, v_cfw, v_cor in ((d, None, None), (d2, b"Software version:   3.14\n", b"Software version:   1.1.0\n")):
    open(dd + "/f_swap.cfw", "wb").write(M.vendor_image(bit_g, app_with(200000, *([(90000, v_cfw)] if v_cfw else [])), True))
    swap_app = app_with(200000, *([(80000, v_cor)] if v_cor else []))[12:]
    open(dd + "/g_swap.cor", "wb").write(C.pack_large("fw-0f1e2d3c", "u64_mk2_artix 2026/10/25 12:00:00", "bit 2026/10/25", bit_g, swap_app,
                                                      bytes(2048), origin="foreign"))
PY
P="$out/t254"
{ printf 'wait 15000\nkey space\nwait 800\nkey f3\nwait 1500\nkey down\nwait 500\nkey down\nwait 500\nkey return\nwait 2500\n'
  printf 'key down\nwait 500\nkey return\nwait 3000\n'                                            # USB0 > cores: .., a_gideon.cfw, b_trx.ue2, c_plain.cfw, d_old.cor
  printf 'key down\nwait 3000\npng %s-1-gideon.png\nkey down\nwait 3000\npng %s-2-trx.png\n' "$P" "$P"
  printf 'key down\nwait 3000\npng %s-3-plain.png\nkey down\nwait 3000\npng %s-4-oldpkg.png\n' "$P" "$P"
  printf 'key up\nwait 400\nkey up\nwait 400\nkey up\nwait 3000\n'                                 # back on a_gideon.cfw
  printf 'key s\nwait 3000\npng %s-5-start-ask.png\nkey space\nwait 2000\n' "$P"                    # the question, then any other key cancels
  printf 'key a\nwait 4000\npng %s-6-added.png\n' "$P"
  printf 'key p\nwait 40000\npng %s-7-packaged.png\nkey down\nwait 500\nkey down\nwait 3000\npng %s-8-packaged-panel.png\n' "$P" "$P"   # the list starts at .. again: a_gideon.cor is row 2"
  printf 'key down\nwait 3000\nkey a\nwait 4000\nkey down\nwait 500\nkey down\nwait 500\nkey down\nwait 3000\nkey a\nwait 4000\n'   # A on b_trx.ue2 and on e_c121.cfw
  printf 'key down\nwait 3000\nkey a\nwait 4000\nkey down\nwait 3000\nkey a\nwait 8000\n'   # T254 D7: A on f_swap.cfw and g_swap.cor (no version line yet)
  printf 'key f1\nwait 2500\npng %s-9-menu.png\nquit\n' "$P"; } > "$w/flow.ctl"
cp "$kit/flash.bin" "$out/flash.bin"
( cd "$w" && nice -n 15 timeout 600 "$UE2EMU" run --board c64u --trx-io=on --icap --cor-dir "$w/cors" --core-dir "$w/profiles" --firmware "$img/hostapp_emu.elf" \
    --roms "$kit/roms" --flash "$out/flash.bin" --usb-dir "$w/stick" --usb-dir-work "$w/usbwork" --headless --speed max ${EMU_SCREEN:+--screen $EMU_SCREEN} --script "$w/flow.ctl" \
    --max-seconds 500 > "$out/t254.log" 2>&1 )
rc=$?
FAIL=0
[ $rc -eq 0 ] || { echo "FAIL T254: the emulator exited $rc (see $out/t254.log)"; FAIL=1; }
# S3: the entry A added, as boot.cfg on the emulated flash holds it
boot_cfg() {     # boot.cfg as the emulated flash holds it
python3 -I - "$out/flash.bin" "$ROOT/tests/updater" <<'PY'
import sys
sys.path.insert(0, sys.argv[2])
from fatread import Fat
f = Fat(open(sys.argv[1], "rb").read()[0x580000:])
e = f.find("boot.cfg")
sys.stdout.write(f.read(e).decode() if e else "NO BOOT.CFG\n")
PY
}
boot_cfg > "$out/t254-boot.cfg"
if grep -q '^core=/USB0/cores/a_gideon.cfw|Gideon 3.15|' "$out/t254-boot.cfg" && grep -q '^core=/USB0/cores/b_trx.ue2|TRX64 Ultimate 0.3.1|' "$out/t254-boot.cfg" \
    && grep -q '^core=/USB0/cores/e_c121.cfw|Commodore 1.2.1|' "$out/t254-boot.cfg"; then echo "PASS T254 S3: A added the entries as 'Gideon 3.15', 'TRX64 Ultimate 0.3.1', 'Commodore 1.2.1'"
else echo "FAIL T254 S3: boot.cfg lacks a line of the three entries (see $out/t254-boot.cfg)"; FAIL=1; fi
# S7, the state before run 2: run 1 added f_swap.cfw and g_swap.cor under the .bit header's name, as the host did before T254
OLD="u64_mk2_artix 2026/10/25 12:00:00"
if grep -qF "core=/USB0/cores/f_swap.cfw|$OLD|" "$out/t254-boot.cfg" && grep -qF "core=/USB0/cores/g_swap.cor|$OLD|" "$out/t254-boot.cfg"; then
    echo "PASS T254 S7 (given): run 1 added f_swap.cfw and g_swap.cor as '$OLD'"
else echo "FAIL T254 S7 (given): boot.cfg lacks f_swap.cfw or g_swap.cor under '$OLD' (see $out/t254-boot.cfg)"; FAIL=1; fi
# S5: the manifest of the .cor that P wrote
cor="$(find "$w" -name a_gideon.cor | head -1)"
if [ -n "$cor" ] && python3 "$ROOT/tools/cor_pack.py" check "$cor" > "$out/t254-cor.txt" 2>&1 && python3 -I - "$ROOT/tools" "$cor" >> "$out/t254-cor.txt" <<'PY'
import sys
sys.path.insert(0, sys.argv[1])
import cor_pack as C
m, _ = C.parse(open(sys.argv[2], "rb").read())
print("manifest name=%s version=%s origin=%s" % (m.get("name"), m.get("version"), m.get("origin")))
sys.exit(0 if (m.get("name"), m.get("version"), m.get("origin")) == ("Gideon 3.15", "3.15", "foreign") else 1)
PY
then echo "PASS T254 S5: P wrote a_gideon.cor with the name 'Gideon 3.15' and the version '3.15' ($out/t254-cor.txt)"
else echo "FAIL T254 S5: the .cor that P wrote is missing or names something else (see $out/t254-cor.txt)"; FAIL=1; fi
# S7, S8, run 2: the same flash; f_swap.cfw and g_swap.cor now carry the version line. C= held at power-on shows the menu instead of
# starting the default (T135 S2); F3 > USB0 > cores; A on f_swap.cfw and on g_swap.cor; F1. A work directory of its own: the stick's image
# is made again from the folder, which holds what run 1 wrote (a_gideon.cor) and the two new files.
cp "$w/run2/f_swap.cfw" "$w/run2/g_swap.cor" "$w/stick/cores/"
row="$(ls "$w/stick/cores" | LC_ALL=C sort | grep -n '^f_swap\.cfw$' | cut -d: -f1)"      # its row below ".."
{ printf 'hold cbm\nwait 3000\nrelease cbm\nwait 15000\nkey f3\nwait 1500\nkey down\nwait 500\nkey down\nwait 500\nkey return\nwait 2500\n'
  printf 'key down\nwait 500\nkey return\nwait 3000\n'
  for ((i = 1; i < row; i++)); do printf 'key down\nwait 500\n'; done
  printf 'key down\nwait 3000\nkey a\nwait 4000\nkey down\nwait 3000\nkey a\nwait 8000\n'   # A on f_swap.cfw, then on g_swap.cor
  printf 'key f1\nwait 2500\npng %s-10-renamed.png\nquit\n' "$P"; } > "$w/flow2.ctl"
( cd "$w" && nice -n 15 timeout 600 "$UE2EMU" run --board c64u --trx-io=on --icap --cor-dir "$w/cors" --core-dir "$w/profiles" --firmware "$img/hostapp_emu.elf" \
    --roms "$kit/roms" --flash "$out/flash.bin" --usb-dir "$w/stick" --usb-dir-work "$w/usbwork2" --headless --speed max ${EMU_SCREEN:+--screen $EMU_SCREEN} --script "$w/flow2.ctl" \
    --max-seconds 500 > "$out/t254-2.log" 2>&1 )
rc=$?
[ $rc -eq 0 ] || { echo "FAIL T254 run 2: the emulator exited $rc (see $out/t254-2.log)"; FAIL=1; }
grep -aq 'decision Menu: C= was held' "$out/t254-2.log" || { echo "FAIL T254 run 2: C= did not hold the menu (see $out/t254-2.log)"; FAIL=1; }
boot_cfg > "$out/t254-boot2.cfg"
B2="$out/t254-boot2.cfg"
if grep -qF 'core=/USB0/cores/f_swap.cfw|Gideon 3.14|' "$B2" && grep -qF 'core=/USB0/cores/g_swap.cor|Commodore 1.1.0|' "$B2" && ! grep -qF "|$OLD|" "$B2" \
    && grep -qF 'core=/USB0/cores/a_gideon.cfw|Gideon 3.15|' "$B2" && grep -qF 'core=/USB0/cores/b_trx.ue2|TRX64 Ultimate 0.3.1|' "$B2" \
    && grep -qF 'core=/USB0/cores/e_c121.cfw|Commodore 1.2.1|' "$B2" && [ "$(grep -c '^core=' "$B2")" = 5 ]; then
    echo "PASS T254 S7: A renamed run 1's two entries to 'Gideon 3.14' and 'Commodore 1.1.0'; the other three kept their names; five entries"
else echo "FAIL T254 S7: boot.cfg after run 2 (see $B2)"; FAIL=1; fi
# the pictures: every step is a picture of its own (S1, S2, S4 are read off them)
bad=""; prev=""
for n in 1-gideon 2-trx 3-plain 4-oldpkg 5-start-ask 6-added 7-packaged 8-packaged-panel 9-menu 10-renamed; do
    [ -s "$P-$n.png" ] || bad="$bad $n(missing)"
    [ -z "$prev" ] || [ ! -s "$P-$n.png" ] || ! cmp -s "$P-$prev.png" "$P-$n.png" || bad="$bad $n(same as $prev)"; prev=$n
done
[ -z "$bad" ] && echo "PASS T254 pictures: ten different screens ($P-*.png; S8 is read off 9-menu and 10-renamed)" || { echo "FAIL T254 pictures:$bad"; FAIL=1; }
rm -rf "$w" "$out/flash.bin"
exit $FAIL
