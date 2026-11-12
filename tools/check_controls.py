#!/usr/bin/env python3
"""T174 D5.4 (Alex, 2026-11-08, translated: "The negative controls run in exactly 2 places: flash operations, persistence operations. Nowhere else.
Never."): a negative control (mutant, control loop) exists only in the tests of flash write/erase/update and of persistence (settings files, saves,
journals, anything written to keep). Everywhere else it is deleted, not moved to another tier.

  tools/check_controls.py               fail when a control exists that ci/controls.allow does not allow
  tools/check_controls.py --inventory   every control found: file, name, allowed or not (then the table per file)
  tools/check_controls.py --selftest    the check against generated trees: each way a control could come back fails, the allowed ones pass

What counts as a control (by the call that makes one, not by a comment):
  - shell tests: a line that starts with a control wrapper (control, mutant, bc_control, t139_control, sc_control, t137_ctl, ctl, *_ctl, *_control)
    followed by the control's name - under tests/, host/sim, cores/*/sim, designs/*/sim, ci/
  - ci/sims.txt: a bench whose name ends in _mut / _control / _mutant, an argument mut_* or ctl_*, or MODE=ctl
  - simulation scripts: a case arm or call named mut_* / ctl_*, an output line prefixed "ctl| "
ci/controls.allow and ci/controls.allow.d/*.allow: `<path regex> : <name regex>   # flash|persistence - why`. Both must match, the whole string."""
import os, re, subprocess, sys, tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)

WRAP = r"(?:control|mutant|bc_control|t139_control|sc_control|t137_ctl|ctl|[a-z0-9_]*_ctl|[a-z0-9_]*_control)"
CALL = re.compile(r"^\s*(?:" + WRAP + r")\s+[\"']?([A-Za-z0-9_.$/{}-]+)")
ARM = re.compile(r"^\s*((?:mut|ctl)_[A-Za-z0-9_]+)(?:\|[^)]*)?\)")
CTLOUT = re.compile(r"ctl\|\s")
SCRIPT = re.compile(r"\.(sh|bash)$")
SIMDIRS = ("tests/", "host/sim/", "designs/", "ci/")
SKIP = ("ci/test_", "ci/lint.sh", "tests/ctlcache_selftest.sh")


def tracked(root):
    out = subprocess.run(["git", "-C", root, "ls-files", "-z"], capture_output=True, check=True).stdout.decode("utf-8", "replace")
    return [f for f in out.split("\0") if f]


def wanted(f):
    if not SCRIPT.search(f) or f.startswith(SKIP) or f.startswith("vendor/"):
        return False
    return f.startswith(SIMDIRS) or re.match(r"cores/[^/]+/sim/", f) is not None


def scan(root):
    """[(file, name)] of every control found."""
    found = []
    for f in tracked(root):
        if f == "ci/sims.txt":
            for ln in open(os.path.join(root, f), encoding="utf-8", errors="replace"):
                if ln.startswith("#") or ln.count("|") < 3:
                    continue
                t, bench, _, cmd = ln.rstrip("\n").split("|", 3)
                if re.search(r"(_mut|_control|_mutant)$", bench):
                    found.append((f, bench))
                for tok in cmd.split():
                    if re.match(r"^(mut|ctl)_", tok):
                        found.append((f, "%s:%s" % (bench, tok)))
                    if tok == "MODE=ctl":
                        found.append((f, "%s:MODE=ctl" % bench))
            continue
        if not wanted(f):
            continue
        for ln in open(os.path.join(root, f), encoding="utf-8", errors="replace"):
            if ln.lstrip().startswith("#"):
                continue
            m = CALL.match(ln) or ARM.match(ln)
            if m:
                found.append((f, m.group(1)))
            elif CTLOUT.search(ln):
                found.append((f, "ctl-output"))
    return sorted(set(found))


def load_allow(path):
    rules = []
    paths = [path] + sorted(os.path.join(path + ".d", x) for x in (os.listdir(path + ".d") if os.path.isdir(path + ".d") else []) if x.endswith(".allow"))
    lines = [ln for p in paths if os.path.exists(p) for ln in open(p, encoding="utf-8")]
    for ln in lines:
        body = ln.split("#", 1)[0].strip()
        if not body:
            continue
        if " : " not in body:
            raise SystemExit("ci/controls.allow: line without ' : ': %s" % ln.strip())
        a, b = body.split(" : ", 1)
        rules.append((re.compile(a.strip()), re.compile(b.strip())))
    return rules


