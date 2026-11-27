#!/usr/bin/env bash
# T100 D12 on ci/apptests.sh and tests/parallel.sh, no compiler: (1) the key of a suite follows the content of what it reads - a changed
# coreapp source moves it, an unchanged tree does not, a change outside a suite's inputs leaves that suite's key alone; (2) the marker
# flow - a PASS is cached, a changed source runs again, a FAIL leaves no marker, -f runs again; (3) the parallel controls - the output
# is in submission order, a failing job fails the suite, and JOBS jobs really overlap. Every check has its control: the same
# assertion on the wrong state must be false.
set -euo pipefail
# T215: the key checks below change a source by appending a code line and compare keys many times; a normalised key hashes the whole tree through
# tree-sitter (about 55 s each on the NAS), so the plumbing here runs with the raw content hash. The normaliser itself is tested by
# ci/test_code_hash.py.
export CODE_HASH=raw
cd "$(dirname "${BASH_SOURCE[0]}")/.."
SRC="$PWD"
W="$(mktemp -d)"; trap 'rm -rf "${W:?}"' EXIT
fail() { echo "FAIL: $*"; exit 1; }

git clone -q "$SRC" "$W/repo" 2>/dev/null
for d in ci tests; do rm -rf "${W:?}/repo/$d"; cp -R "$SRC/$d" "$W/repo/$d"; done
R="$W/repo"
G() { git -C "$R" -c user.name=t -c user.email=t@t "$@"; }
G add -A; G commit -q --allow-empty -m scratch
K() { ( cd "$R" && ci/apptests.sh --key "$1" 2>/dev/null ); }

# (1) keys
c0="$(K coreapp)"; h0="$(K hostapp)"; l0="$(K loader)"
[ "$c0" = "$(K coreapp)" ] || fail "the same tree gave two keys"
echo "int x_$RANDOM;" >> "$R/coreapp/src/ops.cc"
c1="$(K coreapp)"
[ "$c1" != "$c0" ] || fail "a changed coreapp source did not move the coreapp key"
[ "$(K loader)" != "$l0" ] || fail "a changed coreapp source did not move the loader key"
echo "ok   key: unchanged tree same key, coreapp source moves coreapp and loader"
G checkout -q coreapp/src/ops.cc
[ "$(K coreapp)" = "$c0" ] || fail "restoring the source did not restore the key"
echo ": d12 $RANDOM" >> "$R/tests/coreapp/run.sh"
[ "$(K coreapp)" != "$c0" ] || fail "a changed run.sh did not move the coreapp key"
[ "$(K hostapp)" = "$h0" ] || fail "a change of tests/coreapp moved the hostapp key"
echo "ok   key: tests/coreapp moves coreapp, not hostapp"
G checkout -q tests/coreapp/run.sh

# (1b) T174 D3: the core application tests are one suite per group - a change to one core's file moves that core's key and `cross`, no other group's
a0="$(K coreapp:snes)"; v0="$(K coreapp:apple2)"; x0="$(K coreapp:cross)"; sh0="$(K coreapp)"
echo "int x_$RANDOM;" >> "$R/coreapp/cores/apple2.cc"
[ "$(K coreapp:apple2)" != "$v0" ] || fail "a changed apple2.cc did not move the coreapp:apple2 key"
[ "$(K coreapp:cross)" != "$x0" ] || fail "a changed apple2.cc did not move the coreapp:cross key (its units link every core)"
[ "$(K coreapp:snes)" = "$a0" ] || fail "a changed apple2.cc moved the coreapp:snes key"
[ "$(K coreapp)" = "$sh0" ] || fail "a changed apple2.cc moved the shared coreapp key"
G checkout -q coreapp/cores/apple2.cc
echo "int x_$RANDOM;" >> "$R/coreapp/src/ops.cc"
for pair in "coreapp=$sh0" "coreapp:cross=$x0" "coreapp:apple2=$v0" "coreapp:snes=$a0"; do
  [ "$(K "${pair%%=*}")" != "${pair#*=}" ] || fail "a shared source (coreapp/src/ops.cc) did not move ${pair%%=*}"
