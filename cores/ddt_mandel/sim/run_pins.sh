#!/bin/sh
# T168 S5: a DDT core drives nothing on the slot or the IEC bus (tools/check_layer2_pins.py reads the wrapper and the host's decoupler list in host/top.v), for either core:
#   sh cores/ddt_mandel/sim/run_pins.sh ddt_mandel | ddt_novic
set -e
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../.." && pwd)
core=${1:?core id}
case $core in
  ddt_mandel) coremod=ddt_mandel_core ;;
  ddt_novic)  coremod=ddt_novic_core ;;
  *) echo "unknown core $core"; exit 2 ;;
esac
w="$root/cores/$core/layer2_$core.v"
chk() { python3 "$root/tools/check_layer2_pins.py" "$1" "layer2_$core" "$coremod"; }
chk "$w"
echo "PASS pins $core"
