# T113 §5.3: the C128 as a monolithic full bitstream, measurement build. The host top (this core's copy mono/top.v of host/top.v, T140) and layer2_c128 (as the
# module "layer2") in ONE design: no HD.RECONFIGURABLE, no pb_l1 / pb_l2 / pb_cpu pblocks, no out-of-context checkpoints, no host cache.
# Pins and timing constraints are the host's own (read from mono/host/*.xdc) plus mono/avid_pins.xdc; the one timing exception host/build.tcl adds after synthesis
# (the 8502's RDY, two clk2x periods) is repeated. Nothing under host/ is edited: Vivado's working directory is host/ and this
# script is read from cores/c128/mono/.
#
# Environment (all optional):
#   TRX_BUILD   output directory under host/ (default build_mono)
#   TRX_STOP    synth | place | route (default route = also writes the bitstream)
#   TRX_DEFS    extra Verilog defines for the whole design, space separated (e.g. "VDC_WHOLE VDC64 ROM_P2|ROM_CACHE SID2 SOCK_AUDIO TAPE Z80")
#   TRX_Z80=1   read the Z80 sources (with the define Z80);  TRX_VDC=none|whole|split (core_sources.tcl, also sets the define)
#   TRX_REU=1   keeps the REU (the core_sources.tcl define NO_REU is dropped)
#   TRX_STUB    host modules replaced by a port-compatible stub (cores/c128/mono/stubs): overlay con_ring
#   TRX_TOP_OFF host switches top.v knows, appended to the defines: NO_UCI NO_JTAG_DBG NO_STREAMS
# T164 3a Q1 (Alex, 2026-11-01: "Copies instead."): the host files this build reads are this core's OWN COPY in cores/c128/mono/host/ ($hd; mono/host/FROM says where they
# come from and what was edited). A change in host/ moves neither this core's cache key nor this build; a host fix gets here only by copying it in on purpose. Vivado's working
# directory stays host/ for the output directory and the ../vendor, ../tools, ../cores paths; no host file is read from it. Where a comment below says host/X, it means mono/host/X.
set part xc7a50tfgg484-1
set bdir build_mono
if {[info exists ::env(TRX_BUILD)] && $::env(TRX_BUILD) ne ""} { set bdir $::env(TRX_BUILD) }
set stop route
if {[info exists ::env(TRX_STOP)] && $::env(TRX_STOP) ne ""} { set stop $::env(TRX_STOP) }
set fpga_type 2
set core ../vendor/icepi
set kawari $core/vicii-kawari/hdl
source ../tools/vivado_lib.tcl
set common ../cores/common
set hd     ../cores/c128/mono/host
set c64u   ../cores/c64u
set c128   ../cores/c128
file mkdir $bdir
file mkdir mem
set ::trx_build $bdir
set ::env(TRX_MONO) 1

# the sources of the core and of its layer 2 (core_sources.tcl stops after defining them when TRX_MONO is set)
source $c128/core_sources.tcl
source $hd/gideon_vhdl.tcl
# T133: the RISC-V's caches of this build (Alex: "bursts/caches optional per core"). The core leaves 1.5 block RAM tiles over with the host's 8 KB caches (run 1013:
# 75 of 75 after synthesis, placement failed), so this build lists its own cache_cfg_pkg.vhd (2 KB instruction and 2 KB data cache, 16-byte lines) in place of the host's.
set k [lsearch -exact $gvhdl $hd/cache_cfg_pkg.vhd]
if {$k < 0} { error "gideon_vhdl.tcl lists no cache_cfg_pkg.vhd" }
set gvhdl [lreplace $gvhdl $k $k $c128/mono/cache_cfg_pkg.vhd]
puts "=== T133 mono: cache sizes from cores/c128/mono/cache_cfg_pkg.vhd"

# T140 D29: the UART receive FIFO (uart_dma.vhd, 511 x 8, sync_fifo with ram_style "auto") went to LUTRAM in the D28 run (RAMB18 1 -> 0, LUTRAM +96)
# when block RAM got short; this build pins it to block RAM with a copy of uart_dma.vhd (vendor/gideon and the host stay untouched).
set k [lsearch -glob $gvhdl */uart_dma.vhd]
if {$k < 0} { error "gideon_vhdl.tcl lists no uart_dma.vhd" }
set fh [open [lindex $gvhdl $k]]; set t [read $fh]; close $fh
set n [string first "g_threshold    => 500,\n        g_fall_through => true" $t]
if {$n < 0} { error "uart_dma.vhd: the i_rxfifo generic map has changed" }
set t [string map [list "g_threshold    => 500,\n        g_fall_through => true" "g_threshold    => 500,\n        g_storage_xil  => \"block\",\n        g_fall_through => true"] $t]
set fh [open $bdir/uart_dma.vhd w]; puts -nonewline $fh $t; close $fh
set gvhdl [lreplace $gvhdl $k $k $bdir/uart_dma.vhd]
puts "=== T140 D29: uart_dma i_rxfifo pinned to block RAM"

