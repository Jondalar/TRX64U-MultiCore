# shellcheck shell=bash
# The units of c264 (T239 D17), moved out of tests/coreapp/run.sh by tools/tests_to_core.py. tests/coreapp/run.sh sources this file at its end, with every variable
# and function of the shared harness set; the test programs, sources and headers of this folder win over the shared ones for this app (CORE_TESTS, find_src, tpath).
TESTS_C264="$ROOT/cores/c264/app/tests"
source "$TESTS_C264/c264_drv.sh"
c264_drv_dir "$T/c264tab" || { echo "coreapp: the Commodore 264's driver table was not generated" >&2; exit 1; }   # c264_regs.h names the registers through it
C264LIB="$ROOT/cores/c264/app/lib"   # T239.5 D15: the Commodore 264's library lives in its own repository part (cores/c264/app/lib), not in coreapp/cores
# The Commodore 264 is its own app as well (T239.5 D15: cores/c264/app). c264app CMD... builds like nesapp, with the 264's library, its table and TEST_APP_C264: its units are in the group c264.
# APPLIB puts its library first in the source lookup (no shared unit finds c264.cc: T239.5 D13, the 264's cases of the shared tests are its units, at the end of this file).
INC_C264=(-DTEST_APP_C264=1 -I"$TESTS_C264" -DGOLDEN_DIR=\"$TESTS_C264/golden\" -I"$C264LIB" -I"$T/c264tab/drv")
for i in "${INC[@]}"; do case "$i" in "-I$T/drv"|-DGOLDEN_DIR=*) ;; *) INC_C264+=("$i") ;; esac; done
c264app() { local INC=("${INC_C264[@]}") CORE_TESTS="$TESTS_C264" CA_APP=c264 APPLIB="$C264LIB"; "$@"; }
# T90 (the Commodore 264 on the generic application): the descriptor against a model of the register block and the 16 KB aperture
C264="c264.cc cbm_run.cc cbm_prg.cc freeze.cc $DRV $BASEL"
c264app run c264 test_c264.cc $C264
# T239.5 D15: the Commodore 264's units built as its app (c264app: its library cores/c264/app/lib and its own table, the group c264) that are not above (c264) - the units its TOML B
# (cores/c264/app/sdk.toml) names for the steps 2 and 8 to 10 of its lane. The sources are the app's own: c264.cc with the PRG job, the drive library and the base's (no other core).
C264D="desc_check.cc overlay.cc descrec.cc $C264 menuctl.cc picklist.cc filemgr.cc menukeytab.cc menuflows.cc menuvlist.cc lineedit.cc menutree.cc"
c264app run c264_memops test_memops.cc $SVC $C264D
c264app run c264_ledcores test_ledcores.cc $C264D
C264M="test_media_mounts_engines.cc $C264D start.cc handover.cc"
c264app run c264_mounts $C264M
# T180 D2/D5 on the 264 (the controls of the shared unit media_mounts_engines that stood on its profile, T239.5 D13): the drive library's mount state (mount, eject, an eject of the empty
# drive, the old disk out on a failed replacement, the mode word, the hook) and the 264's cartridge image (told by its slot, the restored cartridge sets the Plus/4 model)
c264app control mme_drive_mount d1541_drive.cc 's/^    changed(); *\/\/ T180 D2$/    (void)0;/' $C264M
c264app control mme_drive_eject d1541_drive.cc 's/^    changed(); *\/\/ T180 D2: the empty drive is stored$/    (void)0;/' $C264M
c264app control mme_drive_eject_empty d1541_drive.cc 's/^        changed(); *\/\/ T180 D2: an eject of the empty.*$/        (void)0;/' $C264M
c264app control mme_drive_modes cbm1541.cc 's/m == MM_RO ? "ro " : //' $C264M
c264app control mme_drive_hook cbm1541.cc 's/    d->on_change(drive_changed, c);/    (void)drive_changed;/' $C264M
c264app control mme_c264_cart c264.cc 's/^    mount_notify(c, 1); *\/\/ T180 D2: the slots.*$/    (void)0;/' $C264M
c264app control mme_c264_model c264.cc 's/        c->settings->set(mi, 1); *\/\/ Plus\/4/        (void)mi;/' $C264M
c264app control mme_drive_fail d1541_drive.cc 's/^        changed(); *\/\/ T180 D2: the old disk is out and the drive is empty$/        (void)0;/' $C264M
# T239.5 D13: the 264's cases of the shared tests test_desc, test_hotkeys, test_joyswap, test_keywalk and test_uicover (TEST_APP_C264): the walk of its descriptor, the machine line, the
# Run Disk names, the default hotkey map, its own joystick key, every key of its menu (its own goldens keywalk_c264.txt, ids_c264.txt) and the reach of its declaration over the API
RARGS="$T/bundles/desc_own.bin" c264app run c264_desc test_desc.cc $C264D
c264app run c264_hotkeys test_hotkeys.cc $C264D
c264app run c264_joyswap test_joyswap.cc $C264D
RARGS="$BUN/desc_cut.bin" c264app run c264_keywalk test_keywalk.cc libfiles.cc instfiles.cc instcore.cc $C264D
c264app run c264_uicover test_uicover.cc uiapp.cc libfiles.cc $SVC $C264D
RARGS="$BUN/app" c264app run c264_descgen test_descgen.cc compiled_table.cc libfiles.cc libmenu.cc bundle.cc mediumwait.cc libload.cc libscan.cc instfiles.cc instcore.cc start.cc handover.cc $C264D
# the Commodore 264's rows of the host's ROM table: gen_romreq built as the 264's app, its rows in the host's table
t_romreq_c264() {
  local INC=("${INC_C264[@]}") CORE_TESTS="$TESTS_C264" APPLIB="$C264LIB"
  build "$T/gen_romreq_c264" "$TESTS_C264/gen_romreq.cc" $C264D
  "$T/gen_romreq_c264" > "$T/romreq_c264.inc"
  python3 -I - "$T/romreq_c264.inc" "$HA/romreq_table.inc" <<'PY' || { echo "romreq_table.inc is stale: gen_romreq's rows of the Commodore 264's app are not (in this order) in host-app/software/hostapp/romreq_table.inc" >&2; exit 1; }
import sys
own = [l for l in open(sys.argv[1]) if not l.startswith("//")]
host = iter(l for l in open(sys.argv[2]) if not l.startswith("//"))
sys.exit(0 if all(any(h == r for h in host) for r in own) and own else 1)
PY
  echo "test c264_romreq: PASS"
  return 0
}
if ca_in c264 && ca_unit c264_romreq; then t_romreq_c264; PASS=$((PASS + 1)); fi
