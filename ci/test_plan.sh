#!/usr/bin/env bash
# T100 D9 scenarios D9-1 .. D9-6 on ci/plan.py / ci/plan.sh, no Vivado: a scratch clone of this tree, one commit per scenario, the plan of
# (the commit before)..(the commit) read back. Every scenario has a NEGATIVE CONTROL: the same check, run on a plan that is wrong in the
# way the scenario forbids (a DFX core dropped on a host change, a core added to a core-only change, ...), must FAIL - a check that
# cannot fail proves nothing. D9-7 (cache fallback), D9-8 (time limit) and D9.6 (red targets) need the stage and live in
# ci/test_layers_flow.sh with the fake Vivado.
set -euo pipefail
# T215: this test moves keys by appending a comment line to a source; it tests the plumbing (stage flow, plan, layer keys), not the normaliser, so keys are
# the raw content hash here (the normaliser has its own test: ci/test_code_hash.py).
export CODE_HASH=raw
cd "$(dirname "${BASH_SOURCE[0]}")/.."
SRC="$PWD"
W="$(mktemp -d)"; trap 'rm -rf "${W:?}"' EXIT
git clone -q "$SRC" "$W/repo" 2>/dev/null
git -C "$SRC" ls-files -z -m -o --exclude-standard -- ci host cores vendor tools coreapp .gitea | while IFS= read -r -d '' f; do
  [ -f "$f" ] && { mkdir -p "$W/repo/$(dirname "$f")"; cp "$f" "$W/repo/$f"; }
done
R="$W/repo"
G() { git -C "$R" -c user.name=t -c user.email=t@t "$@"; }
G add -A; G commit -q --allow-empty -m scratch
G checkout -q -b scratch
fail() { echo "FAIL: $*"; exit 1; }

chg() { # chg <file>...: one commit that touches exactly those files; prints its id
  local f; for f in "$@"; do echo "// d9 test $RANDOM" >> "$R/$f"; G add "$f"; done
  G commit -q -m "change $*"; G rev-parse HEAD
}
# D2 (T174): a plan is for a branch; the scenarios of D9/D10/D11 are about the full scope (main), the D2 ones below name the branch they mean
mkplan() { python3 "$R/ci/plan.py" make --base "$1" --branch "${PLAN_BRANCH:-main}" --out "$W/p.json" 2>/dev/null || fail "plan.py make --base $1"; }
# chk <python expression over p (the plan) and U (its universe)>: 0 when true
chk() { python3 - "$W/p.json" "$1" <<'PY'
import json, sys
p = json.load(open(sys.argv[1])); U = p["universe"]
sys.exit(0 if eval(sys.argv[2]) else 1)
PY
}
# control <plan edit, python over p> <check expression>: the edited plan must FAIL the check
control() { # the plan in $W/p.json is edited in place (copy kept)
  cp "$W/p.json" "$W/p.good"
  python3 - "$W/p.json" "$1" <<'PY'
import json, sys
p = json.load(open(sys.argv[1])); exec(sys.argv[2]); json.dump(p, open(sys.argv[1], "w"))
PY
  if chk "$2"; then cp "$W/p.good" "$W/p.json"; return 1; fi
  cp "$W/p.good" "$W/p.json"; return 0
}

# Sanity: the fixture is the real tree, so these hold for the scenarios below
python3 - "$R" <<'PY' || fail "the tree has no DFX core / large core / host bench the scenarios use"
import json, subprocess, sys
r = sys.argv[1]
def ct(*a): return subprocess.run(["python3", r + "/tools/core_toml.py"] + list(a), capture_output=True, text=True, check=True).stdout.split()
assert "vc20" in ct("list") and "atari800" in ct("list") and "amiga1200" in ct("list", "cfw"), "cores"
PY
BASE="$(G rev-parse HEAD)"

