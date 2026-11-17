#!/usr/bin/env python3
"""T174 D4 (Alex, 2026-11-08): the parent of the push pipeline starts child pipelines, one concern each, and ends with one table.

  ci/orchestrate.py graph <plan.json> [--tier N] [--update yes|no] [--ue2 yes|no]
        the children this plan asks for, in dispatch order, with what each needs (nothing is started)
  ci/orchestrate.py run <plan.json> [--tier N] [--update yes|no] [--ue2 yes|no] [--lint CMD] [--fake SPEC]
        dispatch the children through the Gitea API (workflow_dispatch, inputs sha / parent / target / plan), run the lint (a step of the
        parent) meanwhile, wait, print the table; exit 1 when a child is red or was not started. --fake SPEC: a fake Gitea for the tests
        (ci/test_orchestrate.sh), SPEC = {"red": [ids], "never": [ids], "log": file}
  ci/orchestrate.py tier <plan.json>             the sims tier of the parent's plan;  benches <plan.json> <owner>: that owner's benches
  ci/orchestrate.py check
        ci/children.txt against the workflow files and pipeline.yml; every bench of ci/sims.txt has exactly one owner in ci/sims.lane

The graph: ci/children.txt (the one table: child, workflow, when, needs). A child id is "<child>" or "<child>/<target>"
(sim/host, vivado/core:vc20, cor/large:amiga). Env of `run`: GITHUB_SERVER_URL, GITHUB_REPOSITORY, GITHUB_REF, GITHUB_SHA, GITHUB_RUN_ID,
ORCH_TOKEN (dispatch; ORCH_TOKEN2 is tried when it is refused), CI_CHILD_START_MIN (240: a child not running that long after its dispatch is
red), CI_CHILD_POLL (30 s), CI_DISPATCH_GAP (1 s between two dispatches).
"""
import json, os, re, signal, subprocess, sys, time, urllib.error, urllib.request

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)
import plan as P  # noqa: E402

TABLE = os.environ.get("ORCH_TABLE") or os.path.join(HERE, "children.txt")   # ORCH_TABLE: the tests' controls
WF = os.path.join(ROOT, ".gitea", "workflows")


# --- the table ---------------------------------------------------------------------------------------------------------
def read_children(path=None):
    rows = []
    for line in open(path or TABLE, encoding="utf-8"):
        if not line.strip() or line.lstrip().startswith("#"):
            continue
        f = line.split()
        if len(f) < 4:
            raise SystemExit("ci/children.txt: a row needs child, workflow, when, needs: %r" % line)
        rows.append({"child": f[0], "workflow": f[1], "when": f[2], "needs": [] if f[3:] == ["-"] else f[3:]})
    return rows


def sims_tiers():
    out = {}
    for line in open(os.path.join(HERE, "sims.txt"), encoding="utf-8"):
        f = line.rstrip("\n").split("|", 3)
        if len(f) == 4 and f[0].strip().isdigit():
            out[f[1]] = int(f[0])
    return out


def bench_owners(text=None):
    """bench -> [owner] from ci/sims.lane (host | core:<id> | large:<id> | none)."""
    out = {}
    for line in (text if text is not None else open(os.path.join(HERE, "sims.lane"), encoding="utf-8").read()).split("\n"):
        if not line.strip() or line.lstrip().startswith("#"):
            continue
        k, _, v = line.partition(":")
        out.setdefault(k.strip(), []).append(v.strip())
    return out


def sim_benches(plan, tier):
    """owner -> the plan's benches it owns that run in this tier (an owner none is never planned)."""
    owners, tiers, out = bench_owners(), sims_tiers(), {}
    for b in plan.get("sims", []):
        o = (owners.get(b) or ["other"])[0]
        if o != "none" and tiers.get(b, 99) <= tier:
            out.setdefault(o, []).append(b)
    return out


