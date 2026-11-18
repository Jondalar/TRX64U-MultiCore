#!/usr/bin/env bash
# Pack the large .cor of a core whose bitstream is built in another repository (release/imports/<id>.pin; T104.2: trx64g).
#
#   tools/pack_import_cor.sh --pin release/imports/<id>.pin --out dir [--app-bin coreapp.bin | (pin has APP_FILE)] [--bit <local file>]
#                            [--app-file <local .app>] [--app-commit <sha>] [--manifest <local manifest.json>]
#
# A pin with DESC (the menu.toml) and OPS (the application's export) carries the core's menu as record 6 of the .cor, compiled and checked by tools/descrec.py.
#
# A pin without APP_FILE is packed with --app-bin (our generic application, trx64u); a pin with APP_FILE takes that .app of the same package
# (Gideon's application, trx64g), SHA-256 checked against APP_SHA256, its 12-byte record header stripped.
#
# Without --bit the bitstream is downloaded from the generic package trx64-ultimate (FW_PKG)/<PKG_VERSION> of the same Gitea owner (needs
# GITHUB_SERVER_URL, GITHUB_REPOSITORY and PACKAGE_TOKEN, as the rest of the pipeline). Either way its SHA-256 has to be the pin's:
# a different file is refused and nothing is packed. The packing itself is tools/pack_large_cor.sh (IDCODE, sync word, map and hashes are
# checked there and again by `cor_pack.py check`).
#
# SEMVER= (T230 P-1): `x.y.z` (PKG_VERSION is a release) or `x.y.z-dev+<sha8>` (<sha8> = the one of PKG_VERSION); anything else is refused. The package's
# manifest.json (downloaded with the bitstream, or --manifest) has "version" from TRX64-Ultimate's APPL_VERSION_NUMBER: when present, the x.y.z of
# SEMVER must equal it; an older package without the field is logged and passed. With --bit and no --manifest nothing is downloaded and the check is skipped.
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/.." && pwd)"
source "$ROOT/ci/pkg.env"
PIN="" APPBIN="" OUT="" BIT="" APPC="" APPLOCAL="" MANIFEST=""
while [ $# -gt 0 ]; do
    case "$1" in
        --pin) PIN="$2"; shift 2;;
        --app-bin) APPBIN="$2"; shift 2;;
        --out) OUT="$2"; shift 2;;
        --bit) BIT="$2"; shift 2;;
        --app-file) APPLOCAL="$2"; shift 2;;
        --app-commit) APPC="$2"; shift 2;;
        --manifest) MANIFEST="$2"; shift 2;;
        *) echo "unknown option $1" >&2; exit 2;;
    esac
done
[ -f "$PIN" ] && [ -n "$OUT" ] || { sed -n '2,10p' "$0" >&2; exit 2; }
ID="" NAME="" VERSION="" SEMVER="" PKG_VERSION="" BIT_FILE="" BIT_SHA256="" ICON="" APP_FILE="" APP_SHA256="" DESC="" OPS="" FILES=""
# shellcheck disable=SC1090
. "$PIN"
# T221 D15 / T230 P-1: the version of an imported core is the SemVer its pin copies from the TRX64-Ultimate package (SEMVER=); a pin without one shows the commit.
SEMVER_BASE=""
if [ -n "$SEMVER" ]; then
    SEMVER_BASE="${SEMVER%%-dev+*}"
    if [ "$SEMVER_BASE" = "$SEMVER" ]; then
        VERSION="$("$ROOT/ci/version.sh" with "$SEMVER")"
    else
        PKG_SHA8="${PKG_VERSION##*-}"
        [ "$SEMVER" = "$SEMVER_BASE-dev+$PKG_SHA8" ] && [ -n "$PKG_SHA8" ] || { echo "$PIN: SEMVER=$SEMVER is neither x.y.z nor x.y.z-dev+<sha8 of PKG_VERSION $PKG_VERSION>" >&2; exit 2; }
        printf '%s' "$SEMVER_BASE" | grep -Eq '^(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)$' || { echo "$PIN: SEMVER=$SEMVER has no x.y.z base" >&2; exit 2; }
        VERSION="$SEMVER"
    fi