# T140 Q2b/Q2c: no Ethernet MAC in this build (the WiFi UART stays): the copy's ultimate_sys.vhd with g_net = 0, top.v passes it under the define NO_NET (TRX_DEFS).

# Defines: the core's (core_synth, minus the OOC-only parts) and the variant's
set defs {}
set i 0
foreach t $core_synth {
  if {$t eq "-verilog_define"} { lappend defs [lindex $core_synth [expr {$i + 1}]] }
  incr i
}
if {[info exists ::env(TRX_REU)] && $::env(TRX_REU) eq "1"} { set defs [lsearch -all -inline -not -exact $defs NO_REU] }
lappend defs MONO RAM_P2
if {[info exists ::env(TRX_DEFS)]} { foreach d $::env(TRX_DEFS) { lappend defs $d } }
if {[info exists ::env(TRX_TOP_OFF)]} { foreach d $::env(TRX_TOP_OFF) { lappend defs $d } }
if {[info exists ::env(TRX_Z80)] && $::env(TRX_Z80) eq "1"} { lappend defs Z80 }
set defs [lsort -unique $defs]
set synth_defs {}
foreach d $defs { lappend synth_defs -verilog_define $d }
puts "=== T113 mono: defines $defs"

# --- reading: every file once (the core and the host both read the HDMI sources, and Gideon's VHDL twice over) ---
set ::seen [dict create]
rename read_verilog _read_verilog
rename read_vhdl _read_vhdl
proc _dedupe {args} {
  set files {}
  foreach a $args {
    foreach f $a {
      set k [file normalize $f]
      if {[dict exists $::seen $k]} continue
      dict set ::seen $k 1
      lappend files $f
    }
  }
  return $files
}
proc read_verilog {args} {
  set flags {}; set rest {}
  foreach a $args { if {[string match -* $a]} { lappend flags $a } else { lappend rest $a } }
  set files [_dedupe {*}$rest]
  if {[llength $files]} { _read_verilog {*}$flags $files }
}
proc read_vhdl {args} {
  set flags {}; set rest {}; set skip 0
  foreach a $args {
    if {$skip} { lappend flags $a; set skip 0; continue }
    if {$a eq "-library"} { lappend flags $a; set skip 1; continue }
    if {[string match -* $a]} { lappend flags $a } else { lappend rest $a }
  }
  set files [_dedupe {*}$rest]
  if {[llength $files]} { _read_vhdl {*}$flags $files }
}

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

