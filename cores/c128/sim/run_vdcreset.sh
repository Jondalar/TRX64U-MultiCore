#!/bin/sh
# T140 5.17: 80 columns (+D80), boot to READY on the VDC, machine reset (the core's reset request, what the case rocker and the menu's Reset pulse), READY again?
# Local bench with the REAL ROMs (never in CI): C128_ROMDIR=<dir with the VICE-named ROM files> sh cores/c128/sim/run_vdcreset.sh [real real_cache]
set -u
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../.." && pwd)
build=${C128_BUILD:-$here/build}
: "${C128_ROMDIR:?set C128_ROMDIR to the directory with the ROM files}"
modes=${*:-"real"}
base="Z80 SID_REDIP NO_TURBO NO_REU VDC_WHOLE VDC64 TRX_SIM_ROMS"
T1=${VR_T1:-7000000}     # us: the screen before the reset
T2=${VR_T2:-14000000}    # us: the screen after
rc=0
for m in $modes; do
  case $m in
    real)        defs="$base" ;;
    real_cache)  defs="$base ROM_CACHE" ;;
    *) echo "unknown mode $m"; exit 2 ;;
  esac
  b="$build.vdcrst_$m"
  [ -x "$b/obj/tb_c128" ] || C128_SIM_DEFS="$defs" sh "$here/build_machine.sh" "$b" || { echo "FAIL $m: build"; rc=1; continue; }
  run="$b/run_real"; rm -rf "$run"; mkdir -p "$run"; cp -a "$b/run/." "$run/"
  python3 "$root/tools/c128_rom_image.py" "$C128_ROMDIR" "$run/mem/c128_rom.mem" --mem > /dev/null || exit 1
  (cd "$run" && "$b/obj/tb_c128" +D80 +MS=$((T2 / 1000)) +VSHOT_AT=$((T1 - 100000)) +RESET_AT=$T1 +RESET_US=50000 +VDCSCREEN ${VR_ARGS:-}) > "$run/log" 2>&1
  pre=$(awk '/^evt\| vdc screen/ { f = 1; next } f && /^vdc\|/ { print; n++ } n >= 25 { exit }' "$run/log")
  post=$(awk '/^cycles:/ { f = 1; next } f && /^vdc\|/ { print }' "$run/log")
  echo "$m, before the reset ($((T1 / 1000)) ms):"; echo "$pre" | grep -v '^vdc| *$' | cat
  echo "$m, after the reset:"; echo "$post" | grep -v '^vdc| *$' | cat
  a=$(echo "$pre" | grep -c "READY\."); v7=$(echo "$post" | grep -c "BASIC V7.0"); r=$(echo "$post" | grep -c "READY\.")
  if [ "$a" -ge 1 ] && [ "$v7" -ge 1 ] && [ "$r" -ge 1 ]; then echo "PASS $m: READY. before and after the reset"; else echo "FAIL $m: READY before $a, banner after $v7, READY after $r"; rc=1; fi
done
[ $rc = 0 ] && echo "PASS run_vdcreset" || echo "FAIL run_vdcreset"
exit $rc
