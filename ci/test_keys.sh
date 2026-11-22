#!/usr/bin/env bash
# T239.1 D13: ci/keys.sh and the ci/lib.sh helpers that depend on where a run happens. Unit and happy path, nothing built.
#  1. keys.sh prints one name per gateware target (host, aux, reference core, DFX cores, large cores), twice the same, and the names are
#     what lib.sh computes with the options of ci/vivado.env (host_key / core_key by hand)
#  2. a caller's stray option variables do not move a name
#  3. CI_BRANCH / CI_REF_TYPE win over GITHUB_REF_NAME / GITHUB_REF_TYPE in branch_slug and pin_entry
#  4. heavy_lock required refuses under CI without the lock directory; plain heavy_lock and runs without CI go on
#  5. cache_put leaves a world-readable entry directory and no temp directory
#  6. ci/vivado.env lines that are no build option (PATH, LD_PRELOAD, a value with a space or $(...)) end ci/keys.sh with exit 1 and the line number
#  7. the builder's way (T239.1 D14): the key code alone in a folder of its own runs with --data over a checkout of HEAD made as the builder makes it
#     (core.symlinks=false: a symlink is a plain file holding its link text). Its names are those of a plain checkout of HEAD, and no file of the read
#     tree runs or is written: every script and module the key code could start or import is planted there and leaves a marker when it runs.
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/.."
W="$(mktemp -d)"; trap 'rm -rf "${W:?}"' EXIT
fail() { echo "FAIL: $*"; exit 1; }

# 7, set up here so that its two runs go beside those of 1 and 2. The key code under test is this working tree's; KEYCODE is the list of files the
# builder copies from its trusted ref (a file missing here makes the --data run fail, which is the point).
KEYCODE="ci/keys.sh ci/lib.sh ci/pkg.env ci/code_hash.py ci/codehash-requirements.txt ci/plan.py ci/simreads.py tools/core_toml.py"
head_checkout() {   # head_checkout <dir> <core.symlinks true|false>: HEAD of this repository, no hooks, no history
  git init -q "$1" && git -C "$1" config core.symlinks "$2" && git -C "$1" fetch -q --no-tags --depth 1 "$PWD" HEAD \
    && git -C "$1" -c core.hooksPath=/dev/null checkout -q --detach FETCH_HEAD
}
head_checkout "$W/ref" true || fail "plain checkout of HEAD"
head_checkout "$W/data" false || fail "checkout of HEAD with core.symlinks=false"
for f in $KEYCODE; do
  mkdir -p "$W/code/$(dirname "$f")"; cp "$f" "$W/code/$f"; cp "$f" "$W/ref/$f"
done
[ -z "$(find "$W/data" -path "$W/data/.git" -prune -o -type l -print)" ] || fail "the core.symlinks=false checkout holds a symlink"
for f in ci/keys.sh ci/lib.sh ci/pkg.env; do printf 'touch "%s/ran.%s"\n' "$W" "${f//\//_}" > "$W/data/$f"; done
for f in ci/plan.py ci/code_hash.py ci/simreads.py tools/core_toml.py ci/json.py ci/re.py ci/subprocess.py ci/hashlib.py tools/json.py tools/re.py \
         json.py re.py subprocess.py hashlib.py sitecustomize.py; do
  printf 'open("%s/ran.%s", "w").close()\n' "$W" "${f//\//_}" > "$W/data/$f"
done
git -C "$W/data" status --porcelain --ignored --untracked-files=all > "$W/data_before.txt"

# four runs side by side (each hashes the whole tree, minutes): the plain one, one with option variables in the caller's environment, and the two of 7
ci/keys.sh > "$W/a.txt" & pa=$!
T79_ROUTE=zzz TRX_SID=other TRX_FEATURES=none ci/keys.sh > "$W/b.txt" & pb=$!
"$W/ref/ci/keys.sh" > "$W/r.txt" & pr=$!
"$W/code/ci/keys.sh" --data "$W/data" > "$W/s.txt" 2> "$W/s.err" & ps=$!
wait "$pa" || fail "ci/keys.sh failed"; wait "$pb" || fail "ci/keys.sh with stray options failed"
wait "$pr" || fail "ci/keys.sh in a plain checkout of HEAD failed"
wait "$ps" || fail "the key code with --data over the checkout failed: $(tail -3 "$W/s.err")"
a="$(cat "$W/a.txt")"; b="$(cat "$W/b.txt")"
[ "$a" = "$b" ] || fail "a stray TRX_SID / T79_ROUTE of the caller moved the names"
n="$(printf '%s\n' "$a" | wc -l | tr -d ' ')"
want=$(( 3 + $(python3 ci/plan.py lanes | grep -c '^core:') + $(python3 ci/plan.py lanes | grep -c '^large:') ))
[ "$n" = "$want" ] || fail "$n names, expected $want (host, aux, reference core, DFX cores, large cores)"
printf '%s\n' "$a" | grep -Eq '^host-[0-9a-f]{16}$' || fail "no host-<16 hex> name"
printf '%s\n' "$a" | grep -Eq '^aux-[0-9a-f]{16}$' || fail "no aux-<16 hex> name"
[ "$(printf '%s\n' "$a" | sort -u | wc -l | tr -d ' ')" = "$n" ] || fail "duplicate names"

