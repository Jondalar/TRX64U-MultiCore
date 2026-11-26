#!/usr/bin/env python3
"""T174 D4 (Alex, 2026-11-08), T239.1 S6: the children of the pipeline and the one table they come from.

On GitHub the parent is the job graph of .github/workflows/pipeline.yml: every child is a job that calls its own workflow file with `uses:`, `if:`
asks the plan whether it runs, `needs:` is the table's needs. Nothing is dispatched any more; this file only reads the table and the plan.

  ci/orchestrate.py graph <plan.json> [--tier N] [--update yes|no]
        the children this plan asks for, in dependency order, with what each needs (nothing is started)
  ci/orchestrate.py graph --json <plan.json> [...]
        the same as one JSON line: {"children": [names], "targets": {child: [targets]}, "ids": [...], "tier": N}
  ci/orchestrate.py outputs <plan.json> [--tier N] [--update yes|no] [--matrix yes|no]
        `key=value` lines for $GITHUB_OUTPUT: children, sim_owners, cor_targets (JSON), order_targets (JSON), sims_tier, update, docs_only
        T239.5 D13: cor_targets (the legs of the child cor) leave out the cores with an app of their own (plan universe.own): their .cor comes from their matrix
        lane (.github/workflows/matrix.yml). order_targets (the run's work order, ci/order.py COR_TARGETS) are the cor legs' targets and, with --matrix yes
        (the default: the matrix runs beside this pipeline), the bitstreams of every core lane the dispatcher starts (matrix_lanes, its rule); --matrix no (a
        pull request, which runs no matrix): the cor legs' targets only.
  ci/orchestrate.py stamp <plan.json> --tier N   write the run's sims tier into the plan (ci/child.sh sim reads it back)
  ci/orchestrate.py owners [--tier N]            JSON list of every bench owner that has a bench up to tier N (the nightly's matrix)
  ci/orchestrate.py tier <plan.json>             the sims tier of the plan (env SIMS_TIER, default 2, without one);  benches <plan.json> <owner>: that owner's
                                                 benches (every bench of the tier, without a plan)
  ci/orchestrate.py check
        ci/children.txt against the workflow files and pipeline.yml; every bench of ci/sims.txt has exactly one owner in ci/sims.lane

The table: ci/children.txt (child, workflow, when, needs). A child id is "<child>" or "<child>/<target>" (sim/host, cor/core:vc20).
"""
import json, os, re, sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)
import plan as P  # noqa: E402

TABLE = os.environ.get("ORCH_TABLE") or os.path.join(HERE, "children.txt")   # ORCH_TABLE: the tests' controls
WF = os.environ.get("ORCH_WORKFLOWS") or os.path.join(ROOT, ".github", "workflows")


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


def targets_of(row, plan, tier, update):
    """the targets this child runs with in this plan: [] = not started, [""] = once without a target."""
    w, apps = row["when"], plan.get("apps", {})
    app_any = any(apps.values())
    full = plan.get("scope", "full") == "full"
    cors = order_cors(plan, matrix=False)
    package = full or update
    if w == "local":
        return [""]
    if w == "apps":
        return [""] if app_any or plan.get("tests") or plan.get("fundamental") and full else []
    if w == "images":
        return [""] if app_any or package or (not full and cors) else []
    if w == "emu":      # D23: only where update runs (its kit reads no update file since T239.1 step 17)
        return [""] if (apps.get("host") or apps.get("core")) and update else []
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
    if w == "prerelease":
        return [""] if package else []
    raise SystemExit("ci/children.txt: unknown when %r" % w)


def lane_target(name, kind):
    """the bitstream target of a core lane (ci/fetch_bits.sh, ci/order.py): core:<id> for a partial, large:<id> for a flat bitstream."""
    return ("large:" if kind == "large" else "core:") + name


HOST_EMU_CORE = "vc20"   # the core whose .cor the host menu lane's emulator test runs (tests/hostapp/emu_host.sh EMU_OWN_APP, matrix.yml host-emu)


