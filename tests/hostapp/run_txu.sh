#!/usr/bin/env bash
# T152 S1-S4: the MultiCore update package (txu.cc), the single-part writes of the flash layer (flashfw.cc fw_write_parts) and the survey of the cores.
# Packages from mktxutest.py (the real writer, tools/txu_pack.py), a fake flash that counts erases and programs per region. Each decision has a control: a
# mutant of the source under test has to go red.
#   tests/hostapp/run_txu.sh [scratch dir]          (run.sh calls it with its own)
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
source "$ROOT/tests/hostgen.sh"   # T239.3 V-3b: CPATH holds hostio.h (IOBASE, U2P_IO_BASE) and basetab.h, generated from the TOML A of the host
H="$ROOT/host-app/software/hostapp"
HS="$ROOT/host-app/software"
T="${1:-}"
if [ -z "$T" ]; then T="$(mktemp -d)"; trap 'rm -rf "$T"' EXIT; fi
[ -f "$T/sha256.o" ] || cc -c -w -I"$H" -o "$T/sha256.o" "$H/sha256.c"
if [ ! -f "$HS/io/flash/flash.h" ]; then
  echo "SKIPPED test_txu: vendor/gideon/software is missing (tools/gideon_sources.sh)"
  exit 0
fi
python3 -I "$ROOT/tools/txu_pack.py" selftest
mkdir -p "$T/txu"
python3 "$ROOT/tests/hostapp/mktxutest.py" "$T/txu"
# the CI package step (ci/package.sh -> tools/pack_txu_mc.sh): the host bitstream and the app of one run make one .txu that reads back; an input that is no bitstream
# leaves no file and fails
mkdir -p "$T/pk"
"$ROOT/tools/pack_txu_mc.sh" --host "$T/txu/bit.bin" --app "$T/txu/app.bin" --branch t152-update-and-flash --sha 1a2b3c4d --commit 1a2b3c4d5e6f708 --out "$T/pk" > "$T/pk/ok.log"
if [ ! -s "$T/pk/trx64host_update_1a2b3c4d.txu" ] || ! grep -q "version t152-update-an-1a2b3c4d" "$T/pk/ok.log"; then
  echo "run_txu: pack_txu_mc.sh did not make the .txu" >&2; exit 1
fi
if "$ROOT/tools/pack_txu_mc.sh" --host "$T/txu/app.bin" --app "$T/txu/app.bin" --branch x --sha deadbeef --commit abc --out "$T/pk" > /dev/null 2>&1; then
  echo "CONTROL pack_txu_mc: a host part that is no bitstream was packed" >&2; exit 1
