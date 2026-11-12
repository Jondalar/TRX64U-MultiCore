#!/bin/sh
# T88 §5: the 6502 of the Atari core (cores/atari800/cpu.vhd on proc_core) under ghdl.
#   run.sh cycles     every opcode's cycle count against the 6502 table, in four clocking/RDY/wait-state variants
#   run.sh halt       RDY (ANTIC) halt bench: the executed bus cycles with RDY patterns = those without
#   run.sh dormann    Klaus Dormann's functional test (needs DORMANN_DIR); stress: the same with wait states and RDY
#   run.sh decimal    Bruce Clark's decimal test (needs DORMANN_DIR)
#   run.sh interrupt  Klaus Dormann's interrupt test (needs DORMANN_DIR and 64tass)
#   run.sh all        cycles halt interrupt dormann decimal (not stress)
# Needs ghdl on PATH; without it, the container image vc20-ghdl is used (Apple `container`, 2 CPUs, niced).
# The Dormann sources are not in the repository: DORMANN_DIR is a clone of
# Klaus2m5/6502_65C02_functional_tests (GPL-3; the decimal test is Bruce Clark's, public domain).
set -e
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../../.." && pwd)
out=$here/${BUILD:-build}
mkdir -p "$out"
mode=${1:-all}

# the images that need the host's python3/64tass
prep() {
  if [ -n "$DORMANN_DIR" ]; then
    case $mode in dormann|stress|decimal|all)
      python3 $here/mkimage.py $DORMANN_DIR/bin_files/6502_functional_test.bin $out/dormann.hex --reset 0x400
      python3 $here/a65conv.py $DORMANN_DIR/6502_decimal_test.a65 $out/dec.asm chk_n=1 chk_v=1 chk_z=1
      64tass -q -b --nostart -L $out/dec.lst -o $out/dec.bin $out/dec.asm
      python3 $here/lstaddr.py $out/dec.lst '^[.][0-9a-f]{4}\s+DONE\s*$' > $out/dec.addr
      python3 $here/mkimage.py $out/dec.bin $out/dec.hex --load 0x200 --reset 0x200 ;; esac
    case $mode in interrupt|all)
      # two checks are patched out: a BRK that an NMI takes over leaves B set on the stack, and the BRK is then lost
      # (the test's own comments say a real NMOS 6502 does this); what it tests after them stays
      python3 $here/a65conv.py $DORMANN_DIR/6502_interrupt_test.a65 $out/int.asm \
          "@unexpected B-flag!=>nop" "@lost an interrupt=>nop"
      64tass -q -b --nostart -L $out/int.lst -o $out/int.bin $out/int.asm
      python3 $here/lstaddr.py $out/int.lst 'test passed, no errors' > $out/int.addr
      python3 $here/mkimage.py $out/int.bin $out/int.hex --load 0x400 --reset 0x400 ;; esac
  fi
  python3 $here/cycles.py gen $out/cycles.hex $out/cycles.json
  python3 $here/cycles.py gen $out/stable.hex $out/stable.json --stable
}

if [ -z "$IN_CONTAINER" ] && ! command -v ghdl >/dev/null 2>&1; then
  prep
  exec nice -n 15 container run --rm --cpus 2 -v "$root":/work -e IN_CONTAINER=1 -e BUILD="${BUILD:-build}" \
    vc20-ghdl sh /work/cores/atari800/sim/cpu/run.sh "$@"
fi
[ -z "$IN_CONTAINER" ] && prep

cd "$out"
N=$root/vendor/icepi/6502n
A=$root/cores/atari800
F="--std=08 -frelaxed -fsynopsys"
ghdl -a $F $N/pkg_6502_defs.vhd $N/pkg_6502_opcodes.vhd $N/pkg_6502_decode.vhd $N/alu.vhd \
    $N/bit_cpx_cpy.vhd $N/shifter.vhd $N/implied.vhd $N/data_oper.vhd $N/proc_control.vhd \
    $N/proc_interrupt.vhd $N/proc_registers.vhd $N/proc_core.vhd $A/proc_interrupt_a8.vhd $A/proc_core_a8.vhd $A/cpu.vhd $here/tb_cpu.vhd