def matrix_lanes(plan, lanes):
    """T239.5 D13: the one rule of the dispatcher (ci/dispatch.py, a push without the whole package) and of the work order (order_cors): of the core lanes
    `lanes` ((name, kind) of the cores with an app of their own), those that start - a core the plan names (plan dfx and large, plan lanes), every one when
    the plan has no base (all) or the application tests changed (tests) - and the VIC-20 whenever the host app changed: the host menu lane's emulator
    test (S6, D15) runs the host with the .cor of the vc20 lane."""
    L = plan.get("lanes") or {}
    named = set(plan.get("dfx", [])) | set(plan.get("large", [])) | set(L.get("dfx", [])) | set(L.get("large", []))
    every = bool(plan.get("all") or plan.get("tests"))
    if (plan.get("apps") or {}).get("host"):
        named.add(HOST_EMU_CORE)
    return [(c, k) for c, k in lanes if every or c in named]


def order_cors(plan, matrix=True):
    """the .cor targets of a feature branch's plan: the cor legs' targets (the cores the plan names, those with an app of their own left out: plan
    universe.own) and, with matrix (the matrix runs beside this pipeline), the bitstream targets of every core lane it starts (matrix_lanes; a core of
    universe.large is a large lane) - the work order's COR_TARGETS."""
    uni = plan.get("universe", {})
    own = uni.get("own", [])
    named = ["core:" + c for c in plan.get("dfx", []) if c in uni.get("cor", plan.get("dfx", []))] + ["large:" + c for c in plan.get("large", [])]
    legs = [t for t in named if t.split(":", 1)[1] not in own]
    if not matrix:
        return legs
    # T265 P5: the matrix owns the work order and builds the host gateware: the target host when the plan changes the host or aux designs
    host = ["host"] if plan.get("host") or plan.get("aux") else []
    lanes = [(c, "large" if c in uni.get("large", []) else "dfx") for c in own]
    return host + legs + [lane_target(c, k) for c, k in matrix_lanes(plan, lanes)]


def nid(child, target):
    return child + ("/" + target if target else "")


def graph(plan, tier=1, update=False, rows=None):
    """[(id, child, target, workflow, [needed ids])] in an order that respects every need; [] for a docs-only plan."""
    if plan.get("docs_only"):
        return []
    rows = rows or read_children()
    nodes = []
    for r in rows:
        for t in targets_of(r, plan, tier, update):
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




def outputs(plan, tier, update, matrix=True):
    """what the jobs of .github/workflows/pipeline.yml ask the plan: which children run, the matrix targets of sim and cor, and the work order's targets."""
    nodes = graph(plan, tier, update)
    names = list(dict.fromkeys(n[1] for n in nodes))
    targets = {}
    for _, child, target, _, _ in nodes:
        targets.setdefault(child, [])
        if target:
            targets[child].append(target)
    full = plan.get("scope", "full") == "full"
    return {"children": names, "targets": targets, "ids": [n[0] for n in nodes], "tier": tier,
            "order": [] if full or plan.get("docs_only") else order_cors(plan, matrix)}


def job_map(path):
    import yaml
    return (yaml.safe_load(open(path, encoding="utf-8")) or {}).get("jobs", {})


def read_yaml(path):
    import yaml
    text = open(path, encoding="utf-8").read()
    return text, yaml.safe_load(text)


def triggers(y):
    return y.get("on", y.get(True, {})) or {}


