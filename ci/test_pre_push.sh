#!/usr/bin/env bash
# T239.1 S1: the path rule of .githooks/pre-push, with a temporary side repository (neutral names) and a stub of the content checker.
# An edited copy at a listed path is refused (also when a later commit removes it), a clean branch passes, an unset trx.side switches the rule off.
set -euo pipefail
HK="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)/.githooks/pre-push"
W="$(mktemp -d)"; trap 'rm -rf "${W:?}"' EXIT
fail() { echo "FAIL: $*"; exit 1; }
g() { git -C "$W/r" -c user.name=t -c user.email=t@example.org "$@"; }
git init -q "$W/side"; mkdir -p "$W/side/zz"; echo a > "$W/side/zz/neutral-one.dat"; echo r > "$W/side/README.md"
git -C "$W/side" add .; git -C "$W/side" -c user.name=t -c user.email=t@example.org commit -qm s
git init -q "$W/r"; mkdir -p "$W/r/tools"; printf 'import sys\nsys.exit(0)\n' > "$W/r/tools/check_public_tree.py"; echo x > "$W/r/f"
g add .; g commit -qm base; base="$(g rev-parse HEAD)"
mkdir -p "$W/r/zz"; echo edited > "$W/r/zz/neutral-one.dat"; g add zz; g commit -qm plant; g rm -q -f zz/neutral-one.dat; g commit -qm unplant; plant="$(g rev-parse HEAD)"
push() { (cd "$W/r" && echo "refs/heads/b $1 refs/heads/b $2" | "$HK" 2>&1); }
g config trx.side "$W/side"
push "$base" "$base" >/dev/null || fail "the clean branch was refused"
out="$(push "$plant" "$base")" && fail "an edited copy of a listed path was accepted"
echo "$out" | grep -q 'zz/neutral-one.dat' || fail "the refusal does not name the path"
g config --unset trx.side
out="$(push "$plant" "$base")" || fail "unset trx.side refused"
echo "$out" | grep -q 'rule is off' || fail "no line that the path rule is off"
echo "pre-push path rule: ok"
