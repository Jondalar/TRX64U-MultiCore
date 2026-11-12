#!/usr/bin/env bash
# T215 D2: the pinned tree-sitter install ci/code_hash.py needs, into one directory (default: the builder's cache, which every child mounts;
# ci/code_hash.py puts $CODE_HASH_LIBS or /cache/multicore/pylibs on its path). The grammars come from the language pack's own download,
# sha256-checked by its manifest, into the same directory (TREE_SITTER_LANGUAGE_PACK_CACHE_DIR). The trx64-sim-env image does the same at build time.
# An upgrade of the pins changes every key (ci/key_alias.sh pairs old and new).   ci/install_codehash.sh [dir]
set -euo pipefail
dir="${1:-${CODE_HASH_LIBS:-/cache/multicore/pylibs}}"
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
mkdir -p "$dir"
pip3 install --no-cache-dir --upgrade --target "$dir" -r "$here/codehash-requirements.txt"
TREE_SITTER_LANGUAGE_PACK_CACHE_DIR="$dir/grammars" PYTHONPATH="$dir" python3 -c \
  "import tree_sitter_language_pack as p; p.download(['verilog','vhdl','c','cpp','python','tcl','bash','toml'])"
echo "code_hash libs in $dir (set TREE_SITTER_LANGUAGE_PACK_CACHE_DIR=$dir/grammars)"
