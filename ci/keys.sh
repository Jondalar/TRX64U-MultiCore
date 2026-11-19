#!/usr/bin/env bash
# The cache entry names of every gateware target of this tree, one per line, in a fixed order (D13, D19):
#   host-<H>   aux-<A>   core-<id>-<C> (the reference core first, it comes out of the host run)   mono-<id>-<K>
# The names are the directory names of /cache/multicore/bit and, with a "bit-" in front, the tags of the bitstream releases. Build options come
# from ci/vivado.env, the part from the c64u branch of ci/bitstream.sh: nothing is read from the caller's environment, so every caller gets the same names.
#   ci/keys.sh [--env FILE]    FILE replaces ci/vivado.env (tests)
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
envfile="$ROOT/ci/vivado.env"
if [ "${1:-}" = "--env" ]; then envfile="${2:?ci/keys.sh --env FILE}"; shift 2; fi
[ $# -eq 0 ] || { echo "usage: ci/keys.sh [--env FILE]" >&2; exit 2; }
source "$ROOT/ci/lib.sh"

# vivado_env_load <file>: export the KEY=VALUE lines, after dropping every option variable the caller had (a stray T79_ROUTE would move the keys)
vivado_env_load() {
  local k v
  while IFS= read -r k; do case "$k" in TRX_*|T79_*|T16_*) unset "$k" ;; esac; done < <(compgen -e)
  while IFS='=' read -r k v; do
    case "$k" in ''|'#'*) continue ;; esac
    export "$k=$v"
  done < "$1"
}
vivado_env_load "$envfile"

part="$(sed -n 's/^  c64u) part=\([^;]*\);.*/\1/p' "$ROOT/ci/bitstream.sh")"
[ -n "$part" ] || { echo "ci/keys.sh: no c64u part in ci/bitstream.sh" >&2; exit 1; }

H="$(host_key "$part")"; echo "host-$H"
echo "aux-$(aux_key "$part")"
ref="$(layer_reference "$ROOT/host/layers.txt")"
echo "core-$ref-$(core_key "$H" "$(ooc_key "$part" "$ref")")"
while IFS= read -r core; do
  CP="$(python3 "$ROOT/tools/core_toml.py" get "$core" place)"; CP="${CP:-${T79_PLACE:-}}"
  CR="$(python3 "$ROOT/tools/core_toml.py" get "$core" route)"; CR="${CR:-${T79_ROUTE:-}}"
  echo "core-$core-$(T79_PLACE="$CP" T79_ROUTE="$CR" core_key "$H" "$(ooc_key "$part" "$core")")"
done < <(python3 "$ROOT/ci/plan.py" lanes | sed -n 's/^core://p')
# a large core's flat build gets no build options (ci/bitstream_mono.sh), so its key is made without them
for k in $(compgen -e); do case "$k" in TRX_*|T79_*|T16_*) unset "$k" ;; esac; done
while IFS= read -r core; do echo "mono-$core-$(mono_key "$part" "$core")"; done < <(python3 "$ROOT/ci/plan.py" lanes | sed -n 's/^large://p')
