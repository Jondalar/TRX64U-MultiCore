#!/bin/sh
# T113: the machine with the REAL C128 ROMs (local bench, never in CI: the ROM files live outside the repository). Boots the Z80 BIOS, hands over
# to the 8502, runs the KERNAL's reset and prints the 40-column screen at $0400 ("scr|" lines); PASS when the BASIC 7.0 banner is on it.
#   C128_ROMDIR=<dir with the VICE-named ROM files> sh cores/c128/sim/run_realrom.sh [ms]     (default 3500 ms of simulated time)
set -u
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../.." && pwd)
build=${C128_BUILD:-$here/build}
: "${C128_ROMDIR:?set C128_ROMDIR to the directory with the ROM files}"
[ -x "$build/obj/tb_c128" ] || sh "$here/build_machine.sh" "$build" || exit 1
run="$build/run_real"
rm -rf "$run"; mkdir -p "$run"; cp -a "$build/run/." "$run/"
python3 "$root/tools/c128_rom_image.py" "$C128_ROMDIR" "$run/mem/c128_rom.mem" --mem || exit 1
(cd "$run" && "$build/obj/tb_c128" +MS="${1:-3500}" +SCREEN ${C128_ARGS:-}) > "$run/log" 2>&1
grep -E "^(cycles|scr\||vdc\|)" "$run/log" | grep -vE "^(scr|vdc)\| *$"
if grep -q "BASIC V7.0" "$run/log"; then echo "PASS run_realrom"; else echo "FAIL run_realrom: no BASIC V7.0 banner on the screen"; exit 1; fi
