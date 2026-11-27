# shellcheck shell=bash
# The core application built natively (T236 D2): the sources, defines and include path of tests/coreapp/render/run.sh and tools/menusim/run.sh.
# Sourced with ROOT set; coreapp_srcs fills SRCS.
#
# The sources are the board build's own list (SRCS_CC of the core app's Makefile), the ones that live in the application's
# directories; a file added there is linked here too, so the two lists cannot drift apart (CI 941: the drive library was
# missing here only). NOT_HERE are the board-only files the native build replaces with the program's main and the stubs.
SRC="$ROOT/coreapp/src"; CORES="$ROOT/coreapp/cores"; HA="$ROOT/host-app/software/hostapp"
MK="$ROOT/host-app/target/u64ii/riscv/coreapp/Makefile"
APPDIRS="$SRC $CORES $ROOT/cores/vc20/app/lib $ROOT/app/loaders $ROOT/app/lib/cbm1541 $ROOT/app/edit $ROOT/app/view $ROOT/app/video $HA $ROOT/uicore"   # $HA: handover, bootcfg
NOT_HERE="libexports.cc libtab.cc c64cart_exports.cc cbm1541_exports.cc sid_exports.cc tempdisk_exports.cc cbmprg_exports.cc table.cc hw.cc run.cc fm_fs.cc fm_medium.cc tempdisk.cc fastseek.cc fastseek_exports.cc ftp_exports.cc libboard.cc nettransport.cc restsrv.cc uiapp_board.cc"
# the TRX64U's board-side contributors and what they stand on (the U64 registers, USB HID, the network stack, FreeRTOS tasks, lwIP sockets): tests/coreapp/board_stubs.cc stands in
NOT_HERE="$NOT_HERE libfiles.cc diag.cc netops.cc netsvc.cc dmasvc.cc hostwifi.cc netfs.cc ftp_client.cc resid8.cc dma_uart.cc appsvc.cc network_esp32.cc wifi.cc wifi_cmd.cc esp32.cc wifinet.cc usb_xinput.cc mouse_board.cc pad_board.cc u64_resample.cc"   # usb_xinput.cc: the XInput USB driver (T196), on the USB stack
# test-only sources: the descriptor check, sha256.c (a SRCS_C file of the Makefile) built as C++, the board's video stubbed
TEST_SRCS="$ROOT/tests/coreapp/libtab_native.cc $SRC/desc_check.cc $ROOT/tests/coreapp/video_stub.cc $ROOT/tests/coreapp/sha256_cc.cc $ROOT/tests/coreapp/board_stubs.cc"
# the defines and the include path (LV_CONF_INCLUDE_SIMPLE: lv_conf.h comes from the directory the caller puts first)
CORE_DEFS="-std=gnu++14 -w -DVIDEOMODE_NO_BOARD=1 -DLV_CONF_INCLUDE_SIMPLE"
CORE_INCS="-I$ROOT/app/view -I$ROOT/app/video -I$SRC -I$ROOT/uicore -I$CORES -I$ROOT/cores/vc20/app/lib -I$ROOT/app/loaders -I$ROOT/app/lib/cbm1541 -I$ROOT/app/edit -I$ROOT/tests/coreapp -I$ROOT/tests -I$ROOT/host-app/software/io/c64 -I$ROOT/host-app/software/lvgl_port -I$ROOT/host-app/software/filesystem -I$HA"

