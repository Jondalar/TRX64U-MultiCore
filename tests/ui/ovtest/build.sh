#!/bin/bash
# The test app of the overlay features (T256 S3, S5, S10; T239.5 D19: the overlay service is the base's, so the test library runs only in the SDK lane). It is linked into the SDK's own example app
# (app/sdkpkg/example in the package), not into a core's app: the example's TOML B with tests/ui/ovtest/library.toml appended (base 4.0 loads no library file, T239.3 D58), linked from the SDK
# package as `make app` of the example links it (its TOML A gateware/example.toml, its record app/desc.toml, its sources example/ and this folder).
# Result: tests/ui/build/example_ovtest.app, the image tests/ui/emu.py runs for a feature with `# core: vc20+ovtest` (OVTEST_APP= names another); tests/ui/fixtures.py make_ovtest_cor packs the UI
# TOML of the test element (ovtest.toml) into the .cor beside it. The machine is the emulator profile vc20 (tests/coreapp/emu/profiles/vc20.toml): the example has no profile of its own. The RISC-V
# compiler is the build image's, so run it where the SDK lane runs, or with RV_PREFIX=<your riscv32 toolchain>-.
#   tests/ui/ovtest/build.sh <trx64-sdk-x.y.z.tar.gz>
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
tgz="${1:?usage: tests/ui/ovtest/build.sh <trx64-sdk-x.y.z.tar.gz>}"
tgz="$(cd "$(dirname "$tgz")" && pwd)/$(basename "$tgz")"
RV="${RV_PREFIX:-riscv32-unknown-elf-}"
fail() { echo "ovtest build: FAILED: $*" >&2; exit 1; }
command -v "${RV}gcc" >/dev/null 2>&1 || fail "no ${RV}gcc: the test app is linked with the compiler of the build image"
W="$(mktemp -d "${TMPDIR:-/tmp}/ovtest.XXXXXX")"
trap 'rm -rf "$W"' EXIT
tar -xzf "$tgz" -C "$W"
P="$(echo "$W"/trx64-sdk-*)"
[ -d "$P/example/app" ] || fail "no example/app in $tgz"
# the example's TOML B with the test library appended, and the menu record file it names (menu = "../menu.toml" is relative to the TOML B)
mkdir -p "$W/app"
cp "$P/example/app/toml_b.toml" "$W/app/toml_b.toml"
cp "$P/example/menu.toml" "$W/menu.toml"
{ echo; cat "$HERE/library.toml"; } >> "$W/app/toml_b.toml"
cd "$P/example"
python3 -I "$P/tools/descrec.py" compile app/desc.toml "$W/desc.bin" --sizes "$W/app/toml_b.toml" >/dev/null || fail "the description of the example does not compile from the package"
env -i PATH="$PATH" HOME="$W" LANG=C RV_PREFIX="$RV" nice -n 15 python3 -I "$P/tools/app_build.py" build --sdk "$P" --toml-a gateware/example.toml --maps gateware --toml-b "$W/app/toml_b.toml" \
  --out "$W/out" --src app --src . --src "$HERE" --record "$W/desc.bin" > "$W/link.log" 2>&1 || { tail -40 "$W/link.log" >&2; fail "the link of the example with ovtest"; }
[ -s "$W/out/app.bin" ] || fail "app.bin missing"
grep -q 'ovtest\.o' "$W/out/app.map" || fail "the link map holds no ovtest.o"
mkdir -p "$HERE/../build"
cp "$W/out/app.bin" "$HERE/../build/example_ovtest.app"
ls -l "$HERE/../build/example_ovtest.app"
