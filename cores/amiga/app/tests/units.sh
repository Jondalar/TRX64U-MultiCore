# shellcheck shell=bash
# The units of amiga (T239 D17), moved out of tests/coreapp/run.sh by tools/tests_to_core.py. tests/coreapp/run.sh sources this file at its end, with every variable
# and function of the shared harness set; the test programs, sources and headers of this folder win over the shared ones for this app (CORE_TESTS, find_src, tpath).
TESTS_AMIGA="$ROOT/cores/amiga/app/tests"
source "$TESTS_AMIGA/amiga_drv.sh"
amiga_drv_dir "$T/amigatab" || { echo "coreapp: the Amiga 500's driver table was not generated" >&2; exit 1; }   # amiga_regs.h names the registers through it
AMLIB="$ROOT/cores/amiga/app/lib"   # T239.5 D15: the Amiga 500's library lives in its own repository part (cores/amiga/app/lib), not in coreapp/cores
# The Amiga 500 is its own app as well (T239.5 D15: cores/amiga/app). amigaapp CMD... builds like neogeoapp, with the Amiga's library, its table and TEST_APP_AMIGA: its units are in the group amiga.
INC_AMIGA=(-DTEST_APP_AMIGA=1 -I"$TESTS_AMIGA" -DGOLDEN_DIR=\"$TESTS_AMIGA/golden\" -I"$AMLIB" -I"$T/amigatab/drv")
for i in "${INC[@]}"; do case "$i" in "-I$T/drv"|-DGOLDEN_DIR=*) ;; *) INC_AMIGA+=("$i") ;; esac; done
amigaapp() { local INC=("${INC_AMIGA[@]}") CORE_TESTS="$TESTS_AMIGA" CA_APP=amiga APPLIB="$AMLIB"; "$@"; }
# T104 5 (the Amiga and the A1200 on the generic application): the engines the records are served by, each against a model of the core
AM="amiga.cc $BASEL"
amigaapp run amiga_regset test_regset.cc $AM
# T141: the USB keyboard as raw Amiga key events (S3 the keys, S4 each key once / the menu / the register's room)
amigaapp run amiga_rawkeys test_rawkeys.cc $AM
# rk_menukey is gone (T141 D5): SCROLL LOCK, the one menu key left, has no Amiga code, so dropping it from the menu keys sends nothing either
# T159 S1/S2: case LED 1 = the machine's drive LED (the selector written once)
amigaapp run amiga_ledload test_ledload.cc $AM
# T190 (#51): Mouse sensitivity / acceleration in the Amiga cores: the rows, a +10 report scaled as each setting says, the restart applying the stored values
amigaapp run amiga_mousescale test_mousescale.cc amiga.cc $BASEL
amigaapp control scale_store settings.cc 's/^    return write_cfg();$/    return true;/' test_mousescale.cc amiga.cc $BASEL
# T104.1 5.6: the mouse library, a report stream into the Amiga's counters and the C64's 1351, and the keys-only UI
amigaapp run amiga_mouselib test_mouselib.cc amiga.cc $BASEL
amigaapp run amiga_romset test_romset.cc $AM
# T104 5.15: the Amiga 500 runs the generic application; its record against the register decode of the A500 gateware
amigaapp run amiga500 test_amiga500.cc $AM
# T97 5.10: the picture window follows the display window, in the application (fbwin.h)
amigaapp run amiga_fbwin test_fbwin.cc $AM
# T239.5 D15: the Amiga 500's units built as its app (amigaapp: its library cores/amiga/app/lib and its own table, the group amiga): amiga500 (its record against the decode of its gateware), amiga_regset, amiga_rawkeys, amiga_ledload,
# amiga_mousescale (with its control), amiga_mouselib, amiga_romset and amiga_fbwin, run above where they were the shared units on the A500's record, and the units below, which the
# TOML B (cores/amiga/app/sdk.toml) names for the steps 2 and 8 to 10 of its lane. The sources are the app's own: amiga.cc and the base's (no other core).
AMD="desc_check.cc overlay.cc descrec.cc amiga.cc menuctl.cc picklist.cc filemgr.cc menukeytab.cc menuflows.cc menuvlist.cc lineedit.cc menutree.cc $BASEL"
amigaapp run amiga_joyswap test_joyswap.cc $AMD
amigaapp run amiga_memops test_memops.cc $SVC $AMD
RARGS="$BUN/app" amigaapp run amiga_descgen test_descgen.cc compiled_table.cc libfiles.cc libmenu.cc bundle.cc mediumwait.cc libload.cc libscan.cc instfiles.cc instcore.cc start.cc handover.cc freeze.cc $AMD
amigaapp run amiga_ledcores test_ledcores.cc $AMD
# the Amiga 500's rows of the host's ROM table: gen_romreq built as the Amiga 500's app, its rows in the host's table
t_romreq_amiga() {
  local INC=("${INC_AMIGA[@]}") CORE_TESTS="$TESTS_AMIGA" APPLIB="$AMLIB"
  build "$T/gen_romreq_amiga" "$TESTS_AMIGA/gen_romreq.cc" $AMD
  "$T/gen_romreq_amiga" > "$T/romreq_amiga.inc"
  python3 -I - "$T/romreq_amiga.inc" "$HA/romreq_table.inc" <<'PY' || { echo "romreq_table.inc is stale: gen_romreq's rows of the Amiga 500's app are not (in this order) in host-app/software/hostapp/romreq_table.inc" >&2; exit 1; }
import sys
own = [l for l in open(sys.argv[1]) if not l.startswith("//")]
host = iter(l for l in open(sys.argv[2]) if not l.startswith("//"))
sys.exit(0 if all(any(h == r for h in host) for r in own) and own else 1)
PY
  echo "test amiga_romreq: PASS"
  return 0
}
if ca_in amiga && ca_unit amiga_romreq; then t_romreq_amiga; PASS=$((PASS + 1)); fi
# T239.5 D13: the Amiga 500's cases of the shared tests, built as its app (amigaapp, the group amiga; the shared units name no Amiga any more). The units whose every case stands on the
# A500's record (the engines of its media, the folder disk, the mouse sink, the home folder, the reopen, the UI service), with their controls, and the A500's cases of the files that
# serve several cores (TEST_APP_AMIGA there). AMA: the app's sources with the menu; AMD with the descriptor check and the record reader (above).
AMA="amiga.cc menuctl.cc picklist.cc filemgr.cc menukeytab.cc menuflows.cc menuvlist.cc lineedit.cc menutree.cc $BASEL"
amigaapp run amiga_start test_start.cc start.cc handover.cc amiga.cc $BASEL
amigaapp run amiga_video test_video.cc amiga.cc $BASEL
RARGS="$T/bundles/desc_own.bin" amigaapp run amiga_desc test_desc.cc $AMD
amigaapp run amiga_hotkeys test_hotkeys.cc $AMD
# every key of its menu against its golden (amiga_keywalk: golden/keywalk_amiga.txt, ids_amiga.txt) and every row, setting value and operation of its declaration over the API (amiga_uicover)
amigaapp run amiga_keywalk test_keywalk.cc libfiles.cc instfiles.cc instcore.cc $AMD
amigaapp run amiga_uicover test_uicover.cc uiapp.cc libfiles.cc $SVC $AMD
amigaapp run amiga_volume test_volume.cc $AMA
amigaapp run amiga_tone test_tone.cc $AMA
amigaapp run amiga_baseset test_baseset.cc amiga.cc $BASEL
amigaapp run amiga_scale test_scale.cc desc_check.cc $AMA
amigaapp run amiga_newdisk test_newdisk.cc $AMD
amigaapp run amiga_createflow test_createflow.cc $AMD
amigaapp run amiga_corereport test_corereport.cc $SVC corereport.cc bugreport.cc board.cc $AMD   # board.cc: board_name, which the report calls
amigaapp run amiga_corefolder test_corefolder.cc start.cc handover.cc amiga.cc $BASEL
# T180 D10 (#40): the home folder of every core (the Amiga 500 here): Set as home folder lands in its .cfg, Enter Home on Startup decides where F3 opens
amigaapp run amiga_home test_home.cc menuctl.cc picklist.cc filemgr.cc menukeytab.cc menuflows.cc menuvlist.cc lineedit.cc menutree.cc amiga.cc libfiles.cc $BASEL
amigaapp control home_not_stored libfiles.cc 's/    ops_call(c, "settings.set", in, 2, &r);/    (void)r;/' test_home.cc menuctl.cc picklist.cc filemgr.cc menukeytab.cc menuflows.cc menuvlist.cc lineedit.cc menutree.cc amiga.cc libfiles.cc $BASEL
# T192.3 D30: the menu opens where it was left ("Menu opens at" = Last screen): area, tab, row, Library folder and file, scroll; Home (F1) as before; what is gone falls back
amigaapp run amiga_reopen test_reopen.cc menuctl.cc picklist.cc filemgr.cc menukeytab.cc menuflows.cc menuvlist.cc lineedit.cc menutree.cc amiga.cc libfiles.cc $BASEL
amigaapp run amiga_uiapp test_uiapp.cc uiapp.cc menuctl.cc picklist.cc filemgr.cc menukeytab.cc menuflows.cc menuvlist.cc lineedit.cc menutree.cc amiga.cc libfiles.cc $SVC $BASEL
amigaapp run amiga_winslot test_winslot.cc $AM
amigaapp control win_overflow winslot.cc 's/if (c->io->rd(w->dlog_status) \& 2) {/if (false) {/' test_winslot.cc $AM
amigaapp control win_idle winslot.cc 's/now - f.last_ms >= w->idle_flush_ms || //' test_winslot.cc $AM
amigaapp control win_maxdirty winslot.cc 's/ || now - f.first_ms >= w->max_dirty_ms//' test_winslot.cc $AM
amigaapp control win_eject_flush winslot.cc 's/^    if (f.mounted \&\& !flush_drive(c, n)) {/    if (false) {/' test_winslot.cc $AM
amigaapp run amiga_sectors test_sectors.cc $AM
amigaapp control sec_stage_write sectors.cc 's/c->io->ddr_read(s->stage_off, sec, SEC_BYTES);/memset(sec, 0, SEC_BYTES);/' test_sectors.cc $AM
amigaapp control sec_idle sectors.cc 's/now - h.last_write_ms >= s->idle_sync_ms/false/' test_sectors.cc $AM
amigaapp control sec_eject_sync sectors.cc 's/^    bool ok = h.file->sync();/    bool ok = true;/' test_sectors.cc $AM
# T180 D4/D1 (S1, S7): every path that lets go of a writable medium writes back first, completely; a failed write-back keeps the medium, stops the action and says why;
# a mount on an occupied slot lets the old medium go the same way. The Amiga 500's record on its model: a floppy, a hard disk image and a folder disk with writes no sync has taken yet.
MWB="test_media_writeback.cc menuctl.cc picklist.cc filemgr.cc menukeytab.cc menuflows.cc menuvlist.cc lineedit.cc menutree.cc libfiles.cc $AM"
amigaapp run amiga_media_writeback $MWB
amigaapp control mwb_return_host engines.cc 's/if (media_writeback(c, WB_WHY_CORE, o.msg, o.msglen)) {/if (wb_flush(c, WB_WHY_CORE, o.msg, o.msglen)) {/' $MWB
amigaapp control mwb_reset_writes_nothing boot.cc 's/    if (media_writeback(c, why, msg, msglen)) {/    if (false) {/' $MWB
amigaapp control mwb_reset_goes_on boot.cc 's/printf("\[core\] write-back failed, the action stops: %s\\n", msg);/printf("write-back failed: %s\\n", msg); return 0;/' $MWB
amigaapp control mwb_all_win engines.cc 's/if (!win_flush_all(c, m, sizeof(m)) \&\& !bad++) {/if (false) {/' $MWB
amigaapp control mwb_all_sec engines.cc 's/if (!sec_sync_all(c, m, sizeof(m)) \&\& !bad++) {/if (false \&\& sec_sync_all(c, m, sizeof(m))) {/' $MWB
amigaapp control mwb_win_eject winslot.cc 's/if (f.mounted \&\& !flush_drive(c, n)) {/if (f.mounted \&\& (flush_drive(c, n), false)) {/' $MWB
amigaapp control mwb_win_unsynced winslot.cc 's/(f.ndirty == 0 \&\& !f.unsynced)/f.ndirty == 0/' $MWB
amigaapp control mwb_win_replace winslot.cc 's/    if (eject_slot(c, n, msg, msglen)) {           \/\/ the old disk/    if (false) {                                   \/\/ the old disk/' $MWB
amigaapp control mwb_sec_eject sectors.cc 's/^    if (!sync_unit(c, u, msg, msglen)) {$/    if (!sync_unit(c, u, msg, msglen) \&\& false) {/' $MWB
amigaapp control mwb_sec_replace sectors.cc 's/    if (eject_unit(c, u, msg, msglen)) {              \/\/ T180 D1/    if (false) {                                      \/\/ T180 D1/' $MWB
amigaapp control mwb_saving engines.h 's/if (c_ \&\& c_->saving_depth++ == 0) {/if (false) {/' $MWB
# T180 D2/D5 (S2, S3, S8): "Persist medium mounts" keeps the mounts in the .cfg and the next start mounts them before the release; a missing medium leaves its slot empty with a
# note; a media set is the same keys in <core folder>/media/<name>.mnt, loaded with a write-back first and one reset. The Amiga 500's record again, a .cfg and a core folder in the fake file system.
MNT="test_media_mounts.cc menuctl.cc picklist.cc filemgr.cc menukeytab.cc menuflows.cc menuvlist.cc lineedit.cc menutree.cc libfiles.cc $AM"
amigaapp run amiga_media_mounts $MNT
# T198 S1, S7, S8, S10, S11, S14, S15: the configs (save, start with one, load, legacy media sets, no folder, many files)
amigaapp run amiga_configs test_configs.cc menuctl.cc picklist.cc filemgr.cc menukeytab.cc menuflows.cc menuvlist.cc lineedit.cc menutree.cc libfiles.cc $AM
CFG="test_configs.cc menuctl.cc picklist.cc filemgr.cc menukeytab.cc menuflows.cc menuvlist.cc lineedit.cc menutree.cc libfiles.cc $AM"
amigaapp control cfg_save_inplace mediamounts.cc 's/export_config(tmp, pk, pv, count, &ns)/export_config(path, pk, pv, count, \&ns)/' test_configs.cc ${CFG#test_configs.cc }
amigaapp control cfg_save_no_swap mediamounts.cc 's/        ok = c->fs->rename(tmp, path);/        ok = true;/' test_configs.cc ${CFG#test_configs.cc }
amigaapp control cfg_no_recovery mediamounts.cc 's/        configlist_fix_plan(&fix, n, G::get, raw);/        fix.n = 0;/' test_configs.cc ${CFG#test_configs.cc }
amigaapp control cfg_load_rebind mediamounts.cc 's/    c->settings->rebind(path);/    (void)path;/' test_configs.cc ${CFG#test_configs.cc }
amigaapp control mnt_not_stored mediamounts.cc 's/    if (!c->mounts_ready || !persist_on(c)) {/    if (true) {/' $MNT
amigaapp control mnt_off_acts mediamounts.cc 's/    if (c->settings \&\& persist_on(c)) {/    if (c->settings) {/' $MNT
amigaapp control mnt_off_keys_stay mediamounts.cc 's/    if (!persist_on(c)) {/    if (false) {/' $MNT
amigaapp control mnt_eject_not_stored winslot.cc 's/^        mounts_changed(c, false, n);                \/\/ T180 D2.*$/        (void)0;/' $MNT
# T180 #38: a floppy image with a FAT long name picked in the Library into DF0: the whole path reaches the mount; the status line shows the cut form. Controls: the entry at 55 characters,
# the window's name cut without the "..".
amigaapp control mnt_set_writeback mediamounts.cc 's/    if (media_writeback(c, WB_WHY_RESET, msg, msglen)) {/    if (false) {/' $MNT
# T180 D4 for the engines with their own flush (CoreDesc::media_flush / media_pending): the common path asks them on every exit, each descriptor hands its engine over, and each
# engine writes back before it lets the medium go (eject, replacement, reset, reload, restart), completely, and keeps the medium when it cannot
amigaapp control mwb_hook_flush engines.cc 's/if (d \&\& d->media_flush \&\& d->media_flush(c, m, sizeof(m)) \&\& !bad++) {/if (false) {/' $MWB
amigaapp control mwb_hook_pending engines.cc 's/ || (d \&\& d->media_pending \&\& d->media_pending(c))//' $MWB
# T128: a folder as an Amiga hard disk (docs/specs/T128-folder-hdd.md). One program for S1-S8 and S10 with an independent reader and writer of FFS volumes
# (folderhd_bench.h); S9 is tests/coreapp/fmfs64/.
FHD="test_folderhd.cc $AM"
amigaapp run amiga_folderhd $FHD
# S1: the disk the gateware accepts
# S2: a valid FFS volume
# S3: offsets beyond 4 GiB
# S4: names, dates, protection
# S5: data written in place waits for the metadata that owns the block (D5 changed, spec 5.5 no. 2)
amigaapp control fh_nopatch folderhd_fold.cc 's/cc == 1 \&\& consistent/false/' $FHD
amigaapp control fh_consistent folderhd_fold.cc 's/consistent = ffs_rd(rb, FL_BM_FLAG) == 0xFFFFFFFFu;/consistent = true;/' $FHD
# S6: the folder follows
amigaapp control fh_swap_sync winslot.cc 's/^    if (f.mounted \&\& !flush_drive(c, n)) {/    sec_sync_all(c); if (f.mounted \&\& !flush_drive(c, n)) {/' $FHD
amigaapp control fh_nofold folderhd_fold.cc 's/if (gen == folded_gen) {/if (true) {/' $FHD
amigaapp control fh_rename folderhd_fold.cc 's/p.ops |= OP_REN;/(void)0;/' $FHD
amigaapp control fh_delete folderhd_fold.cc 's/p.ops = OP_DEL;/p.ops = 0;/' $FHD
amigaapp control fh_content folderhd_fold.cc 's/int cc = content_changed(me, h);/int cc = 0;/' $FHD
amigaapp control fh_mkdir folderhd_fold.cc 's/if (!fs->mkdir(path)) {/if (false) {/' $FHD
amigaapp control fh_sidecar folderhd_fold.cc 's/if (p.protect == 0 \&\& cl == 0) {/if (true) {/' $FHD
amigaapp control fh_scratch folderhd_fold.cc 's/if (!keep \&\& xr_ok) sr_remove_range(run_b, run_n);/(void)0;/' $FHD
amigaapp control fh_link folderhd_fold.cc 's/if (!isdir \&\& h.sectype != FFS_ST_FILE) {/if (false) {/' $FHD
amigaapp control fh_abort folderhd_fold.cc 's/if (phase >= PH_WALK \&\& phase <= PH_MATERIAL \&\& gen != job_gen) {/if (false) {/' $FHD
amigaapp control fh_bmflag folderhd_fold.cc 's/if (ffs_rd(rb, FL_BM_FLAG) != 0xFFFFFFFFu) {/if (false) {/' $FHD
amigaapp control fh_limbo folderhd_fold.cc 's/if (!(p.ops \& OP_HOP)) continue;/continue;/' $FHD
# S7: loss
amigaapp control fh_inplace folderhd_fold.cc 's/tmp_path(p.tmp_seq, out, n);/path_of(p.node, out, n);/' $FHD
amigaapp control fh_repair folderhd_fold.cc 's/nodes\[r.node\].hsize = r.size;/(void)0;/' $FHD
amigaapp control fh_absent folderhd_fold.cc 's/bool gone = fs->remove(path) || host_absent(path);/bool gone = fs->remove(path) || true;/' $FHD
# S8: limits and the memory
# S10: the poll is not held longer than the slice
amigaapp control fh_slice folderhd_fold.cc 's/if (now - t0 + longest >= FH_SLICE_MS) {/if (false) {/' $FHD
amigaapp run amiga_mousesink test_mousesink.cc $AM