# ---- D9-1 a host change builds host + reference and every DFX core, nothing else (T164 3a Q1: no large core, they build from their own copy of the host files) --------------------------------------------------
H1="$(chg host/ddr2_lean.v)"
mkplan "$BASE"
D91='p["host"] and sorted(p["dfx"]) == sorted(U["dfx"]) and p["large"] == [] and all(c in p["package"]["fresh"] for c in ["cor:"+x for x in U["cor"]]) and "ddr2_cal" in p["sims"] and "ddr2_ctrl" in p["sims"]'
chk "$D91" || fail "D9-1: $(cat "$W/p.json" | head -c 600)"
control 'p["dfx"].remove("vc20")' "$D91" || fail "D9-1 control: a plan that drops a DFX core passed the check"
control 'p["large"] = list(p["universe"]["large"])' "$D91" || fail "D9-1 control: a plan that builds the large cores on a host change passed the check"
control 'p["host"] = False' "$D91" || fail "D9-1 control: a plan without the host passed the check"
echo "D9-1 ok (host change: host + all $(python3 -c 'import json,sys; print(len(json.load(open(sys.argv[1]))["dfx"]))' "$W/p.json") DFX cores, no large core; 3 controls fail as required)"

# ---- D9-2 a DFX core change builds only that core ------------------------------------------------------------------------------------
C2="$(chg cores/vc20/layer2_vc20.v cores/vc20/rtl/src/m6561.vhd)"
mkplan "$H1"
D92='not p["host"] and p["dfx"] == ["vc20"] and p["large"] == [] and p["package"]["fresh"] == ["cor:vc20"] and "vc20" in p["sims"]'
chk "$D92" || fail "D9-2: $(cat "$W/p.json" | head -c 600)"
control 'p["dfx"].append("amiga")' "$D92" || fail "D9-2 control: a second core passed"
control 'p["host"] = True' "$D92" || fail "D9-2 control: a host build passed"
echo "D9-2 ok (only vc20, host not built, only its .cor is fresh; 2 controls fail as required)"

# ---- D9-3 a large-core change builds only that core --------------------------------------------------------------------------------------
L3="$(chg cores/amiga1200/rtl/amiga_regs.v)"
mkplan "$C2"
D93='not p["host"] and p["dfx"] == [] and p["large"] == ["amiga1200"] and p["package"]["fresh"] == ["cfw:amiga1200"] and "amiga_cfg" in p["sims"]'
chk "$D93" || fail "D9-3: $(cat "$W/p.json" | head -c 600)"
control 'p["dfx"].append("amiga")' "$D93" || fail "D9-3 control: a DFX core passed"
control 'p["host"] = True' "$D93" || fail "D9-3 control: the host passed"
control 'p["large"] = []' "$D93" || fail "D9-3 control: a plan without the large core passed"
echo "D9-3 ok (only amiga1200 flat, no host, no DFX core, its .cfw fresh; 3 controls fail as required)"

# ---- D9-4 an app change rebuilds every consumer -------------------------------------------------------------------------------------------
A4="$(chg coreapp/src/menuctl.cc coreapp/src/fsif.h)"
mkplan "$L3"
D94='p["apps"]["core"] and not p["host"] and p["dfx"] == [] and p["large"] == [] and sorted(p["package"]["fresh"]) == sorted(["cor:"+x for x in U["cor"]] + ["cfw:"+x for x in U["large"]]) and "atari_disk" in p["sims"]'
chk "$D94" || fail "D9-4: $(cat "$W/p.json" | head -c 600)"
control 'p["package"]["fresh"].remove("cor:vc20")' "$D94" || fail "D9-4 control: a plan that leaves one .cor out passed"
control 'p["package"]["fresh"].remove("cfw:amiga1200")' "$D94" || fail "D9-4 control: a plan that leaves the large package out passed"
control 'p["sims"].remove("atari_disk")' "$D94" || fail "D9-4 control: a plan without the app bench passed"
echo "D9-4 ok (core app: every .cor and every large package fresh, bitstreams not rebuilt, app bench planned; 3 controls fail as required)"

