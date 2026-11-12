#!/usr/bin/env bash
# T174 D1: tools/treesync.py keeps a build tree equal to the working tree and touches only what changed (so make rebuilds only that).
# A changed file is copied, an unchanged one keeps its mtime, a removed one goes, an ignored build output in the destination stays, --clean empties.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
W="$(mktemp -d "${TMPDIR:-/tmp}/treesync.XXXXXX")"; trap 'rm -rf "$W"' EXIT
fail() { echo "test_treesync: FAIL $*" >&2; exit 1; }
S="$W/src"; D="$W/dst"; mkdir -p "$S/a/b"
git -C "$S" init -q
printf 'out/\n' > "$S/.gitignore"
echo one > "$S/a/one.c"; echo two > "$S/a/b/two.c"; echo three > "$S/three.h"; ln -s three.h "$S/link.h"
git -C "$S" add -A
sync() { python3 -I "$ROOT/tools/treesync.py" "$S" "$D" "$@"; }
sync | grep -q "5 changed" || fail "first sync did not copy 5 files"
[ -L "$D/link.h" ] && [ "$(readlink "$D/link.h")" = three.h ] || fail "the symlink was not copied as a link"
# a build output of the destination (ignored in the source) must survive a later sync
mkdir -p "$D/out"; echo obj > "$D/out/one.o"
# make the mtimes of the copies old, so an unwanted rewrite shows
touch -t 200001010000 "$D/a/one.c" "$D/a/b/two.c" "$D/three.h"
echo two-changed > "$S/a/b/two.c"; rm "$S/three.h"; echo four > "$S/four.c"
sync | grep -q "2 changed, 1 removed" || fail "second sync: wanted 2 changed (two.c, four.c), 1 removed (three.h)"
python3 -c 'import os,sys; sys.exit(0 if os.path.getmtime(sys.argv[1]) < 1e9 else 1)' "$D/a/one.c" || fail "an unchanged file was rewritten (mtime moved)"
[ "$(cat "$D/a/b/two.c")" = two-changed ] || fail "a changed file was not copied"
[ ! -e "$D/three.h" ] || fail "a removed file stayed"
[ -f "$D/out/one.o" ] || fail "an ignored build output of the destination was deleted"
# control: a sync of an unchanged tree changes nothing
sync | grep -q "0 changed, 0 removed" || fail "control: an unchanged tree synced something"
sync --clean | grep -q "changed" || fail "--clean failed"
[ ! -e "$D/out/one.o" ] || fail "--clean kept a build output"
echo "test_treesync: ok (changed copied, unchanged keeps its mtime, removed removed, build output kept, --clean empties)"
