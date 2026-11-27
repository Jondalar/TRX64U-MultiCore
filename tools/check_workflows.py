#!/usr/bin/env python3
"""T239.1 D1, S9: the rules a workflow of .github/ must keep, read from the parsed YAML (and, for the repository names, the raw text).

  1. no pull_request_target, workflow_run or issue_comment trigger in a workflow that names a secret (secrets.X, secrets: inherit) or an environment
  2. environment `builds` only in a job called `prerelease` whose `if:` demands a push of main or a v* tag, or in a job called `sdk-release` (T261 D4) whose `if:` demands
     a push of integration, main or t239.3; the checkout takes the run's own commit
  3. top-level `permissions: contents: read` (the only key)
  4. every `uses:` is a local path (./) or pinned by a full 40-hex commit SHA; docker:// needs a digest
  5. every container / service image carries @sha256:<64 hex>
  6. no `runs-on` with self-hosted
  7. no name of a private repository (the Gitea side repository, the builder repository) anywhere under .github/

  tools/check_workflows.py [DIR]      (default: .github of the checkout)
  tools/check_workflows.py --selftest"""
import os, re, sys, tempfile

import yaml

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
RISKY = ("pull_request_target", "workflow_run", "issue_comment")
PRIVATE_NAMES = re.compile(r"trx64u-multicore-private|trx64mc-builder\b", re.I)
SHA40 = re.compile(r"^[0-9a-f]{40}$")
DIGEST = re.compile(r"@sha256:[0-9a-f]{64}\b")


def triggers(doc):
    on = doc.get("on", doc.get(True))  # YAML 1.1 reads the key `on` as True
    if isinstance(on, str):
        return [on]
    return list(on or [])


def walk(node):
    if isinstance(node, dict):
        for k, v in node.items():
            yield k, v
            yield from walk(v)
    elif isinstance(node, list):
        for v in node:
            yield from walk(v)


def names_secret_or_env(doc, text):
    if re.search(r"\bsecrets\s*\.|\bsecrets\s*\[", text):
        return True
    for k, v in walk(doc.get("jobs") or {}):
        if k == "environment" or (k == "secrets" and v):
            return True
    return False


def images_of(job):
    c = job.get("container")
    if isinstance(c, str):
        yield c
    elif isinstance(c, dict) and "image" in c:
        yield str(c["image"])
    for s in (job.get("services") or {}).values():
        if isinstance(s, dict) and "image" in s:
            yield str(s["image"])
        elif isinstance(s, str):
            yield s


def env_name(job):
    e = job.get("environment")
    return e.get("name") if isinstance(e, dict) else e


BUILDS_JOBS = {"prerelease": ("refs/heads/main", "refs/tags/v"),
               "sdk-release": ("refs/heads/integration", "refs/heads/main", "refs/heads/t239.3")}


def prerelease_guard(jid, job):
    bad = []
    cond = str(job.get("if", ""))
    if jid not in BUILDS_JOBS:
        bad.append("environment builds in job %s, only jobs %s may use it" % (jid, " and ".join(BUILDS_JOBS)))
    if "github.event_name == 'push'" not in cond:
        bad.append("job %s: if: does not demand github.event_name == 'push'" % jid)
    for ref in BUILDS_JOBS.get(jid, BUILDS_JOBS["prerelease"]):
        if ref not in cond:
            bad.append("job %s: if: does not name %s" % (jid, ref))
    if "pull_request" in cond or "head_ref" in cond:
        bad.append("job %s: if: lets a pull request in" % jid)
    ok = False
    for st in job.get("steps") or []:
        if str(st.get("uses", "")).startswith("actions/checkout@"):
            ok = str((st.get("with") or {}).get("ref", "")).replace(" ", "") == "${{github.sha}}"
    if not ok:
        bad.append("job %s: the checkout does not take ref: ${{ github.sha }}" % jid)
    return bad


def check_file(path):
    name, bad = os.path.basename(path), []
    text = open(path).read()
    doc = yaml.safe_load(text) or {}
    if not isinstance(doc, dict):
        return ["%s: not a mapping" % name]
    risky = [t for t in triggers(doc) if t in RISKY]
    if risky and names_secret_or_env(doc, text):
        bad.append("%s: trigger %s with a secret or an environment" % (name, ", ".join(risky)))
    if doc.get("permissions") != {"contents": "read"}:
        bad.append("%s: no top-level permissions: contents: read (alone)" % name)
    for jid, job in (doc.get("jobs") or {}).items():
        if not isinstance(job, dict):
            continue
        if env_name(job) == "builds":
            bad += ["%s: %s" % (name, b) for b in prerelease_guard(jid, job)]
        ro = job.get("runs-on")
        if "self-hosted" in str(ro).lower():
            bad.append("%s: job %s runs-on self-hosted" % (name, jid))
        for img in images_of(job):
            if not DIGEST.search(img):
                bad.append("%s: job %s: image %s has no @sha256 digest" % (name, jid, img))
        uses = [job["uses"]] if "uses" in job else []
        uses += [s["uses"] for s in job.get("steps") or [] if "uses" in s]
        for u in map(str, uses):
            if u.startswith("./"):
                continue
            if u.startswith("docker://"):
                if not DIGEST.search(u):
                    bad.append("%s: job %s: %s has no digest" % (name, jid, u))
            elif not SHA40.match(u.partition("@")[2]):
                bad.append("%s: job %s: %s is not pinned by a full commit SHA" % (name, jid, u))
    return bad


