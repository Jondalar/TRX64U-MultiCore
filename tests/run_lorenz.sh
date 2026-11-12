#!/usr/bin/env bash
# tests/lorenz.py with the JTAG venv (pyftdi), see tools/trxdbg.sh
VENV="${U64II_JTAG_VENV:-${XDG_CACHE_HOME:-$HOME/.cache}/1541ultimate-tools/jtag-venv}"
exec "$VENV/bin/python" "$(dirname "$0")/lorenz.py" "$@"
