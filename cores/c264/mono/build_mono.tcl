# T164 WP19b: the Commodore 264 (C16, C116, Plus/4) as a LARGE core - one full bitstream of the XC7A50T, no partition (Alex 2026-11-01, docs/specs/T164-host-package-2.md 3a Q1 "Second cut": pb_l2 shrinks to
# X0-49 rows 0-99, 13,600 LUT, 40 block RAM tiles, 40 DSP; the core needs 43 block RAM tiles; "whatever does not fit becomes a large core"). Flow of cores/zxnext/mono/build_mono.tcl: the host top
# (mono/host/top.v, unchanged copy) and layer2_c264 (as the module "layer2" top.v instantiates) in ONE design: no HD.RECONFIGURABLE, no pb_l1 / pb_l2 / pb_cpu pblocks, no out-of-context
# checkpoints, no ICAP swap. Pins and timing constraints are the host's own (mono/host/*.xdc). The sources are the list the former cores/c264/core_sources.tcl read.
# Nothing under host/ is edited: Vivado's working directory is host/ and this script is read from cores/c264/mono/. The cache sizes of the host's RISC-V are the host's.
#
# Environment (all optional):
#   TRX_BUILD     output directory under host/ (default build_mono)
#   TRX_STOP      synth | place | route (default route = also writes the bitstream)
#   TRX_FEATURES  host blocks: a list of uci, jtag, streams (default "jtag": the DFX build's host)
#   TRX_DEFS      extra Verilog defines for the whole design, space separated
# T164 3a Q1 (Alex, 2026-11-01: "Copies instead."): the host files this build reads are this core's OWN COPY in cores/c264/mono/host/ ($hd; mono/host/FROM says where they
# come from and what was edited). A change in host/ moves neither this core's cache key nor this build; a host fix gets here only by copying it in on purpose. Vivado's working
# directory stays host/ for the output directory and the ../vendor, ../tools, ../cores paths; no host file is read from it. Where a comment below says host/X, it means mono/host/X.
set part xc7a50tfgg484-1
set bdir build_mono
if {[info exists ::env(TRX_BUILD)] && $::env(TRX_BUILD) ne ""} { set bdir $::env(TRX_BUILD) }
set stop route
if {[info exists ::env(TRX_STOP)] && $::env(TRX_STOP) ne ""} { set stop $::env(TRX_STOP) }
set fpga_type 2
set core ../vendor/icepi
source ../tools/vivado_lib.tcl
set common ../cores/common
set hd     ../cores/c264/mono/host

file mkdir $bdir
set ::trx_build $bdir
set ::env(TRX_MONO) 1


# the host's VHDL list (Gideon's blocks, the RISC-V with its caches)
source $hd/gideon_vhdl.tcl
# Defines: the host's switches from TRX_FEATURES (as the reference core derives them) and the variant's
set trx_on {jtag}
if {[info exists ::env(TRX_FEATURES)] && $::env(TRX_FEATURES) ne ""} { set trx_on [split $::env(TRX_FEATURES) ", "] }
set defs {}
if {"uci" ni $trx_on}     { lappend defs NO_UCI }
if {"jtag" ni $trx_on}    { lappend defs NO_JTAG_DBG }
if {"streams" ni $trx_on} { lappend defs NO_STREAMS }
if {[info exists ::env(TRX_DEFS)]} { foreach d $::env(TRX_DEFS) { lappend defs $d } }
set defs [lsort -unique $defs]
set synth_defs {}
foreach d $defs { lappend synth_defs -verilog_define $d }
puts "=== T164 mono: defines $defs"

create_project -in_memory -part $part
set_property target_language Verilog [current_project]
set_msg_config -id {Project 1-486} -new_severity INFO

trx_ip gt_hdmi xilinx.com:ip:gtwizard [list \
  CONFIG.identical_val_tx_line_rate {1.5} \
  CONFIG.identical_val_tx_reference_clock {150.000} \
  CONFIG.identical_val_rx_line_rate {1.5} \
  CONFIG.identical_val_rx_reference_clock {150.000} \
  CONFIG.identical_val_no_rx {true} \
  CONFIG.gt0_val {true} CONFIG.gt1_val {true} CONFIG.gt2_val {true} CONFIG.gt3_val {true} \
  CONFIG.gt0_val_tx_refclk {REFCLK0_Q0} CONFIG.gt1_val_tx_refclk {REFCLK0_Q0} \
  CONFIG.gt2_val_tx_refclk {REFCLK0_Q0} CONFIG.gt3_val_tx_refclk {REFCLK0_Q0} \
  CONFIG.gt0_val_encoding {None} CONFIG.gt0_val_tx_data_width {20} CONFIG.gt0_val_tx_int_datawidth {20} \
  CONFIG.gt0_val_txbuf_en {true} CONFIG.gt0_usesharedlogic {1} CONFIG.gt_val_drp {false} \
]