def targets_of(row, plan, tier, update, ue2):
    """the targets this child runs with in this plan: [] = not started, [""] = once without a target."""
    w, apps = row["when"], plan.get("apps", {})
    app_any = any(apps.values())
    full = plan.get("scope", "full") == "full"
    uni = plan.get("universe", {})
    cors = ["core:" + c for c in plan.get("dfx", []) if c in uni.get("cor", plan.get("dfx", []))] + ["large:" + c for c in plan.get("large", [])]
    package = full or update
    if w == "local":
        return [""]
    if w == "apps":
        return [""] if app_any or plan.get("tests") or plan.get("fundamental") and full else []
    if w == "images":
        return [""] if app_any or package or (not full and cors) else []
    if w == "emu":
        return [""] if apps.get("host") or apps.get("core") else []
    if w == "owners":
        return sorted(sim_benches(plan, tier))
    if w == "gateware":
        return (["host"] if plan.get("host") or plan.get("aux") else []) + ["core:" + c for c in plan.get("dfx", [])] + \
            ["large:" + c for c in plan.get("large", [])]
    if w == "cor":
        return [] if full else cors
    if w == "package":
        return [""] if package else []
    if w == "update":
        return [""] if update else []
    if w == "ue2":
        return [""] if ue2 else []
    raise SystemExit("ci/children.txt: unknown when %r" % w)


def nid(child, target):
    return child + ("/" + target if target else "")


def graph(plan, tier=1, update=False, ue2=False, rows=None):
    """[(id, child, target, workflow, [needed ids])] in an order that respects every need; [] for a docs-only plan."""
    if plan.get("docs_only"):
        return []
    rows = rows or read_children()
    nodes = []
    for r in rows:
        for t in targets_of(r, plan, tier, update, ue2):
            nodes.append({"id": nid(r["child"], t), "child": r["child"], "target": t, "workflow": r["workflow"], "row": r})
    ids = [n["id"] for n in nodes]
    for n in nodes:
        need, star, minus = [], False, set()
        for tok in n["row"]["needs"]:
            if tok == "*":
                star = True
                continue
            if tok.startswith("-"):
                minus.add(tok[1:])
                continue
            m = re.match(r"^([a-z0-9_]+)(?:\(([^)]*)\))?(?:\[([^\]]*)\])?$", tok)
            if not m:
                raise SystemExit("ci/children.txt: bad need %r" % tok)
            child, arg, only = m.groups()
            if only is not None and not n["target"].startswith(only):
                continue
            if arg is None:
                need += [i for i in ids if i == child or i.startswith(child + "/")]
            else:
                need.append(nid(child, n["target"] if arg == "=" else arg))
        if star:
            need += [m["id"] for m in nodes if m["child"] not in minus]
        n["needs"] = [i for i in dict.fromkeys(need) if i in ids and i != n["id"]]
    order, done = [], set()
    while len(order) < len(nodes):
        ready = [n for n in nodes if n["id"] not in done and all(d in done for d in n["needs"])]
        if not ready:
            raise SystemExit("ci/children.txt: the needs have a cycle among %s" % [n["id"] for n in nodes if n["id"] not in done])
        for n in ready:
            order.append(n)
            done.add(n["id"])
    return [(n["id"], n["child"], n["target"], n["workflow"], n["needs"]) for n in order]


