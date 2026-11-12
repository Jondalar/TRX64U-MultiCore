#!/bin/sh
# T88 S2 (spec 5.6): the whole Atari core analyses and elaborates under GHDL (VHDL-2008, -frelaxed -fsynopsys).
# Reads cores/atari800/gen (Watson's files after tools/atari800_gen.sh: patched and with the clock enable) and
# cores/atari800/trx (ours); elaborates atari800core, the machine proper, and a8_machine, the machine with its
# glue. No simulation here (the benches do that); this is the gate before them.
#   container run --rm --cpus 2 -v "$PWD":/work -w /work -e IN_CONTAINER=1 vc20-ghdl sh cores/atari800/sim/gate/run.sh
set -e
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../../.." && pwd)
. "$root/cores/atari800/sim/files.sh"
out=${OUT:-$here/build/gate}
mkdir -p "$out"
cd "$out"
rm -f ./*.cf
echo "== import"
nice -n 15 ghdl -i $A8OPTS --work=work $A8FILES
for top in ${TOPS:-atari800core a8_core}; do
  echo "== elaborate $top"
  nice -n 15 ghdl -m $A8OPTS "$top"
done
echo "PASS gate"
