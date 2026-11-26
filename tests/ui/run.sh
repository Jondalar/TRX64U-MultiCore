#!/bin/bash
# The UI suite: Gherkin features against the cores' menus in ue2emu (docs/specs/T153-ui-sdk-and-tests.md D5/D6).
#   tests/ui/run.sh               every feature (Amiga 1200: features_a1200/, first line `# core: amiga1200`; NES: features_nes/; the other cores: features_cores/, first line `# core: <name>`)
#   A core with an app of its own (the VIC-20, the A1200: T239.5 D13) runs on its image, COREAPP_<core>; COREAPP is the shared image of the others.
#   tests/ui/run.sh features_a1200/navigation.feature [--only "part of a scenario name"] [--keep-going]
set -euo pipefail
cd "$(dirname "$0")"
files=()
while [ $# -gt 0 ] && [[ "$1" != --* ]]; do files+=("$1"); shift; done
[ ${#files[@]} -gt 0 ] || files=(features_a1200/*.feature features_cores/*.feature features_nes/*.feature)
nice -n 15 python3 runner.py "$@" "${files[@]}"
