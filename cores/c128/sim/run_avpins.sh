#!/bin/sh
# T140 D24 item 1 (c128_avpins.v): every ball of the A/V port carries the bit of the word the board says it must (Y word on the luma pin's
# group, chroma on its group, composite on the third);
# a source guard checks that mono/top.v uses the module with its default and keeps no other AVID assign. Final line PASS or FAIL. Run from anywhere.
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../.." && pwd)
out=${1:-$here/build/avpins}
mkdir -p "$out"
fail=0
for s in 1; do
  iverilog -g2012 -DSWAP=$s -o "$out/sw$s" "$here/tb_c128_avpins.v" "$root/cores/c128/mono/c128_avpins.v" || { echo "FAIL build"; exit 1; }
  vvp "$out/sw$s" > "$out/sw$s.log"
done
if python3 "$here/check_avpins.py" "$out/sw1.log"; then echo "ok   SWAP_YCV=1 (default): Y on the luma group, C on the chroma group, CV on the third"; else echo "FAIL default order"; fail=1; fi
top="$root/cores/c128/mono/top.v"
if grep -q "c128_avpins avpins (" "$top" && ! grep -q "SWAP_YCV" "$top" && [ "$(grep -c 'assign {AVID' "$top")" = 0 ]; then echo "ok   top.v uses c128_avpins with its default and has no AVID assign of its own"; else echo "FAIL top.v guard"; fail=1; fi
if grep -q "parameter SWAP_YCV = 1" "$root/cores/c128/mono/c128_avpins.v"; then echo "ok   default is the board's order"; else echo "FAIL default"; fail=1; fi
if [ $fail = 0 ]; then echo "PASS run_avpins"; else echo "FAIL run_avpins"; exit 1; fi
