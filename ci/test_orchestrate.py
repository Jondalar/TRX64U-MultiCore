#!/usr/bin/env python3
"""T239.1 S6, T239.5 D22: ci/orchestrate.py against plans made from this tree: the outputs the matrix's jobs read for a docs-only, a full-scope and a
feature-branch plan, stamp/tier/benches/owners, `check` on the tree's own workflows, and the lanes and jobs ci/dispatch.py starts against the work order's targets
(T239.5 D13). Unit and happy path; no network, no git history needed beyond HEAD.

  python3 -I ci/test_orchestrate.py
"""
import json, os, subprocess, sys, tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
ORCH = os.path.join(HERE, "orchestrate.py")
sys.path.insert(0, HERE)
import orchestrate as O  # noqa: E402
fails = []


def run(*a, **kw):
    r = subprocess.run([sys.executable, ORCH, *a], capture_output=True, text=True, cwd=ROOT, **kw)
    return r.returncode, r.stdout, r.stderr


def kv_of(out):
    return dict(l.split("=", 1) for l in out.strip().splitlines() if "=" in l)


def dispatch(plan_path, *a):
    """ci/dispatch.py over a plan: its $GITHUB_OUTPUT lines as a dict (the JSON values decoded)."""
    env = {k: v for k, v in os.environ.items() if k != "GITHUB_STEP_SUMMARY"}
    r = subprocess.run([sys.executable, os.path.join(HERE, "dispatch.py"), plan_path, "--tier", "0", *a], capture_output=True, text=True, cwd=ROOT, env=env)
    kv = kv_of(r.stdout)
    return r.returncode, {k: v if v in ("true", "false") or k in ("sims_tier", "host_emu_core") else json.loads(v) for k, v in kv.items()}, r.stdout + r.stderr


def want(name, ok, detail=""):
    print(("ok   " if ok else "FAIL ") + name + ("" if ok else ": " + str(detail)))
    if not ok:
        fails.append(name)


