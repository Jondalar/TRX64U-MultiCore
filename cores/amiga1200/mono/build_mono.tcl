# T97 5.14: the Amiga 1200 as a LARGE core - one full bitstream of the XC7A50T, no partition. Flow and structure of cores/c128/mono/build_mono.tcl
# (T113 5.3, branch t113-reset; taken here as the generic flat build, the C128's machine is not): the host top (mono/host/top.v, unchanged copy) and the
# Amiga's layer2_amiga (as the module "layer2" top.v instantiates) in ONE design: no HD.RECONFIGURABLE, no pb_l1 / pb_l2 / pb_cpu pblocks, no
# decoupler fences, no out-of-context checkpoints, no host cache, no ICAP swap. Pins and timing constraints are the host's own (mono/host/*.xdc).
# Nothing under host/ is edited: Vivado's working directory is host/ and this script is read from cores/amiga1200/mono/.
#
# T164 WP18 (Alex, 2026-11-01: "NO slices! I want complete, whole things."): the A1200 has no fork of a host file any more. Picture (modes, Scale, Smoothing, status 0x4F04), menu overlay,
# master volume, case lamp and keys are the host copy's, as for the A500; the A1200's own are layer 2 (TG68K, AGA, Zorro III, its SD / floppy / HDD glue, its registers) and the 25 MHz SD (cores/amiga1200/sd/sd_mono.tcl).
#
# What is in it: Gideon's RISC-V and the blocks around it (vendor/gideon: UART, SPI/SD, Ethernet, USB through usb_phy.v, I2C), our DDR2 controller and arbiter (ddr2_ctrl, ddr2_lean, cal_seq, ddr2_port), hdmi_out (the host copy) with the pixel layer (format 2, T136 D1; SMOOTH 1 as the host's top sets it),
# the menu overlay and master volume (trx_gain), HDMI (hdmi_enc + hdmi_gtp + GTP), the per-mode clock chip sequencer (cdce_mode), keyboard (c64u_keyboard, bling_keyboard, bling_io, app_input), and cores/amiga (minimig-aga, nanomig, trx/*) + cores/amiga1200/rtl (TG68K 68020, its own copies) as layer 2.
# NO_C64VIDEO (core.toml mono_env): the A1200 is no C64, so hdmi_out leaves out the VIC path (vic_scaler, pal_ram) and the freeze text (c64_text), as the console builds do. Ethernet stays in.
# The host list is that of host/build.tcl; the Amiga list is the one of cores/amiga1200/core_sources.tcl, which stops after defining it when
# TRX_MONO is set (the partial's recipe stays one file). The layer-2 module is the Amiga's own: no c64 core, no 1541, no Gideon drive VHDL.
#
# Environment (all optional):
#   TRX_BUILD     output directory under host/ (default build_mono)
#   TRX_STOP      synth | place | route (default route = also writes the bitstream)
#   TRX_FEATURES  host blocks, as in cores/c64ref/core_sources.tcl: a list of uci, jtag, streams (default "jtag": the DFX build's host)
#   TRX_DEFS      extra Verilog defines for the whole design, space separated
# T164 3a Q1 (Alex, 2026-11-01: "Copies instead."): the host files this build reads are this core's OWN COPY in cores/amiga1200/mono/host/ ($hd; mono/host/FROM says where they
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
set hd     ../cores/amiga1200/mono/host
set a1200  ../cores/amiga1200
file mkdir $bdir
set ::trx_build $bdir
set ::env(TRX_MONO) 1

# the Amiga's sources (core_sources.tcl stops after defining them when TRX_MONO is set): mm_v, a12_vhdl, a12_defs, a12_generics; and Gideon's VHDL
source $hd/gideon_vhdl.tcl
# T149 (D5): the SD interface with IOB flops and 25 MHz, in this flat build only (host/ stays as it is): cores/amiga1200/sd/sd_mono.tcl
source ../cores/amiga1200/sd/sd_mono.tcl
trx_sd_swap $bdir
source $a1200/core_sources.tcl

# Defines: the Amiga's, the host's switches from TRX_FEATURES (as the reference core derives them) and the variant's
set trx_on {jtag}
if {[info exists ::env(TRX_FEATURES)] && $::env(TRX_FEATURES) ne ""} { set trx_on [split $::env(TRX_FEATURES) ", "] }
set defs $a12_defs
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

