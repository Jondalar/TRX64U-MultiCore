#!/usr/bin/env bash
# Assembles the SuperCPU board suite (T241.1) with 64tass: ops_tab.inc from gen_ops.py, then one .prg per scpu*.asm.
# The .prg files are committed, so the board needs no assembler.
set -eu
cd "$(dirname "$0")"
command -v 64tass >/dev/null || { echo "64tass is not installed" >&2; exit 2; }
python3 gen_ops.py > ops_tab.inc
for src in scpu*.asm; do
    64tass -a -q -o "${src%.asm}.prg" "$src"
    echo "${src%.asm}.prg $(wc -c < "${src%.asm}.prg" | tr -d ' ') bytes"
done
