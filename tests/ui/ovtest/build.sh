#!/bin/bash
# The test core of the overlay features (T256 S3, S5, S10). Base 4.0 loads no library file (T239.3 D58), so the test library is linked into the app: the core's TOML B
# (cores/<core>/app/sdk.toml) with library.toml appended, linked from the SDK package as tools/sdk_core_app.sh links the core's own app (its sources cores/<core>/app/lib, and this folder).
# Result: tests/ui/build/<core>_ovtest.app, the image tests/ui/emu.py runs for a feature with `# core: <core>+ovtest` (OVTEST_APP= names another); tests/ui/fixtures.py
# make_ovtest_cor packs the UI TOML of the test element (ovtest.toml) into the .cor beside it. The RISC-V compiler is the build image's, so run it where tools/dev.sh runs (the builder),
# or with RV_PREFIX=<your riscv32 toolchain>-.
#   tests/ui/ovtest/build.sh <trx64-sdk-x.y.z.tar.gz> [core]        core: vc20 (default)
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../../.." && pwd)"
tgz="${1:?usage: tests/ui/ovtest/build.sh <trx64-sdk-x.y.z.tar.gz> [core]}"
core="${2:-vc20}"
tgz="$(cd "$(dirname "$tgz")" && pwd)/$(basename "$tgz")"
RV="${RV_PREFIX:-riscv32-unknown-elf-}"
app="$ROOT/cores/$core/app"
fail() { echo "ovtest build: FAILED: $*" >&2; exit 1; }
[ -f "$app/sdk.toml" ] && [ -f "$app/desc.toml" ] || fail "cores/$core/app has no sdk.toml and desc.toml"
command -v "${RV}gcc" >/dev/null 2>&1 || fail "no ${RV}gcc: the test core is linked with the compiler of the build image"
W="$(mktemp -d "${TMPDIR:-/tmp}/ovtest.XXXXXX")"
trap 'rm -rf "$W"' EXIT
tar -xzf "$tgz" -C "$W"
P="$(echo "$W"/trx64-sdk-*)"
[ -d "$P" ] || fail "no trx64-sdk-* folder in $tgz"
# the core's TOML B and the files it names beside it (its TOML A), with the test library appended
mkdir -p "$W/app"
cp "$app"/*.toml "$W/app/"
{ echo; cat "$HERE/library.toml"; } >> "$W/app/sdk.toml"
python3 -I "$P/tools/descrec.py" compile "$app/desc.toml" "$W/desc.bin" --maps "$P/maps" --sizes "$W/app/sdk.toml" >/dev/null || fail "the description of $core does not compile from the package"
env -i PATH="$PATH" HOME="$W" LANG=C RV_PREFIX="$RV" nice -n 15 python3 -I "$P/tools/app_build.py" build --sdk "$P" --toml-b "$W/app/sdk.toml" --out "$W/out" \
  --src "$app/lib" --src "$HERE" --record "$W/desc.bin" > "$W/link.log" 2>&1 || { tail -40 "$W/link.log" >&2; fail "the link of the test core $core"; }
[ -s "$W/out/app.bin" ] || fail "app.bin missing"
grep -q 'ovtest\.o' "$W/out/app.map" || fail "the link map holds no ovtest.o"
mkdir -p "$HERE/../build"
cp "$W/out/app.bin" "$HERE/../build/${core}_ovtest.app"
ls -l "$HERE/../build/${core}_ovtest.app"
