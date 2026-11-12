#!/bin/sh
# T95: the whole NES core under Verilator (nes_core with cores/nes/gen - NES_MiSTer as tools/nes_gen.py makes it - and trx), a cartridge of our own (mkcart.py), no third-party
# image. Modes of tb_nes.v: frame (picture, NMI, frame period, silence), tone (sound), win (the app's window and registers), pause (Freeze).
# PASS when every mode passes.
#   sh cores/nes/sim/run.sh [mode ...]        PPM=file writes the newest frame of mode frame; pal = mode frame on a PAL console (register SYSTEM)
set -e
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../.." && pwd)
# One build directory per argument list: ci/sims.sh runs nes and nes_maps at the same time in the same tree (it
# sandboxes only for --check-deps), and both used build/obj - run 868 had gcc die in both while the other
# rewrote the objects. NES_OUT overrides the name.
out=$here/build/${NES_OUT:-$(printf '%s' "$*" | cksum | cut -d' ' -f1)}
mkdir -p "$out"
python3 "$here/mkcart.py" "$out"
for v in tone cpu banks2 banks3 inv pad mmc1 mmc2 mmc3 mmc4 vrc24 mmc66 mmc34 mmc71 mmc79; do python3 "$here/mkcart.py" "$out" $v; done
g=$root/cores/nes/gen
src="$g/regs_savestates.sv $g/nes.v $g/ppu.sv $g/apu.sv $g/cart.sv $g/mappers/*.sv $g/t65/t65_nes.v $root/cores/nes/trx/*.v"
build() { # build <dir> [verilator options]
  d=$1; shift
  # shellcheck disable=SC2086
  nice -n 15 verilator --binary --timing -Wno-fatal -Wno-lint -Wno-style -Wno-TIMESCALEMOD -j 2 --top-module tb_nes \
    -Mdir "$out/$d" --x-assign 0 --x-initial 0 "$@" "$here/tb_nes.v" "$here/ddr2c_model.v" $src > "$out/$d.build.log" 2>&1 || { tail -20 "$out/$d.build.log"; echo "FAIL build"; exit 1; }
}
build obj
modes=${*:-"frame pal tone win cpu pad banks2 banks3 mmc1 mmc2 mmc3 mmc4 vrc24 mmc66 mmc34 mmc71 mmc79 inv pause"}
for m in $modes; do
  cart=cart.hex
  case $m in pal) cart=cart.hex;; tone) cart=carttone.hex;; cpu) cart=cartcpu.hex;; pad|banks2|banks3|inv|mmc1|mmc2|mmc3|mmc4|vrc24|mmc66|mmc34|mmc71|mmc79) cart=cart$m.hex;; esac
  ppm=""
  [ "$m" != frame ] || [ -z "$PPM" ] || ppm="+PPM=$PPM"
  mode=$m; extra=""
  case $m in
    pal) mode=frame; extra="+PAL";;
    cpu|pad|banks2|banks3|mmc1|mmc2|mmc3|mmc4|vrc24|mmc66|mmc34|mmc71|mmc79) mode=check; extra="+EXPECT=$out/expect$m.hex +NEXP=$(wc -l < "$out/expect$m.hex" | tr -d ' ')";;
  esac
  case $m in banks2|banks3|inv|mmc1|mmc2|mmc3|mmc4|vrc24|mmc66|mmc34|mmc71|mmc79) extra="$extra +BD";; esac
  case $m in pad) extra="$extra +PAD";; esac
  echo "== $m"
  # shellcheck disable=SC2086
  (cd "$out" && nice -n 15 "obj/Vtb_nes" +CART="$out/$cart" +MODE=$mode $ppm $extra) | tee "$out/$m.log"
  grep -q "^PASS tb_nes $mode" "$out/$m.log" || { echo "FAIL $m"; exit 1; }
done
echo "PASS run_nes ($modes)"
