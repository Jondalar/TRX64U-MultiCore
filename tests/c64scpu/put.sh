#!/usr/bin/env bash
# Copies the SuperCPU board suite (T241.1) to the board's SD card by FTP: /SD/tests/scpu/.
# A plain copy (no read-back). BOARD names the board's address; there is no default.
# With VICE-testprogs fetched by vice.sh, its SCPU programs go along (into the same folder).
#
#   BOARD=<address> tests/c64scpu/put.sh
set -u
shopt -s nullglob
cd "$(dirname "$0")"
board=${BOARD:?set BOARD to the board address}
files=(scpu*.prg)
vdir=$(./vice.sh --dir)
[ -d "$vdir" ] && files+=("$vdir"/*.prg)
fail=0
for f in "${files[@]}"; do
    echo "${f##*/} -> /SD/tests/scpu/"
    curl -sS --ftp-create-dirs --max-time 60 -T "$f" "ftp://$board/SD/tests/scpu/${f##*/}" || fail=1
done
exit $fail