# host (the file list of host/build.tcl as in cores/amiga1200/mono/build_mono.tcl)
read_verilog -sv $hd/hdmi_gtp.v
# T164 G4: the encoder is mono/host/hdmi_enc (six forked files); the three unforked ones come from the vendor tree
read_verilog -sv [list {*}[glob $hd/hdmi_enc/*.sv] $hd/hdmi_enc/hdmi.v $core/hdmi/audio_sample_packet.sv $core/hdmi/audio_info_frame.sv \
  $core/hdmi/source_product_description_info_frame.sv]
read_verilog [list $hd/top.v $hd/vic_scaler.v $hd/pal_ram.v $hd/hdmi_out.v $hd/hdmi_timing.v $hd/mode_req.v $hd/cdce_mode.v $hd/tmds_rep.v $hd/mode_ctl.v $hd/c64u_keyboard.v $hd/bling_keyboard.v $hd/jtag_dbg.v $hd/ddr2_port.v $hd/ddr2_ctrl.v $hd/ddr2_lean.v $hd/cal_seq.v $hd/i2s_out.v $hd/trx_gain.v $hd/trx_tone.v $hd/portlock_clk.v $hd/esp_ctl.v $hd/ui_buttons.v \
  $hd/con_ring.v $hd/pktport_host.v $hd/app_i2c.v $hd/overlay.v $hd/usb_phy.v $common/iec_bus.v \
  $hd/cart_fill.v $common/trx_io.v $common/trx_sys.v $common/dma_win.v $hd/pix_layer.v $hd/c64_text.v $common/trx_engine.v \
  $common/drv_dbg.v $hd/app_input.v $hd/rv_dbg.v $hd/bling_io.v $hd/case_leds.v $hd/reu_ddr_map.v $hd/dfx_decouple.v]
read_vhdl -library work $gvhdl

# layer 2: layer2_c264.v becomes the module "layer2" that top.v instantiates (top.v passes no parameters; REU_BASE keeps its default)
set fh [open ../cores/c264/layer2_c264.v]; set txt [read $fh]; close $fh
set n [regsub {module layer2_c264 } $txt {module layer2 } txt]
if {$n != 1} { error "layer2_c264.v: 'module layer2_c264 ' not found exactly once" }
set fh [open $bdir/layer2_mono.v w]; puts -nonewline $fh $txt; close $fh
# the host's file list above already holds cores/common/iec_bus.v, which the partial's recipe read besides the core
set c16 ../cores/c264/rtl
read_vhdl -library work [list \
  $c16/src/t65/T65_Pack.vhd $c16/src/t65/T65_ALU.vhd $c16/src/t65/T65_MCode.vhd $c16/src/t65/T65.vhd ]
read_verilog [list $c16/src/ted.v $c16/src/colors_to_rgb.v \
  $c16/trx/c264_nco.v $c16/trx/c264_keys.v $c16/trx/c264_audio.v $c16/trx/c264_mem.v $c16/trx/c264_win.v \
  $c16/trx/c264_regs.v $c16/trx/c264_pixw.v $c16/trx/c264_cpu.v $c16/trx/c264_core.v $bdir/layer2_mono.v ../cores/c1541/drive_leds_map.v]

read_xdc $hd/ddr2_pins.xdc
read_xdc $hd/pins.xdc
read_xdc $hd/gtp_pins.xdc
read_xdc $hd/usb_pins.xdc

set incs [list $core $hd]
synth_design -top top -part $part -generic FPGA_TYPE=$fpga_type -include_dirs $incs {*}$synth_defs
report_utilization -file $bdir/utilization_synth.rpt
report_utilization -hierarchical -hierarchical_depth 8 -file $bdir/utilization_hier_synth.rpt
write_checkpoint -force $bdir/synth.dcp
puts "=== T164 mono: synthesis done"
if {$stop eq "synth"} { return }

opt_design
opt_design -control_set_merge
write_checkpoint -force $bdir/opt.dcp
report_utilization -file $bdir/utilization_opt.rpt

proc impl_pass {place_dir} {
  place_design -directive $place_dir
  phys_opt_design -directive AggressiveExplore
  if {$::stop eq "place"} { return 1000.0 }
  route_design -directive Explore
  phys_opt_design -directive AggressiveExplore
  set p [get_timing_paths -setup -max_paths 1]
  if {[llength $p] == 0} { return 1000.0 }
  foreach d {AlternateReplication AggressiveFanoutOpt} {
    if {[get_property SLACK $p] >= 0} break
    phys_opt_design -directive $d
    set p [get_timing_paths -setup -max_paths 1]
    puts "=== post-route phys_opt $d: WNS [get_property SLACK $p] ns"
  }
  return [get_property SLACK $p]
}
# pass 1 is the C128's flat flow
set wns [impl_pass ExtraTimingOpt]
puts "=== impl pass 1 (ExtraTimingOpt): WNS $wns ns"
if {$wns < 0 && $stop eq "route"} {
  write_checkpoint -force $bdir/routed_pass1.dcp
  set best $wns; set best_dcp $bdir/routed_pass1.dcp
  foreach dir {AltSpreadLogic_high} {
    open_checkpoint $bdir/opt.dcp
    set w [impl_pass $dir]
    puts "=== impl pass ($dir): WNS $w ns"
    if {$w > $best} { set best $w; write_checkpoint -force $bdir/routed_alt.dcp; set best_dcp $bdir/routed_alt.dcp }
  }
  open_checkpoint $best_dcp
  puts "=== kept: $best_dcp (WNS $best ns)"
}
write_checkpoint -force $bdir/routed.dcp
report_utilization -file $bdir/utilization.rpt
report_utilization -hierarchical -hierarchical_depth 8 -file $bdir/utilization_hier.rpt
report_timing_summary -max_paths 5 -file $bdir/timing.rpt
report_route_status -file $bdir/route_status.rpt
report_design_analysis -congestion -file $bdir/congestion.rpt
puts "=== T164 mono: reports written"
if {$stop eq "route"} {
  write_bitstream -force $bdir/c264_mono.bit
  puts "=== bitstream: $bdir/c264_mono.bit [file size $bdir/c264_mono.bit] bytes"
}