else echo "$PIN: no SEMVER, the version shown is the commit $VERSION (T221 D15 open)" >&2; fi
for v in ID NAME VERSION PKG_VERSION BIT_FILE BIT_SHA256 ICON; do [ -n "${!v}" ] || { echo "$PIN: $v is empty" >&2; exit 2; }; done
W="$(mktemp -d)"; trap 'rm -rf "$W"' EXIT
sha() { python3 -c 'import hashlib,sys; print(hashlib.sha256(open(sys.argv[1],"rb").read()).hexdigest())' "$1"; }
fetch() {   # fetch <file in the package> <dest>
    : "${GITHUB_SERVER_URL:?}" "${GITHUB_REPOSITORY:?}" "${PACKAGE_TOKEN:?}"
    curl -sf -H "Authorization: token ${PACKAGE_TOKEN}" \
        "${GITHUB_SERVER_URL}/api/packages/${GITHUB_REPOSITORY%%/*}/generic/${FW_PKG}/${PKG_VERSION}/$1" -o "$2" \
        || { echo "cannot download ${FW_PKG}/${PKG_VERSION}/$1" >&2; exit 1; }
}
# T230 S2: the version of the pinned package's manifest against the pin's SEMVER
if [ -z "$MANIFEST" ] && [ -z "$BIT" ] && [ -n "$SEMVER" ]; then fetch manifest.json "$W/manifest.json"; MANIFEST="$W/manifest.json"; fi
if [ -n "$SEMVER" ] && [ -n "$MANIFEST" ]; then
    MV="$(python3 -I -c 'import json,sys; v=json.load(open(sys.argv[1])).get("version"); print(v if isinstance(v,str) else "")' "$MANIFEST")" || { echo "$PIN: $MANIFEST is not readable JSON" >&2; exit 1; }
    if [ -z "$MV" ]; then echo "$PIN: manifest of $PKG_VERSION has no version (older package), SEMVER=$SEMVER not checked against it" >&2
    elif [ "$MV" != "$SEMVER_BASE" ]; then echo "$PIN: SEMVER=$SEMVER (x.y.z $SEMVER_BASE) differs from the version $MV in the manifest of ${FW_PKG:-package}/$PKG_VERSION: refused" >&2; exit 1
    fi
fi
if [ -z "$BIT" ]; then fetch "$BIT_FILE" "$W/bit"; BIT="$W/bit"; fi
GOT="$(sha "$BIT")"
[ "$GOT" = "$BIT_SHA256" ] || { echo "the bitstream is $GOT, the pin says $BIT_SHA256: refused" >&2; exit 1; }
if [ -n "$APP_FILE" ]; then
    [ -n "$APP_SHA256" ] || { echo "$PIN: APP_FILE without APP_SHA256" >&2; exit 2; }
    if [ -z "$APPLOCAL" ]; then fetch "$APP_FILE" "$W/app"; APPLOCAL="$W/app"; fi
    GOT="$(sha "$APPLOCAL")"
    [ "$GOT" = "$APP_SHA256" ] || { echo "the application is $GOT, the pin says $APP_SHA256: refused" >&2; exit 1; }
    tail -c +13 "$APPLOCAL" > "$W/app.bin"; APPBIN="$W/app.bin"
fi
[ -s "$APPBIN" ] || { echo "no application: give --app-bin or a pin with APP_FILE" >&2; exit 2; }
cp "$BIT" "$W/${ID}_mono.bit"
# the menu of a core of ours (T104.2 D10): record 6, every name and binding checked against what the application exports; a menu without the check is no menu
rel() { case "$1" in /*) echo "$1";; *) echo "$ROOT/$1";; esac; }
[ -z "$DESC$OPS" ] || { [ -n "$DESC" ] && [ -n "$OPS" ] && [ -f "$(rel "$DESC")" ] && [ -f "$(rel "$OPS")" ]; } || { echo "$PIN: DESC and OPS go together and name existing files" >&2; exit 2; }
"$HERE/pack_large_cor.sh" ${DESC:+--desc "$(rel "$DESC")" --ops "$(rel "$OPS")"} ${FILES:+--files "$(rel "$FILES")"} --id "$ID" --name "$NAME" --version "$VERSION" --bit "$W/${ID}_mono.bit" --app-bin "$APPBIN" \
    --icon "$ROOT/$ICON" --bit-commit "${PKG_VERSION##*-}" ${APPC:+--app-commit "$APPC"} --out "$OUT"
