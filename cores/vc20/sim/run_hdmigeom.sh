#!/bin/sh
# T118: the HDMI geometry bench. What does the host's vic_scaler hand to the HDMI encoder (hs / vs / de / cx / cy) for the VC-20 core's
# pix_x/pix_y stream and for the Amiga 1200's, and does anything in it depend on the core? Needs ghdl and iverilog (the sim image,
# trx64-sim-env). Steps: GHDL dumps the VC-20 core's stream (tb_vc20_pix.vhd), iverilog replays it and the A1200 raster into the
# host's scaler (tb_hdmigeom.v). Variant:
#   slow     a core frame of another length: must still PASS with the same numbers (the hypothesis under test)
set -e
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../.." && pwd)
v=$root/cores/vc20/rtl
out=${OUT:-$here/build_geom}
mkdir -p "$out"
cd "$out"
opts="--std=08 -fsynopsys -frelaxed -Wno-hide -Wno-shared -Wno-library"
if [ ! -s vc20_pix.txt ]; then
  nice -n 15 ghdl -i $opts --work=work \
    $v/src/t65/T65_Pack.vhd $v/src/t65/T65_ALU.vhd $v/src/t65/T65_MCode.vhd $v/src/t65/T65.vhd \
    $v/src/vc20_via6522.vhd $v/src/vic20_clocks.vhd $v/src/vic20_keyboard.vhd $v/src/m6561.vhd \
    $v/trx/vc20_ram.vhd $v/src/vic20.vhd $v/trx/vc20_glue.vhd $v/trx/vc20_regs.vhd $v/trx/vc20_pixw.vhd $v/trx/vc20_core.vhd \
    "$here/tb_vc20_pix.vhd" > ghdl_import.log 2>&1 || { tail -20 ghdl_import.log; exit 1; }
  nice -n 15 ghdl -m $opts tb_vc20_pix > ghdl_make.log 2>&1 || { tail -30 ghdl_make.log; exit 1; }
  nice -n 15 ghdl -r $opts tb_vc20_pix --stop-time=${STOP:-130ms} --ieee-asserts=disable 2>&1 | grep -E "records|error" || true
fi
test -s vc20_pix.txt
nice -n 15 iverilog -g2012 -o geom.vvp -s tb "$here/tb_hdmigeom.v" "$root/host/vic_scaler.v" "$root/host/pal_ram.v"
for s in a1200 vc20 slow; do
  nice -n 15 vvp -n geom.vvp +SRC=$s > run_$s.log &
  case $s in a1200) ;; *) wait ;; esac
done
wait
for s in a1200 vc20 slow; do echo "== $s"; grep -E "^(S|PASS|FAIL) " run_$s.log; done
echo "== G lines of the first complete frame"
for s in a1200 vc20 slow; do echo "$s: $(grep '^G ' run_$s.log | head -1)"; done
# comparison without the frame number
for s in a1200 vc20 slow; do grep '^G ' run_$s.log | sed 's/frame=[0-9]* //' | sort -u > g_$s.txt; done
ok=1
cmp -s g_a1200.txt g_vc20.txt && echo "PASS hdmigeom: a1200 and vc20 give the same encoder input (every frame)" || { echo "FAIL hdmigeom: a1200 and vc20 differ"; ok=0; }
cmp -s g_a1200.txt g_slow.txt && echo "PASS hdmigeom: a frame of another length does not change it" || { echo "FAIL hdmigeom: slow differs"; ok=0; }
[ $ok = 1 ]
