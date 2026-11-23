#!/bin/sh
# T257 fault 4: the ruler program (vdc_il_ruler.asm, mode A) on the whole C128 machine with the REAL C128 ROMs (local bench, never in CI: the ROM files live
# outside the repository). The machine boots to READY (40 columns, +NODEV); the program is placed and RUN typed as the application starts a program
# (tools/cbm_prg_sim.cc, tb_c128.v +PRG_AT, after READY is on the screen). Near the end the VDC's registers are read through the read-back port with the
# machine running and stopped (T257 §5 fault 3), four fields of the VDC's picture are captured and the whole VDC RAM is dumped; vdcil_model.py check compares
# them with the program's registers and picture. Final line PASS or FAIL.
#   C128_ROMDIR=<dir with the VICE-named ROM files> sh cores/c128/sim/run_ruler.sh [build dir]
# Run in the sim container (trx64-sim-env: verilator, ghdl, 64tass, python3). RULER_MS (default 8000) the simulated ms; the read-back 400 ms before the end, the
# fields from 250 ms before it (after the stop of the read-back).
set -u
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../.." && pwd)
: "${C128_ROMDIR:?set C128_ROMDIR to the directory with the ROM files}"
build=${1:-${C128_BUILD:-$here/build}.prg}
ms=${RULER_MS:-8000}
at=$(( (ms - 400) * 1000 ))
[ -x "$build/obj/tb_c128" ] || C128_SIM_DEFS="Z80 SID_REDIP NO_TURBO NO_REU VDC_WHOLE VDC64 TRX_SIM_ROMS ROM_CACHE RAM_DDR2" sh "$here/build_machine.sh" "$build" || exit 1
w="$build/ruler"; rm -rf "$w"; mkdir -p "$w/run"
64tass -q -a -o "$w/ruler.prg" "$here/vdc_il_ruler.asm" > "$w/asm.log" 2>&1 || { cat "$w/asm.log"; echo "FAIL asm"; exit 1; }
c++ -std=gnu++11 -w -o "$w/prgsim" "$root/tools/cbm_prg_sim.cc" "$root/app/loaders/cbm_prg.cc" -I"$root/tests/coreapp" -I"$root/coreapp/src" -I"$root/coreapp/cores" -I"$root/app/loaders" -I"$root/app/video" || exit 1
cp -a "$build/run/." "$w/run/"
python3 "$root/tools/c128_rom_image.py" "$C128_ROMDIR" "$w/run/mem/c128_rom.mem" --mem > /dev/null || exit 1
# 6: wait for READY. on the 40-column screen (screen codes), then the application's placement and the typed RUN
{ echo "6 6 12050104192e"; "$w/prgsim" c128 new "$w/ruler.prg"; } > "$w/run/mem/prgscript.txt" || exit 1
( cd "$w/run" && nice -n 15 "$build/obj/tb_c128" +MS="$ms" +NODEV +PRG_AT=1000000 +VKREAD_AT="$at" +VDCFIELDS=$((at + 150000)) +VRAMALL +VDCREGLOG ) > "$w/log" 2>&1
grep -E "^(prg\||evt\||vreg\||vdcw\||vk\||vkram\||vdcfield\||FAIL)" "$w/log" | grep -vE "^vk\|(run|stop)\|"
python3 "$here/vdcil_model.py" check "$w/log" "$w/run" 0; rc=$?
nf=$(grep -c "^FAIL" "$w/log")
[ "$rc" = 0 ] && [ "$nf" = 0 ] && echo "PASS run_ruler" || { echo "FAIL run_ruler ($nf FAIL lines from the bench)"; exit 1; }
