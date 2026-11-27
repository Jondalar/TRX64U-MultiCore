# shellcheck shell=bash
# The units of nes (T239 D17), moved out of tests/coreapp/run.sh by tools/tests_to_core.py. tests/coreapp/run.sh sources this file at its end, with every variable
# and function of the shared harness set; the test programs, sources and headers of this folder win over the shared ones for this app (CORE_TESTS, find_src, tpath).
TESTS_NES="$ROOT/cores/nes/app/tests"
NESSRC="nes.cc ines.cc $BASEL"
source "$TESTS_NES/nes_drv.sh"
nes_drv_dir "$T/nestab" || { echo "coreapp: the NES's driver table was not generated" >&2; exit 1; }   # nes_regs.h names the registers through it
NESLIB="$ROOT/cores/nes/app/lib"   # T239.5 D15: the NES's library lives in its own repository part (cores/nes/app/lib), not in coreapp/cores
# The NES is its own app as well (T239.5 D15: cores/nes/app). nesapp CMD... builds like a8app, with the NES's library (APPLIB: first in the lookup, copied over the controls' tree), its table and TEST_APP_NES: its units are in the group nes.
INC_NES=(-DTEST_APP_NES=1 -I"$TESTS_NES" -DGOLDEN_DIR=\"$TESTS_NES/golden\" -I"$NESLIB" -I"$T/nestab/drv")
for i in "${INC[@]}"; do case "$i" in "-I$T/drv"|-DGOLDEN_DIR=*) ;; *) INC_NES+=("$i") ;; esac; done
nesapp() { local INC=("${INC_NES[@]}") CORE_TESTS="$TESTS_NES" CA_APP=nes APPLIB="$NESLIB"; "$@"; }
nesapp run nes_ines test_ines.cc ines.cc
nesapp run nes_core test_nes.cc $NESSRC
nesapp run nes_menuctl test_nes_menuctl.cc menuctl.cc picklist.cc filemgr.cc menukeytab.cc menuflows.cc menuvlist.cc lineedit.cc menutree.cc libfiles.cc nes.cc ines.cc $BASEL
# T239.5 D15: the NES's units built as its app (nesapp: its library cores/nes/app/lib and its own table, the group nes) that are not above (nes_ines, nes_core, nes_menuctl) - the units its TOML B
# (cores/nes/app/sdk.toml) names for the steps 2 and 8 to 10 of its lane. The sources are the app's own: nes.cc with ines.cc and the base's (no other core). The NES has no ROM: no romreq unit.
NESD="desc_check.cc overlay.cc descrec.cc nes.cc ines.cc $BASEL"
nesapp run nes_memops test_memops.cc $SVC $NESD
RARGS="$BUN/app" nesapp run nes_descgen test_descgen.cc compiled_table.cc libfiles.cc libmenu.cc bundle.cc mediumwait.cc libload.cc libscan.cc instfiles.cc instcore.cc start.cc handover.cc menuctl.cc picklist.cc filemgr.cc menukeytab.cc menuflows.cc menuvlist.cc lineedit.cc menutree.cc freeze.cc $NESD
# T239.5 D13: the NES's cases of the shared tests, built as its app (the shared builds no longer name it): its descriptor and menu, the hotkeys, the joystick swap, the cartridge slot,
# the pads, the picture and the generic items on its record, the record menus over its descriptor (desc_places, desc_cut)
NESA="menuctl.cc picklist.cc filemgr.cc menukeytab.cc menuflows.cc menuvlist.cc lineedit.cc menutree.cc $NESD"
NESM="test_media_mounts_engines.cc $NESA start.cc handover.cc"
RARGS="$T/bundles/desc_own.bin" nesapp run nes_desc test_desc.cc $NESA
nesapp run nes_hotkeys test_hotkeys.cc $NESA
nesapp run nes_joyswap test_joyswap.cc $NESD
nesapp run nes_mounts $NESM
nesapp control mme_nes_eject nes.cc 's/^    mount_notify(c, 0);$/    (void)0;/' $NESM
nesapp control mme_nes_fail nes.cc 's/^        mount_notify(c, 0); *\/\/ the old program is gone and the slot is empty (T180 D2)$/        (void)0;/' $NESM
RARGS="$BUN/desc_own.bin" nesapp run nes_pad test_pad.cc $NESD                                  # with T228 R10, the menu record's pad (golden/desc_own.toml)
nesapp run nes_xinput test_xinput.cc xinput.cc $NESD
nesapp run nes_padmenu test_padmenu.cc $NESA
nesapp run nes_picture test_picture.cc $NESA
nesapp run nes_pictfollow test_pictfollow.cc $NESA
nesapp run nes_scale test_scale.cc $NESA
nesapp run nes_video test_video.cc $NESD
nesapp run nes_volume test_volume.cc $NESA
nesapp run nes_tone test_tone.cc $NESA
nesapp run nes_baseset test_baseset.cc $NESD
nesapp run nes_baserows test_baserows.cc $NESA
RARGS="$T/bundles" nesapp run nes_libscan test_libscan.cc libload.cc libmenu.cc libscan.cc board.cc $NESA
CARGS="$BUN/desc_places.bin" RARGS="$BUN/desc_places.bin" nesapp run nes_uiplace test_uiplace.cc $NESA
RARGS="$BUN/desc_cut.bin" nesapp run nes_keywalk test_keywalk.cc libfiles.cc instfiles.cc instcore.cc $NESA     # the NES's menu and the record menu over it: golden/keywalk_nes.txt, ids_nes.txt
nesapp run nes_uicover test_uicover.cc uiapp.cc libfiles.cc $SVC $NESA                                                  # every row, setting value and operation of its declaration over the API
RARGS="$BUN/desc_cut.bin" nesapp run nes_cut test_cut.cc libload.cc libmenu.cc libfiles.cc $NESA
