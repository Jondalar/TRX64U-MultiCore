#!/usr/bin/env bash
# T265 S3-S6: the guard's application side (guard.cc, the record and the recovery slot in flashfw.cc). A flash with erase semantics and a RAM disk lose their power at every
# operation of the provisioning, of the update writer and of the repair; the boot ROM's rule is modelled in the test. Each decision has a control: a mutant of the source under test
# (or of the slot constants) has to go red.
#   tests/hostapp/run_guard.sh [scratch dir]          (run.sh calls it with its own)
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
source "$ROOT/tests/hostgen.sh"
H="$ROOT/host-app/software/hostapp"
HS="$ROOT/host-app/software"
T="${1:-}"
if [ -z "$T" ]; then T="$(mktemp -d)"; trap 'rm -rf "$T"' EXIT; fi
[ -f "$T/sha256.o" ] || cc -c -w -I"$H" -o "$T/sha256.o" "$H/sha256.c"
if [ ! -f "$HS/io/flash/flash.h" ]; then
  echo "SKIPPED test_guard: vendor/gideon/software is missing (tools/gideon_sources.sh)"
  exit 0
fi
mkdir -p "$T/guard"
INC=(-I"$H" -I"$HS/io/flash" -I"$HS/components" -I"$HS/infra" -I"$HS/system" -I"$HS/api" -I"$HS/filesystem")
TST="$ROOT/tests/hostapp/test_guard.cc"
srcs=("$H/guard.cc" "$H/flashfw.cc")
c++ -std=gnu++14 -Wall -Wno-unused-function "${INC[@]}" -o "$T/test_guard" "$TST" "${srcs[@]}" "$T/sha256.o" 2>&1
"$T/test_guard"
# the slot image of the recovery program (tools/rec_trailer.py): record + data + CRC-32 trailer; a program that does not fit the slot is refused and leaves no file
python3 -I - "$ROOT/tools" "$T/guard" <<'PY'
import struct, subprocess, sys, zlib, os
tools, d = sys.argv[1:]
app = struct.pack("<III", 0x01800000, 1001, 0x01800000) + bytes(range(256)) * 3 + b"xyz" + bytes(234)
app = app[:12 + 1001]
open(os.path.join(d, "r.app"), "wb").write(app)
r = subprocess.run([sys.executable, "-I", os.path.join(tools, "rec_trailer.py"), os.path.join(d, "r.app"), os.path.join(d, "r.slot")], capture_output=True, text=True)
img = open(os.path.join(d, "r.slot"), "rb").read()
assert r.returncode == 0 and len(img) == 12 + 1004 + 4, (r.returncode, r.stderr, len(img))
assert struct.unpack("<I", img[-4:])[0] == zlib.crc32(img[:-4]) & 0xFFFFFFFF
big = struct.pack("<III", 0x01800000, 0x3EFF1, 0x01800000) + bytes(0x3EFF1)
open(os.path.join(d, "big.app"), "wb").write(big)
if os.path.exists(os.path.join(d, "big.slot")): os.remove(os.path.join(d, "big.slot"))
r = subprocess.run([sys.executable, "-I", os.path.join(tools, "rec_trailer.py"), os.path.join(d, "big.app"), os.path.join(d, "big.slot")], capture_output=True, text=True)
assert r.returncode == 1 and not os.path.exists(os.path.join(d, "big.slot")), r
ok = struct.pack("<III", 0x01800000, 0x3EFF0, 0x01800000) + bytes(0x3EFF0)
open(os.path.join(d, "ok.app"), "wb").write(ok)
r = subprocess.run([sys.executable, "-I", os.path.join(tools, "rec_trailer.py"), os.path.join(d, "ok.app"), os.path.join(d, "ok.slot")], capture_output=True, text=True)
assert r.returncode == 0 and os.path.getsize(os.path.join(d, "ok.slot")) == 0x3F000, r
print("PASS rec_trailer: record + data + CRC-32 trailer; 0x3EFF0 data bytes fill the 0x3F000 slot exactly, one byte more is refused and leaves no file")
PY
gd_mutant() {   # name, source file name (in $H), sed expression
  local name="$1" src="$2" expr="$3" bad="$T/guard/mut_$1.${2##*.}" list=()
  sed -e "$expr" "$H/$src" > "$bad"
  cmp -s "$bad" "$H/$src" && { echo "CONTROL $name: the sed changed nothing" >&2; exit 1; }
  if [ "${src##*.}" = h ]; then
    # a quoted include looks in the includer's own folder first: the mutated header goes with copies of everything that includes it
    local d="$T/guard/inc_$name"; mkdir -p "$d"
    for f in guard.h guardimg.h txu.h cor.h fileops.h guard.cc flashfw.cc; do cp "$H/$f" "$d/$f"; done
    cp "$bad" "$d/$src"
    c++ -std=gnu++14 -w -I"$d" "${INC[@]}" -o "$T/guard/mut_$name" "$TST" "$d/guard.cc" "$d/flashfw.cc" "$T/sha256.o" 2>&1
  else
    for f in "${srcs[@]}"; do if [ "$(basename "$f")" = "$src" ]; then list+=("$bad"); else list+=("$f"); fi; done
    c++ -std=gnu++14 -w "${INC[@]}" -o "$T/guard/mut_$name" "$TST" "${list[@]}" "$T/sha256.o" 2>&1
  fi
  if "$T/guard/mut_$name" > "$T/guard/mut_$name.log" 2>&1; then echo "CONTROL $name: the bench PASSED against the mutant" >&2; exit 1; fi
  grep -q '^FAIL' "$T/guard/mut_$name.log" || { echo "CONTROL $name: it failed without a red check" >&2; exit 1; }
  echo "control $name: fails as it must ($(grep -c '^FAIL' "$T/guard/mut_$name.log") check(s) red)"
}
# S5: the order is application, copy, record - a record that is not written leaves a mismatch after a completed update
gd_mutant gd_record_skipped guard.cc 's/    o->record_ok = fw_write_record(fl, len, crc, \&r);/    o->record_ok = true;/'
# S5: a copy that cannot be written must not survive the new record (silent downgrade)
gd_mutant gd_stale_copy_kept guard.cc 's/            guard_copy_drop();                            \/\/ an older copy must not outlive the new record/            ;/'
# S5: the old copy is replaced only after the new data is complete (a cut in between must leave a copy)
gd_mutant gd_old_copy_first guard.cc 's/    void \*h = g_fs->open_write(GUARD_NEW_PATH);/    g_fs->remove(GUARD_COPY_PATH);\n    void *h = g_fs->open_write(GUARD_NEW_PATH);/'
# the copy is read back before it gets its name
gd_mutant gd_copy_unverified guard.cc 's/    if (!h || got < 0 || got_total != len || c != crc) {/    if (false) {/'
# the provisioning refuses a flash application that is not the one the boot ROM measured
gd_mutant gd_foreign_app_provisioned guard.cc 's/    if (gi.flash_crc != 0 \&\& (gi.flash_len != len || gi.flash_crc != crc)) {/    if (false) {/'
# the provisioning writes only what differs
gd_mutant gd_always_rewrite guard.cc 's/    if (!guard_copy_current(len, crc)) {/    if (true) {/'
# a repair refuses an application for another host interface and a damaged stage
gd_mutant gd_iface_unchecked guard.cc 's/    if (gi.iface != iface_needed) {/    if (false) {/'
gd_mutant gd_stage_unchecked guard.cc 's/ || fw_crc32(stage, gi.stage_len) != gi.stage_crc) {/) {/'
# a started recovery block is checked
gd_mutant gd_blockchk_unchecked guard.cc 's/ \&\& rb\[7\] == guard_recovery_chk(rb)) {/) {/'
# the record: erased before it is programmed, chk checked when it is read
gd_mutant gd_record_no_erase flashfw.cc 's/    if (!erase_sector_at(w, FW_RECORD_ADDR, FWP_APP)) {/    if (false) {/'
gd_mutant gd_record_chk_unchecked flashfw.cc 's/ || le32(rd_buf + 12) != fw_guard_chk(le32(rd_buf + 4), le32(rd_buf + 8))) {/) {/'
# the bitstream slot ends where the recovery slot begins: a host write must not erase the recovery program and the record
gd_mutant gd_slot_0x220000 flashfw.h 's/#define FW_BIT_SLOT        0x1E0000u/#define FW_BIT_SLOT        0x220000u/'
echo "run_guard: ok"
