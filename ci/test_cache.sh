#!/usr/bin/env bash
# S7 (T87): a core entry is used only with the host build it was routed against. A host entry that
# was deleted and rebuilt from the same sources (same key H, other host.bit) does not adopt the old
# core entries. On a fake cache directory.
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/.."
source ci/lib.sh
source ci/bitstream_layers.sh
W="$(mktemp -d)"; trap 'rm -rf "${W:?}"' EXIT
host="$W/host-aaaaaaaaaaaaaaaa"; core="$W/core-c64-bbbbbbbbbbbbbbbb"
mkdir -p "$host" "$core"
printf '{"host_key":"aaaaaaaaaaaaaaaa","host_sha":"111"}' > "$host/manifest.json"; : > "$host/DONE"
printf '{"core_key":"bbbbbbbbbbbbbbbb","host_key":"aaaaaaaaaaaaaaaa","host_sha":"111"}' > "$core/manifest.json"
: > "$core/DONE"
core_entry_ok "$core" "$host" || { echo "FAIL: complete entry with the same host_sha was a miss"; exit 1; }
# host evicted and rebuilt: same key H, other bytes
printf '{"host_key":"aaaaaaaaaaaaaaaa","host_sha":"222"}' > "$host/manifest.json"
if core_entry_ok "$core" "$host"; then echo "FAIL: the rebuilt host adopted an old partial"; exit 1; fi
# an entry without DONE is no hit
rm -f "${core:?}/DONE"; printf '{"host_sha":"222"}' > "$core/manifest.json"
if core_entry_ok "$core" "$host"; then echo "FAIL: entry without DONE was a hit"; exit 1; fi
echo "cache test ok"
