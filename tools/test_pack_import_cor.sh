#!/usr/bin/env bash
# T104.2 Q5: tools/pack_import_cor.sh packs the pinned bitstream (and, for trx64g, the pinned application) and refuses any other file; trx64u and
# trx64g never share a core folder or .cfg. Uses a synthetic full bitstream (test_cor_pack.full_bit).
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
for id in trx64u trx64g; do
    sed -e "s/^BIT_SHA256=.*/BIT_SHA256=$(cat "$W/hash")/" -e "s/^APP_SHA256=.*/APP_SHA256=$(cat "$W/apphash")/" "$ROOT/release/imports/$id.pin" > "$W/$id.pin"
done
if [ "${1:-}" = --control ]; then
    sed -i.bak "s/^BIT_SHA256=.*/BIT_SHA256=$(shasum -a 256 "$W/other.bit" | cut -c1-64)/" "$W/trx64u.pin" "$W/trx64g.pin"
fi
fail() { echo "FAIL $*"; exit 1; }
# 1. trx64u: our application, the pinned bitstream; id and name are the pin's
"$HERE/pack_import_cor.sh" --pin "$W/trx64u.pin" --app-bin "$W/app.bin" --out "$W/ok" --bit "$W/good.bit" >/dev/null
M="$(python3 "$HERE/cor_pack.py" check "$W/ok/trx64u.cor")"
echo "$M" | grep -q '"id": "trx64u"' && echo "$M" | grep -q '"name": "TRX64U"' && echo "PASS packed trx64u.cor (TRX64U)" || fail "trx64u not packed"
# 2. trx64g: Gideon's application from the pinned .app, the 12-byte record header stripped
"$HERE/pack_import_cor.sh" --pin "$W/trx64g.pin" --app-bin "$W/app.bin" --out "$W/ok" --bit "$W/good.bit" --app-file "$W/good.app" >/dev/null
M="$(python3 "$HERE/cor_pack.py" check "$W/ok/trx64g.cor")"
echo "$M" | grep -q '"id": "trx64g"' && echo "$M" | grep -q '"app_size": 3000' && echo "PASS packed trx64g.cor (app minus 12 bytes)" || fail "trx64g not packed"
# 2b. T104.2 D10: trx64u carries its menu as record 6 - the bytes menu.toml compiles to - and trx64g (Gideon's application) carries none
python3 -I - "$HERE" "$W/ok/trx64u.cor" "$W/ok/trx64g.cor" "$ROOT" <<'PY' && echo "PASS trx64u.cor carries the menu record, trx64g.cor none" || fail "the menu record"
import sys
sys.path.insert(0, sys.argv[1])
import cor_pack as C, descrec
m, r = C.parse(open(sys.argv[2], "rb").read())
want = descrec.compile_checked(open(sys.argv[4] + "/coreapp/cores/trx64u/menu.toml").read(), open(sys.argv[4] + "/tests/coreapp/golden/ops_trx64u.txt").read())
assert r[C.T_DESC] == want and m["desc_size"] == len(want), "record 6 is not what menu.toml compiles to"
m2, r2 = C.parse(open(sys.argv[3], "rb").read())
assert C.T_DESC not in r2 and "desc_size" not in m2
PY
# 2c. a menu that names an operation the application does not export is refused when packing, and nothing is written
sed -e 's/apply = "drive.set_ram"/apply = "drive.set_nothing"/' "$ROOT/coreapp/cores/trx64u/menu.toml" > "$W/bad_menu.toml"
sed -e "s|^DESC=.*|DESC=$W/bad_menu.toml|" "$W/trx64u.pin" > "$W/trx64u_bad.pin"
if "$HERE/pack_import_cor.sh" --pin "$W/trx64u_bad.pin" --app-bin "$W/app.bin" --out "$W/badmenu" --bit "$W/good.bit" >/dev/null 2>&1; then fail "a menu with an operation that is not exported was packed"; fi
[ ! -e "$W/badmenu/trx64u.cor" ] && echo "PASS a menu with an operation the application does not export is refused, no .cor" || fail ".cor left behind"
# 3. another bitstream or another application is refused and leaves nothing behind
if "$HERE/pack_import_cor.sh" --pin "$W/trx64u.pin" --app-bin "$W/app.bin" --out "$W/bad" --bit "$W/other.bit" >/dev/null 2>&1; then fail "a bitstream other than the pin's was packed"; fi
if "$HERE/pack_import_cor.sh" --pin "$W/trx64g.pin" --app-bin "$W/app.bin" --out "$W/bad" --bit "$W/good.bit" --app-file "$W/other.app" >/dev/null 2>&1; then fail "an application other than the pin's was packed"; fi
[ ! -e "$W/bad/trx64u.cor" ] && [ ! -e "$W/bad/trx64g.cor" ] && echo "PASS other bitstream / application refused, no .cor" || fail ".cor left behind"
# 4. the two never share a core folder or .cfg: the folder is the .cor's file name (T148 D1)
python3 "$HERE/cor_pack.py" check "$W/ok/trx64u.cor" | grep -q '"id": "trx64u"' && [ "$(basename "$W/ok/trx64u.cor" .cor)" != "$(basename "$W/ok/trx64g.cor" .cor)" ] \
    && echo "PASS cores/trx64u/ + trx64u.cfg and cores/trx64g/ + trx64g.cfg differ" || fail "folders shared"
