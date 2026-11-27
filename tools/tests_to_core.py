#!/usr/bin/env python3
"""T239 D17 (Alex 2026-11-27): a core's tests leave the shared harness for the core's own folder, mechanically.

    tools/tests_to_core.py <core> [--no-run] [--commit]
    tools/tests_to_core.py --all [--commit]          every core with an app wrapper in tests/coreapp/run.sh, one after the other
    tools/tests_to_core.py --list                    the cores this script can find (wrapper, include array, TEST_APP_ symbol)

For a core <id> (the wrapper of tests/coreapp/run.sh whose CA_APP is <id>; its symbol TEST_APP_<X> is the -D of its include array INC_<X>):
 1. its lines of tests/coreapp/run.sh go to cores/<id>/app/tests/units.sh, which run.sh sources at its end: the wrapper, the include array, the driver table, the
    library variable, every variable only its lines use, its unit and control lines, the functions that use any of these and the lines that call them, and the
    comment lines directly above each;
 2. a test file under tests/coreapp with a TEST_APP_<X> branch: the core's copy (unifdef -D TEST_APP_<X> -U every other TEST_APP_ symbol) goes to the core's folder
    when it is a header or the core's lines name it (and so does a test program of the core's lines that includes such a header); the shared file loses the
    branches (unifdef -U TEST_APP_<X>: a negative condition loses the core's term);
 3. the files only the core's lines name (its own test programs, its driver table script) and its goldens (golden/*_<id>.*: the core's build then reads its
    GOLDEN_DIR in its folder) move there (git mv);
 4. the core's units run (CA_GROUP=<id> tests/coreapp/run.sh, every unit and control of the group). A red run, a run whose last line is not run.sh's
    summary, a unit of its lines that did not run, or a script that does not parse undoes every change; the core is reported with the error lines. --commit commits a green core (tools/check_todo.py first).
Nothing here names a core: the cores are what run.sh's wrappers say.
"""
import os
import re
import subprocess
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
RUN = os.path.join(ROOT, "tests", "coreapp", "run.sh")
TST = os.path.join(ROOT, "tests", "coreapp")
LOGDIR = os.environ.get("TTC_LOGDIR", os.path.join(os.environ.get("TMPDIR", "/tmp"), "tests_to_core"))

WRAP = re.compile(r'^(\w+)\(\) \{ local INC=\("\$\{(INC_(\w+))\[@\]\}"\)(.*?)CA_APP=(\w+)')
VARDEF = re.compile(r'^([A-Za-z_]\w*)=')
FUNC = re.compile(r'^(\w+)\(\) \{\s*$')
CMD = re.compile(r'^(?:[A-Za-z_]\w*=(?:"[^"]*"|\'[^\']*\'|\S*)\s+)*(\w+)\b')


class Fail(Exception):
    pass


def sh(cmd, **kw):
    return subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True, **kw)


def git(*a):
    r = sh(["git"] + list(a))
    if r.returncode:
        raise Fail("git %s: %s" % (" ".join(a), r.stderr.strip()))
    return r.stdout


def wrappers(lines):
    out = []
    for i, ln in enumerate(lines):
        m = WRAP.match(ln)
        if m:
            out.append(dict(line=i, wrapper=m.group(1), inc=m.group(2), x=m.group(3), id=m.group(5), rest=ln))
    return out


def uses(text, var):
    return re.search(r'\$\{?%s\b' % re.escape(var), text) is not None


def blocks(lines):
    """the multi-line functions of run.sh: name -> (first, last) line"""
    out, i = {}, 0
    while i < len(lines):
        m = FUNC.match(lines[i])
        if m:
            j = i + 1
            while j < len(lines) and lines[j].rstrip() != "}":
                j += 1
            out[m.group(1)] = (i, j)
            i = j
        i += 1
    return out


