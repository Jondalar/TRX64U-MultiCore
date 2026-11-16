# T182: the C64SCPU core as a monolithic full bitstream (forked from cores/c128/mono/build_mono.tcl, T113 §5.3). The host top (this core's copy mono/top.v of host/top.v) and layer2_c64scpu (as the
# module "layer2") in ONE design: no HD.RECONFIGURABLE, no pb_l1 / pb_l2 / pb_cpu pblocks, no out-of-context checkpoints, no host cache.
# Pins and timing constraints are the host's own (read from mono/host/*.xdc) plus mono/avid_pins.xdc; the one timing exception host/build.tcl adds after synthesis
# (the CPU's RDY, two clk2x periods) is repeated. Nothing under host/ is edited: Vivado's working directory is host/ and this
# script is read from cores/c64scpu/mono/.
#
# Environment (all optional):
#   TRX_BUILD   output directory under host/ (default build_mono)
#   TRX_STOP    synth | place | route (default route = also writes the bitstream)
#   TRX_DEFS    extra Verilog defines for the whole design, space separated (e.g. "SID2 SOCK_AUDIO TAPE NO_NET")
#   TRX_REU=1   keeps the REU (the core_sources.tcl define NO_REU is dropped)
#   TRX_STUB    host modules replaced by a port-compatible stub (cores/c64scpu/mono/stubs): overlay con_ring
#   TRX_TOP_OFF host switches top.v knows, appended to the defines: NO_UCI NO_JTAG_DBG NO_STREAMS
# T164 3a Q1 (Alex, 2026-11-01: "Copies instead."): the host files this build reads are this core's OWN COPY in cores/c64scpu/mono/host/ ($hd; mono/host/FROM says where they
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
set hd     ../cores/c64scpu/mono/host
set c64u   ../cores/c64u
set c64scpu   ../cores/c64scpu
file mkdir $bdir
file mkdir mem
set ::trx_build $bdir
set ::env(TRX_MONO) 1

# the sources of the core and of its layer 2 (core_sources.tcl stops after defining them when TRX_MONO is set)
source $c64scpu/core_sources.tcl
source $hd/gideon_vhdl.tcl
# T133: the RISC-V's caches of this build (Alex: "bursts/caches optional per core"). The core leaves 1.5 block RAM tiles over with the host's 8 KB caches (run 1013:
# 75 of 75 after synthesis, placement failed), so this build lists its own cache_cfg_pkg.vhd (2 KB instruction and 2 KB data cache, 16-byte lines) in place of the host's.
set k [lsearch -exact $gvhdl $hd/cache_cfg_pkg.vhd]
if {$k < 0} { error "gideon_vhdl.tcl lists no cache_cfg_pkg.vhd" }
set gvhdl [lreplace $gvhdl $k $k $c64scpu/mono/cache_cfg_pkg.vhd]
puts "=== T133 mono: cache sizes from cores/c64scpu/mono/cache_cfg_pkg.vhd"

# T140 Q2b/Q2c: no Ethernet MAC in this build (the WiFi UART stays): the copy's ultimate_sys.vhd with g_net = 0, top.v passes it under the define NO_NET (TRX_DEFS).

# Defines: the core's (core_synth, minus the OOC-only parts) and the variant's
set defs {}
set i 0
foreach t $core_synth {
  if {$t eq "-verilog_define"} { lappend defs [lindex $core_synth [expr {$i + 1}]] }
  incr i
}
if {[info exists ::env(TRX_REU)] && $::env(TRX_REU) eq "1"} { set defs [lsearch -all -inline -not -exact $defs NO_REU] }
lappend defs MONO
if {[info exists ::env(TRX_DEFS)]} { foreach d $::env(TRX_DEFS) { lappend defs $d } }
if {[info exists ::env(TRX_TOP_OFF)]} { foreach d $::env(TRX_TOP_OFF) { lappend defs $d } }
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
set host_v [list $c64scpu/mono/top.v $hd/vic_scaler.v $hd/pal_ram.v $hd/hdmi_out.v $hd/hdmi_timing.v $hd/mode_req.v $hd/cdce_mode.v $hd/tmds_rep.v $hd/mode_ctl.v $hd/c64u_keyboard.v $hd/bling_keyboard.v $hd/jtag_dbg.v $hd/ddr2_port.v $hd/ddr2_ctrl.v $hd/ddr2_lean.v $hd/cal_seq.v $hd/i2s_out.v $hd/trx_gain.v $hd/trx_tone.v $hd/portlock_clk.v $hd/esp_ctl.v $hd/ui_buttons.v \
  $hd/con_ring.v $hd/pktport_host.v $hd/app_i2c.v $hd/overlay.v $hd/usb_phy.v $common/iec_bus.v \
  $hd/cart_fill.v $common/trx_io.v $common/trx_sys.v $common/dma_win.v $hd/pix_layer.v $hd/c64_text.v $common/trx_engine.v \
  $common/drv_dbg.v $hd/app_input.v $hd/rv_dbg.v $hd/bling_io.v $hd/case_leds.v $hd/reu_ddr_map.v $hd/dfx_decouple.v]
