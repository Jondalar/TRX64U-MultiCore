#!/bin/sh
# Builds libresid.dylib/.so from TRX64's vendored reSID (VICE 3.10 src/resid, "1.0-pre2") with its flat C shim.
# The offline reference of the suite: resid_ref.py drives it with the exact bus timing of the test programs.
#   TRX64=<checkout of Jondalar/TRX64>   default ~/Development/C64/Tools/TRX64
set -e
here=$(cd "$(dirname "$0")" && pwd)
src=${TRX64:-$HOME/Development/C64/Tools/TRX64}/crates/trx64-core/vendor/resid
out=$here/.cache
mkdir -p "$out"
ext=so; [ "$(uname)" = Darwin ] && ext=dylib
nice -n 15 c++ -std=c++11 -O2 -shared -fPIC -ffp-contract=off -w -DVERSION='"1.0-pre2"' -I"$src" \
  "$src"/sid.cc "$src"/voice.cc "$src"/wave.cc "$src"/envelope.cc "$src"/filter8580new.cc "$src"/extfilt.cc \
  "$src"/pot.cc "$src"/dac.cc "$src"/version.cc "$src"/resid_shim.cc -o "$out/libresid.$ext"
echo "$out/libresid.$ext"
