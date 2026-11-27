# shellcheck shell=bash
# The units of cpc (T239 D17), moved out of tests/coreapp/run.sh by tools/tests_to_core.py. tests/coreapp/run.sh sources this file at its end, with every variable
# and function of the shared harness set; the test programs, sources and headers of this folder win over the shared ones for this app (CORE_TESTS, find_src, tpath).
TESTS_CPC="$ROOT/cores/cpc/app/tests"
source "$TESTS_CPC/cpc_drv.sh"
cpc_drv_dir "$T/cpctab" || { echo "coreapp: the CPC's driver table was not generated" >&2; exit 1; }   # cpc_regs.h names the registers through it
CPCLIB="$ROOT/cores/cpc/app/lib"   # the CPC's library lives in its own repository part (cores/cpc/app/lib), not in coreapp/cores; it holds its own copies of tape_deck.*, tape_pulses.* and hk_defaults.h
INC_CPC=(-DTEST_APP_CPC=1 -I"$TESTS_CPC" -DGOLDEN_DIR=\"$TESTS_CPC/golden\" -I"$CPCLIB" -I"$T/cpctab/drv")
for i in "${INC[@]}"; do case "$i" in "-I$T/drv"|-DGOLDEN_DIR=*) ;; *) INC_CPC+=("$i") ;; esac; done
cpcapp() { local INC=("${INC_CPC[@]}") CORE_TESTS="$TESTS_CPC" CA_APP=cpc APPLIB="$CPCLIB"; "$@"; }
# T239.5 D4, D16: the CPC's units, built as its app (cpcapp: its library cores/cpc/app/lib with its own copies of tape_deck.*, tape_pulses.* and hk_defaults.h, its own table, the group cpc) -
# the units its TOML B (cores/cpc/app/sdk.toml) names for the steps 2 and 8 to 10 of its lane. The sources are the app's own: cpc.cc, cpc_disk.cc, the tape deck and the base's.
# The descriptor, the two DSK drives with their write-back and the tape, against a model of the core.
CPCS="cpc.cc cpc_disk.cc tape_pulses.cc tape_deck.cc $BASEL"
CPCA="menuctl.cc picklist.cc filemgr.cc menukeytab.cc menuflows.cc menuvlist.cc lineedit.cc menutree.cc $CPCS"
CPCD="desc_check.cc overlay.cc descrec.cc $CPCA"
CPCM="test_media_mounts_engines.cc $CPCD start.cc handover.cc"
cpcapp run cpc test_cpc.cc $CPCS
cpcapp run cpc_menu test_cpc_menu.cc menuctl.cc picklist.cc filemgr.cc menukeytab.cc menuflows.cc menuvlist.cc lineedit.cc menutree.cc libfiles.cc $CPCS
cpcapp run tape_cpc test_tape.cc tape_pulses.cc
cpcapp run cpc_memops test_memops.cc $SVC $CPCD
RARGS="$T/bundles/desc_own.bin" cpcapp run cpc_desc test_desc.cc $CPCD
RARGS="$BUN/app" cpcapp run cpc_descgen test_descgen.cc compiled_table.cc libfiles.cc libmenu.cc bundle.cc mediumwait.cc libload.cc libscan.cc instfiles.cc instcore.cc start.cc handover.cc $CPCD
cpcapp run cpc_hotkeys test_hotkeys.cc $CPCD
cpcapp run cpc_ledcores test_ledcores.cc $CPCD
cpcapp run cpc_joyswap test_joyswap.cc $CPCD
# T239.5 D13: every key of its menu against its golden (cpc_keywalk: golden/keywalk_cpc.txt, ids_cpc.txt) and its declaration over the API (cpc_uicover), TEST_APP_CPC
cpcapp run cpc_keywalk test_keywalk.cc libfiles.cc instfiles.cc instcore.cc $CPCD
cpcapp run cpc_uicover test_uicover.cc uiapp.cc libfiles.cc $SVC $CPCD
cpcapp run cpc_mounts $CPCM
cpcapp run cpc_newdisk test_newdisk.cc newdisk.cc newfile.cc $CPCD
cpcapp run cpc_createflow test_createflow.cc newdisk.cc newfile.cc $CPCD
cpcapp control cpc_disk_take cpc_disk.cc 's/            c->io->wr(dirty_reg\[drive\] + m, b);    \/\/ taken.*$/            (void)0;/' test_cpc.cc $CPCS
cpcapp control cpc_disk_idle cpc_disk.cc 's/now - d.last_ms >= CPCD_IDLE_MS || //' test_cpc.cc $CPCS
cpcapp control cpc_disk_maxdirty cpc_disk.cc 's/ || now - d.first_ms >= CPCD_MAX_DIRTY_MS//' test_cpc.cc $CPCS
cpcapp control cpc_disk_short cpc_disk.cc 's/uint32_t len = disk.size - off < CPC_DIRTY_CHUNK ? disk.size - off : CPC_DIRTY_CHUNK;/uint32_t len = CPC_DIRTY_CHUNK;/' test_cpc.cc $CPCS
cpcapp control cpc_disk_eject cpc_disk.cc 's/^    cpcd_poll(c, s, false);   .*$/    (void)0;/' test_cpc.cc $CPCS
cpcapp control cpc_disk_retry cpc_disk.cc 's/now - d.try_ms >= 1000/false/' test_cpc.cc $CPCS
cpcapp control cpc_disk_mountflush cpc_disk.cc 's/if (d.mounted \&\& !flush_drive(c, d, drive)) {/if (false) {/' test_cpc.cc $CPCS
cpcapp control cpc_disk_marks cpc_disk.cc 's/c->io->wr(dirty_reg\[drive\] + m, 0xFF);/(void)0;/' test_cpc.cc $CPCS
cpcapp control cpc_disk_flushinit cpc.cc 's/    cpcd_flush_all(c, \&st->disks);   .*$/    (void)0;/;s/^static int cpc_media_flush.*$/static int cpc_media_flush(CoreCtx *, char *, int) { return 0; }/' test_cpc.cc $CPCS
cpcapp control cpc_unsynced_forgotten cpc_disk.cc 's/disk.unsynced = true;/(void)0;/' test_cpc.cc $CPCS
cpcapp control cpc_mount_poll cpc_disk.cc '/T180 D1: the last marks/s/cpcd_poll(c, s, false);/(void)0;/' test_cpc.cc $CPCS
cpcapp control cpc_hook_engine engines.cc 's/if (d \&\& d->media_flush \&\& d->media_flush(c, m, sizeof(m)) \&\& !bad++) {/if (false) {/' test_cpc.cc $CPCS
cpcapp control mme_cpc_mount cpc_disk.cc 's/^    mount_notify(c, drive); *\/\/ T180 D2: the slots are A, B, Tape$/    (void)0;/' $CPCM
cpcapp control mme_cpc_eject cpc_disk.cc 's/^    mount_notify(c, drive); *\/\/ T180 D2$/    (void)0;/' $CPCM
cpcapp control mme_cpc_eject_empty cpc_disk.cc 's/^        mount_notify(c, drive); *\/\/ T180 D2: an eject of the empty.*$/        (void)0;/' $CPCM
cpcapp control mme_cpc_model cpc.cc 's/int mi = c->media_restoring \&\& c->settings/int mi = false \&\& c->settings/' $CPCM
cpcapp control mme_cpc_fail cpc_disk.cc 's/^            mount_notify(c, drive); *\/\/ the old disk is out and the drive is empty$/            (void)0;/' $CPCM
cpcapp control mme_tape_notify tape_deck.cc 's/^    mount_notify(c, r.slot); *\/\/ T180 D2$/    (void)0;/' $CPCM
cpcapp control mme_tape_fail tape_deck.cc 's/^            mount_notify(c, r.slot); *\/\/ the old tape is out and the deck is empty$/            (void)0;/' $CPCM
# the CPC's rows of the host's ROM table: gen_romreq built as the CPC's app, its rows in the host's table
t_romreq_cpc() {
  local INC=("${INC_CPC[@]}") CORE_TESTS="$TESTS_CPC" APPLIB="$CPCLIB"
  build "$T/gen_romreq_cpc" "$TESTS_CPC/gen_romreq.cc" $CPCD
  "$T/gen_romreq_cpc" > "$T/romreq_cpc.inc"
  python3 -I - "$T/romreq_cpc.inc" "$HA/romreq_table.inc" <<'PY' || { echo "romreq_table.inc is stale: gen_romreq's rows of the CPC's app are not (in this order) in host-app/software/hostapp/romreq_table.inc" >&2; exit 1; }
import sys
own = [l for l in open(sys.argv[1]) if not l.startswith("//")]
host = iter(l for l in open(sys.argv[2]) if not l.startswith("//"))
sys.exit(0 if all(any(h == r for h in host) for r in own) and own else 1)
PY
  echo "test cpc_romreq: PASS"
  return 0
}
if ca_in cpc && ca_unit cpc_romreq; then t_romreq_cpc; PASS=$((PASS + 1)); fi
