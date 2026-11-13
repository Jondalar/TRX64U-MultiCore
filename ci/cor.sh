#!/usr/bin/env bash
# T174 D2.7 + D4: the child `cor` of a feature branch - ONE core's .cor from this parent's images (child `images`) and the core's bitstream, published
# into this commit's package version (<branch>-<sha8> and <branch>-latest of $MC_PKG) beside nothing else. On main the package packs every .cor.
#
#   ci/cor.sh core:<id>     a DFX core: ci/pack_one.sh (the partial of the cache under this tree's keys, bound to the host it was routed against)
#   ci/cor.sh large:<id>    a large core: tools/pack_large_cor.sh on the flat bitstream the child `vivado large:<id>` of this parent handed over
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/lib.sh"
target="${1:?usage: ci/cor.sh core:<id>|large:<id>}"
trxapps="$(need_stage trxapps)"
out="$(stage_dir "cor-${target//:/-}")"
case "$target" in
  core:?*)
    "$ROOT/ci/pack_one.sh" "${target#core:}" --app-dir "$trxapps/coreapps" --out "$out" ;;
  large:?*)
    core="${target#large:}"
    mono="$(need_stage "large-$core")"
    [ -f "$mono/${core}_mono.bit" ] || { echo "ci/cor.sh: no ${core}_mono.bit in $mono" >&2; exit 1; }
    desc="$ROOT/cores/$core/mono/core.toml"
    "$ROOT/tools/pack_large_cor.sh" --id "$core" --name "$(TRX_CORE_DESC="$desc" python3 "$ROOT/tools/core_toml.py" get "$core" name)" \
      --version "$(TRX_CORE_DESC="$desc" python3 "$ROOT/tools/core_toml.py" get "$core" version)" \
      --short "$(TRX_CORE_DESC="$desc" python3 "$ROOT/tools/core_toml.py" get "$core" short)" --bit "$mono/${core}_mono.bit" \
      --app-bin "$trxapps/coreapp.bin" --bit-commit "$(git -C "$ROOT" rev-parse --short=8 HEAD)" \
      --app-commit "$(cut -c1-8 "$trxapps/trxapps_commit")" --out "$out" ;;
  *) echo "ci/cor.sh: target core:<id> or large:<id>, not $target" >&2; exit 2 ;;
esac
rm -f "$out/DONE"
ls -l "$out"
(cd "$out" && sha256sum ./*.cor)
publish_files "$out"
: > "$out/DONE"
