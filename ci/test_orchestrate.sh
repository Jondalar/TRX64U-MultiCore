#!/usr/bin/env bash
# T174 D4: the parent of the push pipeline (ci/orchestrate.py) against a fake Gitea, and `ci/orchestrate.py check` on the tree and on scratch copies.
# Every scenario has a negative control that has to fail:
#   1 DAG order        every child is dispatched only after each child it needs is done   control: a table without vivado's needs breaks the order
#   2 only planned     a vc20 change on main starts exactly its children                  control: an app change in the plan adds the app children
#   3 red stops        a red sim host leaves vivado host and core:vc20 not started        control: without the red they start, the parent is green
#   4 docs only        nothing to do, nothing dispatched                                   control: the same plan as code dispatches
#   5 branch scope     a feature branch: cor for the touched core, no package             control: scope full: package, no cor
#   6 never started    a child that stays queued is red "never started"                    control: one that starts is green
#   7 check            the tree passes; a workflow that does not check out inputs.sha, and the bench owner faults of ci/sims.lane, fail
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/.."
SRC="$PWD"
W="$(mktemp -d)"; trap 'rm -rf "${W:?}"' EXIT
fail() { echo "FAIL: $*"; exit 1; }
export GITHUB_RUN_ID=77

# a plan of this tree that names nothing; the scenarios edit it
python3 ci/plan.py make --base HEAD --head HEAD --branch main --source test > "$W/base.json" 2>/dev/null
mkplan() { # mkplan <out> <python edit of p>
  python3 - "$W/base.json" "$1" "$2" <<'PY'
import json, sys
p = json.load(open(sys.argv[1])); exec(sys.argv[3]); json.dump(p, open(sys.argv[2], "w"))
PY
}
fake() { # fake <plan> <red ids> <never ids> [table]: run the parent against the fake Gitea; rc in $W/rc, output in $W/out, log in $W/log
  python3 -c 'import json,sys; json.dump({"red": sys.argv[1].split(), "never": sys.argv[2].split(), "log": sys.argv[3]}, open(sys.argv[4], "w"))' "$2" "$3" "$W/log" "$W/spec.json"
  rm -f "$W/log"; touch "$W/log"
  local rc=0
  ORCH_TABLE="${4:-}" CI_CHILD_START_MIN="${START_MIN:-240}" python3 ci/orchestrate.py run "$1" --tier 1 --fake "$W/spec.json" --lint true > "$W/out" 2>&1 || rc=$?
  echo "$rc" > "$W/rc"
}
dispatched() { awk '$1 == "dispatch" { print $3 }' "$W/log" | sort | tr '\n' ' '; }
order_ok() { # order_ok <plan>: with the REAL table's needs, every dispatched child came after the "done" of each child it needs
  python3 ci/orchestrate.py graph "$1" --tier 1 > "$W/graph"
  python3 - "$W/graph" "$W/log" <<'PY'
import sys
needs = {}
for l in open(sys.argv[1]):
    i, _, n = l.strip().partition(" <- ")
    needs[i] = [] if n == "-" else n.split()
done = set()
for l in open(sys.argv[2]):
    f = l.split()
    if f[0] == "done":
        done.add(f[2])
    elif f[0] == "dispatch":
        bad = [d for d in needs.get(f[2], []) if d != "lint" and d not in done]
        if bad:
            print("%s dispatched before %s was done" % (f[2], " ".join(bad))); sys.exit(1)
PY
}

# main, vc20's gateware and a host change, a host and a vc20 bench
mkplan "$W/main.json" 'p.update(host=True, dfx=["null", "vc20"], sims=["trx_io", "vc20"])'

