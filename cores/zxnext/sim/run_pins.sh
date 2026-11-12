#!/bin/sh
# T116 S11: the ZX Spectrum Next core drives nothing on the slot or the IEC bus (tools/check_layer2_pins.py reads layer2_zxnext.v and the host's decoupler
# list in cores/zxnext/mono/host/top.v, the core's own copy).
set -e
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../.." && pwd)
w="$root/cores/zxnext/layer2_zxnext.v"
chk() { HOSTDIR=cores/zxnext/mono/host python3 "$root/tools/check_layer2_pins.py" "$1" layer2_zxnext zxn_core; }
chk "$w"
echo "PASS pins"
