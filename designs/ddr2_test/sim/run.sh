#!/bin/sh
# Benches of ddr2_lean (host/ddr2_lean.v) in iverilog, one process at a time, niced:
#
#   sh designs/ddr2_test/sim/run.sh cal       init and calibration at five board delays, light traffic
#   sh designs/ddr2_test/sim/run.sh traffic   100k random read/write/mask commands against a reference memory
#   sh designs/ddr2_test/sim/run.sh refresh   refresh every 0.53 us under back-to-back load
#   sh designs/ddr2_test/sim/run.sh arb       the real mig_arb with all its clients, mixed load
#   sh designs/ddr2_test/sim/run.sh test      the ddr2_test design: cal_seq, tester and JTAG register map
#   sh designs/ddr2_test/sim/run.sh ctrl      ddr2_ctrl, the block c64_basic instantiates: calibration result word, 600 words
#                                             written and read back; and with no DRAM on the pins: calibration fails, never ready
#   sh designs/ddr2_test/sim/run.sh equiv     cal_seq against the frozen board version cal_seq_ref, 240 random calibrations
set -e
here=$(cd "$(dirname "$0")" && pwd)
D=$here/../../../host
T=$here/..
out=$here/build
mkdir -p "$out"
what=${1:-cal}
iv() { nice -n 15 iverilog -g2012 "$@"; }
vv() { nice -n 15 vvp -n "$@"; }
lean="$D/cal_seq.v $D/ddr2_lean.v $here/xilinx_models.v $here/ddr2_model.v"
case "$what" in
  cal)
    for d in "100 100" "500 500" "1500 900" "100 3500" "3000 300" "2500 2500"; do
      set -- $d
      iv -Ptb.WR_DLY=$1 -Ptb.RD_DLY=$2 -Ptb.NCMD=3000 -o "$out/cal_$1_$2.vvp" "$here/tb_ddr2_lean.v" $lean
      vv "$out/cal_$1_$2.vvp"
    done ;;
  traffic)
    iv -Ptb.NCMD=100000 -Ptb.IDLE=2 -o "$out/traffic.vvp" "$here/tb_ddr2_lean.v" $lean
    vv "$out/traffic.vvp"
    iv -Ptb.NCMD=40000 -Ptb.IDLE=0 -Ptb.WR_DLY=1200 -Ptb.RD_DLY=1700 -Ptb.SEED=7 -o "$out/traffic0.vvp" "$here/tb_ddr2_lean.v" $lean
    vv "$out/traffic0.vvp" ;;
  refresh)
    iv -Ptb.NCMD=40000 -Ptb.IDLE=0 -Ptb.REFI=40 -Ptb.SEED=3 -o "$out/refresh.vvp" "$here/tb_ddr2_lean.v" $lean
    vv "$out/refresh.vvp" ;;
  arb)
    iv -o "$out/arb.vvp" "$here/tb_mig_arb_lean.v" $D/cal_seq.v $D/ddr2_lean.v $D/ddr2_port.v $here/xilinx_models.v $here/ddr2_model.v
    vv "$out/arb.vvp" ;;
  test)
    iv -o "$out/test.vvp" "$here/tb_ddr2_test.v" $T/top.v $D/cal_seq.v $T/ddr2_tester.v $D/ddr2_lean.v $D/jtag_dbg.v $here/xilinx_models.v $here/ddr2_model.v
    vv "$out/test.vvp" ;;
  ctrl)
    iv -o "$out/ctrl.vvp" "$here/tb_ddr2_ctrl.v" $D/cal_seq.v $D/ddr2_lean.v $D/ddr2_ctrl.v $here/xilinx_models.v $here/ddr2_model.v
    vv "$out/ctrl.vvp"
    iv -Ptb.NODRAM=1 -o "$out/ctrl_nodram.vvp" "$here/tb_ddr2_ctrl.v" $D/cal_seq.v $D/ddr2_lean.v $D/ddr2_ctrl.v $here/xilinx_models.v $here/ddr2_model.v
    vv "$out/ctrl_nodram.vvp" ;;
  equiv)
    iv -o "$out/equiv.vvp" "$here/tb_cal_equiv.v" $here/cal_seq_ref.v $D/cal_seq.v
    vv "$out/equiv.vvp" ;;
  *) echo "unknown bench $what" >&2; exit 2 ;;
esac
