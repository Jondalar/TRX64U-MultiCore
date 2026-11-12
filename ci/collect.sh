#!/usr/bin/env bash
# T174 D4: the first job of the child `package`. The package carries every core, so the bitstream stages this parent did not start (their gateware
# did not change) are handed over from the cache here, one lane after the other, by the same scripts the child `vivado` runs (with CI_COLLECT_ONLY
# set: collect only reads, a cache miss is red and nothing is built - Alex 2026-11-08). A lane a child of this parent already handed over is left alone.
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/lib.sh"
export CI_COLLECT_ONLY=1   # Alex 2026-11-08: collecting never builds; a lane the cache lacks is red here (its vivado child builds it)
n=0
for lane in $(python3 "$ROOT/ci/plan.py" lanes); do
  case "$lane" in host) st=bit-host ;; core:*) st="bit-${lane#core:}" ;; large:*) st="large-${lane#large:}" ;; *) continue ;; esac
  if [ -f "$HANDOFF/$st/DONE" ]; then echo "=== collect $lane: handed over by this parent's child"; continue; fi
  echo "=== collect $lane: from the cache"
  case "$lane" in large:*) "$ROOT/ci/bitstream_mono.sh" --lane "${lane#large:}" ;; *) "$ROOT/ci/bitstream.sh" c64u --lane "$lane" ;; esac
  n=$((n + 1))
done
echo "=== collect: $n lane(s) from the cache in $(lap)"