# by hand with lib.sh: the host name and the reference core's name
cat > "$W/hand.sh" <<'HAND'
set -euo pipefail
source ci/lib.sh
while IFS='=' read -r k v; do case "$k" in ''|'#'*) continue ;; esac; export "$k=$v"; done < ci/vivado.env
part="$(sed -n 's/^  c64u) part=\([^;]*\);.*/\1/p' ci/bitstream.sh)"
H="$(host_key "$part")"; ref="$(layer_reference host/layers.txt)"
echo "host-$H"; echo "core-$ref-$(core_key "$H" "$(ooc_key "$part" "$ref")")"
HAND
by_hand="$(bash "$W/hand.sh")"
[ "$(printf '%s\n' "$a" | sed -n 1p)" = "$(printf '%s\n' "$by_hand" | sed -n 1p)" ] || fail "host name differs from host_key by hand"
[ "$(printf '%s\n' "$a" | sed -n 3p)" = "$(printf '%s\n' "$by_hand" | sed -n 2p)" ] || fail "reference core name differs from core_key by hand"

# 3 branch and tag
( source ci/lib.sh
  [ "$(CI_BRANCH=feat/x GITHUB_REF_NAME=builder-ref branch_slug)" = "feat_x" ] || exit 11
  [ "$(GITHUB_REF_NAME=a/b branch_slug)" = "a_b" ] || exit 12
  e="$W/e1"; mkdir -p "$e"
  CI_REF_TYPE=tag CI_BRANCH=v1.2 GITHUB_REF_NAME=builder-ref GITHUB_REF_TYPE=branch pin_entry "$e" grp
  [ -s "$e/pins/tag.v1.2" ] || exit 13
  e="$W/e2"; mkdir -p "$e"
  CI_BRANCH=feat/x GITHUB_REF_NAME=builder-ref pin_entry "$e" grp
  [ -s "$e/pins/branch.feat_x" ] || exit 14 ) || fail "branch / tag handling (code $?)"

# 4 heavy_lock: 'required' under CI without the lock directory refuses, plain and outside CI it lets the run go on (a Mac without /cache)
if [ ! -d /cache ]; then
  ( source ci/lib.sh; CI=true heavy_lock required ) 2>/dev/null && fail "heavy_lock ran under CI without /cache"
  ( source ci/lib.sh; CI=true heavy_lock ) || fail "plain heavy_lock refused under CI without /cache"
  ( source ci/lib.sh; unset CI; heavy_lock required ) || fail "heavy_lock failed outside CI without /cache"
else
  echo "note: /cache exists here, heavy_lock refusal not tried"
fi

# 5 cache_put
( source ci/lib.sh
  mkdir -p "$W/src/pins"; echo x > "$W/src/DONE"; echo y > "$W/src/pins/p"
  cache_put "$W/src" "$W/bit/host-1"; cache_put "$W/src" "$W/bit/host-1"
  [ -f "$W/bit/host-1/DONE" ] && [ -f "$W/bit/host-1/pins/p" ] || exit 31
  [ "$(ls "$W/bit")" = "host-1" ] || exit 32
  [ "$(stat -c %a "$W/bit/host-1" 2>/dev/null || stat -f %Lp "$W/bit/host-1")" = 755 ] || exit 33 ) || fail "cache_put (code $?)"
echo "keys test ok ($n names)"

# 6 the commit's own ci/vivado.env is data: only TRX_/T79_/T16_ names with plain values, anything else is exit 1 with the line number
printf '# c\n\nTRX_SID=redip2\nPATH=/tmp/x\n' > "$W/e_path"
printf 'T79_FLOOR=B\nLD_PRELOAD=/tmp/x.so\n' > "$W/e_ld"
printf 'TRX_A=a b\n' > "$W/e_space"
printf 'TRX_A=$(touch %s/pwned)\n' "$W" > "$W/e_sub"
printf 'T16_X=`id`\n' > "$W/e_tick"
printf 'CODE_HASH_LIBS=/tmp/x\n' > "$W/e_libs"
for c in path:4 ld:2 space:1 sub:1 tick:1 libs:1; do
  f="$W/e_${c%%:*}"
  rc=0; out="$(ci/keys.sh --env "$f" 2>&1 >/dev/null)" || rc=$?
  [ "$rc" = 1 ] || fail "ci/keys.sh --env $(basename "$f") exited $rc, expected 1"
  case "$out" in *"line ${c##*:} "*) ;; *) fail "$(basename "$f"): no 'line ${c##*:}' in: $out" ;; esac
done
[ ! -e "$W/pwned" ] || fail "a command substitution in ci/vivado.env ran"
echo "ok: keys.sh refuses lines that are no build option"

# 7 (the runs are above)
[ -s "$W/r.txt" ] || fail "no names from the plain checkout of HEAD"
[ "$(cat "$W/s.txt")" = "$(cat "$W/r.txt")" ] || fail "--data over the core.symlinks=false checkout gave other names than the plain checkout: $(diff "$W/r.txt" "$W/s.txt" | head -4)"
ran="$(cd "$W" && ls ran.* 2>/dev/null || true)"
[ -z "$ran" ] || fail "a file of the read tree ran inside the key run: $ran"
git -C "$W/data" status --porcelain --ignored --untracked-files=all | cmp -s - "$W/data_before.txt" || fail "the key run changed the read tree"
echo "ok: the key code from its own folder over a checkout it only reads ($(wc -l < "$W/s.txt" | tr -d ' ') names, equal to the plain checkout; nothing of the tree ran)"
