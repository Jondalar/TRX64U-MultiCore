#!/usr/bin/env bash
# Build the multi-core release bundle: one installable update (.cfw/.ue2) plus every core as a .cor,
# in one zip (T94 P5, built by hand on 2026-10-14 before the CI stage exists).
#
#   tools/make_release_mc.sh --id <id> --bit <c64_basic.bit> --app <hostapp.app> \
#       --bit-commit <sha> --app-commit <sha> --cor <file> [--cor <file> ...] [--out <dir>] [--name <base>]
#       [--update <file.cfw> --update-ue2 <file.ue2>]   the update file the pipeline already built
#       [--u2a <file.u2a> ...]                          applications every core reads from /cores/apps/ (T180 D9: unzip.u2a)
#       [--sd <dir>]                                    the package's SD/ tree (files the core lanes deliver for the card, SD/cores/<id>/...): packed as SD.zip
#       [--release]                                     the bundle of a release (T239.5 D21): every core whose TOML B says [app] release = false (tools/core_toml.py
#                                                       pre-release-only) is left out - its .cor and its SD/cores/<id>/ folder; without it they stay
#       [--txu <file.txu>]                              the host's own update (T152) for boards already on a MultiCore host
#                                                       (ci/update.sh); without it one is built here,
#                                                       which needs the RISC-V toolchain image
#
# The update file carries two blobs, as every TRX64 update does (docs/specs/T49-update-packages.md):
#   * the full bitstream to flash 0x000000 - the host AND the null core (cores/null, T123): the host is
#     routed together with its reference core (T87 D8), but the image in the flash carries the null core
#     in the partition (ci/update.sh packs null_full.bit), so no machine is in the flash;
#   * the app to flash 0x220000 - here the T84 host boot app, not the Ultimate app, so the machine
#     comes up in the boot menu and starts a core from a .cor.
# The other cores are not in the flash (Alex, 2026-10-03: no cores in flash); they ride along in the
# zip as .cor files for /USB1/cores.
#
# Output: <out>/trx64mc-<id>/{<name>.cfw,<name>.ue2,[<host>.txu,]*.cor,INSTALL.md,SHA256SUMS} and <out>/trx64mc-<id>.zip
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/.." && pwd)"
OUT="$ROOT/dist"
SD="" RELEASE=0
ID="" BIT="" APP="" BITC="" APPC="" NAME="" UPD_CFW="" UPD_UE2="" TXU=""
CORS=()
U2AS=()

usage() { sed -n '2,20p' "$0"; exit 2; }
while [ $# -gt 0 ]; do
    case "$1" in
        --id) ID="$2"; shift 2;;
        --bit) BIT="$2"; shift 2;;
        --app) APP="$2"; shift 2;;
        --bit-commit) BITC="$2"; shift 2;;
        --app-commit) APPC="$2"; shift 2;;
        --cor) CORS+=("$2"); shift 2;;
        --u2a) U2AS+=("$2"); shift 2;;
        --out) OUT="$2"; shift 2;;
        --name) NAME="$2"; shift 2;;
        --update) UPD_CFW="$2"; shift 2;;
        --update-ue2) UPD_UE2="$2"; shift 2;;
        --txu) TXU="$2"; shift 2;;
        --sd) SD="$2"; shift 2;;
        --release) RELEASE=1; shift;;
        -h|--help) usage;;
        *) echo "unknown option $1" >&2; usage;;
    esac
done
[ -n "$ID" ] && [ -n "$BIT" ] && [ -n "$APP" ] && [ -n "$BITC" ] && [ -n "$APPC" ] || usage
# T239.5 D21: a release bundle is made without the cores that are for the pre-release only ([app] release = false in their TOML B)
NOREL=""
if [ "$RELEASE" = 1 ]; then
    NOREL="$(python3 -I "$HERE/core_toml.py" pre-release-only)"
    keep=()
    for c in "${CORS[@]}"; do
        if printf '%s\n' "$NOREL" | grep -qx "$(basename "$c" .cor)"; then echo "    release bundle: $(basename "$c") left out (release = false)"; else keep+=("$c"); fi
    done
    CORS=(${keep[@]+"${keep[@]}"})
fi
[ "${#CORS[@]}" -gt 0 ] || { echo "at least one --cor is required" >&2; exit 2; }
if [ -n "$UPD_CFW" ] || [ -n "$UPD_UE2" ]; then
    [ -s "$UPD_CFW" ] && [ -s "$UPD_UE2" ] || { echo "--update needs both the .cfw and the .ue2" >&2; exit 2; }
fi
for f in "$BIT" "$APP" "${CORS[@]}" ${U2AS[@]+"${U2AS[@]}"}; do [ -s "$f" ] || { echo "$f: missing or empty" >&2; exit 1; }; done