done
G checkout -q coreapp/src/ops.cc
# control: with the exclusions gone (every group keyed over the whole tree) the same assertion has to be false
sed -i.bak '/core_excl "\${1#coreapp:}"/s/; core_excl "[^"]*"//' "$R/ci/apptests.sh"
cmp -s "$R/ci/apptests.sh" "$R/ci/apptests.sh.bak" && fail "control: the sed changed nothing"
rm -f "$R/ci/apptests.sh.bak"
a1="$(K coreapp:snes)"; echo "int x_$RANDOM;" >> "$R/coreapp/cores/apple2.cc"
[ "$(K coreapp:snes)" != "$a1" ] || fail "control: without the exclusions an apple2 change still left coreapp:snes alone, the check cannot tell"
G checkout -q coreapp/cores/apple2.cc ci/apptests.sh
# the harness is in a suite key by CC_KEY_VERSION: a comment-only edit of tests/ctlcache.sh moves nothing, a raised number moves every suite
echo "# a comment $RANDOM" >> "$R/tests/ctlcache.sh"
[ "$(K coreapp:apple2)" = "$v0" ] && [ "$(K hostapp)" = "$h0" ] || fail "a comment-only edit of tests/ctlcache.sh moved a suite key"
sed -i.bak 's/^CC_KEY_VERSION=\([0-9]*\)/CC_KEY_VERSION=9\1/' "$R/tests/ctlcache.sh"; rm -f "$R/tests/ctlcache.sh.bak"
[ "$(K coreapp:apple2)" != "$v0" ] && [ "$(K hostapp)" != "$h0" ] || fail "control: a raised CC_KEY_VERSION did not move the suite keys"
G checkout -q tests/ctlcache.sh
echo "ok   groups: a core's file moves that core's suite and cross; a shared source moves all; control: whole-tree keys would move snes"

# (2) markers, with stub suites
export CI_APPTESTS_OK="$W/ok"
for s in hostapp coreapp loader; do printf '#!/bin/sh\necho stub %s ran\n' "$s" > "$R/tests/$s/run.sh"; chmod +x "$R/tests/$s/run.sh"; done
G add -A; G commit -q -m stubs
run() { ( cd "$R" && ci/apptests.sh --tier integration "$@" 2>&1 ); }   # the tier is explicit: the flow must not depend on the branch name of the checkout
o="$(run coreapp)"; echo "$o" | grep -q "stub coreapp ran" || fail "first run did not run the suite: $o"
o="$(run coreapp)"; echo "$o" | grep -q "cached PASS" || fail "second run was not cached: $o"
echo "$o" | grep -q "stub coreapp ran" && fail "a cached suite ran"
o="$(run -f coreapp)"; echo "$o" | grep -q "stub coreapp ran" || fail "-f did not run the suite"
echo "int x_$RANDOM;" >> "$R/coreapp/src/ops.cc"
o="$(run coreapp)"; echo "$o" | grep -q "stub coreapp ran" || fail "a changed source was served from the cache"
echo "ok   marker: PASS cached, -f and a changed source run again"
printf '#!/bin/sh\necho stub fails\nexit 1\n' > "$R/tests/loader/run.sh"
if run loader >/dev/null; then fail "a failing suite passed"; fi
if run loader | grep -q "cached PASS"; then fail "a failing suite left a marker"; fi
ls "$W/ok" | grep -q '^loader\.' && fail "marker for a failing suite"
echo "ok   marker: a failing suite fails and writes none"

# (2a) T174 D3: a fast PASS is remembered for the fast tier only
runf() { ( cd "$R" && ci/apptests.sh --tier fast "$@" 2>&1 ); }
echo "int x_$RANDOM;" >> "$R/coreapp/src/ops.cc"
o="$(runf coreapp)"; echo "$o" | grep -q "stub coreapp ran" || fail "a changed source was not run by the fast tier: $o"
o="$(runf coreapp)"; echo "$o" | grep -q "cached fast PASS" || fail "the second fast run was not served by the fast marker: $o"
echo "$o" | grep -q "stub coreapp ran" && fail "a suite with a fast marker ran again"
o="$(run coreapp)"; echo "$o" | grep -q "stub coreapp ran" || fail "control: the integration tier took a fast marker for a PASS: $o"
echo "ok   fast marker: served in the fast tier only (control: integration still runs)"

# (2b) the one fundamental list (T174 D2.6): ci/fundamental.txt escalates the tier of a feature branch; no second list in the script
grep -qE '^fundamental=\([a-z]' "$SRC/ci/apptests.sh" && fail "ci/apptests.sh has its own fundamental list again"
G add -A; G commit -q --allow-empty -m before_fund
b0="$(G rev-parse HEAD)"
tierline() { ( cd "$R" && CI_BRANCH=feature/x ci/apptests.sh --base "$b0" hostapp 2>&1 | grep 'tier auto' ); }
echo "int x_$RANDOM;" >> "$R/coreapp/src/ops.cc"
tierline | grep -q 'tier auto -> fast' || fail "a plain coreapp change did not stay fast: $(tierline)"
G checkout -q coreapp/src/ops.cc
echo "// fund $RANDOM" >> "$R/app/lib/lib.mk"
tierline | grep -q 'tier auto -> integration' || fail "an [apptests] path (app/lib) did not escalate: $(tierline)"
G checkout -q app/lib/lib.mk
echo "int x_$RANDOM;" >> "$R/coreapp/src/desc.h"
tierline | grep -q 'tier auto -> integration' || fail "an [all] path (coreapp/src/desc.h) did not escalate: $(tierline)"
G checkout -q coreapp/src/desc.h
# the benches section is not the apptests list: cores/common is [benches] only
python3 "$R/ci/plan.py" fundamental apptests | grep -qx 'cores/common' && fail "a [benches] path leaked into the apptests list"
python3 "$R/ci/plan.py" fundamental benches | grep -qx 'app/view' || fail "an [all] path is missing from the benches list"
echo "ok   fundamental: one list in ci/fundamental.txt, [all] + [apptests] escalate, a plain change stays fast"

