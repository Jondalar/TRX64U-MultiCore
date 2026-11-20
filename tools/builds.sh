#!/usr/bin/env bash
# T239.1 S8: read the public builds repository without a token (tools/builds.py has the rules).
#   tools/builds.sh latest [--ref main]  |  files <tag>  |  fetch <tag> <asset> <dest>
set -euo pipefail
exec python3 -I "$(dirname "${BASH_SOURCE[0]}")/builds.py" "$@"
