#!/usr/bin/env python3
"""T174 D3: a change to the keying or the verdict of the test cache must raise CC_KEY_VERSION in the same commit.

A unit's key (tests/ctlcache.sh, ctlcache.py) carries CC_KEY_VERSION instead of the bytes of the harness files. That is only sound when the number
moves whenever the harness's behaviour does. This check compares, commit by commit over BASE..HEAD (and the working tree against HEAD), the CODE of

    tests/ctlcache.sh   tests/ctlcache.py   tests/readlog.c

- comments, blank lines and the CC_KEY_VERSION line left out (shell: full-line and trailing ' # ...' comments; python: the syntax tree without
docstrings; C: /* */ and // comments) - and refuses a step whose code differs while CC_KEY_VERSION did not go up, or went down.

  tools/check_cc_key_version.py [--root DIR] [--base REF]    check the repository (default: the current one, base = merge-base with origin/main, else HEAD~1)
  tools/check_cc_key_version.py selftest                     the scenarios on a throw-away repository, each with its control
"""
import ast
import hashlib
import os
import re
import shutil
import subprocess
import sys
import tempfile

FILES = ("tests/ctlcache.sh", "tests/ctlcache.py", "tests/readlog.c")
VER = re.compile(r"^CC_KEY_VERSION=(\d+)\s*$", re.M)
HERE = os.path.dirname(os.path.realpath(__file__))


def norm_sh(text):
    out = []
    for ln in text.split("\n"):
        if re.match(r"\s*#", ln) or not ln.strip() or ln.startswith("CC_KEY_VERSION="):
            continue
        ln = re.sub(r"\s+#(\s.*)?$", "", ln)
        out.append(ln.rstrip())
    return "\n".join(out)


def norm_py(text):
    try:
        tree = ast.parse(text)
    except SyntaxError:
        return text
    for node in ast.walk(tree):
        if isinstance(node, (ast.Module, ast.ClassDef, ast.FunctionDef, ast.AsyncFunctionDef)):
            b = node.body
            if b and isinstance(b[0], ast.Expr) and isinstance(getattr(b[0], "value", None), ast.Constant) and isinstance(b[0].value.value, str):
                node.body = b[1:] or [ast.Pass()]
    return ast.dump(tree, annotate_fields=False)


def norm_c(text):
    text = re.sub(r"/\*.*?\*/", " ", text, flags=re.S)
    text = re.sub(r"//[^\n]*", "", text)
    return "\n".join(ln.rstrip() for ln in text.split("\n") if ln.strip())


def logic_hash(contents, ignore=False):
    if ignore:
        return ""
    h = hashlib.sha256()
    for f in FILES:
        t = contents.get(f)
        n = "<missing>" if t is None else (norm_sh(t) if f.endswith(".sh") else norm_py(t) if f.endswith(".py") else norm_c(t))
        h.update(f.encode() + b"\0" + n.encode() + b"\0")
    return h.hexdigest()


def version(contents):
    m = VER.search(contents.get("tests/ctlcache.sh") or "")
    return int(m.group(1)) if m else None


def git(root, *args):
    r = subprocess.run(["git", "-C", root] + list(args), capture_output=True, text=True)
    return r.returncode, r.stdout


def at_rev(root, rev):
    out = {}
    for f in FILES:
        rc, t = git(root, "show", "%s:%s" % (rev, f))
        out[f] = t if rc == 0 else None
    return out


def in_tree(root):
    out = {}
    for f in FILES:
        p = os.path.join(root, f)
        out[f] = open(p, encoding="utf-8", errors="replace").read() if os.path.exists(p) else None
    return out


def step(label, old, new, ignore):
    if all(v is None for v in old.values()):
        return []
    vo, vn = version(old), version(new)
    if vn is None and vo is None:
        return []   # before the number existed
    if vn is None:
        return ["%s: tests/ctlcache.sh has no CC_KEY_VERSION=<number> line" % label]
    if vo is not None and vn < vo:
        return ["%s: CC_KEY_VERSION went down (%d -> %d)" % (label, vo, vn)]
    if logic_hash(old, ignore) != logic_hash(new, ignore) and (vo is not None and vn <= vo):
        return ["%s: the code of the test cache harness (%s) changed but CC_KEY_VERSION is still %s - raise it in the same commit "
                "(a unit key carries the number, not the bytes)" % (label, ", ".join(FILES), vn)]
    return []


def check(root, base=None, ignore=False):
    bad = []
    if base is None:
        rc, mb = git(root, "merge-base", "HEAD", "origin/main")
        base = mb.strip() if rc == 0 and mb.strip() else None
    rc, head = git(root, "rev-parse", "HEAD")
    head = head.strip()
    revs = []
    if base and base != head:
        rc, out = git(root, "rev-list", "--reverse", "--first-parent", "%s..HEAD" % base)
        revs = out.split()
    elif git(root, "rev-parse", "--verify", "-q", "HEAD~1")[0] == 0:
        revs = [head]
    for r in revs:
        rc, par = git(root, "rev-list", "--parents", "-n1", r)
        parts = par.split()
        if len(parts) != 2:
            continue   # a merge or a root commit: its sides were checked on their own
        bad += step(r[:8], at_rev(root, parts[1]), at_rev(root, r), ignore)
    bad += step("working tree", at_rev(root, "HEAD"), in_tree(root), ignore)
    return bad


