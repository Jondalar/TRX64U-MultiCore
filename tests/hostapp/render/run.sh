#!/usr/bin/env bash
# Renders the boot menu screens to PNG with the real hostui.cc and LVGL, firmware stubbed (native, no board).
#   tests/hostapp/render/run.sh [outdir]
set -euo pipefail
R="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$R/../../.." && pwd)"
H="$ROOT/host-app/software/hostapp"
ULT="${ULTIMATE_SW:-$ROOT/vendor/gideon/software}"   # ULTIMATE_SW points at another copy
L="$ULT/lvgl"
OUT="${1:-/tmp/hostapp-render}"; mkdir -p "$OUT"
B="$(mktemp -d)"; T="$B"; CXX="${CXX:-c++}"
# T100 D14: the bench and each of its controls is a unit (tests/ctlcache.sh): the unit's reads - every source, header, the mutated copy, the
# fixtures, the goldens, what the compiler and the program open - are recorded when it passes, and a unit whose reads are unchanged is not built
# or run again. LVGL's objects are outside the tree: their key (the pinned commit and lv_conf.h) is part of every unit's key.
source "$ROOT/tests/ctlcache.sh"
CC_SUITE=hostapp_render
trap 'cc_summary; rm -rf "$B"' EXIT
LK="$("$ROOT/tests/lvgl_objs.sh" --key "$L" "$H/lv_conf.h")"
cp "$H/lv_conf.h" "$B/"   # what lvgl_objs.sh puts beside the objects, made at once so that every unit sees it whether or not an earlier one built
need_objs() { [ -n "${OBJS:-}" ] || OBJS="$("$ROOT/tests/lvgl_objs.sh" "$L" "$H/lv_conf.h")"; }
CORE="$ULT"
# build OUT HOSTUI DISCOVER CFW [CFWICON]: the bench around the given sources
build_now() {
  need_objs
  # XFLAGS: flags for the whole bench
  c++ -std=gnu++14 -O1 -w -DHOSTUI_RENDER_TEST -DHOSTAPP_FLASH_LOCK=${FLASH_LOCK:-1} -DVIDEOMODE_NO_BOARD=1 -DWEBUI_NO_BOARD=1 -DLV_CONF_INCLUDE_SIMPLE ${XFLAGS:-} -I"${7:-$B}" -I"$R/stub" -I"$H" -iquote "$L" -I"$ROOT/app/view" -I"$ROOT/uicore" -I"$ROOT/coreapp/src" -I"$ROOT/app/edit" -I"$ROOT/tests" -I"$ROOT/app/video" -I"$CORE/io/c64" -I"$CORE/components" -I"$CORE/system" -I"$ROOT/host-app/software/filesystem" -I"$CORE/io/flash" \
    -o "$1" "$R/render.cc" "$2" "$ROOT/app/view/trxview.cc" "$ROOT/app/video/videomode.cc" "$ROOT/app/video/edid.cc" "$ROOT/coreapp/src/picmodes.cc" "$H/hostvideo.cc" "${HC_SRC:-$H/host_core.cc}" "${HCF_SRC:-$H/host_core_flow.cc}" "${HCR_SRC:-$H/host_core_flow_restore.cc}" "$H/host_core_flow_convert.cc" "$H/host_core_flow_esp.cc" "$H/host_core_flow_fileops.cc" "$H/host_core_flow_wifi.cc" "$H/host_core_flow_format.cc" "$H/host_core_nav.cc" "$H/host_core_cm.cc" "$H/host_core_cfgov.cc" "$H/configlist.cc" "${HCW_SRC:-$H/host_core_rows.cc}" "${HCU_SRC:-$H/host_core_ui.cc}" "$H/host_core_model.cc" "$H/hostdecl.cc" "$H/hostdomain.cc" "$H/startfile.cc" "$ROOT/uicore/uic_json.cc" "$ROOT/uicore/uic_sha.cc" "$ROOT/uicore/uic_svc.cc" "$ROOT/uicore/uic_net.cc" "$H/webui.cc" "$H/hostreport.cc" "$H/hostreport_live.cc" "$H/bugreport.cc" "$H/cor.cc" "$H/directboot.cc" "$4" "${5:-$H/cfwicon.cc}" "$H/bootcfg.cc" "$H/menusnap.cc" "$H/fileops.cc" "$H/coremaint.cc" "$ROOT/app/edit/lineedit.cc" "$H/startcheck.cc" "$H/handover.cc" "$3" "$H/sha256.c" "$H/fmtconfirm.cc" "$H/mediaui.cc" "$H/mediasniff.cc" "$H/fwknown.cc" "$H/fwconvert.cc" "$H/fwrestore.cc" "$H/flashfw.cc" "$H/fwconfirm.cc" "$H/txu.cc" "$H/espinspect.cc" "$H/espflash.cc" "${6:-$OBJS}"/*.o -lz -lm
}
python3 "$R/mktree.py" "$B/tree"
# the benches write into the tree (reports, converted files): every unit starts from the tree as mktree.py made it (cc_block runs CC_PRE first), so
# that what a unit finds there does not depend on which units before it were served from the cache
for d in "$B"/tree "$B"/tree_cv; do [ -d "$d" ] && cp -a "$d" "$d.pristine"; done
reset_trees() { for d in "$B"/tree "$B"/tree_cv; do if [ -d "$d.pristine" ]; then rm -rf "$d"; cp -a "$d.pristine" "$d"; fi; done; }
CC_PRE=reset_trees
# T137: the bench's vendor .ue2 is on the table of official firmware (a row only this build has, known_fw.h FW_TEST_SHA): the name comes from the table
FW_SHA="$(python3 -c 'import hashlib,sys; print(hashlib.sha256(open(sys.argv[1],"rb").read()).hexdigest())' "$B/tree_cv/Usb0/fw/vendor.ue2")"
XFLAGS="${XFLAGS:-} -DFW_TEST_SHA=$FW_SHA"
CC_KEYEXTRA="lvgl=$LK xflags=$XFLAGS"   # cc_block adds this to every unit key: LVGL's objects live outside the tree, the flags are values of this script
main_render() { [ -x "$B/render" ] || build_now "$B/render" "$H/hostui.cc" "$H/discover.cc" "$H/cfw.cc"; }
t_main() { main_render; ROWS_GOLDEN="$R/golden/hostrows.txt" LOGO_DIR="$ROOT/cores" "$B/render" "$OUT" "$B/tree"; }
cc_block render_main t_main main_render build_now need_objs

# T174 D5.4: the negative controls of this bench remain only for what writes the flash (the update at the UI), further below.
# build ARGS... only notes the build: it is made by the control that follows (a control that comes from the cache is not built at all)
PENDING=() PX=""
build() { PENDING=("$@"); PX="${XFLAGS:-}"; }
_control() {   # name, XFLAGS, a marker, then build's arguments: build the mutant and run it, it has to go red
  local name="$1" px="$2"; shift 3
  XFLAGS="$px" build_now "$@"
  local rc=0
  ROWS_GOLDEN="$R/golden/hostrows.txt" LOGO_DIR="$ROOT/cores" "$B/render_$name" "$B/out_$name" "$B/tree" > "$B/$name.log" 2>&1 || rc=$?
  [ "$rc" = 0 ] && { echo "CONTROL $name: the bench PASSED against the mutant" >&2; exit 1; }
  # a signal (rc >= 128) is a crash of the mutant, not a check that went red: a crash in a control can hide a real defect of the code under test
  [ "$rc" -ge 128 ] && { echo "CONTROL $name: the mutant CRASHED (signal $((rc - 128))), it has to fail by a check" >&2; tail -5 "$B/$name.log" >&2; exit 1; }
  grep -q '^FAIL' "$B/$name.log" || { echo "CONTROL $name: it failed without a red check" >&2; exit 1; }
  echo "control $name: fails as it must ($(grep -c '^FAIL' "$B/$name.log") check(s) red)"
}
control() { CC_KIND=control cc_block "$1" _control build_now need_objs -- "$1" "$PX" "lvgl=$LK" "${PENDING[@]}"; }
# T146: the saved menu picture. Process 1 starts the menu, lets it settle and saves the picture (S3); process 2 starts again with that
# boot.cfg and picture (S1, S4) or with another boot.cfg / mode / build (S2: refused). T146_RUN bin outdir expect [CFGADD]
t146_run() {
  local bin="$1" out="$2" expect="$3" cfgadd="${4:-}"
  T146_START=1 T146_EXPECT="$expect" T146_CFGADD="$cfgadd" LOGO_DIR="$ROOT/cores" "$bin" "$out" "$B/tree"
}
t146_save() {   # bin outdir
  mkdir -p "$2"
  T146_SAVE_ONLY=1 LOGO_DIR="$ROOT/cores" "$1" "$2" "$B/tree"
}
t_t146_base() {
  main_render
  t146_save "$B/render" "$B/t146"
  t146_run "$B/render" "$B/t146" shown
  t146_run "$B/render" "$B/t146" refused "core=/SD/other.cor|Other|"        # boot.cfg: an entry added
  t146_run "$B/render" "$B/t146" refused "video=1,1"                         # another picture mode
  mkdir -p "$B/t146_alt"
  XFLAGS="-DT146_ALT_GIT" build_now "$B/render_t146alt" "$H/hostui.cc" "$H/discover.cc" "$H/cfw.cc"
  t146_save "$B/render_t146alt" "$B/t146_alt"                                # a build with another git id saved this picture ...
  mkdir -p "$B/t146_other"
  cp "$B/t146/t146_boot.cfg" "$B/t146_other/"
  cp "$B/t146_alt/t146_menu.snap" "$B/t146_other/"
  t146_run "$B/render" "$B/t146_other" refused
}
cc_block t146_base t_t146_base main_render build_now need_objs t146_run t146_save

# T137 P2/P3 and T152 (the update at the UI; the module's controls are in run_fwrestore.sh, run_txu.sh, run_flashlock.sh): one decision of hostui.cc taken out each.
t137_ctl() { cc_no_controls && return 0;   # name, sed expression on hostui.cc
  local name="$1" expr="$2"
  mkdir -p "$B/out_$name"
  sed -e "$expr" "$H/hostui.cc" > "$B/hostui_$name.cc"
  cmp -s "$B/hostui_$name.cc" "$H/hostui.cc" && { echo "CONTROL $name: the sed changed nothing" >&2; exit 1; }
  build "$B/render_$name" "$B/hostui_$name.cc" "$H/discover.cc" "$H/cfw.cc"
  control "$name"
}
# T192.2 P3, P2: the lock decision and the update live in the core now (host_core.cc: hc_start_sequence, hc_lock_flash; host_core_flow.cc: hc_update_run): the same decisions, taken out of
# those files. hc_ctl NAME SED FILE builds the bench around a mutated copy of FILE (host_core.cc or host_core_flow.cc).
hc_ctl() { cc_no_controls && return 0;
  local name="$1" expr="$2" file="${3:-host_core.cc}"
  mkdir -p "$B/out_$name"
  sed -e "$expr" "$H/$file" > "$B/hc_${name}_$file"
  cmp -s "$B/hc_${name}_$file" "$H/$file" && { echo "CONTROL $name: the sed changed nothing" >&2; exit 1; }
  case "$file" in host_core.cc) export HC_SRC="$B/hc_${name}_$file" ;; host_core_flow.cc) export HCF_SRC="$B/hc_${name}_$file" ;; host_core_flow_restore.cc) export HCR_SRC="$B/hc_${name}_$file" ;; host_core_ui.cc) export HCU_SRC="$B/hc_${name}_$file" ;; host_core_rows.cc) export HCW_SRC="$B/hc_${name}_$file" ;; *) echo "CONTROL $name: unknown file $file" >&2; exit 1 ;; esac
  build "$B/render_$name" "$H/hostui.cc" "$H/discover.cc" "$H/cfw.cc"
  control "$name"
  unset HC_SRC HCF_SRC HCR_SRC HCU_SRC HCW_SRC
}
hc_ctl t137r_unknown_taken_official 's/        if (!known) {/        if (false) {/' host_core_flow_restore.cc
hc_ctl t137r_no_equal_check 's/if (fw_flash_equals(fl, img)) {/if (false) {/' host_core_flow_restore.cc
hc_ctl t152_no_lock 's/        if (!hc_lock_flash(\&why)) {/        if (false) {/'
hc_ctl t152_lock_failure_ignored 's/    if (flashprot_before_handover(\&m)) {/    if (flashprot_before_handover(\&m) || true) {/'
hc_ctl t152_all_parts_written 's/^    parts = 0;/    parts = FW_PART_BIT | FW_PART_APP;/' host_core_flow.cc
hc_ctl t152_equal_part_rewritten 's/    if (img.bit \&\& !fw_flash_equals_bit(fl, img)) {/    if (img.bit) {/' host_core_flow.cc
hc_ctl t152_protect_does_not_flip 's/        hc_protect_set(!flashprot_enabled());/        hc_protect_set(true);/' host_core_ui.cc

# T152 D9: the board build (HOSTAPP_FLASH_LOCK=0): no "Protect Flash from updates" row on F8. The bench renders the board build and checks the row is absent.
FLASH_LOCK=0 build_now "$B/render_nolock" "$H/hostui.cc" "$H/discover.cc" "$H/cfw.cc"
mkdir -p "$B/out_nolock"
T152_NOLOCK=1 LOGO_DIR="$ROOT/cores" "$B/render_nolock" "$B/out_nolock" "$B/tree"
