#!/usr/bin/env bash
# tests/cart (cartridge flash and EEPROM write-back) with the JTAG venv (pyftdi),
# see tools/trxdbg.sh. Puts the test CRTs on the board over FTP, then runs them.
#   tests/run_cart_tests.sh [easyflash_eapi] [flash_amd] [gmod2_eeprom]   (default: all)
# TRX64_HOST (default 192.0.2.189) and TRX64_CART_DIR (default trx64-cart on the first USB drive)
# pick the board and the directory on its storage.
set -uo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
VENV="${U64II_JTAG_VENV:-${XDG_CACHE_HOME:-$HOME/.cache}/1541ultimate-tools/jtag-venv}"
if pgrep -f lorenz.py >/dev/null; then
    echo "tests/lorenz.py holds the board and JTAG; try again when it is done" >&2
    exit 2
fi
tests=("$@")
[[ ${#tests[@]} -eq 0 ]] && tests=(easyflash_eapi flash_amd gmod2_eeprom)
status=0
for t in "${tests[@]}"; do
    echo "== $t"
    "$VENV/bin/python" "$HERE/cart/${t}_test.py" install && \
        "$VENV/bin/python" "$HERE/cart/${t}_test.py" run || status=1
done
exit $status
