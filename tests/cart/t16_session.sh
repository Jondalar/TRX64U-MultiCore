#!/usr/bin/env bash
# tests/cart/t16_session.py with the JTAG venv (pyftdi), niced; see the script's docstring and
# TRX64-Ultimate/docs/specs/T16-board-session.md. selftest and --dry-run need no board and no venv.
#   tests/cart/t16_session.sh boot | status | run --cart ef1 [...] | selftest | mkbaseline
set -uo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
VENV="${U64II_JTAG_VENV:-${XDG_CACHE_HOME:-$HOME/.cache}/1541ultimate-tools/jtag-venv}"
PY="$VENV/bin/python"
for a in "$@"; do
    if [[ "$a" == "selftest" || "$a" == "--dry-run" || "$a" == "mkbaseline" ]]; then PY=python3; fi
done
exec nice -n 15 "$PY" -u "$HERE/t16_session.py" "$@"
