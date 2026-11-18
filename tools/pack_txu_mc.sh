#!/usr/bin/env bash
# Pack the MultiCore update file (.txu) of one CI run (T152 D3/D5; ci/package.sh calls it).
#
#   tools/pack_txu_mc.sh --host <null_full.bit> --app <hostapp.app> --branch <name> --sha <8 digit commit> --commit <15 digit commit> --out <dir> [--version <bundle version>]
#                        [--file-id <name part>]
#
# The host part is the bitstream the update stage writes into the flash (null_full.bit: the host with the null core in the partition, T123), the app part is
# OUR host application (the .app of stage trxapps, record and data as the boot ROM reads them). tools/txu_pack.py checks both like the host does before its first
# erase (a .bit of the stated length, the XC7A50T, the host key stamp, an application record the boot ROM takes), writes <out>/trx64host_update_<file id>.txu
# (--file-id, default the --sha; ci/package.sh passes ci/lib.sh file_id: the version on a tag) and
# reads it back (`inspect`: header, CRC, every part against its SHA-256). The version field holds 23 characters: the bundle version (T221 D17, x.y.z or x.y.z-dev+<sha8>); without --version the branch cut to 14, a dash, the commit.
# Any failure: no file is left behind and the exit status is 1.
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
HOST="" APP="" BRANCH="" SHA="" COMMIT="" OUT="" VER="" FID=""
while [ $# -gt 0 ]; do
    case "$1" in
        --host) HOST="$2"; shift 2;;
        --app) APP="$2"; shift 2;;
        --branch) BRANCH="$2"; shift 2;;
        --sha) SHA="$2"; shift 2;;
        --commit) COMMIT="$2"; shift 2;;
        --out) OUT="$2"; shift 2;;
        --version) VER="$2"; shift 2;;
        --file-id) FID="$2"; shift 2;;
        *) echo "pack_txu_mc: unknown option $1" >&2; exit 2;;
    esac
done
[ -f "$HOST" ] && [ -f "$APP" ] && [ -n "$BRANCH" ] && [ -n "$SHA" ] && [ -n "$COMMIT" ] && [ -d "$OUT" ] || { sed -n '2,12p' "$0" >&2; exit 2; }
f="$OUT/trx64host_update_${FID:-$SHA}.txu"
if python3 -I "$HERE/txu_pack.py" pack --host "$HOST" --app "$APP" --version "${VER:-${BRANCH:0:14}-$SHA}" --commit "$COMMIT" -o "$f" \
   && python3 -I "$HERE/txu_pack.py" inspect "$f"; then
    exit 0
fi
rm -f "$f"
exit 1
