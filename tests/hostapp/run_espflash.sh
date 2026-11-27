#!/usr/bin/env bash
# T152 S8: flashing the ESP32 image set of a .cfw/.ue2 (espflash.cc) against a model of the ESP32 ROM loader (esp_loader_model.h). Files from mkesptest.py.
# Each decision has a control: a mutant of the source under test has to go red.
#   tests/hostapp/run_espflash.sh [scratch dir]          (run.sh calls it with its own)
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
source "$ROOT/tests/hostgen.sh"   # T239.3 V-3b: CPATH holds hostio.h (IOBASE, U2P_IO_BASE) and basetab.h, generated from the TOML A of the host
H="$ROOT/host-app/software/hostapp"
T="${1:-}"
if [ -z "$T" ]; then T="$(mktemp -d)"; trap 'rm -rf "$T"' EXIT; fi
[ -f "$T/sha256.o" ] || cc -c -w -I"$H" -o "$T/sha256.o" "$H/sha256.c"
mkdir -p "$T/espf"
python3 "$ROOT/tests/hostapp/mkesptest.py" "$T/espf"
TST="$ROOT/tests/hostapp/test_espflash.cc"
INC=(-I"$H" -I"$ROOT/tests/hostapp")
c++ -std=gnu++14 -Wall -Wno-unused-function "${INC[@]}" -o "$T/test_espflash" "$TST" "$H/espflash.cc" "$H/espinspect.cc" "$T/sha256.o" 2>&1
"$T/test_espflash" "$T/espf"
ef_mutant() {   # name, source file name (in $H), sed expression
  local name="$1" src="$2" expr="$3" bad="$T/espf/mut_$1.cc" list=()
  sed -e "$expr" "$H/$src" > "$bad"
  cmp -s "$bad" "$H/$src" && { echo "CONTROL $name: the sed changed nothing" >&2; exit 1; }
  for f in espflash.cc espinspect.cc; do if [ "$f" = "$src" ]; then list+=("$bad"); else list+=("$H/$f"); fi; done
  c++ -std=gnu++14 -w "${INC[@]}" -o "$T/espf/mut_$name" "$TST" "${list[@]}" "$T/sha256.o" 2>&1
  if "$T/espf/mut_$name" "$T/espf" > "$T/espf/mut_$name.log" 2>&1; then echo "CONTROL $name: the bench PASSED against the mutant" >&2; exit 1; fi
  grep -q '^FAIL' "$T/espf/mut_$name.log" || { echo "CONTROL $name: it failed without a red check" >&2; exit 1; }
  echo "control $name: fails as it must ($(grep -c '^FAIL' "$T/espf/mut_$name.log") check(s) red)"
}
ef_mutant ef_ptable_address espflash.cc 's/info.ptable_len, ESPF_ADDR_PTABLE)/info.ptable_len, ESPF_ADDR_PTABLE + 0x1000)/'
ef_mutant ef_app_address espflash.cc 's/info.app_len, ESPF_ADDR_APP)/info.app_len, ESPF_ADDR_BOOT)/'
ef_mutant ef_ptable_skipped espflash.cc 's/write_image(j, EFS_PTABLE, info.ptable_off, info.ptable_len, ESPF_ADDR_PTABLE) \&\&/true \&\&/'
ef_mutant ef_not_armed espflash.cc 's/^    link.arm(true);$/    ;/'
ef_mutant ef_never_disarmed espflash.cc 's/^    link.arm(false);$/    ;/'
ef_mutant ef_no_run_mode espflash.cc 's/^    link.leave_loader();$/    ;/'
ef_mutant ef_enter_once espflash.cc 's/tries < 3 \&\& !entered/tries < 1 \&\& !entered/'
ef_mutant ef_sync_unchecked espflash.cc 's/    for (int i = 0; i < 10; i++) {/    for (int i = 0; i < 1; i++) { l.send_packet(f, sizeof(f)); return true;/'
ef_mutant ef_no_attach espflash.cc 's/    if (!command(l, OP_ATTACH_SPI, 8, 0, p, 2500)) {/    if (false) {/'
ef_mutant ef_baud_not_applied espflash.cc 's/^    l.set_baud(ESPF_BAUD);$/    ;/'
ef_mutant ef_status_ignored espflash.cc 's/            return (rx_buf\[2\] == 2 || rx_buf\[2\] == 4) \&\& rx_buf\[8\] == 0;/            return true;/'
ef_mutant ef_checksum_wrong espflash.cc 's/        uint8_t chk = 0xEF;/        uint8_t chk = 0xEE;/'
ef_mutant ef_no_pad espflash.cc 's/        memset(blk + 16, 0xFF, ESPF_BLOCK);/        memset(blk + 16, 0x00, ESPF_BLOCK);/'
ef_mutant ef_started_not_set espflash.cc 's/    j.r->started = true; /    ; /'
ef_mutant ef_version_ignored espflash.cc 's/    if (res->major != want_major || res->minor != want_minor) {/    if (false) {/'
ef_mutant ef_ident_always_asked espflash.cc 's/    if (!esp_version_parse(info.version, \&want_major, \&want_minor)) {/    if (false) {/'
ef_mutant ef_set_unchecked espflash.cc 's/    if (!info.boot || !info.ptable || !info.app || !info.digest_ok || /    if (/'
ef_mutant ef_extent_unchecked espflash.cc 's/ || (uint64_t)info.app_off + info.app_len > size) {/) {/'
ef_mutant ef_read_error_ignored espflash.cc 's/        if (!j.src.read(file_off + i \* ESPF_BLOCK, blk + 16, now)) {/        if (false) {/'
ef_mutant ef_version_parse_loose espflash.cc 's/    if (!digits || \*p || a > 0xFFFF || b > 0xFFFF) {/    if (!digits || a > 0xFFFF || b > 0xFFFF) {/'
echo "run_espflash: ok"
