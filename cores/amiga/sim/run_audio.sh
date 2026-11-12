#!/bin/sh
# T85: amiga_audio (FIFO with rate adaption) under stall patterns. Verilator. Usage: run_audio.sh [scenario ...] (default: all)
set -e
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../.." && pwd)
out=$here/build/audio
mkdir -p "$out"
cd "$out"
nice -n 15 verilator --cc --exe --build -O2 -Wno-fatal -Wno-lint -Wno-style -Wno-WIDTH -j 2 --top-module amiga_audio \
  -CFLAGS -O2 $root/cores/amiga/rtl/trx/amiga_audio.v "$here/tb_audio.cpp" -o tb_audio > build.log 2>&1 || { tail -40 build.log; exit 1; }
[ $# -gt 0 ] || set -- clean board burst heavy long pause rate rate2 rate3
rc=0
for s in "$@"; do nice -n 15 ./obj_dir/tb_audio $s ${SECS:-6} | tee run_$s.log || rc=1; done
exit $rc
