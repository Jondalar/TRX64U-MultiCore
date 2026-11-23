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
for f in VERSION README.md api.md LICENSE NOTICE include/libapi.h include/libfmt.h mk/lib.mk mk/app.mk mk/lib.ld tools/u2a_pack.py tools/descrec.py tools/basereg.def tools/cor_pack.py image/coreapp.bin example/Makefile; do
  [ -f "$P/$f" ] || fail "the package lacks $f"
done
want="$(sed -n 's/^coreapp_sha256 //p' "$P/VERSION")"
have="$( (sha256sum "$P/image/coreapp.bin" 2>/dev/null || shasum -a 256 "$P/image/coreapp.bin") | cut -d' ' -f1)"
[ "$want" = "$have" ] || fail "image/coreapp.bin does not match VERSION"

# S5 (T239.3): the base API of the package is one number in the four places that name it, and the hash VERSION names is the hash of include/
maj="$(sed -n 's/^#define U2A_ABI_MAJOR \([0-9]*\).*/\1/p' "$P/include/libfmt.h")"
min="$(sed -n 's/^#define U2A_BASE_MINOR \([0-9]*\).*/\1/p' "$P/include/libfmt.h")"
[ -n "$maj" ] && [ -n "$min" ] || fail "include/libfmt.h has no base API version"
[ "$(sed -n 's/^base_api //p' "$P/VERSION")" = "$maj.$min" ] || fail "VERSION names base_api $(sed -n 's/^base_api //p' "$P/VERSION"), include/libfmt.h is $maj.$min"
[ "$(sed -n 's/^base_api *= *"\([^"]*\)".*/\1/p' "$P/image/version.toml")" = "$maj.$min" ] || fail "image/version.toml names another base_api than include/libfmt.h ($maj.$min)"
grep -q "This package is base API $maj.$min\." "$P/README.md" || fail "README.md names another base API than include/libfmt.h ($maj.$min)"
hv="$(sed -n 's/^api_headers_sha256 //p' "$P/VERSION")"
[ "$hv" = "$(python3 -I "$ROOT/tools/sdk_api.py" hash-dir "$P/include")" ] || fail "the hash of the public headers in VERSION is not the hash of include/"
grep -q "${hv:0:12}" "$P/README.md" || fail "README.md does not carry the hash of the public headers"

# The build runs with an empty environment but PATH (the compiler is on it) and HOME: no GIT_DIR or other variable of the CI reaches it. That
# the build read nothing of the repository is shown by the grep of its dependency files, its log and the image below.
cd "$P/example"
CLEAN=(env -i PATH="$PATH" HOME="$W" LANG=C)
"${CLEAN[@]}" make RV_PREFIX="$RV" >"$W/make.log" 2>&1 || { cat "$W/make.log" >&2; fail "make of the example"; }
"${CLEAN[@]}" make menu >>"$W/make.log" 2>&1 || { cat "$W/make.log" >&2; fail "make menu of the example"; }
[ -s example.u2a ] && [ -s example.menu.bin ] || fail "example.u2a or example.menu.bin missing"
python3 "$P/tools/descrec.py" dump example.menu.bin >/dev/null || fail "the menu record does not read back"
# a [[window]] compiles from the package alone (descrec.py reads tools/basereg.def beside itself), and one over a register the base drives is refused
printf '[[window]]\nname = "mixer"\naddress = 0xA0000500\nsize = 0x14\n' > win.toml
python3 "$P/tools/descrec.py" compile win.toml win.bin >/dev/null 2>&1 || fail "a record with a window does not compile from the package"
printf '[[window]]\nname = "volume"\naddress = 0xA0000550\nsize = 2\n' > winbad.toml
! python3 "$P/tools/descrec.py" compile winbad.toml winbad.bin >/dev/null 2>&1 || fail "a window over the base's master volume compiles from the package"
# TOML A compiles from the package alone, with the package's maps (api.md 6.14), and a block whose map no file has is refused
[ -f "$P/maps/c64cart.map.toml" ] || fail "the package lacks maps/c64cart.map.toml"
printf '[io]\niobase = 0x04000000\nu2p_io_base = 0xA0000000\n[identity]\nid = "TSTA"\nblock = "ident"\n[[block]]\nname = "ident"\nmodule = "trx_io"\nowner = "base"\naddress = 0xA0000800\nsize = 0x100\n[[block.property]]\nname = "id"\noffset = 0\nwidth = 32\ndir = "r"\n[[block.property]]\nname = "build"\noffset = 4\nwidth = 32\ndir = "r"\n[[block]]\nname = "cart"\nmodule = "trx_io"\nowner = "base"\naddress = 0xA0004000\nsize = 0x100\nmap = "c64cart"\n' > toma.toml
python3 "$P/tools/descrec.py" compile toma.toml toma.bin --maps "$P/maps" >/dev/null 2>&1 || fail "a TOML A record does not compile from the package"
! python3 "$P/tools/descrec.py" compile toma.toml tomabad.bin >/dev/null 2>&1 || fail "a TOML A block with a map compiles without the map files"
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
  "${CLEAN[@]}" make test CXX_HOST="${CXX_HOST:-c++}" >"$W/test.log" 2>&1 && tst="$(tail -1 "$W/test.log")" || { cat "$W/test.log" >&2; fail "the host test"; }
fi
echo "sdk_check: ok: trx64-sdk-$ver: base API $maj.$min (VERSION, README, image/version.toml, include/libfmt.h agree), headers ${hv:0:12}: example.u2a $(wc -c < example.u2a | tr -d " ") bytes, menu record $(wc -c < example.menu.bin | tr -d " ") bytes, $nd dependency files with $dep_hdrs package headers, none names $ROOT; $tst"
