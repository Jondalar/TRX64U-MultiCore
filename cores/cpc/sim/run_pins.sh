#!/bin/sh
# T92 S12: the Amstrad CPC core drives nothing on the slot or the IEC bus (tools/check_layer2_pins.py reads layer2_cpc.v and the host's decoupler
# list in cores/cpc/mono/host/top.v, the core's own copy).
set -e
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../.." && pwd)
w="$root/cores/cpc/layer2_cpc.v"
chk() { HOSTDIR=cores/cpc/mono/host python3 "$root/tools/check_layer2_pins.py" "$1" layer2_cpc cpc_core; }
chk "$w"
echo "PASS pins"
