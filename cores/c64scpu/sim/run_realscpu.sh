#!/bin/sh
# T182: the boot of the whole C64SCPU machine (SuperCPU map, 65816) with the REAL ROM files (local bench, never in CI: the ROM files live outside the repository).
# The 65816 resets through the ROM of VICE's scpu64 (64 KiB, SROM_LOG2 = 16 as the application sets it for that file); the C64 side gets BASIC, KERNAL and the character ROM
# the way the application puts them (tools/c64scpu_rom_image.py). The bench prints the 40-column screen of $0400 ("scr|") and, with PCHIST=1, a histogram of the
# 65816's opcode fetch addresses ("pch|addr|count") and the last 48 fetches ("pcl|addr|byte after the fetch").
#   C64_ROMDIR=<dir with scpu64*, basic*901226*, kernal*901227*, char*901225* files> sh cores/c64scpu/sim/run_realscpu.sh [ms]    (default 400 ms)
# Run from the repository root in trx64-sim-env. C64SCPU_RBUILD overrides the build directory, PCHIST=0 leaves the histogram out.
set -u
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../.." && pwd)
build=${C64SCPU_RBUILD:-$here/build_real}
: "${C64_ROMDIR:?set C64_ROMDIR to the directory with the ROM files}"
ms=${1:-400}
if [ ! -x "$build/obj/tb_c64scpu" ]; then
  C64SCPU_SIM_DEFS="${C64SCPU_SIM_DEFS:-SID_REDIP NO_TURBO NO_REU TRX_SIM_ROMS C64SCPU_PCHIST}" sh "$here/build_machine.sh" "$build" || exit 1
fi
run="$build/run_real"
rm -rf "$run"; mkdir -p "$run"; cp -a "$build/run/." "$run/"
python3 "$root/tools/c64scpu_rom_image.py" "$C64_ROMDIR" "$run/mem/c64scpu_rom.mem" --mem || exit 1
python3 - "$C64_ROMDIR" "$run/mem/scpu_rom.mem" <<'PY' || exit 1
import glob, os, sys
f = [p for p in glob.glob(os.path.join(sys.argv[1], "*")) if os.path.basename(p).lower().startswith("scpu")]
if len(f) != 1: sys.exit("%d scpu* files" % len(f))
d = open(f[0], "rb").read()
if len(d) != 65536: sys.exit("%s: %d bytes, this bench is for the 64 KiB ROM" % (f[0], len(d)))
open(sys.argv[2], "w").write("".join("%02x\n" % b for b in d))
PY
pch=""; [ "${PCHIST:-1}" = 1 ] && pch="+PCHIST"
(cd "$run" && "$build/obj/tb_c64scpu" +MS="$ms" +SROMLOG2=16 +SCREEN $pch $*) > "$run/log" 2>&1
grep -E "^(cycles|scr\||pcl\||PASS|FAIL)" "$run/log" | grep -vE "^scr\| *$"
grep "^pch|" "$run/log" | sort -t'|' -k3 -n -r | head -40
echo "log: $run/log"
