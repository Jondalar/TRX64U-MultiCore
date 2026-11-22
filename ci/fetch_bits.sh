#!/usr/bin/env bash
# T239.1 D19: restore the cache entries a run needs from the bit-<entry> releases of the builds repository (ci/fetch_bits.py has the checks).
#   ci/fetch_bits.sh [core:<id> | large:<id>]       env: CI_BIT_CACHE, CI_HANDOFF, GH_TOKEN (optional)
set -euo pipefail
exec python3 -I "$(dirname "${BASH_SOURCE[0]}")/fetch_bits.py" "$@"
