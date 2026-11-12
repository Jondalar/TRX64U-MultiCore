#!/bin/sh
# T142 S11: the SNES core drives nothing on the slot or the IEC bus (tools/check_layer2_pins.py reads layer2_snes.v and the host's decoupler
# list in cores/snes/mono/host/top.v, the core's own copy).
set -e
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../.." && pwd)
w="$root/cores/snes/layer2_snes.v"
chk() { HOSTDIR=cores/snes/mono/host python3 "$root/tools/check_layer2_pins.py" "$1" layer2_snes snes_core; }
chk "$w"
echo "PASS pins"
