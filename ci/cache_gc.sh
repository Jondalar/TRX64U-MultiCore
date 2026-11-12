#!/usr/bin/env bash
# Cache janitor (T87 D14): see ci/cache_gc.py. Runs under a heavy slot at the end of the bit stages, and only when one is free (ci/lib.sh cache_gc_if_free).
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/.."
runsarg=()
[ ! -d /cache/runs ] || runsarg=(--runs /cache/runs)
exec python3 ci/cache_gc.py ${runsarg[@]+"${runsarg[@]}"} "$@"