def core_lines(lines, w):
    """the indices of run.sh that are the core's, and its macro and driver files"""
    C = set([w["line"]])
    iv = w["inc"]
    defs = [i for i, ln in enumerate(lines) if ln.startswith(iv + "=(") or (ln.startswith("for i in ") and (iv + "+=") in ln)]
    if len(defs) != 2:
        raise Fail("run.sh: the include array %s is not one definition and one for line (%d lines)" % (iv, len(defs)))
    C.update(defs)
    m = re.search(r'-D(TEST_APP_\w+)=1', lines[defs[0]])
    if not m:
        raise Fail("run.sh: %s has no -DTEST_APP_...=1" % iv)
    macro = m.group(1)
    tab = re.search(r'-I"\$T/(\w+)/drv"', lines[defs[0]])
    if tab:
        for i, ln in enumerate(lines):
            if not ln.startswith(" ") and ('"$T/%s"' % tab.group(1)) in ln and i not in defs:
                C.add(i)
                # the script that defines the call's function goes with it when nothing else names that script
                fn = ln.split()[0]
                for k, s in enumerate(lines):
                    m2 = re.match(r'^source "\$TST/(\S+)"', s)
                    if not m2 or not re.search(r'^%s\(\)' % re.escape(fn), open(os.path.join(TST, m2.group(1))).read(), re.M):
                        continue
                    other = [j for j, t in enumerate(lines) if j != k and m2.group(1) in t]
                    refs = [r for r in sh(["git", "grep", "-lwF", m2.group(1), "--", ".", ":!docs", ":!*.md", ":!cores/%s" % w["id"]]).stdout.split() if r not in ("tests/coreapp/run.sh", "tests/coreapp/" + m2.group(1))]
                    if not other and not refs:
                        C.add(k)
    funcs = blocks(lines)
    infunc = {}
    for name, (a, b) in funcs.items():
        for k in range(a, b + 1):
            infunc[k] = name
    # the unit and control lines: the first command word is the wrapper
    for i, ln in enumerate(lines):
        if i in infunc:
            continue
        m = CMD.match(ln)
        if m and m.group(1) == w["wrapper"]:
            C.add(i)
    # closure: variables only the core's lines use, functions that use the core's names, the lines that call them
    vardefs = {}
    for i, ln in enumerate(lines):
        if i in infunc:
            continue
        m = VARDEF.match(ln)
        if m and not ln.startswith("for "):
            vardefs.setdefault(m.group(1), []).append(i)
    names = set([w["wrapper"], iv])
    lib = re.search(r'APPLIB="\$(\w+)"', w["rest"])
    changed = True
    while changed:
        changed = False
        for v, dl in vardefs.items():
            if set(dl) <= C:
                names.add(v)
                continue
            us = [i for i, ln in enumerate(lines) if i not in dl and uses(ln, v)]
            if us and set(us) <= C:
                C.update(dl)
                names.add(v)
                changed = True
        for fn, (a, b) in funcs.items():
            body = "\n".join(lines[a:b + 1])
            inside = set(range(a, b + 1))
            if inside <= C:
                continue
            if any(re.search(r'\b%s\b' % re.escape(n), body) for n in names if n != fn) or re.search(r'\bca_in %s\b' % re.escape(w["id"]), body):
                C.update(inside)
                names.add(fn)
                changed = True
        for i, ln in enumerate(lines):
            if i in C or i in infunc:
                continue
            if any(re.search(r'\b%s\b' % re.escape(n), ln) for n in names if n in funcs) or re.search(r'^if ca_in %s\b' % re.escape(w["id"]), ln):
                C.add(i)
                changed = True
    # continuation lines and here-documents of a moved top-level line
    for i in sorted(C):
        k = i
        while lines[k].rstrip().endswith("\\") and k + 1 < len(lines):
            k += 1
            C.add(k)
        h = re.search(r"<<'?(\w+)'?", lines[i])
        if h and i not in infunc:
            k = i + 1
            while k < len(lines) and lines[k].rstrip() != h.group(1):
                C.add(k)
                k += 1
            C.add(k)
    # the comment lines directly above a moved line
    for i in sorted(C):
        k = i - 1
        while k >= 0 and lines[k].lstrip().startswith("#") and k not in C:
            k -= 1
        if k + 1 < i:
            C.update(range(k + 1, i))
    if lib and lib.group(1) not in names:
        raise Fail("run.sh: the library variable $%s of %s is used by other lines too" % (lib.group(1), w["wrapper"]))
    return C, macro


def unifdef(path, args):
    r = subprocess.run(["unifdef"] + args + [path], capture_output=True, text=True)
    if r.returncode not in (0, 1):
        raise Fail("unifdef %s %s: %s" % (" ".join(args), path, r.stderr.strip()))
    return r.stdout


def run_group(cid, tag):
    os.makedirs(LOGDIR, exist_ok=True)
    log = os.path.join(LOGDIR, "%s.%s.log" % (cid, tag))
    env = dict(os.environ, CA_GROUP=cid)
    env.pop("CA_UNITS", None)
    env.pop("TEST_TIER", None)
    with open(log, "w") as f:
        rc = subprocess.run(["nice", "-n", "15", "bash", RUN], cwd=ROOT, env=env, stdout=f, stderr=subprocess.STDOUT).returncode
    return rc, log


def tail_errors(log, n=12):
    ls = open(log, errors="replace").read().splitlines()
    bad = [l for l in ls if re.search(r'FAIL|error|Error|CONTROL |missing|not generated|No such', l)]
    return (bad or ls)[-n:]


