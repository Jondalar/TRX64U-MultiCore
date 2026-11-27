# shellcheck shell=bash
# The units of neogeo (T239 D17), moved out of tests/coreapp/run.sh by tools/tests_to_core.py. tests/coreapp/run.sh sources this file at its end, with every variable
# and function of the shared harness set; the test programs, sources and headers of this folder win over the shared ones for this app (CORE_TESTS, find_src, tpath).
TESTS_NEOGEO="$ROOT/cores/neogeo/app/tests"
source "$TESTS_NEOGEO/neogeo_drv.sh"
neogeo_drv_dir "$T/neogeotab" || { echo "coreapp: the Neo Geo's driver table was not generated" >&2; exit 1; }   # neogeo_regs.h names the registers through it
NEOLIB="$ROOT/cores/neogeo/app/lib"   # T239.5 D15: the Neo Geo's library lives in its own repository part (cores/neogeo/app/lib), not in coreapp/cores
# The Neo Geo is its own app as well (T239.5 D15: cores/neogeo/app). neogeoapp CMD... builds like zxnextapp, with the Neo Geo's library, its table and TEST_APP_NEOGEO: its units are in the group neogeo.
# APPLIB puts its library first in the source lookup (no shared unit finds its sources: the Neo Geo is no sample core of the shared units, T239.5 D13).
INC_NEOGEO=(-DTEST_APP_NEOGEO=1 -I"$TESTS_NEOGEO" -DGOLDEN_DIR=\"$TESTS_NEOGEO/golden\" -I"$NEOLIB" -I"$T/neogeotab/drv")
for i in "${INC[@]}"; do case "$i" in "-I$T/drv"|-DGOLDEN_DIR=*) ;; *) INC_NEOGEO+=("$i") ;; esac; done
neogeoapp() { local INC=("${INC_NEOGEO[@]}") CORE_TESTS="$TESTS_NEOGEO" CA_APP=neogeo APPLIB="$NEOLIB"; "$@"; }
# T116 D11: Auto
NEOG="neogeo.cc neoset.cc $BASEL"
neogeoapp run neogeo test_neogeo.cc $NEOG
neogeoapp control neo_save_read neogeo.cc 's/    c->io->ddr_read(NEO_W_BACKUP, buf, 0x10000);//' test_neogeo.cc $NEOG
neogeoapp control neo_save_load neogeo.cc 's/    c->io->ddr_write(NEO_W_BACKUP, buf, 0x10000);//' test_neogeo.cc $NEOG
neogeoapp control neo_own_save neogeo.cc 's/bool own = corefolder_save_path(c->cor_path, file, out, outlen);/bool own = false;/' test_neogeo.cc $NEOG
neogeoapp control neo_old_save neogeo.cc 's/        if (n < 0 \&\& st->old_save_path\[0\]) {/        if (false) {/' test_neogeo.cc $NEOG
neogeoapp control neo_leaving_flush neogeo.cc 's/    if (neo_media_flush(c, msg, msglen)) {            \/\/ T180 D1, D4/    if (false) {                                      \/\/ T180 D1, D4/' test_neogeo.cc $NEOG
neogeoapp control neo_failed_forgotten neogeo.cc 's/st->save_failed = true;/(void)0;/' test_neogeo.cc $NEOG
neogeoapp control neo_unchanged_stops neogeo.cc 's/return st->loaded \&\& st->save_path\[0\] \&\& (st->save_failed || st->dirty_seen || (c->io->rd(NEO_STATUS) \& NEO_ST_DIRTY));/return st->loaded \&\& st->save_path[0];/' test_neogeo.cc $NEOG
# T239.5 D15: the Neo Geo's units built as its app (neogeoapp: its library cores/neogeo/app/lib and its own table, the group neogeo) that are not above (neogeo) - the units its TOML B
# (cores/neogeo/app/sdk.toml) names for the steps 2 and 8 to 10 of its lane. The sources are the app's own: neogeo.cc with neoset.cc and the base's (no other core). The pad service is the base's (pad.cc, in $BASEL).
NEOD="desc_check.cc overlay.cc descrec.cc neogeo.cc neoset.cc $BASEL"
neogeoapp run neogeo_joyswap test_joyswap.cc $NEOD
neogeoapp run neogeo_menuctl test_neogeo_menuctl.cc menuctl.cc picklist.cc filemgr.cc menukeytab.cc menuflows.cc menuvlist.cc lineedit.cc menutree.cc libfiles.cc $NEOD
neogeoapp run neogeo_memops test_memops.cc $SVC $NEOD
RARGS="$BUN/app" neogeoapp run neogeo_descgen test_descgen.cc compiled_table.cc libfiles.cc libmenu.cc bundle.cc mediumwait.cc libload.cc libscan.cc instfiles.cc instcore.cc start.cc handover.cc menuctl.cc picklist.cc filemgr.cc menukeytab.cc menuflows.cc menuvlist.cc lineedit.cc menutree.cc freeze.cc $NEOD
# T239.5 D13: the Neo Geo's cases of the shared tests (TEST_APP_NEOGEO), built as its app: the checks every descriptor passes and its menu's golden (neogeo_desc), the default hotkeys of S5
# (neogeo_hotkeys), the cartridge slot kept in the .cfg and restored before the release, a ROM set folder, a set with no game (neogeo_mounts, with the controls of its engine: T180 D2/D5),
# the joystick word its service writes from a USB pad and the capability "pad" (neogeo_pad), every key of its menu against its golden (neogeo_keywalk: golden/keywalk_neogeo.txt, ids_neogeo.txt) and every
# row, setting value and operation of its declaration reachable over the API (neogeo_uicover).
NEOA="menuctl.cc picklist.cc filemgr.cc menukeytab.cc menuflows.cc menuvlist.cc lineedit.cc menutree.cc $NEOD"
NEOM="test_media_mounts_engines.cc $NEOA start.cc handover.cc"
RARGS="$T/bundles/desc_own.bin" neogeoapp run neogeo_desc test_desc.cc $NEOA
neogeoapp run neogeo_hotkeys test_hotkeys.cc $NEOA
neogeoapp run neogeo_mounts $NEOM
neogeoapp control mme_neo_eject neogeo.cc 's/^    mount_notify(c, 0);$/    (void)0;/' $NEOM
neogeoapp control mme_neo_fail neogeo.cc 's/^        mount_notify(c, 0); *\/\/ the old game is gone and the slot is empty (T180 D2)$/        (void)0;/' $NEOM
neogeoapp control mme_neo_folder neogeo.cc 's/"folder %s", arg/"%s", arg/' $NEOM
neogeoapp run neogeo_pad test_pad.cc $NEOD
neogeoapp run neogeo_keywalk test_keywalk.cc libfiles.cc instfiles.cc instcore.cc $NEOA
neogeoapp run neogeo_uicover test_uicover.cc uiapp.cc libfiles.cc $SVC $NEOA
# the Neo Geo's rows of the host's ROM table: gen_romreq built as the Neo Geo's app, its rows (and the trx64u rows it emits) in the host's table
t_romreq_neogeo() {
  local INC=("${INC_NEOGEO[@]}") CORE_TESTS="$TESTS_NEOGEO" APPLIB="$NEOLIB"
  build "$T/gen_romreq_neogeo" "$TESTS_NEOGEO/gen_romreq.cc" $NEOD
  "$T/gen_romreq_neogeo" > "$T/romreq_neogeo.inc"
  python3 -I - "$T/romreq_neogeo.inc" "$HA/romreq_table.inc" <<'PY' || { echo "romreq_table.inc is stale: gen_romreq's rows of the Neo Geo's app are not (in this order) in host-app/software/hostapp/romreq_table.inc" >&2; exit 1; }
import sys
own = [l for l in open(sys.argv[1]) if not l.startswith("//")]
host = iter(l for l in open(sys.argv[2]) if not l.startswith("//"))
sys.exit(0 if all(any(h == r for h in host) for r in own) and own else 1)
PY
  echo "test neogeo_romreq: PASS"
  return 0
}
if ca_in neogeo && ca_unit neogeo_romreq; then t_romreq_neogeo; PASS=$((PASS + 1)); fi