def check():
    errs = []
    rows = read_children()
    names = {r["child"] for r in rows}
    for r in rows:
        if r["when"] not in ("local", "apps", "images", "emu", "owners", "gateware", "cor", "package", "update", "prerelease"):
            errs.append("ci/children.txt: child %s: unknown when %r" % (r["child"], r["when"]))
        for tok in r["needs"]:
            m = re.match(r"^-?([a-z0-9_]+)", tok)
            if tok != "*" and m and m.group(1) not in names:
                errs.append("ci/children.txt: child %s needs %s, which is no child" % (r["child"], m.group(1)))
        if r["workflow"] == "-":
            continue
        p = os.path.join(WF, r["workflow"])
        if not os.path.exists(p):
            errs.append("child %s: no workflow %s" % (r["child"], r["workflow"]))
            continue
        text, y = read_yaml(p)
        if "workflow_call" not in triggers(y):
            errs.append("%s: not a called workflow (no workflow_call)" % r["workflow"])
        if "ci/child.sh" not in text:
            errs.append("%s: the child does not run through ci/child.sh" % r["workflow"])
        if not re.search(r"ref: \$\{\{ github\.event\.pull_request\.head\.sha \|\| github\.sha \}\}", text):
            errs.append("%s: the child does not check out the run's commit (the head of a pull request, else github.sha)" % r["workflow"])
        if (y.get("permissions") or {}).get("contents") != "read":
            errs.append("%s: top-level permissions contents: read is missing" % r["workflow"])
    for f in sorted(os.listdir(WF)):
        t = open(os.path.join(WF, f), encoding="utf-8").read()
        for bad in ("defer_red", "CI_RED_DEFER", "verdict.yml", "lanes.py", "wait_turn", "/api/" + "v1/", "GITEA_TOKEN", "self-hosted"):
            if bad in t:
                errs.append("%s: %s has no place on GitHub (T239.1)" % (f, bad))
    pp = os.path.join(WF, "pipeline.yml")
    if not os.path.exists(pp):
        errs.append("no pipeline.yml")
    else:
        text, y = read_yaml(pp)
        jobs = y.get("jobs", {})
        if (y.get("permissions") or {}).get("contents") != "read":
            errs.append("pipeline.yml: top-level permissions contents: read is missing")
        if not (y.get("concurrency") or {}).get("group"):
            errs.append("pipeline.yml: no concurrency group (one per ref, D9)")
        for j in ("plan", "lint", "approve", "summary"):
            if j not in jobs:
                errs.append("pipeline.yml: no job %s" % j)
        for r in rows:
            if r["workflow"] == "-" and r["when"] in ("local", "gateware"):
                continue                      # lint is a job of the file itself; gateware is the builder's, awaited at an environment
            j = jobs.get(r["child"])
            if j is None:
                errs.append("pipeline.yml: child %s has no job" % r["child"])
                continue
            if r["workflow"] != "-" and j.get("uses") != "./.github/workflows/" + r["workflow"]:
                errs.append("pipeline.yml: job %s calls %r, the table says ./.github/workflows/%s" % (r["child"], j.get("uses"), r["workflow"]))
            need = j.get("needs", [])
            need = [need] if isinstance(need, str) else need
            if "plan" not in need:
                errs.append("pipeline.yml: job %s does not need plan (its `if` reads the plan)" % r["child"])
            for tok in r["needs"]:
                m = re.match(r"^([a-z0-9_]+)", tok)
                if tok == "*" or tok.startswith("-") or not m:
                    continue
                dep = m.group(1)
                drow = next((x for x in rows if x["child"] == dep), None)
                if drow and drow["when"] in ("gateware",):
                    continue
                if dep not in need:
                    errs.append("pipeline.yml: job %s must need %s (ci/children.txt)" % (r["child"], dep))
            if "*" in r["needs"]:
                minus = {t[1:] for t in r["needs"] if t.startswith("-")}
                for x in rows:
                    if x["child"] != r["child"] and x["child"] not in minus and x["workflow"] != "-" and x["child"] not in need:
                        errs.append("pipeline.yml: job %s must need %s (ci/children.txt, *)" % (r["child"], x["child"]))
        for jn, j in jobs.items():
            need = j.get("needs", [])
            for n in [need] if isinstance(need, str) else need:
                if n not in jobs:
                    errs.append("pipeline.yml: job %s needs %s, which does not exist" % (jn, n))
            if "uses" in j and not os.path.exists(os.path.join(ROOT, j["uses"])):
                errs.append("pipeline.yml: job %s uses %s, which does not exist" % (jn, j["uses"]))
        # the environments (D1, D14): vivado only in `approve`, builds only in `prerelease`
        for jn, j in jobs.items():
            env = j.get("environment")
            env = env.get("name") if isinstance(env, dict) else env
            if env == "vivado" and jn != "approve":
                errs.append("pipeline.yml: environment vivado outside job approve (job %s)" % jn)
            if env == "builds" and jn != "prerelease":
                errs.append("pipeline.yml: environment builds outside job prerelease (job %s)" % jn)
    for f, calls in (("nightly.yml", ("sim.yml", "apptests.yml")),):
        p = os.path.join(WF, f)
        if not os.path.exists(p):
            errs.append("no %s" % f)
            continue
        t = open(p, encoding="utf-8").read()
        for w in calls:
            if "./.github/workflows/" + w not in t:
                errs.append("%s does not call %s" % (f, w))
    errs += check_benches()
    for e in errs:
        print("orchestrate: " + e)
    if errs:
        sys.exit(1)
    print("orchestrate ok: %d children in ci/children.txt, pipeline.yml has a job for each with the table's needs, every bench has one owner" % len(rows))


