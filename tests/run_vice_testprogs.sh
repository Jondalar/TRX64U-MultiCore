#!/usr/bin/env bash
# tests/cart/vice_testprogs.py (VICE cartridge testprogs on the board) with the JTAG venv
# (pyftdi), see tools/trxdbg.sh. TRX64_HOST picks the board, VICE_TESTPROGS the testprogs.
VENV="${U64II_JTAG_VENV:-${XDG_CACHE_HOME:-$HOME/.cache}/1541ultimate-tools/jtag-venv}"
if pgrep -f lorenz.py >/dev/null; then
    echo "tests/lorenz.py holds the board and JTAG; try again when it is done" >&2
    exit 2
fi
exec "$VENV/bin/python" "$(dirname "$0")/cart/vice_testprogs.py" "$@"
