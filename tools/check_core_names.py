#!/usr/bin/env python3
"""T239 D17 (Alex 2026-11-27): a core's tests live in its own folder (cores/<id>/app/tests, tools/tests_to_core.py), so the shared test harness names no core.

    tools/check_core_names.py            the check (ci/lint.sh): red for a hit of a core that is not in the ledger, and for a ledger line without a hit
    tools/check_core_names.py --report   every hit, ledger or not

A hit is, in a file tracked under tests/coreapp (outside cores/):
  - the id of a core with an app of its own (cores/<id>/app/sdk.toml) as a word of the text or as a token of the file's name (split at '_', '.', '/');
  - a symbol TEST_APP_<X> (any file tracked under tests/, ci/, tools/, coreapp/ or uicore/ is searched for these).
The ledger tools/core_names_ledger.txt holds the cores whose tests have not moved yet (`<id> TEST_APP_<X>`, one per line), and a moved core that a shared
file still names for a reason the move cannot remove (`<id> <path>`: that file only, with the reason in a comment); it only shrinks.
"""
import os
import re
import subprocess
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
LEDGER = os.path.join(ROOT, "tools", "core_names_ledger.txt")
NAMES_IN = ("tests/coreapp/",)
SYMS_IN = ("tests/", "ci/", "tools/", "coreapp/", "uicore/")
SELF = ("tools/check_core_names.py", "tools/core_names_ledger.txt", "tools/tests_to_core.py")


def tracked():
    out = subprocess.run(["git", "ls-files", "-z"], cwd=ROOT, capture_output=True, text=True, check=True).stdout
    return [f for f in out.split("\0") if f]


def cores(files):
    return sorted({f.split("/")[1] for f in files if re.match(r'^cores/[^/]+/app/sdk\.toml$', f)})


def ledger():
    ids, syms, files = set(), set(), {}
    if not os.path.exists(LEDGER):
        return ids, syms, files
    for ln in open(LEDGER):
        ln = ln.split("#", 1)[0].split()
        if not ln:
            continue
        paths = [x for x in ln[1:] if "/" in x]
        if paths:
            files.setdefault(ln[0], set()).update(paths)
        else:
            ids.add(ln[0])
        syms.update(x for x in ln[1:] if "/" not in x)
    return ids, syms, files


def hits(files, ids):
    word = re.compile(r'(?<![\w-])(%s)(?![\w-])' % "|".join(re.escape(i) for i in sorted(ids, key=len, reverse=True)))
    sym = re.compile(r'\bTEST_APP_\w+')
    out = []   # (core id or symbol, file, line number, text)
    for f in files:
        if f in SELF or not f.startswith(SYMS_IN):
            continue
        names = f.startswith(NAMES_IN)
        if names:
            for tok in re.split(r'[_./]', f[len("tests/coreapp/"):]):
                if tok in ids:
                    out.append((tok, f, 0, "(the file's name)"))
        try:
            text = open(os.path.join(ROOT, f), errors="replace").read()
        except (IsADirectoryError, FileNotFoundError):
            continue
        if "TEST_APP_" not in text and not names:
            continue
        for n, ln in enumerate(text.split("\n"), 1):
            for m in sym.finditer(ln):
                out.append((m.group(0), f, n, ln.strip()))
            if names:
                for m in word.finditer(ln):
                    out.append((m.group(1), f, n, ln.strip()))
    return out


def main(argv):
    files = tracked()
    ids = cores(files)
    lid, lsym, lfiles = ledger()
    hs = hits(files, ids)
    if "--report" in argv:
        for k, f, n, t in hs:
            print("%s: %s:%d: %s" % (k, f, n, t[:160]))
        return 0
    bad = [(k, f, n, t) for k, f, n, t in hs if k not in lid and k not in lsym and f not in lfiles.get(k, ())]
    seen = {k for k, *_ in hs}
    seenf = {(k, f) for k, f, *_ in hs}
    stale = sorted((lid | lsym) - seen) + sorted("%s %s" % (k, f) for k, fs in lfiles.items() for f in fs if (k, f) not in seenf)
    for k, f, n, t in bad[:40]:
        print("core_names: %s:%d names %s, whose tests live in its own folder (T239 D17): %s" % (f, n, k, t[:140]))
    if len(bad) > 40:
        print("core_names: ... %d more" % (len(bad) - 40))
    for k in stale:
        print("core_names: %s is in tools/core_names_ledger.txt but no shared file names it: take it out (the ledger only shrinks)" % k)
    if bad or stale:
        return 1
    print("core_names: ok: %d cores with an app of their own, %d still in the ledger (%s)" % (len(ids), len(lid), " ".join(sorted(lid))))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