# ---- D9-5 sims only for what is built, and of that only what changed ---------------------------------------------------------------------
# one commit: a vc20 file (built) and cores/c64u/sim/run_cw.sh (no descriptor: c64u benches never run)
S5="$(chg cores/vc20/layer2_vc20.v cores/vc20/rtl/src/m6561.vhd cores/c64u/sim/run_cw.sh)"
mkplan "$A4"
python3 - "$R" "$W/p.json" > "$W/oracle.txt" <<'PY'
# the oracle, written independently of plan.py's code path: benches whose declared inputs name cores/vc20, from the raw file
import re, sys, json
root, plan = sys.argv[1:]
txt = open(root + "/ci/sims.deps").read()
vc = set()
for line in txt.split("\n"):
    m = re.match(r"^([a-z0-9_]+):\s*(.*)$", line)
    if m and "cores/vc20" in m.group(2):
        vc.add(m.group(1))
import os
if os.path.exists(root + "/ci/sims.reads"):      # T174 D5: a recorded bench names its files in ci/sims.reads
    cur = None
    for line in open(root + "/ci/sims.reads").read().split("\n"):
        if line and line[0] not in " \t#":
            cur = line.rstrip(":")
        elif cur and "cores/vc20" in line:
            vc.add(cur)
print(" ".join(sorted(vc)))
PY
D95='set(p["sims"]) <= set(open("'"$W"'/oracle.txt").read().split()) and "vc20" in p["sims"] and "c64u_cw" not in p["sims"] and "reu" not in p["sims"] and p["dfx"] == ["vc20"] and not p["host"]'
chk "$D95" || fail "D9-5: $(cat "$W/p.json" | head -c 800)"
control 'p["sims"].append("c64u_cw")' "$D95" || fail "D9-5 control: a c64u bench in the plan passed"
control 'p["sims"].append("eth")' "$D95" || fail "D9-5 control: a bench of nothing that is built passed"
echo "D9-5 ok (vc20 only: benches $(python3 -c 'import json,sys; print(" ".join(json.load(open(sys.argv[1]))["sims"]))' "$W/p.json"); c64u/host benches not planned; 2 controls fail as required)"

