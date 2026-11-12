#!/usr/bin/env bash
# Build the Commodore 264 as a LARGE core (T164): the flat full bitstream, and with --app-bin the .cfw.
#
#   cores/c264/mono/build.sh [--build-dir <name under host/>] [--stop synth|place|route]
#   cores/c264/mono/build.sh --pack --bit host/build_mono_c264/c264_mono.bit --app-bin <coreapp.bin> [--out <dir>]
#
# Build: run it inside vivado-env:2024.1 (vivado on the PATH) at the repository root, e.g. on the builder
#   docker run --rm --name c264-mono -v $PWD:/src -w /src vivado-env:2024.1 nice -n 15 timeout 3h cores/c264/mono/build.sh
# Vivado's working directory is host/ (cores/c264/mono/build_mono.tcl keeps only the output there and reads the host's files from its own copy, cores/c264/mono/host/; nothing under host/ is
# edited). Output in host/<build-dir> (default build_mono_c264): c264_mono.bit, timing.rpt, utilization*.rpt, route_status.rpt,
# vivado.log. Exit 1 when there is no bitstream or the setup slack is negative.
# Pack: tools/pack_cfw_mc.sh makes c264.cfw out of the bit and the generic application (coreapp.bin of tools/build_coreapp.sh);
# it needs the `container` CLI and the u64-build-env image (or --native inside it), not Vivado.
#   cores/c264/mono/build.sh --pack-cor --bit host/build_mono_c264/c264_mono.bit --app-bin <coreapp.bin> [--out <dir>]   the large .cor (tools/pack_large_cor.sh, python3 only)
# The descriptor is cores/c264/mono/core.toml (the only one; the pipeline reads it there); TRX_CORE_DESC names it for the tools run from here.
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/../../.." && pwd)"
export TRX_CORE_DESC="$HERE/core.toml"
CORE=c264
BUILD="build_mono_$CORE" STOP=route PACK=0 PACKCOR=0 BIT="" APPBIN="" OUT="$ROOT/dist"
while [ $# -gt 0 ]; do
  case "$1" in
    --build-dir) BUILD="$2"; shift 2;;
    --stop) STOP="$2"; shift 2;;
    --pack) PACK=1; shift;;
    --pack-cor) PACKCOR=1; shift;;
    --bit) BIT="$2"; shift 2;;
    --app-bin) APPBIN="$2"; shift 2;;
    --out) OUT="$2"; shift 2;;
    -h|--help) sed -n '2,15p' "$0"; exit 0;;
    *) echo "unknown option $1" >&2; exit 2;;
  esac
done

if [ "$PACK" = 1 ]; then
  [ -n "$BIT" ] && [ -n "$APPBIN" ] || { echo "--pack needs --bit and --app-bin" >&2; exit 2; }
  g="$(git -C "$ROOT" rev-parse --short=8 HEAD)"
  exec "$ROOT/tools/pack_cfw_mc.sh" --core "$CORE" --bit "$BIT" --app-bin "$APPBIN" \
    --id "$(git -C "$ROOT" rev-parse --abbrev-ref HEAD | tr '/' '-')-$g" --bit-commit "$g" --app-commit "$g" --out "$OUT"
fi

if [ "$PACKCOR" = 1 ]; then
  [ -n "$BIT" ] && [ -n "$APPBIN" ] || { echo "--pack-cor needs --bit and --app-bin" >&2; exit 2; }
  g="$(git -C "$ROOT" rev-parse --short=8 HEAD)"
  exec "$ROOT/tools/pack_large_cor.sh" --id "$CORE" --name "$(python3 "$ROOT/tools/core_toml.py" get "$CORE" name)" \
    --version "$(python3 "$ROOT/tools/core_toml.py" get "$CORE" version)" --bit "$BIT" --app-bin "$APPBIN" \
    --bit-commit "$g" --app-commit "$g" --out "$OUT"
fi

[ "$(python3 "$ROOT/tools/core_toml.py" get "$CORE" delivery)" = cfw ] || { echo "$CORE: $TRX_CORE_DESC does not say delivery = \"cfw\"" >&2; exit 1; }
[ -d "$ROOT/vendor/gideon/fpga" ] || { echo "vendor/gideon is missing: run tools/vendor_gideon.sh first" >&2; exit 1; }
b="$ROOT/host/$BUILD"
rm -rf "${b:?}"; mkdir -p "$b"
envs=()
while IFS= read -r e; do [ -z "$e" ] || envs+=("$e"); done < <(python3 "$ROOT/tools/core_toml.py" env "$CORE")
export HOME="${HOME_VIVADO:-/tmp/vivado-home}"; mkdir -p "$HOME"
(cd "$ROOT/host" && env TRX_BUILD="$BUILD" TRX_STOP="$STOP" ${envs[@]+"${envs[@]}"} \
   vivado -mode batch -nojournal -log "$BUILD/vivado.log" -source "../cores/$CORE/mono/build_mono.tcl")
bit="$b/${CORE}_mono.bit"
if [ "$STOP" = route ]; then
  [ -f "$bit" ] && [ -f "$b/timing.rpt" ] || { echo "no bitstream; see $b/vivado.log" >&2; exit 1; }
  wns="$({ grep -A2 'WNS(ns)' "$b/timing.rpt" || true; } | sed -n 3p | awk '{print $1}')"
  echo "WNS $wns ns"
  awk -v w="${wns:-0}" 'BEGIN { exit !(w < 0) }' && { echo "timing failed" >&2; exit 1; }
  echo "$bit"
fi