# --- Gitea ------------------------------------------------------------------------------------------------------------
class Gitea:
    def __init__(self):
        self.api = os.environ["GITHUB_SERVER_URL"].rstrip("/") + "/api/v1/repos/" + os.environ["GITHUB_REPOSITORY"]
        self.tokens = [t for t in (os.environ.get("ORCH_TOKEN"), os.environ.get("ORCH_TOKEN2")) if t]
        if not self.tokens:
            raise SystemExit("orchestrate: no ORCH_TOKEN: nothing can be dispatched")

    def call(self, method, path, body=None, tok=None):
        req = urllib.request.Request(self.api + path, method=method, data=json.dumps(body).encode() if body is not None else None,
                                     headers={"Authorization": "token " + (tok or self.tokens[0]), "Content-Type": "application/json"})
        with urllib.request.urlopen(req, timeout=60) as r:
            data = r.read()
        return data

    def dispatch(self, workflow, inputs, node=""):
        body = {"ref": os.environ.get("GITHUB_REF") or "refs/heads/" + os.environ["GITHUB_REF_NAME"], "inputs": inputs}
        err = None
        for tok in self.tokens:
            try:
                d = json.loads(self.call("POST", "/actions/workflows/%s/dispatches?return_run_details=true" % workflow, body, tok) or b"{}")
                if d.get("workflow_run_id"):
                    if tok != self.tokens[0]:
                        self.tokens.remove(tok)
                        self.tokens.insert(0, tok)
                    return d["workflow_run_id"], d.get("html_url", "")
                err = "no run id in the answer: %s" % d
            except urllib.error.HTTPError as e:
                err = "HTTP %s %s" % (e.code, e.read()[:200].decode(errors="replace"))
            except OSError as e:
                err = str(e)
        raise RuntimeError(err)

    def status(self, run):
        d = json.loads(self.call("GET", "/actions/runs/%s" % run))
        return d.get("status", ""), d.get("conclusion") or "", d.get("run_number"), d.get("started_at"), d.get("completed_at")

    def reason(self, run):
        """the line ci/child.sh prints when the child is red ("CHILD RED: ..."), else the last lines of the failed job's log."""
        try:
            jobs = json.loads(self.call("GET", "/actions/runs/%s/jobs" % run)).get("jobs", [])
            for j in jobs:
                if j.get("conclusion") not in ("failure", "cancelled"):
                    continue
                log = self.call("GET", "/actions/jobs/%s/logs" % j["id"]).decode(errors="replace").splitlines()
                red = [l for l in log if "CHILD RED:" in l]
                if red:
                    return red[-1].split("CHILD RED:", 1)[1].strip()
                tail = [re.sub(r"^\S+Z\s+", "", l).strip() for l in log if l.strip()][-3:]
                return "%s: %s" % (j.get("name", "job"), " | ".join(tail))[:300]
        except (OSError, ValueError) as e:
            return "no reason (log not readable: %s)" % e
        return "no failed job"

    def cancel(self, run):
        return False   # Gitea 1.27.3 has no cancel in its API; the child stops itself when its parent is gone (ci/child.sh)


class Fake:
    """for ci/test_orchestrate.sh: every child is green after one poll, unless the spec lists it red or never-starting."""
    def __init__(self, spec):
        self.s = json.load(open(spec))
        self.runs, self.n = {}, 100

    def log(self, line):
        with open(self.s["log"], "a") as fh:
            fh.write(line + "\n")

    def dispatch(self, workflow, inputs, node=""):
        self.n += 1
        i = node
        self.runs[self.n] = {"id": i, "polls": 0}
        self.log("dispatch %s %s %s target=%s sha=%s parent=%s" % (self.n, node, workflow, inputs["target"], inputs["sha"], inputs["parent"]))
        return self.n, "fake://%d" % self.n

    def status(self, run):
        r = self.runs[run]
        r["polls"] += 1
        if r["id"] in self.s.get("never", []):
            return "queued", "", run, None, None
        if r["polls"] < 2:
            return "in_progress", "", run, None, None
        self.log("done %s %s" % (run, r["id"]))
        return "completed", "failure" if r["id"] in self.s.get("red", []) else "success", run, None, None

    def reason(self, run):
        return "fake red " + self.runs[run]["id"]

    def cancel(self, run):
        self.log("cancel %s" % run)
        return True


def tdelta(a, b):
    from datetime import datetime
    try:
        f = lambda s: datetime.fromisoformat(s.replace("Z", "+00:00"))
        s = int((f(b) - f(a)).total_seconds())
    except (TypeError, ValueError, AttributeError):
        return None
    return s