# ---- D10 a lane runs only on its triggers (Alex 2026-10-25) ---------------------------------------------------------------------------
# host lane <- host sources, the reference core, the host app; DFX lane <- the host, an app, the core itself; large lane <- an app, the core itself (its copy of the host files included), the shared files it reads.
# Each scenario has controls: the plan edited the way the rule forbids must fail the check.
lanes() { python3 -c 'import json,sys; L=json.load(open(sys.argv[1]))["lanes"]; print("host=%s dfx=%s large=%s" % (L["host"], " ".join(L["dfx"]) or "-", " ".join(L["large"]) or "-"))' "$W/p.json"; }
P10="$S5"
# D10-1 a host change fires the host lane and every DFX lane, no large lane (T164 3a Q1: the flat builds read their own copy of the host files)
H10="$(chg host/ddr2_lean.v)"; mkplan "$P10"
D101='p["lanes"]["host"] and sorted(p["lanes"]["dfx"]) == sorted(U["dfx"]) and p["lanes"]["large"] == []'
chk "$D101" || fail "D10-1: $(lanes)"
control 'p["lanes"]["dfx"].remove("vc20")' "$D101" || fail "D10-1 control: a DFX lane dropped on a host change passed"
control 'p["lanes"]["large"] = list(p["universe"]["large"])' "$D101" || fail "D10-1 control: every large lane on a host change passed"
# D10-2 the reference core fires the host lane (and, through the host, the DFX lanes)
R10="$(chg cores/c64ref/c64_mem_c64ref.v)"; mkplan "$H10"
D102='p["lanes"]["host"] and sorted(p["lanes"]["dfx"]) == sorted(U["dfx"]) and p["lanes"]["large"] == []'
chk "$D102" || fail "D10-2: $(lanes)"
control 'p["lanes"]["host"] = False' "$D102" || fail "D10-2 control: a reference-core change without the host lane passed"
# D10-3 a DFX core's own file fires that lane only
C10="$(chg cores/vc20/layer2_vc20.v)"; mkplan "$R10"
D103='not p["lanes"]["host"] and p["lanes"]["dfx"] == ["vc20"] and p["lanes"]["large"] == []'
chk "$D103" || fail "D10-3: $(lanes)"
control 'p["lanes"]["dfx"].append("atari800")' "$D103" || fail "D10-3 control: a second DFX lane passed"
control 'p["lanes"]["host"] = True' "$D103" || fail "D10-3 control: the host lane on a core-only change passed"
# D10-4 a large core's own file fires that large lane only
L10="$(chg cores/amiga1200/rtl/amiga_regs.v)"; mkplan "$C10"
D104='not p["lanes"]["host"] and p["lanes"]["dfx"] == [] and p["lanes"]["large"] == ["amiga1200"]'
chk "$D104" || fail "D10-4: $(lanes)"
control 'p["lanes"]["dfx"].append("amiga")' "$D104" || fail "D10-4 control: a DFX lane on a large-core change passed"
# D10-5 the core app fires every DFX and every large lane, not the host lane
A10="$(chg coreapp/src/menuctl.cc)"; mkplan "$L10"
D105='not p["lanes"]["host"] and sorted(p["lanes"]["dfx"]) == sorted(U["dfx"]) and p["lanes"]["large"] == U["large"]'
chk "$D105" || fail "D10-5: $(lanes)"
control 'p["lanes"]["host"] = True' "$D105" || fail "D10-5 control: the host lane on a core-app change passed"
control 'p["lanes"]["large"] = []' "$D105" || fail "D10-5 control: no large lane on an app change passed"
# D10-6 the host app fires the host lane and, as an app, every core lane
HA10="$(chg host-app/patches/io_c64_trx_io.cc.patch)"; mkplan "$A10"
D106='p["lanes"]["host"] and sorted(p["lanes"]["dfx"]) == sorted(U["dfx"]) and p["lanes"]["large"] == U["large"]'
chk "$D106" || fail "D10-6: $(lanes)"
control 'p["lanes"]["host"] = False' "$D106" || fail "D10-6 control: a host-app change without the host lane passed"
# D10-7 a change nothing builds from fires no lane; has() answers for lanes
Z10="$(chg ci/lib.sh)"; mkplan "$HA10"
D107='not p["lanes"]["host"] and p["lanes"]["dfx"] == [] and p["lanes"]["large"] == []'
chk "$D107" || fail "D10-7: $(lanes)"
python3 "$R/ci/plan.py" has "$W/p.json" lane:host && fail "D10-7: has lane:host on an idle plan"
python3 "$R/ci/plan.py" has "$W/p.json" lane:dfx:vc20 && fail "D10-7: has lane:dfx:vc20 on an idle plan"
mkplan "$H10"; python3 "$R/ci/plan.py" has "$W/p.json" lane:dfx:vc20 || fail "D10-7 control: has lane:dfx:vc20 false on a plan that fires it"
# D10-8 benches follow their owner: a change in the A1200's own sdc plans the A1200's bench, not the Amiga's (amiga_sdc was one bench for both)
S10="$(chg cores/amiga1200/rtl/amiga_sdc.v)"; mkplan "$Z10"
D108='p["lanes"]["large"] == ["amiga1200"] and "amiga1200_sdc" in p["sims"] and "amiga_sdc" not in p["sims"] and "amiga_l2" not in p["sims"]'
chk "$D108" || fail "D10-8: $(python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))["sims"])' "$W/p.json")"
control 'p["sims"].append("amiga_sdc")' "$D108" || fail "D10-8 control: the Amiga bench planned for an A1200-only change passed"
control 'p["sims"].remove("amiga1200_sdc")' "$D108" || fail "D10-8 control: a plan without the A1200 bench passed"
# ... and the shared source the Amiga core and the A1200 both read fires both (large) lanes and both benches (T164 WP19b: the A500 is a large core)
S10b="$(chg cores/amiga/rtl/trx/amiga_sdc.v)"; mkplan "$S10"
D108b='"amiga1200_sdc" in p["sims"] and "amiga_sdc" in p["sims"] and p["lanes"]["dfx"] == [] and p["lanes"]["large"] == ["amiga", "amiga1200"]'
chk "$D108b" || fail "D10-8b: the shared Amiga source did not fire both lanes/benches: $(lanes)"
echo "D10 ok (lanes fire on host / reference / app / own sources only, as decided; idle plan fires none, benches follow their owner; 13 controls fail as required)"

