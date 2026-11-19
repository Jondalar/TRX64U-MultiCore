#!/usr/bin/env python3
"""T239.1 D9: ci/vivado.env is the one place for the gateware build options, but while .gitea/workflows/vivado.yml exists it carries a copy
of them in the env of its Vivado job or step. This check reads both and is red when a key of ci/vivado.env is missing from that workflow's
env or has another value there. A repository without the workflow file passes (the copy is gone).

  tools/check_vivado_env.py [--env FILE] [--workflow FILE]
  tools/check_vivado_env.py --selftest"""
import os, sys, tempfile

import yaml

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def read_env(path):
    out = {}
    for ln in open(path):
        ln = ln.strip()
        if ln and not ln.startswith("#"):
            k, _, v = ln.partition("=")
            out[k] = v
    return out


def workflow_envs(path):
    """Every env mapping of every job and step of the workflow, merged: key -> set of values (as strings)."""
    seen = {}
    for job in (yaml.safe_load(open(path)) or {}).get("jobs", {}).values():
        for env in [job.get("env")] + [s.get("env") for s in job.get("steps", [])]:
            for k, v in (env or {}).items():
                seen.setdefault(k, set()).add(str(v))
    return seen


def check(env_file, workflow):
    if not os.path.exists(workflow):
        return []
    want, have = read_env(env_file), workflow_envs(workflow)
    return ["%s: %s in the workflow, %s in ci/vivado.env" % (k, sorted(have[k]) if k in have else "missing", v)
            for k, v in want.items() if have.get(k) != {v}]


def selftest():
    errs = []
    with tempfile.TemporaryDirectory() as td:
        e, w = os.path.join(td, "v.env"), os.path.join(td, "w.yml")
        open(e, "w").write("# c\nA=x\nB=1\n")
        wf = lambda a, b: open(w, "w").write("jobs:\n  j:\n    steps:\n      - env:\n          A: %s\n          B: \"%s\"\n" % (a, b))
        wf("x", "1")
        if check(e, w):
            errs.append("equal values refused")
        wf("y", "1")
        if not check(e, w):
            errs.append("changed value passed")
        open(w, "w").write("jobs:\n  j:\n    steps:\n      - env:\n          A: x\n")
        if not check(e, w):
            errs.append("missing key passed")
        if check(e, os.path.join(td, "none.yml")):
            errs.append("absent workflow refused")
    print("selftest FAILED: " + "; ".join(errs) if errs else "selftest ok")
    return 1 if errs else 0


if __name__ == "__main__":
    a = sys.argv[1:]
    if a == ["--selftest"]:
        sys.exit(selftest())
    opt = dict(zip(a[::2], a[1::2]))
    bad = check(opt.get("--env", os.path.join(ROOT, "ci/vivado.env")), opt.get("--workflow", os.path.join(ROOT, ".gitea/workflows/vivado.yml")))
    for b in bad:
        print("VIVADO-ENV: " + b)
    print("vivado env copies: %s" % ("%d differences" % len(bad) if bad else "agree"))
    sys.exit(1 if bad else 0)
