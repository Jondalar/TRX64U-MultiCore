#!/bin/sh
# S14 (T164 WP10): PHI2 / DOTCLK / RESET under the port lock, PAL and NTSC system clocks (Icarus). Output under build/.
# MODE=ctl: the two controls (today's forced-low lines; a switch without the hold) must FAIL.
set -e
here=$(cd "$(dirname "$0")" && pwd)
D=$here/../..
out=$here/build
mkdir -p "$out"
run() { # <name> <defines...>
  n=$1; shift
  iverilog -g2012 "$@" -o "$out/$n.vvp" -s tb_portlock "$here/tb_portlock.v" "$D/portlock_clk.v"
  nice -n 15 vvp -n "$out/$n.vvp" | grep -v finish > "$out/$n.log" || true
  cat "$out/$n.log"
}
if [ "${MODE:-}" = ctl ]; then
  for c in CTL_OLD CTL_RAW; do
    run "$c" -D$c > /dev/null
    if grep -q '^PASS' "$out/$c.log"; then echo "control $c did not fail"; exit 1; fi
    echo "control $c failed as it must: $(grep -c '^FAIL' "$out/$c.log") lines, last: $(tail -1 "$out/$c.log")"
  done
  exit 0
fi
run pal -DPERIOD_PS=31718
run ntsc -DPERIOD_PS=30557
grep -q '^PASS' "$out/pal.log" && grep -q '^PASS' "$out/ntsc.log"
