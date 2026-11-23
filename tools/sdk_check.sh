#!/usr/bin/env bash
# T239.3 D17 / S18, D31 / D32 / P3-2: build the example of an SDK package with nothing but the package.
#
#   tools/sdk_check.sh <trx64-sdk-x.y.z.tar.gz>      RV_PREFIX (default riscv32-unknown-elf-) names the compiler
#
# The package is unpacked into a fresh temporary directory outside the repository; the example library and its menu record are built there, then the example core's app: its own TOML A (with a block of the core's) and
# its own TOML B (a shared library, no compiled-in descriptor) are turned into the driver table, the library table, the I/O fragment and the sizes, the sources that depend on the core are compiled with those sizes, the base archives are
# linked, and the image is packed with the description and the library that is the core's code (example.u2a) into a .cor. The build fails when any dependency file or a linked image names a path of the repository this script belongs to,
# or when a maximum of the TOML B is not in the image. One result line at the end.
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
for f in VERSION README.md api.md LICENSE NOTICE include/libapi.h include/libfmt.h mk/lib.mk mk/app.mk mk/lib.ld mk/appflags.mk mk/appbuild.mk tools/u2a_pack.py tools/descrec.py tools/cor_pack.py \
         tools/app_build.py tools/libtab.py tools/rtl_check.py lib/libtrxbase.a lib/liblwip.a lib/liblvgl.a lib/crt0.o lib/port_asm.o base/SIZED.txt base/incdirs.txt base/linker.x base/version.toml \
         libs/shared.toml gateware/shared.toml gateware/host.toml bin/menusim menusim/tools/menusim/run.sh menusim/tools/menusim/menusim.cc example/Makefile example/app/desc.toml example/app/toml_b.toml example/gateware/example.toml example/gateware/example_ctl.map.toml; do
  [ -f "$P/$f" ] || fail "the package lacks $f"
done
want="$(sed -n 's/^base_archive_sha256 //p' "$P/VERSION")"
have="$( (sha256sum "$P/lib/libtrxbase.a" 2>/dev/null || shasum -a 256 "$P/lib/libtrxbase.a") | cut -d' ' -f1)"
[ "$want" = "$have" ] || fail "lib/libtrxbase.a does not match VERSION"
# D31: the package carries no app image
[ ! -e "$P/image" ] && [ -z "$(find "$P" \( -name 'coreapp*.bin' -o -name 'coreapp*.app' -o -name 'coreapp*.elf' \) | head -1)" ] || fail "the package carries an app image (D31: a core's app is linked from it)"

