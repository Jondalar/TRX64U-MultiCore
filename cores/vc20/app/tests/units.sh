# shellcheck shell=bash
# The units of vc20 (T239 D17), moved out of tests/coreapp/run.sh by tools/tests_to_core.py. tests/coreapp/run.sh sources this file at its end, with every variable
# and function of the shared harness set; the test programs, sources and headers of this folder win over the shared ones for this app (CORE_TESTS, find_src, tpath).
TESTS_VC20="$ROOT/cores/vc20/app/tests"
vc20_drv_dir "$T/vc20tab" || { echo "coreapp: the VIC-20's driver table was not generated" >&2; exit 1; }   # vc20_regs.h names the registers through it; not $T/vc20, the unit vc20 is built there
# The VIC-20 is its own app (T239.3 V-4: cores/vc20/app, linked from the SDK package with its own table, no shared table, no other core): its units are built like that app.
# vc20app CMD...: runs `run` or `control` with the VIC-20's include path (its library, its plain table from its TOML A and the base blocks of its TOML B, no table of the shared app)
# and with TEST_APP_VC20 defined, so a test file that serves both apps takes the VIC-20's rows only in these builds and the shared app's rows only in the others.
INC_VC20=(-DTEST_APP_VC20=1 -I"$TESTS_VC20" -DGOLDEN_DIR=\"$TESTS_VC20/golden\" -I"$VC20LIB" -I"$T/vc20tab/drv")
for i in "${INC[@]}"; do case "$i" in "-I$T/drv"|-DGOLDEN_DIR=*) ;; *) INC_VC20+=("$i") ;; esac; done
vc20app() { local INC=("${INC_VC20[@]}") CORE_TESTS="$TESTS_VC20" CA_APP=vc20; "$@"; }
# T83 (the VIC-20 on the generic application): the descriptor against a model of the register block and the ROM aperture; the PRG job (cbm_run.cc) is shared with the 264
VC20="vc20.cc cbm_run.cc cbm_prg.cc freeze.cc menuctl.cc picklist.cc filemgr.cc menukeytab.cc menuflows.cc menuvlist.cc lineedit.cc menutree.cc $DRV $BASEL"
vc20app run vc20 test_vc20.cc $VC20
# the drive library of the Commodore cores: written back before eject and replacement, a track the file did not take is written again, an unlinked disk stops nothing
vc20app control drv_mount_settle d1541_drive.cc '/T180 D1, D4: what the head wrote/s/settle();/(void)0;/' test_cbm1541.cc $VC20
vc20app control drv_eject_settle d1541_drive.cc '/T180 D4: written back (again) before the gate asks/s/settle();/wait_for_writeback();/' test_cbm1541.cc $VC20
vc20app control drv_failed_forgotten d1541_drive.cc 's/            failed_tr_ |= 1ull << tr;/            (void)tr;/' test_cbm1541.cc $VC20
vc20app control drv_unlinked_stops d1541_drive.cc 's/state_ != DS_NONE \&\& state_ != DS_CLOSED \&\& unsaved_/state_ != DS_NONE \&\& unsaved_/' test_cbm1541.cc $VC20
vc20app control drv_unsaved_ignored d1541_drive.cc 's/ \&\& !(state_ != DS_NONE \&\& state_ != DS_CLOSED \&\& unsaved_);/;/' test_cbm1541.cc $VC20
vc20app control drv_hook_engine engines.cc 's/if (d \&\& d->media_flush \&\& d->media_flush(c, m, sizeof(m)) \&\& !bad++) {/if (false) {/' test_cbm1541.cc $VC20
vc20app run cbm1541 test_cbm1541.cc $VC20
# The drive library's 1581 tests (d1541_wd.cc, the block drive_wd) run on the VIC-20's descriptor against a table that is the VIC-20's plus drive_wd (vc20_drv.sh, merge_a as the app's link does it).
# No shipped app has that table: this is a test of the library, not of an app; the 1581 is tested in the apps of the cores with one (their own units, cores/<id>/app/tests) and in the C128's app against its own table by the unit c128 (a .d81 in drive A).
t_cbm1541_wd() {
  local INC_VC20=(-I"$T/vc20wd/drv" "${INC_VC20[@]}")
  vc20app run cbm1541_wd test_cbm1541.cc $VC20
}
t_cbm1541_wd
# T180 #38: disk images with a FAT long name picked into the drive: the DiskFs finds the image of a long path, the drive keeps the whole name and path. Controls: the container path
# buffer at 260, the name at 56, the mount path at 260.
# T104 5.14 Reload core: the label, the check before the cut, the order of the sequence, what the new core is told again
vc20app control drv_busy d1541_drive.cc 's/if ((r(DR_STATUS) \& DR_STAT_WRITEBUSY) \&\& drive_track == tr) {/if (false) {/' test_cbm1541.cc $VC20
vc20app control drv_flush d1541_drive.cc 's/^    wait_for_writeback();/    (void)0;/' test_cbm1541.cc $VC20
vc20app control drv_discard d1541_drive.cc 's/    if (discard_armed_) {/    if (true) {/' test_cbm1541.cc $VC20
vc20app control drv_unsaved d1541_drive.cc 's/if (any \&\& !ok) {/if (false) {/' test_cbm1541.cc $VC20
vc20app control drv_exists newfile.cc 's/^        if (fs->stat(full, \&sz, \&isdir)) {/        if (false) {/' test_cbm1541.cc $VC20
vc20app control drv_bare newfile.cc 's/^            if (fs->stat(full, \&sz, \&isdir)) {/            if (false) {/' test_cbm1541.cc $VC20
# T239.5 D4, D10: the VIC-20's cases of the shared tests (TEST_APP_VC20), built as its app (vc20app: its own table and its core only, the group vc20) - the units its
# TOML B (cores/vc20/app/sdk.toml) names for the steps 8 to 10 of its lane. The sources are the shared units' generic ones with the VIC-20 in place of the shared cores.
VC20D="desc_check.cc overlay.cc descrec.cc $VC20"
RARGS="$T/bundles/desc_own.bin" vc20app run vc20_desc test_desc.cc $VC20D
vc20app run vc20_ledcores test_ledcores.cc $VC20D
vc20app run vc20_hotkeys test_hotkeys.cc $VC20D
vc20app run vc20_createflow test_createflow.cc $VC20D
vc20app run vc20_mounts test_media_mounts_engines.cc $VC20D start.cc handover.cc
RARGS="$BUN/app" vc20app run vc20_descgen test_descgen.cc compiled_table.cc libfiles.cc libmenu.cc bundle.cc mediumwait.cc libload.cc libscan.cc instfiles.cc instcore.cc start.cc handover.cc $VC20D
vc20app run vc20_memops test_memops.cc $SVC $VC20D
# the VIC-20's rows of the host's ROM table (t_romreq above checks the shared app's rows only): gen_romreq built as the VIC-20's app, each of its rows in the host's table
t_romreq_vc20() {
  local INC=("${INC_VC20[@]}") CORE_TESTS="$TESTS_VC20"
  build "$T/gen_romreq_vc20" "$TESTS_VC20/gen_romreq.cc" $VC20D
  "$T/gen_romreq_vc20" > "$T/romreq_vc20.inc"
  python3 -I - "$T/romreq_vc20.inc" "$HA/romreq_table.inc" <<'PY' || { echo "romreq_table.inc is stale: gen_romreq's rows of the VIC-20's app are not (in this order) in host-app/software/hostapp/romreq_table.inc" >&2; exit 1; }
import sys
own = [l for l in open(sys.argv[1]) if not l.startswith("//")]
host = iter(l for l in open(sys.argv[2]) if not l.startswith("//"))
sys.exit(0 if all(any(h == r for h in host) for r in own) and own else 1)
PY
  echo "test vc20_romreq: PASS"
  return 0
}
if ca_in vc20 && ca_unit vc20_romreq; then t_romreq_vc20; PASS=$((PASS + 1)); fi