def allowed(rules, f, name):
    return any(a.fullmatch(f) and b.fullmatch(name) for a, b in rules)


def check(root):
    rules = load_allow(os.path.join(root, "ci", "controls.allow"))
    found = scan(root)
    return [(f, n) for f, n in found if not allowed(rules, f, n)], found, rules


def selftest():
    fails = []

    def mk(files, allow):
        d = tempfile.mkdtemp()
        subprocess.run(["git", "init", "-q", d], check=True)
        for p, body in files.items():
            os.makedirs(os.path.dirname(os.path.join(d, p)) or d, exist_ok=True)
            open(os.path.join(d, p), "w").write(body)
        os.makedirs(os.path.join(d, "ci"), exist_ok=True)
        open(os.path.join(d, "ci", "controls.allow"), "w").write(allow)
        subprocess.run(["git", "-C", d, "add", "-A"], check=True)
        return d

    def expect(name, files, allow, must_fail):
        bad, _, _ = check(mk(files, allow))
        print("selftest %-44s %s" % (name, ("FAIL as required: %s %s" % bad[0]) if bad else "pass"))
        if bool(bad) != must_fail:
            fails.append(name)
    flash = "# flash\ncontrol flash_erase_skipped x.cc 's/a/b/' t.cc\n"
    sims = "1|b1|60|sh cores/z/sim/run.sh a\n"
    expect("no control at all", {"tests/a/run.sh": "echo hi\n"}, "", False)
    expect("a control in an app suite", {"tests/a/run.sh": "control ui_x f.cc 's/a/b/' t.cc\n"}, "", True)
    expect("the same control, allowed (flash)", {"tests/a/run.sh": flash}, "tests/a/run.sh : flash_.* # flash\n", False)
    expect("allowance does not cover another name", {"tests/a/run.sh": flash + "control ui_x f.cc 's/a/b/' t.cc\n"}, "tests/a/run.sh : flash_.*\n", True)
    expect("allowance does not cover another file", {"tests/b/run.sh": flash}, "tests/a/run.sh : flash_.*\n", True)
    expect("a mutant wrapper", {"tests/h/run.sh": "mutant cl_x \"$H/cor.cc\" 's/a/b/' t.cc\n"}, "", True)
    expect("a comment naming a control is none", {"tests/h/run.sh": "# control ui_x f.cc\n"}, "", False)
    expect("a bench control mode in sims.txt", {"ci/sims.txt": sims.replace(" a", " a mut_a")}, "", True)
    expect("a *_mut bench in sims.txt", {"ci/sims.txt": "1|b1_mut|60|sh cores/z/sim/run.sh a\n"}, "", True)
    expect("MODE=ctl in sims.txt", {"ci/sims.txt": "1|b1|60|MODE=ctl sh host/sim/x/run.sh\n"}, "", True)
    expect("a plain bench in sims.txt", {"ci/sims.txt": sims}, "", False)
    expect("a mut_ case arm in a sim script", {"cores/z/sim/run.sh": "case $1 in\n  mut_a) sed x;;\nesac\n"}, "", True)
    expect("a ctl| output line in a sim script", {"host/sim/x/run.sh": "echo \"ctl| it failed\"\n"}, "", True)
    if fails:
        print("selftest FAILED: " + ", ".join(fails)); return 1
    print("selftest ok"); return 0


def main(a):
    if "--selftest" in a:
        return selftest()
    bad, found, rules = check(ROOT)
    if "--inventory" in a:
        per = {}
        for f, n in found:
            per.setdefault(f, [0, 0])[0 if allowed(rules, f, n) else 1] += 1
            print("%-7s %s  %s" % ("keep" if allowed(rules, f, n) else "REMOVE", f, n))
        print("\n%-60s %6s %8s" % ("file", "kept", "to remove"))
        for f in sorted(per):
            print("%-60s %6d %8d" % (f, per[f][0], per[f][1]))
        print("%-60s %6d %8d" % ("total", sum(v[0] for v in per.values()), sum(v[1] for v in per.values())))
        return 0
    for f, n in bad:
        print("FAIL control %s in %s: negative controls exist only in flash and persistence tests (ci/controls.allow)" % (n, f))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
