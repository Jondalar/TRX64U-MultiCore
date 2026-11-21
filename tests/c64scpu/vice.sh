#!/usr/bin/env bash
# Fetches the SuperCPU programs of VICE-testprogs (github.com/libsidplayfp/VICE-testprogs, folder SCPU) at a pinned
# commit into a cache outside the repository; nothing of them is committed here. put.sh takes them along.
#   tests/c64scpu/vice.sh          fetch (once per commit)
#   tests/c64scpu/vice.sh --dir    print the cache folder
set -eu
SHA=4049366d71d74143d78b2da443a3aa75aad8a9a4
dir=${XDG_CACHE_HOME:-$HOME/.cache}/trx64-vice-scpu/$SHA
if [ "${1:-}" = --dir ]; then echo "$dir"; exit 0; fi
mkdir -p "$dir"
for f in scputest/scputest.prg scputest/scputest-0.prg scputest/scputest-1.prg badline/test_badline.prg \
         reu/reu-check.prg reu/dmatest.prg reu/dmastart.prg; do
    out=$dir/${f##*/}
    [ -s "$out" ] && continue
    curl -sSfL -o "$out.tmp" "https://raw.githubusercontent.com/libsidplayfp/VICE-testprogs/$SHA/SCPU/$f"
    mv "$out.tmp" "$out"
    echo "${f##*/} $(wc -c < "$out" | tr -d ' ') bytes"
done
echo "$dir"