with tempfile.TemporaryDirectory() as td:
    base = os.path.join(td, "base.json")
    r = subprocess.run([sys.executable, os.path.join(HERE, "plan.py"), "make", "--base", "HEAD", "--head", "HEAD", "--branch", "main", "--source", "test"],
                       capture_output=True, text=True, cwd=ROOT)
    want("a plan of this tree", r.returncode == 0 and r.stdout.strip().startswith("{"), r.stderr[-300:])
    plan = json.loads(r.stdout)

    def mk(name, edit):
        p = json.loads(json.dumps(plan))
        edit(p)
        path = os.path.join(td, name)
        json.dump(p, open(path, "w"))
        return path

    # docs only: nothing
    docs = mk("docs.json", lambda p: p.update(docs_only=True))
    rc, out, _ = run("outputs", docs)
    want("docs only: no children", rc == 0 and "children=[]" in out and "docs_only=true" in out, out)

    # full scope with an app change and a host change: the app tests, the update files, no cor leg
    def full(p):
        p.update(docs_only=False, scope="full", sims=[], dfx=[], large=[], host=True, aux=False)
        p["apps"] = {"host": True, "core": True}
    fp = mk("full.json", full)
    rc, out, _ = run("outputs", fp, "--tier", "0", "--update", "yes")
    kv = kv_of(out)
    want("full: apptests and update, no sim, no cor", rc == 0 and json.loads(kv["children"]) == ["apptests", "update"] and kv["update"] == "yes", out)
    rc, out, _ = run("outputs", fp, "--tier", "0", "--update", "no")
    want("full without update: apptests only", json.loads(kv_of(out)["children"]) == ["apptests"], out)

    # a feature branch: one cor leg per touched core, no package; T239.5 D13: a core with an app of its own (universe.own) has no cor leg (its .cor comes from
    # its matrix lane), and the work order's targets (order_targets) still name it
    def feat(p):
        p.update(docs_only=False, scope="branch", sims=[], large=["amiga1200"], host=False, aux=False, dfx=["vc20", "apple2"])
        p["apps"] = {"host": False, "core": True}
        p.setdefault("universe", {})["cor"] = ["vc20", "apple2"]
        p["universe"]["own"] = ["vc20", "amiga1200"]
    bp = mk("branch.json", feat)
    rc, out, _ = run("outputs", bp, "--tier", "0", "--update", "no")
    kv = dict(l.split("=", 1) for l in out.strip().splitlines())
    want("branch: cor leg core:apple2 only", json.loads(kv["cor_targets"]) == ["core:apple2"] and "cor" in json.loads(kv["children"]), out)
    want("branch: order_targets name every touched core", sorted(json.loads(kv["order_targets"])) == ["core:apple2", "core:vc20", "large:amiga1200"], out)
    rc, out, _ = run("outputs", fp, "--tier", "0", "--update", "yes")
    kv = dict(l.split("=", 1) for l in out.strip().splitlines())
    want("full: no order_targets (the package names every entry)", json.loads(kv["order_targets"]) == [], out)

    # T239.5 D13: a feature branch that changes host gateware and the application tests names no core of its own, yet the dispatcher starts every core lane
    # (plan tests); the work order names the bitstreams of each of them (ci/orchestrate.py matrix_lanes, the rule of both). The tree's own cores and kinds.
    def host_tests(p):
        p.update(docs_only=False, scope="branch", sims=[], large=[], host=True, aux=False, dfx=["null"], tests=True, all=False)
        p["apps"] = {"host": False, "core": False, "ultimate": False}
        p["lanes"] = {"host": True, "dfx": ["null"], "large": []}
    hp = mk("host_tests.json", host_tests)
    own = plan["universe"]["own"]
    want_own = sorted(("large:" if c in plan["universe"]["large"] else "core:") + c for c in own)
    rc, out, _ = run("outputs", hp, "--tier", "0", "--update", "no")
    order = json.loads(kv_of(out)["order_targets"])
    rc2, d, dout = dispatch(hp)
    d_host, dout_host = d, dout
    want("host+tests branch: the dispatcher starts every own-app core lane", rc2 == 0 and own and sorted(d["cores"]) == sorted(own) and d["package"] == "false",
         dout)
    want("host+tests branch: order_targets name every lane the dispatcher starts", rc == 0 and set(d.get("lane_targets", [])) <= set(order)
         and sorted(t for t in order if ":" in t and t.split(":", 1)[1] in own) == want_own, out + dout)

    # T265 P5: the matrix owns the work order: a plan that changes host/ names the target host (first), and the dispatcher's job host-bits runs; outputs --matrix no
    # names no host; a host-app-only or core-only push builds no host gateware
    want("host gateware change: order_targets start with host", order[:1] == ["host"], str(order))
    want("host gateware change: dispatcher host_bits, lane_targets start with host, order_targets the same as outputs with matrix",
         d_host.get("host_bits") == "true" and d_host["lane_targets"][:1] == ["host"] and d_host.get("order_targets") == order, dout_host)
    rc, out, _ = run("outputs", hp, "--tier", "0", "--update", "no", "--matrix", "no")
    want("--matrix no, host gateware change: no host target", "host" not in json.loads(kv_of(out)["order_targets"]), out)

    # T239.5 S6, D15: a push that changes only the host app starts the host menu lane and, with it, the vc20 lane (the host lane's emulator test runs the
    # host with vc20's .cor); the work order names vc20's bitstream, no other core lane starts
    def host_app(p):
        p.update(docs_only=False, scope="branch", sims=[], large=[], host=False, aux=False, dfx=[], tests=False, all=False)
        p["apps"] = {"host": True, "core": False, "ultimate": False}
        p["lanes"] = {"host": False, "dfx": [], "large": []}
    ap = mk("host_app.json", host_app)
    rc2, d, dout = dispatch(ap)
    want("host app only: the dispatcher starts the vc20 lane and nothing else of the cores", rc2 == 0 and d["cores"] == ["vc20"] and d["host"] == "true", dout)
    want("host app only: no host gateware job, no host target", d["host_bits"] == "false" and "host" not in d["lane_targets"] and "host" not in d["order_targets"], dout)
    rc, out, _ = run("outputs", ap, "--tier", "0", "--update", "no")
    want("host app only: the work order names vc20's bitstream", rc == 0 and "core:vc20" in json.loads(kv_of(out)["order_targets"]), out)

    want("host app only: the dispatcher prints the core of the host lane", d.get("host_emu_core") == "vc20", dout)
    # a push that starts a core lane other than the host lane's core (its own source) does not start the host menu lane
    other = next((c for c in own if c != "vc20" and c in plan["universe"].get("dfx", plan["universe"]["own"])), None) or next(c for c in own if c != "vc20")
    def core_only(p):
        p.update(docs_only=False, scope="branch", sims=[], large=[], host=False, aux=False, dfx=[other], tests=False, all=False)
        p["apps"] = {"host": False, "core": False, "ultimate": False}
        p["lanes"] = {"host": False, "dfx": [other], "large": []}
    cp = mk("core_only.json", core_only)
    rc2, d, dout = dispatch(cp)
    want("a core lane of another core: no host menu lane", rc2 == 0 and other in d["cores"] and "vc20" not in d["cores"] and d["host"] == "false", dout)

    # T239.5 D13: --matrix no: the work order names the cor legs only, no core with an app of its own
    rc, out, _ = run("outputs", bp, "--tier", "0", "--update", "no", "--matrix", "no")
    kv = kv_of(out)
    want("--matrix no: order_targets = the cor legs (core:apple2), no own-app core", json.loads(kv["order_targets"]) == ["core:apple2"]
         and json.loads(kv["order_targets"]) == json.loads(kv["cor_targets"]), out)
    rc, out, _ = run("outputs", hp, "--tier", "0", "--update", "no", "--matrix", "no")
    want("--matrix no, host+tests: no own-app entry ordered", not any(":" in t and t.split(":", 1)[1] in own for t in json.loads(kv_of(out)["order_targets"])), out)
    rc, out, err = run("outputs", hp, "--tier", "0", "--matrix", "maybe")
    want("--matrix takes yes or no only", rc != 0, out + err)

    # documentation only (D2.1): the dispatcher starts no lane, no lint leg, no package and no job, also with a full scope; the work order names nothing
    def docs_full(p):
        p.update(docs_only=True, scope="full", tests=False, all=False)
    dp = mk("docs_full.json", docs_full)
    for mode in ([], ["--update", "yes"]):
        rc, d, dout = dispatch(dp, *mode)
        want("docs only, dispatcher %s: no lane, lint leg, package, cor leg or job" % (" ".join(mode) or "plain"),
             rc == 0 and d["cores"] == [] and d["lint_groups"] == [] and d["lane_targets"] == [] and d["cor_legs"] == []
             and (d["sdk"], d["host"], d["package"], d["update"], d["apptests"]) == ("false",) * 5, dout)
    rc, out, _ = run("outputs", dp, "--tier", "0", "--update", "yes")
    want("docs only: order_targets empty", json.loads(kv_of(out)["order_targets"]) == [], out)
    rc, d, dout = dispatch(fp)
    want("full (not docs only): every lane, the lint legs and the package", rc == 0 and sorted(d["cores"]) == sorted(own) and d["package"] == "true"
         and d["lint_groups"] == ["tree", "sdk", "host"] + ["core:" + c for c in d["cores"]] and d["cor_legs"] == [], dout)
    want("full: the package carries the host gateware (host_bits)", d["host_bits"] == "true" and d["lane_targets"][:1] == ["host"], dout)
    want("full: apptests (the plan names apps), no update without the select", d["apptests"] == "true" and d["update"] == "false", dout)
    rc, d, dout = dispatch(fp, "--update", "yes")
    want("full with the update files asked: update", rc == 0 and d["update"] == "true", dout)

    # T239.5 D22: a feature branch of a core without a lane of its own: a cor leg, the shared image of the SDK lane, no package
    rc, d, dout = dispatch(bp)
    want("branch: the cor leg of core:apple2, the SDK lane for its image, no package", rc == 0 and d["cor_legs"] == ["core:apple2"] and d["sdk"] == "true"
         and d["package"] == "false", dout)
    rc, d, dout = dispatch(bp, "--update", "yes")
    want("branch with the update files asked: the package, no cor leg", rc == 0 and d["package"] == "true" and d["cor_legs"] == [], dout)
    # the sims of the owners the plan names, every owner (not only the host and the moved cores), in the tier
    def sims_plan(p):
        p.update(docs_only=False, scope="branch", sims=sorted(O.sims_tiers()), dfx=[], large=[], host=False, aux=False, tests=False)
        p["apps"] = {"host": False, "core": False}
    sp = mk("sims.json", sims_plan)
    rc, d, dout = dispatch(sp)
    want("sims tier 0: no sim leg", rc == 0 and d["sim_owners"] == [], dout)
    r = subprocess.run([sys.executable, os.path.join(HERE, "dispatch.py"), sp, "--tier", "2"], capture_output=True, text=True, cwd=ROOT,
                       env={k: v for k, v in os.environ.items() if k != "GITHUB_STEP_SUMMARY"})
    owners_run = json.loads(kv_of(r.stdout)["sim_owners"])
    want("sims tier 2: every owner the plan names a bench of, host and cores", r.returncode == 0 and "host" in owners_run and len(owners_run) > 3, r.stdout + r.stderr)

    # the sims tier travels in the plan
    run("stamp", fp, "--tier", "2")
    rc, out, _ = run("tier", fp)
    want("stamp then tier", out.strip() == "2", out)
    rc, out, _ = run("tier", os.path.join(td, "none.json"), env={**os.environ, "SIMS_TIER": "3"})
    want("tier without a plan reads SIMS_TIER", out.strip() == "3", out)

    # the nightly's matrix: every owner with a bench; benches of the host owner without a plan
    rc, out, _ = run("owners", "--tier", "2")
    owners = json.loads(out)
    want("owners: host and a core, sorted, no duplicates", "host" in owners and owners == sorted(set(owners)) and len(owners) > 3, out)
    rc, out, _ = run("benches", os.path.join(td, "none.json"), "host", env={**os.environ, "SIMS_TIER": "2"})
    want("benches of the host owner without a plan", rc == 0 and len(out.split()) > 0, out)

    # the tree's own workflows against the table
    rc, out, err = run("check")
    want("check on the tree", rc == 0 and "orchestrate ok" in out, out + err)

print("test_orchestrate: %s" % ("FAILED: " + ", ".join(fails) if fails else "ok"))
sys.exit(1 if fails else 0)
