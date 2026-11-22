#!/usr/bin/env bash
# T239.3 D17 / P8: the SDK package for a developer outside this repository.
#
#   tools/sdk_pack.sh <out dir> [<coreapp.bin>]      makes <out dir>/trx64-sdk-<version>/ and <out dir>/trx64-sdk-<version>.tar.gz
#
# <version> is the SDK SemVer, kept in one place: app/sdkpkg/SDK_VERSION. The app core image defaults to <out dir>/coreapp.bin (the
# stage trxapps puts it there). What goes in, and from where:
#   include/  every header a library or an application includes: the closure of libapi.h, libfmt.h, glue.h, u2p_app.h, u2p_ui.h and hidden.h,
#             computed by the compiler (-MM) from coreapp/src and app/sdk, flat
#   mk/       app/lib/{lib.mk,app.mk,sdkroot.mk,lib.ld}: the same files the tree builds with; sdkroot.mk finds the package's layout
#   sdk/      the C sources app.mk links into an application (libc.c, u2p_compat.c; lib.mk links libc.c on request)
#   tools/    u2a_pack.py, descrec.py, cor_pack.py (the modules they import are Python's own, or descrec.py; cor_pack.py needs Pillow for a PNG --icon only)
#   image/    coreapp.bin, the app core image of this build, and its version.toml
#   example/  app/sdkpkg/example: a library of two files with a menu TOML and a host test
#   licenses/ the licence texts of the third-party code coreapp.bin links (FreeRTOS, lwIP, LVGL, FatFs), copied from vendor/gideon/software
#   api.md    app/sdkpkg/api.md: the API document; tools/check_api_doc.py (lint) keeps it complete against the headers
# The package carries no ROM: tools/check_no_roms.sh runs over the finished directory and the pack fails if it finds one.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
out="${1:?usage: tools/sdk_pack.sh <out dir> [<coreapp.bin>]}"
bin="${2:-$out/coreapp.bin}"
[ -f "$bin" ] || { echo "sdk_pack: no app core image at $bin" >&2; exit 1; }
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
mkdir -p "$P/include" "$P/mk" "$P/sdk" "$P/tools" "$P/image" "$P/licenses"

# the headers: the compiler names them
CXX=""
for c in riscv32-unknown-elf-g++ riscv64-elf-g++ c++ g++; do command -v "$c" >/dev/null 2>&1 && { CXX="$c"; break; }; done
[ -n "$CXX" ] || { echo "sdk_pack: no C++ compiler for the include closure" >&2; exit 1; }
t="$(mktemp -d)"; trap 'rm -rf "$t"' EXIT
printf '#include "libapi.h"\n#include "libfmt.h"\n#include "glue.h"\n#include "u2p_app.h"\n#include "u2p_ui.h"\n#include "hidden.h"\n' > "$t/public.cc"
"$CXX" -x c++ -MM -I"$ROOT/coreapp/src" -I"$ROOT/app/sdk" "$t/public.cc" | tr -d '\\' | tr ' ' '\n' \
  | grep -E "^($ROOT/)?(coreapp/src|app/sdk)/[^/]+\.h$" | sort -u > "$t/headers"
[ "$(wc -l < "$t/headers")" -ge 6 ] || { echo "sdk_pack: the include closure is too small:" >&2; cat "$t/headers" >&2; exit 1; }
while read -r h; do
  case "$h" in /*) f="$h" ;; *) f="$ROOT/$h" ;; esac
  [ ! -e "$P/include/$(basename "$f")" ] || { echo "sdk_pack: two headers named $(basename "$f")" >&2; exit 1; }
  cp "$f" "$P/include/"
done < "$t/headers"

for f in lib.mk app.mk sdkroot.mk lib.ld; do cp "$ROOT/app/lib/$f" "$P/mk/"; done
cp "$ROOT/app/sdk/libc.c" "$ROOT/app/sdk/u2p_compat.c" "$P/sdk/"
for f in u2a_pack.py descrec.py cor_pack.py; do cp "$ROOT/tools/$f" "$P/tools/"; done
cp "$bin" "$P/image/coreapp.bin"
cp "$ROOT/coreapp/version.toml" "$P/image/version.toml"
cp -r "$ROOT/app/sdkpkg/example" "$P/example"
find "$P/example" \( -name '*.o' -o -name '*.d' -o -name '*.elf' -o -name '*.u2a' -o -name '*.bin' -o -name test_example \) -delete
cp "$ROOT/LICENSE" "$P/LICENSE"
V="$ROOT/vendor/gideon/software"
cp "$V/FreeRTOS/License/license.txt" "$P/licenses/FreeRTOS.txt"
cp "$V/lwip/COPYING" "$P/licenses/lwIP.txt"
cp "$V/lvgl/LICENCE.txt" "$P/licenses/LVGL.txt"
cp "$V/chan_fat/ff14/LICENSE.txt" "$P/licenses/FatFs.txt"
cp "$ROOT/app/sdkpkg/api.md" "$P/api.md"
sed -e "s/@SDK_VERSION@/$ver/g" -e "s/@ABI_MAJOR@/$abi_major/g" -e "s/@ABI_MINOR@/$abi_minor/g" "$ROOT/app/sdkpkg/readme.in" > "$P/README.md"
cat > "$P/NOTICE" <<NOTICE
TRX64 SDK $ver, source commit $commit.

Every file of this package is under the GNU General Public License, version 3 or later (LICENSE), except where a file's own header says
otherwise, and except the third-party code that image/coreapp.bin links: FreeRTOS, lwIP, LVGL and FatFs, each under its own licence
(licenses/). The service tables (include/svctab.h), the application interface (include/u2p_app.h) and the C runtime (sdk/libc.c,
sdk/u2p_compat.c) follow the names and layout of the TRX64 line's application interface, GPL-3.0; the headers say so.
Some headers name PROVENANCE.md and files under vendor/gideon/: those are files of the TRX64U-MultiCore source repository, which this package
does not carry.
The package carries no ROM image.
NOTICE
cat > "$P/VERSION" <<VERSION
sdk $ver
base_api $abi_major.$abi_minor
abi_major $abi_major
commit $commit
coreapp_sha256 $( (sha256sum "$P/image/coreapp.bin" 2>/dev/null || shasum -a 256 "$P/image/coreapp.bin") | cut -d' ' -f1)
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
