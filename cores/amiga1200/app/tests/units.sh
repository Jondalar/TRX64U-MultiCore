# shellcheck shell=bash
# The units of amiga1200 (T239 D17), moved out of tests/coreapp/run.sh by tools/tests_to_core.py. tests/coreapp/run.sh sources this file at its end, with every variable
# and function of the shared harness set; the test programs, sources and headers of this folder win over the shared ones for this app (CORE_TESTS, find_src, tpath).
TESTS_A1200="$ROOT/cores/amiga1200/app/tests"
source "$TESTS_A1200/a1200_drv.sh"
A1200LIB="$ROOT/cores/amiga1200/app/lib"   # T213.3 P1: the Amiga 1200's library lives in its own repository part (cores/amiga1200/app/lib)
a1200_drv_dir "$T/a12tab" || { echo "coreapp: the Amiga 1200's driver table was not generated" >&2; exit 1; }   # amiga1200_regs.h names the registers through it
# The Amiga 1200 is its own app as well (T213.3: cores/amiga1200/app). a12app CMD... builds like vc20app, with the A1200's library, its table and TEST_APP_A1200, and with a shim amiga_regs.h
# of this folder, so the files that serve both Amiga apps get the A1200's registers (the A500's header is on amigaapp's path only); amiga.cc, the A500's library, is not in these builds (amiga1200.cc is).
INC_A1200=(-DTEST_APP_A1200=1 -I"$TESTS_A1200" -DGOLDEN_DIR=\"$TESTS_A1200/golden\" -I"$A1200LIB" -I"$T/a12tab/drv")
for i in "${INC[@]}"; do case "$i" in "-I$T/drv"|-DGOLDEN_DIR=*) ;; *) INC_A1200+=("$i") ;; esac; done
a12app() { local INC=("${INC_A1200[@]}") CORE_TESTS="$TESTS_A1200" CA_APP=amiga1200 APPLIB="$A1200LIB"; "$@"; }
# T213.3 P3, T239.5 D4: the Amiga 1200's units, built as its app (a12app: its library cores/amiga1200/app/lib and its own table, the group amiga1200) - the units its TOML B
# (cores/amiga1200/app/sdk.toml) names for the steps 2 and 8 to 10 of its lane. The sources are the app's own: amiga1200.cc and the base's (no amiga.cc, no other core).
A12="amiga1200.cc menuctl.cc picklist.cc filemgr.cc menukeytab.cc menuflows.cc menuvlist.cc lineedit.cc menutree.cc $BASEL"
A12D="desc_check.cc overlay.cc descrec.cc $A12"
a12app run a12 test_amiga500.cc $A12
a12app run a12_memops test_memops.cc $SVC $A12D
RARGS="$T/bundles/desc_own.bin" a12app run a12_desc test_desc.cc $A12D
RARGS="$BUN/app" a12app run a12_descgen test_descgen.cc compiled_table.cc libfiles.cc libmenu.cc bundle.cc mediumwait.cc libload.cc libscan.cc instfiles.cc instcore.cc start.cc handover.cc $A12D
a12app run a12_ledcores test_ledcores.cc $A12D
a12app run a12_hotkeys test_hotkeys.cc $A12D
a12app run a12_createflow test_createflow.cc $A12D
# the Amiga 1200's row of the host's ROM table: gen_romreq built as the A1200's app, its rows in the host's table
t_romreq_a12() {
  local INC=("${INC_A1200[@]}") CORE_TESTS="$TESTS_A1200" APPLIB="$A1200LIB"
  build "$T/gen_romreq_a12" "$TESTS_A1200/gen_romreq.cc" $A12D
  "$T/gen_romreq_a12" > "$T/romreq_a12.inc"
  python3 -I - "$T/romreq_a12.inc" "$HA/romreq_table.inc" <<'PY' || { echo "romreq_table.inc is stale: gen_romreq's rows of the Amiga 1200's app are not (in this order) in host-app/software/hostapp/romreq_table.inc" >&2; exit 1; }
import sys
own = [l for l in open(sys.argv[1]) if not l.startswith("//")]
host = iter(l for l in open(sys.argv[2]) if not l.startswith("//"))
sys.exit(0 if all(any(h == r for h in host) for r in own) and own else 1)
PY
  echo "test a12_romreq: PASS"
  return 0
}
if ca_in amiga1200 && ca_unit a12_romreq; then t_romreq_a12; PASS=$((PASS + 1)); fi
a12app run a12_folderhd test_folderhd.cc $A12
a12app run a12_mouselib test_mouselib.cc $A12
a12app run a12_base test_a12base.cc $A12
# the two records as both arguments by value: a prefix assignment that reads another one of the same command (RARGS="$CARGS") took the global, empty CARGS in a run
# of the whole group (bash 3.2, traced), so the test started without its records
# its own copy of the tables of the sections 7 .. 16 (golden/desc_sections.toml of this folder, the A1200's values), compiled by the real tool as gen_bundle.py compiles the shared one
python3 -I -c 'import sys; sys.path.insert(0, sys.argv[1] + "/tools"); import descrec; open(sys.argv[3], "wb").write(descrec.compile_text(open(sys.argv[2]).read()))' "$ROOT" "$TESTS_A1200/golden/desc_sections.toml" "$T/a12_desc_sections.bin"
A12SEC="$T/a12_desc_sections.bin $BUN/desc_sections_extra.bin"
CARGS="$A12SEC" RARGS="$A12SEC" a12app run a12_descsec test_descsec.cc overlay.cc descrec.cc $A12
a12app run a12_romset test_romset.cc $A12
a12app run a12_regset test_regset.cc $A12
a12app run a12_joyswap test_joyswap.cc $A12
a12app run a12_mousescale test_mousescale.cc $A12
a12app run a12_fbwin test_fbwin.cc $A12
a12app run a12_scale test_scale.cc desc_check.cc $A12   # T213.3: the picture items and their apply on the A1200's record (the A500's app runs them on its record: amiga_scale)
a12app run a12_rawkeys test_rawkeys.cc $A12
a12app run a12_ledload test_ledload.cc $A12
