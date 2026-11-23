#!/usr/bin/env bash
# T239.3 V-4 (D28 point 3, D31, S11, S12): link the app of one core of this repository from the SDK package alone.
#
#   tools/sdk_core_app.sh <trx64-sdk-x.y.z.tar.gz> <core id> <out dir> [--no-link]     RV_PREFIX (default riscv32-unknown-elf-) names the compiler
#
# The package is unpacked into a fresh temporary directory outside the repository. The core's menu record (cores/<id>/app/desc.toml) is compiled against the [sizes] of its TOML B
# (cores/<id>/app/sdk.toml), then tools/app_build.py of the package makes the driver table and the I/O fragment of the core's TOML A (with the base blocks its TOML B names), the library table
# of the libraries its TOML B names, and links the app: the package's base archives, the sources that depend on the core, those libraries (their sources are the package's, the core's own
# are cores/<id>/app/lib) and the core's descriptor. Results: <out dir>/<id>.app (the image, the name tools/pack_cores_mc.sh looks for), <id>.map, <id>.desc.bin.
# Then the proof of V-4: tools/libtab.py mapcheck on the link map - it holds the sources of the libraries the TOML B names, the core's own library and the base, and no source of a
# library the TOML B does not name and no code of another core. --no-link makes everything but the link (no compiler needed).
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
tgz="${1:?usage: tools/sdk_core_app.sh <trx64-sdk-x.y.z.tar.gz> <core id> <out dir> [--no-link]}"
core="${2:?core id}"
out="${3:?out dir}"
nolink=""; [ "${4:-}" != "--no-link" ] || nolink=1
tgz="$(cd "$(dirname "$tgz")" && pwd)/$(basename "$tgz")"
RV="${RV_PREFIX:-riscv32-unknown-elf-}"
app="$ROOT/cores/$core/app"
[ -f "$app/sdk.toml" ] && [ -f "$app/desc.toml" ] || { echo "sdk_core_app: cores/$core/app has no sdk.toml and desc.toml" >&2; exit 1; }
W="$(mktemp -d "${TMPDIR:-/tmp}/sdkcoreapp.XXXXXX")"
trap 'rm -rf "$W"' EXIT
fail() { echo "sdk_core_app: FAILED: $*" >&2; exit 1; }
case "$W" in "$ROOT"/*) fail "the work directory is inside the repository" ;; esac
mkdir -p "$out"; out="$(cd "$out" && pwd)"

tar -xzf "$tgz" -C "$W"
P="$(echo "$W"/trx64-sdk-*)"
[ -d "$P" ] || fail "no trx64-sdk-* folder in $tgz"
for f in tools/app_build.py tools/libtab.py tools/descrec.py mk/appbuild.mk mk/appflags.mk gateware/shared.toml base/SIZED.txt base/linker.x lib/libtrxbase.a; do
  [ -f "$P/$f" ] || fail "the package lacks $f"
done
# the record of the core against the sizes of its TOML B; the TOML A names its registers by block.property (descrec resolves them), the maps are the package's
python3 -I "$P/tools/descrec.py" compile "$app/desc.toml" "$W/desc.bin" --maps "$P/maps" --sizes "$app/sdk.toml" >/dev/null || fail "the description of $core does not compile from the package"
cp "$W/desc.bin" "$out/$core.desc.bin"
args=(--sdk "$P" --toml-b "$app/sdk.toml" --out "$W/out" --src "$app/lib" --record "$W/desc.bin")
python3 -I "$P/tools/app_build.py" build "${args[@]}" --dry-run || fail "app_build.py build --dry-run"
[ -s "$W/out/drv/drvtab.h" ] && [ -s "$W/out/lib/libtab_table.cc" ] && [ -s "$W/out/io.ld" ] || fail "the generated files of the app are missing"
if [ -n "$nolink" ]; then echo "sdk_core_app: $core: generated, not linked (--no-link)"; exit 0; fi
command -v "${RV}gcc" >/dev/null 2>&1 || fail "no ${RV}gcc: the app of $core is linked with the compiler of the build image"
env -i PATH="$PATH" HOME="$W" LANG=C python3 -I "$P/tools/app_build.py" build "${args[@]}" >"$W/link.log" 2>&1 || { tail -40 "$W/link.log" >&2; fail "the link of the app of $core"; }
[ -s "$W/out/app.bin" ] && [ -s "$W/out/app.map" ] || fail "app.bin or app.map missing"
# what the package gave and what the repository gave: the base came from the package alone (no dependency file names the repository's own base), only the core's sources are the repository's
if grep -l -F -e "$ROOT/coreapp/" -e "$ROOT/app/" -e "$ROOT/uicore/" "$W"/out/obj/*.d 2>/dev/null; then fail "the build of $core read a source or header of the repository's base: it was not made from the package alone"; fi
python3 -I "$P/tools/libtab.py" mapcheck "$app/sdk.toml" "$W/out/app.map" "$ROOT" || fail "the link map of $core holds more or less than its TOML B names"
cp "$W/out/app.bin" "$out/$core.app"
cp "$W/out/app.map" "$out/$core.map"
blocks="$(tr '\n' ' ' < "$W/out/blocks.txt" 2>/dev/null || true)"
echo "sdk_core_app: ok: $core: $(wc -c < "$out/$core.app" | tr -d ' ') bytes from trx64-sdk-$(sed -n 's/^sdk //p' "$P/VERSION"), blocks: ${blocks:-the TOML A}, link map checked"
