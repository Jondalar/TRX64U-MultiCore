#!/bin/sh
# T91 S12: the ZX Spectrum core drives nothing on the slot or the IEC bus (tools/check_layer2_pins.py reads layer2_zx.v and the host's decoupler
# list in cores/zx/mono/host/top.v, the core's own copy).
set -e
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../.." && pwd)
w="$root/cores/zx/layer2_zx.v"
chk() { HOSTDIR=cores/zx/mono/host python3 "$root/tools/check_layer2_pins.py" "$1" layer2_zx zx_core; }
chk "$w"
echo "PASS pins"
