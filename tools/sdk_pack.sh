#!/usr/bin/env bash
# T239.3 D17 / P8, D31 / D32 / P3-2: the SDK package for a developer outside this repository.
#
#   tools/sdk_pack.sh <out dir> <base dir>      makes <out dir>/trx64-sdk-<version>/ and <out dir>/trx64-sdk-<version>.tar.gz
#
# <version> is the SDK SemVer, kept in one place: app/sdkpkg/SDK_VERSION. <base dir> is what the core app build leaves with `make sdkbase` (host-app/target/u64ii/riscv/coreapp/output/sdkbase,
# ci/trxapps.sh): the archive of the base, the libraries, the start-up objects, the linker script and the lists of what depends on the core. The package carries no app image (D31): a core's
# app is linked from it (tools/app_build.py build), with the maxima its TOML B names. What goes in, and from where:
#   include/  every header a library or an application includes: the closure of libapi.h, libfmt.h, c64_api.h, glue.h, u2p_app.h, u2p_ui.h and hidden.h,
#             from coreapp/src and app/sdk (tools/sdk_api.py), flat
#   mk/       app/lib/{lib.mk,app.mk,sdkroot.mk,lib.ld}: the same files the tree builds with; sdkroot.mk finds the package's layout; appflags.mk and appbuild.mk link a core's app
#   sdk/      the C sources app.mk links into an application (libc.c, u2p_compat.c; lib.mk links libc.c on request)
#   tools/    u2a_pack.py, descrec.py, cor_pack.py, rtl_check.py, app_build.py, libtab.py (the modules they import are Python's own, or descrec.py; cor_pack.py needs Pillow for a PNG --icon only)
#   lib/      libtrxbase.a (the objects of the sources that depend on nothing a core decides), liblwip.a, liblvgl.a, crt0.o, port_asm.o
#   base/     the sources that depend on the core (compiled for each core, with its [sizes]) and the sources of the shared libraries, at their repository paths under src/, the headers
#             they reach under inc/, SIZED.txt, incdirs.txt, linker.x (tools/app_build.py pack-base)
#   libs/     shared.toml: the TOML B of the shared app, the catalog of the shared libraries (their sources are in base/)
#   gateware/ shared.toml: the TOML A of the shared app (the reference a core's TOML A starts from)
#   menusim/  the menu simulator with the tree it builds from (tools/menusim/run.sh inside), and bin/menusim, which runs it
#   example/  app/sdkpkg/example: a library of two files with a menu TOML and a host test, and the TOML A, TOML B and description of an app
#   licenses/ the licence texts of the third-party code the base links (FreeRTOS, lwIP, LVGL, FatFs), copied from vendor/gideon/software
#   api.md    app/sdkpkg/api.md: the API document; tools/check_api_doc.py (lint) keeps it complete against the headers
# The package carries no ROM: tools/check_no_roms.sh runs over the finished directory and the pack fails if it finds one.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
out="${1:?usage: tools/sdk_pack.sh <out dir> <base dir>}"
bdir="${2:?usage: tools/sdk_pack.sh <out dir> <base dir>}"
[ -f "$bdir/lib/libtrxbase.a" ] || { echo "sdk_pack: no libtrxbase.a in $bdir/lib: run make sdkbase in the core app build first" >&2; exit 1; }
bdir="$(cd "$bdir" && pwd)"
mkdir -p "$out"; out="$(cd "$out" && pwd)"

ver="$(tr -d ' \n' < "$ROOT/app/sdkpkg/SDK_VERSION")"
[[ "$ver" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]] || { echo "sdk_pack: app/sdkpkg/SDK_VERSION is not x.y.z: $ver" >&2; exit 1; }
abi_major="$(sed -n 's/^#define U2A_ABI_MAJOR \([0-9]*\).*/\1/p' "$ROOT/coreapp/src/libfmt.h")"
abi_minor="$(sed -n 's/^#define U2A_BASE_MINOR \([0-9]*\).*/\1/p' "$ROOT/coreapp/src/libfmt.h")"
[ -n "$abi_major" ] && [ -n "$abi_minor" ] || { echo "sdk_pack: the base version is not in coreapp/src/libfmt.h" >&2; exit 1; }
commit="${SDK_COMMIT:-$( (cd "$ROOT" && git rev-parse --short=8 HEAD 2>/dev/null) || echo unknown)}"
name="trx64-sdk-$ver"
P="$out/$name"
rm -rf "$P" "$out/$name.tar.gz"
mkdir -p "$P/include" "$P/mk" "$P/sdk" "$P/tools" "$P/licenses" "$P/libs" "$P/gateware"