# host (the file list of the A500's build_mono.tcl, top.v this core's fork)
read_verilog -sv $hd/hdmi_gtp.v
# T164 G4: the encoder is mono/host/hdmi_enc (six forked files); the three unforked ones come from the vendor tree
read_verilog -sv [list {*}[glob $hd/hdmi_enc/*.sv] $hd/hdmi_enc/hdmi.v $core/hdmi/audio_sample_packet.sv $core/hdmi/audio_info_frame.sv \
  $core/hdmi/source_product_description_info_frame.sv]
set host_v [list $c128/mono/top.v $hd/vic_scaler.v $hd/pal_ram.v $hd/hdmi_out.v $hd/hdmi_timing.v $hd/mode_req.v $hd/cdce_mode.v $hd/tmds_rep.v $hd/mode_ctl.v $hd/c64u_keyboard.v $hd/bling_keyboard.v $hd/jtag_dbg.v $hd/ddr2_port.v $hd/ddr2_ctrl.v $hd/ddr2_lean.v $hd/cal_seq.v $hd/i2s_out.v $hd/trx_gain.v $hd/portlock_clk.v $hd/esp_ctl.v $hd/ui_buttons.v \
  $hd/con_ring.v $hd/pktport_host.v $hd/app_i2c.v $hd/overlay.v $hd/usb_phy.v $common/iec_bus.v \
  $hd/cart_fill.v $common/trx_io.v $common/trx_sys.v $common/dma_win.v $hd/pix_layer.v $hd/c64_text.v $common/trx_engine.v \
  $common/drv_dbg.v $hd/app_input.v $hd/rv_dbg.v $hd/bling_io.v $hd/case_leds.v $hd/reu_ddr_map.v $hd/dfx_decouple.v]
# a variant may leave a host block out: its file is replaced by a stub with the same ports (cores/c128/mono/stubs), host/ stays as it is
if {[info exists ::env(TRX_STUB)]} {
  foreach m $::env(TRX_STUB) {
    set k [lsearch -exact $host_v $hd/$m.v]
    if {$k < 0} { error "TRX_STUB: no host file $m.v" }
    set host_v [lreplace $host_v $k $k $c128/mono/stubs/${m}_stub.v]
    puts "=== T113 mono: host block $m stubbed"
  }
}
read_verilog $host_v
# T140 D1: the analogue picture (S-Video, composite) on the A/V port: our wrapper and MiSTer's encoder (vendor/mister, GPL v2+)
read_verilog -sv [list $c128/mono/c128_avout.v $c128/mono/c128_yc.v $c128/mono/c128_avpins.v]
read_vhdl -library work $gvhdl

# the C128 core (core_read of core_sources.tcl, without its out-of-context XDC)
regsub {read_xdc -mode out_of_context[^\n]*} $core_read {} core_read_flat
# icepi's 6502n packages (pkg_6502_defs / decode / opcodes) share their names with Gideon's, which the host and the c64u drive read into
# the same library: here they are separate partitions. The core's copies get other package names (entities do not clash: Gideon's carry drv_).
file mkdir $bdir/ip6502
foreach f [glob $core/6502n/*.vhd] {
  set fh [open $f]; set t [read $fh]; close $fh
  regsub -all -nocase {pkg_6502_(defs|decode|opcodes)} $t {ipkg_6502_\1} t
  set fh [open $bdir/ip6502/[file tail $f] w]; puts -nonewline $fh $t; close $fh
}
set core_read_flat [string map [list {$core/6502n/} "$bdir/ip6502/"] $core_read_flat]
uplevel #0 $core_read_flat

# layer 2: layer2_c128.v is renamed "layer2" (the module top.v instantiates); the c64u copies of Gideon's VHDL are read where the host
# does not read a file of the same name (39 of the 57 are byte-identical to vendor/gideon, checked with cmp); drive_registers differs
# (no 1571) and gets another entity name
set hostnames {}
foreach f $gvhdl { lappend hostnames [file tail $f] }
set fh [open $c128/layer2_c128.v]; set txt [read $fh]; close $fh
regsub {module layer2_c128 } $txt {module layer2 } txt
set fh [open $bdir/layer2_mono.v w]; puts -nonewline $fh $txt; close $fh
set patched {}
foreach {fn pat rep} [list \
    $gdir/drive_registers.vhd {entity drive_registers is} {entity drive_registers_c64u is} \
    $gdir/mm_drive.vhd {entity work.drive_registers} {entity work.drive_registers_c64u}] {
  set fh [open $fn]; set t [read $fh]; close $fh
  set t [string map [list $pat $rep] $t]
  if {[string match *drive_registers.vhd $fn]} { set t [string map {{architecture rtl of drive_registers is} {architecture rtl of drive_registers_c64u is}} $t] }
  set fh [open $bdir/[file tail $fn] w]; puts -nonewline $fh $t; close $fh
  lappend patched [file tail $fn]
}
set l2_vhdl {}
foreach f $g_files {
  set n [file tail $f]
  if {$n in $patched} { lappend l2_vhdl $bdir/$n } elseif {$n ni $hostnames} { lappend l2_vhdl $f }
}
read_vhdl -library work $l2_vhdl
read_verilog -sv [lreplace $l2_v 0 0 $bdir/layer2_mono.v]

read_xdc $hd/ddr2_pins.xdc
read_xdc $hd/pins.xdc
read_xdc $hd/gtp_pins.xdc
read_xdc $hd/usb_pins.xdc
read_xdc $c128/mono/avid_pins.xdc

set incs [list $core $kawari $core/c64_redip_sid/reDIP-SID/gateware $c64u $c128 $hd]
synth_design -top top -part $part -generic FPGA_TYPE=$fpga_type -include_dirs $incs {*}$synth_defs
report_utilization -file $bdir/utilization_synth.rpt
report_utilization -hierarchical -hierarchical_depth 8 -file $bdir/utilization_hier_synth.rpt
write_checkpoint -force $bdir/synth.dcp
puts "=== T113 mono: synthesis done"
if {$stop eq "synth"} { return }

opt_design
opt_design -control_set_merge
set rdy_nets [get_nets -quiet -hier -filter {NAME =~ l2/c64/c64/cpu_rdy}]
if {[llength $rdy_nets] == 1} {
  set sid2x_c [get_clocks -of_objects [get_pins mmcm_sys/CLKOUT1]]
  set_multicycle_path 2 -setup -end -through $rdy_nets -to $sid2x_c
  set_multicycle_path 1 -hold  -end -through $rdy_nets -to $sid2x_c
  puts "=== T29: cpu_rdy paths to clk2x timed with two clk2x periods"
} else {
  puts "=== T29: WARNING cpu_rdy net not found ([llength $rdy_nets]), no multicycle path"
}
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
puts "=== T113 mono: reports written"
if {$stop eq "route"} {
  write_bitstream -force $bdir/c128_mono.bit
  puts "=== bitstream: $bdir/c128_mono.bit [file size $bdir/c128_mono.bit] bytes"
}
