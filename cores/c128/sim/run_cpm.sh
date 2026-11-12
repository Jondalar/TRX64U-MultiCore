#!/bin/sh
# T140 5.19 (issue #67): CP/M Plus from a real .d64 on a real 1541 (c64_c1541 in the bench, -DSIM_DRIVE) with the REAL C128 ROMs and the VDC probe (+VDCPROBE).
# Local bench, never in CI (the ROM files live outside the repository):
#   C128_ROMDIR=<dir with the VICE-named ROM files + 1541.rom> sh cores/c128/sim/run_cpm.sh [disk.d64] [ms]     (default media/cpm.system.622-3297432.d64, 60000 ms)
# Log: $build/run_cpm/log ("vp|" probe lines, "vdc|" the VDC text screen at the end).
set -u
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../.." && pwd)
build=${C128_BUILD:-$here/build.cpm}
: "${C128_ROMDIR:?set C128_ROMDIR to the directory with the ROM files and 1541.rom}"
disk=${1:-$here/media/cpm.system.622-3297432.d64}
ms=${2:-60000}
[ -x "$build/obj/tb_c128" ] || C128_SIM_DEFS="Z80 SID_REDIP NO_TURBO NO_REU VDC_WHOLE VDC64 TRX_SIM_ROMS SIM_DRIVE" sh "$here/build_machine.sh" "$build" || exit 1
run="$build/run_cpm"; rm -rf "$run"; mkdir -p "$run"; cp -a "$build/run/." "$run/"
python3 "$root/tools/c128_rom_image.py" "$C128_ROMDIR" "$run/mem/c128_rom.mem" --mem > /dev/null || exit 1
python3 "$root/tools/c128_mem_from_bin.py" "$C128_ROMDIR/1541.rom" "$run/mem/d1541.mem" --mirror 32768 || exit 1
python3 "$root/tools/c128_mem_from_bin.py" "$disk" "$run/mem/disk.mem" || exit 1
(cd "$run" && "$build/obj/tb_c128" +MS="$ms" +DRIVE +VDCPROBE +VDCSCREEN ${CPM_ARGS:-}) > "$run/log" 2>&1
grep -c '^vp|.*|cw|' "$run/log" | sed 's/^/vdc cpu writes: /'
grep -c '^vp|.*|zw|' "$run/log" | sed 's/^/z80 writes to D6xx: /'
grep -c 'DROP-PENDING' "$run/log" | sed 's/^/pending actions replaced: /'
grep '^vdc|' "$run/log" | grep -v '^vdc| *$' | tail -30