# host (the file list of host/build.tcl as in cores/amiga/mono/build_mono.tcl; every file is the core's copy in mono/host, T164 WP18: no fork of top, overlay, pix_layer,
# bling_io, hdmi_timing, cdce_mode, hdmi_modes or the encoder is left)
read_verilog -sv $hd/hdmi_gtp.v
read_verilog -sv [list {*}[glob $hd/hdmi_enc/*.sv] $hd/hdmi_enc/hdmi.v $core/hdmi/audio_sample_packet.sv $core/hdmi/audio_info_frame.sv \
  $core/hdmi/source_product_description_info_frame.sv]
read_verilog [list $hd/top.v $hd/vic_scaler.v $hd/pal_ram.v $hd/hdmi_out.v $hd/hdmi_timing.v $hd/mode_req.v $hd/cdce_mode.v $hd/tmds_rep.v $hd/mode_ctl.v $hd/c64u_keyboard.v $hd/bling_keyboard.v $hd/jtag_dbg.v $hd/ddr2_port.v $hd/ddr2_ctrl.v $hd/ddr2_lean.v $hd/cal_seq.v $hd/i2s_out.v $hd/trx_gain.v $hd/trx_tone.v $hd/portlock_clk.v $hd/esp_ctl.v $hd/ui_buttons.v \
  $hd/con_ring.v $hd/pktport_host.v $hd/app_i2c.v $hd/overlay.v $hd/usb_phy.v $common/iec_bus.v \
  $hd/cart_fill.v $common/trx_io.v $common/trx_sys.v $common/dma_win.v $hd/pix_layer.v $hd/c64_text.v $common/trx_engine.v \
  $common/drv_dbg.v $hd/app_input.v $hd/rv_dbg.v $hd/bling_io.v $hd/case_leds.v $hd/reu_ddr_map.v $hd/dfx_decouple.v]
read_vhdl -library work $gvhdl

# layer 2: cores/amiga/layer2_amiga.v becomes the module "layer2" that top.v instantiates (top.v passes no parameters), with the A1200's three
# generics as the parameter defaults (what the partial's synth_design -generic passes)
set fh [open ../cores/amiga/layer2_amiga.v]; set txt [read $fh]; close $fh
set n [regsub {module layer2_amiga } $txt {module layer2 } txt]
if {$n != 1} { error "layer2_amiga.v: 'module layer2_amiga ' not found exactly once" }
foreach {g v} $a12_generics {
  set n [regsub "parameter integer $g = \\d+" $txt "parameter integer $g = $v" txt]
  if {$n != 1} { error "layer2_amiga.v: parameter $g not found exactly once" }
}
# T159 D2/D-all: layer2_amiga.v itself carries drive_leds_n = {1'b1, ~a_drv_hd, ~a_drv_led} (amiga_core drv_led / drv_hd); top.v feeds drive_leds_n[0]
# to case_leds as "drive A activity" (selector source 4) and [1] to bling_io's blue.
set fh [open $bdir/layer2_mono.v w]; puts -nonewline $fh $txt; close $fh
# One flat design has one module namespace. In the DFX build layer 2 is synthesized on its own, so the Amiga's trx_afifo
# (cores/amiga/rtl/trx/trx_afifo.v: wrst/wr/wdata/rrst/rdata) and the host's (cores/common/trx_engine.v: we/wd/re/rd) never meet;
# here they do, and Vivado bound amiga_mem's instance to the host's (run 160: "named port connection 'wrst' does not exist").
# The Amiga's files are read as copies with the module renamed to amiga_trx_afifo; nothing in the tree is edited.
set mm_mono [list]
foreach f $mm_v {
  set fh [open $f]; set txt [read $fh]; close $fh
  if {[regsub -all {\mtrx_afifo\M} $txt {amiga_trx_afifo} txt] > 0} {
    set c $bdir/mono_[file tail $f]
    if {[lsearch -exact $mm_mono $c] >= 0} { error "two Amiga sources named [file tail $f] use trx_afifo" }
    set fh [open $c w]; puts -nonewline $fh $txt; close $fh
    lappend mm_mono $c
  } else {
    lappend mm_mono $f
  }
}
read_verilog -sv $mm_mono
read_vhdl -library work $a12_vhdl
read_verilog -sv $bdir/layer2_mono.v

read_xdc $hd/ddr2_pins.xdc
read_xdc $hd/pins.xdc
trx_sd_xdc
read_xdc $hd/gtp_pins.xdc
read_xdc $hd/usb_pins.xdc

set incs [list $core $hd ../cores/amiga/rtl/src/minimig-aga ../cores/amiga/rtl/trx]
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
# pass 1 is the C128's flat flow; then, while timing misses: the place directive of the partial (cores/amiga1200/core.toml place) and two extra directives
set wns [impl_pass ExtraTimingOpt]
puts "=== impl pass 1 (ExtraTimingOpt): WNS $wns ns"
if {$wns < 0 && $stop eq "route"} {
  write_checkpoint -force $bdir/routed_pass1.dcp
  set best $wns; set best_dcp $bdir/routed_pass1.dcp
  # main run de1061fe (T195 + T192, 2026-11-09): -0.103 ns on a path nobody touched (hout/mtim yact0 -> the HDMI audio packet CE), placement
  # noise after the board-button logic came in; two more placement directives before the run is red
  foreach dir {AltSpreadLogic_high ExtraNetDelay_high ExtraPostPlacementOpt} {
    open_checkpoint $bdir/opt.dcp
    set w [impl_pass $dir]
    puts "=== impl pass ($dir): WNS $w ns"
    if {$w > $best} { set best $w; write_checkpoint -force $bdir/routed_alt.dcp; set best_dcp $bdir/routed_alt.dcp }
    if {$best >= 0} break
  }
  open_checkpoint $best_dcp
  puts "=== kept: $best_dcp (WNS $best ns)"
}
write_checkpoint -force $bdir/routed.dcp
report_utilization -file $bdir/utilization.rpt
report_utilization -hierarchical -hierarchical_depth 8 -file $bdir/utilization_hier.rpt
report_timing_summary -max_paths 5 -file $bdir/timing.rpt
if {[catch trx_report_sd e]} { puts "=== T149 SD report failed: $e" }
report_route_status -file $bdir/route_status.rpt
report_design_analysis -congestion -file $bdir/congestion.rpt
puts "=== T164 mono: reports written"
if {$stop eq "route"} {
  write_bitstream -force $bdir/amiga1200_mono.bit
  puts "=== bitstream: $bdir/amiga1200_mono.bit [file size $bdir/amiga1200_mono.bit] bytes"
}
