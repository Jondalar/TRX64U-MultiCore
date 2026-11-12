#!/bin/sh
# T90: the TED-free pieces of the C264 core (nco, keys, audio, window, memories, registers) in Icarus.
set -e
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../.." && pwd)
c=$root/cores/c264/rtl/trx
out=$here/build
mkdir -p "$out"
python3 "$here/keymap.py" vectors > "$out/keyvec.txt"
python3 - "$out/keyvec.txt" "$out/keyvec.hex" <<'PY'
import sys
with open(sys.argv[2], "w") as o:
    for l in open(sys.argv[1]):
        a, b = l.split()
        o.write(a + b + "\n")
PY
python3 "$here/keymap.py" | cmp - "$c/c264_keys.v" || { echo "c264_keys.v is not what keymap.py generates"; exit 1; }
iverilog -g2012 -o "$out/glue.vvp" "$here/tb_c264_glue.v" "$c/c264_nco.v" "$c/c264_keys.v" "$c/c264_audio.v" "$c/c264_win.v" "$c/c264_mem.v" "$c/c264_regs.v"
cd "$out" && nice -n 15 vvp -n glue.vvp | tee glue.log
grep -q "^PASS tb_c264_glue" glue.log
