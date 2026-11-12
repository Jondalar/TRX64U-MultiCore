#!/usr/bin/env bash
# Run 64tass for the Mac: built once from the 1541ultimate checkout's
# tools/64tass source into .cache/64tass (that checkout's own binary is the
# Linux one its build container makes). Same assembler as its e2e tests use.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
# An installed assembler wins: the CI image has one, and building the Mac's copy there would look for a
# checkout that is not on that machine (run 55).
if command -v 64tass >/dev/null 2>&1; then exec 64tass "$@"; fi
SRC="${TRX_64TASS_SRC:-$HOME/Development/1541ultimate/tools/64tass}"
BIN="$ROOT/.cache/64tass/64tass"
if [[ ! -x "$BIN" ]]; then
    rm -rf "$ROOT/.cache/64tass" && mkdir -p "$ROOT/.cache"
    cp -R "$SRC" "$ROOT/.cache/64tass"
    make -s -C "$ROOT/.cache/64tass" clean >/dev/null 2>&1 || true
    make -s -C "$ROOT/.cache/64tass" >/dev/null
fi
exec "$BIN" "$@"