# ---- D11 (T164 3a Q1, Alex 2026-11-01: "Copies instead.") large cores build from their own copy of the host files ------------------------
# host/top.v changes: the host lane and the DFX lanes fire, NO large lane is planned and no large core is built.
D11B="$(G rev-parse HEAD)"
H11="$(chg host/top.v)"; mkplan "$D11B"
D111='p["host"] and p["large"] == [] and p["lanes"]["large"] == [] and p["lanes"]["host"] and sorted(p["lanes"]["dfx"]) == sorted(U["dfx"])'
chk "$D111" || fail "D11-1: a host/top.v change planned a large core: $(lanes)"
control 'p["large"] = ["nes"]; p["lanes"]["large"] = ["nes"]' "$D111" || fail "D11-1 control: a plan with a large lane on a host/top.v change passed"
# the copy of that file in one large core: that core's lane (and only it) fires, the host does not
N11="$(chg cores/nes/mono/host/top.v)"; mkplan "$H11"
D112='p["large"] == ["nes"] and p["lanes"]["large"] == ["nes"] and not p["host"] and not p["lanes"]["host"] and p["dfx"] == [] and p["lanes"]["dfx"] == []'
chk "$D112" || fail "D11-2: a change in cores/nes/mono/host/top.v did not plan exactly large-nes: $(lanes)"
control 'p["large"] = []; p["lanes"]["large"] = []' "$D112" || fail "D11-2 control: a plan without large-nes passed"
control 'p["large"] = list(p["universe"]["large"]); p["lanes"]["large"] = list(p["universe"]["large"])' "$D112" || fail "D11-2 control: every large core on one core's copy passed"
control 'p["host"] = True' "$D112" || fail "D11-2 control: the host on a large core's copy passed"
# a shared file a large core reads in place (cores/common is read by every flat build) still plans every large core
C11="$(chg cores/common/iec_bus.v)"; mkplan "$N11"
D113='sorted(p["large"]) == sorted(U["large"]) and sorted(p["lanes"]["large"]) == sorted(U["large"])'
chk "$D113" || fail "D11-3: a shared file planned only $(lanes)"
control 'p["large"].remove("nes"); p["lanes"]["large"].remove("nes")' "$D113" || fail "D11-3 control: a plan without large-nes on a shared change passed"
echo "D11 ok (host/top.v: no large lane; cores/nes/mono/host/top.v: large-nes only; a shared file: every large core; 6 controls fail as required)"

# ---- D9-6 the diff base is the last green run of the branch --------------------------------------------------------------------------
# G0 green, A (red run), B pushed: the plan of the run for B diffs G0..B, so A's change is built too. The API answer is canned.
G0="$(G rev-parse HEAD)"
A6="$(chg cores/vc20/layer2_vc20.v)"
B6="$(chg cores/atari800/layer2_atari800.v)"
mkdir -p "$W/bin"
cat > "$W/bin/curl" <<CURL
#!/usr/bin/env bash
cat <<JSON
{"workflow_runs":[
 {"id":12,"path":"pipeline.yml@refs/heads/scratch","status":"in_progress","head_branch":"scratch","head_sha":"$B6"},
 {"id":11,"path":"pipeline.yml@refs/heads/scratch","status":"completed","conclusion":"failure","head_branch":"scratch","head_sha":"$A6"},
 {"id":10,"path":"pipeline.yml@refs/heads/scratch","status":"completed","conclusion":"success","head_branch":"scratch","head_sha":"$G0"},
 {"id":9,"path":"pipeline.yml@refs/heads/scratch","status":"completed","conclusion":"success","head_branch":"scratch","head_sha":"$BASE"}]}
