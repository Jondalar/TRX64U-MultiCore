# Non-project build of c64_basic. Run through tools/vivado.sh or CI.
# TRX_PART picks the part (T53: xc7a100tfgg484-1 for the Ultimate 64 Elite II);
# TRX_BUILD the output directory, so builds for two parts do not overwrite each other.
set part xc7a50tfgg484-1
if {[info exists ::env(TRX_PART)] && $::env(TRX_PART) ne ""} { set part $::env(TRX_PART) }
set bdir build
if {[info exists ::env(TRX_BUILD)] && $::env(TRX_BUILD) ne ""} { set bdir $::env(TRX_BUILD) }
# capability bits 29:28: the FPGA type the app and our boot ROM read (itu.h
# CAPAB_FPGA_TYPE): 2 = XC7A50T, 3 = XC7A100T (flash layout, update, app address)
switch -glob $part {
  xc7a50t*  { set fpga_type 2 }
  xc7a100t* { set fpga_type 3 }
  default   { error "no FPGA type for part $part" }
}
set core ../vendor/icepi
set kawari $core/vicii-kawari/hdl
source ../tools/vivado_lib.tcl
source check/trx_constraints.tcl

# T94 D1: the layer-2 modules more than one core reads (cores/common: the 1541 drive and the IEC bus), the five TRX modules the
# host's top instantiates (they sit in cores/common, in the shared: section of layers.txt) and the cores live outside host/.
# Vivado runs in host/, so every path below is relative to it.
set common ../cores/common
file mkdir $bdir
set ::trx_build $bdir

# --- C64 core and layer 2, out of context and cached: the recipe of the reference core, the one layers.txt names
# (reference:, T115: cores/c64ref, a slim C64U copy built only to route against). Its core_sources.tcl sets core_dcp, core_stub,
# l2_dcp, l2_stub, gvhdl and top_synth. ---
set ref_core ""
set lfh [open layers.txt r]
foreach lline [split [read $lfh] "\n"] {
  if {[regexp {^reference:\s*(\S+)} $lline -> ref_core]} { break }
}
close $lfh
if {$ref_core eq ""} { error "build.tcl: host/layers.txt names no reference core" }
puts "=== reference core: $ref_core (host/layers.txt)"
source ../cores/$ref_core/core_sources.tcl


# T83: the VC-20 core as a second module for the same partition (layer2_vc20.v has the contract's ports).
# Behind TRX_CORE=vc20, default off: cores/vc20/rtl has licences: its NOTICE.
# Not part of a release build. Synthesized out of context like layer 2; implemented into the locked static
# further down (T83 section after the grey box).
set trx_core ""
if {[info exists ::env(TRX_CORE)] && $::env(TRX_CORE) ne ""} { set trx_core $::env(TRX_CORE) }
# T90: TRX_CORE is a comma separated list ("vc20", "c264", "vc20,c264"): each named module is synthesized and
# implemented as its own partial bitstream; the C64 and the VC-20 builds are the same with or without the other.
set trx_cores [split $trx_core ","]
# The layered flow (T87 D8/D9, ci/bitstream_layers.sh) builds the static with the reference core in
# the host stage and every other core in its own stage, against the cached host. So in the host
# stage the list of other cores is ignored: it would synthesize and implement them a second time.
if {[info exists ::env(TRX_STAGE)] && $::env(TRX_STAGE) eq "host" && [llength $trx_cores]} {
  puts "=== host stage: the other cores ($trx_core) are built in their own stage, not here"
  set trx_cores {}
}
if {"c264" in $trx_cores && ![file exists ../cores/c264/rtl/src/ted.v]} {
  puts "=== T90 c264: cores/c264/rtl/src is empty (tools/vendor_c16nano.sh has not been run), the C264 module is skipped"
  set trx_cores [lsearch -all -inline -not -exact $trx_cores c264]
}
if {"vc20" in $trx_cores} {
  source ../cores/vc20/core_sources.tcl
  set l2v_dcp $l2_dcp   ;# the implementation block further down keeps its own name
}

# T85: the Amiga (Minimig) core as the module for the same partition (layer2_amiga.v has the contract's ports).
# Behind TRX_CORE=amiga, default off: cores/amiga/rtl has licences: its NOTICE. Not part of a release
# build. Synthesized out of context like layer 2 and implemented into the locked static further down.
if {"amiga" in $trx_cores} {
  source ../cores/amiga/core_sources.tcl
  set l2a_dcp $l2_dcp   ;# the implementation block further down keeps its own name
}

# T90: the Commodore 264 core (C16, C116, Plus/4): the same socket, TED machine of cores/c264/rtl (licences: its NOTICE).
if {"c264" in $trx_cores} {
  source ../cores/c264/core_sources.tcl
  set l2c_dcp $l2_dcp   ;# the implementation block further down keeps its own name
}

create_project -in_memory -part $part
set_property target_language Verilog [current_project]

# GTP transmitters: 1.5 Gb/s from 150 MHz picks the same PLL dividers
# (N = 20, M = 1, D = 4) as the real 1.481-1.491 Gb/s from 148.06-149.14 MHz.
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

# The DDR2 controller is ours (ddr2_ctrl.v: ddr2_lean.v + cal_seq.v, no IP, no MIG).