def fmt(s):
    return "-" if s is None else "%dm%02ds" % (s // 60, s % 60)


def run(plan, nodes, gitea, lint_cmd=None, poll=30.0, gap=1.0, start_min=240.0, lint_log="lint.log"):
    st = {i: {"state": "pending", "run": None, "number": None, "url": "", "t0": None, "t1": None, "reason": "", "secs": None}
          for i, *_ in nodes}
    by = {n[0]: n for n in nodes}
    planjson = json.dumps(plan, separators=(",", ":"))
    sha, parent = plan.get("head") or os.environ.get("GITHUB_SHA", ""), os.environ.get("GITHUB_RUN_ID", "local")
    lint = None
    stop = {"why": ""}

    def on_term(sig, _):
        stop["why"] = "the parent was cancelled (signal %d)" % sig
    signal.signal(signal.SIGTERM, on_term)
    signal.signal(signal.SIGINT, on_term)

    def red_at_once(i):
        print("RED %s: %s" % (i.replace("/", " "), st[i]["reason"]), flush=True)

    while True:
        if stop["why"]:
            for i, s in st.items():
                if s["state"] in ("running", "queued"):
                    ok = gitea.cancel(s["run"])
                    s["state"], s["reason"] = "red", stop["why"] + ("" if ok else "; the child stops itself when it sees its parent ended")
                elif s["state"] == "pending":
                    s["state"], s["reason"] = "skipped", stop["why"]
            if lint and lint.poll() is None:
                lint.terminate()
            break
        progressed = False
        for i, child, target, wf, needs in nodes:
            s = st[i]
            if s["state"] != "pending":
                continue
            bad = [d for d in needs if st[d]["state"] in ("red", "skipped")]
            if bad:
                d = bad[0]
                root = d if st[d]["state"] == "red" else st[d]["reason"].split("not started: ", 1)[-1].split(" red")[0]
                s["state"], s["reason"] = "skipped", "not started: %s red" % root.replace("/", " ")
                progressed = True
                continue
            if any(st[d]["state"] != "green" for d in needs):
                continue
            s["t0"] = time.time()
            if wf == "-":
                print("=== %s: a step of this job (%s)" % (i, lint_cmd or "nothing"), flush=True)
                if lint_cmd:
                    lint = subprocess.Popen(lint_cmd, shell=True, stdout=open(lint_log, "w"), stderr=subprocess.STDOUT, start_new_session=True)
                    s["state"] = "running"
                else:
                    s["state"] = "green"
                progressed = True
                continue
            try:
                # a workflow that serves two children (vivado.yml: vivado and ue2) gets the child's name as its target
                tgt = target or ("" if wf.split(".")[0] == child else child)
                s["run"], s["url"] = gitea.dispatch(wf, {"sha": sha, "parent": str(parent), "target": tgt, "plan": planjson}, i)
                s["state"] = "queued"
                print("=== dispatched %s: %s" % (i.replace("/", " "), s["url"]), flush=True)
            except RuntimeError as e:
                s["state"], s["reason"] = "red", "dispatch refused: %s" % e
                red_at_once(i)
            progressed = True
            time.sleep(gap)
        active = [i for i, s in st.items() if s["state"] in ("running", "queued")]
        if not active and not progressed:
            break
        if not progressed:
            time.sleep(poll)
        for i in active:
            s = st[i]
            if by[i][3] == "-":
                rc = lint.poll()
                if rc is None:
                    continue
                s["t1"], s["secs"] = time.time(), int(time.time() - s["t0"])
                if rc == 0:
                    s["state"] = "green"
                else:
                    tail = [l.strip() for l in open(lint_log, errors="replace") if l.strip()]
                    fails = [l for l in tail if "FAIL" in l or "fail" in l]
                    s["state"], s["reason"] = "red", (fails[-1] if fails else (tail[-1] if tail else "exit %d" % rc))[:300]
                    red_at_once(i)
                continue
            try:
                status, concl, number, t0, t1 = gitea.status(s["run"])
            except (OSError, ValueError) as e:
                print("=== %s: status not readable (%s), asking again" % (i, e), flush=True)
                continue
            s["number"] = number
            if status in ("queued", "waiting", "blocked", ""):
                if time.time() - s["t0"] > start_min * 60:
                    gitea.cancel(s["run"])
                    s["state"], s["reason"] = "red", "never started: still %s %d min after its dispatch" % (status or "queued", start_min)
                    red_at_once(i)
                continue
            s["state"] = "running"
            if status != "completed":
                continue
            s["secs"] = tdelta(t0, t1)
            if s["secs"] is None:
                s["secs"] = int(time.time() - s["t0"])
            if concl == "success":
                s["state"] = "green"
                print("=== green %s (%s)" % (i.replace("/", " "), fmt(s["secs"])), flush=True)
            else:
                s["state"], s["reason"] = "red", "%s: %s" % (concl, gitea.reason(s["run"]))
                red_at_once(i)
    return st


def table(nodes, st, head=""):
    lines = [head] if head else []
    w = max([len(i) for i, *_ in nodes] + [5])
    lines.append("%-*s  %-8s  %-11s  %-7s  %s" % (w, "child", "run", "result", "time", "reason / link"))
    for i, *_ in nodes:
        s = st[i]
        res = {"green": "green", "red": "RED", "skipped": "not started"}.get(s["state"], s["state"])
        run = ("#%s" % s["number"]) if s["number"] else ("id %s" % s["run"] if s["run"] else "-")
        lines.append("%-*s  %-8s  %-11s  %-7s  %s" % (w, i.replace("/", " "), run, res, fmt(s["secs"]), s["reason"] or s["url"]))
    return "\n".join(lines)


# --- check ------------------------------------------------------------------------------------------------------------
def check_benches():
    errs = []
    owners = bench_owners()
    sims = P.parse_sims(open(os.path.join(HERE, "sims.txt"), encoding="utf-8").read())
    deps = P.parse_deps(open(os.path.join(HERE, "sims.deps"), encoding="utf-8").read(), P.read_reads())
    _, dfx, large = P.universe()
    sys.path.insert(0, os.path.join(os.path.dirname(HERE), "tools"))
    import core_toml as CT  # noqa: E402
    off = set(n for n, _, t in CT.descriptors() + CT.large_descriptors() if not CT.in_pipeline(t))   # T92 D-17e: no stage builds, packs or benches it
    for b in sims:
        o = owners.get(b)
        if not o:
            errs.append("bench %s has no owner in ci/sims.lane" % b)
            continue
        if len(o) > 1:
            errs.append("bench %s has %d owners in ci/sims.lane (exactly one)" % (b, len(o)))
        o = o[0]
        if o.split(":", 1)[-1] in off and o != "host":
            continue                # the owner core has `pipeline = false`: no stage runs the bench (ci/sims.sh skips it too)
        specs = deps.get(b)
        if specs is None:
            errs.append("bench %s has no line in ci/sims.deps" % b)
            continue
        if o == "host":
            ok = any(x == "host" or x.startswith("host/") for x in specs)
        elif o == "none":
            ok = any(x.startswith("cores/c64u") for x in specs)
        elif (o.startswith("core:") and o[5:] in dfx) or (o.startswith("large:") and o[6:] in large):
            c = o.split(":", 1)[1]
            ok = any(x == "cores/" + c or x.startswith("cores/%s/" % c) for x in specs)
        else:
            errs.append("bench %s: owner %r is no lane of this tree" % (b, o))
            continue
        if not ok:
            errs.append("bench %s: owner %s, but its ci/sims.deps line does not read that core" % (b, o))
    for b in owners:
        if b not in sims:
            errs.append("ci/sims.lane names %s, which is no bench of ci/sims.txt" % b)
    return errs


def check():
    import yaml
    errs = []
    rows = read_children()
    for r in rows:
        if r["workflow"] == "-":
            continue
        p = os.path.join(WF, r["workflow"])
        if not os.path.exists(p):
            errs.append("child %s: no workflow %s" % (r["child"], r["workflow"]))
            continue
        text = open(p, encoding="utf-8").read()
        y = yaml.safe_load(text)
        on = y.get("on", y.get(True, {})) or {}
        ins = ((on.get("workflow_dispatch") or {}).get("inputs") or {})
        for k in ("sha", "parent", "target", "plan"):
            if k not in ins:
                errs.append("%s: workflow_dispatch lacks the input %s" % (r["workflow"], k))
        if "inputs.target" not in str(y.get("run-name", "")) or "inputs.sha" not in str(y.get("run-name", "")):
            errs.append("%s: run-name must say what it is (inputs.target, inputs.sha)" % r["workflow"])
        if "ci/child.sh" not in text:
            errs.append("%s: the child does not run through ci/child.sh" % r["workflow"])
        if not re.search(r"SHA: \$\{\{ inputs\.sha", text):
            errs.append("%s: the child does not check out inputs.sha" % r["workflow"])
    for f in sorted(os.listdir(WF)):
        t = open(os.path.join(WF, f), encoding="utf-8").read()
        for bad in ("defer_red", "CI_RED_DEFER", "verdict.yml", "lanes.py"):
            if bad in t:
                errs.append("%s: %s is gone (T174 D4: red is red at once, no deferred verdict)" % (f, bad))
    y = yaml.safe_load(open(os.path.join(WF, "pipeline.yml"), encoding="utf-8"))
    jobs = y.get("jobs", {})
    if list(jobs) != ["parent"]:
        errs.append("pipeline.yml: one job `parent` and nothing else (the children are dispatched), found %s" % list(jobs))
    else:
        j = jobs["parent"]
        names = [s.get("name") for s in j.get("steps", [])]
        if j.get("runs-on") != "nas":
            errs.append("pipeline.yml: the parent runs on the nas pool (the builder's slots are the children's)")
        for want in ("Wait for my turn", "Plan", "Children"):
            if want not in names:
                errs.append("pipeline.yml: the parent has no step %r" % want)
        if names.index("Wait for my turn") > names.index("Plan") if {"Wait for my turn", "Plan"} <= set(names) else False:
            errs.append("pipeline.yml: the parent waits for its turn before it plans")
    nightly = open(os.path.join(WF, "nightly.yml"), encoding="utf-8").read()
    for w in ("sim.yml", "apptests.yml"):
        if "./.gitea/workflows/" + w not in nightly:
            errs.append("nightly.yml does not call %s" % w)
    errs += check_benches()
    for e in errs:
        print("orchestrate: " + e)
    if errs:
        sys.exit(1)
    print("orchestrate ok: %d children in ci/children.txt, every workflow takes sha/parent/target/plan, every bench has one owner" % len(rows))


def opts(a):
    o = {"--tier": "1", "--update": "no", "--ue2": "no", "--lint": "", "--fake": ""}
    it = iter(a)
    for k in it:
        if k not in o:
            raise SystemExit(__doc__)
        o[k] = next(it)
    return o


def main():
    a = sys.argv[1:]
    if a[:1] == ["check"]:
        check()
        return
    if a[:1] == ["tier"] and len(a) == 2:      # the sims tier the parent chose (ci/child.sh sim)
        print(json.load(open(a[1])).get("ci", {}).get("sims_tier", 1) if os.path.exists(a[1]) else 1)
        return
    if a[:1] == ["benches"] and len(a) == 3:   # the benches of one owner that the plan names in its tier (ci/child.sh sim)
        p = json.load(open(a[1]))
        print("\n".join(sim_benches(p, int(p.get("ci", {}).get("sims_tier", 1))).get(a[2], [])))
        return
    if a[:1] not in (["graph"], ["run"]) or len(a) < 2:
        raise SystemExit(__doc__)
    plan = json.load(open(a[1]))
    o = opts(a[2:])
    tier, update, ue2 = int(o["--tier"]), o["--update"] == "yes", o["--ue2"] == "yes"
    plan.setdefault("ci", {})["sims_tier"] = tier
    nodes = graph(plan, tier, update, ue2)
    head = "=== children of parent %s (%s, scope %s, sims tier %d)" % (os.environ.get("GITHUB_RUN_ID", "local"), (plan.get("head") or "")[:8],
                                                                    plan.get("scope", "full"), tier)
    if not nodes:
        print("nothing to do (docs only)" if plan.get("docs_only") else "nothing to do (the plan names nothing)")
        return
    if a[0] == "graph":
        for i, child, target, wf, needs in nodes:
            print("%s <- %s" % (i, " ".join(needs) or "-"))
        return
    gitea = Fake(o["--fake"]) if o["--fake"] else Gitea()
    fast = bool(o["--fake"])
    st = run(plan, nodes, gitea, o["--lint"] or None, poll=0 if fast else float(os.environ.get("CI_CHILD_POLL", "30")),
             gap=0 if fast else float(os.environ.get("CI_DISPATCH_GAP", "1")),
             start_min=float(os.environ.get("CI_CHILD_START_MIN", "240")), lint_log=os.path.abspath(a[1]) + ".lint.log")
    t = table(nodes, st, head)
    print(t)
    if os.environ.get("GITHUB_STEP_SUMMARY"):
        with open(os.environ["GITHUB_STEP_SUMMARY"], "a") as fh:
            fh.write("```\n" + t + "\n```\n")
    sys.exit(1 if any(s["state"] != "green" for s in st.values()) else 0)


if __name__ == "__main__":
    main()