JSON
CURL
chmod +x "$W/bin/curl"
planrun() { # planrun <extra env...>: the plan job's script, in the clone
  env "$@" PATH="$W/bin:$PATH" TOKEN=x GITHUB_REPOSITORY=o/r GITHUB_API_URL=http://gitea/api/v1 GITHUB_REF_NAME=scratch RUN_ID=12 \
    "$R/ci/plan.sh" --out "$W/p.json" > "$W/plan.log" 2>&1 || { cat "$W/plan.log"; fail "plan.sh"; }
}
planrun X=1
D96='p["base"]["commit"] == "'"$G0"'" and "last green run 10" in p["base"]["source"] and p["dfx"] == ["atari800", "vc20"]'
chk "$D96" || fail "D9-6: $(head -c 600 "$W/p.json")"
# control: the previous COMMIT as the base (what a plan without the green-run rule would do) misses A's change and must fail the check
planrun PLAN_BASE="$A6"
if chk "$D96"; then fail "D9-6 control: a plan diffed against the previous commit passed the check"; fi
# fallback: no green run on the branch -> the merge-base with main. The scenario builds its own main: the scratch repo gets origin/main at
# $BASE (its branch point) and no remote, so nothing depends on the refs of the checkout this test runs in (the lint checkout of the
# pipeline has no origin/main: run 159).
cat > "$W/bin/curl" <<'CURL'
#!/usr/bin/env bash
echo '{"workflow_runs":[{"id":11,"path":"pipeline.yml@refs/heads/scratch","status":"completed","conclusion":"failure","head_branch":"scratch","head_sha":"0000"}]}'
CURL
G remote remove origin
G update-ref refs/remotes/origin/main "$BASE"
G update-ref refs/heads/main "$BASE"
{
  planrun X=1
  chk 'p["base"]["source"].startswith("merge-base with main") and p["base"]["commit"] == "'"$BASE"'"' || fail "D9-6: no green run, no merge-base fallback: $(cat "$W/plan.log" | head -5)"
}
echo "D9-6 ok (base = last green run 10, not the previous commit; both A's and B's cores planned; control against the previous commit fails; no green run -> merge-base)"
# ---- D9-7 (T164) a large core's benches read its own copy of the host files, never the live host/ ---------------------------------------
# Declared inputs of every bench owned by a large lane (ci/sims.lane) must hold no file of host/ except the shared bench models host/sim/*.v
# (mig_ui_model.v: a model, built into no core) and the bench directories of host/sim itself (amiga1200_hdmi_timing / _modepath run the host's
# script on the copy via HOSTDIR); the key of such a bench then does not move on a host change.
cat > "$W/largehost.py" <<'PY'
import sys
sys.path.insert(0, sys.argv[1] + "/ci")
import plan
deps = plan.parse_deps(open(sys.argv[1] + "/ci/sims.deps").read(), plan.read_reads(sys.argv[1]))
own = plan.parse_owners(open(sys.argv[1] + "/ci/sims.lane").read())
if len(sys.argv) > 2:      # control: a bench of a large lane that reads host/top.v again
    deps[sys.argv[2]] = deps[sys.argv[2]] + ["host/top.v"]
bad = []
for n, specs in sorted(deps.items()):
    if not own.get(n, "").startswith("large:"):
        continue
    for s in specs:
        f = s.split(":)")[-1]
        if (f == "host" or f.startswith("host/")) and not (f.startswith("host/sim/") and (f.endswith(".v") or f.startswith("host/sim/usb/") or f in ("host/sim/hdmi_timing", "host/sim/modepath"))):
            bad.append(n + " " + f)
