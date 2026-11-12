# T143: the Neo Geo core's source lists for Vivado, read by mono/build_mono.tcl and mono/synth_ooc.tcl. The paths are relative to host/ (Vivado's
# working directory, as in every core's recipe). Sets: neo_v (Verilog), neo_sv (SystemVerilog), neo_vhdl (VHDL, library work), neo_mem (files fx68k reads
# with $readmemb into the working directory), neo_inc (include directories).
set neo ../cores/neogeo
set nr  $neo/rtl
set neo_v   [list {*}[lsort [glob $nr/cells/*.v]] {*}[lsort [glob $nr/io/*.v]] {*}[lsort [glob $nr/video/*.v]] \
  $nr/cpu/cpu_68k.v $nr/cpu/cpu_z80.v \
  {*}[lsort [glob $nr/jt12/hdl/*.v]] {*}[lsort [glob $nr/jt12/hdl/adpcm/*.v]] {*}[lsort [glob $nr/jt12/hdl/mixer/*.v]] {*}[lsort [glob $nr/jt49/hdl/*.v]]]
# pcm.v (the CD's PCM chip, not instantiated) stays out
set neo_v [lsearch -all -inline -not -exact $neo_v $nr/io/pcm.v]
set neo_sv  [list $nr/io/neo_sma.sv $nr/cpu/FX68K/fx68k.sv $nr/cpu/FX68K/fx68kAlu.sv $nr/cpu/FX68K/uaddrPla.sv]
set neo_vhdl [list $nr/cpu/T80/T80_Pack.vhd $nr/cpu/T80/T80_ALU.vhd $nr/cpu/T80/T80_MCode.vhd $nr/cpu/T80/T80_Reg.vhd $nr/cpu/T80/T80.vhd $nr/cpu/T80/T80pa.vhd]
set neo_mem [list $nr/cpu/FX68K/microrom.mem $nr/cpu/FX68K/nanorom.mem]
# our glue (the memory modules, the machine's top, the service and the register block)
set neo_trx [lsort [glob $neo/trx/*.v]]

# files the pristine upstream copy cannot give Vivado as they are: cores/neogeo/fix/<name> (a copy with the change marked "TRX" and <name>.diff against rtl/)
# replaces the file of that name in the lists above. rtl/ itself stays byte for byte the upstream (tools/check_neogeo_tree.py).
proc neo_fixed {lst} {
  global neo
  set out {}
  foreach f $lst {
    set fx $neo/fix/[file tail $f]
    if {[file exists $fx]} { lappend out $fx } else { lappend out $f }
  }
  return $out
}
set neo_v  [neo_fixed $neo_v]
set neo_sv [neo_fixed $neo_sv]
