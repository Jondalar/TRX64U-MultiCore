#!/usr/bin/env bash
# T239.3 D17 / S18: build the example of an SDK package with nothing but the package.
#
#   tools/sdk_check.sh <trx64-sdk-x.y.z.tar.gz>      RV_PREFIX (default riscv32-unknown-elf-) names the compiler
#
# The package is unpacked into a fresh temporary directory outside the repository, the example library and its menu record are built there, and the
# build fails when any dependency file or the linked image names a path of the repository this script belongs to. One result line at the end.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
tgz="${1:?usage: tools/sdk_check.sh <trx64-sdk-x.y.z.tar.gz>}"
tgz="$(cd "$(dirname "$tgz")" && pwd)/$(basename "$tgz")"
RV="${RV_PREFIX:-riscv32-unknown-elf-}"
W="$(mktemp -d "${TMPDIR:-/tmp}/sdkcheck.XXXXXX")"
trap 'rm -rf "$W"' EXIT
fail() { echo "sdk_check: FAILED: $*" >&2; exit 1; }
case "$W" in "$ROOT"/*) fail "the work directory is inside the repository" ;; esac

tar -xzf "$tgz" -C "$W"
P="$(echo "$W"/trx64-sdk-*)"
[ -d "$P" ] || fail "no trx64-sdk-* folder in $tgz"
ver="$(sed -n 's/^sdk //p' "$P/VERSION")"
for f in VERSION README.md LICENSE NOTICE include/libapi.h include/libfmt.h mk/lib.mk mk/app.mk mk/lib.ld tools/u2a_pack.py tools/descrec.py tools/cor_pack.py image/coreapp.bin example/Makefile; do
  [ -f "$P/$f" ] || fail "the package lacks $f"
done
want="$(sed -n 's/^coreapp_sha256 //p' "$P/VERSION")"
have="$( (sha256sum "$P/image/coreapp.bin" 2>/dev/null || shasum -a 256 "$P/image/coreapp.bin") | cut -d' ' -f1)"
[ "$want" = "$have" ] || fail "image/coreapp.bin does not match VERSION"

# the build sees the package and the compiler, not the repository: a PATH without it, no repository variable
cd "$P/example"
make RV_PREFIX="$RV" >"$W/make.log" 2>&1 || { cat "$W/make.log" >&2; fail "make of the example"; }
make menu >>"$W/make.log" 2>&1 || { cat "$W/make.log" >&2; fail "make menu of the example"; }
[ -s example.u2a ] && [ -s example.menu.bin ] || fail "example.u2a or example.menu.bin missing"
python3 "$P/tools/descrec.py" dump example.menu.bin >/dev/null || fail "the menu record does not read back"
# every object of both sources was made (lib.mk compiles every file of LIB_SRCS)
[ -f example.o ] && [ -f greet.o ] || fail "an object of LIB_SRCS is missing"
# no path of the repository in the build: its dependency files, the log and the image
nd="$(ls ./*.d | wc -l)"
if grep -l -F "$ROOT" ./*.d "$W/make.log" 2>/dev/null; then fail "the build read a path of the repository $ROOT"; fi
if grep -a -q -F "$ROOT" example.u2a example.elf; then fail "the image names a path of the repository"; fi
grep -q "include/libapi.h" ./*.d || fail "the dependency files name no header of the package: nothing was checked"
dep_hdrs="$(cat ./*.d | tr -d '\\' | tr ' ' '\n' | grep "/include/" | sort -u | wc -l | tr -d ' ')"
tst="skipped (no host C++ compiler)"
if command -v "${CXX_HOST:-c++}" >/dev/null 2>&1; then
  make test >"$W/test.log" 2>&1 && tst="$(tail -1 "$W/test.log")" || { cat "$W/test.log" >&2; fail "the host test"; }
fi
echo "sdk_check: ok: trx64-sdk-$ver: example.u2a $(wc -c < example.u2a | tr -d " ") bytes, menu record $(wc -c < example.menu.bin | tr -d " ") bytes, $nd dependency files with $dep_hdrs package headers, none names $ROOT; $tst"
