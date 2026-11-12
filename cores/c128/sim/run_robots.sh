#!/bin/sh
# c128-vicglitch: PETSCII Robots C128 (cores/c128/sim/media/128-robots.d81) in the whole machine with the REAL ROMs (local, never in CI: C128_ROMDIR).
# The machine boots to the BASIC prompt (+NODEV: no device on the serial bus, else the KERNAL's boot attempt waits for a drive forever), at INJECT_AT the RAM image the game's loader leaves (sim/vicglitch/mk_inject.py, both RAM blocks) is written
# into the RAM, RETURN is pressed once, BASIC's main loop jumps through IMAIN into the loader's tail ($8E80) and the game starts (VIC: RCR $45, RAM block 1).
#   C128_ROMDIR=<dir with the VICE-named ROM files> sh cores/c128/sim/run_robots.sh <bram|ddr> [ms]
# C128_LEVEL=LEVEL-A-128 (T140 D30): the level file is injected where the game's LOAD puts it and the LOAD call is skipped, so the game runs on into the level; C128_ARGS=+VICEVT logs the slot-4 events.
# T140 D28: `cmp` [ms] runs bram and ddr and checks: ddr shows the bram picture (pixel hash of frame C128_FRAME, default 150); the residual of $FF
# block-1 reads (+VICCHK, "vicwin|") is only the sweep window.
# Frames <F>..<F>+<N>-1 (C128_FRAME, C128_FRAMES) are written as the VIC's pixel stream (pixdump.txt); sim/vicglitch/render.py makes pictures of them.
set -u
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../.." && pwd)
mode=${1:-ddr}; ms=${2:-6500}
if [ "$mode" = cmp ]; then
  rc=0
  for m in bram ddr; do
    sh "$0" $m $ms > "${TMPDIR:-/tmp}/robots_$m.out" 2>&1
    h=$(grep '^pixhash|' "${TMPDIR:-/tmp}/robots_$m.out" | head -1 | cut -d'|' -f3)
    bad=$(grep '^vicchk|' "${TMPDIR:-/tmp}/robots_$m.out" | head -1 | cut -d'|' -f5)
    echo "$m: pixel hash ${h:-none}, block-1 reads wrong ${bad:-n/a}; $(grep '^vicchk|' "${TMPDIR:-/tmp}/robots_$m.out" | head -1) $(grep '^vicwin|' "${TMPDIR:-/tmp}/robots_$m.out" | head -1)"
    eval "h_$m=\${h:-none}; b_$m=\${bad:-0}"
  done
  [ "$h_bram" != none ] && [ "$h_ddr" = "$h_bram" ] && echo "PASS ddr: the game picture is the bram picture ($h_bram), $b_ddr block-1 reads wrong" || { echo "FAIL ddr: $h_ddr against bram $h_bram"; rc=1; }
  exit $rc
fi
: "${C128_ROMDIR:?set C128_ROMDIR to the directory with the ROM files}"
build=${C128_BUILD:-$here/build}.vb_$mode
base="Z80 SID_REDIP NO_TURBO NO_REU VDC_WHOLE VDC64 TRX_SIM_ROMS ROM_CACHE"
case $mode in bram) defs="$base";; ddr) defs="$base RAM_DDR2"; esac
[ -x "$build/obj/tb_c128" ] || C128_SIM_DEFS="$defs" sh "$here/build_machine.sh" "$build" || exit 1
run="$build/run_robots"; rm -rf "$run"; mkdir -p "$run"; cp -a "$build/run/." "$run/"
python3 "$root/tools/c128_rom_image.py" "$C128_ROMDIR" "$run/mem/c128_rom.mem" --mem || exit 1
files="$build/robots_files"; rm -rf "$files"; mkdir -p "$files"
python3 -I "$here/vicglitch/d81x.py" "$here/media/128-robots.d81" "$files" > "$build/robots_dir.txt" || exit 1
python3 -I "$here/vicglitch/mk_inject.py" "$files" "$run/mem" ${C128_START:-start} ${C128_LEVEL:-} || exit 1
devargs=+NODEV
if [ -n "${C128_DEV:-}" ]; then
  # T140 D31: C128_DEV=<file in the d81, e.g. LEVEL-A-128> [C128_DEVLEN=<bytes>, default the whole file]: the bus-level device 8 of tb_c128.v serves it (+DEV8), +IOLOG logs the VIC bank/RCR writes
  python3 -I -c "
import sys
d = open(sys.argv[1] + '/' + sys.argv[2], 'rb').read()
n = int(sys.argv[4]) if len(sys.argv) > 4 and sys.argv[4] else len(d)
open(sys.argv[3], 'w').write('\n'.join('%02x' % x for x in d[:n]) + '\n')
print(n)" "$files" "$C128_DEV" "$run/mem/devfile.mem" "${C128_DEVLEN:-}" > "$run/devlen" || exit 1
  devargs="+DEV8 +DEVAT=${C128_INJECT_US:-2500000} +DEVLEN=$(cat "$run/devlen") +IOLOG"
fi
(cd "$run" && "$build/obj/tb_c128" +MS="$ms" +INJECT_AT=${C128_INJECT_US:-2500000} +KEYS=2 +T0=$((${C128_INJECT_US:-2500000} / 1000 + 100)) \
    +KEYOFF=$((${C128_INJECT_US:-2500000} + 250000)) +PIXHASH=${C128_FRAME:-150} +PIXDUMP=${C128_FRAME:-150} +PIXDUMPN=${C128_FRAMES:-3} $devargs +VICCHK +FRAMES +PROBE=100 +SCREEN \
    +DUMP=${C128_DUMP:-10000} +DUMPN=4 ${C128_ARGS:-}) > "$run/log" 2>&1
grep -E "^(dev|io|iosum|take|vtag|evsum|evt|cycles|ramd|vicchk|vicwin|vicbad|pixhash|ram\||probe|last|scr)" "$run/log" | head -600
grep -E "^(PASS|FAIL) tb_c128" "$run/log"