def check_dir(d):
    bad = []
    for base, _, files in os.walk(d):
        for f in sorted(files):
            p = os.path.join(base, f)
            if f.endswith((".yml", ".yaml")) and os.path.basename(base) == "workflows":
                bad += check_file(p)
            m = PRIVATE_NAMES.search(open(p, errors="replace").read())
            if m:
                bad.append("%s: names a private repository (%s)" % (os.path.relpath(p, d), m.group(0)))
    return bad


GOOD = """name: t
on:
  push:
    branches: [main]
permissions:
  contents: read
jobs:
  a:
    runs-on: ubuntu-latest
    container:
      image: ghcr.io/x/y@sha256:%s
    steps:
      - uses: actions/checkout@%s
  prerelease:
    runs-on: ubuntu-latest
    environment: builds
    if: ${{ github.event_name == 'push' && (github.ref == 'refs/heads/main' || startsWith(github.ref, 'refs/tags/v')) }}
    steps:
      - uses: actions/checkout@%s
        with:
          ref: ${{ github.sha }}
  sdk-release:
    runs-on: ubuntu-latest
    environment: builds
    if: ${{ github.event_name == 'push' && (github.ref == 'refs/heads/integration' || github.ref == 'refs/heads/main' || github.ref == 'refs/heads/t239.3') }}
    steps:
      - uses: actions/checkout@%s
        with:
          ref: ${{ github.sha }}
""" % ("a" * 64, "b" * 40, "b" * 40, "b" * 40)


def selftest():
    faults = {
        "risky trigger + secret": lambda s: s.replace("on:\n  push:\n    branches: [main]", "on:\n  pull_request_target:").replace("steps:\n      - uses: actions/checkout@" + "b" * 40 + "\n  prerelease", "steps:\n      - run: echo ${{ secrets.X }}\n  prerelease"),
        "risky trigger + environment": lambda s: s.replace("on:\n  push:\n    branches: [main]", "on:\n  workflow_run:\n    workflows: [x]"),
        "builds outside prerelease": lambda s: s.replace("  a:\n    runs-on: ubuntu-latest", "  a:\n    environment: builds\n    runs-on: ubuntu-latest"),
        "builds without push": lambda s: s.replace("github.event_name == 'push' && ", ""),
        "builds without ref list": lambda s: s.replace(" || startsWith(github.ref, 'refs/tags/v')", ""),
        "sdk-release without t239.3": lambda s: s.replace(" || github.ref == 'refs/heads/t239.3'", ""),
        "sdk-release without push": lambda s: s.replace("github.event_name == 'push' && (github.ref == 'refs/heads/integration'", "(github.ref == 'refs/heads/integration'"),
        "builds checkout not own commit": lambda s: s.replace("ref: ${{ github.sha }}", "ref: ${{ github.head_ref }}"),
        "no permissions": lambda s: s.replace("permissions:\n  contents: read\n", ""),
        "permissions write": lambda s: s.replace("contents: read", "contents: write"),
        "permissions extra key": lambda s: s.replace("contents: read", "contents: read\n  packages: write"),
        "action by tag": lambda s: s.replace("actions/checkout@" + "b" * 40, "actions/checkout@v4", 1),
        "docker uses without digest": lambda s: s.replace("      - uses: actions/checkout@" + "b" * 40 + "\n  prerelease", "      - uses: docker://alpine:3\n  prerelease"),
        "image without digest": lambda s: s.replace("@sha256:" + "a" * 64, ":latest"),
        "image short digest": lambda s: s.replace("a" * 64, "a" * 63),
        "self-hosted": lambda s: s.replace("  a:\n    runs-on: ubuntu-latest", "  a:\n    runs-on: [self-hosted, linux]"),
        "private repository name": lambda s: s + "# TRX64U-MultiCore-private\n",
        "builder repository name": lambda s: s + "# trx64mc-builder\n",
    }
    errs = []
    with tempfile.TemporaryDirectory() as td:
        wd = os.path.join(td, "workflows")
        os.makedirs(wd)
        w = os.path.join(wd, "t.yml")
        open(w, "w").write(GOOD)
        if check_dir(td):
            errs.append("the good workflow is refused: %s" % check_dir(td))
        for label, f in faults.items():
            src = f(GOOD)
            if src == GOOD:
                errs.append("%s: the plant changed nothing" % label)
                continue
            open(w, "w").write(src)
            if not check_dir(td):
                errs.append("%s: passed" % label)
        open(w, "w").write(GOOD)
        open(os.path.join(td, "notes.md"), "w").write("see TRX64U-MultiCore-private\n")
        if not check_dir(td):
            errs.append("private name in a non-workflow file passed")
    for e in errs:
        print("selftest FAIL:", e)
    if not errs:
        print("selftest ok (%d faults planted, one rule each, plus the clean file)" % (len(faults) + 1))
    return 1 if errs else 0


if __name__ == "__main__":
    if "--selftest" in sys.argv:
        sys.exit(selftest())
    d = next((a for a in sys.argv[1:] if not a.startswith("-")), os.path.join(ROOT, ".github"))
    bad = check_dir(d)
    for b in bad:
        print("FAIL", b)
    print("workflows: %s" % ("%d findings" % len(bad) if bad else "ok"))
    sys.exit(1 if bad else 0)
