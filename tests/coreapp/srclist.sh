# shellcheck shell=bash
# The core application built natively (T236 D2): the sources, defines and include path of tests/coreapp/render/run.sh and tools/menusim/run.sh.
# Sourced with ROOT set; coreapp_srcs fills SRCS.
#
# The sources are the board build's own list (SRCS_CC of the core app's Makefile), the ones that live in the application's
# directories; a file added there is linked here too, so the two lists cannot drift apart (CI 941: the drive library was
# missing here only). NOT_HERE are the board-only files the native build replaces with the program's main and the stubs.
SRC="$ROOT/coreapp/src"; CORES="$ROOT/coreapp/cores"; HA="$ROOT/host-app/software/hostapp"
MK="$ROOT/host-app/target/u64ii/riscv/coreapp/Makefile"
APPDIRS="$SRC $CORES $ROOT/app/loaders $ROOT/app/lib/cbm1541 $ROOT/app/edit $ROOT/app/view $ROOT/app/video $HA $ROOT/uicore"   # $HA: handover, bootcfg
NOT_HERE="table.cc hw.cc run.cc fm_fs.cc fm_medium.cc tempdisk.cc fastseek.cc libboard.cc nettransport.cc restsrv.cc uiapp_board.cc"
# the TRX64U's board-side contributors and what they stand on (the U64 registers, USB HID, the network stack, FreeRTOS tasks, lwIP sockets): tests/coreapp/board_stubs.cc stands in
NOT_HERE="$NOT_HERE u64cfg.cc libfiles.cc ringops.cc ring.cc ringfile.cc monitor.cc monsrv.cc online.cc diag.cc netops.cc netsvc.cc hostwifi.cc netfs.cc ftp_client.cc a64_http.cc a64_assembly.cc trx64u_ops.cc dma_uart.cc appsvc.cc uci.cc network_esp32.cc wifi.cc wifi_cmd.cc esp32.cc wifinet.cc usb_xinput.cc"   # usb_xinput.cc: the XInput USB driver (T196), on the USB stack
# test-only sources: the descriptor check, sha256.c (a SRCS_C file of the Makefile) built as C++, the board's video stubbed
TEST_SRCS="$SRC/desc_check.cc $ROOT/tests/coreapp/video_stub.cc $ROOT/tests/coreapp/sha256_cc.cc $ROOT/tests/coreapp/board_stubs.cc"
# the defines and the include path (LV_CONF_INCLUDE_SIMPLE: lv_conf.h comes from the directory the caller puts first)
CORE_DEFS="-std=gnu++14 -w -DVIDEOMODE_NO_BOARD=1 -DLV_CONF_INCLUDE_SIMPLE"
CORE_INCS="-I$ROOT/app/view -I$ROOT/app/video -I$SRC -I$ROOT/uicore -I$CORES -I$ROOT/app/loaders -I$ROOT/app/lib/cbm1541 -I$ROOT/app/edit -I$ROOT/tests/coreapp -I$ROOT/tests -I$ROOT/host-app/software/io/c64 -I$ROOT/host-app/software/lvgl_port -I$ROOT/host-app/software/filesystem -I$HA"

coreapp_srcs() {
  SRCS=""
  local f d
  for f in $(sed -n '/^SRCS_CC/,/^$/p' "$MK" | grep -oE '[A-Za-z0-9_]+\.cc'); do
    case " $NOT_HERE " in *" $f "*) continue ;; esac
    for d in $APPDIRS; do [ -f "$d/$f" ] && { SRCS="$SRCS $d/$f"; break; }; done
  done
  [ -n "$SRCS" ] || { echo "coreapp: no sources from $MK" >&2; return 1; }
  SRCS="$SRCS $TEST_SRCS"
}
