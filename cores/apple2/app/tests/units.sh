# shellcheck shell=bash
# The units of apple2 (T239 D17), moved out of tests/coreapp/run.sh by tools/tests_to_core.py. tests/coreapp/run.sh sources this file at its end, with every variable
# and function of the shared harness set; the test programs, sources and headers of this folder win over the shared ones for this app (CORE_TESTS, find_src, tpath).
TESTS_A2="$ROOT/cores/apple2/app/tests"
own_drv_dir apple2 "$T/a2tab" || { echo "coreapp: the Apple IIe's driver table was not generated" >&2; exit 1; }   # apple2_regs.h names the registers through it
A2LIB="$ROOT/cores/apple2/app/lib"   # T239.3 D61: the Apple IIe's library lives in its own repository part (cores/apple2/app/lib), not in coreapp/cores
# The Apple IIe is its own app as well (T239.3 D61: cores/apple2/app). apple2app CMD... builds like a8app, with the Apple's library (APPLIB: first in the lookup, copied over the controls' tree), its table and TEST_APP_APPLE2: its units are in the group apple2.
INC_A2=(-DTEST_APP_APPLE2=1 -I"$TESTS_A2" -I"$A2LIB" -I"$T/a2tab/drv")
for i in "${INC[@]}"; do [ "$i" = "-I$T/drv" ] || INC_A2+=("$i"); done
apple2app() { local INC=("${INC_A2[@]}") CORE_TESTS="$TESTS_A2" CA_APP=apple2 APPLIB="$A2LIB"; "$@"; }
# T239.3 D61: the Apple IIe's units built as its app (apple2app: its library cores/apple2/app/lib and its own table, the group apple2) - the units its TOML B (cores/apple2/app/sdk.toml) names for the steps 2 and 8 to 10
# of its lane. The sources are the app's own: apple2.cc with its disk, hard disk, nibble and WOZ code and the base's (no other core). The Apple IIe was the shared units' sample core: a generic case that stood on its
# descriptor is its case.
# T89 Apple //e disk: 6-and-2 nibblizer (golden from the RWTS definition + an independent POSTNIB decoder), file orders, the track window service; the eject and the replacement stop on a flush that fails, the sync is
# not forgotten, a track the file did not take is kept and written again
A2D="apple2_disk.cc apple2_nib.cc apple2_woz.cc apple2_hdd.cc"
apple2app run apple2_disk test_apple2_disk.cc $A2D
apple2app control a2d_no_flush apple2_disk.cc '/T180 D4: written back first/s/if (a2_flush(c, drive, msg, msglen))/if (false)/' test_apple2_disk.cc $A2D
apple2app control a2d_flush_ignored apple2_disk.cc 's/if (a2_flush(c, drive, msg, msglen)) {/if (a2_flush(c, drive, msg, msglen) \&\& false) {/' test_apple2_disk.cc $A2D
apple2app control a2d_sync_ignored apple2_disk.cc 's/if (!d->file->sync()) {/if (!d->file->sync() \&\& false) {/' test_apple2_disk.cc $A2D
apple2app control a2d_kept_dropped apple2_disk.cc 's/    if (r < 0) {/    if (false) {/' test_apple2_disk.cc $A2D
apple2app control a2d_kept_retry apple2_disk.cc 's/g_kept\[drive\], d->kept_bits) < 0) {/g_kept[drive], d->kept_bits) < -1) {/' test_apple2_disk.cc $A2D
apple2app control a2d_saving apple2_disk.cc 's/MediaSaving saving(c, d->kept || d->needs_sync || (/MediaSaving saving(c, 0 \&\& (/' test_apple2_disk.cc $A2D
apple2app control a2d_hdd_sync_ignored apple2_hdd.cc 's/if (!u->file->sync()) {/if (!u->file->sync() \&\& false) {/' test_apple2_disk.cc $A2D
A2="apple2.cc $A2D $BASEL"
apple2app run apple2 test_apple2.cc $A2
A2L="desc_check.cc overlay.cc descrec.cc $A2"
apple2app run apple2_memops test_memops.cc $SVC $A2L
RARGS="$BUN/app" apple2app run apple2_descgen test_descgen.cc compiled_table.cc libfiles.cc libmenu.cc bundle.cc mediumwait.cc libload.cc libscan.cc instfiles.cc instcore.cc start.cc handover.cc menuctl.cc picklist.cc filemgr.cc menukeytab.cc menuflows.cc menuvlist.cc lineedit.cc menutree.cc freeze.cc $A2L
# the Apple IIe's rows of the host's ROM table: gen_romreq built as the Apple's app, its rows in the host's table
t_romreq_apple2() {
  local INC=("${INC_A2[@]}") CORE_TESTS="$TESTS_A2" APPLIB="$A2LIB"
  build "$T/gen_romreq_a2" "$TESTS_A2/gen_romreq.cc" $A2L
  "$T/gen_romreq_a2" > "$T/romreq_a2.inc"
  python3 -I - "$T/romreq_a2.inc" "$HA/romreq_table.inc" <<'PY' || { echo "romreq_table.inc is stale: gen_romreq's rows of the Apple IIe's app are not (in this order) in host-app/software/hostapp/romreq_table.inc" >&2; exit 1; }
import sys
own = [l for l in open(sys.argv[1]) if not l.startswith("//")]
host = iter(l for l in open(sys.argv[2]) if not l.startswith("//"))
sys.exit(0 if all(any(h == r for h in host) for r in own) and own else 1)
PY
  echo "test apple2_romreq: PASS"
  return 0
}
if ca_in apple2 && ca_unit apple2_romreq; then t_romreq_apple2; PASS=$((PASS + 1)); fi
# T239.3 D61: the Apple IIe's cases of the shared tests (TEST_APP_APPLE2), built as its app: the shared units no longer name it.
A2M="menuctl.cc picklist.cc filemgr.cc menukeytab.cc menuflows.cc menuvlist.cc lineedit.cc menutree.cc libfiles.cc"
A2DM="$A2L $A2M"
RARGS="$T/bundles/desc_own.bin" apple2app run apple2_desc test_desc.cc $A2DM
apple2app run apple2_joyswap test_joyswap.cc $A2L
apple2app run apple2_ledcores test_ledcores.cc $A2L
apple2app run apple2_hotkeys test_hotkeys.cc $A2DM
# the Apple IIe's two floppies and two slot 7 volumes kept in the .cfg, restored before the release, a missing medium left empty with a note (T180 D2/D5), with the controls of its engine
A2MME="test_media_mounts_engines.cc $A2DM start.cc handover.cc"
apple2app run apple2_mounts $A2MME
apple2app control mme_a2_mount apple2_disk.cc 's/^    mount_notify(c, drive); *\/\/ T180 D2$/    (void)0;/' $A2MME
apple2app control mme_a2_eject apple2_disk.cc 's/^    mount_notify(c, drive); *\/\/ T180 D2: with.*$/    (void)0;/' $A2MME
apple2app control mme_a2_path apple2_disk.cc 's/^    snprintf(d->path, sizeof(d->path), "%s", path);$/    (void)0;/' $A2MME
apple2app control mme_a2_hdd_mount apple2_hdd.cc 's/^    mount_notify(c, A2_DRIVES + unit); *\/\/ T180 D2$/    (void)0;/' $A2MME
apple2app control mme_a2_hdd_eject apple2_hdd.cc 's/^    mount_notify(c, A2_DRIVES + unit); *\/\/ T180 D2: the slots.*$/    (void)0;/' $A2MME
# T122: Create blank DSK (the images read back by checkers written from the format definitions) and the create flow (the prompt, the folder, the chunks, cancel), with the controls of the file writer
A2ND="freeze.cc $A2M $A2"
apple2app run apple2_newdisk test_newdisk.cc $A2ND
apple2app control nd_exists newfile.cc 's/^        if (fs->stat(full, \&sz, \&isdir)) {/        if (false) {/' test_newdisk.cc $A2ND
apple2app control nd_bare newfile.cc 's/^            if (fs->stat(full, \&sz, \&isdir)) {/            if (false) {/' test_newdisk.cc $A2ND
apple2app run apple2_createflow test_createflow.cc $A2ND
apple2app control cf_chunk newfile.h 's|^#define NF_CHUNK 16384u|#define NF_CHUNK 0x7FFFFFFFu|' test_createflow.cc $A2ND
apple2app control cf_remove newfile.cc 's|^        fs_->remove(full_); .*|        (void)0;|' test_createflow.cc $A2ND
# the bug report on the real descriptor (T162 D3: the registers it names, the count of the reads)
apple2app run apple2_corereport test_corereport.cc $SVC corereport.cc bugreport.cc board.cc $A2L   # board.cc: board_name, which the report calls
# every key of its menu (its own goldens golden/keywalk_apple2.txt, ids_apple2.txt) and the reach of its declaration over the API: the walks of test_keywalk and test_uicover on its table
apple2app run apple2_keywalk test_keywalk.cc instfiles.cc instcore.cc $A2DM
apple2app run apple2_uicover test_uicover.cc uiapp.cc $SVC $A2DM
