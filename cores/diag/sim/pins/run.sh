#!/bin/sh
# T175: the open-drain joystick pads of the Port Diagnostics core (cores/diag/rtl/diag_pads.v). Released pins are the old pure inputs (1024 outside combinations x 2 checks); low[k]
# pulls pin k low only; decouple releases; the pad is never driven high. Then the wiring: mono/top.v declares the pins inout, feeds the host's pin synchroniser from the pads'
# level and hands layer 2 the same wires; layer2_diag.v takes kbjoy_low from the engine and the engine reads kbjoy_i with the comparators; pins.xdc limits the drive.
set -e
here=$(cd "$(dirname "$0")/.." && pwd)
root=$here/../../..
out=$here/build/pins; mkdir -p "$out"
f=$root/cores/diag/rtl/diag_pads.v
vvp_run() { iverilog -g2012 -o "$out/$1.vvp" "$here/tb_pads.v" "$2" "$here/unisim_stub.v" && vvp -n "$out/$1.vvp" > "$out/$1.log" || true; }
rm -f "$out"/*.log
vvp_run base "$f"
grep "^PASS\|^FAIL" "$out/base.log"
grep -q '^PASS tb_pads errors=0' "$out/base.log" || { cat "$out/base.log"; echo "FAIL pins: the pad bench fails"; exit 1; }
top=$root/cores/diag/mono/top.v; l2=$root/cores/diag/mono/layer2_diag.v; xdc=$root/cores/diag/mono/host/pins.xdc
wiring() { # top layer2
  grep -q 'inout  wire \[4:0\] KBJOY_1_JOY, KBJOY_2_JOY' "$1" &&
  grep -q 'diag_pads kbjoy_pads (.KBJOY_1_JOY(KBJOY_1_JOY), .KBJOY_2_JOY(KBJOY_2_JOY), .low(l2_kbjoy_low), .decouple(core_decouple), .j1_i(kbj1_i), .j2_i(kbj2_i));' "$1" &&
  grep -q 'pins_s1 <= ~{KBJOY_RESTORE, kbj2_i, kbj1_i};' "$1" &&
  grep -q '\.kbjoy_i({kbj2_i, kbj1_i}),' "$1" && grep -q '\.kbjoy_low(l2_kbjoy_low),' "$1" &&
  ! grep -v '^ *//' "$1" | grep -v 'diag_pads kbjoy_pads' | grep -v 'inout  wire \[4:0\] KBJOY_1_JOY' | grep -q 'KBJOY_[12]_JOY\[\|KBJOY_[12]_JOY[,)]' &&
  grep -q 'assign kbjoy_low = e_low;' "$2" && grep -q '\.pins_async({paddle_cmp_i, kbjoy_i}), \.drv_low(e_low),' "$2" &&
  grep -q '\.pad_dis_o(paddle_dis_o), \.pad_dis_en(paddle_dis_en), \.pad_adj_o(paddle_adj_o), \.pad_adj_en(paddle_adj_en),' "$2"
}
wiring "$top" "$l2" || { echo "FAIL wiring of the pads in top.v / layer2_diag.v"; exit 1; }
grep -q 'DRIVE 4 \[get_ports {KBJOY_1_JOY\[\*\] KBJOY_2_JOY\[\*\]}\]' "$xdc" || { echo "FAIL pins.xdc: no drive limit"; exit 1; }
echo "ok   wiring: top.v pads inout, the synchroniser reads the pad level, layer2_diag drives kbjoy_low and reads kbjoy_i with PADDLE_CMP"
echo "PASS pins (released = old input, drive = low only, wiring checked)"
