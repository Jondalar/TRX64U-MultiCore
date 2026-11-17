#!/usr/bin/env bash
# T236: the menu simulator - the core application's real menu natively on this machine, in a window (docs/specs/T236-menu-simulator.md).
#
#   tools/menusim/run.sh <core> [--mode M] [--toml FILE] [--ops FILE] [--stick FILE] [--scale N]     the window
#   tools/menusim/run.sh <core> --keys F3,DOWN,RETURN --shot out.png [...]                         headless: the keys, one PNG, end
#   tools/menusim/run.sh --tree DIR <core> ...                                                      the application of another tree (a worktree)
#   tools/menusim/run.sh --list                                                                     the cores of the core table
#
# The build is the render bench's (tests/coreapp/srclist.sh: the core app's Makefile list, the stubs; tests/lvgl_objs.sh: LVGL's objects), one
# object per source in .ci/menusim/<tree>/ with its dependencies, so a change rebuilds what it touches; nice -n 15, two jobs. A tool: no pipeline
# stage, no image. Needs a C++ compiler, python3 and SDL2 (Mac: brew install sdl2; found with pkg-config, else sdl2-config).
set -euo pipefail
TOOL="$(cd "$(dirname "$0")/../.." && pwd)"
TREE="$TOOL"
args=()
while [ $# -gt 0 ]; do
  case "$1" in
    --tree) TREE="$(cd "${2:?--tree needs a directory}" && pwd)"; shift 2 ;;
    *) args+=("$1"); shift ;;
  esac
done
[ ${#args[@]} -gt 0 ] || { sed -n 2,11p "$0" | sed 's/^# \{0,1\}//'; exit 2; }
ROOT="$TREE"

if pkg-config --exists sdl2 2>/dev/null; then
  SDL_CFLAGS="$(pkg-config --cflags sdl2)"; SDL_LIBS="$(pkg-config --libs sdl2)"
elif command -v sdl2-config >/dev/null 2>&1; then
  SDL_CFLAGS="$(sdl2-config --cflags)"; SDL_LIBS="$(sdl2-config --libs)"
else
  echo "menusim: SDL2 not found - brew install sdl2 (Windows/MSYS2: pacman -S mingw-w64-x86_64-SDL2)" >&2; exit 1
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
LVSH="$ROOT/tests/lvgl_objs.sh"; [ -x "$LVSH" ] || LVSH="$TOOL/tests/lvgl_objs.sh"
OBJS="$(nice -n 15 "$LVSH" "$L" "$HA/lv_conf.h")"

key="$(printf '%s' "$ROOT" | shasum | cut -c1-12)"
BD="$TOOL/.ci/menusim/$key"
mkdir -p "$BD/obj" "$BD/conf"
cmp -s "$HA/lv_conf.h" "$BD/conf/lv_conf.h" || cp "$HA/lv_conf.h" "$BD/conf/lv_conf.h"
FLAGS="$CORE_DEFS -O0 -g -DGOLDEN_DIR=\\\"$ROOT/tests/coreapp/golden\\\" -I$BD/conf -iquote $L $CORE_INCS -idirafter $TOOL/tests/coreapp -MMD -MP"
{
  echo "FLAGS := $FLAGS"
  echo "SDL_CFLAGS := $SDL_CFLAGS"
  echo "OBJ :="
  for s in $SRCS; do
    o="obj/$(printf '%s' "${s#"$ROOT"/}" | tr '/' '_' | sed 's/\.cc$/.o/')"
    echo "OBJ += $o"
    printf '%s: %s\n\t$(CXX) $(FLAGS) -c $< -o $@\n' "$o" "$s"
  done
  printf 'obj/menusim.o: %s\n\t$(CXX) $(FLAGS) $(SDL_CFLAGS) -c $< -o $@\n' "$TOOL/tools/menusim/menusim.cc"
  printf 'menusim: $(OBJ) obj/menusim.o\n\t$(CXX) -o $@ $(OBJ) obj/menusim.o %s/*.o %s -lz -lm -pthread\n' "$OBJS" "$SDL_LIBS"
  echo '-include $(OBJ:.o=.d) obj/menusim.d'
} > "$BD/Makefile.new"
cmp -s "$BD/Makefile.new" "$BD/Makefile" && rm "$BD/Makefile.new" || mv "$BD/Makefile.new" "$BD/Makefile"

touch "$BD/.stamp"
now() { python3 -c 'import time; print("%.1f" % time.time())'; }
t0=$(now)
nice -n 15 make -s -C "$BD" -j2 menusim
n_all=$(find "$BD/obj" -name '*.o' | wc -l | tr -d ' ')
n_new=$(find "$BD/obj" -name '*.o' -newer "$BD/.stamp" | wc -l | tr -d ' ')
echo "menusim: $n_all objects, $n_new rebuilt, linked in $(python3 -c "print('%.1f' % ($(now) - $t0))") s"
exec "$BD/menusim" --root "$ROOT" "${args[@]}"