# a variant may leave a host block out: its file is replaced by a stub with the same ports (cores/c64scpu/mono/stubs), host/ stays as it is
if {[info exists ::env(TRX_STUB)]} {
  foreach m $::env(TRX_STUB) {
    set k [lsearch -exact $host_v $hd/$m.v]
    if {$k < 0} { error "TRX_STUB: no host file $m.v" }
    set host_v [lreplace $host_v $k $k $c64scpu/mono/stubs/${m}_stub.v]
    puts "=== T113 mono: host block $m stubbed"
  }
}
read_verilog $host_v
# T140 D1: the analogue picture (S-Video, composite) on the A/V port: our wrapper and MiSTer's encoder (vendor/mister, GPL v2+)
read_verilog -sv [list $c64scpu/mono/c64scpu_avout.v $c64scpu/mono/c64scpu_yc.v $c64scpu/mono/c64scpu_avpins.v]
read_vhdl -library work $gvhdl

# the core (core_read of core_sources.tcl, without its out-of-context XDC)
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

# layer 2: layer2_c64scpu.v is renamed "layer2" (the module top.v instantiates); the c64u copies of Gideon's VHDL are read where the host
# does not read a file of the same name (39 of the 57 are byte-identical to vendor/gideon, checked with cmp); drive_registers differs
# (no 1571) and gets another entity name
set hostnames {}
foreach f $gvhdl { lappend hostnames [file tail $f] }
set fh [open $c64scpu/layer2_c64scpu.v]; set txt [read $fh]; close $fh
regsub {module layer2_c64scpu } $txt {module layer2 } txt
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
read_xdc $c64scpu/mono/avid_pins.xdc

