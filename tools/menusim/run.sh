#!/usr/bin/env bash
# T236: the menu simulator - the real menus of the core application and the host application natively on this machine, in a window
# (docs/specs/T236-menu-simulator.md).
#
#   tools/menusim/run.sh <core> [--mode M] [--toml FILE] [--ops FILE] [--stick FILE] [--scale N]     a core's menu in the window
#   tools/menusim/run.sh <core> --keys F3,DOWN,RETURN --shot out.png [...]                         headless: the keys, one PNG, end
#   tools/menusim/run.sh <core> --keys K1,K2,... --stats FILE                                       headless: one line of figures per key (T239.3 P7; speed.sh runs it)
#   tools/menusim/run.sh --host [--mode M] [--scale N] [--keys ... --shot out.png]                 the host application's menu
#   tools/menusim/run.sh --tree DIR <core>|--host ...                                               the application of another tree (a worktree)
#   tools/menusim/run.sh --list                                                                     the cores of the core table
#
# The build is the render benches' (tests/coreapp/srclist.sh, tests/hostapp/srclist.sh: the sources and the stubs; tests/lvgl_objs.sh: LVGL's
# objects), one object per source in .ci/menusim/<tree>/ with its dependencies, so a change rebuilds what it touches; nice -n 15, two jobs. The host
# application runs over a fresh tree of tests/hostapp/render/mktree.py each start. A tool: no pipeline stage, no image. Needs a C++ compiler,
# python3 and SDL2 (Mac: brew install sdl2; found with pkg-config, else sdl2-config).
set -euo pipefail
TOOL="$(cd "$(dirname "$0")/../.." && pwd)"
TREE="$TOOL"
HOST=""
args=()
while [ $# -gt 0 ]; do
  case "$1" in
    --tree) TREE="$(cd "${2:?--tree needs a directory}" && pwd)"; shift 2 ;;
    --host) HOST=1; shift ;;
    *) args+=("$1"); shift ;;
  esac