def opts(a):
    o = {"--tier": "1", "--update": "no", "--matrix": "yes"}
    it = iter(a)
    for k in it:
        if k not in o:
            raise SystemExit(__doc__)
        o[k] = next(it)
    if o["--matrix"] not in ("yes", "no"):
        raise SystemExit(__doc__)
    return o


def main():
    a = sys.argv[1:]
    if a[:1] == ["check"]:
        check()
        return
    if a[:1] == ["tier"] and len(a) == 2:      # the sims tier the pipeline chose (ci/child.sh sim)
        has = os.path.exists(a[1])
        print(json.load(open(a[1])).get("ci", {}).get("sims_tier", 1) if has else int(os.environ.get("SIMS_TIER") or 2))
        return
    if a[:1] == ["benches"] and len(a) == 3:   # the benches of one owner that the plan names in its tier (ci/child.sh sim)
        if os.path.exists(a[1]):
            p = json.load(open(a[1]))
            tier = int(p.get("ci", {}).get("sims_tier", 1))
        else:                                  # run alone (nightly): every bench there is
            p, tier = {"sims": sorted(sims_tiers())}, int(os.environ.get("SIMS_TIER") or 2)
        print("\n".join(sim_benches(p, tier).get(a[2], [])))
        return
    if a[:1] == ["owners"]:
        o = opts(a[1:]) if len(a) > 1 else {"--tier": "2"}
        print(json.dumps(sorted(sim_benches({"sims": sorted(sims_tiers())}, int(o["--tier"]))), separators=(",", ":")))
        return
    if a[:1] == ["stamp"] and len(a) >= 2:
        o = opts(a[2:])
        p = json.load(open(a[1]))
        p.setdefault("ci", {})["sims_tier"] = int(o["--tier"])
        json.dump(p, open(a[1], "w"))
        return
    as_json = a[:2] == ["graph", "--json"]
    if as_json:
        a = ["graph"] + a[2:]
    if a[:1] not in (["graph"], ["outputs"]) or len(a) < 2:
        raise SystemExit(__doc__)
    plan = json.load(open(a[1]))
    o = opts(a[2:])
    tier, update, matrix = int(o["--tier"]), o["--update"] == "yes", o["--matrix"] == "yes"
    plan.setdefault("ci", {})["sims_tier"] = tier
    if a[0] == "outputs":
        d = outputs(plan, tier, update, matrix)
        print("children=" + json.dumps(d["children"], separators=(",", ":")))
        print("sim_owners=" + json.dumps(d["targets"].get("sim", []), separators=(",", ":")))
        print("cor_targets=" + json.dumps(d["targets"].get("cor", []), separators=(",", ":")))
        print("order_targets=" + json.dumps(d["order"], separators=(",", ":")))
        print("sims_tier=%d" % tier)
        print("update=%s" % ("yes" if update else "no"))
        print("docs_only=%s" % ("true" if plan.get("docs_only") else "false"))
        return
    if as_json:
        print(json.dumps(outputs(plan, tier, update, matrix), separators=(",", ":")))
        return
    nodes = graph(plan, tier, update)
    if not nodes:
        print("nothing to do (docs only)" if plan.get("docs_only") else "nothing to do (the plan names nothing)")
        return
    for i, child, target, wf, needs in nodes:
        print("%s <- %s" % (i, " ".join(needs) or "-"))


if __name__ == "__main__":
    main()
