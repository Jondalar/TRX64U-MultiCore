#!/usr/bin/env python3
"""T211 S1: the host app names its keys itself. In host-app/software/hostapp/*.cc|*.h (code only) KEY_HELP and KEY_TASKS never appear, and
KEY_PAGEUP / KEY_PAGEDOWN appear only in the remote key-name table (hostdomain.cc) - never a handler that treats them as F1/F3/F5/F7.

    tools/check_host_keys.py            check the tree
    tools/check_host_keys.py --selftest plant the faults in a scratch copy: every one must be found
"""
import os
import re
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DIR = "host-app/software/hostapp"
BAD = re.compile(r"\bKEY_(HELP|TASKS)\b")
PAGE = re.compile(r"\bKEY_(PAGEUP|PAGEDOWN)\b")
PAGE_OK = {"hostdomain.cc"}


def code(text):
    text = re.sub(r"/\*.*?\*/", lambda m: re.sub(r"[^\n]", " ", m.group(0)), text, flags=re.S)
    return re.sub(r"//[^\n]*", "", text)


def scan(root):
    out = []
    d = os.path.join(root, DIR)
    for f in sorted(os.listdir(d)):
        if not f.endswith((".cc", ".h")):
            continue
        for n, line in enumerate(code(open(os.path.join(d, f), errors="replace").read()).split("\n"), 1):
            if BAD.search(line) or (PAGE.search(line) and f not in PAGE_OK):
                out.append("%s/%s:%d: %s" % (DIR, f, n, line.strip()))
    return out


def selftest():
    bad = 0
    for name, plant in (("fold", "int m(int k){ switch(k){ case KEY_PAGEUP: return KEY_F1; } return k; }\n"),
                        ("help", "int m(int k){ return k == KEY_HELP; }\n"),
                        ("tasks", "int m(int k){ return k == KEY_TASKS; }\n")):
        with tempfile.TemporaryDirectory() as t:
            os.makedirs(os.path.join(t, DIR))
            open(os.path.join(t, DIR, "x.cc"), "w").write(plant)
            if not scan(t):
                print("selftest: fault '%s' not found" % name)
                bad += 1
    with tempfile.TemporaryDirectory() as t:
        os.makedirs(os.path.join(t, DIR))
        open(os.path.join(t, DIR, "x.cc"), "w").write("// KEY_HELP in a comment\nint a;\n")
        if scan(t):
            print("selftest: a comment was counted")
            bad += 1
    return bad


if __name__ == "__main__":
    if "--selftest" in sys.argv:
        sys.exit(1 if selftest() else 0)
    r = scan(ROOT)
    for l in r:
        print(l)
    sys.exit(1 if r else 0)
