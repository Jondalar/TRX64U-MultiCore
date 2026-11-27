#!/usr/bin/env python3
"""T192.3 (Alex, 2026-11-09): the UI has ONE state and ONE way to read it - the UI service's snapshot (uicore, GET /v1/ui/state, contract 2). The v1 state of
T153 (uistate.cc, `?v=1`, the `seq` counter) is gone; this gate is red if it or a second reader of the screen comes back.

What it refuses (comments do not count; strings do, because a route or a JSON key is a string):
  - a file named uistate.* anywhere in the tree
  - the names uistate_*, rest_ui_state, UiState, in any source, script or Makefile
  - the route /v1/ui/state anywhere in C/C++ code outside uicore/uic_net.cc, tests/ and cores/<id>/app/tests/ (the service owns the route)
  - `v=1` on the state route, in any source, script or test
  - a v1 body: the JSON key "v":1 in coreapp/ or app/ code, or a test that reads `['seq']` / `.get('seq')` from the state
  - ui_shell_info_now (the screen's own top bar) outside ui.cc, ui.h and uiapp_board.cc: the snapshot gets the bar from the platform hook, nobody else reads it

    tools/check_no_v1_ui.py             check the tree
    tools/check_no_v1_ui.py --list      the same, one line per finding, exit 0
    tools/check_no_v1_ui.py --selftest  plant each fault in a scratch copy: every one must be found
"""
import os
import re
import shutil
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC_EXT = (".cc", ".h", ".cpp", ".c", ".py", ".sh", ".mk", ".toml", ".feature")
SKIP = {"tools/check_no_v1_ui.py"}
NAMES = re.compile(r"\b(?:uistate_\w+|rest_ui_state|UiState)\b")
ROUTE = re.compile(r"/v1/ui/state")
ROUTE_V1 = re.compile(r"/v1/ui/state\?[^\"'\s]*\bv=1")
V1_BODY = re.compile(r"\\\"v\\\"\s*:\s*1\b")
SEQ = re.compile(r"""\[['"]seq['"]\]|\.get\(['"]seq['"]""")
SHELL = re.compile(r"\bui_shell_info_now\b")
SHELL_OK = {"coreapp/src/ui.cc", "coreapp/src/ui.h", "coreapp/src/uiapp_board.cc"}


def files(root):
    out = subprocess.run(["git", "ls-files", "-co", "--exclude-standard"], cwd=root, capture_output=True, text=True).stdout.split("\n")
    return [f for f in out if f and os.path.isfile(os.path.join(root, f))]


def strip_comments(text, py):
    if py:
        return re.sub(r"(?m)^\s*#.*$", "", text)
    text = re.sub(r"/\*.*?\*/", lambda m: re.sub(r"[^\n]", " ", m.group(0)), text, flags=re.S)
    return re.sub(r"(?m)(?<![:\"'])//.*$", "", text)


def check(root):
    found = []
    for f in files(root):
        base = os.path.basename(f)
        if f.startswith("docs/") or f in SKIP:
            continue
        if base.startswith("uistate."):
            found.append("%s: a v1 state file" % f)
            continue
        if not (f.endswith(SRC_EXT) or base == "Makefile"):
            continue
        try:
            text = open(os.path.join(root, f), errors="replace").read()
        except OSError:
            continue
        code = strip_comments(text, f.endswith((".py", ".sh", ".toml", ".feature", ".mk")))
        cpp = f.endswith((".cc", ".h", ".cpp", ".c"))
        for n, line in enumerate(code.split("\n"), 1):
            def hit(why):
                found.append("%s:%d: %s" % (f, n, why))
            if NAMES.search(line):
                hit("the v1 state reader (%s)" % NAMES.search(line).group(0))
            if ROUTE_V1.search(line):
                hit("the state route with v=1")
            is_test = f.startswith("tests/") or re.match(r"cores/[^/]+/app/tests/", f)          # a core's own tests count as tests (T239 D17)
            if cpp and ROUTE.search(line) and f != "uicore/uic_net.cc" and not is_test:
                hit("the state route outside the service")
            if V1_BODY.search(line) and (f.startswith(("coreapp/", "app/")) or f.startswith("tests/ui/")):
                hit('a v1 body ("v":1)')
            if f.startswith("tests/") and SEQ.search(line):
                hit("a test reads the v1 counter seq (the revision is `rev`)")
            if cpp and SHELL.search(line) and f not in SHELL_OK:
                hit("a second reader of the screen's top bar")
    return found


FAULTS = [
    ("coreapp/src/uistate.cc", "int x;\n", "a uistate file"),
    ("coreapp/src/zz_a.cc", "int f() { return uistate_request(0, 0); }\n", "uistate_request"),
    ("coreapp/src/zz_b.cc", "int (*rest_ui_state)(char *, int);\n", "rest_ui_state"),
    ("coreapp/src/zz_c.cc", 'const char *r = "/v1/ui/state";\n', "the route outside the service"),
    ("tests/ui/zz_d.py", 'url = "http://x/v1/ui/state?v=1"\n', "v=1"),
    ("coreapp/src/zz_e.cc", 'const char *b = "{\\"v\\":1,\\"seq\\":3}";\n', "a v1 body"),
    ("tests/ui/zz_f.py", "n = s['seq']\n", "seq"),
    ("coreapp/src/zz_g.cc", "void f(MenuCtl *m) { TrxShellInfo i; ui_shell_info_now(&i, m); }\n", "a second top bar reader"),
]


def selftest():
    bad = 0
    for rel, body, what in FAULTS:
        with tempfile.TemporaryDirectory() as d:
            subprocess.run(["git", "init", "-q", d], check=True)
            p = os.path.join(d, rel)
            os.makedirs(os.path.dirname(p), exist_ok=True)
            open(p, "w").write(body)
            if not check(d):
                print("selftest: NOT found: %s" % what)
                bad += 1
    with tempfile.TemporaryDirectory() as d:                     # controls: comments and the service's own route are fine
        subprocess.run(["git", "init", "-q", d], check=True)
        os.makedirs(os.path.join(d, "uicore"))
        open(os.path.join(d, "uicore/uic_net.cc"), "w").write('const char *r = "/v1/ui/state";\n')
        os.makedirs(os.path.join(d, "coreapp/src"))
        open(os.path.join(d, "coreapp/src/ok.cc"), "w").write("// uistate_request is gone, /v1/ui/state?v=1 too\nint x;\n")
        if check(d):
            print("selftest: a false finding: %s" % check(d))
            bad += 1
    print("selftest: %d fault(s) planted, %s" % (len(FAULTS), "all found" if not bad else "%d problem(s)" % bad))
    return 1 if bad else 0


if __name__ == "__main__":
    if "--selftest" in sys.argv:
        sys.exit(selftest())
    res = check(ROOT)
    for r in res:
        print(r)
    print("check_no_v1_ui: %d finding(s)" % len(res))
    sys.exit(0 if (not res or "--list" in sys.argv) else 1)
