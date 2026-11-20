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

# vivado_env_load <file>: export the KEY=VALUE lines, after dropping every option variable the caller had (a stray T79_ROUTE would move the keys).
# The file is the commit's own data and this code runs over it with the builder's rights, so a line is taken only when its name is one of the
# build options (TRX_*, T79_*, T16_*: never PATH, PYTHONPATH, CODE_HASH_LIBS, LD_PRELOAD) and its value has no character a shell or a path could use
# ([A-Za-z0-9_.,:+-] only). Comments and empty lines are skipped; anything else ends the run with exit 1 and the line number.
vivado_env_load() {
  local k v line n=0
  while IFS= read -r k; do case "$k" in TRX_*|T79_*|T16_*) unset "$k" ;; esac; done < <(compgen -e)
  while IFS= read -r line || [ -n "$line" ]; do
    n=$((n + 1))
    [[ "$line" =~ ^[[:space:]]*(#.*)?$ ]] && continue
    if [[ "$line" =~ ^((TRX|T79|T16)_[A-Z0-9_]+)=([A-Za-z0-9_.,:+-]*)$ ]]; then
      export "${BASH_REMATCH[1]}=${BASH_REMATCH[3]}"
    else
      echo "ci/keys.sh: $1 line $n is not a build option (NAME is TRX_*, T79_* or T16_*; value only A-Za-z0-9_.,:+-)" >&2
      exit 1
    fi
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
