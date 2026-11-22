# T255: DDT's Mandelbrot demo as a LARGE core - one full bitstream of the XC7A50T, no partition: the slim top mono/top.v (clocks, HDMI, joystick, nothing else), the demo and the
# HDMI chain. Flow of cores/diag/mono/build_mono.tcl (the C128's flat flow): no HD.RECONFIGURABLE, no pblocks, no out-of-context checkpoints. Pins and clocks: mono/pins.xdc.
#
# Environment (all optional):
#   TRX_BUILD     output directory under host/ (default build_mono)
#   TRX_STOP      synth | place | route (default route = also writes the bitstream)
#   TRX_ENGINES   the number of Mandelbrot engines (default 20; 6 DSP each, the device has 120). The splash text takes the number from here.
# T164 3a Q1 ("Copies instead."): the HDMI files this build reads are this core's OWN COPY in cores/ddt_mandel/mono/host/ ($hd; mono/host/FROM). Vivado's working directory stays host/ (output
# directory, relative paths); no file of host/ is read. The ROM images (palette, font) are read from the core's own folders by $readmemh, relative to that directory.
set part xc7a50tfgg484-1
set bdir build_mono
if {[info exists ::env(TRX_BUILD)] && $::env(TRX_BUILD) ne ""} { set bdir $::env(TRX_BUILD) }
set stop route
if {[info exists ::env(TRX_STOP)] && $::env(TRX_STOP) ne ""} { set stop $::env(TRX_STOP) }
set engines 20
if {[info exists ::env(TRX_ENGINES)] && $::env(TRX_ENGINES) ne ""} { set engines $::env(TRX_ENGINES) }
set core ../vendor/icepi
source ../tools/vivado_lib.tcl
set hd     ../cores/ddt_mandel/mono/host
set dm     ../cores/ddt_mandel/mono
set d      ../cores/ddt_mandel
file mkdir $bdir
set ::trx_build $bdir
puts "=== T255 mono: $engines engines"

create_project -in_memory -part $part
set_property target_language Verilog [current_project]
set_property XPM_LIBRARIES {XPM_MEMORY} [current_project]
set_msg_config -id {Project 1-486} -new_severity INFO

# the GTP transmitters of the host (same configuration as cores/diag/mono/build_mono.tcl: 1.5 Gb/s from 150 MHz on REFCLK0 of quad 216, four lanes, 20 bit words)
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

# the HDMI chain: this core's copies; the encoder's three unforked files come from the vendor tree (as the host)
read_verilog -sv [list {*}[glob $hd/hdmi_enc/*.sv] $hd/hdmi_enc/hdmi.v $core/hdmi/audio_sample_packet.sv $core/hdmi/audio_info_frame.sv \
  $core/hdmi/source_product_description_info_frame.sv]
read_verilog [list $hd/hdmi_gtp.v $hd/hdmi_timing.v $hd/cdce_mode.v $hd/tmds_rep.v]
# DDT's engines, scheduler and buffers (rtl/ as he wrote them, trx/ the changed ones) and the glue of this core
read_verilog -sv [list $d/trx/pixel_gen_mandelbrot.sv $d/trx/fb_scanline_writer.sv $d/rtl/fb_bram_dma.sv $d/rtl/linebuf_iter8.sv \
  $d/rtl/framebuffer_bram.sv $d/rtl/joystick_buttons.sv]
read_verilog [list $d/trx/screen_matrix.v $dm/ddt_mandel_hd.v $dm/ddt_hdmi.v $dm/top.v]

read_xdc $dm/pins.xdc
read_xdc $hd/gtp_pins.xdc

synth_design -top top -part $part -generic MANDEL_CORES=$engines -include_dirs [list $core $hd]
report_utilization -file $bdir/utilization_synth.rpt
report_utilization -hierarchical -hierarchical_depth 8 -file $bdir/utilization_hier_synth.rpt
write_checkpoint -force $bdir/synth.dcp
set fh [open $bdir/utilization_synth.rpt]
foreach ln [split [read $fh] "\n"] {
  if {[regexp {^\| (Slice LUTs\*?|Slice Registers|Block RAM Tile|RAMB36|RAMB18|DSPs) } $ln]} { puts "=== T255 synth $ln" }
}
close $fh
puts "=== T255 mono: synthesis done"
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
puts "=== T255 mono: reports written"
if {$stop eq "route"} {
  write_bitstream -force $bdir/ddt_mandel_mono.bit
  puts "=== bitstream: $bdir/ddt_mandel_mono.bit [file size $bdir/ddt_mandel_mono.bit] bytes"
}