# a release id x.y.z names the update file alone, as ci/update.sh does on a tag (ci/lib.sh file_id); any other id its commit and the date
if [[ "$ID" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]]; then NAME="${NAME:-trx64host_update_$ID}"; else NAME="${NAME:-trx64host_update_${ID##*-}_$(date +%Y%m%d)}"; fi
D="$OUT/trx64mc-$ID"
rm -rf "$D"; mkdir -p "$D"

echo ">>> every .cor against the contract and the host in this bundle"
CV="$(cat "$ROOT/contract/VERSION")"
# T84 D5b: the host key is the AXSS word of the very bitstream this bundle installs, so a .cor that
# belongs to another host cannot ride along. A bitstream from before D5b has a TIMESTAMP there and
# its .cor files carry that number instead; either way the two have to agree.
HK="$(python3 "$HERE/cor_pack.py" hostkey "$BIT")"
echo "    host key of $BIT: $HK"
for c in "${CORS[@]}"; do
    python3 "$HERE/cor_pack.py" check --contract "$CV" --host-key "$HK" "$c" > "$D/.$(basename "$c").json"
    cp "$c" "$D/$(basename "$c")"
done

# T239.5 D21: the SD/ tree of the package as one SD.zip (a pre-release holds flat files only; unzipped at the root of the card it puts SD/cores/<id>/ in place), without the folder of a core left out above
if [ -n "$SD" ] && [ -d "$SD" ]; then
    python3 - "$SD" "$D/SD.zip" "$NOREL" <<'PY'
import os, sys, zipfile
sd, out, norel = sys.argv[1], sys.argv[2], sys.argv[3].split()
files = []
for d, _, fs in os.walk(sd):
    for f in fs:
        rel = os.path.relpath(os.path.join(d, f), sd).replace(os.sep, "/")
        parts = rel.split("/")
        if parts[0].lower() == "cores" and len(parts) > 2 and parts[1] in norel:
            continue
        files.append(rel)
if files:
    with zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED) as z:
        for rel in sorted(files):
            zi = zipfile.ZipInfo("SD/" + rel, date_time=(2026, 1, 1, 0, 0, 0))
            zi.compress_type = zipfile.ZIP_DEFLATED
            zi.external_attr = 0o644 << 16
            z.writestr(zi, open(os.path.join(sd, rel), "rb").read())
PY
fi

for u in ${U2AS[@]+"${U2AS[@]}"}; do cp "$u" "$D/$(basename "$u")"; done

if [ -n "$UPD_CFW" ]; then
    # Built by the pipeline (ci/update.sh), which is where it belongs: no RISC-V toolchain needed here.
    echo ">>> updater: the one the pipeline built ($(basename "$UPD_CFW"))"
    cp "$UPD_CFW" "$UPD_UE2" "$D/"
    NAME="$(basename "$UPD_CFW" .cfw)"
else
    echo ">>> updater (.cfw/.ue2) from the bitstream and the host app"
    TRX_UPDATE_NAME="$NAME" "$HERE/make_update.sh" --id "$ID" --bit-commit "$BITC" --app-commit "$APPC" \
        --out "$OUT/.upd" "$BIT" "$APP"
    UD="$OUT/.upd/trx64-$ID"
    cp "$UD/$NAME.cfw" "$UD/$NAME.ue2" "$D/"
fi

TXUARG=()
if [ -n "$TXU" ]; then
    [ -s "$TXU" ] || { echo "--txu $TXU: no such file" >&2; exit 2; }
    cp "$TXU" "$D/"
    TXUARG=(--txu "$(basename "$TXU")")
fi

echo ">>> INSTALL.md"
python3 "$HERE/release_notes_mc.py" --id "$ID" --name "$NAME" --bit "$BIT" --app "$APP" \
    --bit-commit "$BITC" --app-commit "$APPC" --cor-dir "$D" -o "$D/INSTALL.md" ${TXUARG[@]+"${TXUARG[@]}"}

echo ">>> ROMS.txt"
python3 "$HERE/gen_roms_txt.py" -o "$D/ROMS.txt"

rm -f "$D"/.*.json
SHA="shasum -a 256"; command -v shasum >/dev/null || SHA=sha256sum
( cd "$D" && $SHA -- * > SHA256SUMS )

python3 - "$D" "$OUT/trx64mc-$ID.zip" "trx64mc-$ID" <<'PY'
import os, sys, zipfile
d, out, top = sys.argv[1:]
with zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED) as z:
    for f in sorted(os.listdir(d)):
        zi = zipfile.ZipInfo("%s/%s" % (top, f), date_time=(2026, 1, 1, 0, 0, 0))
        zi.compress_type = zipfile.ZIP_DEFLATED
        zi.external_attr = 0o644 << 16
        with open(os.path.join(d, f), "rb") as fh:
            z.writestr(zi, fh.read())
PY
# T148 D8: a release holds .cor files, never a core folder
python3 "$HERE/check_package_layout.py" "$D" "$OUT/trx64mc-$ID.zip"
ls -l "$D" "$OUT/trx64mc-$ID.zip"
cat "$D/SHA256SUMS"
