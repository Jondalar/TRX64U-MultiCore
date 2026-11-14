#!/usr/bin/env bash
# T215: a failing stage inside tree_key / files_key must fail the key (non-zero, a message, no key on stdout). Runs ci/lib.sh in a scratch repo whose
# ci/code_hash.py is a fake that fails the stage named in FAKE_FAIL. bash ignores set -e inside a subshell on the left of `||`, so each stage needs its own check.
set -u
src="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
T="$(mktemp -d)"; trap 'rm -rf "$T"' EXIT
mkdir -p "$T/ci" "$T/a" "$T/vendor/gideon/fpga" "$T/vendor/gideon/target/simulation"
cp "$src/ci/lib.sh" "$T/ci/lib.sh"
cat > "$T/ci/code_hash.py" <<'PY'
import os, sys
c = sys.argv[1]
if os.environ.get("FAKE_FAIL") == c:
    sys.exit(1)
if c == "index":
    sys.stdout.write(sys.stdin.read())
elif c == "hash-paths":
    [print("0" * 40) for _ in sys.stdin.read().split("\n") if _]
elif c == "sha256":
    [print("0" * 64 + "  " + p) for p in sys.argv[2:]]
PY
echo x > "$T/a/f"; echo v > "$T/vendor/gideon/fpga/x.vhd"
for f in LICENSE.txt software/io/usb/nano_minimal.nan software/portable/riscv/bootloader_u64ii.c tools/parse_nano.py; do mkdir -p "$T/vendor/gideon/$(dirname $f)"; echo 1 > "$T/vendor/gideon/$f"; done
git -C "$T" init -q && git -C "$T" add a ci && git -C "$T" -c user.email=a@b -c user.name=t commit -qm t
fails=0
t() {   # t <name> <expect: ok|fail> <FAKE_FAIL> <function...>
  local name="$1" want="$2" ff="$3"; shift 3
  local out err rc
  out="$(FAKE_FAIL="$ff" bash -c 'source "$1/ci/lib.sh"; shift; "$@"' _ "$T" "$@" 2>"$T/err")"; rc=$?
  err="$(cat "$T/err")"
  if [ "$want" = ok ]; then [ "$rc" = 0 ] && [ -n "$out" ] && { echo "ok   $name"; return; }
  else [ "$rc" != 0 ] && [ -z "$out" ] && [ -n "$err" ] && { echo "ok   $name (rc=$rc, no key, message: $(echo "$err" | tail -1))"; return; }; fi
  echo "FAIL $name rc=$rc out='$out'"; fails=$((fails + 1))
}
t "tree_key all stages fine gives a key" ok none tree_key a
t "tree_key: code_hash index fails -> no key" fail index tree_key a
t "tree_key: gideon sha256 fails -> no key" fail sha256 tree_key a
t "files_key all stages fine gives a key" ok none files_key a/f
t "files_key: hash-paths fails -> no key" fail hash-paths files_key a/f
echo "$fails failed"; [ "$fails" = 0 ]
