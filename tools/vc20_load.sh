#!/usr/bin/env bash
# T83: tools/vc20_load.py with pyftdi from the same JTAG venv as trxdbg.sh.
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
VENV="${U64II_JTAG_VENV:-${XDG_CACHE_HOME:-$HOME/.cache}/1541ultimate-tools/jtag-venv}"
exec "$VENV/bin/python" "$HERE/vc20_load.py" "$@"
