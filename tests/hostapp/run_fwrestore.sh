#!/usr/bin/env bash
# T137 S2/S3: the restore of the original firmware: which file is offered (fwrestore.cc), the check before the first erase, the flash layer
# (flashfw.cc: the gate, the write order, the read-back), the two confirmations (fwconfirm.cc). Packages from mkrestoretest.py (the real writers), a fake flash
# with erase semantics. Each decision has a control: a mutant of the source under test has to go red.
#   tests/hostapp/run_fwrestore.sh [scratch dir]          (run.sh calls it with its own)
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
source "$ROOT/tests/hostgen.sh"   # T239.3 V-3b: CPATH holds hostio.h (IOBASE, U2P_IO_BASE) and basetab.h, generated from the TOML A of the host
H="$ROOT/host-app/software/hostapp"
HS="$ROOT/host-app/software"
T="${1:-}"
if [ -z "$T" ]; then T="$(mktemp -d)"; trap 'rm -rf "$T"' EXIT; fi
[ -f "$T/sha256.o" ] || cc -c -w -I"$H" -o "$T/sha256.o" "$H/sha256.c"
# host-app/software/io/flash/flash.h is the Flash class of the Ultimate application, as the board builds it.
if [ ! -f "$HS/io/flash/flash.h" ]; then
  echo "SKIPPED test_fwrestore: vendor/gideon/software is missing (tools/gideon_sources.sh)"
  exit 0
