# shellcheck shell=bash
# The units of c128 (T239 D17), moved out of tests/coreapp/run.sh by tools/tests_to_core.py. tests/coreapp/run.sh sources this file at its end, with every variable
# and function of the shared harness set; the test programs, sources and headers of this folder win over the shared ones for this app (CORE_TESTS, find_src, tpath).
TESTS_C128="$ROOT/cores/c128/app/tests"
source "$TESTS_C128/c128_drv.sh"
c128_drv_dir "$T/c128tab" || { echo "coreapp: the C128's driver table was not generated" >&2; exit 1; }   # c128_regs.h names the registers through it
C128LIB="$ROOT/cores/c128/app/lib"   # the C128's library lives in its own repository part (cores/c128/app/lib), not in coreapp/cores; it holds its own copies of hk_defaults.h and drive_rom_pick.h
INC_C128=(-DTEST_APP_C128=1 -I"$TESTS_C128" -DGOLDEN_DIR=\"$TESTS_C128/golden\" -I"$C128LIB" -I"$T/c128tab/drv")
for i in "${INC[@]}"; do case "$i" in "-I$T/drv"|-DGOLDEN_DIR=*) ;; *) INC_C128+=("$i") ;; esac; done
c128app() { local INC=("${INC_C128[@]}") CORE_TESTS="$TESTS_C128" CA_APP=c128 APPLIB="$C128LIB"; "$@"; }
# T239.5 D4, D16: the C128's units, built as its app (c128app: its library cores/c128/app/lib with its own copies of hk_defaults.h and drive_rom_pick.h, its own table with its own drive window, the group c128) -
# the units its TOML B (cores/c128/app/sdk.toml) names for the steps 2, 8 and 9 of its lane. The sources are the app's own: c128.cc, the PRG library (cbm_run.cc, cbm_prg.cc), Freeze, the drive library and the base's.
C128S="c128.cc cbm_run.cc cbm_prg.cc freeze.cc $DRV $BASEL"
C128X="$C128S menuctl.cc picklist.cc filemgr.cc menukeytab.cc menuflows.cc menuvlist.cc lineedit.cc menutree.cc libfiles.cc"   # T185 D4: the bench also drives the file selector of the settings rows
C128A="menuctl.cc picklist.cc filemgr.cc menukeytab.cc menuflows.cc menuvlist.cc lineedit.cc menutree.cc $C128S"
C128D="desc_check.cc overlay.cc descrec.cc $C128A"
C128M="test_media_mounts_engines.cc $C128D start.cc handover.cc"
# T113 5.5 (the Commodore 128 on the generic application): the descriptor against a model of register page E and the DDR2 window; the extra keys are pure
c128app run c128 test_c128.cc $C128X
# T194 S3: the C128's two pictures on the one layer (the VDC, and the VIC-IIe's capture in format 3): Integer / Fit / Fill, 1080p and 720p, 50 and 60 Hz family, Smoothing, the switch both ways
c128app run c128pic test_c128pic.cc $C128X
RARGS="$T/bundles/desc_own.bin" c128app run c128_desc test_desc.cc $C128D
RARGS="$BUN/app" c128app run c128_descgen test_descgen.cc compiled_table.cc libfiles.cc libmenu.cc bundle.cc mediumwait.cc libload.cc libscan.cc instfiles.cc instcore.cc start.cc handover.cc $C128D
c128app run c128_hotkeys test_hotkeys.cc $C128D
c128app run c128_ledcores test_ledcores.cc $C128D
c128app run c128_joyswap test_joyswap.cc $C128D
# T239.5 D13: every key of its menu against its golden (c128_keywalk: golden/keywalk_c128.txt, ids_c128.txt) and its declaration over the API (c128_uicover), TEST_APP_C128
c128app run c128_keywalk test_keywalk.cc libfiles.cc instfiles.cc instcore.cc $C128D
c128app run c128_uicover test_uicover.cc uiapp.cc libfiles.cc $SVC $C128D
c128app run c128_mounts $C128M
# the C128's rows of the host's ROM table: gen_romreq built as the C128's app, its rows in the host's table
t_romreq_c128() {
  local INC=("${INC_C128[@]}") CORE_TESTS="$TESTS_C128" APPLIB="$C128LIB"
  build "$T/gen_romreq_c128" "$TESTS_C128/gen_romreq.cc" $C128D
  "$T/gen_romreq_c128" > "$T/romreq_c128.inc"
  python3 -I - "$T/romreq_c128.inc" "$HA/romreq_table.inc" <<'PY' || { echo "romreq_table.inc is stale: gen_romreq's rows of the C128's app are not (in this order) in host-app/software/hostapp/romreq_table.inc" >&2; exit 1; }
import sys
own = [l for l in open(sys.argv[1]) if not l.startswith("//")]
host = iter(l for l in open(sys.argv[2]) if not l.startswith("//"))
sys.exit(0 if all(any(h == r for h in host) for r in own) and own else 1)
PY
  echo "test c128_romreq: PASS"
  return 0
}
if ca_in c128 && ca_unit c128_romreq; then t_romreq_c128; PASS=$((PASS + 1)); fi
