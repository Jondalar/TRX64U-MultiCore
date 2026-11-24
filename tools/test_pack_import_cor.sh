#!/usr/bin/env bash
# T104.2 Q5: tools/pack_import_cor.sh packs the pinned bitstream and the pinned application of trx64g and refuses any other file. (trx64u is TRX64-Ultimate's
# finished .cor since T239.3 P1-8: ci/test_imports.py proves its pin.) Uses a synthetic full bitstream (test_cor_pack.full_bit).
#   tools/test_pack_import_cor.sh            runs the scenarios;  with --control the pins are NOT matched (the run must fail)
set -euo pipefail
unset GITHUB_REF_TYPE GITHUB_REF_NAME GITHUB_EVENT_NAME   # a tag run's environment must not reach the scenarios this test builds itself (run 2449)
HERE="$(cd "$(dirname "$0")" && pwd)"; ROOT="$HERE/.."
W="$(mktemp -d)"; trap 'rm -rf "$W"' EXIT
python3 -I - "$HERE" "$W" <<'PY'
import sys, hashlib, os
sys.path.insert(0, sys.argv[1]); w = sys.argv[2]
import test_cor_pack as T
b = T.full_bit(); open(w + "/good.bit", "wb").write(b)
open(w + "/other.bit", "wb").write(T.full_bit(frames=2100))
open(w + "/hash", "w").write(hashlib.sha256(b).hexdigest())
app = bytes([0, 0, 3, 0, 0x10, 0, 0, 0, 0, 0, 3, 0]) + os.urandom(3000)
open(w + "/good.app", "wb").write(app); open(w + "/other.app", "wb").write(app[:-1] + bytes([app[-1] ^ 0xff]))  # always differs (b"x" matched 1 in 256 random apps, run 258)
open(w + "/apphash", "w").write(hashlib.sha256(app).hexdigest())
PY
head -c 4000 /dev/urandom > "$W/app.bin"
for id in trx64g; do
    sed -e "s/^BIT_SHA256=.*/BIT_SHA256=$(cat "$W/hash")/" -e "s/^APP_SHA256=.*/APP_SHA256=$(cat "$W/apphash")/" "$ROOT/release/imports/$id.pin" > "$W/$id.pin"
done
if [ "${1:-}" = --control ]; then
    sed -i.bak "s/^BIT_SHA256=.*/BIT_SHA256=$(shasum -a 256 "$W/other.bit" | cut -c1-64)/" "$W/trx64g.pin"
fi
fail() { echo "FAIL $*"; exit 1; }
# 2. trx64g: Gideon's application from the pinned .app, the 12-byte record header stripped
"$HERE/pack_import_cor.sh" --pin "$W/trx64g.pin" --app-bin "$W/app.bin" --out "$W/ok" --bit "$W/good.bit" --app-file "$W/good.app" >/dev/null
M="$(python3 "$HERE/cor_pack.py" check "$W/ok/trx64g.cor")"
echo "$M" | grep -q '"id": "trx64g"' && echo "$M" | grep -q '"app_size": 3000' && echo "PASS packed trx64g.cor (app minus 12 bytes)" || fail "trx64g not packed"
# 3. another bitstream or another application is refused and leaves nothing behind
if "$HERE/pack_import_cor.sh" --pin "$W/trx64g.pin" --app-bin "$W/app.bin" --out "$W/bad" --bit "$W/other.bit" --app-file "$W/good.app" >/dev/null 2>&1; then fail "a bitstream other than the pin's was packed"; fi
if "$HERE/pack_import_cor.sh" --pin "$W/trx64g.pin" --app-bin "$W/app.bin" --out "$W/bad" --bit "$W/good.bit" --app-file "$W/other.app" >/dev/null 2>&1; then fail "an application other than the pin's was packed"; fi
[ ! -e "$W/bad/trx64g.cor" ] && echo "PASS other bitstream / application refused, no .cor" || fail ".cor left behind"
# 5. T230 P-1/S2: SEMVER=x.y.z or x.y.z-dev+<sha8 of PKG_VERSION>, checked against the "version" of the package's manifest.json (fake local files, no network)
PKGSHA="$(sed -n 's/^PKG_VERSION=.*-\([0-9a-f]*\)$/\1/p' "$W/trx64g.pin")"
mkpin() { sed -e "s/^SEMVER=.*/SEMVER=$1/" "$W/trx64g.pin" > "$W/sv.pin"; }
cor_version() { python3 "$HERE/cor_pack.py" check "$W/$1/trx64g.cor" | sed -n 's/.*"version": "\([^"]*\)".*/\1/p'; }
packsv() { rm -rf "$W/sv"; "$HERE/pack_import_cor.sh" --pin "$W/sv.pin" --app-bin "$W/app.bin" --out "$W/sv" --bit "$W/good.bit" --app-file "$W/good.app" "$@"; }
mkpin 0.3.0
packsv >/dev/null 2>&1 && V="$(cor_version sv)" && case "$V" in 0.3.0-dev+*) echo "PASS SEMVER=0.3.0 (release form) packed as $V";; *) fail "strict form gave '$V'";; esac
mkpin "0.3.1-dev+$PKGSHA"
packsv >/dev/null 2>&1 && [ "$(cor_version sv)" = "0.3.1-dev+$PKGSHA" ] && echo "PASS SEMVER=0.3.1-dev+$PKGSHA (sha8 of PKG_VERSION) packed as it is, no second suffix" || fail "dev form not packed as is"
mkpin "0.3.1-dev+deadbeef"
if packsv >/dev/null 2>&1; then fail "a dev SEMVER with a foreign sha8 was packed"; fi
[ ! -e "$W/sv/trx64g.cor" ] && echo "PASS SEMVER=0.3.1-dev+deadbeef (not the sha8 of PKG_VERSION) refused, no .cor" || fail ".cor left behind"
mkpin "0.3.1-rc1"
if packsv >/dev/null 2>&1; then fail "a malformed SEMVER was packed"; fi
echo "PASS SEMVER=0.3.1-rc1 refused"
mkpin 0.3.0
echo '{"trx64_commit": "x", "version": "0.3.0"}' > "$W/m_eq.json"; echo '{"version": "0.4.0"}' > "$W/m_ne.json"; echo '{"trx64_commit": "x"}' > "$W/m_none.json"
packsv --manifest "$W/m_eq.json" >/dev/null 2>&1 && echo "PASS manifest version 0.3.0 equals SEMVER 0.3.0: packed" || fail "equal manifest version refused"
mkpin "0.3.0-dev+$PKGSHA"
packsv --manifest "$W/m_eq.json" >/dev/null 2>&1 && echo "PASS dev SEMVER: its x.y.z 0.3.0 equals the manifest version: packed" || fail "equal manifest version refused (dev form)"
mkpin 0.3.0
E="$(packsv --manifest "$W/m_ne.json" 2>&1 >/dev/null)" && fail "a manifest with another version was packed"
[ ! -e "$W/sv/trx64g.cor" ] && echo "$E" | grep -q "0.3.0" && echo "$E" | grep -q "0.4.0" && echo "PASS manifest version 0.4.0 differs from SEMVER 0.3.0: refused, message names both ($(echo "$E" | tail -1 | cut -c1-120)...)" || fail "message does not name both: $E"
E="$(packsv --manifest "$W/m_none.json" 2>&1 >/dev/null)" && echo "$E" | grep -q "has no version (older package)" && echo "PASS manifest without version: packed, one log line" || fail "manifest without version: $E"
