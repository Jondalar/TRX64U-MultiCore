#!/usr/bin/env python3
"""T161, T238: the GitHub issue forms are well formed and their core lists are the tree's.

  tools/check_issue_forms.py [--selftest]

Checks (no YAML library, the forms are written in the narrow shape GitHub documents):
  - every form has name, description and a body; every input, dropdown and textarea has an id, a label and required: true
    (except the fields in OPTIONAL: "more" of both bug forms, and the photos, T238 D3), ids are unique, dropdowns have options;
  - config.yml switches blank issues off;
  - the bug form's area dropdown names exactly the supported areas (Alex 2026-11-14, CONTRIBUTING.md, discussion #66): host,
    official firmware, generic core application, trx64u and trx64g;
  - the experimental core form (T238 D1, D4) exists and its core dropdown names exactly the shipped cores (core.toml read by
    tools/core_toml.py: in the pipeline, not build_only, not a flash filler) without the supported areas and without the cores
    in EXCLUDED (the Port Diagnostics core and the two DDT demos: Discussions only).
--selftest runs the checks against broken copies (a field without id, a core missing from either form, an excluded core in the
experimental form, blank issues on) and fails if they pass.
"""
import glob, os, re, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))
import core_toml  # noqa: E402

DIR = os.path.join(ROOT, ".github", "ISSUE_TEMPLATE")
EXP = "experimental.yml"
OPTIONAL = {("bug.yml", "more"), ("bug.yml", "photos"), (EXP, "more"), (EXP, "photos")}
SUPPORTED = {"host", "firmware", "coreapp", "trx64u", "trx64g"}
EXCLUDED = {"diag", "ddt_mandel", "ddt_novic"}     # T238 D4 (Alex 2026-11-19): no issue form, Discussions only


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


def experimental():
    """The cores a user starts from a .cor that are neither supported nor excluded (the same filter as tools/gen_roms_txt.py)."""
    out = set()
    for name, _, t in core_toml.descriptors() + core_toml.large_descriptors():
        if core_toml.in_pipeline(t) and not t.get("flash_filler") and not t.get("build_only"):
            out.add(name)
    return out - SUPPORTED - EXCLUDED


def compare(form, what, want, opts):
    errs = []
    got = {o.split(" - ", 1)[0] for o in opts}
    for c in sorted(want - got):
        errs.append("%s: %s %s is not in the form" % (form, what, c))
    for c in sorted(got - want):
        errs.append("%s: %s is in the form but is not a %s" % (form, c, what))
    return errs


def check(forms, config, cores, exp):
    errs = []
    if not re.search(r"^blank_issues_enabled: false$", config, re.M):
        errs.append("config.yml: blank issues are not switched off")
    if EXP not in forms:
        errs.append("%s: the experimental core form is missing" % EXP)
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
                errs += compare(name, "supported area", cores, opts)
            if name == EXP and fid == "core":
                errs += compare(name, "experimental core", exp, opts)
    return errs


def main():
    forms = {os.path.basename(p): open(p).read() for p in sorted(glob.glob(os.path.join(DIR, "*.yml"))) if not p.endswith("config.yml")}
    config = open(os.path.join(DIR, "config.yml")).read()
    cores, exp = shipped(), experimental()
    errs = check(forms, config, cores, exp)
    for e in errs:
        print(e)
    if errs:
        return 1
    if "--selftest" in sys.argv:
        bad = dict(forms, **{"bug.yml": forms["bug.yml"].replace("    id: steps\n", "", 1)})
        if not check(bad, config, cores, exp):
            print("control: a field without id passed")
            return 1
        if not check(forms, config, cores | {"newcore"}, exp):
            print("control: a supported area missing from the bug form passed")
            return 1
        if not check(forms, config, cores, exp | {"newcore"}):
            print("control: a core missing from the experimental form passed")
            return 1
        first = re.search(r"^        - \S+ - .+\n", forms[EXP].split("    id: core\n", 1)[1], re.M).group(0)
        bad = dict(forms, **{EXP: forms[EXP].replace(first, first + "        - diag - Port Diagnostics\n", 1)})
        if not check(bad, config, cores, exp):
            print("control: an excluded core in the experimental form passed")
            return 1
        if not check(forms, config.replace("false", "true"), cores, exp):
            print("control: blank issues on passed")
            return 1
        print("issue forms: 5 controls fail as required")
    print("issue forms ok (%d forms, %d supported areas, %d experimental cores)" % (len(forms), len(cores), len(exp)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
