# shellcheck shell=bash
# The units of c64scpu (T239 D17), moved out of tests/coreapp/run.sh by tools/tests_to_core.py. tests/coreapp/run.sh sources this file at its end, with every variable
# and function of the shared harness set; the test programs, sources and headers of this folder win over the shared ones for this app (CORE_TESTS, find_src, tpath).
TESTS_C64SCPU="$ROOT/cores/c64scpu/app/tests"
source "$TESTS_C64SCPU/c64scpu_drv.sh"
c64scpu_drv_dir "$T/c64scputab" || { echo "coreapp: the C64SCPU's driver table was not generated" >&2; exit 1; }   # c64scpu_regs.h names the registers through it
C64SCPULIB="$ROOT/cores/c64scpu/app/lib"   # the C64SCPU's library lives in its own repository part (cores/c64scpu/app/lib), not in coreapp/cores; it holds its own copies of hk_defaults.h and drive_rom_pick.h
INC_C64SCPU=(-DTEST_APP_C64SCPU=1 -I"$TESTS_C64SCPU" -DGOLDEN_DIR=\"$TESTS_C64SCPU/golden\" -I"$C64SCPULIB" -I"$T/c64scputab/drv")
for i in "${INC[@]}"; do case "$i" in "-I$T/drv"|-DGOLDEN_DIR=*) ;; *) INC_C64SCPU+=("$i") ;; esac; done
c64scpuapp() { local INC=("${INC_C64SCPU[@]}") CORE_TESTS="$TESTS_C64SCPU" CA_APP=c64scpu APPLIB="$C64SCPULIB"; "$@"; }
# T239.3 D61: the C64SCPU's units, built as its app (c64scpuapp: its library cores/c64scpu/app/lib with its own copies of hk_defaults.h and drive_rom_pick.h, its own table with its own drive window, the group c64scpu) -
# the units its TOML B (cores/c64scpu/app/sdk.toml) names for the steps 2, 8, 9 and 10 of its lane. The sources are the app's own: c64scpu.cc, the PRG library (cbm_run.cc, cbm_prg.cc), Freeze, the drive library and the base's.
C64SCPUS="c64scpu.cc cbm_run.cc cbm_prg.cc freeze.cc $DRV $BASEL"
C64SCPUA="menuctl.cc picklist.cc filemgr.cc menukeytab.cc menuflows.cc menuvlist.cc lineedit.cc menutree.cc $C64SCPUS"
C64SCPUD="desc_check.cc overlay.cc descrec.cc $C64SCPUA"
c64scpuapp run c64scpu test_c64scpu.cc $C64SCPUS
c64scpuapp run c64scpu_video test_video.cc $C64SCPUS
c64scpuapp run c64scpu_picture test_picture.cc $PICSRC cbm_run.cc freeze.cc c64scpu.cc
RARGS="$T/bundles/desc_own.bin" c64scpuapp run c64scpu_desc test_desc.cc $C64SCPUD
RARGS="$BUN/app" c64scpuapp run c64scpu_descgen test_descgen.cc compiled_table.cc libfiles.cc libmenu.cc bundle.cc mediumwait.cc libload.cc libscan.cc instfiles.cc instcore.cc start.cc handover.cc $C64SCPUD
c64scpuapp run c64scpu_hotkeys test_hotkeys.cc $C64SCPUD
c64scpuapp run c64scpu_ledcores test_ledcores.cc $C64SCPUD
c64scpuapp run c64scpu_joyswap test_joyswap.cc $C64SCPUD
# T239.5 D13: every key of its menu against its golden (c64scpu_keywalk: golden/keywalk_c64scpu.txt, ids_c64scpu.txt) and its declaration over the API (c64scpu_uicover), TEST_APP_C64SCPU
c64scpuapp run c64scpu_keywalk test_keywalk.cc libfiles.cc instfiles.cc instcore.cc $C64SCPUD
c64scpuapp run c64scpu_uicover test_uicover.cc uiapp.cc libfiles.cc $SVC $C64SCPUD
# the C64SCPU's rows of the host's ROM table: gen_romreq built as the C64SCPU's app, its rows in the host's table
t_romreq_c64scpu() {
  local INC=("${INC_C64SCPU[@]}") CORE_TESTS="$TESTS_C64SCPU" APPLIB="$C64SCPULIB"
  build "$T/gen_romreq_c64scpu" "$TESTS_C64SCPU/gen_romreq.cc" $C64SCPUD
  "$T/gen_romreq_c64scpu" > "$T/romreq_c64scpu.inc"
  python3 -I - "$T/romreq_c64scpu.inc" "$HA/romreq_table.inc" <<'PY' || { echo "romreq_table.inc is stale: gen_romreq's rows of the C64SCPU's app are not (in this order) in host-app/software/hostapp/romreq_table.inc" >&2; exit 1; }
import sys
own = [l for l in open(sys.argv[1]) if not l.startswith("//")]
host = iter(l for l in open(sys.argv[2]) if not l.startswith("//"))
sys.exit(0 if all(any(h == r for h in host) for r in own) and own else 1)
PY
  echo "test c64scpu_romreq: PASS"
  return 0
}
if ca_in c64scpu && ca_unit c64scpu_romreq; then t_romreq_c64scpu; PASS=$((PASS + 1)); fi