def revert(cid):
    sh(["git", "reset", "-q", "HEAD", "--", "tests", "cores/%s/app/tests" % cid])
    sh(["git", "checkout", "-q", "HEAD", "--", "tests"])
    sh(["git", "clean", "-fdq", "--", "tests", "cores/%s/app/tests" % cid])
    if git("ls-tree", "HEAD", "cores/%s/app/tests" % cid).strip():
        sh(["git", "checkout", "-q", "HEAD", "--", "cores/%s/app/tests" % cid])


def convert(cid, run=True, commit=False):
    if sh(["git", "status", "--porcelain", "--", "tests", "cores/%s" % cid]).stdout.strip():
        raise Fail("tests/ or cores/%s has uncommitted changes" % cid)
    lines = open(RUN).read().split("\n")
    ws = [w for w in wrappers(lines) if w["id"] == cid]
    if len(ws) != 1:
        raise Fail("run.sh has %d wrappers with CA_APP=%s" % (len(ws), cid))
    w = ws[0]
    dest = os.path.join(ROOT, "cores", cid, "app", "tests")
    if not os.path.isdir(os.path.join(ROOT, "cores", cid, "app")):
        raise Fail("cores/%s/app does not exist" % cid)
    if os.path.exists(os.path.join(dest, "units.sh")):
        raise Fail("cores/%s/app/tests/units.sh exists already" % cid)
    C, macro = core_lines(lines, w)
    x = w["x"]
    tv = "TESTS_" + x
    moved_text = "\n".join(lines[i] for i in sorted(C))
    words = set(re.findall(r'[\w.]+', moved_text))
    os.makedirs(dest, exist_ok=True)
    # 2. the files with the core's branches
    with_macro = [f for f in sh(["git", "grep", "-lw", macro, "--", "tests"]).stdout.split() if f.startswith("tests/coreapp/") and f.endswith((".cc", ".h", ".c", ".cpp"))]   # a script that names the symbol in a comment is no C
    others = set(re.findall(r'TEST_APP_\w+', "".join(open(os.path.join(ROOT, f)).read() for f in with_macro))) - {macro}
    copied = set()
    for f in with_macro:
        b = os.path.basename(f)
        if b.endswith(".h") or b in words:
            copied.add(b)
    # a test program of the core's lines that includes a copied header is copied too (a quoted include looks in the includer's folder first)
    for b in sorted(words):
        p = os.path.join(TST, b)
        if b not in copied and b.endswith(".cc") and os.path.isfile(p):
            inc = set(re.findall(r'#include "([^"]+)"', open(p).read()))
            if inc & {c for c in copied if c.endswith(".h")}:
                copied.add(b)
    for b in sorted(copied):
        src = os.path.join(TST, b)
        txt = unifdef(src, ["-D" + macro] + ["-U" + o for o in sorted(others)])
        open(os.path.join(dest, b), "w").write(txt)
    for f in with_macro:
        p = os.path.join(ROOT, f)
        open(p, "w").write(unifdef(p, ["-U" + macro]))
    # 3. the files only the core's lines name, and its goldens
    rest_text = "\n".join(lines[i] for i in range(len(lines)) if i not in C)
    moved = set()
    for b in sorted(words):
        p = os.path.join(TST, b)
        if b in copied or not os.path.isfile(p) or b == "run.sh":
            continue
        if re.search(r'(?<![\w.])%s(?![\w])' % re.escape(b), rest_text):
            continue
        refs = [r for r in sh(["git", "grep", "-lwF", b, "--", ".", ":!docs", ":!*.md", ":!cores/%s" % cid]).stdout.split() if r not in ("tests/coreapp/run.sh", "tests/coreapp/" + b)]
        if refs:
            continue
        git("mv", "tests/coreapp/" + b, "cores/%s/app/tests/%s" % (cid, b))
        moved.add(b)
    gold = []
    gre = re.compile(r'(^|_)%s(\.|_)' % re.escape(cid))
    for g in sorted(os.listdir(os.path.join(TST, "golden"))):
        if not gre.search(g):
            continue
        if sh(["git", "grep", "-lF", g, "--", ".", ":!docs", ":!*.md", ":!cores/%s" % cid]).stdout.strip():
            continue
        os.makedirs(os.path.join(dest, "golden"), exist_ok=True)
        git("mv", "tests/coreapp/golden/" + g, "cores/%s/app/tests/golden/%s" % (cid, g))
        gold.append(g)
    # 1. units.sh
    out = []
    for i in sorted(C):
        ln = lines[i]
        ln = ln.replace('local INC=("${%s[@]}")' % w["inc"], 'local INC=("${%s[@]}") CORE_TESTS="$%s"' % (w["inc"], tv))
        if ln.startswith(w["inc"] + "=("):
            add = '-I"$%s" ' % tv + ('-DGOLDEN_DIR=\\"$%s/golden\\" ' % tv if gold else "")
            ln = re.sub(r'(-D%s=1 )' % macro, r'\g<1>' + add.replace("\\", "\\\\"), ln, count=1)
        if gold and ln.startswith("for i in ") and (w["inc"] + "+=") in ln:
            old = '[ "$i" = "-I$T/drv" ] || %s+=("$i")' % w["inc"]
            if old not in ln:
                raise Fail("run.sh: the for line of %s has another form; its goldens cannot move" % w["inc"])
            ln = ln.replace(old, 'case "$i" in "-I$T/drv"|-DGOLDEN_DIR=*) ;; *) %s+=("$i") ;; esac' % w["inc"])
        for b in copied | moved:
            ln = ln.replace("$TST/" + b, "$%s/%s" % (tv, b))
        out.append(ln)
    hdr = ["# shellcheck shell=bash",
           "# The units of %s (T239 D17), moved out of tests/coreapp/run.sh by tools/tests_to_core.py. tests/coreapp/run.sh sources this file at its end, with every variable" % cid,
           "# and function of the shared harness set; the test programs, sources and headers of this folder win over the shared ones for this app (CORE_TESTS, find_src, tpath).",
           '%s="$ROOT/cores/%s/app/tests"' % (tv, cid)]
    open(os.path.join(dest, "units.sh"), "w").write("\n".join(hdr + out) + "\n")
    open(RUN, "w").write("\n".join(lines[i] for i in range(len(lines)) if i not in C))
    for p in (RUN, os.path.join(dest, "units.sh")):
        r = sh(["bash", "-n", p])
        if r.returncode:
            raise Fail("bash -n %s: %s" % (p, r.stderr.strip()))
    left = [l for l in sh(["git", "grep", "-nw", macro, "--", "tests"]).stdout.splitlines() if not l.startswith("cores/")]
    if any(not l.split(":", 2)[2].lstrip().startswith(("#", "//")) for l in left):
        raise Fail("%s still in code of a shared file: %s" % (macro, left[:3]))
    units = re.findall(r'^(?:\S+=\S*\s+)*%s\s+(?:run|control)\s+(\S+)' % re.escape(w["wrapper"]), "\n".join(out), re.M)
    summary = "%s: %d run.sh lines, %d copies (%s), %d files moved (%s), %d goldens, %d units" % (
        cid, len(C), len(copied), " ".join(sorted(copied)), len(moved), " ".join(sorted(moved)), len(gold), len(units))
    if not run:
        return summary + " (not run)"
    rc, log = run_group(cid, "after")
    text = open(log, errors="replace").read()
    missing = [u for u in units if not re.search(r'^(test|control) %s:' % re.escape(u), text, re.M)]
    last = [l for l in text.splitlines() if l.strip()][-1:] or [""]
    summ = re.match(r'^tests/coreapp: \d+ test programs passed, \d+ parallel controls', last[0])
    if rc or missing or not summ:
        msg = ["red run (exit %d), log %s" % (rc, log)] if rc else []
        if not rc and not summ:
            msg.append("red run: the last line is not the summary of tests/coreapp/run.sh (a control may have failed unseen), log %s" % log)
        if missing:
            msg.append("units of its lines that did not run: " + " ".join(missing))
        raise Fail("; ".join(msg) + "\n    " + "\n    ".join(tail_errors(log)))
    if commit:
        git("add", "-u", "--", "tests")
        git("add", "--", "cores/%s/app/tests" % cid)
        r = sh([sys.executable, "tools/check_todo.py"])
        if r.returncode:
            raise Fail("tools/check_todo.py: " + (r.stdout + r.stderr).strip()[-300:])
        git("commit", "-q", "-m", "T239 D17: %s's tests into cores/%s/app/tests (tools/tests_to_core.py)\n\n%s" % (cid, cid, summary))
    return summary + "; green (log %s)" % log


def main(argv):
    lines = open(RUN).read().split("\n")
    if argv[:1] == ["--list"]:
        for w in wrappers(lines):
            print(w["id"], w["wrapper"], w["inc"])
        return 0
    run = "--no-run" not in argv
    commit = "--commit" in argv
    ids = [a for a in argv if not a.startswith("--")]
    if "--all" in argv:
        ids = [w["id"] for w in wrappers(lines)]
    if not ids:
        print(__doc__)
        return 2
    done, failed = [], []
    for cid in ids:
        print("=== %s" % cid, flush=True)
        try:
            s = convert(cid, run=run, commit=commit)
            print("done: " + s, flush=True)
            done.append(cid)
        except Fail as e:
            print("FAILED: %s: %s" % (cid, e), flush=True)
            revert(cid)
            if run and "red run" in str(e):
                rc, log = run_group(cid, "before")
                print("    the group before the move: %s (log %s)" % ("green" if rc == 0 else "red too (exit %d)" % rc, log), flush=True)
            failed.append(cid)
    print("tests_to_core: done %s; failed %s" % (" ".join(done) or "-", " ".join(failed) or "-"))
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
