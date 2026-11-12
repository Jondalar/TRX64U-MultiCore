#!/usr/bin/env bash
# Self-test of the test cache (T100 D13, D14): a throw-away project with two test programs that share a header (X, Y), a test built and run
# by a shell function (W), a render-like bench that reads LVGL-like headers, a scenario and a golden (R), and a negative control for X and Y.
# A small suite script runs them through the real helpers (tests/ctlcache.sh, ctlcache.py, readlog.c) and the scenarios below change one thing
# and read off, from the log lines of the next run, which units ran and which came from the cache:
#   d  nothing changed                       -> every unit cached ("ran 0")
#   a  a line in a file only X reads         -> X and its control run, Y, W, R and the other control are cached
#   b  a header X and Y both include         -> X, Y and both controls run, W and R are cached (the dependency list, not a map)
#   c  a golden that only X opens at run time -> X runs;  a golden / scenario / header of R -> R runs and nothing else
#   e  a file Y opens by a relative path nobody declared -> Y runs (found by the recorder), nothing else
#   f  a test that fails is not stored       -> it fails again on the next run
#   g  no dependency list                    -> no key, and no key is never a hit
# NEGATIVE CONTROLS of the cache itself: a helper that records no reads, a key that ignores the content of the files, and a verify that always
# agrees must each make these assertions go red. The recorder is an LD_PRELOAD (Linux): without it only d, a, b and g are asserted, the
# suite directory's data files and tools/*.py are part of the key (the coarse mode) and a shell function is not cached.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
W="$(mktemp -d)"; trap '[ -z "${SELFTEST_KEEP:-}" ] || cp -R "$W" "$SELFTEST_KEEP"; rm -rf "$W"' EXIT
export CXX="${CXX:-c++}"
unset CONTROLS_ALL CC_DRYRUN UPDATE_GOLDEN   # tools/dev.sh integrate exports CONTROLS_ALL=1, which makes the helper never hit the cache: every "cached" assertion went red
if ! command -v "$CXX" >/dev/null || ! command -v python3 >/dev/null; then echo "ctlcache selftest SKIPPED out loud: no $CXX or no python3"; exit 0; fi
MODE=coarse
if [ "$(uname -s)" = Linux ] && command -v cc >/dev/null && cc -shared -fPIC -O1 -o "$W/probe.so" "$ROOT/tests/readlog.c" -ldl 2>/dev/null; then MODE=dyn; fi