# (2c) T239.1: a CI checkout is shallow (depth 1: no origin/integration, no HEAD~1). The base comes from the plan's plan.json (fetched when missing); without
# any base nothing counts as untouched and every suite is planned. Control: the same checkout with a base from the plan sees the touched suite only.
echo "int x_$RANDOM;" >> "$R/coreapp/src/ops.cc"; G add -A; G commit -q -m touch_coreapp
S="$W/shallow"; git clone -q --depth 1 --no-local "file://$R" "$S" 2>/dev/null
git -C "$S" update-ref -d refs/remotes/origin/integration 2>/dev/null || true
git -C "$S" rev-parse -q --verify HEAD~1 >/dev/null 2>&1 && fail "scenario error: the shallow checkout has a HEAD~1"
HO="$W/handoff"; mkdir -p "$HO"; rm -f "$HO/plan.json"
o="$( cd "$S" && CI_HANDOFF="$HO" CI_BRANCH=feature/x ci/apptests.sh --plan hostapp 2>&1 )" || true
echo "$o" | grep -q "NO DIFF BASE" || fail "a shallow checkout without a base did not say NO DIFF BASE: $o"
echo "$o" | grep -q "apptests plan (tier integration" || fail "no base: the tier did not become integration: $o"
echo "$o" | grep -q "not touched by this branch" && fail "no base: a suite was skipped as untouched"
printf '{"version":1,"base":{"commit":"%s","source":"test"}}\n' "$b0" > "$HO/plan.json"
o="$( cd "$S" && CI_HANDOFF="$HO" CI_BRANCH=feature/x ci/apptests.sh --plan hostapp 2>&1 )" || true
echo "$o" | grep -q "base ${b0:0:8} from the plan" || fail "the plan's base was not used in a shallow checkout: $o"
echo "$o" | grep -q "tier auto -> fast, scoped" || fail "plan base: the branch was not scoped: $o"
echo "$o" | grep -qE "hostapp\[[1-9][0-9]* files\]" || fail "plan base: hostapp counts no touched file: $o"
printf '{"version":1,"base":{"commit":"none","source":"test"}}\n' > "$HO/plan.json"
o="$( cd "$S" && CI_HANDOFF="$HO" CI_BRANCH=feature/x ci/apptests.sh --plan hostapp 2>&1 )" || true
echo "$o" | grep -q "NO DIFF BASE" || fail "base none in the plan did not fall to every-suite-runs: $o"
echo "ok   base: shallow checkout takes the plan's base and sees the touched suite; no base -> every suite runs, never skipped"
G reset -q --hard HEAD~1

# (3) parallel controls
par() {   # par JOBS FAILAT N -> prints the output of N fake controls
  ( T="$W/t$RANDOM"; mkdir -p "$T"; JOBS="$1"; export FAILAT="$2"
    source "$SRC/tests/parallel.sh"
    fake() { sleep "${NAP:-0.$((RANDOM % 4))}"; echo "job $1"; [ "$1" = "$FAILAT" ] && exit 1; return 0; }
    for i in $(seq 1 "$3"); do pjob_spawn fake "$i"; done
    pjob_drain )
}
exp="$(seq 1 8 | sed 's/^/job /')"
[ "$(par 3 0 8)" = "$exp" ] || fail "output of 8 jobs at 3 a time is not in submission order"
if out="$(par 3 4 8 2>/dev/null)"; then fail "a failing job did not fail the suite"; fi
echo "$out" | grep -q "job 4" || fail "the failing job's output is lost"
if echo "$out" | grep -q "job 6"; then fail "jobs after the failing one were still printed"; fi
# overlap: four 1 s jobs at JOBS=4 take about 1 s; the control, the same four at JOBS=1, has to take about 4 s
s0=$SECONDS; NAP=1 par 4 0 4 >/dev/null; el=$((SECONDS - s0))
s1=$SECONDS; NAP=1 par 1 0 4 >/dev/null; el1=$((SECONDS - s1))
[ "$el" -le 2 ] || fail "JOBS=4 did not overlap: $el s for four 1 s jobs"
[ "$el1" -ge 4 ] || fail "control: JOBS=1 took only $el1 s, the timing check cannot tell serial from parallel"
echo "ok   parallel: stable order, a failing job fails the suite"
echo "test_apptests: all scenarios hold"
