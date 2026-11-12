#!/bin/sh
# T113 5.6: the board's sequence with the REAL C128 ROMs (local bench like run_realrom.sh, never in CI: the ROM files live outside the repository):
# C= held at the start -> the C64 mode (banner "COMMODORE 64 BASIC V2"), C= released, a machine reset -> the C128 mode again ("COMMODORE BASIC V7.0").
#   C128_ROMDIR=<dir with the VICE-named ROM files> sh cores/c128/sim/run_realrom_reset.sh [real real_cache]
# Simulated time per mode: 4,500 ms (about 16 minutes on the builder without the ROM cache; the C64 KERNAL's RAM test alone takes 1.5 s of it).
set -u
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../.." && pwd)
build=${C128_BUILD:-$here/build}
: "${C128_ROMDIR:?set C128_ROMDIR to the directory with the ROM files}"
modes=${*:-"real"}
base="Z80 SID_REDIP NO_TURBO NO_REU VDC_WHOLE VDC64 TRX_SIM_ROMS"
rc=0
for m in $modes; do
  case $m in
    real)        defs="$base" ;;
    real_cache)  defs="$base ROM_CACHE" ;;                # the board's build: the ROM behind the line cache (T113 5.7: a miss holds the CPU only)
    *) echo "unknown mode $m"; exit 2 ;;
  esac
  b="$build.realrst_$m"
  [ -x "$b/obj/tb_c128" ] || C128_SIM_DEFS="$defs" sh "$here/build_machine.sh" "$b" || { echo "FAIL $m: build"; rc=1; continue; }
  run="$b/run_real"; rm -rf "$run"; mkdir -p "$run"; cp -a "$b/run/." "$run/"
  python3 "$root/tools/c128_rom_image.py" "$C128_ROMDIR" "$run/mem/c128_rom.mem" --mem > /dev/null || exit 1
  # C= is key 61 of the matrix (col 7, row 5); held for the first 100 ms (the Z80 BIOS reads it in the first 400 us), the C64 KERNAL has its READY by
  # about 2.7 s, the screen is read at 3.0 s, the machine reset comes at 3.1 s and lasts 50 ms like the application's
  (cd "$run" && "$b/obj/tb_c128" +MS=4500 +KEYS=2000000000000000 +KEYOFF=100000 +SHOT_AT=3000000 +RESET_AT=3100000 +RESET_US=50000 +SCREEN) > "$run/log" 2>&1
  first=$(awk '/^evt\| screen at/ { f = 1; next } f && /^scr\|/ { print; n++ } n >= 25 { exit }' "$run/log")
  last=$(awk '/^cycles:/ { f = 1; next } f && /^scr\|/ { print }' "$run/log")
  echo "$m, 3.0 s (C= was held at the start): $(echo "$first" | grep -c "COMMODORE 64 BASIC V2") line(s) with the C64 banner"
  echo "$first" | grep -v '^scr| *$' | head -6 | cat
  echo "$m, 4.5 s (after the machine reset at 3.1 s):"
  echo "$last" | grep -v '^scr| *$' | head -6 | cat
  c64=$(echo "$first" | grep -c "COMMODORE 64 BASIC V2")
  v7=$(echo "$last" | grep -c "BASIC V7.0")
  if [ "$c64" -ge 1 ] && [ "$v7" -ge 1 ]; then echo "PASS $m: C64 mode first, the C128's BASIC V7.0 after the reset"; else echo "FAIL $m: C64 banner lines $c64, V7.0 lines $v7"; rc=1; fi
done
[ $rc = 0 ] && echo "PASS run_realrom_reset" || echo "FAIL run_realrom_reset"
exit $rc
