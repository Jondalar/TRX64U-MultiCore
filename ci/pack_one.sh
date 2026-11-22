#!/usr/bin/env bash
# T174 D2.7 (Alex, 2026-11-07: "single .cor files must be packable"): ONE core's .cor from this working tree's application image and the bitstream
# the pipeline's cache holds for this tree's gateware - no package, no waiting for the other cores.
#
#   ci/pack_one.sh <core> [--app-dir <dir with <core>.app>] [--out <dir>]
#
# The .cor is the core's partial bound to the AXSS word of the host it was routed against (tools/pack_cores_mc.sh), plus the blueprint application. Both
# parts are looked up under the keys this very tree has (ci/lib.sh host_key / ooc_key / core_key, the build options of the Vivado step of
# ci/vivado.env): when the tree's gateware was built (by a lane of a push, of this branch or of main) the entries
# are there and the .cor is packed in seconds; when it was not, the script says which key is missing and what the cache does have - the bitstream is built
# by the lane of a push, never here. App-only changes keep the gateware key, so they hit. Defaults: --app-dir $CI_HANDOFF/trxapps/coreapps (ci/trxapps.sh
# has built it), --out $CI_HANDOFF/cor. DFX cores only (core.toml delivery .cor); a large core's .cor is packed by ci/package.sh.
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/lib.sh"
source "$ROOT/ci/bitstream_layers.sh"
core="${1:?usage: ci/pack_one.sh <core> [--app-dir D] [--out D]}"; shift
appdir="$HANDOFF/trxapps/coreapps" out="$HANDOFF/cor"
while [ $# -gt 0 ]; do
  case "$1" in --app-dir) appdir="$2"; shift 2 ;; --out) out="$2"; shift 2 ;; *) echo "ci/pack_one.sh: unknown argument $1" >&2; exit 2 ;; esac
done
die() { echo "ci/pack_one.sh: $*" >&2; exit 1; }

printf '%s\n' $(python3 "$ROOT/tools/core_toml.py" list-cor) | grep -qx "$core" || die "$core is no .cor core of this tree (tools/core_toml.py list-cor); a large core's .cor is packed by ci/package.sh"
[ -s "$appdir/$core.app" ] || die "no $appdir/$core.app (run ci/trxapps.sh first: tools/dev.sh cor does)"

# the build options of the lanes: ci/vivado.env, the one place that has them
while IFS='=' read -r k v; do case "$k" in ''|'#'*) continue ;; esac; export "$k=$v"; done < "$ROOT/ci/vivado.env"
# the part string is part of every key: the one the c64u branch of ci/bitstream.sh builds with (a hard-coded short name gave other keys - found against the cached dc11e82f entry)
part="$(sed -n 's/^  c64u) part=\([^;]*\);.*/\1/p' "$ROOT/ci/bitstream.sh")"
[ -n "$part" ] || die "no c64u part in ci/bitstream.sh"
cache="${CI_BIT_CACHE:-/cache/multicore/bit}"
ref="$(layer_reference "$ROOT/host/layers.txt")"
[ "$core" != "$ref" ] || die "$core is the reference core: its .cor comes out of the host run"
H="$(host_key "$part")"; hostdir="$cache/host-$H"
CORE_PLACE="$(python3 "$ROOT/tools/core_toml.py" get "$core" place)"; CORE_PLACE="${CORE_PLACE:-${T79_PLACE:-}}"
CORE_ROUTE="$(python3 "$ROOT/tools/core_toml.py" get "$core" route)"; CORE_ROUTE="${CORE_ROUTE:-${T79_ROUTE:-}}"
C="$(T79_PLACE="$CORE_PLACE" T79_ROUTE="$CORE_ROUTE" core_key "$H" "$(ooc_key "$part" "$core")")"
coredir="$cache/core-$core-$C"
Cr="$(core_key "$H" "$(ooc_key "$part" "$ref")")"; refdir="$cache/core-$ref-$Cr"
miss() {
  echo "ci/pack_one.sh: the cache has no bitstream for this tree's gateware: $1" >&2
  echo "  host key $H, $core key $C (reference $ref key $Cr)" >&2
  echo "  newest entries of $core: $(ls -t "$cache" 2>/dev/null | grep "^core-$core-" | head -3 | tr '\n' ' ')" >&2
  echo "  push the branch (its lane builds the entry; it needs a Vivado run), or pack the .cor from a package of a commit whose gateware is the same" >&2
  exit 3
}
[ -f "$hostdir/DONE" ] || miss "no host entry"
[ -f "$refdir/DONE" ] || miss "no entry of the reference core $ref"
core_entry_ok "$coredir" "$hostdir" "$core" || miss "no entry of $core routed against this host"

bits="$(mktemp -d)"; trap 'rm -rf "${bits:?}"' EXIT
cp "$refdir/${ref}_full.bit" "$bits/c64_basic.bit"
cp "$coredir/${core}_partial.bit" "$bits/"
mkdir -p "$out"
"$ROOT/tools/pack_cores_mc.sh" --bits "$bits" --app-dir "$appdir" --out "$out" --core "$core"
[ -s "$out/$core.cor" ] || die "no $core.cor was packed"
echo "=== pack_one: $out/$core.cor  host $H  $core $C (from $(cat "$coredir/from" 2>/dev/null || echo ?))"
sha256sum "$out/$core.cor"
