#!/bin/sh
# T113 S1: the tree of cores/c128 against PROVENANCE.md and the "no ROM" rule (tools/check_c128_tree.py); controls (lines "ctl| ") plant a
# 16 KiB kernal.rom, an unlisted source, a third-party file without its author line and a releases/ directory into a scratch copy.
set -e
root=$(cd "$(dirname "$0")/../../.." && pwd)
python3 "$root/tools/check_c128_tree.py" "$root/cores/c128"
python3 "$root/tools/check_c128_tree.py" --selftest
echo "PASS run_scan"
