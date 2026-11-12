#!/usr/bin/env bash
# Run tools/pincheck.tcl in vivado-env on the NAS (T53). Reads, writes nothing there.
#
#   tools/pincheck.sh [part A [part B]]
set -euo pipefail
NAS="${TRX_NAS:-admin@192.0.2.2}"
DOCKER=/srv/.qpkg/container-station/bin/docker
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
work=/srv/trx64-build/work/pincheck
ssh "$NAS" "rm -rf '$work' && mkdir -p '$work/host' '$work/tools'"
scp -q "$ROOT"/host/*.xdc "$ROOT/host/mig_ddr2.prj" "$NAS:$work/host/"
scp -q "$ROOT/tools/pincheck.tcl" "$NAS:$work/tools/"
ssh "$NAS" "$DOCKER run --rm --cpus=1 -v '$work:/work' -e HOME=/tmp -e PART_A=${1:-xc7a50tfgg484-1} -e PART_B=${2:-xc7a100tfgg484-1} \
  -w /work vivado-env:2024.1 vivado -mode batch -nojournal -nolog -source tools/pincheck.tcl" | grep -E '^(DIFFERS|ERROR|xc7)'
