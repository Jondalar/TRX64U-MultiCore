# shellcheck shell=bash
# The units of diag (T239 D17), moved out of tests/coreapp/run.sh by tools/tests_to_core.py. tests/coreapp/run.sh sources this file at its end, with every variable
# and function of the shared harness set; the test programs, sources and headers of this folder win over the shared ones for this app (CORE_TESTS, find_src, tpath).
TESTS_DIAG="$ROOT/cores/diag/app/tests"
source "$TESTS_DIAG/diag_drv.sh"
diag_drv_dir "$T/diagtab" || { echo "coreapp: the diag core's driver table was not generated" >&2; exit 1; }   # portdiag_regs.h names the registers through it
DIAGLIB="$ROOT/cores/diag/app/lib"   # T260 P7: the diag core's library lives in its own app (cores/diag/app/lib)
# The diag core is its own app too (T260 P7: cores/diag/app). diagapp CMD... builds like a8app, with the diag core's library (APPLIB), its table and TEST_APP_DIAG: its units are in the group diag.
INC_DIAG=(-DTEST_APP_DIAG=1 -I"$TESTS_DIAG" -DGOLDEN_DIR=\"$TESTS_DIAG/golden\" -I"$DIAGLIB" -I"$T/diagtab/drv")
for i in "${INC[@]}"; do case "$i" in "-I$T/drv"|-DGOLDEN_DIR=*) ;; *) INC_DIAG+=("$i") ;; esac; done
diagapp() { local INC=("${INC_DIAG[@]}") CORE_TESTS="$TESTS_DIAG" CA_APP=diag APPLIB="$DIAGLIB"; "$@"; }
# T175, T260: the diag core's app (cores/diag/app/lib), built as that app (diagapp). The shipped tests and presets are the delivery's folder cores/diag/sd (T260 D6; cores/diag/app/tests/diag_examples.h
# copies it onto a fake card), no record of the .cor. The units are the ones its TOML B names (tests = ...).
diagapp run diagtest test_diagtest.cc diagtest.cc dtv2.cc panel.cc pdnames.cc instfiles.cc
diagapp control diagtest_halffile diagtest.cc 's/        if (env->fs->remove(a)) {  /        if (true) {  /' test_diagtest.cc diagtest.cc dtv2.cc panel.cc pdnames.cc instfiles.cc
diagapp run portdiag test_portdiag.cc portdiag.cc panel.cc pdnames.cc xinput.cc diagtest.cc dtv2.cc instfiles.cc instcore.cc $BASEL
diagapp run diag_avtrace test_diag_avtrace.cc portdiag.cc panel.cc pdnames.cc xinput.cc diagtest.cc dtv2.cc instfiles.cc instcore.cc $BASEL
diagapp run dtv2 test_dtv2.cc dtv2.cc
diagapp run panel test_panel.cc portdiag.cc panel.cc pdnames.cc xinput.cc diagtest.cc dtv2.cc instfiles.cc instcore.cc $BASEL
# T239.5 D13, T260 P7: the diag core's cases of the shared tests, built as its app (diagapp: its library cores/diag/app/lib and its own table, the group diag) - the units its
# TOML B (cores/diag/app/sdk.toml) names besides the runner's (diagtest, portdiag, diag_avtrace, panel, dtv2 above). The sources are the app's own and the base's (no other core).
DIAG="portdiag.cc panel.cc pdnames.cc xinput.cc diagtest.cc dtv2.cc menuctl.cc picklist.cc filemgr.cc menukeytab.cc menuflows.cc menuvlist.cc lineedit.cc menutree.cc $BASEL"
DIAGD="desc_check.cc overlay.cc descrec.cc $DIAG"
diagapp run diag_memops test_memops.cc $SVC $DIAGD
RARGS="$T/bundles/desc_own.bin" diagapp run diag_desc test_desc.cc $DIAGD
RARGS="$BUN/app" diagapp run diag_descgen test_descgen.cc compiled_table.cc libfiles.cc libmenu.cc bundle.cc mediumwait.cc libload.cc libscan.cc instfiles.cc instcore.cc start.cc handover.cc $DIAGD
diagapp run diag_hotkeys test_hotkeys.cc $DIAGD
# every key of its menu against its golden (golden/keywalk_diag.txt, ids_diag.txt) and its declaration over the API (diag_uicover), TEST_APP_DIAG
diagapp run diag_keywalk test_keywalk.cc libfiles.cc instfiles.cc instcore.cc $DIAGD
diagapp run diag_uicover test_uicover.cc uiapp.cc libfiles.cc $SVC $DIAGD
