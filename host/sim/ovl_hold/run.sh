#!/bin/sh
# T135 D13: the box of the direct-boot line (41 x 21 cells of 8 x 9, ovtext.cc) through host/overlay.v in each of the 9 mode rows of
# hdmi_modes.vh (tb_ovl_hold.v), against the table of the spec (T135 section 5, D13): mode:SX:SY.
# Needs iverilog. JOBS = parallel runs (default 4); niced.
#   run.sh
set -u
here=$(cd "$(dirname "$0")" && pwd)
D=$here/../..
out=$here/build
rm -rf "$out"; mkdir -p "$out/mem"
cp "$D"/mem/ovl_font8.mem "$D"/mem/ovl_font12.mem "$out/mem/"
JOBS=${JOBS:-4}
# mode:SX:SY   0 720x576, 1/7 1280x720, 2/8 1920x1080, 6 640x480, 9 800x600, 10 1024x768, 11 1280x1024
TABLE="0:2:3 6:1:2 1:3:3 7:3:3 2:4:4 8:4:4 9:2:3 10:3:4 11:3:4"

build() { # name mode cpl nl sx sy
  iverilog -g2012 -I "$D" -P tb.MODE=$2 -P tb.CPL=$3 -P tb.NL=$4 -P tb.EXP_SX=$5 -P tb.EXP_SY=$6 -s tb -o "$out/$1.vvp" \
    "$here/tb_ovl_hold.v" "$D/overlay.v" "$D/c64_text.v" "$D/pal_ram.v" || exit 2
}
for e in $TABLE; do
  m=${e%%:*}; r=${e#*:}; build "real$m" "$m" 41 21 "${r%%:*}" "${r#*:}"
done
for e in $TABLE; do echo "${e%%:*}"; done | xargs -P "$JOBS" -I{} sh -c "cd '$out' && nice -n 15 timeout 1500 vvp -n real{}.vvp > real{}.log 2>&1"
fail=0
for e in $TABLE; do
  m=${e%%:*}
  grep -h '^mode' "$out/real$m.log"
  grep -q "^OVL_HOLD mode $m PASS" "$out/real$m.log" || { fail=1; cat "$out/real$m.log"; }
done
if [ $fail = 0 ]; then echo "OVL_HOLD ALL PASS"; else echo "OVL_HOLD FAILED"; fi
exit $fail
