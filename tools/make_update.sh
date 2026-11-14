#!/bin/bash
# Build a TRX64 Ultimate tester update package (docs/specs/done/T49-update-packages.md).
#
#   tools/make_update.sh [options] <bitstream.bit> <ultimate.app>
#
# The id, and the two commits it records, come from the CI package's manifest.json
# (--manifest-json) or from --id/--bit-commit/--app-commit. Output: <out>/trx64-<id>/
#   trx64_update_<commit>_<YYYYMMDD>.cfw/.ue2  SHA256SUMS   (+ trxmon.u2a with --u2a)
# <commit> = the first 8 characters of the TRX64 commit (the id's last part), <YYYYMMDD> = the build date.
# --emu-test builds an updater that also accepts ue2emu's S25FL128L flash, into
# <out>/trx64-<id>-EMUTEST/; such a package must never reach a real board.
#
# The updater is compiled in the build container (u64-build-env, RISC-V toolchain
# inside); nothing is installed on the host. Needs `container` (Apple Container CLI) and python3.
# --native: already inside that image (CI, ci/lib.sh build_update_host): run the same make in place, no `container`.
set -euo pipefail
EMUTEST=0

HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/.." && pwd)"
APP_REPO="$ROOT/vendor/gideon"
OUT="$ROOT/dist"
IMAGE="u64-build-env"
ID="" BITC="" APPC="" MJSON="" U2A="" NATIVE=0

usage() { sed -n '2,12p' "$0"; exit 2; }
while [ $# -gt 0 ]; do
    case "$1" in
        --manifest-json) MJSON="$2"; shift 2;;
        --id) ID="$2"; shift 2;;
        --bit-commit) BITC="$2"; shift 2;;
        --app-commit) APPC="$2"; shift 2;;
        --out) OUT="$2"; shift 2;;
        --app-repo) APP_REPO="$2"; shift 2;;
        --u2a) U2A="$2"; shift 2;;
        --emu-test) EMUTEST=1; shift;;
        --native) NATIVE=1; shift;;
        -h|--help) usage;;
        -*) echo "unknown option $1" >&2; usage;;
        *) break;;
    esac
done
[ $# -eq 2 ] || usage
BIT="$(cd "$(dirname "$1")" && pwd)/$(basename "$1")"
APP="$(cd "$(dirname "$2")" && pwd)/$(basename "$2")"
[ -f "$BIT" ] && [ -f "$APP" ] || { echo "input file missing" >&2; exit 1; }

if [ -n "$MJSON" ]; then
    eval "$(python3 - "$MJSON" <<'PY'
import json, sys
m = json.load(open(sys.argv[1]))
print("ID=%r BITC=%r APPC=%r" % ("%s-%s" % (m["branch"], m["trx64_commit"][:8]), m["trx64_commit"][:15], (m.get("app_commit") or m.get("trxapps_commit") or m["trx64_commit"])[:15]))
PY
)"
fi
[ -n "$ID" ] && [ -n "$BITC" ] && [ -n "$APPC" ] || { echo "id and both commits are required (--manifest-json, or --id --bit-commit --app-commit)" >&2; exit 1; }

WORK="$(mktemp -d "${TMPDIR:-/tmp}/trx-update.XXXXXX")"
trap 'rm -rf "$WORK"' EXIT
cp "$BIT" "$WORK/trx_50t.bit"
cp "$APP" "$WORK/ultimate.app"

echo ">>> blobs ($ID)"
# 1. the inputs are checked here (bitstream <= 0x220000 and a .bit whose header length matches, AXSS found,
#    app record valid, data <= 0x1DFFF4) and the blob region (manifest, bitstream, app) is written
python3 "$HERE/trx_pkg.py" blobs "$WORK/trx_50t.bit" "$WORK/ultimate.app" -o "$WORK/trx_blobs.bin" \
    --id "$ID" --bit-commit "$BITC" --app-commit "$APPC"

# the C header in vendor/gideon must be the one trx_pkg.py writes
if ! python3 "$HERE/trx_pkg.py" header | cmp -s - "$APP_REPO/software/application/update_u2p/trx_manifest.h"; then
    echo "trx_manifest.h in vendor/gideon differs from tools/trx_pkg.py header; regenerate it" >&2
    exit 1
fi

if [ "$NATIVE" = 1 ]; then echo ">>> updater (in place)"; else echo ">>> updater (container $IMAGE)"; fi
# 2. the host tools (hex2bin, checksum) are built for the container once, in vendor/gideon/tools/
TEST_OPTS=""
[ "$EMUTEST" = 1 ] && TEST_OPTS="-DTRX_EMU_TEST=1"
UPD_SCRIPT='set -eo pipefail
        [ -x "$PROJ/tools/hex2bin" ] && [ -x "$PROJ/tools/checksum" ] && head -c4 "$PROJ/tools/hex2bin" | grep -q ELF || make -C "$PROJ/tools" hex2bin checksum swap >/dev/null
        cd "$PROJ/target/u64ii/riscv/update_trx"
        make clean >/dev/null 2>&1 || true
        make TRX_TEST_OPTS="$TEST_OPTS" BLOBS_FILE="$IN/trx_blobs.bin" output result gitinfo result/update.cfw result/update.app result/update.elf 2>&1 | grep -vE "^(Assembling|Compiling|Converting|Creating|Extended address)" | tail -25
        test -s result/update.cfw && test -s result/update.app'
if [ "$NATIVE" = 1 ]; then
    PROJ="$APP_REPO" IN="$WORK" TEST_OPTS="$TEST_OPTS" bash -c "$UPD_SCRIPT"
else
    nice -n 15 container run --rm --rosetta --arch amd64 --cpus 2 --memory 4g \
        -e TEST_OPTS="$TEST_OPTS" -e PROJ=/project -e IN=/in \
        -v "$APP_REPO:/project" -v "$WORK:/in:ro" "$IMAGE" \
        bash -c "$UPD_SCRIPT"
fi

RES="$APP_REPO/target/u64ii/riscv/update_trx/result"
DEST="$OUT/trx64-$ID"
[ "$EMUTEST" = 1 ] && DEST="$DEST-EMUTEST"
mkdir -p "$DEST"
# Names (Alex, 2026-10-10): commit and date in the file name. TRX_UPDATE_NAME replaces the whole
# base name: the hook for the release tag, which later takes the place of commit and date.
NAME="${TRX_UPDATE_NAME:-trx64_update_${ID##*-}_$(date +%Y%m%d)}"
cp "$RES/update.cfw" "$DEST/$NAME.cfw"
cp "$RES/update.app" "$DEST/$NAME.ue2"
[ -n "$U2A" ] && cp "$U2A" "$DEST/trxmon.u2a"

echo ">>> checking the packages"
python3 "$HERE/trx_pkg_check.py" --bitstream "$WORK/trx_50t.bit" --app "$WORK/ultimate.app" "$DEST/$NAME.cfw" "$DEST/$NAME.ue2"

SHA="shasum -a 256"; command -v shasum >/dev/null || SHA=sha256sum
( cd "$DEST" && $SHA "$NAME.cfw" "$NAME.ue2" $( [ -n "$U2A" ] && echo trxmon.u2a ) > SHA256SUMS )
echo ">>> $DEST"
ls -l "$DEST"
cat "$DEST/SHA256SUMS"
