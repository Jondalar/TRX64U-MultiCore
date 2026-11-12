#!/usr/bin/env bash
# Run tools/trxdbg.py with pyftdi from the 1541ultimate-tools JTAG venv
# (created by tooling/u64ii_bscan.sh on first use).
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
VENV="${U64II_JTAG_VENV:-${XDG_CACHE_HOME:-$HOME/.cache}/1541ultimate-tools/jtag-venv}"
exec "$VENV/bin/python" "$HERE/trxdbg.py" "$@"