# 5. T230 P-1/S2: SEMVER=x.y.z or x.y.z-dev+<sha8 of PKG_VERSION>, checked against the "version" of the package's manifest.json (fake local files, no network)
PKGSHA="$(sed -n 's/^PKG_VERSION=.*-\([0-9a-f]*\)$/\1/p' "$W/trx64u.pin")"
mkpin() { sed -e "s/^SEMVER=.*/SEMVER=$1/" "$W/trx64u.pin" > "$W/sv.pin"; }
cor_version() { python3 "$HERE/cor_pack.py" check "$W/$1/trx64u.cor" | sed -n 's/.*"version": "\([^"]*\)".*/\1/p'; }
packsv() { rm -rf "$W/sv"; "$HERE/pack_import_cor.sh" --pin "$W/sv.pin" --app-bin "$W/app.bin" --out "$W/sv" --bit "$W/good.bit" "$@"; }
mkpin 0.3.0
packsv >/dev/null 2>&1 && V="$(cor_version sv)" && case "$V" in 0.3.0-dev+*) echo "PASS SEMVER=0.3.0 (release form) packed as $V";; *) fail "strict form gave '$V'";; esac
mkpin "0.3.1-dev+$PKGSHA"
packsv >/dev/null 2>&1 && [ "$(cor_version sv)" = "0.3.1-dev+$PKGSHA" ] && echo "PASS SEMVER=0.3.1-dev+$PKGSHA (sha8 of PKG_VERSION) packed as it is, no second suffix" || fail "dev form not packed as is"
mkpin "0.3.1-dev+deadbeef"
if packsv >/dev/null 2>&1; then fail "a dev SEMVER with a foreign sha8 was packed"; fi
[ ! -e "$W/sv/trx64u.cor" ] && echo "PASS SEMVER=0.3.1-dev+deadbeef (not the sha8 of PKG_VERSION) refused, no .cor" || fail ".cor left behind"
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
[ ! -e "$W/sv/trx64u.cor" ] && echo "$E" | grep -q "0.3.0" && echo "$E" | grep -q "0.4.0" && echo "PASS manifest version 0.4.0 differs from SEMVER 0.3.0: refused, message names both ($(echo "$E" | tail -1 | cut -c1-120)...)" || fail "message does not name both: $E"
E="$(packsv --manifest "$W/m_none.json" 2>&1 >/dev/null)" && echo "$E" | grep -q "has no version (older package)" && echo "PASS manifest without version: packed, one log line" || fail "manifest without version: $E"
