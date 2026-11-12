#!/bin/sh
# T168: the benches of the DDT "Look Ma, No VIC" core under Verilator: layer2_ddt_novic at the partition's real edge (tb_ddt_novic.v), DDT's scroller and text screen as he wrote
# them (rtl/), our glue (trx/ and the Mandelbrot core's trx/ for the register block, raster, frame writer and font), the model of the picture (novic_model.py, written from his
# code) as the expectation.
#   sh cores/ddt_novic/sim/run.sh [mode ...]
# modes: pix       three pictures (after 2, 9 and 14 frames, the zoom envelope running in the later two): the buffer in the DDR2 equals the model pixel for pixel
set -e
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../.." && pwd)
out=$here/build/${DDT_OUT:-$(printf '%s' "$*" | cksum | cut -d' ' -f1)}
mkdir -p "$out/mem"
modes=${*:-"pix"}
cp "$root/host/mem/ovl_font8.mem" "$out/mem/"
python3 "$root/cores/ddt_mandel/sim/mk_l2ports.py" "$root/cores/ddt_novic/layer2_ddt_novic.v" "$out/ports.vh"
n=$root/cores/ddt_novic
m=$root/cores/ddt_mandel
shared="$m/trx/ddt_regs.v $m/trx/ddt_dots.v $m/trx/ddt_pixw.v $m/trx/ddt_clk_load.v $m/trx/c64_font_rom.v"
src="$n/rtl/scroller.v $n/rtl/screen_matrix.v $n/trx/*.v $shared $n/layer2_ddt_novic.v $m/sim/ddr2e_model.v $here/tb_ddt_novic.v"
nice -n 15 python3 -I "$here/novic_model.py" "$out/exp.hex" 24 508

build() { # build <dir> [defines]
  d=$1; shift
  mkdir -p "$out/$d"
  # shellcheck disable=SC2086
  nice -n 15 verilator --binary --timing -Wno-fatal -Wno-lint -Wno-style -Wno-TIMESCALEMOD -j 2 --top-module tb_ddt_novic \
    -Mdir "$out/$d" --x-assign 0 --x-initial 0 -I"$out" "$@" $src > "$out/$d.build.log" 2>&1 || { tail -30 "$out/$d.build.log"; echo "FAIL build $d"; exit 1; }
}
runbench() { # runbench <dir> <log>
  (cd "$out" && nice -n 15 "$1/Vtb_ddt_novic" +MODE=pix) > "$2" 2>&1 || true
}
build obj
for md in $modes; do
  case $md in
    pix)
      echo "== pix"
      runbench obj "$out/pix.log"
      cat "$out/pix.log"
      grep -q "^PASS ddt_novic pix" "$out/pix.log" || { echo "FAIL pix"; exit 1; } ;;
    *) echo "unknown mode $md"; exit 2 ;;
  esac
done
echo "PASS ddt_novic: $modes"