def selftest():
    src_root = os.path.realpath(os.path.join(HERE, ".."))
    fails = []
    with tempfile.TemporaryDirectory() as w:
        def g(*a):
            r = subprocess.run(["git", "-C", w, "-c", "user.name=t", "-c", "user.email=t@t"] + list(a), capture_output=True, text=True)
            if r.returncode:
                sys.exit("selftest: git %s: %s" % (" ".join(a), r.stderr))
        g("init", "-q")
        os.makedirs(os.path.join(w, "tests"))
        for f in FILES:
            shutil.copy(os.path.join(src_root, f), os.path.join(w, f))
        g("add", "-A"); g("commit", "-q", "-m", "base")

        def edit(f, fn):
            p = os.path.join(w, f); s = open(p).read(); n = fn(s)
            if n == s:
                sys.exit("selftest: the edit of %s changed nothing" % f)
            open(p, "w").write(n)

        def commit_check(label, expect_bad, ignore=False):
            g("add", "-A"); g("commit", "-q", "-m", label)
            bad = check(w, base="HEAD~1", ignore=ignore)
            if bool(bad) != expect_bad:
                fails.append("%s: expected %s, got %s" % (label, "a refusal" if expect_bad else "ok", bad or "ok"))
            return bad

        bump = lambda s: re.sub(r"^CC_KEY_VERSION=(\d+)$", lambda m: "CC_KEY_VERSION=%d" % (int(m.group(1)) + 1), s, flags=re.M)
        # 1 a comment-only edit of ctlcache.sh (a full-line comment and a trailing one) invalidates nothing
        edit("tests/ctlcache.sh", lambda s: s.replace("CC_SRC=", "# only a comment\nCC_SRC=", 1).replace("mkdir -p \"$CC_DIR\" 2>/dev/null || CC_DIR=\"\"", "mkdir -p \"$CC_DIR\" 2>/dev/null || CC_DIR=\"\"   # also a trailing comment", 1))
        commit_check("comment-only edit of ctlcache.sh", False)
        # 2 a key-logic edit without a bump is refused; with the bump it passes
        edit("tests/ctlcache.sh", lambda s: s.replace("-mtime +21", "-mtime +22", 1))
        commit_check("ctlcache.sh logic edit, no bump", True)
        edit("tests/ctlcache.sh", lambda s: bump(s))
        commit_check("ctlcache.sh: only the number raised afterwards", False)
        edit("tests/ctlcache.sh", lambda s: bump(s.replace("-mtime +22", "-mtime +23", 1)))
        commit_check("ctlcache.sh logic edit with the bump", False)
        # 3 the python helper: a docstring/comment edit passes, a code edit is refused
        edit("tests/ctlcache.py", lambda s: s.replace('"""', '"""docstring edit. ', 1))
        commit_check("ctlcache.py docstring edit", False)
        edit("tests/ctlcache.py", lambda s: s.replace("BIG = 64 << 20", "BIG = 32 << 20", 1))
        commit_check("ctlcache.py code edit, no bump", True)
        edit("tests/ctlcache.sh", lambda s: bump(s))
        commit_check("ctlcache.py code edit followed by the bump", False)
        # 4 the recorder
        edit("tests/readlog.c", lambda s: "/* a comment */\n" + s)
        commit_check("readlog.c comment edit", False)
        edit("tests/readlog.c", lambda s: s.replace("#include", "#include <stdint.h>\n#include", 1))
        commit_check("readlog.c code edit, no bump", True)
        edit("tests/ctlcache.sh", lambda s: bump(s))
        commit_check("readlog.c code edit followed by the bump", False)
        # 5 the version going down
        edit("tests/ctlcache.sh", lambda s: re.sub(r"^CC_KEY_VERSION=(\d+)$", "CC_KEY_VERSION=1", s, flags=re.M))
        commit_check("the version lowered", True)
        # CONTROL: a checker that ignores the code must pass the very edit the real one refuses
        edit("tests/ctlcache.sh", lambda s: bump(s))
        g("add", "-A"); g("commit", "-q", "-m", "restore the number")
        edit("tests/ctlcache.sh", lambda s: s.replace("-mtime +23", "-mtime +24", 1))
        g("add", "-A"); g("commit", "-q", "-m", "logic edit for the control")
        real, blind = check(w, base="HEAD~1"), check(w, base="HEAD~1", ignore=True)
        if not real:
            fails.append("control setup: the real check did not refuse the unbumped logic edit")
        if blind:
            fails.append("control: a check that ignores the code still refused (%s)" % blind)
    if fails:
        print("check_cc_key_version selftest FAILED:\n  " + "\n  ".join(fails))
        return 1
    print("check_cc_key_version selftest ok: comment-only edits pass, an unbumped logic edit is refused, a bump or a later bump commit passes, "
          "a lowered number is refused, and a check blind to the code passes the edit the real one refuses")
    return 0


def main(argv):
    if argv[1:2] == ["selftest"]:
        return selftest()
    root, base = os.getcwd(), None
    i = 1
    while i < len(argv):
        if argv[i] == "--root":
            root = argv[i + 1]; i += 2
        elif argv[i] == "--base":
            base = argv[i + 1]; i += 2
        else:
            print(__doc__); return 2
    bad = check(root, base)
    for b in bad:
        print("check_cc_key_version: " + b)
    if bad:
        return 1
    print("check_cc_key_version: ok (the harness code is unchanged, or CC_KEY_VERSION went up with it)")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
