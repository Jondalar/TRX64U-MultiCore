#!/bin/sh
# T164 WP7 (S6): the host's mode request path (host/mode_req.v, host/cdce_mode.v, host/hdmi_timing.v), iverilog:
#   1. path, frame-buffer core (+c64=0): every request value 0..15 after every other and 0x10 / 0xFF / 0x12 / 0x8C: accepted ones reach vm_mode, mode_pix and
#      hdmi_timing (clocks per frame), refused ones (above 11, bits 7:4, the 50 Hz PC rows) change nothing and do not restart the CDCE; the status
#      register 0x4F04..0x4F09 (magic, version, flags, mode, requests written / processed) after every request;
#   2. path, C64-type core (+c64=1): only the 1080p rows, the family from fam_v60, and a family change restarts the CDCE on the other row;
#   3. CDCE I2C (+i2c): every effective row sends two transactions C8 14 08 57 00 01 01 + the row's 4 bytes (written out in the bench);
set -e
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../.." && pwd)
# T164 WP18: HOSTDIR (relative to the repository root) points the bench at a large core's copy of the host files (cores/<id>/mono/host); default host/
hd=$root/${HOSTDIR:-host}
out=$here/build$(echo "${HOSTDIR:-}" | tr / _)
mkdir -p "$out"
# T188: the Amiga copies carry the Amiga edition of hdmi_modes.vh (rows 0..2 bent to the Amiga frame, no lock bytes for row 2); they are never C64-type cores
def=""; amiga=0; case "${HOSTDIR:-}" in cores/amiga/*|cores/amiga1200/*) def="-DT188"; amiga=1;; esac
run() {   # <name> <mode_req.v> <cdce_mode.v> [vvp args]
  n=$1; req=$2; cdce=$3; shift 3
  nice -n 15 iverilog -g2012 $def -I"$hd" -o "$out/$n.vvp" -s tb "$here/tb_modepath.v" "$req" "$cdce" "$hd/hdmi_timing.v"
  timeout 3000 nice -n 15 vvp -n "$out/$n.vvp" "$@" > "$out/$n.log" 2>&1 || true
}
good() {   # <name> <args> <PASS line start> <label>
  run "$1" "$hd/mode_req.v" "$hd/cdce_mode.v" $2
  grep -q "^PASS modepath: $3" "$out/$1.log" || { grep '^FAIL' "$out/$1.log" | head; echo "FAIL modepath ($4)"; exit 1; }
}
good all "+c64=0" "c64=0 path" "frame-buffer path"
grep -m3 '^request' "$out/all.log"
[ $amiga = 1 ] || good all_c64 "+c64=1" "c64=1 path" "C64-type path"
[ $amiga = 1 ] || grep '^family bit' "$out/all_c64.log"
# T234: the Amiga copies' mode_req has the Frame follow register (0x4F02): the run above is the default Off (rows 0..2 = table rows 12..14), this one has it On first
[ $amiga = 0 ] || good all_f1 "+c64=0 +fol=1" "c64=0 path" "frame-buffer path, Frame follow On"
[ $amiga = 0 ] || grep -h '^Frame follow' "$out/all.log" "$out/all_f1.log"
good i2c "+c64=0 +i2c" "c64=0 i2c" "frame-buffer i2c"
[ $amiga = 0 ] || good i2c_f1 "+c64=0 +i2c +fol=1" "c64=0 i2c" "frame-buffer i2c, Frame follow On"
[ $amiga = 1 ] || good i2c_c64 "+c64=1 +i2c" "c64=1 i2c" "C64-type i2c"
if [ $amiga = 1 ]; then grep -h '^i2c row' "$out/i2c.log" "$out/i2c_f1.log"; else grep -h '^i2c row' "$out/i2c.log" "$out/i2c_c64.log"; fi
echo "PASS modepath (fb and C64-type paths, i2c bytes of 9 + 2 rows, status register; Amiga copies: Frame follow Off and On)"