fi
[ ! -e "$T/pk/trx64host_update_deadbeef.txu" ] || { echo "CONTROL pack_txu_mc: a failed pack left its file" >&2; exit 1; }
echo "control pack_txu_mc: a host part that is no bitstream is refused and leaves no file"
INC=(-I"$H" -I"$HS/io/flash" -I"$HS/components" -I"$HS/infra" -I"$HS/system" -I"$HS/api" -I"$HS/filesystem")
TST="$ROOT/tests/hostapp/test_txu.cc"
srcs=("$H/txu.cc" "$H/flashfw.cc" "$H/cfw.cc" "$H/cor.cc" "$H/handover.cc")
c++ -std=gnu++14 -Wall -Wno-unused-function "${INC[@]}" -o "$T/test_txu" "$TST" "${srcs[@]}" "$T/sha256.o" 2>&1
"$T/test_txu" "$T/txu"
tx_mutant() {   # name, source file name (in $H), sed expression
  local name="$1" src="$2" expr="$3" bad="$T/txu/mut_$1.cc" list=()
  sed -e "$expr" "$H/$src" > "$bad"
  cmp -s "$bad" "$H/$src" && { echo "CONTROL $name: the sed changed nothing" >&2; exit 1; }
  for f in "${srcs[@]}"; do if [ "$(basename "$f")" = "$src" ]; then list+=("$bad"); else list+=("$f"); fi; done
  c++ -std=gnu++14 -w "${INC[@]}" -o "$T/txu/mut_$name" "$TST" "${list[@]}" "$T/sha256.o" 2>&1
  if "$T/txu/mut_$name" "$T/txu" > "$T/txu/mut_$name.log" 2>&1; then echo "CONTROL $name: the bench PASSED against the mutant" >&2; exit 1; fi
  grep -q '^FAIL' "$T/txu/mut_$name.log" || { echo "CONTROL $name: it failed without a red check" >&2; exit 1; }
  echo "control $name: fails as it must ($(grep -c '^FAIL' "$T/txu/mut_$name.log") check(s) red)"
}
# the package reader (S4, S3)
tx_mutant tx_no_magic txu.cc 's/    if (memcmp(h, "TXU1", 4)) {/    if (false) {/'
tx_mutant tx_no_crc txu.cc 's/    if (fw_crc32(h, hdr_len - 4) != le32(h + hdr_len - 4)) {/    if (false) {/'
tx_mutant tx_any_origin txu.cc 's/    if (strcmp(ci->origin, "trx")) {/    if (false) {/'
tx_mutant tx_trailing_bytes txu.cc 's/    if (end != ci->file_size) {/    if (false) {/'
tx_mutant tx_truncated_ok txu.cc 's/t.off > ci->file_size || ci->file_size - t.off < t.size) {/false) {/;s/    if (end != ci->file_size) {/    if (false) {/'
tx_mutant tx_addr_any txu.cc 's/(t.kind != TXU_KIND_HOST \&\& t.kind != TXU_KIND_APP) || t.addr != addr || /(t.kind != TXU_KIND_HOST \&\& t.kind != TXU_KIND_APP) || /'
# S3: the interface check
tx_mutant tx_iface_unchecked txu.cc 's/    if (provided != needed) {/    if (false) {/'
tx_mutant tx_iface_running_host_only txu.cc 's/    uint32_t provided = o->has_host ? ci.iface_provided : run.iface_provided;/    uint32_t provided = run.iface_provided;/'
# S2: the host key and the survey
tx_mutant tx_key_change_never txu.cc 's/    o->host_key_changes = o->has_host \&\& ci.host_key != run.host_key;/    o->host_key_changes = false;/'
tx_mutant tx_key_change_always txu.cc 's/    o->host_key_changes = o->has_host \&\& ci.host_key != run.host_key;/    o->host_key_changes = o->has_host;/'
tx_mutant tx_large_breaks txu.cc 's/        s->large++;/        s->large++; s->broken++;/'
tx_mutant tx_survey_ignores_key txu.cc 's/    if (works_now \&\& !works_after) {/    if (works_now) {/'
# the check of every byte before the first erase
tx_mutant tx_host_sha_unchecked txu.cc 's/        if (!sha_equal(o->host_sha, hp->sha)) {/        if (false) {/'
tx_mutant tx_app_sha_unchecked txu.cc 's/        if (!sha_equal(o->app_sha, ap->sha)) {/        if (false) {/'
tx_mutant tx_bit_header_unchecked txu.cc 's/        if (bit_length_from_header(bit_dst, hp->size, \&data_off) != hp->size) {/        if (false) {/'
tx_mutant tx_idcode_unchecked txu.cc 's/        if (!cfw_bit_idcode(ms, data_off, hp->size - data_off, \&id) || ((id ^ CFW_IDCODE_XC7A50T) \& CFW_IDCODE_MASK)) {/        if (false) {/'
tx_mutant tx_key_unchecked txu.cc 's/        if (!fw_axss(bit_dst, hp->size, \&ax) || ax != ci.host_key) {/        if (false) {/'
tx_mutant tx_abort_ignored txu.cc 's/return fail(o, abort \&\& abort(ctx) ? "cancelled" : "the file cannot be read");/return fail(o, "the file cannot be read");/'
# the flash layer: a part that is not named is not touched, and an unsealed image is not written
tx_mutant tx_bit_always flashfw.cc 's/const bool do_bit = (parts \& FW_PART_BIT) != 0, do_app = (parts \& FW_PART_APP) != 0;/const bool do_bit = img.bit != 0, do_app = (parts \& FW_PART_APP) != 0;/'
tx_mutant tx_app_always flashfw.cc 's/const bool do_bit = (parts \& FW_PART_BIT) != 0, do_app = (parts \& FW_PART_APP) != 0;/const bool do_bit = (parts \& FW_PART_BIT) != 0, do_app = img.app != 0;/'
tx_mutant tx_parts_unsealed flashfw.cc 's/if (img.sealed != FW_SEALED || (!do_bit/if (false || (!do_bit/'
tx_mutant tx_part_missing_ok flashfw.cc 's/ || (do_bit \&\& !img.bit) || (do_app \&\& !img.app)//'
tx_mutant tx_equal_bit_always flashfw.cc 's/    return img.bit \&\& region_equals(fl, FW_BIT_ADDR, img.bit, img.bit_len, \&bad);/    return true;/'
tx_mutant tx_seal_empty flashfw.cc 's/    if (!bit \&\& !app) {/    if (false) {/'
tx_mutant tx_parts_tail_kept flashfw.cc 's/        if (!clear_tail(w, FW_BIT_ADDR + img.bit_len, bslot, FWP_BIT)) {/        if (false) {/'
# the sources print no format the firmware's printf gets wrong
if grep -nE '%%|%\.[0-9*]+s|%0[0-9]x' "$H/txu.cc" "$H/flashlock.cc" "$H/flashlock_board.cc" "$H/espinspect.cc" | grep -vE '^[^:]+:[0-9]+:[[:space:]]*//'; then echo "run_txu: a format the firmware's printf gets wrong (above)" >&2; exit 1; fi
echo "run_txu: ok"
