# shellcheck shell=bash
# The units of zx (T239 D17), moved out of tests/coreapp/run.sh by tools/tests_to_core.py. tests/coreapp/run.sh sources this file at its end, with every variable
# and function of the shared harness set; the test programs, sources and headers of this folder win over the shared ones for this app (CORE_TESTS, find_src, tpath).
TESTS_ZX="$ROOT/cores/zx/app/tests"
source "$TESTS_ZX/zx_drv.sh"
zx_drv_dir "$T/zxtab" || { echo "coreapp: the ZX Spectrum's driver table was not generated" >&2; exit 1; }   # zx_regs.h names the registers through it
ZXLIB="$ROOT/cores/zx/app/lib"   # the ZX Spectrum's library lives in its own repository part (cores/zx/app/lib), not in coreapp/cores; it holds its own copies of tape_deck.*, tape_pulses.* and hk_defaults.h
# The ZX Spectrum is its own app too (cores/zx/app): zxapp CMD... builds like ddtapp, with its library, its table and TEST_APP_ZX; APPLIB puts its library first in the source lookup,
# so its own copies of tape_deck.cc, tape_pulses.cc (and the headers, by -I) are the ones built, not the shared app's.
INC_ZX=(-DTEST_APP_ZX=1 -I"$TESTS_ZX" -DGOLDEN_DIR=\"$TESTS_ZX/golden\" -I"$ZXLIB" -I"$T/zxtab/drv")
for i in "${INC[@]}"; do case "$i" in "-I$T/drv"|-DGOLDEN_DIR=*) ;; *) INC_ZX+=("$i") ;; esac; done
zxapp() { local INC=("${INC_ZX[@]}") CORE_TESTS="$TESTS_ZX" CA_APP=zx APPLIB="$ZXLIB"; "$@"; }
ZXS="zx.cc zx_snap.cc tape_pulses.cc tape_deck.cc $BASEL"
ZXA="menuctl.cc picklist.cc filemgr.cc menukeytab.cc menuflows.cc menuvlist.cc lineedit.cc menutree.cc $ZXS"
ZXD="desc_check.cc overlay.cc descrec.cc $ZXA"
zxapp run zx test_zx.cc $ZXS
zxapp run zx_snap test_zx_snap.cc zx_snap.cc
zxapp run boot_zx test_boot_zx.cc $ZXS
zxapp run tape_zx test_tape.cc tape_pulses.cc
zxapp run zx_memops test_memops.cc $SVC $ZXD
RARGS="$T/bundles/desc_own.bin" zxapp run zx_desc test_desc.cc $ZXD
RARGS="$BUN/app" zxapp run zx_descgen test_descgen.cc compiled_table.cc libfiles.cc libmenu.cc bundle.cc mediumwait.cc libload.cc libscan.cc instfiles.cc instcore.cc start.cc handover.cc $ZXD
zxapp run zx_hotkeys test_hotkeys.cc $ZXD
# T239.5 D13: every key of its menu against its golden (zx_keywalk: golden/keywalk_zx.txt, ids_zx.txt) and its declaration over the API (zx_uicover), TEST_APP_ZX
zxapp run zx_keywalk test_keywalk.cc libfiles.cc instfiles.cc instcore.cc $ZXD
zxapp run zx_uicover test_uicover.cc uiapp.cc libfiles.cc $SVC $ZXD
zxapp run zx_mounts test_media_mounts_engines.cc $ZXD start.cc handover.cc
# the ZX Spectrum's rows of the host's ROM table: gen_romreq built as the Spectrum's app, its rows in the host's table
t_romreq_zx() {
  local INC=("${INC_ZX[@]}") CORE_TESTS="$TESTS_ZX" APPLIB="$ZXLIB"
  build "$T/gen_romreq_zx" "$TESTS_ZX/gen_romreq.cc" $ZXD
  "$T/gen_romreq_zx" > "$T/romreq_zx.inc"
  python3 -I - "$T/romreq_zx.inc" "$HA/romreq_table.inc" <<'PY' || { echo "romreq_table.inc is stale: gen_romreq's rows of the ZX Spectrum's app are not (in this order) in host-app/software/hostapp/romreq_table.inc" >&2; exit 1; }
import sys
own = [l for l in open(sys.argv[1]) if not l.startswith("//")]
host = iter(l for l in open(sys.argv[2]) if not l.startswith("//"))
sys.exit(0 if all(any(h == r for h in host) for r in own) and own else 1)
PY
  echo "test zx_romreq: PASS"
  return 0
}
if ca_in zx && ca_unit zx_romreq; then t_romreq_zx; PASS=$((PASS + 1)); fi