mkproject() {   # mkproject DIR PYFILE : the project, with the helper the caller names
  local P="$1" PY="$2"
  rm -rf "$P"; mkdir -p "$P/tests/s/golden" "$P/golden" "$P/data" "$P/lvgl" "$P/scen" "$P/tools"
  cp "$ROOT/tests/ctlcache.sh" "$ROOT/tests/readlog.c" "$P/"; cp "$PY" "$P/ctlcache.py"
  printf 'static inline int shared(void) { return 2; }\n' > "$P/shared.h"
  printf 'int xf(void) { return 1; }\n' > "$P/x.cc"
  printf 'int yf(void) { return 4; }\n' > "$P/y.cc"
  cat > "$P/t_x.cc" <<'EOF'
#include <stdio.h>
#include "shared.h"
int xf(void);
int main(void) {
  FILE *f = fopen(DATA "/gx.txt", "r"); int c = f ? fgetc(f) : 0; if (f) fclose(f);
  if (c != 'G') { puts("FAIL golden"); return 1; }
  if (xf() + shared() != 3) { puts("FAIL sum"); return 1; }
  puts("x ok"); return 0;
}
EOF
  cat > "$P/t_y.cc" <<'EOF'
#include <stdio.h>
#include "shared.h"
int yf(void);
int main(void) {
  FILE *f = fopen("data/extra.txt", "r"); int c = f ? fgetc(f) : 0; if (f) fclose(f);   /* a relative path nobody declared */
  if (c != 'E') { puts("FAIL extra"); return 1; }
  if (yf() + shared() != 6) { puts("FAIL sum"); return 1; }
  puts("y ok"); return 0;
}
EOF
  printf '#include "w.h"\nint main(void) { return WV == 5 ? 0 : 1; }\n' > "$P/w.cc"
  printf '#define WV 5\n' > "$P/w.h"
  printf '#define LV_VER 9\n' > "$P/lvgl/lv.h"
  cat > "$P/r.cc" <<'EOF'
#include <stdio.h>
#include <lv.h>
int main(int argc, char **argv) {
  char a[16] = {0}, b[16] = {0};
  FILE *f = fopen(argv[1], "r"); if (f) { if (!fgets(a, sizeof a, f)) a[0] = 0; fclose(f); }
  f = fopen(argv[2], "r"); if (f) { if (!fgets(b, sizeof b, f)) b[0] = 0; fclose(f); }
  if (a[0] != 'S' || b[0] != 'R' || LV_VER != 9) { puts("FAIL render"); return 1; }
  puts("r ok"); return 0;
}
EOF
  printf 'GX\n' > "$P/golden/gx.txt"; printf 'E\n' > "$P/data/extra.txt"; printf 'S1\n' > "$P/scen/sc.txt"; printf 'R1\n' > "$P/golden/gr.txt"
  cat > "$P/suite.sh" <<'EOF'
#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")" && pwd)"; cd "$ROOT"
T="$(mktemp -d)"; trap 'cc_summary; rm -rf "$T"' EXIT
export CONTROLS_CACHE="$ROOT/../cache"
source "$ROOT/ctlcache.sh"; CC_SUITE=s
FLAGS="-std=gnu++11 -Wall -DDATA=\"$ROOT/golden\" -I$ROOT"
unit_test() {   # NAME TEST SRC... : like coreapp's run
  local name="$1" test="$2"; shift 2
  local key t0=$SECONDS; key="$(cc_key s "test $name" -- $FLAGS -- "$ROOT/$test" "${@/#/$ROOT/}")"
  if cc_cached "$name" "$key" test; then return 0; fi
  $CXX $FLAGS -o "$T/$name" "$ROOT/$test" "${@/#/$ROOT/}"
  cc_exec "$T/rl_$name" "$T/$name"
  echo "test $name: PASS $((SECONDS - t0))s"
  cc_store "$name" "$key" "$T/rl_$name" test
}
unit_control() {   # NAME FILE SED TEST SRC... : like coreapp's control_job
  local name="$1" file="$2" expr="$3" test="$4"; shift 4
  local key; key="$(cc_key s "control $name $file $expr $test $*" -- $FLAGS -- "$ROOT/$test" "${@/#/$ROOT/}")"
  if cc_cached "$name" "$key"; then return 0; fi
  local d="$T/mut_$name"; mkdir -p "$d"; cp "$ROOT"/*.cc "$ROOT"/*.h "$d"/
  sed -e "$expr" "$d/$file" > "$d/$file.sed" && mv "$d/$file.sed" "$d/$file"
  $CXX $FLAGS -I"$d" -o "$T/x_$name" "$d/$test" ${@/#/$d/}
  if cc_exec "$T/rl_$name" "$T/x_$name" > "$T/x_$name.log" 2>&1; then echo "CONTROL $name: PASSED against the mutant" >&2; exit 1; fi
  grep -q '^FAIL' "$T/x_$name.log" || { echo "CONTROL $name: no red check" >&2; exit 1; }
  echo "control $name: fails as it must"
  cc_store "$name" "$key" "$T/rl_$name"
}
t_w() { $CXX -std=gnu++11 -o "$T/w" "$ROOT/w.cc" -I"$ROOT" && "$T/w"; }
t_r() { $CXX -std=gnu++11 -o "$T/r" "$ROOT/r.cc" -I"$ROOT/lvgl" && "$T/r" "$ROOT/scen/sc.txt" "$ROOT/golden/gr.txt"; }
unit_test X t_x.cc x.cc
unit_control CX x.cc 's/return 1;/return 7;/' t_x.cc x.cc
unit_test Y t_y.cc y.cc
unit_control CY y.cc 's/return 4;/return 9;/' t_y.cc y.cc
cc_block W t_w
cc_block R t_r
EOF
  chmod +x "$P/suite.sh"
}

run_suite() {   # run_suite PROJECT OUTFILE : the suite's exit code
  ( set +e; "$1/suite.sh" > "$2" 2>&1; echo $? ) | tail -1
}

scenarios() {   # scenarios PYFILE : runs every scenario with that helper, prints the failed assertions, returns their number
  local P="$W/p" PY="$1" FAILS=0 out n=0 rc
  mkproject "$P" "$PY"
  step() { n=$((n + 1)); out="$W/out.$n"; rc="$(run_suite "$P" "$out")"; }
  ran() { grep -Eq "^(test $1: PASS|control $1: fails as it must$|test $1: reads|control $1: reads)" "$out"; }
  cached() { grep -Eq "^(test $1: cached PASS|control $1: cached)" "$out"; }
  expect() { if ! eval "$2"; then echo "  FAIL $1"; FAILS=$((FAILS + 1)); fi; }
  rm -rf "$W/cache"; mkdir -p "$W/cache"   # the project's cache directory is its sibling (suite.sh: $ROOT/../cache)
  step
  expect "cold: the suite passes" '[ "$rc" = 0 ]'
  expect "cold: X, Y, both controls ran" 'ran X && ran Y && ran CX && ran CY'
  step
  expect "d: nothing changed: X Y CX CY cached" 'cached X && cached Y && cached CX && cached CY'
  expect "d: nothing changed: the suite passes" '[ "$rc" = 0 ]'
  if [ "$MODE" = dyn ]; then
    expect "d: nothing changed: W and R cached, ran 0" 'cached W && cached R && grep -q "ran 0 " "$out"'
  fi
  # k (T174 D3): the harness is in a key by CC_KEY_VERSION, not by its bytes - a comment-only edit of ctlcache.sh invalidates nothing
  printf '# a comment, nothing else (T174 D3)\n' >> "$P/ctlcache.sh"
  step
  expect "k: a comment-only edit of ctlcache.sh: X, Y and both controls cached" 'cached X && cached Y && cached CX && cached CY'
  [ "$MODE" = dyn ] && expect "k: a comment-only edit of ctlcache.sh: W and R cached, ran 0" 'cached W && cached R && grep -q "ran 0 " "$out"'
  printf 'int xf(void) { return 1; } /* edited */\nint xf_unused(void) { return 0; }\n' > "$P/x.cc"
  step
  expect "a: X's own source changed: X and CX ran" 'ran X && ran CX'
  expect "a: Y and CY stay cached" 'cached Y && cached CY'
  [ "$MODE" = dyn ] && expect "a: W and R stay cached" 'cached W && cached R'
  printf 'static inline int shared(void) { return 2; }\n#define SHARED_EDITED 1\n' > "$P/shared.h"
  step
  expect "b: the shared header changed: X, Y and both controls ran" 'ran X && ran Y && ran CX && ran CY'
  [ "$MODE" = dyn ] && expect "b: W and R do not include it: cached" 'cached W && cached R'
  step
  expect "b2: and now all four are cached again" 'cached X && cached Y && cached CX && cached CY'
  if [ "$MODE" = dyn ]; then
    printf 'GX edited\n' > "$P/golden/gx.txt"
    step
    expect "c: X's golden changed: X ran" 'ran X'
    expect "c: Y, W, R stay cached" 'cached Y && cached W && cached R'
    printf 'R2\n' > "$P/golden/gr.txt"
    step
    expect "c: R's golden changed: R ran, nothing else" 'ran R && cached X && cached Y && cached W'
    printf 'S2\n' > "$P/scen/sc.txt"
    step
    expect "c: R's scenario changed: R ran, nothing else" 'ran R && cached X && cached Y && cached W'
    printf '#define LV_VER 9\n#define LV_EDITED 1\n' > "$P/lvgl/lv.h"
    step
    expect "c: LVGL's header changed: R ran, nothing else" 'ran R && cached X && cached Y && cached W'
    printf '#define WV 5\n#define W_EDITED 1\n' > "$P/w.h"
    step
    expect "c: W's header changed: W ran, nothing else" 'ran W && cached R && cached X && cached Y'
    printf 'E edited\n' > "$P/data/extra.txt"
    step
    expect "e: a file Y opens by a relative path changed: Y ran" 'ran Y'
    expect "e: X and R stay cached" 'cached X && cached R'
    printf 'B\n' > "$P/golden/gx.txt"
    step
    expect "f: a failing test fails the suite" '[ "$rc" != 0 ]'
    step
    expect "f: and fails again, it was not stored" '[ "$rc" != 0 ] && ! cached X'
    printf 'GX\n' > "$P/golden/gx.txt"
  fi
  # g: no dependency list, no key; no key is never a hit
  ( ROOT="$P"; T="$W/tg"; mkdir -p "$T"; CONTROLS_CACHE="$W/cache"; source "$P/ctlcache.sh"
    [ -z "$(cc_key s x -- -std=gnu++11 -- "$P/nonexistent.cc")" ] && ! cc_cached x "" >/dev/null ) || { echo "  FAIL g: no key, no hit"; FAILS=$((FAILS + 1)); }
  return "$FAILS"
}

echo "ctlcache selftest ($MODE mode): the helper"
scenarios "$ROOT/tests/ctlcache.py" || { echo "SELFTEST FAIL: the real helper does not pass its own test" >&2; exit 1; }
# NEGATIVE CONTROLS: break the helper on purpose, the same assertions must go red
sed 's|(sha(f).encode() if os.path.isfile(f) else b"missing")|b"x"|' "$ROOT/tests/ctlcache.py" > "$W/nosrc.py"                    # a key that ignores the content of the files
sed 's|^    print("\\n".join(sorted(manifest)))|    print("")|' "$ROOT/tests/ctlcache.py" > "$W/noreads.py"                       # a manifest that records no reads
sed 's|^        if now != ln:|        if False:|' "$ROOT/tests/ctlcache.py" > "$W/noverify.py"                                # a verify that always agrees
sed 's|ctx.norm(a.id) + "\\0"|"" + "\\0"|' "$ROOT/tests/ctlcache.py" > "$W/noid.py"                                                # a key that ignores the unit's id (the sed)
sed 's|^    files = set()$|    files = {os.path.join(os.path.dirname(os.path.realpath(__file__)), "ctlcache.sh")}|' "$ROOT/tests/ctlcache.py" > "$W/bytes.py"       # a key that carries the harness's bytes again (the pre-D3 key)
for m in nosrc noreads noverify noid bytes; do
  cmp -s "$W/$m.py" "$ROOT/tests/ctlcache.py" && { echo "SELFTEST: mutant $m changed nothing" >&2; exit 1; }
  if [ "$MODE" != dyn ] && { [ "$m" = noreads ] || [ "$m" = noverify ]; }; then continue; fi   # these two act on the recorder only
  if scenarios "$W/$m.py" > "$W/$m.log" 2>&1; then echo "SELFTEST FAIL: a cache with $m passed the test" >&2; cat "$W/$m.log" >&2; exit 1; fi
  echo "  negative control $m: the test goes red ($(grep -c FAIL "$W/$m.log") assertion(s))"
done
# T174 D3: a change to the key / verdict logic without a raised CC_KEY_VERSION is refused (the checker has its own controls)
python3 -I "$ROOT/tools/check_cc_key_version.py" selftest || { echo "SELFTEST FAIL: the key version check" >&2; exit 1; }
echo "ctlcache selftest: ok"
