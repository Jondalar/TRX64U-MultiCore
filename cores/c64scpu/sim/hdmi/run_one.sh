#!/bin/sh
# T222: one iverilog bench of cores/c64scpu/sim/hdmi against this core's own copy of the host video files (cores/c64scpu/mono/host),
# the counterpart of TRX64-Ultimate's ci/sim_iv.sh. The design's directory is the module library (-y: a module is found by its file name).
#   sh cores/c64scpu/sim/hdmi/run_one.sh tb_hdmi_timing.v [iverilog options...]
# With SIM_DEPS_LOG set (ci/sims.sh does) the repository files the compiler read are appended to it, one path per line.
set -eu
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../../.." && pwd)
hd=$root/cores/c64scpu/mono/host
tb=$1; shift
out=$(mktemp -d)
trap 'rm -rf "$out"' EXIT
iverilog -g2012 -y "$hd" -y "$here" -I "$hd" -M "all=$out/deps" -o "$out/tb.vvp" "$@" "$here/$tb"
if [ -n "${SIM_DEPS_LOG:-}" ]; then
  sed "s|^$root/||" "$out/deps" | sort -u >> "$SIM_DEPS_LOG"
fi
(cd "$here" && vvp -n "$out/tb.vvp")
