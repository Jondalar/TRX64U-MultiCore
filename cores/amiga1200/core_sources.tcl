# T85 / T97 step 2: layer-2 synthesis of the Amiga 1200 core, out of context. It is the Amiga core's recipe with
# three generics - the machine powers up with AGA (chipset_config bit 4) and 2 MB chip RAM (memory_config[1:0] = 3)
# instead of OCS and 512 KB, and the HDF size registers carry sectors (HDF_SECT, T102 D1) so that its application can
# announce an image of 4 GiB and more - and with a different CPU: TG68K as a 68020 (T97 step 2) instead of fx68k, a 68000.
# Kickstart 40.68, the A1200's, is built for the 68EC020 and does not run on a 68000. The CPU is the only difference in
# the gateware: ENABLE_TG68K makes the shared cpu_wrapper.v build the TG68K and not the fx68k; the wrapper itself is
# cores/amiga1200/rtl/cpu_wrapper.v (the shared one with recorded changes, tools/check_wrapper_fork.sh), and
# rtl/tg68k_020.v ties the kernel to the 68020 whatever the application writes into the CPU register.
# Sourced from host/ (Vivado's working directory); needs $part and $bdir; sets l2_dcp.
if {![info exists common]} { set common ../cores/common }
if {![info exists gvhdl]}  { source gideon_vhdl.tcl }

set mm ../cores/amiga/rtl
set a12 ../cores/amiga1200/rtl
set l2_dcp [file join $bdir layer2_amiga1200_ooc.dcp]
# the shared minimig-aga files except the ones this core keeps its own copy of (Alex, 2026-10-20, T97 5.8): cpu_wrapper.v
# and paula_floppy_fifo.v (its read register in block RAM, not 1,000 LUT of distributed RAM); and trx/amiga_core.v (T97 5.9: no slow RAM
# for the chipset when the fast cards took it)
set a12_own {cpu_wrapper.v paula_floppy_fifo.v minimig.v}
# T97 5.11 (area): the sector buffer of Paula's floppy DMA in block RAM (paula_floppy.v), the sd service's two queues in block RAM (amiga_sdc.v with
# trx_afifo_rr.v); each copy is the shared file plus a recorded .diff (tools/check_wrapper_fork.sh)
lappend a12_own paula_floppy.v
set mm_v [list]
foreach f [glob $mm/src/minimig-aga/*.v] { if {[file tail $f] ni $a12_own} { lappend mm_v $f } }
set trx_v [list]
# amiga_fb.v and amiga_regs.v too, and minimig.v / nanomig.v (T97 5.10: the crop window and the display window are registers of the A1200 only;
# the display window is snooped in minimig.v, nanomig.v carries the port - the A500 keeps its sources and its cache key)
set a12_trx {amiga_core.v amiga_fb.v amiga_regs.v amiga_sdc.v amiga_mem.v amiga_input.v}   ;# amiga_input.v: T141, the A1200's own copy
foreach f [glob $mm/trx/*.v] { if {[file tail $f] ni $a12_trx} { lappend trx_v $f } }
set mm_v [concat $mm_v $a12/nanomig.v $a12/minimig.v $trx_v $a12/amiga_core.v $a12/amiga_fb.v $a12/amiga_regs.v $a12/amiga_input.v $a12/cpu_wrapper.v $a12/paula_floppy_fifo.v $a12/tg68k_020.v $a12/amiga_mem.v]
set mm_v [concat $mm_v $a12/paula_floppy.v $a12/amiga_sdc.v $a12/trx_afifo_rr.v]
# the 68020: TG68K (Tobias Gubener), VHDL, the package first; its entity is TG68KdotC_Kernel_core (rtl/NOTICE)
set a12_vhdl [list $a12/tg68k/TG68K_Pack.vhd $a12/tg68k/TG68K_ALU.vhd $a12/tg68k/TG68KdotC_Kernel.vhd]
set a12_defs {INFER_DPRAM ENABLE_TG68K}
set a12_generics {CHS_INIT 16 MEM_INIT 3 HDF_SECT 1}
# Flat build (cores/amiga1200/mono/build_mono.tcl, T97 5.14): no out-of-context run, the caller reads $mm_v, $a12_vhdl and layer2_amiga.v itself.
if {[info exists ::env(TRX_MONO)] && $::env(TRX_MONO) eq "1"} { return }
create_project -in_memory -part $part
set_property target_language Verilog [current_project]
read_verilog -sv $mm_v
read_vhdl -library work $a12_vhdl
read_verilog -sv ../cores/amiga/layer2_amiga.v
set synth_gen {}
foreach {g v} $a12_generics { lappend synth_gen -generic $g=$v }
set synth_def {}
foreach d $a12_defs { lappend synth_def -verilog_define $d }
synth_design -top layer2_amiga -part $part -mode out_of_context {*}$synth_def {*}$synth_gen
write_checkpoint -force $l2_dcp
report_utilization -file $bdir/utilization_amiga1200_synth.rpt
report_utilization -hierarchical -hierarchical_depth 4 -file $bdir/utilization_amiga1200_hier_synth.rpt
set fh [open $bdir/utilization_amiga1200_synth.rpt]
foreach ln [split [read $fh] "\n"] {
  if {[regexp {^\| (Slice LUTs|LUT as Logic|LUT as Memory|Slice Registers|Block RAM Tile|RAMB36|RAMB18|DSPs)} $ln]} { puts "=== T85 amiga1200 synth $ln" }
}
close $fh
close_project
puts "=== T85: Amiga 1200 module synthesized out of context ($l2_dcp)"
