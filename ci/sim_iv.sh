#!/usr/bin/env bash
# One iverilog bench of host/sim: compile with the design's own
# directory as module library (-y: a module is found by its file name) and run.
#
#   ci/sim_iv.sh tb_scaler.v [iverilog options...]
#
# With SIM_DEPS_LOG set (ci/sims.sh does) the repository files the compiler read
# are appended to it, one path per line: ci/sims.sh checks them against ci/sims.deps.
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
here="$root/host"
tb="$1"; shift
out="$(mktemp -d)"
trap 'rm -rf "$out"' EXIT
iverilog -g2012 -y "$here" -y "$here/sim" -M "all=$out/deps" -o "$out/tb.vvp" "$@" "$here/sim/$tb"
if [ -n "${SIM_DEPS_LOG:-}" ]; then
  sed "s|^$root/||" "$out/deps" | sort -u >> "$SIM_DEPS_LOG"
fi
(cd "$here/sim" && vvp -n "$out/tb.vvp")
