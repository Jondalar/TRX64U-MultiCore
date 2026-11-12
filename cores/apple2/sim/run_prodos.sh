#!/bin/sh
# T89.1 S13: a real ProDOS 2.4 (blocks 0..127 of a Wizard Replay volume, media/) boots from the slot 7 card on the real Enhanced //e ROM.
#   A2_ROMS=<dir with apple2e.rom apple2e-video.rom (third party, outside the repository)> sh cores/apple2/sim/run_prodos.sh      (in a container with ghdl)
set -e
here=$(cd "$(dirname "$0")" && pwd)
. "$here/lib.sh"
: "${A2_ROMS:?A2_ROMS: directory with apple2e.rom and apple2e-video.rom}"
out=$(a2_workdir prodos${A2_DDR:-0}${A2_TAG:-})
python3 "$here/rom2hex.py" "$A2_ROMS/apple2e.rom" "$out/apple2e.hex"
python3 "$here/rom2hex.py" "$A2_ROMS/apple2e-video.rom" "$out/apple2e-video.hex"
python3 "$here/rom2hex.py" "$here/media/wizard_replay_blk0-127.bin" "$out/img.hex"
python3 - "$root/cores/apple2/rtl/roms/hdd_rom.h" "$out/hddrom.hex" <<'P'
import re, sys
b = re.findall(r"0x([0-9A-Fa-f]{2})", open(sys.argv[1]).read())
assert len(b) == 256
open(sys.argv[2], "w").write("".join(x.lower() + "\n" for x in b))
P
# A2_TRACE=<t0_ms>:<t1_ms>: a copy of apple2.vhd that logs every CPU bus cycle of the window to $out/trace.txt (trace_patch.py; the repository's file is untouched)
if [ -n "${A2_TRACE:-}" ]; then
  python3 "$here/trace_patch.py" "$root/cores/apple2/rtl/src/apple2.vhd" "$out/trc_apple2.vhd" "${A2_TRACE%%:*}" "${A2_TRACE##*:}"
  A2_SRC=$(printf '%s' "$A2_SRC" | sed "s#$root/cores/apple2/rtl/src/apple2.vhd#$out/trc_apple2.vhd#")
fi
a2_build tb_a2_prodos "$out"
( cd "$out" && ghdl -r $A2_GHDL_OPTS tb_a2_prodos -gDIR="$out" -gMAXMS="${MAXMS:-900}" -gAUX_DDR="${A2_DDR:-0}" --ieee-asserts=disable 2>&1 | tee run.log | sed -n 's/^.*(report note): /  /p' )
grep -q "PASS tb_a2_prodos" "$out/run.log"
echo "PASS prodos"
