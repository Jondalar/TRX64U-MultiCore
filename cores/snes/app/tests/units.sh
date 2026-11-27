# shellcheck shell=bash
# The units of snes (T239 D17), moved out of tests/coreapp/run.sh by tools/tests_to_core.py. tests/coreapp/run.sh sources this file at its end, with every variable
# and function of the shared harness set; the test programs, sources and headers of this folder win over the shared ones for this app (CORE_TESTS, find_src, tpath).
TESTS_SNES="$ROOT/cores/snes/app/tests"
own_drv_dir snes "$T/snestab" || { echo "coreapp: the SNES's driver table was not generated" >&2; exit 1; }   # snes_regs.h names the registers through it
SNESLIB="$ROOT/cores/snes/app/lib"   # T239.3 D61: the SNES's library lives in its own repository part (cores/snes/app/lib), not in coreapp/cores
# The SNES is its own app as well (T239.3 D61: cores/snes/app). snesapp CMD... builds like a8app, with the SNES's library (APPLIB: first in the lookup, copied over the controls' tree), its table and TEST_APP_SNES: its units are in the group snes.
INC_SNES=(-DTEST_APP_SNES=1 -I"$TESTS_SNES" -DGOLDEN_DIR=\"$TESTS_SNES/golden\" -I"$SNESLIB" -I"$T/snestab/drv")
for i in "${INC[@]}"; do case "$i" in "-I$T/drv"|-DGOLDEN_DIR=*) ;; *) INC_SNES+=("$i") ;; esac; done
snesapp() { local INC=("${INC_SNES[@]}") CORE_TESTS="$TESTS_SNES" CA_APP=snes APPLIB="$SNESLIB"; "$@"; }
# T239.3 D61: the SNES's units, built as its app (snesapp: its library cores/snes/app/lib with its own copy of hk_defaults.h, its own table, the group snes) - the units its TOML B (cores/snes/app/sdk.toml)
# names for the steps 2, 8, 9 and 10 of its lane. The sources are the app's own: snes.cc with the cartridge reader snes_rom.cc and the base's (the pad library is in $BASEL). These are
# the SNES's cases of the shared tests (T239.5 D13, D17): the shared units name no SNES any more.
SNESS="snes.cc snes_rom.cc $BASEL"
SNESA="menuctl.cc picklist.cc filemgr.cc menukeytab.cc menuflows.cc menuvlist.cc lineedit.cc menutree.cc $SNESS"
SNESD="desc_check.cc overlay.cc descrec.cc xinput.cc $SNESA"
snesapp run snes_rom test_snes_rom.cc snes_rom.cc
snesapp run snes test_snes.cc $SNESS
snesapp control snes_load snes.cc 's/int n = st->save_path\[0\] ? c->fs->load(st->save_path, st->shadow, info.ram_bytes) : -1;/int n = -1;/' test_snes.cc $SNESS
snesapp control snes_changed snes.cc 's/    if (!force \&\& !memcmp(cur, st->shadow, n)) {/    if (false) {/' test_snes.cc $SNESS
snesapp control snes_flush_init snes.cc 's/    flush_save(c, false, msg, msglen);                        \/\/ what the game wrote/    (void)0;                                                  \/\/ what the game wrote/' test_snes.cc $SNESS
snesapp control snes_retry snes.cc 's/    if (!c->fs->save(st->save_path, cur, n)) {/    if (!c->fs->save(st->save_path, cur, n) \&\& (memcpy(st->shadow, cur, n), true)) {/' test_snes.cc $SNESS
snesapp control snes_dir snes.cc 's|snprintf(dir, len, "%s/saves", parent);|snprintf(dir, len, "%s/save", parent);|' test_snes.cc $SNESS
snesapp control snes_replace_flush snes.cc '/T180 D1, D4: the old game/s/if (snes_media_flush(c, msg, msglen))/if (false)/' test_snes.cc $SNESS
snesapp control snes_flush_fails snes.cc '/^static int snes_media_flush/,/^}/s/if (flush_save(c, false, msg, msglen)) {/if (flush_save(c, false, msg, msglen) \&\& false) {/' test_snes.cc $SNESS
RARGS="$T/bundles/desc_own.bin" snesapp run snes_desc test_desc.cc $SNESD
RARGS="$BUN/app" snesapp run snes_descgen test_descgen.cc compiled_table.cc libfiles.cc libmenu.cc bundle.cc mediumwait.cc libload.cc libscan.cc instfiles.cc instcore.cc start.cc handover.cc $SNESD
snesapp run snes_hotkeys test_hotkeys.cc $SNESD
# T156, T196: the pad service, the XInput pads and the mapping wizard on the SNES's descriptor and a model of its register block (the NES's: nes_pad, nes_xinput, nes_padmenu); a control is one defect, the same test built against it must fail
snesapp run snes_pad test_pad.cc $SNESS overlay.cc descrec.cc
snesapp run snes_xinput test_xinput.cc xinput.cc $SNESS
snesapp control pad_cfgkeys settings.cc 's/if (!strncasecmp(key, "Pad ", 4)) {/if (false) {/' test_pad.cc $SNESS overlay.cc descrec.cc
snesapp run snes_padmenu test_padmenu.cc $SNESA
# T192.3 leak 1 (persistence): the wizard module saves the map through pad_save_map and tells the truth about it
snesapp control pad_wiz_not_saved padwiz.cc 's/bool saved = c->settings \&\& pad_save_map(c->settings, g_player);/bool saved = false;/' test_padmenu.cc menuctl.cc picklist.cc filemgr.cc menukeytab.cc menuflows.cc menuvlist.cc lineedit.cc menutree.cc $SNESS
snesapp control pad_wiz_saved_lie padwiz.cc 's/bool saved = c->settings \&\& pad_save_map(c->settings, g_player);/bool saved = true;/' test_padmenu.cc menuctl.cc picklist.cc filemgr.cc menukeytab.cc menuflows.cc menuvlist.cc lineedit.cc menutree.cc $SNESS
# T180 D2/D5: the game cartridge keeps its mount in the .cfg, is restored before the release, a missing medium stays empty with a note, a set goes in and out with one reset (the SNES's profile of test_media_mounts_engines.cc)
SNESM="test_media_mounts_engines.cc $SNESD start.cc handover.cc"
snesapp run snes_mounts $SNESM
snesapp control mme_snes_eject snes.cc 's/^    mount_notify(c, 0);$/    (void)0;/' $SNESM
snesapp control mme_snes_fail snes.cc 's/^        mount_notify(c, 0); *\/\/ the old game is gone and the slot is empty (T180 D2)$/        (void)0;/' $SNESM
# the engine's eject (mediamounts.cc) is reached through a game slot's key only (the cartridge port's profile of the shared unit calls the slot directly): the control stands on the SNES's profile
snesapp control mme_engine_eject mediamounts.cc 's/        return c->desc->mount_slots->eject(c, idx - nwin(c) - nsec(c), msg, msglen);/        return 1;/' $SNESM
# T192.3 S6 / S15: every key of its menu against its golden (golden/keywalk_snes.txt, ids_snes.txt) and its declaration over the API
snesapp run snes_keywalk test_keywalk.cc libfiles.cc instfiles.cc instcore.cc $SNESD
snesapp run snes_uicover test_uicover.cc uiapp.cc libfiles.cc $SVC $SNESD
