#!/bin/sh
# T113 S10: the extra keys (c128_keys.v) against vectors written from the keyboard matrices (gen_keys_vec.py).  Run from the repository root (iverilog).
set -e
here=$(cd "$(dirname "$0")" && pwd)
out=${TMPDIR:-/tmp}/c128_keys
mkdir -p "$out"
python3 "$here/gen_keys_vec.py" > "$out/vec.txt"
iverilog -g2012 -o "$out/tb" "$here/tb_c128_keys.v" "$here/../c128_keys.v"
vvp "$out/tb" +VEC="$out/vec.txt" | tee "$out/log"
grep -q "^PASS tb_c128_keys" "$out/log" || { echo "FAIL run_keys"; exit 1; }
echo "PASS run_keys"
