#!/usr/bin/env python3
"""T239.1 S6: ci/orchestrate.py against plans made from this tree: the graph of a docs-only, a full-scope and a feature-branch plan, the outputs the
pipeline's jobs read, stamp/tier/benches/owners, `check` on the tree's own workflows, and the lanes ci/dispatch.py starts against the work order's targets
(T239.5 D13). Unit and happy path; no network, no git history needed beyond HEAD.

  python3 -I ci/test_orchestrate.py
"""
import json, os, subprocess, sys, tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
ORCH = os.path.join(HERE, "orchestrate.py")
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
    return r.returncode, {k: v if v in ("true", "false") or k == "sims_tier" else json.loads(v) for k, v in kv.items()}, r.stdout + r.stderr


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
    rc, out, _ = run("graph", docs)
    want("docs only: nothing to do", rc == 0 and "nothing to do" in out, out)
    rc, out, _ = run("outputs", docs)
    want("docs only: no children", rc == 0 and "children=[]" in out and "docs_only=true" in out, out)

    # full scope with an app change and a host change: package, update (yes), emu after update, prerelease last
    def full(p):
        p.update(docs_only=False, scope="full", sims=[], dfx=[], large=[], host=True, aux=False)
        p["apps"] = {"host": True, "core": True}
    fp = mk("full.json", full)
    rc, out, _ = run("graph", fp, "--tier", "0", "--update", "yes")
    lines = dict(l.split(" <- ") for l in out.strip().splitlines())
    want("full: apptests, images, package, update, emu, prerelease", {"apptests", "images", "package", "update", "emu", "prerelease"} <= set(lines), out)
    want("full: emu needs update (D23)", "update" in lines.get("emu", "").split() and "images" in lines.get("emu", "").split(), lines.get("emu"))
    want("full: package needs images and apptests, not emu", {"images", "apptests"} <= set(lines.get("package", "").split()) and "emu" not in lines.get("package", ""), lines.get("package"))
    want("full: prerelease needs package, update, emu", {"package", "update", "emu"} <= set(lines.get("prerelease", "").split()), lines.get("prerelease"))
    rc, out, _ = run("graph", fp, "--tier", "0", "--update", "no")
    want("full without update: no update, no emu", "update <-" not in out and "emu <-" not in out and "package <-" in out, out)
    rc, out, _ = run("graph", "--json", fp, "--tier", "0", "--update", "yes")
    j = json.loads(out)
    want("graph --json: children, targets, ids", j["children"][0] == "lint" and "prerelease" in j["children"] and "ids" in j and j["tier"] == 0, out)

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
    want("branch: cor leg core:apple2 only, no package", json.loads(kv["cor_targets"]) == ["core:apple2"] and "package" not in json.loads(kv["children"]), out)
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
    rc2, d, dout = dispatch(hp, "--beside")
    want("host+tests branch: the dispatcher starts every own-app core lane", rc2 == 0 and own and sorted(d["cores"]) == sorted(own) and d["package"] == "false",
         dout)
    want("host+tests branch: order_targets name every lane the dispatcher starts", rc == 0 and set(d.get("cor_targets", [])) <= set(order)
         and sorted(t for t in order if t.split(":", 1)[1] in own) == want_own, out + dout)

    # T239.5 D13: a pull request runs no matrix (--matrix no): the work order names the cor legs only, no core with an app of its own
    rc, out, _ = run("outputs", bp, "--tier", "0", "--update", "no", "--matrix", "no")
    kv = kv_of(out)
    want("pull request: order_targets = the cor legs (core:apple2), no own-app core", json.loads(kv["order_targets"]) == ["core:apple2"]
         and json.loads(kv["order_targets"]) == json.loads(kv["cor_targets"]), out)
    rc, out, _ = run("outputs", hp, "--tier", "0", "--update", "no", "--matrix", "no")
    want("pull request, host+tests: no own-app entry ordered", not any(t.split(":", 1)[1] in own for t in json.loads(kv_of(out)["order_targets"])), out)
    rc, out, err = run("outputs", hp, "--tier", "0", "--matrix", "maybe")
    want("--matrix takes yes or no only", rc != 0, out + err)

    # documentation only (D2.1): the dispatcher starts no lane, no lint leg and no package, alone and beside a full-scope run; the work order names nothing
    def docs_full(p):
        p.update(docs_only=True, scope="full", tests=False, all=False)
    dp = mk("docs_full.json", docs_full)
    for mode in ([], ["--beside"], ["--beside", "--update", "yes"]):
        rc, d, dout = dispatch(dp, *mode)
        want("docs only, dispatcher %s: no lane, lint leg or package" % (" ".join(mode) or "alone"),
             rc == 0 and d["cores"] == [] and d["lint_groups"] == [] and d["cor_targets"] == [] and (d["sdk"], d["host"], d["package"]) == ("false",) * 3, dout)
    rc, out, _ = run("outputs", dp, "--tier", "0", "--update", "yes")
    want("docs only: order_targets empty", json.loads(kv_of(out)["order_targets"]) == [], out)
    rc, d, dout = dispatch(fp)
    want("full alone (not docs only): every lane and the package", rc == 0 and sorted(d["cores"]) == sorted(own) and d["package"] == "true", dout)

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
