#!/bin/sh
# T113 5.7: the character ROM store (c128_chr.v) gives the VIC and the CPU the bytes of the system ROM image's character ROMs, at the moments the VIC takes them.
# Program chrshow (tools/gen_c128_test.py): C128 mode, a text screen of all 256 screen codes in the C128 character ROM. Checks:
#   1  every character ROM byte the arbiter selected with a steady address (3 clocks) is the image's byte (0x10000 + set + address): "chrchk|checks|wrong",
#   2  the VIC's pixel stream of frame 6 hashes to GOLDEN, the hash the same program gave before 5.7, when the character ROM was read from the ROM
#      block through the memory command (so the VIC samples the glyph bytes at the same moments as then; the screen is static from the third frame).
# GOLDEN is the hash of the build of commit e33910793 (the last before 5.7), non-cache build, same program, same plusarg: see spec 5.7.
# Run from the repository root in trx64-sim-env (Verilator).  sh cores/c128/sim/run_chr.sh [chr]
set -u
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../.." && pwd)
build=${C128_BUILD:-$here/build}
modes=${*:-"chr"}
GOLDEN=${CHR_GOLDEN:-0dedf578}
base="Z80 SID_REDIP NO_TURBO NO_REU VDC_WHOLE VDC64 TRX_SIM_ROMS"
rc=0
for m in $modes; do
  case $m in
    chr)      defs="$base" ;;
    *) echo "unknown mode $m"; exit 2 ;;
  esac
  b="$build.chr_$m"
  [ -x "$b/obj/tb_c128" ] || C128_SIM_DEFS="$defs" sh "$here/build_machine.sh" "$b" || { echo "FAIL $m: build"; rc=1; continue; }
  run="$b/run_chrshow"; rm -rf "$run"; mkdir -p "$run"; cp -a "$b/run/." "$run/"
  exp=$(python3 "$root/tools/gen_c128_test.py" chrshow "$run") || { echo "FAIL $m: generator"; rc=1; continue; }
  (cd "$run" && "$b/obj/tb_c128" +MS="${C128_MS:-160}" +EXPECT="$exp" +PIXHASH=6) > "$run/log" 2>&1
  ck=$(grep '^chrchk|' "$run/log" | head -1); nck=$(echo "$ck" | cut -d'|' -f2); nbad=$(echo "$ck" | cut -d'|' -f3)
  hs=$(grep '^pixhash|' "$run/log" | head -1 | cut -d'|' -f3)
  prog=PASS; grep -q "^PASS tb_c128" "$run/log" || prog=FAIL
  c1=PASS; { [ "${nck:-0}" -ge 1000 ] && [ "${nbad:-1}" -eq 0 ]; } || c1=FAIL
  c2=PASS; [ "${hs:-x}" = "$GOLDEN" ] || c2=FAIL
  echo "$m: program $prog, ${nck:-0} character ROM bytes checked, ${nbad:-?} wrong (check 1 $c1); pixel hash of frame 6 ${hs:-?} against $GOLDEN (check 2 $c2)"
  if [ "$c1" = PASS ] && [ "$c2" = PASS ] && [ "$prog" = PASS ]; then echo "PASS $m"; else echo "FAIL $m"; rc=1; fi
done
[ $rc = 0 ] && echo "PASS run_chr" || echo "FAIL run_chr"
exit $rc