set incs [list $core $kawari $core/c64_redip_sid/reDIP-SID/gateware $c64u $c64scpu $hd]
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
# T182 step 2 (T2, spec 5.4 table): the 65C816 (cpu_0/cpu = s816_cpu) takes a step only on its clock enable core_ce = g_edge && phi2_l, never closer than 3 clk2x cycles
# (asserted by the bench, sim/tb_c64scpu.v; step 4's 20 MHz enable must keep it). Every register of s816_cpu changes only on that enable, so a path between two of them,
# or from a register that changes at another time than the enable (the sampled bus data, the port) into them, has N = 3 clk2x periods. NOT a false path. Not covered, left at
# one period on purpose: the enable's own path (g_edge, phi2_l -> core_ce -> the CE pins), the asynchronous reset rst_q, and cpu_rdy (its own two-period exception above).
# The one register that does not obey the enable is the XCE branch of A/X/Y/SP in s816_cpu (it writes a constant whenever IR = XCE, whatever EN is): see spec 5.4.
if {[llength $rdy_nets] == 1} {
  set pfx [string range [get_property NAME $rdy_nets] 0 end-[string length cpu_rdy]]
  set seqf {IS_SEQUENTIAL}
  set cpu_r  [get_cells -quiet -hier -filter "$seqf && NAME =~ ${pfx}cpu_0/cpu/*"]
  set smp_r  [get_cells -quiet -hier -filter "$seqf && (NAME =~ ${pfx}cpu_0/rdy_r_reg* || NAME =~ ${pfx}cpu_0/irq_r_reg* || NAME =~ ${pfx}cpu_0/nmi_r_reg* || NAME =~ ${pfx}cpu_0/din_r_reg*)"]
  set port_r [get_cells -quiet -hier -filter "$seqf && NAME =~ ${pfx}cpu_0/pport/*"]
  set outp_r [get_cells -quiet -hier -filter "$seqf && (NAME =~ ${pfx}cpu_0/pc_q_reg* || NAME =~ ${pfx}addr_a_reg* || NAME =~ ${pfx}we_a_reg* || NAME =~ ${pfx}dout_a_reg*)"]
  # T182 step 3 (spec 5.5): the memory map beside the CPU (cpu_0/smem = scpu_mem.v), its registers without the cache (cpu_0/smem/cache/*: that has its own single-cycle pipeline,
  # and lk_q, the byte the CPU takes, arrives together with `hit` and is taken at the very next edge: it stays at one period). scpu_mem counts clk2x cycles from the CPU's enable (cnt):
  # sv_* sample the CPU's outputs, b_* take them at cnt 3, the decision is registered into tx_* at cnt 5, and the enable stays withheld until tx_valid (cnt 5) and tx_valid_q: every
  # path between the CPU's registers and these has the three periods of the recipe; int_q (int_r) is registered at cnt 5 and read at the next enable.
  set smm_r [get_cells -quiet -hier -filter "$seqf && NAME =~ ${pfx}cpu_0/smem/* && NAME !~ ${pfx}cpu_0/smem/cache/*"]
  puts "=== T182 T2 multicycle: cpu regs [llength $cpu_r], sampled-input regs [llength $smp_r], port regs [llength $port_r], consumer regs [llength $outp_r], smem regs [llength $smm_r]"
  if {[llength $cpu_r] < 200 || [llength $smp_r] < 8 || [llength $port_r] < 8 || [llength $outp_r] < 8 || [llength $smm_r] < 100} { error "T182 T2: a register set of the multicycle recipe is (nearly) empty" }
  # cpu regs -> cpu regs, the sampled inputs (din_r, rdy_r, irq_r, nmi_r: change at phi2_n, a third of a PHI2 cycle before the enable) and the port -> cpu regs,
  # cpu regs -> the port, pc_q, the arbiter's input stage
  foreach {from to} [list $cpu_r $cpu_r  $cpu_r $port_r  $cpu_r $outp_r  $port_r $smm_r] {
    set_multicycle_path 3 -setup -end -from $from -to $to
    set_multicycle_path 2 -hold  -end -from $from -to $to
  }
  # T182 step 4 (spec 5.6, 5.4 f): at 20 MHz the enable is not tied to phi2_l. An I/O cycle's enable comes in the clock after n_d[1], i.e. two clocks after din_r / rdy_r / irq_r / nmi_r and
  # the port registers were loaded at the PHI2 fall (they are loaded at the clk edge that ends the n_ev clock; n_d[0] is visible in the next clock, n_d[1] in the one after, core_ce
  # in that one): two periods, hold 1. At 1 MHz the enable is >= 17 clk2x cycles later, at 20 MHz an enable can come at any other time, but then these registers do not change.
  foreach {from to} [list $smp_r $cpu_r  $port_r $cpu_r] {
    set_multicycle_path 2 -setup -end -from $from -to $to
    set_multicycle_path 1 -hold  -end -from $from -to $to
  }
  # T182 step 4 (spec 5.6): scpu_mem samples the CPU's address, class bits and data in EVERY clock (b_*, the class table's and the cache's block RAM addresses) and decides from the
  # values of the second clock after the enable on: two periods, hold 1. The paths from scpu_mem's registers and block RAMs INTO the CPU (core_ce at the CE pins, the byte of the
  # cache, the class bits to the data mux) are single-period: the old `smm_r -> cpu_r` exception is gone on purpose.
  set cch_r [get_cells -quiet -hier -filter "$seqf && NAME =~ ${pfx}cpu_0/smem/cache/*"]
  foreach {from to} [list $cpu_r $smm_r  $cpu_r $cch_r] {
    set_multicycle_path 2 -setup -end -from $from -to $to
    set_multicycle_path 1 -hold  -end -from $from -to $to
  }
  puts "=== T182 T2: set_multicycle_path -setup 3 / -hold 2 for the registers of cpu_0/cpu, -setup 2 / -hold 1 from din_r / rdy_r / irq_r / nmi_r / the port into the CPU and from the CPU into smem and its cache (not a false path), cache regs [llength $cch_r]"
} else {
  error "T182 T2: cpu_rdy net not found, the multicycle recipe cannot be scoped"
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
  write_bitstream -force $bdir/c64scpu_mono.bit
  puts "=== bitstream: $bdir/c64scpu_mono.bit [file size $bdir/c64scpu_mono.bit] bytes"
}
