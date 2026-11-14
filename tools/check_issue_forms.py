#!/usr/bin/env python3
"""T161: the GitHub issue forms are well formed and the bug form's core list is the tree's.

  tools/check_issue_forms.py [--selftest]

Checks (no YAML library, the forms are written in the narrow shape GitHub documents):
  - every form has name, description and a body; every input, dropdown and textarea has an id, a label and required: true
    (except the field "more" of the bug form), ids are unique, dropdowns have options;
  - config.yml switches blank issues off;
  - the bug form's area dropdown names exactly the supported areas (Alex 2026-11-14, CONTRIBUTING.md, discussion #66): host,
    official firmware, generic core application, trx64u and trx64g; every other core has no support and no issue.
--selftest runs the checks against broken copies (a field without id, a core missing from the form) and fails if they pass.
"""
import glob, os, re, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DIR = os.path.join(ROOT, ".github", "ISSUE_TEMPLATE")
OPTIONAL = {("bug.yml", "more")}
SUPPORTED = {"host", "firmware", "coreapp", "trx64u", "trx64g"}


def fields(text):
    out = []
    for blk in re.split(r"\n  - type: ", "\n" + text.split("\nbody:\n", 1)[-1])[1:]:
        kind = blk.split("\n", 1)[0].strip()
        fid = re.search(r"^    id: (\S+)", blk, re.M)
        label = re.search(r"^      label: (.+)", blk, re.M)
        req = re.search(r"^      required: true", blk, re.M)
        opts = re.findall(r"^        - (.+)", blk.split("options:", 1)[1], re.M) if "options:" in blk else []
        out.append((kind, fid and fid.group(1), label and label.group(1), bool(req), opts))
    return out


def shipped():
    return set(SUPPORTED)


def check(forms, config, cores):
    errs = []
    if not re.search(r"^blank_issues_enabled: false$", config, re.M):
        errs.append("config.yml: blank issues are not switched off")
    for name, text in forms.items():
        for key in ("name", "description"):
            if not re.search(r"^%s: \S" % key, text, re.M):
                errs.append("%s: no %s" % (name, key))
        seen = set()
        for kind, fid, label, req, opts in fields(text):
            if kind == "markdown":
                continue
            if not fid or not label:
                errs.append("%s: a %s without id or label" % (name, kind))
                continue
            if fid in seen:
                errs.append("%s: id %s twice" % (name, fid))
            seen.add(fid)
            if not req and (name, fid) not in OPTIONAL:
                errs.append("%s: %s is not required" % (name, fid))
            if kind == "dropdown" and not opts:
                errs.append("%s: dropdown %s has no options" % (name, fid))
            if name == "bug.yml" and fid == "core":
                got = {o.split(" - ", 1)[0] for o in opts}
                for c in sorted(cores - got):
                    errs.append("bug.yml: supported area %s is not in the form" % c)
                for c in sorted(got - cores):
                    errs.append("bug.yml: %s is in the form but is not a supported area" % c)
    return errs


def main():
    forms = {os.path.basename(p): open(p).read() for p in sorted(glob.glob(os.path.join(DIR, "*.yml"))) if not p.endswith("config.yml")}
    config = open(os.path.join(DIR, "config.yml")).read()
    cores = shipped()
    errs = check(forms, config, cores)
    for e in errs:
        print(e)
    if errs:
        return 1
    if "--selftest" in sys.argv:
        bad = dict(forms, **{"bug.yml": forms["bug.yml"].replace("    id: steps\n", "", 1)})
        if not check(bad, config, cores):
            print("control: a field without id passed")
            return 1
        if not check(forms, config, cores | {"newcore"}):
            print("control: a core missing from the form passed")
            return 1
        if not check(forms, config.replace("false", "true"), cores):
            print("control: blank issues on passed")
            return 1
        print("issue forms: 3 controls fail as required")
    print("issue forms ok (%d forms, %d cores)" % (len(forms), len(cores)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
