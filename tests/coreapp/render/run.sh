#!/usr/bin/env bash
# Renders the core application's menu with the real ui.cc, MenuCtl and LVGL, the board stubbed.
#   tests/coreapp/render/run.sh [outdir]
set -euo pipefail
R="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$R/../../.." && pwd)"
SRC="$ROOT/coreapp/src"; CORES="$ROOT/coreapp/cores"; HA="$ROOT/host-app/software/hostapp"
L="${ULTIMATE_SW:-$ROOT/vendor/gideon/software}/lvgl"   # ULTIMATE_SW points at another copy
OUT="${1:-/tmp/coreapp-render}"; mkdir -p "$OUT"
B="$(mktemp -d)"; T="$B"; CXX="${CXX:-c++}"
# the diag core's example tests are record 8 of diag.cor (T175): the real packer makes the record, the application's installer writes it (tests/coreapp/diag_examples.h)
python3 "$ROOT/tools/cor_pack.py" files-record "$ROOT/cores/diag/sd" --old "$ROOT/cores/diag/superseded" -o "$B/diag_files.rec" > /dev/null
export DIAG_FILES_REC="$B/diag_files.rec"
# T100 D14: the render is a unit (tests/ctlcache.sh): what a unit read - every source and header the compile opens, LVGL's headers,
# the goldens, what the program opens - is recorded when it passes; a unit whose reads are unchanged is not built or run again. Two values
# of this script enter the key by hand: LVGL's objects live outside the tree (their key), and the source list is derived from the Makefile.
source "$ROOT/tests/ctlcache.sh"
CC_SUITE=coreapp_render
trap 'cc_summary; rm -rf "$B"' EXIT
LK="$("$ROOT/tests/lvgl_objs.sh" --key "$L" "$HA/lv_conf.h")"
cp "$HA/lv_conf.h" "$B/"   # what lvgl_objs.sh puts beside the objects, made at once so that every unit sees it whether or not an earlier one built
need_objs() { [ -n "${OBJS:-}" ] || OBJS="$("$ROOT/tests/lvgl_objs.sh" "$L" "$HA/lv_conf.h")"; }
# The sources are the board build's own list (SRCS_CC of the core app's Makefile), the ones that live in the application's
# directories; a file added there is linked here too, so the two lists cannot drift apart (CI 941: the drive library was
# missing here only). NOT_HERE are the board-only files this build replaces with render.cc and the stubs.
MK="$ROOT/host-app/target/u64ii/riscv/coreapp/Makefile"
APPDIRS="$SRC $CORES $ROOT/app/loaders $ROOT/app/lib/cbm1541 $ROOT/app/edit $ROOT/app/view $ROOT/app/video $HA $ROOT/uicore"   # $HA: handover, bootcfg
NOT_HERE="table.cc hw.cc run.cc fm_fs.cc fm_medium.cc fastseek.cc libboard.cc nettransport.cc restsrv.cc uiapp_board.cc"
# the TRX64U's board-side contributors and what they stand on (the U64 registers, USB HID, the network stack, FreeRTOS tasks, lwIP sockets): tests/coreapp/board_stubs.cc stands in
NOT_HERE="$NOT_HERE u64cfg.cc libfiles.cc ringops.cc ring.cc ringfile.cc monitor.cc monsrv.cc online.cc diag.cc netops.cc hostwifi.cc netfs.cc ftp_client.cc a64_http.cc a64_assembly.cc trx64u_ops.cc dma_uart.cc appsvc.cc network_esp32.cc wifi.cc wifi_cmd.cc esp32.cc wifinet.cc usb_xinput.cc"   # usb_xinput.cc: the XInput USB driver (T196), on the USB stack
SRCS=""
for f in $(sed -n '/^SRCS_CC/,/^$/p' "$MK" | grep -oE '[A-Za-z0-9_]+\.cc'); do
  case " $NOT_HERE " in *" $f "*) continue ;; esac
  for d in $APPDIRS; do [ -f "$d/$f" ] && { SRCS="$SRCS $d/$f"; break; }; done
done
[ -n "$SRCS" ] || { echo "render: no sources from $MK" >&2; exit 1; }
# test-only sources: the descriptor check, sha256.c (a SRCS_C file of the Makefile) built as C++, the board's video stubbed
SRCS="$SRCS $SRC/desc_check.cc $ROOT/tests/coreapp/video_stub.cc $ROOT/tests/coreapp/sha256_cc.cc $ROOT/tests/coreapp/board_stubs.cc"
build() {   # build <output> [extra defines]
  need_objs
  c++ -std=gnu++14 -O1 -w -DVIDEOMODE_NO_BOARD=1 -DLV_CONF_INCLUDE_SIMPLE -DGOLDEN_DIR=\"$ROOT/tests/coreapp/golden\" ${2:-} -I"$B" -iquote "$L" -I"$ROOT/app/view" -I"$ROOT/app/video" -I"$SRC" -I"$ROOT/uicore" -I"$CORES" -I"$ROOT/app/loaders" -I"$ROOT/app/lib/cbm1541" -I"$ROOT/app/edit" \
    -I"$ROOT/tests/coreapp" -I"$ROOT/tests" -I"$ROOT/host-app/software/io/c64" -I"$ROOT/host-app/software/lvgl_port" -I"$ROOT/host-app/software/filesystem" -I"$HA" \
    -pthread -o "$1" "$R/render.cc" $SRCS "$OBJS"/*.o -lz -lm
}
CC_KEYEXTRA="lvgl=$LK srcs=$SRCS cheap=${CHEAP_DEBUG:-}"
t_render() { build "$B/render" "${CHEAP_DEBUG:-}"; "$B/render" "$OUT"; }
cc_block render_coreapp t_render build need_objs
