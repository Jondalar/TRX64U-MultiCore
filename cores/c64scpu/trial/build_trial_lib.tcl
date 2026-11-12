# T182 step 1 area trial: the procs the driver (build_trial.tcl) and the out-of-context run (ooc.tcl) share. Paths are relative to host/ (Vivado's working directory).
# The 65C816 (cores/snes/rtl/65C816, MiSTer, GPL) is read into the library s816: its entity ALU would meet the 6502n's alu in work.

# TRX_MCODE=block: a copy of MCode.vhd in which the microcode ROM can be a block RAM (the SNES build gets this from its GHDL-generated Verilog, cores/snes/gen/snes.v:7816,
# `rom_style = "block"`): the asynchronous reset of the output register MI goes (the CPU's own STATE = 0 path loads the first row anyway) and the attribute is set.
proc scpu_mcode_block {src dst} {
  set fh [open $src]; set t [read $fh]; close $fh
  set n [regsub {process\(CLK, RST_N\)(\s+variable STATE2[^\n]*\s+begin\s+)if RST_N = '0' then\s+MI <= \([^\n]*\);\s+elsif rising_edge\(CLK\) then} $t {process(CLK)\1if rising_edge(CLK) then} t]
  if {$n != 1} { error "MCode.vhd: reset pattern not found" }
  set m [regsub {signal ALUFlags\s*: ALUCtrl_r;} $t "signal ALUFlags : ALUCtrl_r;\n\tattribute rom_style : string;\n\tattribute rom_style of M_TAB : constant is \"block\";" t]
  if {$m != 1} { error "MCode.vhd: attribute anchor not found" }
  set fo [open $dst w]; puts -nonewline $fo $t; close $fo
}
proc scpu_read {} {
  set s ../cores/snes/rtl/65C816
  set mc $s/MCode.vhd
  if {[info exists ::env(TRX_MCODE)] && $::env(TRX_MCODE) eq "block"} {
    set mc $::env(TRX_BUILD)/MCode_block.vhd
    scpu_mcode_block $s/MCode.vhd $mc
    puts "=== T182 trial: MCode.vhd with the microcode ROM as block RAM"
  }
  read_vhdl -library s816 [list $s/P65816_pkg.vhd $s/BCDAdder.vhd $s/AddSubBCD.vhd $s/ALU.vhd $s/AddrGen.vhd $mc $s/P65C816.vhd]
  read_vhdl -library work ../cores/c64scpu/trial/scpu_cpu816.vhd
  read_verilog -sv ../cores/c64scpu/trial/scpu_trial.v
  puts "=== T182 trial: scpu_trial_top read"
}