ghdl -e $F tb_cpu
fail=0
sim() { # generics...
  g=""
  for x in "$@"; do g="$g -g$x"; done
  ./tb_cpu $g > sim.log 2>&1 || true
  tail -2 sim.log
  grep -q "PASS" sim.log
}
want() { [ "$mode" = "$1" ] || { [ "$mode" = all ] && [ "$1" != stress ]; }; }

if want cycles; then
  # variants: CEMODE ENMODE MEMMODE RDYMODE
  for v in "0 0 0 0" "1 1 1 0" "1 0 1 1" "1 1 1 2" "1 1 0 4"; do
    set -- $v
    echo "== cycles: CE=$1 EN=$2 MEM=$3 RDY=$4"
    sim IMAGE=cycles.hex MAXCYC=200000 SUCCESS=$(python3 -c "import json;print(json.load(open('cycles.json'))['end'])") \
        TRACE=cyc_$1$2$3$4.txt CEMODE=$1 ENMODE=$2 MEMMODE=$3 RDYMODE=$4 || fail=1
    python3 $here/cycles.py check cyc_$1$2$3$4.txt cycles.json || fail=1
  done
fi
if want halt; then
  # SHA/SHS/SHX/SHY are left out of the program: with RDY low in their cycles the real NMOS 6502 (and proc_core) write
  # to another address, so "same as unhalted" is not their rule
  END=$(python3 -c "import json;print(json.load(open('stable.json'))['end'])")
  sim IMAGE=stable.hex MAXCYC=200000 SUCCESS=$END TRACE=halt_ref.txt CEMODE=0 ENMODE=0 MEMMODE=0 RDYMODE=0 || fail=1
  for v in "1 1 1 1" "1 1 1 2" "1 1 1 3" "1 0 0 4" "0 0 0 2" "1 1 0 4"; do
    set -- $v
    echo "== halt: CE=$1 EN=$2 MEM=$3 RDY=$4"
    sim IMAGE=stable.hex MAXCYC=200000 SUCCESS=$END TRACE=halt_$1$2$3$4.txt CEMODE=$1 ENMODE=$2 MEMMODE=$3 RDYMODE=$4 || fail=1
    python3 $here/halt.py halt_ref.txt halt_$1$2$3$4.txt || fail=1
  done
fi
if want dormann && [ -f dormann.hex ]; then
  date
  echo "== dormann functional: CE 9/10, a cycle on every CE tick (96.2 M cycles, ~25 min)"
  sim IMAGE=dormann.hex MAXCYC=400000000 SUCCESS=13417 CEMODE=1 ENMODE=0 || fail=1
fi
if want stress && [ -f dormann.hex ]; then
  date
  echo "== dormann functional: CE 9/10, memory wait states, RDY bursts (~45 min)"
  sim IMAGE=dormann.hex MAXCYC=400000000 SUCCESS=13417 CEMODE=1 ENMODE=0 MEMMODE=1 RDYMODE=1 || fail=1
fi
if want decimal && [ -f dec.hex ]; then
  date
  echo "== Bruce Clark decimal test, ADC/SBC for all inputs, A C N V Z checked (35 M cycles)"
  sim IMAGE=dec.hex MAXCYC=400000000 SUCCESS=$(cat dec.addr) RESADDR=11 CEMODE=1 ENMODE=0 || fail=1
fi
if want interrupt && [ -f int.hex ]; then
  echo "== dormann interrupt test"
  sim IMAGE=int.hex SUCCESS=$(cat int.addr) IPORT=49148 CEMODE=1 ENMODE=0 || fail=1
  sim IMAGE=int.hex SUCCESS=$(cat int.addr) IPORT=49148 CEMODE=1 ENMODE=0 MEMMODE=1 RDYMODE=0 || fail=1
fi
exit $fail
