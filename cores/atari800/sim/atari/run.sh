#!/bin/sh
# T88 §5: Watson's ANTIC and shared_enable (outside the repository: ATARI_SRC = a clone of MiSTer-devel/Atari800_MiSTer)
# in the mini machine of tb_mini.vhd, once with Watson's own 6502 (cpu.vhd + cpu_65xx.vhd: "orig") and once with ours
# (cores/atari800/cpu.vhd on proc_core: "ours") running testos.asm; the bus logs must be identical.
#   run.sh [orig|ours|both]        Needs ghdl, else the vc20-ghdl container.   CPUCYC, MEMMODE, SEED as environment.
set -e
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../../.." && pwd)
what=${1:-both}
if [ -z "$IN_CONTAINER" ]; then
  : "${ATARI_SRC:?set ATARI_SRC to a clone of MiSTer-devel/Atari800_MiSTer (commit 1440e41)}"
  mkdir -p "$here/build"
  64tass -q -b --nostart -o "$here/build/testos.rom" "$here/testos.asm"
  python3 -c "
d = open('$here/build/testos.rom','rb').read()
open('$here/build/testos.hex','w').write(''.join('%02X\n' % b for b in d))"
  if ! command -v ghdl >/dev/null 2>&1; then
    exec nice -n 15 container run --rm --cpus 2 -v "$root":/work -v "$ATARI_SRC":/atari:ro -e IN_CONTAINER=1 \
      -e CPUCYC="${CPUCYC:-150000}" -e MEMMODE="${MEMMODE:-0}" -e SEED="${SEED:-1}" -e THROTTLE="${THROTTLE:-000001}" -e DBG0="${DBG0:-0}" -e DBG1="${DBG1:-0}" \
      vc20-ghdl sh /work/cores/atari800/sim/atari/run.sh "$@"
  fi
else
  ATARI_SRC=/atari
  here=/work/cores/atari800/sim/atari
  root=/work
fi
F="--std=08 -frelaxed -fsynopsys -fexplicit"
A=$ATARI_SRC/rtl/common/a8core
C=$ATARI_SRC/rtl/common/components
W="$A/antic.vhdl $A/antic_counter.vhdl $A/antic_dma_clock.vhdl $A/simple_counter.vhdl $A/wide_delay_line.vhdl \
   $C/latch_delay_line.vhdl $C/generic_ram_infer.vhdl $C/complete_address_decoder.vhdl $A/enable_divider.vhdl \
   $C/delay_line.vhdl $A/shared_enable.vhdl"
run() { # name, hold1, cpu files
  name=$1; hold1=$2; shift; shift
  mkdir -p $here/build/$name
  cd $here/build/$name
  rm -f *.cf trace.txt
  ghdl -i $F $W $here/tb_mini.vhd "$@"
  ghdl -m $F tb_mini
  echo "== $name (MEMMODE ${MEMMODE:-0}, seed ${SEED:-1})"
  ./tb_mini -gROM=$here/build/testos.hex -gTRACE=trace.txt -gCPUCYC=${CPUCYC:-150000} -gMEMMODE=${MEMMODE:-0} \
      -gSEED=${SEED:-1} -gHOLD1=$hold1 -gDBG0=${DBG0:-0} -gDBG1=${DBG1:-0} -gTHROTTLE=${THROTTLE:-000001} 2>&1 | grep -v "metavalue" | tail -3
}
N=$root/vendor/icepi/6502n
A=$root/cores/atari800
if [ "$what" = orig ] || [ "$what" = both ]; then
  run orig true $A/cpu.vhd $A/cpu_65xx.vhd
fi
if [ "$what" = ours ] || [ "$what" = both ]; then
  run ours false $N/pkg_6502_defs.vhd $N/pkg_6502_opcodes.vhd $N/pkg_6502_decode.vhd $N/alu.vhd $N/bit_cpx_cpy.vhd \
      $N/shifter.vhd $N/implied.vhd $N/data_oper.vhd $N/proc_control.vhd $N/proc_interrupt.vhd \
      $N/proc_registers.vhd $N/proc_core.vhd $A/proc_interrupt_a8.vhd $A/proc_core_a8.vhd $A/cpu.vhd
fi
if [ "$what" = both ]; then
  python3 $here/cmp_logs.py $here/build/orig/trace.txt $here/build/ours/trace.txt
fi