if bad: print("\n".join(bad))
sys.exit(1 if bad else 0)
PY
python3 "$W/largehost.py" "$R" || fail "D9-7: a large-core bench reads the live host/ (lines above)"
if python3 "$W/largehost.py" "$R" zx_pins > /dev/null; then fail "D9-7 control: a large-core bench with host/top.v passed the check"; fi
echo "D9-7 ok (no large-core bench declares a file of host/ besides the sim models; the control with host/top.v fails)"

# ---- D2 (T174, Alex 2026-11-07) ------------------------------------------------------------------------------------------------------
D2B="$(G rev-parse HEAD)"
# D2.1 only real code triggers: documentation, specs, TODO and a *.md inside a code tree name nothing
mkdir -p "$R/docs/specs"; echo "# x" > "$R/host/NOTES.md"; echo "x" > "$R/docs/specs/zz.md"; echo "x" > "$R/TODO.md"
G add host/NOTES.md docs/specs/zz.md TODO.md; G commit -q -m "docs only"
mkplan "$D2B"
D21='p["docs_only"] and not p["host"] and p["dfx"] == [] and p["large"] == [] and not any(p["apps"].values()) and p["sims"] == [] and p["package"]["fresh"] == [] and p["changed"] == 0 and not p["lanes"]["host"] and p["lanes"]["dfx"] == [] and p["lanes"]["large"] == []'
chk "$D21" || fail "D2.1: a documentation-only push named something: $(cat "$W/p.json" | head -c 700)"
control 'p["host"] = True' "$D21" || fail "D2.1 control: a plan that builds the host on a docs-only push passed"
control 'p["lanes"]["dfx"] = ["vc20"]' "$D21" || fail "D2.1 control: a lane on a docs-only push passed"
# ... and a code file next to the documentation is not hidden by it
D2C="$(chg host/ddr2_lean.v)"; mkplan "$(G rev-parse HEAD~1)"
chk 'p["host"] and not p["docs_only"]' || fail "D2.1: a code change was taken for documentation"
echo "D2.1 ok (docs/, TODO, *.md - also inside host/ - name no target, a code file does; 2 controls fail as required)"

# D2 addendum: a feature branch builds the cores it touches plus host, reference core and null; main builds what the host change drags in
D2H="$(chg host/ddr2_lean.v)"
PLAN_BRANCH=feature/x mkplan "$D2B"
D22='p["scope"] == "branch" and p["host"] and p["dfx"] == ["null"] and p["large"] == [] and p["lanes"]["dfx"] == ["null"] and p["lanes"]["large"] == [] and p["lanes"]["host"] and not any(x in p["package"]["fresh"] for x in ["cor:vc20", "cor:atari800"])'
chk "$D22" || fail "D2 addendum: host change on a feature branch: $(cat "$W/p.json" | head -c 900)"
control 'p["dfx"] = list(p["universe"]["dfx"])' "$D22" || fail "D2 addendum control: every DFX core on a feature branch passed"
control 'p["lanes"]["dfx"] = list(p["universe"]["dfx"])' "$D22" || fail "D2 addendum control: every DFX lane on a feature branch passed"
mkplan "$D2B"
chk 'sorted(p["dfx"]) == sorted(U["dfx"]) and p["scope"] == "full"' || fail "D2 addendum control: the same push on main must build every DFX core"
# the branch touches vc20 and the core app: vc20 only, the other lanes stay idle
C23="$(chg cores/vc20/layer2_vc20.v coreapp/src/menuctl.cc)"
PLAN_BRANCH=feature/x mkplan "$D2H"
D23='p["dfx"] == ["vc20"] and p["lanes"]["dfx"] == ["vc20"] and p["lanes"]["large"] == [] and not p["host"] and p["apps"]["core"] and p["package"]["fresh"] == ["cor:vc20"]'
chk "$D23" || fail "D2 addendum: core + app on a feature branch: $(cat "$W/p.json" | head -c 900)"
control 'p["lanes"]["large"] = list(p["universe"]["large"])' "$D23" || fail "D2 addendum control: every large lane on a one-core branch passed"
control 'p["dfx"].append("apple2")' "$D23" || fail "D2 addendum control: a second core passed"
# D2 7/8 the package scope: this feature branch is "branch", main is "full" (ci/orchestrate.py starts the package on full only)
chk 'p["scope"] == "branch"' || fail "D2 7/8: a feature branch plan has no scope branch"
control 'p["scope"] = "full"' 'p["scope"] == "branch"' || fail "D2 7/8 control: a plan edited to full passed as branch"
# D4: the application tests of this change are named only when they changed (the core app change here is an app, not a test)
chk 'not p["tests"]' || fail "D4: a branch that touches no test names the tests"
control 'p["tests"] = True' 'not p["tests"]' || fail "D4 control: tests edited to true passed"
echo "D2 addendum ok (feature branch: touched cores + host/ref/null only; main unchanged; 5 controls fail as required)"

