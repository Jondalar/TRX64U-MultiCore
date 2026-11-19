#!/usr/bin/env bash
# T239.1 D13: ci/keys.sh and the ci/lib.sh helpers that depend on where a run happens. Unit and happy path, nothing built.
#  1. keys.sh prints one name per gateware target (host, aux, reference core, DFX cores, large cores), twice the same, and the names are
#     what lib.sh computes with the options of ci/vivado.env (host_key / core_key by hand)
#  2. a caller's stray option variables do not move a name
#  3. CI_BRANCH / CI_REF_TYPE win over GITHUB_REF_NAME / GITHUB_REF_TYPE in branch_slug and pin_entry
#  4. heavy_lock refuses under CI without the lock directory; without CI it runs on
#  5. cache_put leaves a world-readable entry directory and no temp directory
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/.."
W="$(mktemp -d)"; trap 'rm -rf "${W:?}"' EXIT
fail() { echo "FAIL: $*"; exit 1; }

# two runs side by side (each hashes the whole tree, minutes): the plain one and one with option variables in the caller's environment
ci/keys.sh > "$W/a.txt" & pa=$!
T79_ROUTE=zzz TRX_SID=other TRX_FEATURES=none ci/keys.sh > "$W/b.txt" & pb=$!
wait "$pa" || fail "ci/keys.sh failed"; wait "$pb" || fail "ci/keys.sh with stray options failed"
a="$(cat "$W/a.txt")"; b="$(cat "$W/b.txt")"
[ "$a" = "$b" ] || fail "a stray TRX_SID / T79_ROUTE of the caller moved the names"
n="$(printf '%s\n' "$a" | wc -l | tr -d ' ')"
want=$(( 3 + $(python3 ci/plan.py lanes | grep -c '^core:') + $(python3 ci/plan.py lanes | grep -c '^large:') ))
[ "$n" = "$want" ] || fail "$n names, expected $want (host, aux, reference core, DFX cores, large cores)"
printf '%s\n' "$a" | grep -Eq '^host-[0-9a-f]{16}$' || fail "no host-<16 hex> name"
printf '%s\n' "$a" | grep -Eq '^aux-[0-9a-f]{16}$' || fail "no aux-<16 hex> name"
[ "$(printf '%s\n' "$a" | sort -u | wc -l | tr -d ' ')" = "$n" ] || fail "duplicate names"

# by hand with lib.sh: the host name and the reference core's name
cat > "$W/hand.sh" <<'HAND'
set -euo pipefail
source ci/lib.sh
while IFS='=' read -r k v; do case "$k" in ''|'#'*) continue ;; esac; export "$k=$v"; done < ci/vivado.env
part="$(sed -n 's/^  c64u) part=\([^;]*\);.*/\1/p' ci/bitstream.sh)"
H="$(host_key "$part")"; ref="$(layer_reference host/layers.txt)"
echo "host-$H"; echo "core-$ref-$(core_key "$H" "$(ooc_key "$part" "$ref")")"
HAND
by_hand="$(bash "$W/hand.sh")"
[ "$(printf '%s\n' "$a" | sed -n 1p)" = "$(printf '%s\n' "$by_hand" | sed -n 1p)" ] || fail "host name differs from host_key by hand"
[ "$(printf '%s\n' "$a" | sed -n 3p)" = "$(printf '%s\n' "$by_hand" | sed -n 2p)" ] || fail "reference core name differs from core_key by hand"

# 3 branch and tag
( source ci/lib.sh
  [ "$(CI_BRANCH=feat/x GITHUB_REF_NAME=builder-ref branch_slug)" = "feat_x" ] || exit 11
  [ "$(GITHUB_REF_NAME=a/b branch_slug)" = "a_b" ] || exit 12
  e="$W/e1"; mkdir -p "$e"
  CI_REF_TYPE=tag CI_BRANCH=v1.2 GITHUB_REF_NAME=builder-ref GITHUB_REF_TYPE=branch pin_entry "$e" grp
  [ -s "$e/pins/tag.v1.2" ] || exit 13
  e="$W/e2"; mkdir -p "$e"
  CI_BRANCH=feat/x GITHUB_REF_NAME=builder-ref pin_entry "$e" grp
  [ -s "$e/pins/branch.feat_x" ] || exit 14 ) || fail "branch / tag handling (code $?)"

# 4 heavy_lock: under CI without the lock directory it refuses, outside CI it lets the run go on (a Mac without /cache)
if [ ! -d /cache ]; then
  ( source ci/lib.sh; CI=true heavy_lock ) 2>/dev/null && fail "heavy_lock ran under CI without /cache"
  ( source ci/lib.sh; unset CI; heavy_lock ) || fail "heavy_lock failed outside CI without /cache"
else
  echo "note: /cache exists here, heavy_lock refusal not tried"
fi

# 5 cache_put
( source ci/lib.sh
  mkdir -p "$W/src/pins"; echo x > "$W/src/DONE"; echo y > "$W/src/pins/p"
  cache_put "$W/src" "$W/bit/host-1"; cache_put "$W/src" "$W/bit/host-1"
  [ -f "$W/bit/host-1/DONE" ] && [ -f "$W/bit/host-1/pins/p" ] || exit 31
  [ "$(ls "$W/bit")" = "host-1" ] || exit 32
  [ "$(stat -f %Lp "$W/bit/host-1" 2>/dev/null || stat -c %a "$W/bit/host-1")" = 755 ] || exit 33 ) || fail "cache_put (code $?)"
echo "keys test ok ($n names)"
