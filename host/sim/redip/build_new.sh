#!/bin/sh
# T25.4: sid_multi in the hardware configuration (LEGACY_FC = 2, variant A, 8 SIDs) with tb_new.cpp
# and tb_sym.cpp (built twice: PULSE_FIX = 1 and 0 = the hardware default).
set -e
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../.." && pwd)
g=$root/vendor/icepi/c64_redip_sid
w=$g/reDIP-SID/gateware
b3=$here/build/new8fc2
mkdir -p "$b3/run"; cp "$w"/*.mem "$b3/run/"
nice -n 15 verilator --cc --exe --build -j 1 -O2 -Wno-fatal -Wno-WIDTHEXPAND -Wno-WIDTHTRUNC -Wno-lint -Wno-TIMESCALEMOD --public-flat-rw \
  --top-module sid_multi -I"$w" -GNS=8 -GLEGACY_FC=2 -GVARIANT_B=0 -GMEMDIR='"."' \
  -CFLAGS "-O2 -std=c++17" --Mdir "$b3/obj" -o "$b3/tb_new" \
  "$w/sid_pkg.sv" "$g/muladd.sv" "$w/sid_dac.sv" "$w/sid_waveform.sv" \
  "$w/sid_envelope.sv" "$w/sid_voice.sv" "$w/sid_filter.sv" "$w/sid_control.sv" "$w/sid_core.sv" "$g/sid_multi.sv" \
  "$here/tb_new.cpp" > "$b3/build.log" 2>&1 || { grep -E "^%Error" -A4 "$b3/build.log" | head -60; exit 1; }
for pf in 1 0; do
  b2=$here/build/sym$pf
  mkdir -p "$b2/run"; cp "$w"/*.mem "$b2/run/"
  nice -n 15 verilator --cc --exe --build -j 1 -O2 -Wno-fatal -Wno-WIDTHEXPAND -Wno-WIDTHTRUNC -Wno-lint -Wno-TIMESCALEMOD --public-flat-rw \
    --top-module sid_multi -I"$w" -GNS=8 -GLEGACY_FC=2 -GVARIANT_B=0 -GPULSE_FIX=$pf -GMEMDIR='"."' \
    -CFLAGS "-O2 -std=c++17" --Mdir "$b2/obj" -o "$b2/tb_sym" \
    "$w/sid_pkg.sv" "$g/muladd.sv" "$w/sid_dac.sv" "$w/sid_waveform.sv" \
    "$w/sid_envelope.sv" "$w/sid_voice.sv" "$w/sid_filter.sv" "$w/sid_control.sv" "$w/sid_core.sv" "$g/sid_multi.sv" \
    "$here/tb_sym.cpp" > "$b2/build.log" 2>&1 || { grep -E "^%Error" -A4 "$b2/build.log" | head -60; exit 1; }
done