# the options of the package's build are the tree's: every -D and --wrap of the core app Makefile is in mk/appflags.mk
python3 -I "$ROOT/tools/app_build.py" flags "$ROOT/host-app/target/u64ii/riscv/coreapp/Makefile" "$ROOT/app/lib/appflags.mk" || { echo "sdk_pack: app/lib/appflags.mk and the core app Makefile do not agree" >&2; exit 1; }

# the headers: tools/sdk_api.py names them (the include closure of the public roots), the same list the lock of the base API hashes (coreapp/api_lock.toml)
t="$(mktemp -d)"; trap 'rm -rf "$t"' EXIT
python3 -I "$ROOT/tools/sdk_api.py" headers > "$t/headers" || { echo "sdk_pack: the list of public headers failed" >&2; exit 1; }
[ "$(wc -l < "$t/headers")" -ge 6 ] || { echo "sdk_pack: the include closure is too small:" >&2; cat "$t/headers" >&2; exit 1; }
python3 -I "$ROOT/tools/sdk_api.py" check > "$t/api.txt" || { cat "$t/api.txt" >&2; echo "sdk_pack: the base API, its lock and the headers do not agree" >&2; exit 1; }
api_hash="$(python3 -I "$ROOT/tools/sdk_api.py" hash)"
while read -r h; do
  case "$h" in /*) f="$h" ;; *) f="$ROOT/$h" ;; esac
  [ ! -e "$P/include/$(basename "$f")" ] || { echo "sdk_pack: two headers named $(basename "$f")" >&2; exit 1; }
  cp "$f" "$P/include/"
done < "$t/headers"

for f in lib.mk app.mk sdkroot.mk lib.ld appflags.mk appbuild.mk; do cp "$ROOT/app/lib/$f" "$P/mk/"; done
cp "$ROOT/app/sdk/libc.c" "$ROOT/app/sdk/u2p_compat.c" "$P/sdk/"
for f in u2a_pack.py descrec.py cor_pack.py rtl_check.py app_build.py libtab.py; do cp "$ROOT/tools/$f" "$P/tools/"; done
mkdir -p "$P/maps" && cp "$ROOT"/app/sdkpkg/maps/*.map.toml "$P/maps/"       # the module maps of the shared drivers (TOML A, api.md 6.14.11)
cp "$ROOT/coreapp/libs/shared.toml" "$P/libs/shared.toml"
cp "$ROOT/coreapp/gateware/shared.toml" "$P/gateware/shared.toml"
cp "$ROOT/coreapp/version.toml" "$P/base-version.toml"
# the base: the archives and the sources that depend on the core (what the core app build listed), the shared libraries' sources, the headers they reach
python3 -I "$ROOT/tools/app_build.py" pack-base "$bdir" "$P" "$ROOT/coreapp/libs/shared.toml" | tee "$t/base.txt"
mv "$P/base-version.toml" "$P/base/version.toml"
cp -r "$ROOT/app/sdkpkg/example" "$P/example"
find "$P/example" \( -name '*.o' -o -name '*.d' -o -name '*.elf' -o -name '*.u2a' -o -name '*.bin' -o -name test_example -o -name obj -o -name appout \) -prune -exec rm -rf {} + 2>/dev/null || true

# the menu simulator (D32) with the tree it builds from: tools/menusim/run.sh builds the application's sources natively against the stubs of the render bench
M="$P/menusim"
mkdir -p "$M"
{
  ( cd "$ROOT" && for d in tools/menusim coreapp app/loaders app/lib/cbm1541 app/edit app/view app/video app/sdk uicore host-app/software/hostapp host-app/software/io/c64 \
        host-app/software/lvgl_port host-app/software/filesystem vendor/gideon/software/appsdk tests/coreapp; do
      find "$d" -type f \
        -not -name 'test_*' -not -path '*/golden/*' -not -path '*/render/*' -not -path '*/output/*' -not -path '*/.ci/*' -not -name '.DS_Store' -not -name '*.o' -not -name '*.d'
    done
    echo host-app/target/u64ii/riscv/coreapp/Makefile
    for f in tools/gen_base_ui.py tools/descrec.py tools/libtab.py tools/check_d24.py tools/d24_ledger.txt tests/lvgl_objs.sh tests/overlay_probe.h tests/drv_fake.h tests/parallel.sh; do [ -f "$f" ] && echo "$f"; done
    find vendor/gideon/software/lvgl -type f -not -path '*/demos/*' -not -path '*/examples/*' -not -path '*/docs/*' -not -path '*/tests/*' -not -path '*/scripts/*' -not -path '*/env_support/*' -not -name '.DS_Store'
  ) | LC_ALL=C sort -u > "$t/menusim.list"
}
( cd "$ROOT" && COPYFILE_DISABLE=1 tar -cf - -T "$t/menusim.list" ) | ( cd "$M" && tar -xf - )
mkdir -p "$P/bin"
cat > "$P/bin/menusim" <<'MENUSIM'
#!/usr/bin/env bash
# The menu simulator of this SDK package (T239.3 D32): the menu of a core on this machine, in a window, from the core's TOML.
#   bin/menusim --desc <core description.toml> [--mode M] [--ops FILE] [--keys K1,K2 --shot out.png]     a core that is in no table: its description is its TOML
#   bin/menusim <core> ...                                                                              a core of the shared app's table
# Needs a C++ compiler, python3 and SDL2 (tools/menusim/run.sh in the package says how it is built).
exec "$(cd "$(dirname "$0")/.." && pwd)/menusim/tools/menusim/run.sh" "$@"
MENUSIM
chmod +x "$P/bin/menusim"

cp "$ROOT/LICENSE" "$P/LICENSE"
V="$ROOT/vendor/gideon/software"
cp "$V/FreeRTOS/License/license.txt" "$P/licenses/FreeRTOS.txt"
cp "$V/lwip/COPYING" "$P/licenses/lwIP.txt"
cp "$V/lvgl/LICENCE.txt" "$P/licenses/LVGL.txt"
cp "$V/chan_fat/ff14/LICENSE.txt" "$P/licenses/FatFs.txt"
cp "$ROOT/app/sdkpkg/api.md" "$P/api.md"
sed -e "s/@SDK_VERSION@/$ver/g" -e "s/@ABI_MAJOR@/$abi_major/g" -e "s/@ABI_MINOR@/$abi_minor/g" -e "s/@API_HASH@/${api_hash:0:12}/g" "$ROOT/app/sdkpkg/readme.in" > "$P/README.md"
cat > "$P/NOTICE" <<NOTICE
TRX64 SDK $ver, source commit $commit.

Every file of this package is under the GNU General Public License, version 3 or later (LICENSE), except where a file's own header says
otherwise, and except the third-party code that lib/ and base/ link or carry: FreeRTOS, lwIP, LVGL and FatFs, each under its own licence
(licenses/). The service tables (include/svctab.h), the application interface (include/u2p_app.h) and the C runtime (sdk/libc.c,
sdk/u2p_compat.c) follow the names and layout of the TRX64 line's application interface, GPL-3.0; the headers say so.
base/ and menusim/ carry sources and headers of the firmware tree vendor/gideon/ of the TRX64U-MultiCore repository, under the licences their files name.
Some headers name PROVENANCE.md: that is a file of the TRX64U-MultiCore source repository, which this package does not carry.
The package carries no ROM image.
NOTICE
sha() { (sha256sum "$1" 2>/dev/null || shasum -a 256 "$1") | cut -d' ' -f1; }
cat > "$P/VERSION" <<VERSION
sdk $ver
base_api $abi_major.$abi_minor
abi_major $abi_major
api_headers_sha256 $api_hash
commit $commit
base_archive_sha256 $(sha "$P/lib/libtrxbase.a")
VERSION

# the ROM check of the repository over the finished package
"$ROOT/tools/check_no_roms.sh" "$P" > "$t/roms.txt" 2>&1 && romres="ROM check: clean ($(tail -1 "$t/roms.txt"))" || { cat "$t/roms.txt" >&2; echo "sdk_pack: the ROM check found something in $P" >&2; exit 1; }

# the tarball: files in byte order, mtime 0, owner 0, no gzip timestamp or name, so the same tree packs to the same bytes (GNU tar and bsdtar)
TZ=UTC find "$P" -exec touch -h -t 197001010000 {} +
if tar --version 2>/dev/null | grep -q 'GNU tar'; then own=(--owner=0 --group=0); else own=(--uid 0 --gid 0); fi
( cd "$out" && find "$name" | LC_ALL=C sort > "$t/list" \
  && COPYFILE_DISABLE=1 tar "${own[@]}" --numeric-owner --no-recursion -cf - -T "$t/list" | gzip -n -9 > "$name.tar.gz" )
echo "=== sdk_pack: $name ($(wc -c < "$out/$name.tar.gz") bytes), base $abi_major.$abi_minor, commit $commit; $romres"
du -sk "$P"/* | sed 's|'"$P"'/||' | awk '{printf "    %-10s %s KiB\n", $2, $1}'
