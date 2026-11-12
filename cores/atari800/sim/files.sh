# T88: the file list of the Atari core for GHDL and the benches (sourced by the run scripts; $root is set by the caller).
# gen/ = Watson's files, patched and with the clock enable (tools/atari800_gen.sh), trx/ = ours, cpu.vhd on proc_core_a8
# (cores/atari800's own copy of vendor/icepi/6502n's proc_core, direct NMI; every other sub-unit comes from the vendor tree).
A8OPTS="--std=08 -frelaxed -fsynopsys -fexplicit -Wno-hide -Wno-shared -Wno-library -Wno-others"
N=$root/vendor/icepi/6502n
A8CPU="$N/pkg_6502_defs.vhd $N/pkg_6502_opcodes.vhd $N/pkg_6502_decode.vhd $N/alu.vhd $N/bit_cpx_cpy.vhd
  $N/shifter.vhd $N/implied.vhd $N/data_oper.vhd $N/proc_control.vhd $N/proc_registers.vhd
  $root/cores/atari800/proc_interrupt_a8.vhd $root/cores/atari800/proc_core_a8.vhd $root/cores/atari800/cpu.vhd"
A8GEN=$(find "$root/cores/atari800/gen" -name '*.vhd*' | sort)
A8TRX=$(find "$root/cores/atari800/trx" -name '*.vhd' | sort)
A8FILES="$A8CPU $A8GEN $A8TRX"