# 1 DAG order
fake "$W/main.json" "" ""
[ "$(cat "$W/rc")" = 0 ] || fail "1: the green plan ended red: $(cat "$W/out")"
order_ok "$W/main.json" || fail "1: DAG order"
grep -q "vivado/core:vc20 <- sim/core:vc20 vivado/host" "$W/graph" || fail "1: vivado core:vc20 does not need its sim and the host: $(cat "$W/graph")"
# control: a table where vivado needs nothing - the order against the real needs must break
sed 's/^vivado     vivado.yml     gateware      .*/vivado     vivado.yml     gateware      -/' ci/children.txt > "$W/children.bad"
fake "$W/main.json" "" "" "$W/children.bad"
if order_ok "$W/main.json" > /dev/null; then fail "1 control: a table without vivado's needs kept the order"; fi
echo "1 DAG order ok (control: a table without vivado's needs breaks it)"

# 2 only planned children
fake "$W/main.json" "" ""
got="$(dispatched)"
# main is fundamental (D2.6: the full app benches), the package needs the images (it packs them); no app changed, so no emu; no update asked for
[ "$got" = "apptests images package sim/core:vc20 sim/host vivado/core:null vivado/core:vc20 vivado/host " ] || fail "2: dispatched: $got"
mkplan "$W/app.json" 'p.update(host=True, dfx=["null", "vc20"], sims=["trx_io", "vc20"]); p["apps"]["core"] = True'
fake "$W/app.json" "" ""
[ "$(dispatched)" != "$got" ] || fail "2 control: an app change dispatched the same children"
dispatched | grep -q "emu " || fail "2 control: an app change did not start emu: $(dispatched)"
echo "2 only planned ok: $got(control: an app change adds emu)"

# 3 a red child stops its dependents
fake "$W/main.json" "sim/host" ""
[ "$(cat "$W/rc")" = 1 ] || fail "3: a red child left the parent green"
dispatched | grep -q "vivado/host " && fail "3: vivado host started behind a red sim host"
dispatched | grep -q "vivado/core:vc20 " && fail "3: vivado core:vc20 started behind a red host chain"
grep -qE "^vivado host +- +not started +- +not started: sim host red" "$W/out" || fail "3: the table does not say why vivado host did not start: $(cat "$W/out")"
grep -qE "^vivado core:vc20 .*not started: sim host red" "$W/out" || fail "3: the table does not name the root red for vivado core:vc20"
grep -qE "^sim host .*RED .*fake red sim/host" "$W/out" || fail "3: the red child has no reason in the table"
dispatched | grep -q "sim/core:vc20 " || fail "3: an independent child was not started"
fake "$W/main.json" "" ""
[ "$(cat "$W/rc")" = 0 ] && dispatched | grep -q "vivado/host " || fail "3 control: without the red the host was not built"
echo "3 red stops dependents ok (control: without the red they start)"

# 4 docs only
mkplan "$W/docs.json" 'p.update(docs_only=True, host=True, dfx=["vc20"])'
fake "$W/docs.json" "" ""
grep -qx "nothing to do (docs only)" "$W/out" && [ "$(cat "$W/rc")" = 0 ] && [ ! -s "$W/log" ] || fail "4: docs only: $(cat "$W/out")"
mkplan "$W/code.json" 'p.update(docs_only=False, host=True, dfx=["vc20"])'
fake "$W/code.json" "" ""
[ -s "$W/log" ] || fail "4 control: the same plan as code dispatched nothing"
echo "4 docs only ok (control: as code it dispatches)"

# 5 feature-branch scope
mkplan "$W/branch.json" 'p.update(scope="branch", branch="feat/x", host=True, dfx=["null", "vc20"], sims=[])'
fake "$W/branch.json" "" ""
got="$(dispatched)"
[ "$got" = "cor/core:vc20 images vivado/core:null vivado/core:vc20 vivado/host " ] || fail "5: branch: $got"
mkplan "$W/full.json" 'p.update(scope="full", host=True, dfx=["null", "vc20"], sims=[])'
fake "$W/full.json" "" ""
dispatched | grep -q "package " && ! dispatched | grep -q "cor/" || fail "5 control: scope full: $(dispatched)"
echo "5 branch scope ok: $got(control: full packs, no cor)"

