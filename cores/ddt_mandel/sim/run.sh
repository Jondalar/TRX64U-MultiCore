#!/bin/sh
# T168: the benches of the DDT Mandelbrot core under Verilator: layer2_ddt_mandel at the partition's real edge (tb_ddt_mandel.v), DDT's engines as he wrote them
# (rtl/), the Xilinx primitives CARRY4 and xpm_memory_tdpram as models (sim/), the model of the picture (mandel_model.py, written from his code) as the expectation.
#   sh cores/ddt_mandel/sim/run.sh [mode ...]
# modes: pix       the default view: the buffer in the DDR2 equals the model with the splash text, and without it after the splash counter ran out
#        joy       pix, then fire + up on joystick port 2: the buffer equals the model of the zoomed view
# The joystick's debounce clock is scaled 100 : 1 (-DDDT_JOY_CLK_HZ) so that the bench does not wait 8 ms of simulated time per contact; the logic is the same.
set -e
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../.." && pwd)
out=$here/build/${DDT_OUT:-$(printf '%s' "$*" | cksum | cut -d' ' -f1)}
mkdir -p "$out/mem"
modes=${*:-"pix joy"}
cp "$root/host/mem/ovl_font8.mem" "$out/mem/"
cp "$root/cores/ddt_mandel/mem/palette_rgb565_256.hex" "$out/"
python3 "$here/mk_l2ports.py" "$root/cores/ddt_mandel/layer2_ddt_mandel.v" "$out/ports.vh"
m=$root/cores/ddt_mandel
rtl="$m/trx/pixel_gen_mandelbrot.sv $m/trx/fb_scanline_writer.sv $m/rtl/fb_bram_dma.sv $m/rtl/linebuf_iter8.sv $m/rtl/framebuffer_bram.sv $m/rtl/joystick_buttons.sv"
glue="$m/trx/*.v $m/layer2_ddt_mandel.v"
simm="$here/xpm_memory_tdpram.v $here/carry4.v $here/ddr2e_model.v $here/tb_ddt_mandel.v"
nice -n 15 python3 -I "$here/mandel_model.py" "$out/exp_text.hex" --text
nice -n 15 python3 -I "$here/mandel_model.py" "$out/exp_plain.hex"
nice -n 15 python3 -I "$here/mandel_model.py" "$out/exp_zoom.hex" --zoom 1

build() { # build <dir> [defines]
  d=$1; shift
  mkdir -p "$out/$d"
  # shellcheck disable=SC2086
  nice -n 15 verilator --binary --timing -Wno-fatal -Wno-lint -Wno-style -Wno-TIMESCALEMOD -j 2 --top-module tb_ddt_mandel \
    -Mdir "$out/$d" --x-assign 0 --x-initial 0 -I"$out" -DDDT_JOY_CLK_HZ=1000000 "$@" $simm $rtl $glue > "$out/$d.build.log" 2>&1 || { tail -30 "$out/$d.build.log"; echo "FAIL build $d"; exit 1; }
}
runbench() { # runbench <dir> <mode> <log>
  (cd "$out" && nice -n 15 "$1/Vtb_ddt_mandel" +MODE="$2") > "$3" 2>&1 || true
}
build obj
for md in $modes; do
  case $md in
    pix|joy)
      echo "== $md"
      runbench obj "$md" "$out/$md.log"
      cat "$out/$md.log"
      grep -q "^PASS ddt_mandel $md" "$out/$md.log" || { echo "FAIL $md"; exit 1; } ;;
    *) echo "unknown mode $md"; exit 2 ;;
  esac
done
echo "PASS ddt_mandel: $modes"
