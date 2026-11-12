#!/bin/sh
# T89 S13: the Apple core drives nothing on the slot or the IEC bus (check_pins.py reads layer2_apple2.v and the host's decoupler list in host/top.v).
set -e
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../.." && pwd)
w="$root/cores/apple2/layer2_apple2.v"
python3 "$here/check_pins.py" "$w"
echo "PASS pins"
