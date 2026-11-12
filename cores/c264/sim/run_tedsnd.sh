#!/bin/sh
# T90 S7: the TED's register writes to audio samples (tb_c264_tedsnd) and its negative control (digi_sound cut: must FAIL).
set -e
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../.." && pwd)
c=$root/cores/c264/rtl
out=$here/build
mkdir -p "$out"
src="$here/tb_c264_tedsnd.v $c/trx/c264_nco.v $c/trx/c264_audio.v $c/src/ted.v"
iverilog -g2012 -o "$out/tedsnd.vvp" $src
cd "$out"
nice -n 15 vvp -n tedsnd.vvp | tee tedsnd.log
grep -q "^PASS tb_c264_tedsnd" tedsnd.log
for m in DEAF QUIET; do
  iverilog -g2012 -DTEDSND_MUT_$m -o "$out/tedsnd_$m.vvp" $src
  nice -n 15 vvp -n tedsnd_$m.vvp > tedsnd_$m.log
  grep -q "^FAIL tb_c264_tedsnd" tedsnd_$m.log || { echo "control $m did not fail"; exit 1; }
  if grep -q "^PASS" tedsnd_$m.log; then echo "control $m passed"; exit 1; fi
  echo "control $m: $(grep -c '^FAIL ' tedsnd_$m.log) lines failed, as they must"
done
echo "PASS tedsnd"