source "$ROOT/tests/coreapp/vc20_drv.sh"
source "$ROOT/tests/coreapp/own_drv.sh"   # own_drv_dir: the driver table of a core's own app
coreapp_srcs() {
  SRCS=""
  VC20_DRV="${TMPDIR:-/tmp}/vc20drv.$(id -u)"                  # T239.3 V-4: vc20_regs.h names the registers through the VIC-20's driver table
  vc20_drv_dir "$VC20_DRV" || return 1
  CORE_INCS="$CORE_INCS -I$VC20_DRV/drv"
  local f d
  for f in $(sed -n '/^SRCS_CC/,/^$/p' "$MK" | grep -oE '[A-Za-z0-9_]+\.cc'); do
    case " $NOT_HERE " in *" $f "*) continue ;; esac
    for d in $APPDIRS; do [ -f "$d/$f" ] && { SRCS="$SRCS $d/$f"; break; }; done
  done
  [ -n "$SRCS" ] || { echo "coreapp: no sources from $MK" >&2; return 1; }
  # T239.3 P3-1: the libraries are the sources the app's TOML B names (the Makefile takes them from the same file). Only the loader-bound exports (the bind entries: libtab.cc,
  # c64cart_exports.cc ...) stay out of the native build with the loader's start (libboard.cc); a library's `table` file (mouse_exports.cc, pad_exports.cc, padkeys_exports.cc) is
  # linked natively, as libtab_native.cc expects.
  for f in $(python3 "$ROOT/tools/libtab.py" sources "$ROOT/coreapp/libs/shared.toml"); do
    case " $NOT_HERE " in *" $f "*) continue ;; esac
    for d in $APPDIRS; do [ -f "$d/$f" ] && { SRCS="$SRCS $d/$f"; break; }; done
  done
  # T239.3 V-4b, T213.3 (T239.5 D13): a core with an app of its own (tools/core_toml.py lanes: a partial or a large core) is not in the shared image's list. The libraries of its
  # TOML B are compiled here as its own build compiles them, so the render benches and the menu simulator have its descriptor: a shared library once (a source the shared list
  # has is not added twice), each of the core's own sources (cores/<id>/app/lib) in a unit of its own that includes the driver table of the core's TOML A first (own_drv_dir;
  # the table's guard DRVTAB_H keeps out the shared table, which the callers put first on the one include path of their command line: without the unit, vc20.cc took the
  # shared app's table and did not compile, T213.3).
  # T239.5 D13: OWN_ONLY="<core> ..." keeps those cores only (a render build of one core that moved to its own app), OWN_SKIP="<core> ..." leaves those out (the shared
  # render bench builds none of them); neither: every core of the lanes (tools/menusim). OWN_TAG names the build's own directory (own_cores.h differs between the builds).
  local own kind lib w lanes d descs="" h OWN_SEEN=""
  OWN_DIR="${TMPDIR:-/tmp}/owncores.$(id -u)${OWN_TAG:+.$OWN_TAG}"
  lanes="$(python3 "$ROOT/tools/core_toml.py" lanes)" || { echo "srclist: tools/core_toml.py lanes failed" >&2; return 1; }   # first, so a failure is not swallowed
  while read -r own kind; do
    [ -n "$own" ] || continue
    [ -z "${OWN_ONLY:-}" ] || case " $OWN_ONLY " in *" $own "*) ;; *) continue ;; esac
    case " ${OWN_SKIP:-} " in *" $own "*) continue ;; esac
    d="$(python3 "$ROOT/tools/core_toml.py" sdk "$own" desc)" && [ -n "$d" ] || { echo "srclist: cores/$own/app/sdk.toml names no descriptor" >&2; return 1; }
    descs="$descs $d"
    own_drv_dir "$own" "$OWN_DIR/$own" || return 1
    lib="$ROOT/cores/$own/app/lib"
    for f in $(python3 "$ROOT/tools/libtab.py" sources "$ROOT/cores/$own/app/sdk.toml"); do
      case " $NOT_HERE " in *" $f "*) continue ;; esac
      case " $SRCS " in *"/$f "*) continue ;; esac
      if [ -f "$lib/$f" ]; then
        # T239.5 D16: cores that copied one shared file (tape_deck.cc, tape_pulses.cc: the copy rule) each have the same code in their library; one native program takes it once (a second
        # definition of its functions would not link). Same code = same file but for the leading "// Copy of <path> at commit <sha>" line.
        h="$(grep -v '^// Copy of ' "$lib/$f" | cksum | awk '{ print $1 "." $2 }')"
        case " $OWN_SEEN " in *" $f:$h "*) continue ;; esac
        OWN_SEEN="$OWN_SEEN $f:$h"
        w="$OWN_DIR/$own/own_$f"
        printf '// generated by tests/coreapp/srclist.sh (%s, a %s core): its source with the driver table of its own TOML A\n#include "%s"\n#include "%s"\n' \
          "$own" "$kind" "$OWN_DIR/$own/drv/drvtab.h" "$lib/$f" > "$w.new"
        if cmp -s "$w.new" "$w"; then rm -f "$w.new"; else mv "$w.new" "$w"; fi
        SRCS="$SRCS $w"
      else
        for d in $APPDIRS; do [ -f "$d/$f" ] && { SRCS="$SRCS $d/$f"; break; }; done
      fi
    done
  done <<< "$lanes"
  # own_cores.h: their descriptors as one list (the render bench walks it with the board's table; tools/menusim makes the same list in its own directory)
  mkdir -p "$OWN_DIR"
  {
    echo "// generated by tests/coreapp/srclist.sh: the descriptors of the cores with an app of their own (tools/core_toml.py lanes)"
    for w in $descs; do echo "extern const CoreDesc $w;"; done
    echo "static const CoreDesc *const own_cores[] = {"
    for w in $descs; do echo "    &$w,"; done
    echo "    0 };"
  } > "$OWN_DIR/own_cores.h.new"
  if cmp -s "$OWN_DIR/own_cores.h.new" "$OWN_DIR/own_cores.h"; then rm -f "$OWN_DIR/own_cores.h.new"; else mv "$OWN_DIR/own_cores.h.new" "$OWN_DIR/own_cores.h"; fi
  CORE_INCS="$CORE_INCS -idirafter $OWN_DIR"
  SRCS="$SRCS $TEST_SRCS"
}
