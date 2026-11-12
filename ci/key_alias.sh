#!/usr/bin/env bash
# T215 D3: one-time migration of the bitstream cache to the normalised keys (ci/code_hash.py). Run on the builder, in a checkout of the commit that
# carries T215 BEFORE its first push, with the cache mounted. For every entry the tree has today (host, core:<id>, mono-<id>, aux) it computes
# the key as it was (CODE_HASH=raw) and as it is now; where an entry of the old key exists and the new one does not, it copies it under the new name
# (a copy, not a link: cache_gc and the pins stay per entry). Nothing is built, nothing deleted; run it again at will.
#   ci/key_alias.sh [part]          CI_BIT_CACHE (default /cache/multicore/bit)
# Test caches (apptests-ok, render-ok, sims-ok, trxapps) are not migrated: they are cheap and re-run once.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
source "$ROOT/ci/lib.sh"
part="${1:-xc7a50tfgg484-1}"
cache="${CI_BIT_CACHE:-/cache/multicore/bit}"

keys() {   # keys <raw|norm>: the entry directory names, in a fixed order
  local H core O CP CR
  H="$(host_key "$part")"; echo "host-$H"
  echo "aux-$(aux_key "$part")"
  while IFS= read -r core; do
    CP="$(python3 "$ROOT/tools/core_toml.py" get "$core" place)"; CP="${CP:-${T79_PLACE:-}}"
    CR="$(python3 "$ROOT/tools/core_toml.py" get "$core" route)"; CR="${CR:-${T79_ROUTE:-}}"
    O="$(ooc_key "$part" "$core")"
    echo "core-$core-$(T79_PLACE="$CP" T79_ROUTE="$CR" core_key "$H" "$O")"
  done < <(python3 "$ROOT/ci/plan.py" lanes | sed -n 's/^core://p')
  while IFS= read -r core; do echo "mono-$core-$(mono_key "$part" "$core")"; done < <(python3 "$ROOT/ci/plan.py" lanes | sed -n 's/^large://p')
}

old=(); while IFS= read -r l; do old+=("$l"); done < <(CODE_HASH=raw keys)
new=(); while IFS= read -r l; do new+=("$l"); done < <(keys)
[ "${#old[@]}" = "${#new[@]}" ] || { echo "key_alias: ${#old[@]} old and ${#new[@]} new keys" >&2; exit 1; }
n=0
for i in "${!old[@]}"; do
  if [ "${old[$i]}" = "${new[$i]}" ]; then echo "same      ${new[$i]}"; continue; fi
  if [ -f "$cache/${new[$i]}/DONE" ]; then echo "present   ${new[$i]}"; continue; fi
  if [ -f "$cache/${old[$i]}/DONE" ]; then
    cp -R "$cache/${old[$i]}" "$cache/${new[$i]}.tmp.$$" && rm -rf "$cache/${new[$i]}" && mv "$cache/${new[$i]}.tmp.$$" "$cache/${new[$i]}"
    echo "migrated  ${old[$i]} -> ${new[$i]}"; n=$((n + 1))
  else echo "no entry  ${old[$i]} (builds as before)"; fi
done
echo "key_alias: $n entries migrated"
