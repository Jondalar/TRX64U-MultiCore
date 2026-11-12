#!/usr/bin/env bash
# Pack the .cfw of one monolithic core (delivery = "cfw" in cores/<id>/core.toml; T113 Q12, T115).
#
#   tools/pack_cfw_mc.sh --core <id> --bit <id>_mono.bit --app-bin <coreapp.bin> --id <package id> \
#       --bit-commit <sha> --app-commit <sha> [--out <dir>] [--native]
#
# The core's full bitstream (ci/bitstream_mono.sh: host and core flat in one design) and the generic core application
# (coreapp.bin of stage trxapps, the raw image linked to 0x30000) go into the updater of the TRX64 Ultimate exactly as
# tools/make_update.sh does it for the TRX64 firmware: same file layout, so the host's .cfw parser (T115) takes it:
# it stages the application at 0x30000 and sends IPROG for the bitstream's copy in the flash drive. What this adds:
#   1. tools/cfw_core.py prepare: the bitstream's design name (field a of the .bit header) becomes the core's id, because
#      Vivado writes `top`, and `top` is the TRX64 firmware's name with the TRX64 logo (cfwicon.cc, T115 D7); the
#      application image becomes the record {0x30000, length, 0x30000} the .cfw carries
#   2. tools/make_update.sh (with --native: already inside the u64-build-env image, CI) builds <id>.cfw/.ue2 and runs
#      tools/trx_pkg_check.py on them (blobs byte-equal to the inputs, CRCs, the boot ROM's sum)
#   3. tools/cfw_core.py check: the .cfw read as cfw.cc cfw_probe reads it; a failure stops here
# Output: <out>/<id>.cfw, and <out>/<id>.icon when cores/<id>/logo/icon.rgb565 (2048 bytes) exists - the sidecar that gives
# the row its logo (a .cfw has no icon part). The .ue2 is NOT kept: it would install the monolith over the host in the flash.
#
# MAKE_UPDATE (environment) replaces tools/make_update.sh: the bench stands a stub in for the container build.
# Needs python3 and, without --native, the `container` CLI and the u64-build-env image (see make_update.sh).
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/.." && pwd)"
CORE="" BIT="" APPBIN="" ID="" BITC="" APPC="" OUT="$ROOT/dist" NATIVE=()

usage() { sed -n '2,20p' "$0"; exit 2; }
while [ $# -gt 0 ]; do
    case "$1" in
        --core) CORE="$2"; shift 2;;
        --bit) BIT="$2"; shift 2;;
        --app-bin) APPBIN="$2"; shift 2;;
        --id) ID="$2"; shift 2;;
        --bit-commit) BITC="$2"; shift 2;;
        --app-commit) APPC="$2"; shift 2;;
        --out) OUT="$2"; shift 2;;
        --native) NATIVE=(--native); shift;;
        -h|--help) usage;;
        *) echo "unknown option $1" >&2; usage;;
    esac
done
[ -n "$CORE" ] && [ -n "$BIT" ] && [ -n "$APPBIN" ] && [ -n "$ID" ] && [ -n "$BITC" ] && [ -n "$APPC" ] || usage
[ "$(python3 "$HERE/core_toml.py" get "$CORE" delivery)" = cfw ] || { echo "$CORE: its core.toml does not say delivery = \"cfw\"" >&2; exit 1; }
[ -s "$BIT" ] && [ -s "$APPBIN" ] || { echo "input file missing or empty" >&2; exit 1; }

WORK="$(mktemp -d "${TMPDIR:-/tmp}/cfw-core.XXXXXX")"
trap 'rm -rf "$WORK"' EXIT
mkdir -p "$OUT"

echo ">>> $CORE: design name and application record"
python3 "$HERE/cfw_core.py" prepare --core "$CORE" --bit "$BIT" --app-bin "$APPBIN" --out-bit "$WORK/$CORE.bit" --out-app "$WORK/$CORE.app"

echo ">>> $CORE: updater"
TRX_UPDATE_NAME="$CORE" "${MAKE_UPDATE:-$HERE/make_update.sh}" ${NATIVE[@]+"${NATIVE[@]}"} --id "$ID" --bit-commit "$BITC" --app-commit "$APPC" \
    --out "$WORK/upd" "$WORK/$CORE.bit" "$WORK/$CORE.app"
CFW="$WORK/upd/trx64-$ID/$CORE.cfw"
[ -s "$CFW" ] || { echo "make_update.sh left no $CORE.cfw" >&2; exit 1; }

echo ">>> $CORE: the way the host reads it (T115)"
python3 "$HERE/cfw_core.py" check --design "$CORE" "$CFW"

cp "$CFW" "$OUT/$CORE.cfw"
ICON="$ROOT/cores/$CORE/logo/icon.rgb565"
rm -f "$OUT/$CORE.icon"
if [ -s "$ICON" ] && [ "$(wc -c < "$ICON" | tr -d ' ')" = 2048 ]; then
    cp "$ICON" "$OUT/$CORE.icon"
else
    echo "    no 2048 byte cores/$CORE/logo/icon.rgb565: the row gets the generic firmware logo (T115 D7)"
fi
ls -l "$OUT/$CORE.cfw"
