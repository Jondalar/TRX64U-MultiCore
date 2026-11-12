#!/bin/sh
# T164 WP6 / S9: overlay.v and c64_text.v in each of the 9 mode rows of hdmi_modes.vh (tb_ovl_geom.v). Needs iverilog. JOBS = parallel runs (default 4); niced.
#   run.sh [mode ...]     (default: all nine rows)
set -u
here=$(cd "$(dirname "$0")" && pwd)
D=$here/../..
out=$here/build
rm -rf "$out"; mkdir -p "$out/mem"
cp "$D"/mem/ovl_font8.mem "$D"/mem/ovl_font12.mem "$out/mem/"
modes=${*:-0 1 2 6 7 8 9 10 11}
JOBS=${JOBS:-4}

# build <name> <overlay.v> <c64_text.v> <mode> <NEG>   -> $out/<name>.vvp
build() {
  iverilog -g2012 -I "$D" -P tb.MODE=$4 -P tb.NEG=$5 -s tb -o "$out/$1.vvp" "$here/tb_ovl_geom.v" "$2" "$3" "$D/pal_ram.v" || exit 2
}
OV=$D/overlay.v; TX=$D/c64_text.v

fail=0
# 1. the real modules, every row
for m in $modes; do build "real$m" "$OV" "$TX" "$m" 0; done
for m in $modes; do echo "$m"; done | xargs -P "$JOBS" -I{} sh -c "cd '$out' && nice -n 15 timeout 1500 vvp -n real{}.vvp > real{}.log 2>&1"
for m in $modes; do
  sed -n '/^mode/,$p' "$out/real$m.log" | grep -v '\$finish'
  grep -q "^OVL_GEOM mode $m PASS" "$out/real$m.log" || fail=1
done

if [ $fail = 0 ]; then echo "OVL_GEOM ALL PASS"; else echo "OVL_GEOM FAILED"; fi
exit $fail
