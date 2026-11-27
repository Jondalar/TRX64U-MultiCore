# shellcheck shell=bash
# The units of zxnext (T239 D17), moved out of tests/coreapp/run.sh by tools/tests_to_core.py. tests/coreapp/run.sh sources this file at its end, with every variable
# and function of the shared harness set; the test programs, sources and headers of this folder win over the shared ones for this app (CORE_TESTS, find_src, tpath).
TESTS_ZXNEXT="$ROOT/cores/zxnext/app/tests"
source "$TESTS_ZXNEXT/zxnext_drv.sh"
zxnext_drv_dir "$T/zxnexttab" || { echo "coreapp: the ZX Spectrum Next's driver table was not generated" >&2; exit 1; }   # zxnext_regs.h names the registers through it
ZXNLIB="$ROOT/cores/zxnext/app/lib"   # T239.5 D15: the ZX Spectrum Next's library lives in its own repository part (cores/zxnext/app/lib), not in coreapp/cores
# The ZX Spectrum Next is its own app as well (T239.5 D15: cores/zxnext/app). zxnextapp CMD... builds like c264app, with the Next's library, its table and TEST_APP_ZXNEXT: its units are in the group zxnext.
INC_ZXNEXT=(-DTEST_APP_ZXNEXT=1 -I"$TESTS_ZXNEXT" -DGOLDEN_DIR=\"$TESTS_ZXNEXT/golden\" -I"$ZXNLIB" -I"$T/zxnexttab/drv")
for i in "${INC[@]}"; do case "$i" in "-I$T/drv"|-DGOLDEN_DIR=*) ;; *) INC_ZXNEXT+=("$i") ;; esac; done
zxnextapp() { local INC=("${INC_ZXNEXT[@]}") CORE_TESTS="$TESTS_ZXNEXT" CA_APP=zxnext APPLIB="$ZXNLIB"; "$@"; }
ZXN="zxnext.cc $BASEL"
zxnextapp run zxnext test_zxnext.cc $ZXN
# T239.5 D15: the ZX Spectrum Next's units built as its app (zxnextapp: its library cores/zxnext/app/lib and its own table, the group zxnext) that are not above (zxnext) - the units its TOML B
# (cores/zxnext/app/sdk.toml) names for the steps 2 and 8 to 10 of its lane. The sources are the app's own: zxnext.cc and the base's (no other core). The Next has no ROM: no romreq unit.
# T239.5 D13: the Next's cases of the shared tests run here (TEST_APP_ZXNEXT): zxnext_desc (the descriptor checks, its golden menu, the places of its tasks), zxnext_hotkeys (S5, its default map),
# zxnext_keywalk (every key of its menu: golden/keywalk_zxnext.txt, ids_zxnext.txt), zxnext_uicover (its declaration over the API), zxnext_joyswap and zxnext_memops; the shared units no longer link it.
ZXND="desc_check.cc overlay.cc descrec.cc zxnext.cc $BASEL"
ZXNM="menuctl.cc picklist.cc filemgr.cc menukeytab.cc menuflows.cc menuvlist.cc lineedit.cc menutree.cc libfiles.cc"
RARGS="$T/bundles/desc_own.bin" zxnextapp run zxnext_desc test_desc.cc $ZXND
zxnextapp run zxnext_hotkeys test_hotkeys.cc $ZXND
zxnextapp run zxnext_keywalk test_keywalk.cc $ZXNM instfiles.cc instcore.cc $ZXND
zxnextapp run zxnext_uicover test_uicover.cc uiapp.cc $ZXNM $SVC $ZXND
zxnextapp run zxnext_joyswap test_joyswap.cc $ZXND
zxnextapp run zxnext_menuctl test_zxnext_menuctl.cc menuctl.cc picklist.cc filemgr.cc menukeytab.cc menuflows.cc menuvlist.cc lineedit.cc menutree.cc libfiles.cc $ZXND
zxnextapp run zxnext_memops test_memops.cc $SVC $ZXND
RARGS="$BUN/app" zxnextapp run zxnext_descgen test_descgen.cc compiled_table.cc libfiles.cc libmenu.cc bundle.cc mediumwait.cc libload.cc libscan.cc instfiles.cc instcore.cc start.cc handover.cc menuctl.cc picklist.cc filemgr.cc menukeytab.cc menuflows.cc menuvlist.cc lineedit.cc menutree.cc freeze.cc $ZXND
