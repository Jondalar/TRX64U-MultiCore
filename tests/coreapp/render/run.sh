#!/usr/bin/env bash
# Renders the core application's menu with the real ui.cc, MenuCtl and LVGL, the board stubbed.
#   tests/coreapp/render/run.sh [outdir]
#   tests/coreapp/render/run.sh --own   prints the cores of RENDER_OWN (ci/lane.sh: which core lane runs its render build) and stops
#   RENDER_CORE=shared|<core>|all (default shared): the shared bench (ci/apptests.sh coreapp_render, ci/render.sh), the build of one core of RENDER_OWN (that core's
#   lane, ci/lane.sh core:<id> ui), or the shared bench and every build of RENDER_OWN (by hand)
set -euo pipefail
# T239.5 D13: the cores that moved to an app of their own are render builds of their own (RENDER_OWN_APP, RENDER_APP_<CORE>: render.cc takes that core's cases only, its walks
# visit that core alone; srclist.sh compiles its library against its own driver table and no other core's). The shared bench builds none of them; each runs in its core's lane.
RENDER_OWN="atari800 nes c264 zxnext neogeo amiga ddt_novic zx cpc pet c128"
if [ "${1:-}" = --own ]; then echo "$RENDER_OWN"; exit 0; fi
R="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$R/../../.." && pwd)"
source "$ROOT/tests/hostgen.sh"   # T239.3 V-3b: CPATH holds hostio.h (IOBASE, U2P_IO_BASE) and basetab.h, generated from the TOML A of the host
HA="$ROOT/host-app/software/hostapp"
L="${ULTIMATE_SW:-$ROOT/vendor/gideon/software}/lvgl"   # ULTIMATE_SW points at another copy
OUT="${1:-/tmp/coreapp-render}"; mkdir -p "$OUT"
B="$(mktemp -d)"; T="$B"; CXX="${CXX:-c++}"
# the diag core's example tests are record 8 of diag.cor (T175): the real packer makes the record, the application's installer writes it (tests/coreapp/diag_examples.h)
python3 "$ROOT/tools/cor_pack.py" files-record "$ROOT/cores/diag/sd" --old "$ROOT/cores/diag/superseded" -o "$B/diag_files.rec" > /dev/null
export DIAG_FILES_REC="$B/diag_files.rec"
# the record of a core with a Timewarp page and a Monitor page (golden/desc_cut.toml, T239.3 P1-8), compiled by the real tool; render.cc reads it over the NES (argv[2], the NES's build)
python3 -I -c 'import sys; sys.path.insert(0, sys.argv[1] + "/tools"); import descrec; open(sys.argv[3], "wb").write(descrec.compile_text(open(sys.argv[2]).read()))' "$ROOT" "$ROOT/tests/coreapp/golden/desc_cut.toml" "$B/desc_cut.bin"
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
RENDER_CORE="${RENDER_CORE:-shared}"
case " shared all $RENDER_OWN " in *" $RENDER_CORE "*) ;; *) echo "render: RENDER_CORE=$RENDER_CORE is not shared, all or one of: $RENDER_OWN" >&2; exit 2 ;; esac
CORE_INCS0="$CORE_INCS"
srcs_of() {   # srcs_of shared|<core>: SRCS and CORE_INCS of that build
  CORE_INCS="$CORE_INCS0"
  if [ "$1" = shared ]; then OWN_ONLY="" OWN_SKIP="$RENDER_OWN" OWN_TAG=shared coreapp_srcs; else OWN_ONLY="$1" OWN_SKIP="" OWN_TAG="$1" coreapp_srcs; fi
}
# the driver table the shared app is linked with (T239.3 D31): the drive library (d1541_drive.cc) names its registers through drvtab.h
python3 "$ROOT/tools/descrec.py" drivers "$ROOT/coreapp/gateware/shared.toml" "$B/drv" --maps "$ROOT/app/sdkpkg/maps" > /dev/null
build() {   # build <output> [extra defines]: the build of $SRCS and $CORE_INCS (srcs_of)
  need_objs
  # shellcheck disable=SC2086
  c++ $CORE_DEFS -O1 -DGOLDEN_DIR=\"$ROOT/tests/coreapp/golden\" ${2:-} -I"$B" -I"$B/drv" -iquote "$L" $CORE_INCS \
    -pthread -o "$1" "$R/render.cc" $SRCS "$OBJS"/*.o -lz -lm
}
t_render() { build "$B/render_$RNAME" "$RDEFS ${CHEAP_DEBUG:-}"; "$B/render_$RNAME" "$OUT" "$B/desc_cut.bin"; }
render_one() {   # render_one shared|<core>: one build as a unit (the shared bench keeps its unit name render_coreapp)
  RNAME="$1"; RDEFS=""
  srcs_of "$1" || exit 1
  [ "$1" = shared ] || RDEFS="-DRENDER_OWN_APP=1 -DRENDER_APP_$(printf '%s' "$1" | tr '[:lower:]' '[:upper:]')=1 -DRENDER_APP_NAME=\"$1\""
  CC_KEYEXTRA="lvgl=$LK srcs=$SRCS incs=$CORE_INCS defs=$RDEFS cheap=${CHEAP_DEBUG:-}"
  [ "$1" != nes ] || CC_KEYEXTRA="$CC_KEYEXTRA cut=$(shasum -a 256 "$B/desc_cut.bin" | cut -c1-16)"
  if [ "$1" = shared ]; then cc_block render_coreapp t_render build need_objs; else cc_block "render_coreapp_$1" t_render build need_objs; fi
}
if [ "$RENDER_CORE" = all ]; then
  for c in shared $RENDER_OWN; do render_one "$c"; done
else
  render_one "$RENDER_CORE"
fi
