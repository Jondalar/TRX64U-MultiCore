#!/bin/sh
# T83, LOCAL ONLY (not in ci/sims.txt): boot the VC-20 core with real VIC-20 ROMs and look at the screen.
#   sh cores/vc20/sim/run_boot.sh CHAR.bin BASIC.bin KERNAL.bin [WAIT_MS]
# The ROM images are third-party and stay on your machine; they go into build/ (ignored by git).
# Takes about a quarter of an hour of wall clock per simulated second. In the container:
#   container run --rm -v "$PWD":/w -w /w vc20-ghdl sh cores/vc20/sim/run_boot.sh ...
set -e
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../.." && pwd)
v=$root/cores/vc20/rtl
out=$here/build
mkdir -p "$out"
python3 "$here/gen_rompkg.py" "$1" "$2" "$3" "$out/vc20_rom_pkg.vhd"
wait_ms=${4:-700}
ext=${EXT:-0}
ro=${RO:-0}
G="nice -n 15 ghdl"
opts="--std=08 -fsynopsys -frelaxed -Wno-hide -Wno-shared -Wno-library"
cd "$out"
$G -i $opts --work=work \
  $v/src/t65/T65_Pack.vhd $v/src/t65/T65_ALU.vhd $v/src/t65/T65_MCode.vhd $v/src/t65/T65.vhd \
  $v/src/vc20_via6522.vhd $v/src/vic20_clocks.vhd $v/src/vic20_keyboard.vhd $v/src/m6561.vhd \
  $v/trx/vc20_ram.vhd $v/src/vic20.vhd $v/trx/vc20_glue.vhd $v/trx/vc20_regs.vhd $v/trx/vc20_pixw.vhd $v/trx/vc20_core.vhd \
  "$out/vc20_rom_pkg.vhd" "$here/tb_vc20_boot.vhd" > ghdl_boot_import.log 2>&1 || { tail -20 ghdl_boot_import.log; exit 1; }
$G -m $opts tb_vc20_boot > ghdl_boot_make.log 2>&1 || { tail -30 ghdl_boot_make.log; exit 1; }
$G -r $opts tb_vc20_boot -gWAIT_MS="$wait_ms" -gEXT="$ext" -gRO="$ro" -gSCRADR="${SCR:-7680}" --stop-time=$((wait_ms + 400))ms --ieee-asserts=disable 2>&1 | sed 's/^.*(report [a-z]*)://'
