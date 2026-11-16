#!/bin/sh
# T217: the program start on the SuperCPU core, against the REAL ROMs (local bench, never in CI: the ROM files live outside the repository).
# The machine boots to the BASIC prompt (the real KERNAL, BASIC and SuperCPU ROM). At PRG_AT the application's start of a program is replayed into the machine's model:
# tools/cbm_prg_sim.cc places the .prg with the application's own library code and numbers (app/loaders/cbm_prg.cc, coreapp/cores/c64scpu_prg.h) and writes what
# reached the machine - bytes into the DDR2 window (the 65816's SRAM), the cache flush, the key matrix of the typed RUN / SYS - and tb_c64scpu.v +PRG_AT replays it.
# Cases (all must hold):
#   basic   new way: the BASIC program runs: its POKE lands in the SRAM ($C000 = 42), its PEEK(45) shows BASIC's VARTAB = the end the relinked chain ends at, TXTTAB is $0801
#   ml      new way: a machine-language file at $C100 is started by SYS 49408 (its STA lands in the SRAM)
#   old     the control, the way the application started a program before T217 (RAM window and keyboard buffer only): the program must NOT run - the bench cannot pass
#           for the reason that the program ran by itself
#   C64_ROMDIR=<dir with scpu64*, basic*901226*, kernal*901227*, char*901225* files> sh cores/c64scpu/sim/run_prgrun.sh [basic|ml|old]...     (default: all three)
# PRG_AT_US (default 3700000: after READY, see the "scr|" lines of the first case) and PRG_MS (default 8500: simulated milliseconds) can be set.
set -u
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../.." && pwd)
build=${C64SCPU_RBUILD:-$here/build_real}
: "${C64_ROMDIR:?set C64_ROMDIR to the directory with the ROM files}"
at=${PRG_AT_US:-3700000}; ms=${PRG_MS:-5000}
cases=${*:-"basic ml old"}
if [ ! -x "$build/obj/tb_c64scpu" ]; then
  C64SCPU_SIM_DEFS="${C64SCPU_SIM_DEFS:-SID_REDIP NO_TURBO NO_REU TRX_SIM_ROMS}" sh "$here/build_machine.sh" "$build" || exit 1
fi
w="$build/prgrun_$(echo $cases | tr " " _)"; rm -rf "$w"; mkdir -p "$w"
eval "$(python3 -I "$here/prgrun_gen.py" scpu "$w" | sed 's/ /\n/g' | sed 's/^/G_/')"
c++ -std=gnu++11 -w -o "$w/prgsim" "$root/tools/cbm_prg_sim.cc" "$root/app/loaders/cbm_prg.cc" -I"$root/tests/coreapp" -I"$root/coreapp/src" -I"$root/coreapp/cores" -I"$root/app/loaders" -I"$root/app/video" || exit 1
rc=0
for cs in $cases; do
  run="$w/$cs"; mkdir -p "$run"; cp -a "$build/run/." "$run/"
  python3 "$root/tools/c64scpu_rom_image.py" "$C64_ROMDIR" "$run/mem/c64scpu_rom.mem" --mem >/dev/null || exit 1
  python3 - "$C64_ROMDIR" "$run/mem/scpu_rom.mem" <<'PY' || exit 1
import glob, os, sys
f = [p for p in glob.glob(os.path.join(sys.argv[1], "*")) if os.path.basename(p).lower().startswith("scpu")]
if len(f) != 1: sys.exit("%d scpu* files" % len(f))
d = open(f[0], "rb").read()
if len(d) not in (65536, 131072): sys.exit("%s: %d bytes, this bench takes 64 or 128 KiB" % (f[0], len(d)))
open(os.path.join(os.path.dirname(sys.argv[2]), "srom_log2"), "w").write("%d\n" % (len(d).bit_length() - 1))
open(sys.argv[2], "w").write("".join("%02x\n" % b for b in d))
PY
  case $cs in
    basic) "$w/prgsim" scpu new "$w/basic.prg" > "$run/mem/prgscript.txt"; exp="03${G_MARK#c}"; ;;
    ml)    "$w/prgsim" scpu new "$w/ml.prg" > "$run/mem/prgscript.txt"; ;;
    old)   "$w/prgsim" scpu old "$w/basic.prg" > "$run/mem/prgscript.txt"; ;;
    *) echo "unknown case $cs"; exit 2 ;;
  esac || exit 1
  case $cs in
    basic|old) dexp="0300${G_MARK}:2a,0300$(printf %04x $((0x$G_MARK + 1))):${G_VAR_LO}" ;;
    ml)        dexp="0300${G_MLMARK}:2a" ;;
  esac
  (cd "$run" && "$build/obj/tb_c64scpu" +MS="$ms" +SROMLOG2=$(cat "$run/mem/srom_log2") +PRG_AT="$at" +SCREEN +SRAMDUMP +DEXPECT="$dexp") > "$run/log" 2>&1
  grep -E "^(prg\||ok |FAIL: ddr)" "$run/log" | sed "s/^/$cs: /"
  scr=$(grep -c "^scr| *READY\." "$run/log")
  tx=$(grep "^sram|0020|" "$run/log" | head -1 | cut -d'|' -f3)     # $20-$2F: $2B/$2C are bytes 11 and 12
  txt=$(echo "$tx" | cut -c23-26)                                    # hex digits 22..25 = bytes $2B $2C
  if [ "$cs" = old ]; then
    if grep -q "^FAIL: ddr\[0300c000\]" "$run/log" && ! grep -q "^ok   ddr\[0300c000\]" "$run/log"; then echo "PASS prgrun old (control): the program did not run (SRAM \$C000 not 42)"; else echo "FAIL prgrun old (control): the program ran although only the RAM window and the keyboard buffer were written"; rc=1; fi
  else
    nok=$(grep -c "^ok   ddr" "$run/log"); want=2; [ "$cs" = ml ] && want=1
    if [ "$nok" -eq $want ] && ! grep -q "^FAIL: ddr" "$run/log" && [ "$scr" -ge 1 ]; then echo "PASS prgrun $cs: marker(s) in the 65816's SRAM, READY after the run, TXTTAB bytes $txt"; else echo "FAIL prgrun $cs: $nok markers ok (want $want), READY lines $scr, TXTTAB bytes $txt"; rc=1; fi
  fi
done
exit $rc
