#!/bin/sh
# T175 / T260: the pads of the diag core (cores/diag/rtl/diag_pads.v) and the wiring of the pin bank in mono/top.v / mono/layer2_diag.v.
#  - diag_pads (joystick): released pins are the old pure inputs (1024 outside combinations x 2 checks); low[k] pulls pin k low only; decouple releases; never driven high.
#  - diag_slot_pads (T260): the cartridge port's A / D / R/W pads keep the host's idle, take a drive on one pad only, high-Z on A0-A11, and the host's port lock wins.
#  - the pin table (S3, lint): tools/diag_pins_gen.py --check = the generated files are current and the table equals the connector ports of mono/top.v (every port a bank pin or excluded).
#  - the wiring: mono/top.v includes the generated files, hands layer 2 dg_in / dg_en / dg_o / dg_z and the decouple, the joystick pads take their drive from the bank, the cartridge pads, C2N,
#    PADDLE, IEC, SID and LED lines take it from the bank, the synchroniser of the host reads the pad level; layer2_diag.v wires the engine and the capture to those ports and to the DDR2 client e_*;
#    pins.xdc limits the joystick drive.
set -e
here=$(cd "$(dirname "$0")/.." && pwd)
root=$here/../../..
out=$here/build/pins; mkdir -p "$out"
f=$root/cores/diag/rtl/diag_pads.v
vvp_run() { iverilog -g2012 -s "$3" -o "$out/$1.vvp" "$here/tb_pads.v" "$2" "$here/unisim_stub.v" && vvp -n "$out/$1.vvp" > "$out/$1.log" || true; }
rm -f "$out"/*.log
vvp_run base "$f" tb_pads
vvp_run slot "$f" tb_slotpads
grep "^PASS\|^FAIL" "$out/base.log" "$out/slot.log"
grep -q '^PASS tb_pads errors=0' "$out/base.log" || { cat "$out/base.log"; echo "FAIL pins: the joystick pad bench fails"; exit 1; }
grep -q '^PASS tb_slotpads errors=0' "$out/slot.log" || { cat "$out/slot.log"; echo "FAIL pins: the cartridge pad bench fails"; exit 1; }
python3 "$root/tools/diag_pins_gen.py" --check || { echo "FAIL pins: the pin table"; exit 1; }
top=$root/cores/diag/mono/top.v; l2=$root/cores/diag/mono/layer2_diag.v; xdc=$root/cores/diag/mono/host/pins.xdc
wiring() { # top layer2
  grep -q 'inout  wire \[4:0\] KBJOY_1_JOY, KBJOY_2_JOY' "$1" &&
  grep -q 'diag_pads kbjoy_pads (.KBJOY_1_JOY(KBJOY_1_JOY), .KBJOY_2_JOY(KBJOY_2_JOY), .low({dg_en\[IX_KBJOY_2_JOY0 +: 5\], dg_en\[IX_KBJOY_1_JOY0 +: 5\]}), .decouple(core_decouple), .j1_i(kbj1_i), .j2_i(kbj2_i));' "$1" &&
  grep -q 'pins_s1 <= ~{KBJOY_RESTORE, kbj2_i, kbj1_i};' "$1" &&
  grep -q '`include "diag_pintab.vh"' "$1" && grep -q '`include "diag_dgin.vh"' "$1" &&
  grep -q '\.dg_in(dg_in), \.dg_en(dg_en), \.dg_o(dg_o), \.dg_z(dg_z), \.dg_dc(dc_cpu),' "$1" &&
  grep -q 'diag_slot_pads slot_pads (.SLOT_ADDR(SLOT_ADDR), .SLOT_DATA(SLOT_DATA), .SLOT_RWn(SLOT_RWn), .port_off(port_off),' "$1" &&
  grep -q '\.SLOT_DATA(slot_d_i),' "$1" &&
  ! grep -v '^ *//' "$1" | grep -v 'diag_pads kbjoy_pads' | grep -v 'inout  wire \[4:0\] KBJOY_1_JOY' | grep -q 'KBJOY_[12]_JOY\[\|KBJOY_[12]_JOY[,)]' &&
  grep -q '\.pins_async(dg_in), \.dg_en(dg_en), \.dg_o(dg_o), \.dg_z(dg_z),' "$2" && grep -q '\.dc(dg_dc)' "$2" &&
  grep -q '\.e_en(e_en), \.e_addr(e_addr), \.e_cmd(e_cmd), \.e_wdf_data(e_wdf_data), \.e_wdf_mask(e_wdf_mask), \.e_wdf_wren(e_wdf_wren),' "$2" &&
  grep -q '\.e_rdy(e_rdy), \.e_rd_valid(e_rd_valid), \.e_rd_data(c_rd_data)' "$2" && grep -q '\.calib(ddr_calib)' "$2"
}
wiring "$top" "$l2" || { echo "FAIL wiring of the pin bank in top.v / layer2_diag.v"; exit 1; }
for p in C2N_MOTOR C2N_SENSE C2N_READ C2N_WRITE PADDLE_DIS PADDLE_ADJ IEC_ATN_O IEC_CLOCK_O IEC_DATA_O IEC_RESET_O IEC_SRQ_O SLOT_SID1n SLOT_SID2n SLOT_SID_ADDR SLOT_ROMLn SLOT_ROMHn SLOT_IO1n SLOT_IO2n SLOT_BA SLOT_ADDRDIR SLOT_ADDROEn SLOT_DATADIR SLOT_DATAOEn SLOT_PHI2 SLOT_DOTCLK SLOT_RST_O LED_BOARDn LED_CASE1n LED_CASE2n LED_CASE3n LED_DATA LED_CLOCK BLING_LED ETH_LED_YELn; do
  grep -v '^ *//' "$top" | grep -q "dg_en\[IX_$p\]" || { echo "FAIL wiring: $p does not take the bank's drive in top.v"; exit 1; }
done
grep -q 'DRIVE 4 \[get_ports {KBJOY_1_JOY\[\*\] KBJOY_2_JOY\[\*\]}\]' "$xdc" || { echo "FAIL pins.xdc: no drive limit"; exit 1; }
echo "ok   wiring: the bank's vectors reach every drivable pad of top.v, the engine and the capture are wired in layer2_diag, the synchroniser reads the pad level"
echo "PASS pins (released = old input, drive on one pad only, host idle kept, pin table = top ports, wiring checked)"
