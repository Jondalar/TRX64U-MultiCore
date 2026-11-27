# shellcheck shell=bash
# The units of pet (T239 D17), moved out of tests/coreapp/run.sh by tools/tests_to_core.py. tests/coreapp/run.sh sources this file at its end, with every variable
# and function of the shared harness set; the test programs, sources and headers of this folder win over the shared ones for this app (CORE_TESTS, find_src, tpath).
TESTS_PET="$ROOT/cores/pet/app/tests"
source "$TESTS_PET/pet_drv.sh"
pet_drv_dir "$T/pettab" || { echo "coreapp: the PET's driver table was not generated" >&2; exit 1; }   # pet_regs.h names the registers through it
PETLIB="$ROOT/cores/pet/app/lib"   # the PET's library lives in its own repository part (cores/pet/app/lib), not in coreapp/cores; it holds its own copies of tape_deck.*, tape_pulses.*  and hk_defaults.h
INC_PET=(-DTEST_APP_PET=1 -I"$TESTS_PET" -DGOLDEN_DIR=\"$TESTS_PET/golden\" -I"$PETLIB" -I"$T/pettab/drv")
for i in "${INC[@]}"; do case "$i" in "-I$T/drv"|-DGOLDEN_DIR=*) ;; *) INC_PET+=("$i") ;; esac; done
petapp() { local INC=("${INC_PET[@]}") CORE_TESTS="$TESTS_PET" CA_APP=pet APPLIB="$PETLIB"; "$@"; }
# T239.5 D4, D16: the PET's units, built as its app (petapp: its library cores/pet/app/lib with its own tape_deck.*, tape_pulses.* and hk_defaults.h, its own table, the group pet) - the units its
# TOML B (cores/pet/app/sdk.toml) names for the steps 2, 8 and 9 of its lane. The sources are the app's own: pet.cc, the tape deck, the PRG library (cbm_run.cc, cbm_prg.cc, c64_go.cc) and the base's.
PETS="pet.cc tape_pulses.cc tape_deck.cc cbm_run.cc cbm_prg.cc c64_go.cc $BASEL"
PETA="menuctl.cc picklist.cc filemgr.cc menukeytab.cc menuflows.cc menuvlist.cc lineedit.cc menutree.cc $PETS"
PETD="desc_check.cc overlay.cc descrec.cc $PETA"
petapp run pet_models test_pet_models.cc cbm_prg.cc
petapp run tape_pet test_tape.cc tape_pulses.cc
RARGS="$T/bundles/desc_own.bin" petapp run pet_desc test_desc.cc $PETD
RARGS="$BUN/app" petapp run pet_descgen test_descgen.cc compiled_table.cc libfiles.cc libmenu.cc bundle.cc mediumwait.cc libload.cc libscan.cc instfiles.cc instcore.cc start.cc handover.cc $PETD
petapp run pet_hotkeys test_hotkeys.cc $PETD
petapp run pet_cut test_pet_cut.cc $PETD
# T239.5 D13: every key of its menu against its golden (pet_keywalk: golden/keywalk_pet.txt, ids_pet.txt) and its declaration over the API (pet_uicover), TEST_APP_PET
petapp run pet_keywalk test_keywalk.cc libfiles.cc instfiles.cc instcore.cc $PETD
petapp run pet_uicover test_uicover.cc uiapp.cc libfiles.cc $SVC $PETD
# the PET's rows of the host's ROM table: gen_romreq built as the PET's app, its rows in the host's table
t_romreq_pet() {
  local INC=("${INC_PET[@]}") CORE_TESTS="$TESTS_PET" APPLIB="$PETLIB"
  build "$T/gen_romreq_pet" "$TESTS_PET/gen_romreq.cc" $PETD
  "$T/gen_romreq_pet" > "$T/romreq_pet.inc"
  python3 -I - "$T/romreq_pet.inc" "$HA/romreq_table.inc" <<'PY' || { echo "romreq_table.inc is stale: gen_romreq's rows of the PET's app are not (in this order) in host-app/software/hostapp/romreq_table.inc" >&2; exit 1; }
import sys
own = [l for l in open(sys.argv[1]) if not l.startswith("//")]
host = iter(l for l in open(sys.argv[2]) if not l.startswith("//"))
sys.exit(0 if all(any(h == r for h in host) for r in own) and own else 1)
PY
  echo "test pet_romreq: PASS"
  return 0
}
if ca_in pet && ca_unit pet_romreq; then t_romreq_pet; PASS=$((PASS + 1)); fi
