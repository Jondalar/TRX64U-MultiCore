# shellcheck shell=bash
# The units of ddt_novic (T239 D17), moved out of tests/coreapp/run.sh by tools/tests_to_core.py. tests/coreapp/run.sh sources this file at its end, with every variable
# and function of the shared harness set; the test programs, sources and headers of this folder win over the shared ones for this app (CORE_TESTS, find_src, tpath).
TESTS_DDT="$ROOT/cores/ddt_novic/app/tests"
source "$TESTS_DDT/ddt_drv.sh"
ddt_drv_dir "$T/ddttab" || { echo "coreapp: DDT Look Ma, No VIC's driver table was not generated" >&2; exit 1; }   # ddt_regs.h names the registers through it
DDTLIB="$ROOT/cores/ddt_novic/app/lib"   # the DDT core's library lives in its own repository part (cores/ddt_novic/app/lib), not in coreapp/cores
# DDT Look Ma, No VIC is its own app too (cores/ddt_novic/app): ddtapp CMD... builds like vc20app, with its library, its table and TEST_APP_DDT.
INC_DDT=(-DTEST_APP_DDT=1 -I"$TESTS_DDT" -DGOLDEN_DIR=\"$TESTS_DDT/golden\" -I"$DDTLIB" -I"$T/ddttab/drv")
for i in "${INC[@]}"; do case "$i" in "-I$T/drv"|-DGOLDEN_DIR=*) ;; *) INC_DDT+=("$i") ;; esac; done
ddtapp() { local INC=("${INC_DDT[@]}") CORE_TESTS="$TESTS_DDT" CA_APP=ddt_novic APPLIB="$DDTLIB"; "$@"; }
# T239.5 D4, D16: DDT Look Ma, No VIC's units, built as its app (ddtapp: its library cores/ddt_novic/app/lib and its own table, the group ddt_novic) - the units its TOML B
# (cores/ddt_novic/app/sdk.toml) names for the steps 2 and 8 to 10 of its lane. The sources are the app's own: ddt_novic.cc and the base's (no other core).
DDT="ddt_novic.cc menuctl.cc picklist.cc filemgr.cc menukeytab.cc menuflows.cc menuvlist.cc lineedit.cc menutree.cc $BASEL"
DDTD="desc_check.cc overlay.cc descrec.cc $DDT"
ddtapp run ddt test_ddt.cc $DDT
ddtapp run ddt_memops test_memops.cc $SVC $DDTD
RARGS="$T/bundles/desc_own.bin" ddtapp run ddt_desc test_desc.cc $DDTD
RARGS="$BUN/app" ddtapp run ddt_descgen test_descgen.cc compiled_table.cc libfiles.cc libmenu.cc bundle.cc mediumwait.cc libload.cc libscan.cc instfiles.cc instcore.cc start.cc handover.cc $DDTD
ddtapp run ddt_hotkeys test_hotkeys.cc $DDTD
# T239.5 D13: every key of its menu against its golden (ddt_keywalk: golden/keywalk_ddt_novic.txt, ids_ddt_novic.txt) and its declaration over the API (ddt_uicover), TEST_APP_DDT
ddtapp run ddt_keywalk test_keywalk.cc libfiles.cc instfiles.cc instcore.cc $DDTD
ddtapp run ddt_uicover test_uicover.cc uiapp.cc libfiles.cc $SVC $DDTD
