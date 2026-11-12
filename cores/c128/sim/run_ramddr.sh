#!/bin/sh
# T140 D25: the whole machine with the RAM in the DDR2 (RAM_DDR2: c128_ramd.v, c128_ddrx.v) against the block RAM build (same ROM cache, same synthetic ROM).
# Machine programs (tools/gen_c128_test.py) must give the same RAM results; program vicstress (bitmap mode, 8 sprites, matrix/bitmap/sprites in VIC banks 0 and 1,
# CIA 2 bank switches at raster lines 96 and 176 of every frame, a CIA-timed loop) is run on both builds and
#   1  the VIC's pixel stream of frame 10 hashes the same,
#   2  the known loop takes the same number of CIA 1 timer clocks in pass 2 ($0320/$0330) - the CPU is not slowed once the code is in the caches,
#   3  the VIC missed nothing after the hashed frame (the banks are resident): ramd|vic_miss at the end = at the end of frame 10; none wrong ($FF) shows in 1,
#   4  no write was lost in the write FIFO.
# ddr_slow: DDR_LATMIN/DDR_LATRND slow run (worst case reads 90..150 ui clocks = 1.2-2 us: must still pass 1, 3, 4; check 2 is not asked of it).
# Run from the repository root in trx64-sim-env (Verilator).  sh cores/c128/sim/run_ramddr.sh [modes]
set -u
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../.." && pwd)
build=${C128_BUILD:-$here/build}
# lines 94..99 and 174..179 are left out of the hash: the program polls the raster line and switches CIA 2's bank bits when it sees line 96 / 176, which lands on a cycle of
# that line that depends on the cycles the CPU lost before (a miss holds it): measured, the only line that differed between the two builds was 176. Every other line, and the
# banks' effect from the next line on, must be identical.
C128_SKIP=${C128_SKIP-94,100,174,180}
modes=${*:-"bram ddr ddr_slow progs calib"}
base="Z80 SID_REDIP NO_TURBO NO_REU VDC_WHOLE VDC64 TRX_SIM_ROMS ROM_CACHE"
rc=0
runprog() { # runprog <mode> <program> <extra args...> -> log in $b/run_<program>/log
  b="$build.ddr_$1"; prog=$2; shift 2
  run="$b/run_$prog"; rm -rf "$run"; mkdir -p "$run"; cp -a "$b/run/." "$run/"
  exp=$(python3 "$root/tools/gen_c128_test.py" "$prog" "$run") || { echo "FAIL generator $prog"; return 1; }
  (cd "$run" && "$b/obj/tb_c128" +EXPECT="$exp" "$@") > "$run/log" 2>&1
}
buildmode() { # buildmode <mode> <defs>
  b="$build.ddr_$1"
  [ -x "$b/obj/tb_c128" ] || C128_SIM_DEFS="$2" sh "$here/build_machine.sh" "$b" || { echo "FAIL $1: build"; return 1; }
}
getv() { grep "^$2|" "$1/log" | head -1; }
stress() { # stress <mode> -> prints hash|loop|vic_miss|vm_at|wlost
  skip=""; [ -n "${C128_SKIP:-}" ] && { set -- "$1" $(echo "$C128_SKIP" | tr , " "); skip="+PIXSKIP_LO=$2 +PIXSKIP_HI=$3 +PIXSKIP2_LO=$4 +PIXSKIP2_HI=$5"; }
  runprog "$1" vicstress +MS="${C128_MS:-260}" +PIXHASH=10 +PIXLINES=1 $skip +DUMP=0320 +DUMPN=4 || return 1
  l="$build.ddr_$1/run_vicstress/log"
  h=$(grep '^pixhash|' "$l" | head -1 | cut -d'|' -f3)
  lp=$(grep '^ram|0320|' "$l" | head -1 | cut -d'|' -f3 | cut -d' ' -f2)
  hp=$(grep '^ram|0330|' "$l" | head -1 | cut -d'|' -f3 | cut -d' ' -f2)
  ok=PASS; grep -q '^PASS tb_c128' "$l" || ok=FAIL
  rm_=$(grep '^ramd|' "$l" | head -1)
  echo "$h|${hp}${lp}|$(echo "$rm_" | cut -d'|' -f2)|$(echo "$rm_" | cut -d'|' -f6)|$(echo "$rm_" | cut -d'|' -f5)|$ok"
}
ref=""
for m in $modes; do
  case $m in
    bram)        buildmode bram "$base" || { rc=1; continue; }; ref=$(stress bram); echo "bram:        hash|loop|vic_miss|vm_at|wlost|program = $ref" ;;
    ddr)         buildmode ddr "$base RAM_DDR2" || { rc=1; continue; }; r=$(stress ddr); echo "ddr:         hash|loop|vic_miss|vm_at|wlost|program = $r"
                 [ -n "$ref" ] || ref=$(stress bram 2>/dev/null)
                 h0=$(echo "$ref" | cut -d'|' -f1); l0=$(echo "$ref" | cut -d'|' -f2)
                 h1=$(echo "$r" | cut -d'|' -f1); l1=$(echo "$r" | cut -d'|' -f2); vm=$(echo "$r" | cut -d'|' -f3); va=$(echo "$r" | cut -d'|' -f4); wl=$(echo "$r" | cut -d'|' -f5); pr=$(echo "$r" | cut -d'|' -f6)
                 c1=PASS; [ -n "$h1" ] && [ "$h1" = "$h0" ] || c1=FAIL
                 c2=PASS; [ -n "$l1" ] && [ "$l1" = "$l0" ] || c2=FAIL
                 c3=PASS; [ -n "$vm" ] && [ "$vm" = "$va" ] || c3=FAIL
                 c4=PASS; [ "${wl:-1}" = 0 ] || c4=FAIL
                 echo "ddr: pixel hash of frame 10 $h1 against bram $h0 (check 1 $c1); loop count $l1 against $l0 (check 2 $c2); VIC misses $vm at the end, $va after frame 10 (check 3 $c3); writes lost $wl (check 4 $c4); program $pr"
                 if [ $c1 = PASS ] && [ $c2 = PASS ] && [ $c3 = PASS ] && [ $c4 = PASS ] && [ "$pr" = PASS ]; then echo "PASS ddr"; else echo "FAIL ddr"; rc=1; fi ;;
    ddr_slow)    buildmode ddr_slow "$base RAM_DDR2 DDR_LATMIN=90 DDR_LATRND=60" || { rc=1; continue; }; r=$(stress ddr_slow); echo "ddr_slow:    hash|loop|vic_miss|vm_at|wlost|program = $r"
                 h0=$(echo "$ref" | cut -d'|' -f1); h1=$(echo "$r" | cut -d'|' -f1); vm=$(echo "$r" | cut -d'|' -f3); va=$(echo "$r" | cut -d'|' -f4); wl=$(echo "$r" | cut -d'|' -f5)
                 if [ -n "$h1" ] && [ "$h1" = "$h0" ] && [ "$vm" = "$va" ] && [ "${wl:-1}" = 0 ]; then echo "PASS ddr_slow (reads 90..150 ui clocks)"; else echo "FAIL ddr_slow"; rc=1; fi ;;
    calib)
                 # the board: the DDR2 controller is in reset (ui_rst || !ddr_calib) for the first part of a millisecond while c128_ramd already issues its first sweep line
                 # (ungated clock, no reset, no timeout). The machine must still start: program `boot` PASS and the whole pinned RAM swept (4 slots x 64 pages x 16 lines = 4096 fetches).
                 buildmode $m "$base RAM_DDR2" || { rc=1; continue; }
                 runprog $m boot +MS="${C128_MS:-10}" +UIRST_US="${C128_UIRST_US:-300}"
                 l="$build.ddr_$m/run_boot/log"
                 pr=FAIL; grep -q '^PASS tb_c128' "$l" && pr=PASS
                 fe=$(grep '^ramd|' "$l" | head -1 | cut -d'|' -f4); fe=${fe:-0}
                 echo "$m: program boot $pr, DDR2 line fetches $fe (4096 for the sweep alone), reset ${C128_UIRST_US:-300} us"
                 if [ $pr = PASS ] && [ "$fe" -ge 4096 ]; then echo "PASS calib (the machine starts although the first sweep request came in the DDR2 reset)"; else echo "FAIL calib"; rc=1; fi ;;
    progs)       buildmode ddr "$base RAM_DDR2" || { rc=1; continue; }
                 for p in boot mmu fast c64mode z80back; do
                   runprog ddr $p +MS="${C128_MS:-6}" || { rc=1; continue; }
                   if grep -q '^PASS tb_c128' "$build.ddr_ddr/run_$p/log"; then echo "PASS program $p (RAM in the DDR2)"; else echo "FAIL program $p"; grep -E '^(FAIL|ok)' "$build.ddr_ddr/run_$p/log" | head -5; rc=1; fi
                 done ;;
    *) echo "unknown mode $m"; exit 2 ;;
  esac
done
[ $rc = 0 ] && echo "PASS run_ramddr" || echo "FAIL run_ramddr"
exit $rc
