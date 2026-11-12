#!/usr/bin/env bash
# Renders the shared view layer (app/view) natively with the real LVGL, no firmware.
#   tests/view/run.sh [outdir]
# T100 D14: the render is a unit (tests/ctlcache.sh): when nothing it read - the sources and headers the compile opens, LVGL's headers, what the
# program opens - nor LVGL's objects (their key is part of the unit's) has changed since it passed, it is not built or run again.
set -euo pipefail
R="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$R/../.." && pwd)"
L="${ULTIMATE_SW:-$ROOT/vendor/gideon/software}/lvgl"
OUT="${1:-/tmp/view-render}"; mkdir -p "$OUT"
B="$(mktemp -d)"; T="$B"; CXX="${CXX:-c++}"
source "$ROOT/tests/ctlcache.sh"
CC_SUITE=view
trap 'cc_summary; rm -rf "$B"' EXIT
CONF="$ROOT/host-app/software/hostapp/lv_conf.h"
CC_KEYEXTRA="lvgl=$("$ROOT/tests/lvgl_objs.sh" --key "$L" "$CONF")"   # LVGL's objects live outside the tree
t_render() {
  local OBJS; OBJS="$("$ROOT/tests/lvgl_objs.sh" "$L" "$CONF")"
  cp "$OBJS/lv_conf.h" "$B/"
  c++ -std=gnu++14 -O1 -w -DLV_CONF_INCLUDE_SIMPLE -I"$B" -I"$L" -I"$ROOT/app/view" \
    -o "$B/render" "$R/render.cc" "$ROOT/app/view/trxview.cc" "$OBJS"/*.o -lz -lm
  "$B/render" "$OUT"
}
cc_block render_view t_render
