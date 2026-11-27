# shellcheck shell=bash
# The units of atari800 (T239 D17), moved out of tests/coreapp/run.sh by tools/tests_to_core.py. tests/coreapp/run.sh sources this file at its end, with every variable
# and function of the shared harness set; the test programs, sources and headers of this folder win over the shared ones for this app (CORE_TESTS, find_src, tpath).
TESTS_A8="$ROOT/cores/atari800/app/tests"
ATARI="atari800.cc atari_media.cc atari_sio.cc atari_atr.cc atari_xex.cc atari_cart.cc $BASEL"
own_drv_dir atari800 "$T/a8tab" || { echo "coreapp: the Atari 800 XL's driver table was not generated" >&2; exit 1; }   # atari800_regs.h names the registers through it
A8LIB="$ROOT/cores/atari800/app/lib"   # T239.5 D15: the Atari 800 XL's library lives in its own repository part (cores/atari800/app/lib), not in coreapp/cores
# The Atari 800 XL is its own app as well (T239.5 D15: cores/atari800/app). a8app CMD... builds like vc20app, with the Atari's library, its table and TEST_APP_ATARI800: its units are in the group atari800.
INC_A8=(-DTEST_APP_ATARI800=1 -I"$TESTS_A8" -DGOLDEN_DIR=\"$TESTS_A8/golden\" -I"$A8LIB" -I"$T/a8tab/drv")
for i in "${INC[@]}"; do case "$i" in "-I$T/drv"|-DGOLDEN_DIR=*) ;; *) INC_A8+=("$i") ;; esac; done
a8app() { local INC=("${INC_A8[@]}") CORE_TESTS="$TESTS_A8" CA_APP=atari800 APPLIB="$A8LIB"; "$@"; }
a8app run a8_boot test_boot_atari.cc $ATARI
a8app run a8_xex test_atari_xex.cc atari_xex.cc
a8app run a8_sio test_atari_sio.cc atari_sio.cc atari_atr.cc
a8app control sio_nowrite atari_sio.cc 's/if (d->write(pend_sector_, data_)) {/if (true) {/' test_atari_sio.cc atari_sio.cc atari_atr.cc
a8app run a8_media test_atari_media.cc $ATARI
a8app run a8_menuctl test_menuctl.cc menuctl.cc picklist.cc filemgr.cc menukeytab.cc menuflows.cc menuvlist.cc lineedit.cc menutree.cc libfiles.cc $ATARI
# Atari: a sync the medium refuses leaves the drive dirty and the disk in the drive
a8app control a8_flush_ignored atari_media.cc 's/if (!st->sio.flush_drive(drive, msg, msglen)) {/if (!st->sio.flush_drive(drive, msg, msglen) \&\& false) {/' test_atari_media.cc $ATARI
a8app control a8_hook_engine engines.cc 's/if (d \&\& d->media_flush \&\& d->media_flush(c, m, sizeof(m)) \&\& !bad++) {/if (false) {/' test_atari_media.cc $ATARI
# T239.5 D15: the Atari 800 XL's units built as its app (a8app: its library cores/atari800/app/lib and its own table, the group atari800) that are not above (a8_xex, a8_sio, a8_media, a8_boot, a8_menuctl) - the units its TOML B
# (cores/atari800/app/sdk.toml) names for the steps 2 and 8 to 10 of its lane. The sources are the app's own: atari800.cc with its formats and the base's (no other core).
A8D="desc_check.cc overlay.cc descrec.cc $ATARI"
a8app run a8_memops test_memops.cc $SVC $A8D
RARGS="$BUN/app" a8app run a8_descgen test_descgen.cc compiled_table.cc libfiles.cc libmenu.cc bundle.cc mediumwait.cc libload.cc libscan.cc instfiles.cc instcore.cc start.cc handover.cc menuctl.cc picklist.cc filemgr.cc menukeytab.cc menuflows.cc menuvlist.cc lineedit.cc menutree.cc freeze.cc $A8D
# the Atari 800 XL's rows of the host's ROM table: gen_romreq built as the Atari's app, its rows in the host's table
t_romreq_a8() {
  local INC=("${INC_A8[@]}") CORE_TESTS="$TESTS_A8" APPLIB="$A8LIB"
  build "$T/gen_romreq_a8" "$TESTS_A8/gen_romreq.cc" $A8D
  "$T/gen_romreq_a8" > "$T/romreq_a8.inc"
  python3 -I - "$T/romreq_a8.inc" "$HA/romreq_table.inc" <<'PY' || { echo "romreq_table.inc is stale: gen_romreq's rows of the Atari 800 XL's app are not (in this order) in host-app/software/hostapp/romreq_table.inc" >&2; exit 1; }
import sys
own = [l for l in open(sys.argv[1]) if not l.startswith("//")]
host = iter(l for l in open(sys.argv[2]) if not l.startswith("//"))
sys.exit(0 if all(any(h == r for h in host) for r in own) and own else 1)
PY
  echo "test a8_romreq: PASS"
  return 0
}
if ca_in atari800 && ca_unit a8_romreq; then t_romreq_a8; PASS=$((PASS + 1)); fi
# T239.5 D13: the Atari 800 XL's cases of the shared tests (TEST_APP_ATARI800), built as its app: the shared units no longer name it. The Atari was their main sample core: a
# generic case that stood on its descriptor is its case. The sources are the app's own: atari800.cc with its formats, the base's and the menu's (no other core).
A8M="menuctl.cc picklist.cc filemgr.cc menukeytab.cc menuflows.cc menuvlist.cc lineedit.cc menutree.cc libfiles.cc"
A8DM="$A8D $A8M"
a8app run a8_settings test_settings.cc $ATARI
a8app control set_loses_value settings.cc '/^bool Settings::set(int i, int v)/,/^}/s/    return write_cfg();/    { bool ok_ = write_cfg(); if (!ok_) core_[i] = -1; return ok_; }/' test_settings.cc $ATARI
a8app control set_writes_all settings.cc 's/^        if (core_\[i\] >= 0 || all) {/        if (true) {/' test_settings.cc $ATARI
a8app control set_reset_keeps settings.cc 's/if (fs_->remove(cfg_path_) || write_cfg())/if (false)/' test_settings.cc $ATARI
a8app control set_case cfgfile.cc 's/strncasecmp(a, b, la) == 0/strncmp(a, b, la) == 0/' test_settings.cc $ATARI
a8app run a8_video test_video.cc $ATARI
RARGS="$T/bundles" a8app run a8_libscan test_libscan.cc overlay.cc libload.cc libmenu.cc descrec.cc libscan.cc desc_check.cc menutree.cc board.cc $ATARI
RARGS="$T/bundles/desc_own.bin" a8app run a8_desc test_desc.cc $A8DM
a8app run a8_joyswap test_joyswap.cc $A8D
a8app run a8_ledcores test_ledcores.cc $A8D
a8app run a8_hotkeys test_hotkeys.cc $A8DM
a8app run a8_baseset test_baseset.cc $ATARI
a8app run a8_f3 test_f3.cc $A8M $ATARI
a8app run a8_library_n test_library_n.cc $A8M $ATARI
a8app run a8_picture test_picture.cc $A8DM
a8app run a8_pictfollow test_pictfollow.cc $A8DM
a8app run a8_scale test_scale.cc $A8DM
a8app run a8_fs64 test_fs64.cc $ROMF atari_atr.cc
A8BOARD="board.cc start.cc handover.cc $A8DM"
a8app run a8_board test_board.cc $A8BOARD
a8app control board_migrate_overwrite start.cc 's/if (c->fs->stat(c->cfg_path, \&size, \&dir) || /if (/' test_board.cc $A8BOARD
a8app control board_migrate_move start.cc 's/    int got = c->fs->load(old, buf, sizeof(buf));/    int got = c->fs->load(old, buf, sizeof(buf)); c->fs->remove(old);/' test_board.cc $A8BOARD
a8app control board_cfg_dir board.cc 's|"/flash/%s", core_id);|"/flash/trx64", core_id);|' test_board.cc $A8BOARD
a8app control board_cfg_path start.cc 's/if (!c->cfg_path\[0\] \&\& prof \&\& prof->flash_cfg \&\& prof->cfg_dir\[0\] \&\& c->desc \&\& c->desc->folder) {/if (false) {/' test_board.cc $A8BOARD
a8app control board_cfg_named_by_store start.cc 's/prof->cfg_dir, c->desc->folder);/prof->cfg_dir, c->desc->store);/' test_board.cc $A8BOARD
a8app control board_bootcfg board.cc 's/    snprintf(b->boot_cfg, sizeof(b->boot_cfg), "\/flash\/%s\/boot.cfg", core_id);/    snprintf(b->boot_cfg, sizeof(b->boot_cfg), "%s", FS_FLASH_BOOT_MULTICORE);/' test_board.cc $A8BOARD
A8WBM="$A8M $ATARI"
a8app run a8_writeback_menu test_writeback_menu.cc $A8WBM
a8app control wbm_answer_ignored writeback.cc 's/wb_answer(c, a.i(0) != 0, o.msg, o.msglen)/wb_answer(c, true, o.msg, o.msglen)/' test_writeback_menu.cc $A8WBM
a8app run a8_prompt test_prompt.cc $A8M $ATARI
A8MME="test_media_mounts_engines.cc $A8DM start.cc handover.cc"
a8app run a8_mounts $A8MME
a8app control mme_a8_disk atari_media.cc 's/^    mount_notify(c, drive); *\/\/ T180 D2: the slots.*$/    (void)0;/' $A8MME
a8app control mme_a8_cart atari_media.cc 's/^    mount_notify(c, SIO_DRIVES); *\/\/ T180 D2$/    (void)0;/' $A8MME
a8app control mme_a8_cart_reg atari_media.cc 's/^    a8_write_cart_reg(c); *\/\/ the machine is held.*$/    (void)0;/' $A8MME
a8app control mme_a8_xex atari_media.cc 's/"xex %s", path/"%s", path/' $A8MME
a8app run a8_newdisk test_newdisk.cc freeze.cc $A8M $ATARI
a8app run a8_createflow test_createflow.cc freeze.cc $A8M $ATARI
RARGS="$BUN/desc_cut.bin" a8app run a8_cut test_cut.cc overlay.cc libload.cc libmenu.cc descrec.cc $A8M $ATARI
a8app run a8_pad test_pad.cc overlay.cc descrec.cc $ATARI
a8app run a8_xinput test_xinput.cc xinput.cc $ATARI
# every key of its menu (its own goldens golden/keywalk_atari800.txt, ids_atari800.txt) and the reach of its declaration over the API: the walks of test_keywalk and test_uicover on its table
a8app run a8_keywalk test_keywalk.cc instfiles.cc instcore.cc $A8DM
a8app run a8_uicover test_uicover.cc uiapp.cc $SVC $A8DM
