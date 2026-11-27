#!/usr/bin/env python3
"""T174 D4 (Alex, 2026-11-08), T239.1 S6, T239.5 D22: the called workflows of the pipeline and the one table they come from.

.github/workflows/pipeline.yml is only the trigger: its one job calls .github/workflows/matrix.yml (D22), and the matrix calls the workflows of this table with `uses:`
(one job each). This file reads the table and the plan: which of them a plan asks for, the sims' owners and the work order's targets.

  ci/orchestrate.py outputs <plan.json> [--tier N] [--update yes|no] [--matrix yes|no]
        `key=value` lines for $GITHUB_OUTPUT: children, sim_owners, cor_targets (JSON), order_targets (JSON), sims_tier, update, docs_only
        T239.5 D13: cor_targets (the legs of the child cor) leave out the cores with an app of their own (plan universe.own): their .cor comes from their matrix
        lane. order_targets (the run's work order, ci/order.py COR_TARGETS) are the cor legs' targets and, with --matrix yes (the default), the bitstreams of the
        host and of every core lane the dispatcher starts (matrix_lanes, its rule); --matrix no: the cor legs' targets only.
  ci/orchestrate.py stamp <plan.json> --tier N   write the run's sims tier into the plan (ci/child.sh sim reads it back)
  ci/orchestrate.py owners [--tier N]            JSON list of every bench owner that has a bench up to tier N (the nightly's matrix)
  ci/orchestrate.py tier <plan.json>             the sims tier of the plan (env SIMS_TIER, default 2, without one);  benches <plan.json> <owner>: that owner's
                                                 benches (every bench of the tier, without a plan)
  ci/orchestrate.py check
        ci/children.txt against the workflow files, pipeline.yml and matrix.yml; every bench of ci/sims.txt has exactly one owner in ci/sims.lane

The table: ci/children.txt (child, workflow, when). A child id is "<child>" or "<child>/<target>" (sim/host, cor/core:vc20).
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
        if len(f) != 3:
            raise SystemExit("ci/children.txt: a row needs child, workflow, when: %r" % line)
        rows.append({"child": f[0], "workflow": f[1], "when": f[2]})
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
    full = plan.get("scope", "full") == "full"
    if plan.get("docs_only"):
        return []
    if w == "apps":
        return [""] if any(apps.values()) or plan.get("tests") or plan.get("fundamental") and full else []
    if w == "owners":
        return sorted(sim_benches(plan, tier))
    if w == "cor":
        return [] if full else order_cors(plan, matrix=False)
    if w == "update":
        return [""] if update else []
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




def wants_apptests(plan):
    """T239.5 D22: the matrix job apptests (the child of the row `apps`) runs: an application, the application tests or, at full scope, a fundamental file changed."""
    return bool(targets_of(next(r for r in read_children() if r["when"] == "apps"), plan, 0, False))


def outputs(plan, tier, update, matrix=True):
    """what the jobs of the matrix ask the plan: which children run, the targets of sim and cor, and the work order's targets."""
    rows = read_children()
    targets = {r["child"]: targets_of(r, plan, tier, update) for r in rows}
    full = plan.get("scope", "full") == "full"
    return {"children": [c for c, ts in targets.items() if ts], "targets": targets, "tier": tier,
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


def called_by(jobs, workflow):
    """the jobs of a workflow that call ./.github/workflows/<workflow>"""
    return [n for n, j in jobs.items() if j.get("uses") == "./.github/workflows/" + workflow]


def check():
    errs = []
    rows = read_children()
    for r in rows:
        if r["when"] not in ("apps", "owners", "cor", "update"):
            errs.append("ci/children.txt: child %s: unknown when %r" % (r["child"], r["when"]))
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
    # D22: pipeline.yml is the trigger and calls the matrix, nothing else
    pp, mp = os.path.join(WF, "pipeline.yml"), os.path.join(WF, "matrix.yml")
    if not os.path.exists(pp):
        errs.append("no pipeline.yml")
    else:
        text, y = read_yaml(pp)
        jobs = y.get("jobs", {})
        if (y.get("permissions") or {}).get("contents") != "read":
            errs.append("pipeline.yml: top-level permissions contents: read is missing")
        if not (y.get("concurrency") or {}).get("group"):
            errs.append("pipeline.yml: no concurrency group (one per ref, D9)")
        if list(jobs) != ["matrix"] or called_by(jobs, "matrix.yml") != ["matrix"]:
            errs.append("pipeline.yml: the only job is `matrix`, calling matrix.yml (T239.5 D22); found %s" % list(jobs))
    if not os.path.exists(mp):
        errs.append("no matrix.yml")
    else:
        text, y = read_yaml(mp)
        jobs = y.get("jobs", {})
        for r in rows:
            if not called_by(jobs, r["workflow"]):
                errs.append("matrix.yml: child %s has no job that calls %s" % (r["child"], r["workflow"]))
        for jn, j in jobs.items():
            need = j.get("needs", [])
            for n in [need] if isinstance(need, str) else need:
                if n not in jobs:
                    errs.append("matrix.yml: job %s needs %s, which does not exist" % (jn, n))
            if "uses" in j and not os.path.exists(os.path.join(ROOT, j["uses"])):
                errs.append("matrix.yml: job %s uses %s, which does not exist" % (jn, j["uses"]))
        # the environments (D1, D14): vivado only in `approve`, builds only in `prerelease` and `sdk-release`
        for jn, j in jobs.items():
            env = j.get("environment")
            env = env.get("name") if isinstance(env, dict) else env
            if env == "vivado" and jn != "approve":
                errs.append("matrix.yml: environment vivado outside job approve (job %s)" % jn)
            if env == "builds" and jn not in ("prerelease", "sdk-release"):
                errs.append("matrix.yml: environment builds outside the jobs prerelease and sdk-release (job %s)" % jn)
        for j in ("dispatch", "approve", "lint", "summary", "prerelease"):
            if j not in jobs:
                errs.append("matrix.yml: no job %s" % j)
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
    print("orchestrate ok: %d children in ci/children.txt, matrix.yml has a job for each, pipeline.yml only calls the matrix, every bench has one owner" % len(rows))


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
    if a[:1] != ["outputs"] or len(a) < 2:
        raise SystemExit(__doc__)
    plan = json.load(open(a[1]))
    o = opts(a[2:])
    tier, update, matrix = int(o["--tier"]), o["--update"] == "yes", o["--matrix"] == "yes"
    plan.setdefault("ci", {})["sims_tier"] = tier
    d = outputs(plan, tier, update, matrix)
    print("children=" + json.dumps(d["children"], separators=(",", ":")))
    print("sim_owners=" + json.dumps(d["targets"].get("sim", []), separators=(",", ":")))
    print("cor_targets=" + json.dumps(d["targets"].get("cor", []), separators=(",", ":")))
    print("order_targets=" + json.dumps(d["order"], separators=(",", ":")))
    print("sims_tier=%d" % tier)
    print("update=%s" % ("yes" if update else "no"))
    print("docs_only=%s" % ("true" if plan.get("docs_only") else "false"))


if __name__ == "__main__":
    main()
