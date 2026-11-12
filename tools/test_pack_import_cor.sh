#!/usr/bin/env bash
# T104.2 Q5: tools/pack_import_cor.sh packs the pinned bitstream (and, for trx64g, the pinned application) and refuses any other file; trx64u and
# trx64g never share a core folder or .cfg. Uses a synthetic full bitstream (test_cor_pack.full_bit).
#   tools/test_pack_import_cor.sh            runs the scenarios;  with --control the pins are NOT matched (the run must fail)
set -euo pipefail
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