done
[ -n "$HOST" ] || [ ${#args[@]} -gt 0 ] || { sed -n 2,15p "$0" | sed 's/^# \{0,1\}//'; exit 2; }
ROOT="$TREE"

if pkg-config --exists sdl2 2>/dev/null; then
  SDL_CFLAGS="$(pkg-config --cflags sdl2)"; SDL_LIBS="$(pkg-config --libs sdl2)"
elif command -v sdl2-config >/dev/null 2>&1; then
  SDL_CFLAGS="$(sdl2-config --cflags)"; SDL_LIBS="$(sdl2-config --libs)"
else
  echo "menusim: SDL2 not found - brew install sdl2 (Windows/MSYS2: pacman -S mingw-w64-x86_64-SDL2)" >&2; exit 1
fi
key="$(printf '%s' "$ROOT" | shasum | cut -c1-12)"
BD="$TOOL/.ci/menusim/$key"
LVSH="$ROOT/tests/lvgl_objs.sh"; [ -x "$LVSH" ] || LVSH="$TOOL/tests/lvgl_objs.sh"
now() { python3 -c 'import time; print("%.1f" % time.time())'; }

# gen NAME MAIN "FLAGS" "SOURCES" LVGL_OBJS: $BD/<NAME>/Makefile, one object per source and the program NAME linked from them, MAIN and LVGL
gen() {
  local name="$1" main="$2" flags="$3" srcs="$4" lvobjs="$5" d="$BD/$1" s o
  mkdir -p "$d/obj"
  {
    echo "FLAGS := $flags"
    echo "SDL_CFLAGS := $SDL_CFLAGS"
    echo "OBJ :="
    for s in $srcs; do
      o="obj/$(printf '%s' "${s#"$ROOT"/}" | tr '/' '_' | sed -E 's/\.(cc|c)$/.o/')"
      echo "OBJ += $o"
      printf '%s: %s\n\t$(CXX) $(FLAGS) -c $< -o $@\n' "$o" "$s"
    done
    printf 'obj/%s.o: %s\n\t$(CXX) $(FLAGS) $(SDL_CFLAGS) -c $< -o $@\n' "$name" "$main"
    printf '%s: $(OBJ) obj/%s.o\n\t$(CXX) -o $@ $(OBJ) obj/%s.o %s/*.o %s -lz -lm -pthread\n' "$name" "$name" "$name" "$lvobjs" "$SDL_LIBS"
    echo "-include \$(OBJ:.o=.d) obj/$name.d"
  } > "$d/Makefile.new"
  if cmp -s "$d/Makefile.new" "$d/Makefile"; then rm "$d/Makefile.new"; else mv "$d/Makefile.new" "$d/Makefile"; fi
  touch "$d/.stamp"
  local t0; t0=$(now)
  nice -n 15 make -s -C "$d" -j2 "$name"
  echo "menusim: $(find "$d/obj" -name '*.o' | wc -l | tr -d ' ') objects, $(find "$d/obj" -name '*.o' -newer "$d/.stamp" | wc -l | tr -d ' ') rebuilt," \
       "linked in $(python3 -c "print('%.1f' % ($(now) - $t0))") s"
}

# T239.3 D32: the rows' tab, group and order are the base UI TOML's (coreapp/base_ui.toml, a core's menu.toml places over it): the table the simulator links is made of that file now
if [ -z "$HOST" ] && [ -f "$ROOT/tools/gen_base_ui.py" ]; then
  python3 "$ROOT/tools/gen_base_ui.py" --check > /dev/null 2>&1 || python3 "$ROOT/tools/gen_base_ui.py" > /dev/null
fi

if [ -n "$HOST" ]; then
  [ -f "$ROOT/tests/hostapp/srclist.sh" ] || { echo "menusim: $ROOT has no tests/hostapp/srclist.sh (older than T236): --host needs it" >&2; exit 1; }
  # shellcheck source=/dev/null
  source "$ROOT/tests/hostapp/srclist.sh"
  hostapp_srcs
  # the tree the host runs over, fresh each start: what a run writes (boot.cfg, the menu picture) does not carry into the next
  rm -rf "$BD/host-tree" "$BD/host-tree_"*
  mkdir -p "$BD"
  python3 "$ROOT/tests/hostapp/render/mktree.py" "$BD/host-tree" > /dev/null
  FW_SHA="$(python3 -c 'import hashlib,sys; print(hashlib.sha256(open(sys.argv[1],"rb").read()).hexdigest())' "$BD/host-tree_cv/Usb0/fw/vendor.ue2")"
  mkdir -p "$BD/hostsim/conf"
  cmp -s "$H/lv_conf.h" "$BD/hostsim/conf/lv_conf.h" || cp "$H/lv_conf.h" "$BD/hostsim/conf/lv_conf.h"
  OBJS="$(nice -n 15 "$LVSH" "$L" "$H/lv_conf.h")"
  gen hostsim "$TOOL/tools/menusim/hostsim.cc" \
    "$HOST_DEFS -O0 -g -DHOSTAPP_FLASH_LOCK=1 -DFW_TEST_SHA=$FW_SHA -I$BD/hostsim/conf -I$ROOT/tests/hostapp/render/stub $HOST_INCS -I$ROOT/tests/hostapp -MMD -MP" \
    "$H/hostui.cc $H/discover.cc $H/cfw.cc $H/cfwicon.cc $HOST_SRCS" "$OBJS"
  exec "$BD/hostsim/hostsim" --tree "$BD/host-tree" ${args[@]+"${args[@]}"}
fi

# The tree's own source list; a tree older than T236 (no srclist.sh) gets this tool's, with APPDIRS and NOT_HERE read from its bench.
if [ -f "$ROOT/tests/coreapp/srclist.sh" ]; then
  # shellcheck source=/dev/null
  source "$ROOT/tests/coreapp/srclist.sh"
else
  # shellcheck source=../../tests/coreapp/srclist.sh
  source "$TOOL/tests/coreapp/srclist.sh"
  eval "$(grep -E '^(APPDIRS|NOT_HERE)=' "$ROOT/tests/coreapp/render/run.sh")"
  echo "menusim: $ROOT has no tests/coreapp/srclist.sh: its bench's list is used, menu_rig.h comes from $TOOL"
fi
coreapp_srcs
L="${ULTIMATE_SW:-$ROOT/vendor/gideon/software}/lvgl"
OBJS="$(nice -n 15 "$LVSH" "$L" "$HA/lv_conf.h")"
mkdir -p "$BD/menusim/conf"
# T239.3 D31: the driver table the drive library and the SID library name their registers through (the same call as tests/coreapp/run.sh)
python3 "$ROOT/tools/descrec.py" drivers "$ROOT/coreapp/gateware/shared.toml" "$BD/drv" --maps "$ROOT/app/sdkpkg/maps" > /dev/null
cmp -s "$HA/lv_conf.h" "$BD/menusim/conf/lv_conf.h" || cp "$HA/lv_conf.h" "$BD/menusim/conf/lv_conf.h"
gen menusim "$TOOL/tools/menusim/menusim.cc" \
  "$CORE_DEFS -O0 -g -DGOLDEN_DIR=\\\"$ROOT/tests/coreapp/golden\\\" -I$BD/menusim/conf -I$BD/drv -iquote $L $CORE_INCS -idirafter $TOOL/tests/coreapp -MMD -MP" \
  "$SRCS" "$OBJS"
exec "$BD/menusim/menusim" --root "$ROOT" "${args[@]}"