# --- board glue ---
# c64 is a black box until read_checkpoint -cell fills it in after synthesis.
set_msg_config -id {Project 1-486} -new_severity INFO
read_verilog -sv hdmi_gtp.v
# T164 G4: the encoder is host/hdmi_enc (ce, runtime frame, PR = 0: six forked files); the three unforked ones come from the vendor tree
read_verilog -sv [list {*}[glob hdmi_enc/*.sv] hdmi_enc/hdmi.v $core/hdmi/audio_sample_packet.sv $core/hdmi/audio_info_frame.sv \
  $core/hdmi/source_product_description_info_frame.sv]
read_verilog $core_stub $l2_stub top.v vic_scaler.v pal_ram.v hdmi_out.v hdmi_timing.v mode_req.v cdce_mode.v tmds_rep.v mode_ctl.v portlock_clk.v c64u_keyboard.v bling_keyboard.v jtag_dbg.v ddr2_port.v ddr2_ctrl.v ddr2_lean.v cal_seq.v i2s_out.v trx_gain.v \
  con_ring.v pktport_host.v app_i2c.v overlay.v usb_phy.v $common/iec_bus.v \
  cart_fill.v $common/trx_io.v $common/trx_sys.v $common/dma_win.v pix_layer.v c64_text.v $common/trx_engine.v \
  $common/drv_dbg.v app_input.v rv_dbg.v
read_verilog bling_io.v esp_ctl.v ui_buttons.v case_leds.v reu_ddr_map.v dfx_decouple.v
# Gideon's RISC-V and the blocks around it (vendor/gideon), our boot ROM package
# for his bootrom_8k, his 1541 drive (drive_unit.vhd, drive_mem.vhd), his
# command interface (the UCI, uci_unit.vhd), and ultimate_sys.vhd that puts
# them together.
read_vhdl -library work $gvhdl
read_xdc ddr2_pins.xdc
read_xdc pins.xdc
read_xdc gtp_pins.xdc
read_xdc usb_pins.xdc

synth_design -top top -part $part -generic FPGA_TYPE=$fpga_type {*}$top_synth
# T79 control (run 715): T79_NO_DFX=1 keeps the same content and fences but no partition, to tell
# the DFX routing containment of layer 2 apart from the content and the fences
if {[info exists ::env(T79_NO_DFX)] && $::env(T79_NO_DFX) eq "1"} {
  puts "=== T79 control: no partition (HD.RECONFIGURABLE off), same fences"
} else {
  set_property HD.RECONFIGURABLE true [get_cells l2]
}
# T79 host first (Alex, 2026-10-11: "then like that and only the host first; when it builds, hold
# the core against it and the wiring step by step"): T79_HOST_ONLY=1 builds layer 1 with an empty
# layer 2 (grey box: the partition's ports driven by constants), so routing failures of the host
# and the edge show without the core.
set t79_host_only [expr {[info exists ::env(T79_HOST_ONLY)] && $::env(T79_HOST_ONLY) eq "1"}]
if {$t79_host_only} {
  # l2 is a black box already (its stub); run 710: update_design -black_box fails on it ([Coretcl 2-1015])
  update_design -cell l2 -buffer_ports
  puts "=== T79 host only: layer 2 is a grey box ([llength [get_cells -hier -filter {NAME =~ l2/*}]] cells)"
} else {
  read_checkpoint -cell l2 $l2_dcp
  # the C64 core into layer 2's black box here: inside the out-of-context run, read_checkpoint -cell
  # fails with [Project 1-9] (runs 696, 697)
  read_checkpoint -cell l2/c64 $core_dcp
}
report_utilization -file $bdir/utilization_synth.rpt
report_utilization -hierarchical -hierarchical_depth 8 -file $bdir/utilization_hier_synth.rpt
# T87 host stage: layer-2 boundary nets stay real nets (tools/vivado_lib.tcl trx_keep_boundary, run 396)
if {[info exists ::env(TRX_STAGE)] && $::env(TRX_STAGE) eq "host" && !$t79_host_only} { trx_keep_boundary; trx_check_boundary "host, before opt_design" }
trx_check_constraints "host DFX, before opt_design"
opt_design
# T79 run 712: layer 1 is slice-bound at 74 % LUT (many small blocks, each with its own clock,
# reset and enable: half-filled slices); merge equivalent control sets
opt_design -control_set_merge
if {[info exists ::env(TRX_STAGE)] && $::env(TRX_STAGE) eq "host" && !$t79_host_only} { trx_check_boundary "host, after opt_design" }
# T29 (spec 12.10): the CPU core and the engine's slow-access state run on clk2x, but what the
# CPU is waiting on (RDY: VIC BA, pause, REU DMA, the cartridge's wait) is a function of clk-domain
# registers that settles over a whole clk cycle. The core takes RDY, and the engine
# completes a slow access, only on clk2x edges that gclk shares (g_edge: at normal speed the core's
# enable, in turbo pn and a stalled CPU), and in turbo mode the core's RDY is tied high (cpu6510.v
# rdy_latched): every path that ends on a clk2x register through cpu_rdy has two clk2x periods.
set rdy_nets [get_nets -quiet -hier -filter {NAME =~ l2/c64/c64/cpu_rdy}]
if {[llength $rdy_nets] == 1} {
  set sid2x_c [get_clocks -of_objects [get_pins mmcm_sys/CLKOUT1]]
  set_multicycle_path 2 -setup -end -through $rdy_nets -to $sid2x_c
  set_multicycle_path 1 -hold  -end -through $rdy_nets -to $sid2x_c
  puts "=== T29: cpu_rdy paths to clk2x timed with two clk2x periods"
} else {
  puts "=== T29: WARNING cpu_rdy net not found ([llength $rdy_nets]), no multicycle path"
}
# The C64's RAM/ROM block RAMs carry three clocks (gclk on port A, clk and clk2x on port B and
# the staging). With 70 block RAMs of 75 in use the placer put some of them into clock region
# X1Y0, which already holds the DDR2, GTP, USB and Ethernet clocks, and failed with "13 clocks in a
# clock region" (run 352, [Place 30-410]): keep them out of that region.
# T79 trial A2 (branch dfx-trial only): Alex's cut (2026-10-11) as a floorplan of today's design.
# Layer 1, the host that survives: JTAG debug, RISC-V (with its ITU, console, debug tap), DDR2 with
# its arbiter and bridge, the ESP32 UART and pins, flash, USB, SD, the HDMI transmitter (GTP), the
# clock chip and mode control, resets. Layer 2: everything else (docs/specs/T79-dfx-trial.md).
# Layer 1 gets clock region X1Y0 (DDR2, GTP, USB clocks) with all its block RAM, slices X20-35 of
# X0Y2 (the GTP reference clock's BUFH loads, run 678) and slices X56-65 rows 50-99; layer 2 the
# rest: about 5,350 slices, 55 BRAM tiles, 100 DSP. A leak out of the region fails the build.
# Trial A3 (run 687): X1Y0 may hold no layer-2 block RAM: the C64's clocks would join the 12 host
# clocks there ([Place 30-410], 14 of 12). Layer 2 needs 59.5 tiles on the slim stand, 4.5 more than
# it has outside X1Y0, so the UDP streams, Ethernet and the scaler (7 tiles, about 1,130 LUT) move
# to layer 1 for this trial (streams: Alex leans to layer 1; Ethernet and scaler: trial choice).
# Trial B: layer 2 is one cell, l2 (the reference core's layer 2, layer2_c64ref.v), with the 1541 drive (drive_l2.vhd), scaler and overlay.
set l2_cells [get_cells l2]
puts "=== T79 trial A7: [llength $l2_cells] layer-2 cells: $l2_cells"
create_pblock pb_l2
add_cells_to_pblock pb_l2 $l2_cells
# T79 floorplan B (run 719: the conflicts sit in the L's waist, x 32-47 y 0-49, the only way between
# the core's X0 part and its X1Y0 arm; without the partition the nets detour through the host):
# a compact core (run 720: X0-49 gave it only 15,600 LUT, 90 %; now X0-55, two columns clear of
# BRAM column 2 at 57|58), slices X0-55 of the two lower rows (BRAM columns 0 and 1) plus X0-9 of X0Y2 for
# column 0's upper tiles; the host gets the right edge (USB, DDR2 pins) and the top row.
set t79_floor A
if {[info exists ::env(T79_FLOOR)] && $::env(T79_FLOOR) ne ""} { set t79_floor $::env(T79_FLOOR) }
puts "=== T79 floorplan $t79_floor"
if {$t79_floor eq "B"} {
  # T133 (Alex 2026-10-26: "Mach den L1 teil grösser"; rebalanced 2026-10-27 so that every core fits again): pb_l1 is X56-X65 (rows 0-49) and X50-X65
  # (rows 50-99) in the lower band (the block RAM column X1 sits between X47 and X48; pb_l2 needs slices past it to own it, edge X49|X50) - a staircase (T164 WP19: now straight at X49|X50 in rows 0-99; the whole DSP column X1, between X53 and X54, is pb_l1's). Block RAM: pb_l2 has X0 (30) and X1Y0:Y19 (20) = 50 tiles, pb_l1 X2Y0:Y19 and X1Y25:Y29 = 25;
  # pb_cpu reaches RAMB36_X2Y15:Y19 (the rows just below it) and X1Y25:Y29.
  # T164 WP19 (cut A, Alex 2026-10-31): the lower-row edge moves from X55 to X50 (pb_l2 X0-49 in rows 0-99, two columns clear of BRAM column 1 at 47|48), so pb_l1 owns
  # SLICE_X50Y0:X55Y49 (+1,200 LUT: 14,200 -> 15,400, pb_l2 16,800 -> 15,600) and DSP column 1 (53|54) whole, DSP48_X1Y0:Y59; pb_l2 keeps DSP48_X0Y0:Y39 (40). BRAM unchanged.
  # T164 run 1117 (Alex 2026-11-01: the host keeps every feature, only the core region shrinks): host placement failed at 12,802 LUT in 15,400
  # (83.1 %, short 10-36 slices). The left edge X0-5 of rows 0-99 goes to pb_l1 (+2,400 LUT: 15,400 -> 17,800, pb_l2 15,600 -> 13,200); X6-7 stay
  # with pb_l2, two columns clear of BRAM column 0 (7|8). No block RAM or DSP moves. That strip lies beside banks 14 (flash, SD, ESP32) and 16 (RMII).
  # T164 run 1119 (2026-11-01): the X0-5 island of the host gave a deeply negative WNS (host nets crossed the core region). Instead the
  # top-left strip X0-9 of rows 100-149 goes to the host: pb_l2 is one rectangle, X0-49 of rows 0-99, the host one connected L
  # (17,400 LUT); pb_l2 13,600 LUT, block RAM 40 tiles (RAMB36_X0Y20:Y29 move with the strip), DSP 40. Cores that no longer fit become large (Alex).
  resize_pblock pb_l2 -add {SLICE_X0Y0:SLICE_X49Y99}
  resize_pblock pb_l2 -add {RAMB36_X0Y0:RAMB36_X0Y19 RAMB36_X1Y0:RAMB36_X1Y19}
  resize_pblock pb_l2 -add {RAMB18_X0Y0:RAMB18_X0Y39 RAMB18_X1Y0:RAMB18_X1Y39}
  resize_pblock pb_l2 -add {DSP48_X0Y0:DSP48_X0Y39}
} else {
resize_pblock pb_l2 -add {SLICE_X0Y0:SLICE_X35Y99 SLICE_X0Y100:SLICE_X19Y149 SLICE_X36Y0:SLICE_X65Y49}
resize_pblock pb_l2 -add {RAMB36_X0Y0:RAMB36_X0Y29 RAMB36_X1Y0:RAMB36_X1Y9 RAMB36_X2Y0:RAMB36_X2Y9}
resize_pblock pb_l2 -add {RAMB18_X0Y0:RAMB18_X0Y59 RAMB18_X1Y0:RAMB18_X1Y19 RAMB18_X2Y0:RAMB18_X2Y19}
resize_pblock pb_l2 -add {DSP48_X0Y0:DSP48_X0Y59 DSP48_X1Y0:DSP48_X1Y19}
}
# I/O and clock primitives stay static in a DFX design (7 series): the pin cells (buffers, IOB
# registers, I/O DDR/SERDES/delays) and clock buffers/PLLs inside layer-2 blocks leave the pblock.
# Run 682 failed placement with them inside ([Place 30-99]).
set l2_io [get_cells -quiet -hier -filter {PBLOCK == pb_l2 && IS_PRIMITIVE && (IOB == TRUE || REF_NAME =~ IBUF* || REF_NAME =~ OBUF* || REF_NAME =~ IOBUF* || REF_NAME =~ ODDR* || REF_NAME =~ IDDR* || REF_NAME =~ ISERDES* || REF_NAME =~ OSERDES* || REF_NAME =~ IDELAY* || REF_NAME =~ ODELAY* || REF_NAME =~ BUFG* || REF_NAME =~ BUFH* || REF_NAME =~ BUFR* || REF_NAME =~ BUFIO* || REF_NAME =~ MMCM* || REF_NAME =~ PLLE* || REF_NAME =~ STARTUP* || REF_NAME =~ ICAP*)}]
puts "=== T79 trial A2: [llength $l2_io] I/O or clock primitives in layer-2 blocks, kept static"
foreach c [lrange $l2_io 0 39] { puts "=== T79 static: $c ([get_property REF_NAME $c])" }
if {[llength $l2_io]} { remove_cells_from_pblock pb_l2 $l2_io }
set_property SNAPPING_MODE ON [get_pblocks pb_l2]
# After a partial reload the core's flip-flops must start at their INIT values (7 series: only with
# RESET_AFTER_RECONFIG, a GSR over the region; pb_l2 spans whole clock-region heights). Without it,
# registers without a reset start at random: PoC 4, the DMA window's toggle pair (dma_win.v dreq_s,
# dma_done_tgl) came up misaligned and every REST readmem after the swap returned zeros.
set_property RESET_AFTER_RECONFIG true [get_pblocks pb_l2]
# T79.1 P1/P2 floorplan A13: by pins (prjxray package: DDR2 bank 35 = X1Y1, USB bank 34 = X1Y0,
# RMII bank 16 = X0Y2, flash/SD/ESP32 bank 14 = X0Y0, HDMI GTP = X1Y2) and whole block RAM columns
# (frames, run 702), boundaries two slice columns clear of BRAM/DSP columns (run 704). Layer 1 (host
# with peripherals and the video chain): X1Y1 and X1Y2 whole, X0Y2 slices X14-35, block RAM of X1Y1
# and X1Y2 (25 tiles for about 22.5). Layer 2 (core): X0Y0, X0Y1, X1Y0 whole, X0Y2 slices X0-13,
# block RAM of X0 and X1Y0 (50 tiles for about 47).
# H3 step 1 (run 711: host + grey box routes, the full core does not; pb_l2 was 88.6 % LUT, pb_l1
# 74.3 %): the X0Y2 boundary moves from slice X14 to X20, about 1,200 LUT from layer 1 to layer 2.
# Layer 1 keeps X20-35 of X0Y2 (the GTP reference clock's BUFH loads, run 678).
# trial A5: run 688 placed with 906 layer-2 cells outside and 1,702 layer-1 cells inside; say hard
# Trial A6: layer 1 fenced too, the complement of pb_l2, so Vivado reports what each side needs
# (run 689: UTLZ-3, layer 2 needs 5,406 slices of 5,350, and the placer spreads over the edge).
set l1_all [get_cells *]
set l1_cells {}
foreach c $l1_all pb [get_property PBLOCK $l1_all] prim [get_property IS_PRIMITIVE $l1_all] ref [get_property REF_NAME $l1_all] {
  if {$pb eq "pb_l2" || [get_property NAME $c] eq "l2"} { continue }   ;# run 699: the partition cell itself does not report pb_l2
  if {$prim && [regexp {^(IBUF|OBUF|IOBUF|ODDR|IDDR|ISERDES|OSERDES|IDELAY|ODELAY|BUFG|BUFH|BUFR|BUFIO|MMCM|PLLE|STARTUP|ICAP|GTP|IBUFDS|VCC|GND)} $ref]} { continue }
  lappend l1_cells $c
}
puts "=== T79 trial A6: [llength $l1_all] top-level cells, [llength $l1_cells] to pb_l1"
create_pblock pb_l1
add_cells_to_pblock pb_l1 $l1_cells
if {$t79_floor eq "B"} {
  resize_pblock pb_l1 -add {SLICE_X50Y0:SLICE_X65Y99 SLICE_X0Y100:SLICE_X65Y149}
  resize_pblock pb_l1 -add {RAMB36_X2Y0:RAMB36_X2Y19 RAMB36_X1Y25:RAMB36_X1Y29 RAMB36_X0Y20:RAMB36_X0Y29 RAMB18_X2Y0:RAMB18_X2Y39 RAMB18_X1Y50:RAMB18_X1Y59 RAMB18_X0Y40:RAMB18_X0Y59}
  resize_pblock pb_l1 -add {DSP48_X1Y0:DSP48_X1Y59 DSP48_X0Y40:DSP48_X0Y59}
} else {
resize_pblock pb_l1 -add {SLICE_X36Y50:SLICE_X65Y149 SLICE_X20Y100:SLICE_X35Y149}
resize_pblock pb_l1 -add {RAMB36_X1Y10:RAMB36_X1Y19 RAMB36_X2Y10:RAMB36_X2Y19 RAMB36_X1Y25:RAMB36_X1Y29 RAMB18_X1Y20:RAMB18_X1Y39 RAMB18_X2Y20:RAMB18_X2Y39 RAMB18_X1Y50:RAMB18_X1Y59}
resize_pblock pb_l1 -add {DSP48_X1Y20:DSP48_X1Y59}
}
set_property EXCLUDE_PLACEMENT true [get_pblocks pb_l1]
# The RISC-V (ult/i_cpu: 1,801 LUT, 2 RAMB36, 4 RAMB18, 4 DSP) in its own corner of layer 1, at the
# RAMB36_X1 and DSP48_X1 columns of the upper band: its instruction cache loop (tag RAM -> compare ->
# next fetch address -> cache RAM) is layer 1's critical path in every build (runs 728-736, +0.14 to
# -0.007 ns); held together it no longer depends on where the placer happens to put it.
if {$t79_floor eq "B"} {
  create_pblock pb_cpu
  add_cells_to_pblock pb_cpu [get_cells ult/i_cpu]
  resize_pblock pb_cpu -add {SLICE_X34Y100:SLICE_X59Y149 RAMB36_X2Y15:RAMB36_X2Y19 RAMB36_X1Y25:RAMB36_X1Y29 RAMB18_X2Y30:RAMB18_X2Y39 RAMB18_X1Y50:RAMB18_X1Y59 DSP48_X1Y40:DSP48_X1Y59}   ;# T133: more slices for the data cache
  set_property PARENT pb_l1 [get_pblocks pb_cpu]
  puts "=== T79 pb_cpu: [llength [get_cells -hier -filter {NAME =~ ult/i_cpu/* && IS_PRIMITIVE}]] cells"
}
report_utilization -pblocks [get_pblocks pb_l2] -file $bdir/utilization_pb_l2_opt.rpt
report_utilization -pblocks [get_pblocks pb_l1] -file $bdir/utilization_pb_l1_opt.rpt
# need against room, in the log (a failed run uploads no reports)
foreach pbn {pb_l2 pb_l1} {
  set fh [open $bdir/utilization_${pbn}_opt.rpt]
  foreach ln [split [read $fh] "\n"] {
    if {[regexp {^\| (Slice LUTs|Slice  |LUT as Logic|Block RAM Tile|DSPs)} $ln]} { puts "=== T79 $pbn $ln" }
  }
  close $fh
}
# The app CPU's register file -> multiplier path (rvlite, 100 MHz) failed on
# placement luck alone (run 289); patch 0006 (docs/specs/OPT-rvmul.md) pipelines
# the multiplier, the directives stay for the rest of the design.
write_checkpoint -force $bdir/opt.dcp

# One implementation pass: place, phys_opt, route, phys_opt. Returns the worst setup slack.
proc impl_pass {place_dir} {
  # T87 run 399: the global placer can fail on the cascaded USB buffer RAM (6 RAMB36 that must sit in one
  # clock-region column, [Place 30-859] "120 %" inside pb_l1's 25 tiles) in one netlist and not in the
  # next; it is placement luck, so a failed placement is tried again with other directives before the
  # build gives up. A pass that places at the first try is unchanged.
  if {[catch {place_design -directive $place_dir} perr]} {
    puts "=== placement with $place_dir failed: [string range $perr 0 200]"
    set placed 0
    foreach alt {Explore AltSpreadLogic_high ExtraNetDelay_high SSI_SpreadLogic_high} {
      if {$alt eq $place_dir} continue
      if {![catch {place_design -directive $alt} perr2]} { puts "=== placement retry with $alt succeeded"; set placed 1; break }
      puts "=== placement retry with $alt failed: [string range $perr2 0 200]"
    }
    if {!$placed} { error $perr }
  }
  phys_opt_design -directive AggressiveExplore
  # T79: where routing will be tight, before the router runs (runs 705-707 hung in rip-up and reroute).
  # Run 708: the placer's table was empty (no congested window); the router's own estimate
  # (Route 35-449, printed by ci/bitstream.sh) is the next look.
  if {![catch {report_design_analysis -congestion -file $::bdir/congestion_placed.rpt}]} {
    set fh [open $::bdir/congestion_placed.rpt]
    set n 0
    foreach ln [split [read $fh] "\n"] {
      if {[regexp {^\| (North|South|East|West)|Window|INT_|CLE|Level|Congestion} $ln] && $n < 40} { puts "=== T79 congestion $ln"; incr n }
    }
    close $fh
  }
  if {[info exists ::env(T79_PLACE_ONLY)] && $::env(T79_PLACE_ONLY) eq "1"} {
    foreach pb {pb_l1 pb_l2} { report_utilization -pblocks [get_pblocks $pb] -file $::bdir/utilization_${pb}_placed.rpt }
    error "T79_PLACE_ONLY: stopped after placement (congestion report above)"
  }
  # T79 run 713: routing oscillates (each iteration from 12-16 k overlaps down to about 1,600):
  # T79_ROUTE overrides the router directive (AlternateCLBRouting for dense CLB areas)
  set rdir Explore
  if {[info exists ::env(T79_ROUTE)] && $::env(T79_ROUTE) ne ""} { set rdir $::env(T79_ROUTE) }
  if {[catch {route_design -directive $rdir} rerr]} {
    # T79 run 718: routing gave up with conflicts left; where are they? (driver site of each
    # conflicted net, counted per clock region and per 8 x 25 slice window)
    set cn [get_nets -quiet -hier -filter {ROUTE_STATUS == CONFLICTS}]
    puts "=== T79 conflicts: [llength $cn] nets"
    set reg [dict create]; set win [dict create]; set nm [dict create]
    foreach n $cn {
      set d [get_cells -quiet -of_objects [get_pins -quiet -of_objects $n -filter {DIRECTION == OUT && IS_LEAF}]]
      if {[llength $d] == 0} { continue }
      set site [get_property LOC [lindex $d 0]]
      if {$site eq ""} { continue }
      set cr [get_property CLOCK_REGION [get_sites $site]]
      dict incr reg $cr
      if {[regexp {X(\d+)Y(\d+)} $site -> sx sy]} { dict incr win "[expr {$sx / 8 * 8}]-[expr {$sy / 25 * 25}] ([lindex [split $site _] 0])" }
      dict incr nm [join [lrange [split $n /] 0 2] /]
    }
    foreach {k v} [lsort -stride 2 -index 1 -integer -decreasing $reg] { puts "=== T79 conflicts region $k: $v" }
    set i 0
    foreach {k v} [lsort -stride 2 -index 1 -integer -decreasing $win] { if {[incr i] > 25} break; puts "=== T79 conflicts window x-y $k: $v" }
    set i 0
    foreach {k v} [lsort -stride 2 -index 1 -integer -decreasing $nm] { if {[incr i] > 25} break; puts "=== T79 conflicts module $k: $v" }
    error $rerr
  }
  phys_opt_design -directive AggressiveExplore
  set p [get_timing_paths -setup -max_paths 1]
  if {[llength $p] == 0} { return 1000.0 }
  # a few picoseconds short (run 735: -0.007 ns on the RISC-V cache): more post-route
  # optimisation, minutes, instead of a second placement
  foreach d {AlternateReplication AggressiveFanoutOpt} {
    if {[get_property SLACK $p] >= 0} break
    phys_opt_design -directive $d
    set p [get_timing_paths -setup -max_paths 1]
    puts "=== post-route phys_opt $d: WNS [get_property SLACK $p] ns"
  }
  return [get_property SLACK $p]
}

# The chip is ~99.9 % slices, so placement decides timing. A failed first pass gets a
# second one with directives that spread logic / pessimise net delay, from the same
# post-opt checkpoint; the better of the two is kept (docs/architecture-reset-2026-10-05.md).
# T79: T79_PLACE overrides the placer directive (AltSpreadLogic_high spreads the core's logic)
set pdir ExtraTimingOpt
if {[info exists ::env(T79_PLACE)] && $::env(T79_PLACE) ne ""} { set pdir $::env(T79_PLACE) }
set wns [impl_pass $pdir]
puts "=== impl pass 1 ($pdir): WNS $wns ns"
# T79 trial branch: the second placement only when the first misses (Alex 2026-10-11: 60 min a run
# is too long; green runs stay one pass). Runs 735/736: the RISC-V instruction cache loop (tag RAM ->
# compare -> next fetch address) is layer 1's critical path in every build (+0.138, +0.103, -0.007 ns).
if {$wns < 0} {
  write_checkpoint -force $bdir/routed_pass1.dcp
  set best $wns
  set best_dcp $bdir/routed_pass1.dcp
  foreach dir {AltSpreadLogic_high ExtraNetDelay_high} {
    open_checkpoint $bdir/opt.dcp
    set w [impl_pass $dir]
    puts "=== impl pass ($dir): WNS $w ns"
    if {$w > $best} {
      set best $w
      write_checkpoint -force $bdir/routed_alt.dcp
      set best_dcp $bdir/routed_alt.dcp
    }
    if {$w >= 0} { break }
  }
  open_checkpoint $best_dcp
  puts "=== kept: $best_dcp (WNS $best ns)"
}
write_checkpoint -force $bdir/routed.dcp
trx_axss
write_bitstream -force $bdir/c64_basic.bit
foreach f [glob -nocomplain $bdir/c64_basic*partial*.bit] { puts "=== T79 partial bitstream: [file tail $f] [file size $f] bytes" }
puts "=== bitstream: $bdir/c64_basic.bit"
report_utilization -file $bdir/utilization.rpt
report_utilization -pblocks [get_pblocks pb_l2] -file $bdir/utilization_pb_l2.rpt
# T79 trial A: is the fence tight? Placed leaf cells of layer 2 outside the region, and of layer 1 inside.
set rsites [get_sites -of_objects [get_pblocks pb_l2]]
set rset [dict create]
foreach st $rsites { dict set rset $st 1 }
set l2_out {}; set l1_in {}; set n_l2 0; set n_l1 0
set fcells [get_cells -hier -filter {IS_PRIMITIVE && LOC != ""}]
foreach c $fcells site [get_property LOC $fcells] pb [get_property PBLOCK $fcells] {
  if {![regexp {^(SLICE|RAMB|DSP48)} $site]} { continue }
  set in [dict exists $rset $site]
  if {$pb eq "pb_l2"} {
    incr n_l2
    if {!$in} { lappend l2_out "$c@$site" }
  } else {
    incr n_l1
    if {$in} { lappend l1_in "$c@$site" }
  }
}
puts "=== T79 fence: layer-2 cells $n_l2, outside the region [llength $l2_out]; layer-1 cells $n_l1, inside the region [llength $l1_in]"
foreach x [lrange $l2_out 0 19] { puts "=== T79 fence: L2 outside $x" }
foreach x [lrange $l1_in 0 19] { puts "=== T79 fence: L1 inside $x" }
if {[llength $l2_out] + [llength $l1_in] > 0} {
  # the bitstream does not count: the split as drawn does not fit (CI then reports no bitstream)
  file delete -force $bdir/c64_basic.bit
  # why: what Vivado said about the pblock (a failed run uploads no log)
  if {![catch {open $bdir/vivado.log} lh]} {
    set seen [dict create]
    foreach ln [split [read $lh] "\n"] {
      if {[regexp {UTLZ|[Pp]block|IS_SOFT|EXCLUDE_PLACEMENT|Place 30-|Constraints 18-} $ln] && ![dict exists $seen $ln] && [dict size $seen] < 40} {
        dict set seen $ln 1; puts "=== T79 log: [string range $ln 0 300]"
      }
    }
    close $lh
  }
  error "T79 fence leaked: [llength $l2_out] layer-2 cells outside, [llength $l1_in] layer-1 cells inside"
}
report_utilization -hierarchical -hierarchical_depth 8 -file $bdir/utilization_hier.rpt
report_timing_summary -max_paths 5 -file $bdir/timing.rpt
# Analysis for the next change: where it is tight and what Vivado would do about it.
if {[catch {report_design_analysis -congestion -complexity -timing -max_paths 10 -file $bdir/design_analysis.rpt} e]} {
  puts "report_design_analysis: $e"
}
# Clock-domain crossings (check only, not a gate yet): summary plus every crossing with its safety class.
if {[catch {report_cdc -details -file $bdir/cdc.rpt} e]} {
  puts "report_cdc: $e"
}
if {[catch {report_qor_suggestions -max_paths 20 -file $bdir/qor_suggestions.rpt} e]} {
  puts "report_qor_suggestions: $e"
}
# every endpoint that fails, worst first, one path each
report_timing -delay_type max -max_paths 60 -nworst 1 -slack_lesser_than 0 -sort_by slack -file $bdir/timing_neg.rpt
# the turbo CPU's block RAM port: address, write enable, data pins and the data out (T29 §12.3), whatever their slack
report_timing -delay_type max -max_paths 4 -nworst 1 -to [get_pins -quiet -hier -filter {NAME =~ l2/c64/mem/*/ADDRBWRADDR*}] -file $bdir/timing_bram_addr.rpt
report_timing -delay_type max -max_paths 4 -nworst 1 -to [get_pins -quiet -hier -filter {NAME =~ l2/c64/mem/*/WEBWE*}] -file $bdir/timing_bram_we.rpt
report_timing -delay_type max -max_paths 4 -nworst 1 -to [get_pins -quiet -hier -filter {NAME =~ l2/c64/mem/*/DIBDI*}] -file $bdir/timing_bram_di.rpt
report_timing -delay_type max -max_paths 4 -nworst 1 -from [get_pins -quiet -hier -filter {NAME =~ l2/c64/mem/*/CLKBWRCLK}] -file $bdir/timing_bram_do.rpt
report_timing -delay_type max -max_paths 20 -nworst 1 -sort_by slack -to [get_clocks -of_objects [get_pins mmcm_sys/CLKOUT1]] -file $bdir/timing_clk2x.rpt
# T87 D8 host stage (TRX_STAGE=host, ci/bitstream.sh host): routed.dcp (written above) is the routed
# full design WITH the reference core, so the core's pins and clock loads shaped the static. It is the
# cached host checkpoint: core.tcl opens it and carves layer 2 out exactly as the working second
# configuration of t83-vc20 does (open_checkpoint routed.dcp, update_design -cell l2 -black_box,
# lock_design -level routing, read_checkpoint -cell l2). Run 394: a locked, black-boxed static written
# to a checkpoint first and the grey box placed from it fail with [DRC HDPR-72] on ui_clk (a boundary net
# with dedicated routing may not become non-dedicated), so that variant is not used for the cache.
# host.bit (static + empty layer 2, T84's "host + null core") is tried after that and is optional: the
# same grey box that fails HDPR-72 in T79 trial B. Without it the host id is the SHA-256 of routed.dcp.
set trx_stage ""
if {[info exists ::env(TRX_STAGE)]} { set trx_stage $::env(TRX_STAGE) }
if {$trx_stage eq "host"} {
  if {[catch {
    update_design -cell l2 -black_box
    lock_design -level routing
    update_design -cell l2 -buffer_ports
    place_design
    route_design
    trx_axss
    write_bitstream -force $bdir/host.bit
    puts "=== T87 host: host.bit [file size $bdir/host.bit] bytes"
  } e]} {
    puts "=== T87 host: no host.bit (grey box failed: $e)"
  }
}
# T79 trial B: the grey-box variant, layer 2 empty with its ports buffered, static part locked
# as routed above: its partial bitstream is what an "empty" layer 2 loads.
if {[info exists ::env(T79_GREYBOX)] && !$t79_host_only && $trx_stage ne "host" && [catch {
  update_design -cell l2 -black_box
  lock_design -level routing
  update_design -cell l2 -buffer_ports
  place_design
  route_design
  write_bitstream -force -cell l2 $bdir/c64_basic_l2_greybox.bit
  puts "=== T79 grey box: c64_basic_l2_greybox.bit [file size $bdir/c64_basic_l2_greybox.bit] bytes"
} e]} {
  puts "=== T79 grey box failed: $e"
}
# T83: the second configuration. The routed C64 design (routed.dcp) is the static part: l2 goes to a
# black box, the routing of the static part is locked, the VC-20 module (layer2_vc20_ooc.dcp) is put
# into l2, placed and routed inside pb_l2, and its partial bitstream is written. Both partials load
# into the same host bitstream (T79.1 PoC 4: decouple, partial_load, release).
if {"vc20" in $trx_cores && !$t79_host_only} {
  if {[catch {
    catch {close_design}
    open_checkpoint $bdir/routed.dcp
    update_design -cell l2 -black_box
    lock_design -level routing
    read_checkpoint -cell l2 $l2v_dcp
    opt_design
    place_design
    phys_opt_design
    route_design
    phys_opt_design
    write_checkpoint -force $bdir/routed_vc20.dcp
    report_utilization -pblocks [get_pblocks pb_l2] -file $bdir/utilization_pb_l2_vc20.rpt
    report_utilization -hierarchical -hierarchical_depth 3 -file $bdir/utilization_vc20_hier.rpt
    report_timing_summary -max_paths 5 -file $bdir/timing_vc20.rpt
    report_route_status -file $bdir/route_status_vc20.rpt
    set fh [open $bdir/utilization_pb_l2_vc20.rpt]
    foreach ln [split [read $fh] "\n"] {
      if {[regexp {^\| (Slice LUTs|Slice  |LUT as Logic|LUT as Memory|Slice Registers|Block RAM Tile|RAMB36|RAMB18|DSPs)} $ln]} { puts "=== T83 vc20 pb_l2 $ln" }
    }
    close $fh
    set p [get_timing_paths -setup -max_paths 1]
    set vc_wns [expr {[llength $p] == 0 ? 1000.0 : [get_property SLACK $p]}]
    puts "=== T83 vc20 WNS $vc_wns ns"
    write_bitstream -force -cell l2 $bdir/c64_basic_pb_l2_vc20_partial.bit
    puts "=== T83 VC-20 partial bitstream: c64_basic_pb_l2_vc20_partial.bit [file size $bdir/c64_basic_pb_l2_vc20_partial.bit] bytes"
  } e]} {
    puts "=== T83 VC-20 configuration failed: $e"
  }
}


if {"amiga" in $trx_cores && !$t79_host_only} {
  if {[catch {
    catch {close_design}
    open_checkpoint $bdir/routed.dcp
    update_design -cell l2 -black_box
    lock_design -level routing
    read_checkpoint -cell l2 $l2a_dcp
    opt_design
    place_design
    phys_opt_design
    route_design
    phys_opt_design
    write_checkpoint -force $bdir/routed_amiga.dcp
    report_utilization -pblocks [get_pblocks pb_l2] -file $bdir/utilization_pb_l2_amiga.rpt
    report_utilization -hierarchical -hierarchical_depth 3 -file $bdir/utilization_amiga_hier.rpt
    report_timing_summary -max_paths 5 -file $bdir/timing_amiga.rpt
    report_route_status -file $bdir/route_status_amiga.rpt
    set fh [open $bdir/utilization_pb_l2_amiga.rpt]
    foreach ln [split [read $fh] "\n"] {
      if {[regexp {^\| (Slice LUTs|Slice  |LUT as Logic|LUT as Memory|Slice Registers|Block RAM Tile|RAMB36|RAMB18|DSPs)} $ln]} { puts "=== T85 amiga pb_l2 $ln" }
    }
    close $fh
    set p [get_timing_paths -setup -max_paths 1]
    set am_wns [expr {[llength $p] == 0 ? 1000.0 : [get_property SLACK $p]}]
    puts "=== T85 amiga WNS $am_wns ns"
    write_bitstream -force -cell l2 $bdir/c64_basic_pb_l2_amiga_partial.bit
    puts "=== T85 Amiga partial bitstream: c64_basic_pb_l2_amiga_partial.bit [file size $bdir/c64_basic_pb_l2_amiga_partial.bit] bytes"
  } e]} {
    puts "=== T85 Amiga configuration failed: $e"
  }
}

# T90: the third configuration, the C264 module into the same locked static part (as the VC-20's above).
if {"c264" in $trx_cores && !$t79_host_only} {
  if {[catch {
    catch {close_design}
    open_checkpoint $bdir/routed.dcp
    update_design -cell l2 -black_box
    lock_design -level routing
    read_checkpoint -cell l2 $l2c_dcp
    opt_design
    place_design
    phys_opt_design
    route_design
    phys_opt_design
    write_checkpoint -force $bdir/routed_c264.dcp
    report_utilization -pblocks [get_pblocks pb_l2] -file $bdir/utilization_pb_l2_c264.rpt
    report_utilization -hierarchical -hierarchical_depth 3 -file $bdir/utilization_c264_hier.rpt
    report_timing_summary -max_paths 5 -file $bdir/timing_c264.rpt
    report_route_status -file $bdir/route_status_c264.rpt
    set fh [open $bdir/utilization_pb_l2_c264.rpt]
    foreach ln [split [read $fh] "\n"] {
      if {[regexp {^\| (Slice LUTs|Slice  |LUT as Logic|LUT as Memory|Slice Registers|Block RAM Tile|RAMB36|RAMB18|DSPs)} $ln]} { puts "=== T90 c264 pb_l2 $ln" }
    }
    close $fh
    set p [get_timing_paths -setup -max_paths 1]
    set c2_wns [expr {[llength $p] == 0 ? 1000.0 : [get_property SLACK $p]}]
    puts "=== T90 c264 WNS $c2_wns ns"
    write_bitstream -force -cell l2 $bdir/c64_basic_pb_l2_c264_partial.bit
    puts "=== T90 C264 partial bitstream: c64_basic_pb_l2_c264_partial.bit [file size $bdir/c64_basic_pb_l2_c264_partial.bit] bytes"
  } e]} {
    puts "=== T90 C264 configuration failed: $e"
  }
}

puts "=== done"