# 6 a child that never starts
START_MIN=0 fake "$W/main.json" "" "sim/host"
grep -qE "^sim host .*RED .*never started" "$W/out" && [ "$(cat "$W/rc")" = 1 ] || fail "6: a child that never started: $(cat "$W/out")"
grep -q "^cancel " "$W/log" || fail "6: the parent did not try to cancel the child that never started"
START_MIN=0 fake "$W/main.json" "" "" || true
[ "$(cat "$W/rc")" = 0 ] || fail "6 control: a child that starts was taken for never started"
echo "6 never started ok (control: a started child is green)"

# 7 check on the tree and its controls
python3 ci/orchestrate.py check > "$W/chk" || { cat "$W/chk"; fail "7: the tree fails ci/orchestrate.py check"; }
S="$W/s"; mkdir -p "$S/.gitea"; cp -R ci tools "$S/"; cp -R .gitea/workflows "$S/.gitea/"; mkdir -p "$S/host"; cp host/layers.txt "$S/host/"
(find cores -name core.toml -o -name core_sources.tcl) | while IFS= read -r f; do mkdir -p "$S/$(dirname "$f")"; cp "$f" "$S/$f"; done
python3 "$S/ci/orchestrate.py" check > /dev/null || fail "7: the scratch copy fails before the controls"
sed -i.bak 's/SHA: \${{ inputs.sha || github.sha }}/SHA: ${{ github.sha }}/' "$S/.gitea/workflows/emu.yml"
if python3 "$S/ci/orchestrate.py" check > "$W/c1" 2>&1; then fail "7 control: a child that checks out the branch head passed"; fi
grep -q "emu.yml: the child does not check out inputs.sha" "$W/c1" || fail "7 control: not named: $(cat "$W/c1")"
cp "$S/.gitea/workflows/emu.yml.bak" "$S/.gitea/workflows/emu.yml"; rm "$S/.gitea/workflows/emu.yml.bak"
printf '  verdict:\n    uses: ./.gitea/workflows/verdict.yml\n' >> "$S/.gitea/workflows/pipeline.yml"
if python3 "$S/ci/orchestrate.py" check > "$W/c2" 2>&1; then fail "7 control: a pipeline with a second job passed"; fi
cp .gitea/workflows/pipeline.yml "$S/.gitea/workflows/pipeline.yml"
ownedit() { # ownedit <python edit of the lines list>: edit the scratch ci/sims.lane, run the check, restore
  cp "$S/ci/sims.lane" "$W/sims.lane.good"
  python3 - "$S/ci/sims.lane" "$1" <<'PY'
import sys
p = sys.argv[1]; lines = open(p).read().split("\n"); exec(sys.argv[2]); open(p, "w").write("\n".join(lines))
PY
  local rc=0; python3 "$S/ci/orchestrate.py" check > "$W/outb" 2>&1 || rc=$?
  cp "$W/sims.lane.good" "$S/ci/sims.lane"; return $rc
}
ownedit 'lines = [l for l in lines if not l.startswith("trx_io:")]' && fail "7: a bench without an owner passed"
grep -q "bench trx_io has no owner" "$W/outb" || fail "7: the missing owner is not named: $(cat "$W/outb")"
ownedit 'lines.append("trx_io: core:vc20")' && fail "7: a bench with two owners passed"
ownedit 'lines.append("nosuchbench: host")' && fail "7: a row of no bench passed"
ownedit 'lines = [l.replace("vc20: core:vc20", "vc20: core:nosuchcore") for l in lines]' && fail "7: an owner that is no lane passed"
ownedit 'lines = [l.replace("vc20: core:vc20", "vc20: core:atari800") for l in lines]' && fail "7: a bench owned by a core its deps do not read passed"
ownedit 'pass' || fail "7 control: the unedited table failed"
echo "7 check ok (controls: a child on the branch head, a second pipeline job, five bench owner faults)"
echo "orchestrate test ok"
