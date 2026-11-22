#!/bin/sh
# T255: the bench of the Mandelbrot large core's picture path under Verilator: ddt_mandel_hd (mono/ddt_mandel_hd.v) behind the HDMI timing of the core's own copy (mono/host/hdmi_timing.v), DDT's
# engines as he wrote them (rtl/, trx/), the Xilinx primitives CARRY4 and xpm_memory_tdpram as models (sim/), the model of the picture (mandel_model.py, written from his code) as the expectation.
# Not part of the CI (Alex: no benches before a merge); run by hand, niced:  sh cores/ddt_mandel/sim/run.sh [mode ...]
# modes: pix       the default view: a whole 1080p frame equals the model with the splash text, and without it after the splash counter ran out
#        joy       pix, then fire + up on port 2: the frame equals the model of the zoomed view
# The joystick's debounce clock is scaled 100 : 1 (JOY_CLK_HZ) so that the bench does not wait 8 ms of simulated time per contact; the logic is the same. 2 engines (DDT_NE): the picture does not depend on the count.
set -e
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../.." && pwd)
out=$here/build/${DDT_OUT:-$(printf '%s' "$*" | cksum | cut -d' ' -f1)}
mkdir -p "$out/mem"
modes=${*:-"pix joy"}
ne=${DDT_NE:-2}
cp "$root/cores/ddt_mandel/mono/mem/ovl_font8.mem" "$out/mem/"
cp "$root/cores/ddt_mandel/mem/palette_rgb565_256.hex" "$out/"
m=$root/cores/ddt_mandel
rtl="$m/trx/pixel_gen_mandelbrot.sv $m/trx/fb_scanline_writer.sv $m/rtl/fb_bram_dma.sv $m/rtl/linebuf_iter8.sv $m/rtl/framebuffer_bram.sv $m/rtl/joystick_buttons.sv"
glue="$m/trx/screen_matrix.v $m/mono/ddt_mandel_hd.v $m/mono/host/hdmi_timing.v"
simm="$here/xpm_memory_tdpram.v $here/carry4.v $here/tb_ddt_hd.v"
nice -n 15 python3 -I "$here/mandel_model.py" "$out/exp_text.hex" --text --engines "$ne"
nice -n 15 python3 -I "$here/mandel_model.py" "$out/exp_plain.hex"
nice -n 15 python3 -I "$here/mandel_model.py" "$out/exp_zoom.hex" --zoom 1

d=obj
mkdir -p "$out/$d"
# shellcheck disable=SC2086
nice -n 15 verilator --binary --timing -Wno-fatal -Wno-lint -Wno-style -Wno-TIMESCALEMOD -j 2 --top-module tb_ddt_hd \
  -Mdir "$out/$d" --x-assign 0 --x-initial 0 -I"$m/mono/host" -GNE="$ne" $simm $rtl $glue > "$out/$d.build.log" 2>&1 || { tail -30 "$out/$d.build.log"; echo "FAIL build"; exit 1; }
for md in $modes; do
  case $md in
    pix|joy)
      echo "== $md"
      (cd "$out" && nice -n 15 "$d/Vtb_ddt_hd" +MODE="$md") > "$out/$md.log" 2>&1 || true
      cat "$out/$md.log"
      grep -q "^PASS ddt_mandel_hd $md" "$out/$md.log" || { echo "FAIL $md"; exit 1; } ;;
    *) echo "unknown mode $md"; exit 2 ;;
  esac
done
echo "PASS ddt_mandel_hd: $modes"