# S5 (T239.3): the base API of the package is one number in the four places that name it, and the hash VERSION names is the hash of include/
maj="$(sed -n 's/^#define U2A_ABI_MAJOR \([0-9]*\).*/\1/p' "$P/include/libfmt.h")"
min="$(sed -n 's/^#define U2A_BASE_MINOR \([0-9]*\).*/\1/p' "$P/include/libfmt.h")"
[ -n "$maj" ] && [ -n "$min" ] || fail "include/libfmt.h has no base API version"
[ "$(sed -n 's/^base_api //p' "$P/VERSION")" = "$maj.$min" ] || fail "VERSION names base_api $(sed -n 's/^base_api //p' "$P/VERSION"), include/libfmt.h is $maj.$min"
[ "$(sed -n 's/^base_api *= *"\([^"]*\)".*/\1/p' "$P/base/version.toml")" = "$maj.$min" ] || fail "base/version.toml names another base_api than include/libfmt.h ($maj.$min)"
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
# a [[window]] compiles from the package alone (descrec.py reads gateware/host.toml beside itself)
printf '[[window]]\nname = "mixer"\naddress = 0xA0000500\nsize = 0x14\n' > win.toml
python3 "$P/tools/descrec.py" compile win.toml win.bin >/dev/null 2>&1 || fail "a record with a window does not compile from the package"
# TOML A compiles from the package alone, with the package's maps (api.md 6.14)
[ -f "$P/maps/c64cart.map.toml" ] || fail "the package lacks maps/c64cart.map.toml"
printf '[io]\niobase = 0x04000000\nu2p_io_base = 0xA0000000\n[identity]\nid = "TSTA"\nblock = "ident"\n[[block]]\nname = "ident"\nmodule = "trx_io"\nowner = "base"\naddress = 0xA0000800\nsize = 0x100\n[[block.property]]\nname = "id"\noffset = 0\nwidth = 32\ndir = "r"\n[[block.property]]\nname = "build"\noffset = 4\nwidth = 32\ndir = "r"\n[[block]]\nname = "cart"\nmodule = "trx_io"\nowner = "base"\naddress = 0xA0004000\nsize = 0x100\nmap = "c64cart"\n' > toma.toml
python3 "$P/tools/descrec.py" compile toma.toml toma.bin --maps "$P/maps" >/dev/null 2>&1 || fail "a TOML A record does not compile from the package"
# the driver table of that TOML A (api.md 6.17): a block has its macro, and the ICAP block the file lacks has none
python3 "$P/tools/descrec.py" drivers toma.toml drv --maps "$P/maps" >/dev/null 2>&1 || fail "the driver table of a TOML A is not generated from the package"
grep -q -F '#define DRV_cart_fl_enable_WR(v)' drv/drvtab.h && [ -f drv/drvtab.c ] || fail "the driver table lacks the register macro of a block"
grep -q -F '#define DRV_cart_desc_COUNT 43u' drv/drvtab.h && grep -q -F '#define DRV_cart_desc_WR(i, v)' drv/drvtab.h || fail "the driver table lacks the count and the indexed access of a run of registers"
! grep -q -F 'DRV_HAS_icap' drv/drvtab.h || fail "the driver table names a block the TOML A does not have"
# the sizes of the example's TOML B are written as the macros of the headers, and its record fits them (the [sizes] of the TOML B are the compiler's too: --sizes)
mkdir -p appcheck
python3 "$P/tools/app_build.py" sizes app/toml_b.toml appcheck --inc "$P/base/inc/coreapp/src" || fail "the sizes of the example's TOML B were not generated from the package"
grep -q -F '#define OPS_MAX 200' appcheck/appsizes.h && grep -q -F '#define CORE_REPORT_MAX 16384' appcheck/appsizes.h || fail "appsizes.h does not carry the example's sizes"
python3 "$P/tools/descrec.py" compile app/desc.toml appcheck/desc.bin --sizes app/toml_b.toml >/dev/null || fail "the example's description does not compile from the package"
# the example core's own TOML A (S17): the base's blocks and the block of the core, whose map is beside the file; the driver table has the macros of the core's block
python3 "$P/tools/descrec.py" compile gateware/example.toml appcheck/toma_example.bin --maps "$P/maps" --maps gateware >/dev/null || fail "the example's TOML A does not compile from the package"
# the example's base blocks are the shared TOML A's blocks, so the two cannot drift apart
python3 -I - "$P/tools" gateware/example.toml "$P/gateware/shared.toml" <<'PY' || fail "the base blocks of the example's TOML A differ from the package's gateware/shared.toml"
import sys
sys.path.insert(0, sys.argv[1])
import descrec as D
ex = D.toml_loads(open(sys.argv[2]).read())
sh = D.toml_loads(open(sys.argv[3]).read())
sys.exit(0 if [b for b in ex["block"] if b["owner"] == "base"] == sh["block"] and ex["io"] == sh["io"] else 1)
PY
python3 "$P/tools/descrec.py" drivers gateware/example.toml appcheck/exdrv --maps "$P/maps" --maps gateware >/dev/null || fail "the driver table of the example's TOML A is not generated from the package"
grep -q -F '#define DRV_example_ctl_control_WR(v)' appcheck/exdrv/drvtab.h && grep -q -F '#define DRV_example_ctl_OWNER 1' appcheck/exdrv/drvtab.h || fail "the driver table lacks the register macro or the owner of the example's own block"
# the example's app: everything but the compiler runs always; with the compiler the app is linked from the package alone
python3 "$P/tools/app_build.py" build --sdk "$P" --toml-a gateware/example.toml --maps gateware --toml-b app/toml_b.toml --out appcheck/gen --src app --record appcheck/desc.bin --dry-run >/dev/null || fail "app_build.py build --dry-run"
[ -z "$(sed -n 's/^CORE_DESC := *//p' appcheck/gen/build.mk)" ] || fail "the example's app names a compiled-in descriptor: its core is described by its record"
grep -q -F 'cbm_prg.cc' appcheck/gen/build.mk || fail "the library of the example's TOML B is not in its build"
[ -s appcheck/gen/drv/basetab.h ] && grep -q -F 'BASE_BLOCK(ident,' appcheck/gen/drv/basetab.h && [ -s appcheck/gen/drv/drvtab.h ] && [ -s appcheck/gen/lib/libtab_table.cc ] && [ -s appcheck/gen/io.ld ] && [ -s appcheck/gen/appsizes.h ] || fail "the generated files of the example's app are missing"
app_res="app not linked (no compiler)"
if command -v "${RV}gcc" >/dev/null 2>&1; then
  "${CLEAN[@]}" make app RV_PREFIX="$RV" >"$W/app.log" 2>&1 || { tail -40 "$W/app.log" >&2; fail "make app of the example"; }
  [ -s appout/app.bin ] && [ -s appout/app.elf ] || fail "appout/app.bin missing"
  if grep -l -F "$ROOT" appout/obj/*.d "$W/app.log" 2>/dev/null; then fail "the app's build read a path of the repository $ROOT"; fi
  if grep -a -q -F "$ROOT" appout/app.elf; then fail "the app's image names a path of the repository"; fi
  # the maxima of the TOML B are in the image: the report buffer of diag.cc is a static array of CORE_REPORT_MAX bytes
  rep_sz="$("${RV}nm" -S -t d appout/obj/diag.o | awk '$4 == "g_rep" { print $2 + 0 }')"
  [ "$rep_sz" = "16384" ] || fail "the report buffer of the example's app is ${rep_sz:-missing} bytes, its TOML B says report_bytes = 16384"
  # and packs: the image, the description and the library that is the core's code (hooks = "example": cor_pack refuses a bundle whose library does not export corehooks=1) in a .cor (a dummy partial: the packer's own checks are what is shown here, not a bitstream)
  head -c 4096 /dev/zero > appcheck/partial.bin
  python3 "$P/tools/cor_pack.py" pack -o appcheck/example.cor --name Example --version 1 --partial appcheck/partial.bin --app appout/app.bin --host-key 00000000 --desc app/desc.toml --lib example.u2a --sizes app/toml_b.toml >/dev/null \
    || fail "the example's app does not pack into a .cor"
  python3 "$P/tools/cor_pack.py" check appcheck/example.cor >/dev/null || fail "the packed .cor of the example does not check"
  app_res="app.bin $(wc -c < appout/app.bin | tr -d " ") bytes, report buffer $rep_sz bytes, packed into a .cor"
fi
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
echo "sdk_check: ok: trx64-sdk-$ver: base API $maj.$min (VERSION, README, image/version.toml, include/libfmt.h agree), headers ${hv:0:12}: example.u2a $(wc -c < example.u2a | tr -d " ") bytes, menu record $(wc -c < example.menu.bin | tr -d " ") bytes, $nd dependency files with $dep_hdrs package headers, none names $ROOT; $app_res; $tst"
