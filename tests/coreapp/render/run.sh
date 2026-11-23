#!/usr/bin/env bash
# Renders the core application's menu with the real ui.cc, MenuCtl and LVGL, the board stubbed.
#   tests/coreapp/render/run.sh [outdir]
set -euo pipefail
R="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$R/../../.." && pwd)"
HA="$ROOT/host-app/software/hostapp"
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
source "$ROOT/tests/coreapp/srclist.sh"   # T236 D2: the sources, defines and include path, shared with tools/menusim
coreapp_srcs || exit 1
# the driver table the shared app is linked with (T239.3 D31): the drive library (d1541_drive.cc) names its registers through drvtab.h
python3 "$ROOT/tools/descrec.py" drivers "$ROOT/coreapp/gateware/shared.toml" "$B/drv" --maps "$ROOT/app/sdkpkg/maps" > /dev/null
build() {   # build <output> [extra defines]
  need_objs
  # shellcheck disable=SC2086
  c++ $CORE_DEFS -O1 -DGOLDEN_DIR=\"$ROOT/tests/coreapp/golden\" ${2:-} -I"$B" -I"$B/drv" -iquote "$L" $CORE_INCS \
    -pthread -o "$1" "$R/render.cc" $SRCS "$OBJS"/*.o -lz -lm
}
CC_KEYEXTRA="lvgl=$LK srcs=$SRCS cheap=${CHEAP_DEBUG:-}"
t_render() { build "$B/render" "${CHEAP_DEBUG:-}"; "$B/render" "$OUT"; }
cc_block render_coreapp t_render build need_objs