# D2.6 the full benches only for a fundamental file (ci/fundamental.txt), always on main
A26="$(chg coreapp/src/menuctl.cc)"; PLAN_BRANCH=feature/x mkplan "$(G rev-parse HEAD~1)"
chk 'p["fundamental"] == False' || fail "D2.6: a plain app change on a feature branch asks for the full benches"
F26="$(chg app/view/trxview.cc)"; PLAN_BRANCH=feature/x mkplan "$(G rev-parse HEAD~1)"
chk 'p["fundamental"] == True' || fail "D2.6: a change in app/view did not ask for the full benches"
python3 "$R/ci/plan.py" has "$W/p.json" fundamental || fail "D2.6: plan.py has fundamental false on a fundamental plan"
mkplan "$(G rev-parse HEAD~2)"
chk 'p["fundamental"] == True' || fail "D2.6: main must always run the full benches"
PLAN_BRANCH=feature/x mkplan "$(G rev-parse HEAD~1)"; control 'p["fundamental"] = False' 'p["fundamental"] == True' || fail "D2.6 control: a plan edited to false passed as fundamental"
# coordinator decision under D2: a pipeline edit is not fundamental, the bench harness itself is
chg ci/lib.sh >/dev/null; PLAN_BRANCH=feature/x mkplan "$(G rev-parse HEAD~1)"
chk 'p["fundamental"] == False' || fail "D2.6: an edit of ci/lib.sh asks for the full benches"
chg ci/render.sh >/dev/null; PLAN_BRANCH=feature/x mkplan "$(G rev-parse HEAD~1)"
chk 'p["fundamental"] == True' || fail "D2.6: an edit of the bench harness ci/render.sh did not ask for the full benches"
# one list: the sections of ci/fundamental.txt, both readers get [all]; a path before a section header is an error
python3 "$R/ci/plan.py" fundamental benches | grep -qx 'host' || fail "D2.6: [benches] lacks host"
python3 "$R/ci/plan.py" fundamental apptests | grep -qx 'host' && fail "D2.6 control: host leaked into the [apptests] list"
for k in benches apptests; do python3 "$R/ci/plan.py" fundamental $k | grep -qx 'coreapp/src/desc.h' || fail "D2.6: [all] path missing from $k"; done
cp "$R/ci/fundamental.txt" "$W/fund.bak"; printf 'stray/path\n%s\n' "$(cat "$W/fund.bak")" > "$R/ci/fundamental.txt"
python3 "$R/ci/plan.py" fundamental benches >/dev/null 2>&1 && fail "D2.6 control: a path before any section header was accepted"
cp "$W/fund.bak" "$R/ci/fundamental.txt"
echo "D2.6 ok (app change: no full benches on a branch; app/view: yes; main: always)"
echo "plan test ok"
