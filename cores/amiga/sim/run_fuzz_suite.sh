#!/bin/sh
# T85: the cache fuzz (tb_fuzz) over several DDR2 timings.
set -e
here=$(cd "$(dirname "$0")" && pwd)
w=$here/build/fuzzsuite      # not a fixed name in /tmp: the benches run in parallel
mkdir -p "$w"
for cfg in "8 0 0 11" "4 0 0 12" "12 4 3 13" "40 20 30 15"; do
  sh "$here/run_fuzz.sh" $cfg ${NCYC:-2000000} > "$w/fuzz.log" 2>&1 || { tail -30 "$w/fuzz.log"; echo "FAIL amiga_fuzz $cfg"; exit 1; }
  grep -E "^fuzz:" "$w/fuzz.log"
  grep -q "^PASS tb_fuzz" "$w/fuzz.log" || { grep -E "MISMATCH" "$w/fuzz.log" | head; echo "FAIL amiga_fuzz $cfg"; exit 1; }
done
echo "PASS amiga_fuzz"
