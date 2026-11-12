#!/bin/sh
# T182 step 1: the boot bench with the REAL C64 ROMs (local bench, never in CI: the ROM files live outside the repository). The 6510 runs the real KERNAL's reset
# and BASIC's cold start through the core's ROM path; the bench prints the 40-column screen of $0400 ("scr|" lines) and reads the VIC's own picture of a frame
# back as text through the real character ROM (cores/c64scpu/sim/check_screen.py, 2 cells may differ: the blinking cursor). PASS when the screen has the BASIC V2
# banner and READY. and the picture shows the same.
#   C64_ROMDIR=<dir with basic*901226*, kernal*901227*, char*901225* files> sh cores/c64scpu/sim/run_real.sh [ms [frame]]     (default 3600 ms, frame 170)
# The KERNAL's RAM test alone takes about 1.5 s of simulated time. Run from the repository root in trx64-sim-env, like run_base.sh.
set -u
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../.." && pwd)
build=${C64SCPU_BUILD:-$here/build}
: "${C64_ROMDIR:?set C64_ROMDIR to the directory with the ROM files}"
ms=${1:-3600}
frame=${2:-170}
[ -x "$build/obj/tb_c64scpu" ] || sh "$here/build_machine.sh" "$build" || exit 1
run="$build/run_real"
rm -rf "$run"; mkdir -p "$run"; cp -a "$build/run/." "$run/"
python3 "$root/tools/c64scpu_rom_image.py" "$C64_ROMDIR" "$run/mem/c64scpu_rom.mem" --mem || exit 1
python3 "$root/tools/c64scpu_rom_image.py" "$C64_ROMDIR" "$run/rom.bin" > /dev/null || exit 1
(cd "$run" && "$build/obj/tb_c64scpu" +MS="$ms" +PIXDUMP="$frame" +SCRCODES +SCREEN) > "$run/log" 2>&1
grep -E "^(cycles|romc|scr\||PASS|FAIL)" "$run/log" | grep -vE "^scr\| *$"
python3 - "$run" <<'EOF'
import json, sys
run = sys.argv[1]
codes = [int(l, 16) for l in open(run + "/scrcodes.txt").read().split()]
rom = open(run + "/rom.bin", "rb").read()
rows = [codes[40 * r:40 * r + 40] for r in range(25)]
json.dump({"rows": rows, "chargen": list(rom[0x4000:0x4800]), "fg": 14, "bg": 6, "border": 14}, open(run + "/expect.json", "w"))
EOF
python3 "$here/check_screen.py" "$run/pixdump.txt" "$run/expect.json" 2 | tee "$run/pic.log"
ban=$(grep -c "COMMODORE 64 BASIC V2" "$run/log")
rdy=$(grep -c "^scr| *READY\." "$run/log")
pic=$(grep -c "^PASS check_screen" "$run/pic.log")
if [ "$ban" -ge 1 ] && [ "$rdy" -ge 1 ] && [ "$pic" -ge 1 ]; then echo "PASS run_real: BASIC V2 banner and READY. in RAM and in the picture"; else echo "FAIL run_real: banner lines $ban, READY lines $rdy, picture $pic"; exit 1; fi
