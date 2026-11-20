#!/usr/bin/env python3
"""T239.1 S6: ci/orchestrate.py against plans made from this tree: the graph of a docs-only, a full-scope and a feature-branch plan, the outputs the
pipeline's jobs read, stamp/tier/benches/owners, and `check` on the tree's own workflows. Unit and happy path; no network, no git history needed beyond HEAD.

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

    # a feature branch: one cor leg per touched core, no package
    def feat(p):
        p.update(docs_only=False, scope="branch", sims=[], large=[], host=False, aux=False, dfx=["vc20"])
        p["apps"] = {"host": False, "core": True}
        p.setdefault("universe", {})["cor"] = ["vc20"]
    bp = mk("branch.json", feat)
    rc, out, _ = run("outputs", bp, "--tier", "0", "--update", "no")
    kv = dict(l.split("=", 1) for l in out.strip().splitlines())
    want("branch: cor leg core:vc20, no package", json.loads(kv["cor_targets"]) == ["core:vc20"] and "package" not in json.loads(kv["children"]), out)

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
