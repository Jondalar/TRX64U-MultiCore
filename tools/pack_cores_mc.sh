#!/usr/bin/env bash
# Pack one .cor per core out of a CI package, against the host that package built (T94 P5).
#
#   tools/pack_cores_mc.sh --pkg <release tag> --app-dir <dir> [--out <dir>] [--core <id>]...
#   tools/pack_cores_mc.sh --bits <dir>           --app-dir <dir> [--out <dir>] [--core <id>]...
#
# --pkg downloads what it needs out of the CI pre-release <release tag> (<branch>-<8 hex>) of the public builds repository, through
# tools/builds.sh and without a token (c64_basic.bit and every <core>_partial.bit); --bits takes the same files from a directory.
# --app-dir holds one <core>.app per core - the application that rides in the bundle, loaded at
# 0x30000. A core without an application there is skipped and named; the core applications are
# built outside this repository until T93 lands.
#
# Name, version and the set of cores come from the descriptors (cores/<id>/core.toml, T94 D1).
# The host key is the AXSS word of the package's own c64_basic.bit (T84 D5b), so every .cor names
# the host it was routed against and the boot app refuses it on any other one. The partials of one
# package all belong to that host: a .cor is never mixed across packages.
#
# The core's logo (T114) is packed in as the bundle's icon when cores/<id>/logo/icon.rgb565 exists, its data description (cores/<id>/app/desc.toml, T239.3 D17) as record 6 when its core.toml says desc_record = true (D25).
#
# Output: <out>/<core>.cor plus one line per core. Feed them to tools/make_release_mc.sh --cor.
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/.." && pwd)"
PKG="" BITS="" APPDIR="" OUT="" ONLY=()

usage() { sed -n '2,20p' "$0"; exit 2; }
while [ $# -gt 0 ]; do
    case "$1" in
        --pkg) PKG="$2"; shift 2;;
        --bits) BITS="$2"; shift 2;;
        --app-dir) APPDIR="$2"; shift 2;;
        --out) OUT="$2"; shift 2;;
        --core) ONLY+=("$2"); shift 2;;
        -h|--help) usage;;
        *) echo "unknown option $1" >&2; usage;;
    esac
done
[ -n "$APPDIR" ] || usage
[ -n "$PKG" ] || [ -n "$BITS" ] || usage
[ -z "$PKG" ] || [ -z "$BITS" ] || { echo "--pkg and --bits are exclusive" >&2; exit 2; }
[ -d "$APPDIR" ] || { echo "$APPDIR: no such directory" >&2; exit 1; }
OUT="${OUT:-$ROOT/dist/cor-${PKG:-local}}"
mkdir -p "$OUT"

# bash 3.2 on the Mac has no mapfile and no nice way to test an empty array under set -u
set +u
CORES=("${ONLY[@]}")
set -u
if [ "${#CORES[@]}" -eq 0 ] || [ -z "${CORES[0]}" ]; then
    CORES=()
    for c in $(python3 "$HERE/core_toml.py" list-cor); do CORES+=("$c"); done
fi

# the files: from the package or from a directory
if [ -n "$PKG" ]; then
    BITS="$OUT/.bits"; mkdir -p "$BITS"
    for f in c64_basic.bit $(for c in "${CORES[@]}"; do echo "${c}_partial.bit"; done); do
        [ -s "$BITS/$f" ] && continue
        "$HERE/builds.sh" fetch "$PKG" "$f" "$BITS/$f" ||
            { echo "    $f: not in package $PKG"; rm -f "$BITS/$f"; }
    done
fi
[ -s "$BITS/c64_basic.bit" ] || { echo "$BITS/c64_basic.bit: missing" >&2; exit 1; }

HK="$(python3 "$HERE/cor_pack.py" hostkey "$BITS/c64_basic.bit")"
CV="$(cat "$ROOT/contract/VERSION")"
echo ">>> host key $HK, contract $CV, from ${PKG:-$BITS}"

n=0
for c in "${CORES[@]}"; do
    part="$BITS/${c}_partial.bit"
    app="$APPDIR/$c.app"
    if [ ! -s "$part" ]; then echo "    $c: no partial in this package, skipped"; continue; fi
    if [ ! -s "$app" ]; then echo "    $c: no $c.app in $APPDIR, skipped"; continue; fi
    name="$(python3 "$HERE/core_toml.py" get "$c" name)"
    ver="$("$ROOT/ci/version.sh" with "$(python3 "$HERE/core_toml.py" get "$c" version)")"   # T221 D3: x.y.z-dev+<sha8> on a branch build, x.y.z on a release
    # T114: the core's logo (cores/<id>/logo/icon.rgb565) rides in the bundle as record 4; a core
    # without one is packed without an icon and the boot menu shows its text tile.
    icon=()
    if [ -s "$ROOT/cores/$c/logo/icon.rgb565" ]; then
        icon=(--icon "$ROOT/cores/$c/logo/icon.rgb565")
    else
        echo "    $c: no cores/$c/logo/icon.rgb565, packed without an icon"
    fi
    files=()                                      # record 8: the files the core installs (core.toml install_dir / install_old)
    idir="$(python3 "$HERE/core_toml.py" get "$c" install_dir)"; iold="$(python3 "$HERE/core_toml.py" get "$c" install_old)"
    [ -z "$idir" ] || files+=(--files "$ROOT/cores/$c/$idir")
    [ -z "$iold" ] || files+=(--files-old "$ROOT/cores/$c/$iold")
    desc=()                                       # record 6: the core's data description, compiled by tools/descrec.py (T239.3 D17), only for a core whose core.toml says desc_record = true (D25); any other core carries no record and runs on its compiled-in descriptor
    [ "$(python3 "$HERE/core_toml.py" get "$c" desc_record)" != true ] || [ ! -s "$ROOT/cores/$c/app/desc.toml" ] || desc=(--desc "$ROOT/cores/$c/app/desc.toml")
    heap=(); h="$(python3 "$HERE/core_toml.py" get "$c" heap)"; [ -z "$h" ] || heap=(--heap "$h")   # T239.3 D15: the core's heap, set in its copy of the app
    python3 "$HERE/cor_pack.py" pack ${heap[@]+"${heap[@]}"} --host-key "$HK" --contract "$CV" -o "$OUT/$c.cor" ${files[@]+"${files[@]}"} ${desc[@]+"${desc[@]}"} \
        --name "$name" --version "$ver" --partial "$part" --app "$app" \
        --load 0x30000 --entry 0x30000 ${icon[@]+"${icon[@]}"} --short "$(python3 "$HERE/core_toml.py" get "$c" short)" > /dev/null
    python3 "$HERE/cor_pack.py" check --contract "$CV" --host-key "$HK" "$OUT/$c.cor" > /dev/null
    printf '    %-9s %-28s %8d bytes  %s\n' "$c" "$name" "$(wc -c < "$OUT/$c.cor")" "$ver"
    n=$((n + 1))
done
echo ">>> $n .cor in $OUT"
