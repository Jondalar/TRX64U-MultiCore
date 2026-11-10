#!/bin/bash
# The UI suite: Gherkin features against the TRX64U menu in ue2emu (docs/specs/T153-ui-sdk-and-tests.md D5/D6).
#   tests/ui/run.sh               every feature (TRX64U: features/, Amiga 1200: features_a1200/, first line `# core: amiga1200`)
#   tests/ui/run.sh features/navigation.feature [--only "part of a scenario name"] [--keep-going]
set -euo pipefail
cd "$(dirname "$0")"
files=()
while [ $# -gt 0 ] && [[ "$1" != --* ]]; do files+=("$1"); shift; done
[ ${#files[@]} -gt 0 ] || files=(features/*.feature features_a1200/*.feature)
nice -n 15 python3 runner.py "$@" "${files[@]}"