fi
mkdir -p "$T/fwr"
python3 "$ROOT/tests/hostapp/mkrestoretest.py" "$T/fwr"
INC=(-I"$H" -I"$HS/io/flash" -I"$HS/components" -I"$HS/infra" -I"$HS/system" -I"$HS/api" -I"$HS/filesystem")
TST="$ROOT/tests/hostapp/test_fwrestore.cc"
# the sources the bench links; a mutant replaces one of them (by its file name) with the changed copy
srcs=("$H/fwrestore.cc" "$H/flashfw.cc" "$H/fwconfirm.cc" "$H/fwknown.cc" "$H/cfw.cc" "$H/cor.cc" "$H/handover.cc")
c++ -std=gnu++14 -Wall -Wno-unused-function "${INC[@]}" -o "$T/test_fwrestore" "$TST" "${srcs[@]}" "$T/sha256.o" 2>&1
"$T/test_fwrestore" "$T/fwr"
# the emulator's flash chip is accepted by a test build only
c++ -std=gnu++14 -w -DHOSTAPP_EMU_FLASH "${INC[@]}" -o "$T/test_fwrestore_emu" "$TST" "${srcs[@]}" "$T/sha256.o" 2>&1
"$T/test_fwrestore_emu" "$T/fwr"
fwr_mutant() {   # name, source file name (in $H), sed expression
  local name="$1" src="$2" expr="$3" bad="$T/fwr/mut_$1.cc" list=()
  sed -e "$expr" "$H/$src" > "$bad"
  cmp -s "$bad" "$H/$src" && { echo "CONTROL $name: the sed changed nothing" >&2; exit 1; }
  for f in "${srcs[@]}"; do if [ "$(basename "$f")" = "$src" ]; then list+=("$bad"); else list+=("$f"); fi; done
  c++ -std=gnu++14 -w "${INC[@]}" -o "$T/fwr/mut_$name" "$TST" "${list[@]}" "$T/sha256.o" 2>&1
  if "$T/fwr/mut_$name" "$T/fwr" > "$T/fwr/mut_$name.log" 2>&1; then echo "CONTROL $name: the bench PASSED against the mutant" >&2; exit 1; fi
  grep -q '^FAIL' "$T/fwr/mut_$name.log" || { echo "CONTROL $name: it failed without a red check" >&2; exit 1; }
  echo "control $name: fails as it must ($(grep -c '^FAIL' "$T/fwr/mut_$name.log") check(s) red)"
}
# who may be restored (P2)
fwr_mutant fwr_any_firmware fwrestore.cc 's/case FWK_FIRMWARE: return known != 0;/case FWK_FIRMWARE: return true;/'
fwr_mutant fwr_backup_other_chip fwrestore.cc 's/case FWK_BACKUP_COR: return chip \&\& !strcmp(p.flash_id, chip);/case FWK_BACKUP_COR: return true;/'
fwr_mutant fwr_foreign_cor fwrestore.cc 's/case FWK_FIRMWARE: return known != 0;/case FWK_FIRMWARE: return known != 0; case FWK_FOREIGN_COR: return true;/'
fwr_mutant fwr_our_cor fwrestore.cc 's/case FWK_FIRMWARE: return known != 0;/case FWK_FIRMWARE: return known != 0; case FWK_OUR_COR: return true;/'
# the check before the first erase (P3)
fwr_mutant fwr_stage_gate_skipped fwrestore.cc 's/if (!fw_can_restore(p, known, chip)) {/if (false) {/'
fwr_mutant fwr_cor_sha_skipped fwrestore.cc 's/if (ci.from_cor \&\& (strcmp(o->bit_sha, ci.bit_sha) || strcmp(o->app_sha, ci.app_sha))) {/if (false) {/'
fwr_mutant fwr_slot_unchecked fwrestore.cc 's/if (p.bit_flash_len > FW_BIT_SLOT) {/if (false) {/'
fwr_mutant fwr_app_slot_unchecked fwrestore.cc 's/if (p.app_flash_len > FW_APP_SLOT) {/if (false) {/'
fwr_mutant fwr_load_addr_unchecked fwrestore.cc 's/return fail(o, "the application does not load and start at 0x30000");/(void)0;/'
fwr_mutant fwr_cancel_ignored fwrestore.cc 's/return fail(o, abort \&\& abort(ctx) ? "cancelled" : "the file cannot be read");/return fail(o, "the file cannot be read");/'
fwr_mutant fwr_seal_unchecked flashfw.cc 's/if (!checked) {/if (false) {/'
fwr_mutant fwr_record_unchecked flashfw.cc 's/if (run < dest || run >= dest + len) {/if (false) {/'
fwr_mutant fwr_write_unsealed flashfw.cc 's/if (img.sealed != FW_SEALED || /if (false || /'
fwr_mutant fwr_write_chip_unchecked flashfw.cc 's/if (!fw_flash_check(fl, \&type, \&cwhy)) {/if (false) {/'
# the flash layer: order, retry, read-back
fwr_mutant fw_header_first flashfw.cc 's/write_range(w, FW_APP_ADDR, img.app, img.app_len, true, FWP_APP)/write_range(w, FW_APP_ADDR, img.app, img.app_len, false, FWP_APP)/'
fwr_mutant fw_bit_before_app flashfw.cc 's/if (do_app \&\& !write_range(w, FW_APP_ADDR, img.app, img.app_len, true, FWP_APP)) {/if (!write_range(w, FW_BIT_ADDR, img.bit, img.bit_len, false, FWP_BIT) || !write_range(w, FW_APP_ADDR, img.app, img.app_len, true, FWP_APP)) {/'
fwr_mutant fw_no_retry flashfw.cc 's/for (int tries = 0; tries < 3; tries++) {/for (int tries = 0; tries < 1; tries++) {/'
fwr_mutant fw_no_blank_check flashfw.cc 's/    for (uint32_t off = 0; off < FW_SECTOR; off += FW_PAGE) {$/    for (uint32_t off = 0; off < 0; off += FW_PAGE) {/'
fwr_mutant fw_locked_is_started flashfw.cc 's/if (have_before \&\& sector_sum(w.fl, addr, \&after) \&\& after == before) {/if (false) {/'
fwr_mutant fw_no_readback flashfw.cc 's/if (!region_equals(fl, reg\[k\].addr + off, reg\[k\].p + off, n, \&bad)) {/if (false) {/'
fwr_mutant fw_tail_kept flashfw.cc 's/if (!clear_tail(w, FW_BIT_ADDR + img.bit_len, FW_BIT_SLOT, FWP_BIT)) {/if (false) {/'
fwr_mutant fw_flash_stamp_unchecked flashfw.cc 's/if (!flash_axss(fl, \&v, \&found) || !found || v != img.axss) {/if (false) {/'
fwr_mutant fw_chip_any flashfw.cc 's/if (!strcmp(type, "Winbond W25Q128")) {/if (true) {/'
fwr_mutant fw_geometry_any flashfw.cc 's/if (fl->get_page_size() != (int)FW_PAGE || fl->get_sector_size(0) != (int)FW_SECTOR || !fl->need_erase()) {/if (false) {/'
# the two confirmations
fwr_mutant fwc_one_confirmation fwconfirm.cc 's/            c->state = FWC_ASK2;/            c->state = FWC_STARTED; return FWCE_GO;/'
fwr_mutant fwc_held_y_answers fwconfirm.cc 's/if (now - c->t_arm < FWC_MIN_GAP_MS) {/if (false) {/'
fwr_mutant fwc_first_any_key fwconfirm.cc 's/if (k == FWCK_Y) {/if (true) {/'
fwr_mutant fwc_second_any_key fwconfirm.cc 's/if (k == FWCK_RETURN) {/if (true) {/'
fwr_mutant fwc_no_timeout fwconfirm.cc 's/if (c->state == FWC_ASK2 \&\& now - c->t_arm >= FWC_ARM_MS) {/if (false) {/'
# what the firmware's printf cannot do (seen in ue2emu 2026-10-29: "%.8s" printed the whole string, "%08x" printed capitals, "%%" printed two): not in the files that
# make text a reader parses or a person reads after a write
if grep -nE '%%|%\.[0-9*]+s|%0[0-9]x' "$H/fwrestore.cc" "$H/flashfw.cc" "$H/fwconfirm.cc" | grep -vE '^[^:]+:[0-9]+:[[:space:]]*//'; then echo "run_fwrestore: a format the firmware's printf gets wrong (above)" >&2; exit 1; fi
# T135 B2 / B1: a backup that holds our host is refused with the reason; a stamp alone (a self-built firmware) is not "ours"
fwr_mutant fwr_host_backup_allowed fwrestore.cc 's/if (ourfw_is_ours(/if (false \&\& ourfw_is_ours(/'
fwr_mutant fwr_stamp_alone_is_ours fwrestore.cc 's/ourfw_app_marked(app_dst + FW_REC_HDR, p.app_flash_len > FW_REC_HDR ? p.app_flash_len - FW_REC_HDR : 0)/true/'
