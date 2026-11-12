# Non-project build of hdmi_test. Run through tools/vivado.sh.
set part xc7a50tfgg484-1
source ../../tools/vivado_lib.tcl
file mkdir build
create_project -in_memory -part $part
set_property target_language Verilog [current_project]

trx_ip gt_hdmi xilinx.com:ip:gtwizard [list \
  CONFIG.identical_val_tx_line_rate {0.7425} \
  CONFIG.identical_val_tx_reference_clock {148.500} \
  CONFIG.identical_val_rx_line_rate {0.7425} \
  CONFIG.identical_val_rx_reference_clock {148.500} \
  CONFIG.identical_val_no_rx {true} \
  CONFIG.gt0_val {true} CONFIG.gt1_val {true} CONFIG.gt2_val {true} CONFIG.gt3_val {true} \
  CONFIG.gt0_val_tx_refclk {REFCLK0_Q0} CONFIG.gt1_val_tx_refclk {REFCLK0_Q0} \
  CONFIG.gt2_val_tx_refclk {REFCLK0_Q0} CONFIG.gt3_val_tx_refclk {REFCLK0_Q0} \
  CONFIG.gt0_val_encoding {None} CONFIG.gt0_val_tx_data_width {20} CONFIG.gt0_val_tx_int_datawidth {20} \
  CONFIG.gt0_val_txbuf_en {true} CONFIG.gt0_usesharedlogic {1} CONFIG.gt_val_drp {false} \
]

read_verilog -sv hdmi_gtp.v
read_verilog top.v tmds.v video_720p.v cdce_i2c.v
read_xdc pins.xdc
read_xdc gtp_pins.xdc

synth_design -top top -part $part
opt_design
place_design
route_design
write_checkpoint -force build/routed.dcp
write_bitstream -force build/hdmi_test.bit
puts "=== bitstream: build/hdmi_test.bit"
report_utilization -file build/utilization.rpt
report_timing -max_paths 5 -nworst 1 -file build/timing.rpt
puts "=== done"
