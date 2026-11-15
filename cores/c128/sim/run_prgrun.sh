#!/bin/sh
# T217: the program start on the C128 core (128 mode), against the REAL C128 ROMs (local bench, never in CI: the ROM files live outside the repository).
# The machine boots to BASIC 7's prompt. At PRG_AT the application's start of a program is replayed: tools/cbm_prg_sim.cc places the .prg with the application's own
# library code and numbers (app/loaders/cbm_prg.cc, coreapp/cores/c128_prg.h) - the program and BASIC's pointers into the RAM window, "RUN" or "SYS n" into the keyboard
# buffer ($034A, count $D0) - and tb_c128.v +PRG_AT replays it into RAM block 0.
# Cases (all must hold):
#   basic   the BASIC program (load $1C01) runs: its POKE lands in RAM ($0B00 = 42), its PEEK(47) shows BASIC's VARTAB = the end of the relinked chain, TXTTAB is $1C01
#   ml      a machine-language file at $1300 is started by SYS 4864 (its STA lands in RAM)
#   none    the control: nothing is placed, nothing typed: the marker stays 0 (a bench that could not tell would pass the two cases above for nothing)
#   C128_ROMDIR=<dir with the VICE-named ROM files> sh cores/c128/sim/run_prgrun.sh [basic|ml|none]...     (default: all three)
# PRG_AT_US (default 5000000) and PRG_MS (default 9000) can be set. The machine is the product's: RAM in the DDR2 behind the line cache (RAM_DDR2), ROM cache.
set -u
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../.." && pwd)
: "${C128_ROMDIR:?set C128_ROMDIR to the directory with the ROM files}"
build=${C128_BUILD:-$here/build}.prg
at=${PRG_AT_US:-5000000}; ms=${PRG_MS:-9000}
cases=${*:-"basic ml none"}
[ -x "$build/obj/tb_c128" ] || C128_SIM_DEFS="Z80 SID_REDIP NO_TURBO NO_REU VDC_WHOLE VDC64 TRX_SIM_ROMS ROM_CACHE RAM_DDR2" sh "$here/build_machine.sh" "$build" || exit 1
w="$build/prgrun_$(echo $cases | tr " " _)"; rm -rf "$w"; mkdir -p "$w"
eval "$(python3 -I "$root/cores/c64scpu/sim/prgrun_gen.py" c128 "$w" | sed 's/ /\n/g' | sed 's/^/G_/')"
c++ -std=gnu++11 -w -o "$w/prgsim" "$root/tools/cbm_prg_sim.cc" "$root/app/loaders/cbm_prg.cc" -I"$root/tests/coreapp" -I"$root/coreapp/src" -I"$root/coreapp/cores" -I"$root/app/loaders" -I"$root/app/video" || exit 1
rc=0
for cs in $cases; do
  run="$w/$cs"; mkdir -p "$run"; cp -a "$build/run/." "$run/"
  python3 "$root/tools/c128_rom_image.py" "$C128_ROMDIR" "$run/mem/c128_rom.mem" --mem >/dev/null || exit 1
  prg=""
  case $cs in
    basic) "$w/prgsim" c128 new "$w/basic.prg" > "$run/mem/prgscript.txt"; prg="+PRG_AT=$at"; exp="${G_MARK}:2a,$(printf %04x $((0x$G_MARK + 1))):${G_VAR_LO}"; want=2 ;;
    ml)    "$w/prgsim" c128 new "$w/ml.prg" > "$run/mem/prgscript.txt"; prg="+PRG_AT=$at"; exp="${G_MLMARK}:2a"; want=1 ;;
    none)  exp="${G_MARK}:2a"; want=0 ;;
    *) echo "unknown case $cs"; exit 2 ;;
  esac || exit 1
  (cd "$run" && "$build/obj/tb_c128" +MS="$ms" $prg +SCREEN +DUMP=2d +DUMPN=1 +EXPECT="$exp") > "$run/log" 2>&1
  grep -E "^(prg\||ok |FAIL: ram|ram\|002d)" "$run/log" | sed "s/^/$cs: /"
  nok=$(grep -c "^ok   ram" "$run/log")
  rdy=$(grep -c "^scr| *READY\." "$run/log")
  if [ "$cs" = none ]; then
    if [ "$nok" -eq 0 ] && grep -q "^FAIL: ram\[0b00\] = 00" "$run/log"; then echo "PASS prgrun none (control): no program placed, the marker stays 0"; else echo "FAIL prgrun none (control)"; rc=1; fi
  else
    txt=$(grep "^ram|002d|" "$run/log" | head -1 | cut -d'|' -f3 | cut -c1-5)
    if [ "$nok" -eq $want ] && ! grep -q "^FAIL: ram" "$run/log" && [ "$rdy" -ge 1 ] && [ "$txt" = "$G_TXT_LO $G_TXT_HI" ]; then echo "PASS prgrun $cs: marker(s) in RAM, READY after the run, TXTTAB $txt"; else echo "FAIL prgrun $cs: $nok markers ok (want $want), READY lines $rdy, TXTTAB '$txt'"; rc=1; fi
  fi
done
exit $rc
