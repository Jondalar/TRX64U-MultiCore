#!/usr/bin/env python3
"""T239.3 S5: the lock of the base API (tools/sdk_api.py) - unit and happy path. A copy of the files the check reads is made in a temporary
directory; a comment fix, a header change, a version change and a wrong base_api are each tried on it."""
import os
import re
import shutil
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import sdk_api  # noqa: E402

REAL = sdk_api.ROOT
FAILS = []


def check(cond, what):
    if not cond:
        FAILS.append(what)
        print("FAIL: " + what)


def make_tree():
    t = tempfile.mkdtemp(prefix="sdkapi.")
    for rel in sdk_api.headers(REAL) + [sdk_api.LOCK, sdk_api.VERSION_TOML, sdk_api.LIBFMT]:
        dst = os.path.join(t, rel)
        os.makedirs(os.path.dirname(dst), exist_ok=True)
        shutil.copy(os.path.join(REAL, rel), dst)
    return t


def edit(t, rel, fn):
    p = os.path.join(t, rel)
    s = open(p, encoding="utf-8").read()
    n = fn(s)
    assert n != s, "the edit changed nothing in " + rel
    open(p, "w", encoding="utf-8").write(n)


def main():
    t = make_tree()
    try:
        check(sdk_api.problems(t) == [], "the tree as it is passes: %s" % sdk_api.problems(t))
        ver = sdk_api.version(t)
        check(sdk_api.version(t) == sdk_api.version(REAL), "the version is the real one")

        # a comment and a blank line in a public header are no API change
        edit(t, "coreapp/src/libapi.h", lambda s: s.replace("struct LibPollDecl {", "// a new sentence about the poll\n\n\nstruct LibPollDecl {", 1))
        check(sdk_api.problems(t) == [], "a comment and blank lines change nothing: %s" % sdk_api.problems(t))

        # a member added under the same version is red, naming the version and the way out
        edit(t, "coreapp/src/libapi.h", lambda s: s.replace("    bool when_frozen;\n};", "    bool when_frozen;\n    uint8_t spare;\n};", 1))
        p = sdk_api.problems(t)
        check(len(p) == 1 and "changed under base API " + ver in p[0] and "U2A_BASE_MINOR" in p[0], "a header change under the same version is red: %s" % p)

        # the minor raised and no lock for it: red, naming the lock to add
        edit(t, sdk_api.LIBFMT, lambda s: re.sub(r"(#define U2A_BASE_MINOR )(\d+)", lambda m: m.group(1) + str(int(m.group(2)) + 1), s, 1))
        edit(t, sdk_api.VERSION_TOML, lambda s: re.sub(r'base_api = "3\.\d+"', 'base_api = "3.%d"' % (int(ver.split(".")[1]) + 1), s))
        p = sdk_api.problems(t)
        check(len(p) == 1 and "add the lock of this version" in p[0], "a new version with no lock is red: %s" % p)

        # the lock of the new version appended: green
        with open(os.path.join(t, sdk_api.LOCK), "a", encoding="utf-8") as f:
            f.write(sdk_api.lock_entry(t))
        check(sdk_api.problems(t) == [], "the new version with its lock passes: %s" % sdk_api.problems(t))
        check(sdk_api.read_lock(t)[0][0] == ver and len(sdk_api.read_lock(t)) == 2, "the old lock stays")

        # a lock that is not newest, or the same version twice: red
        with open(os.path.join(t, sdk_api.LOCK), "a", encoding="utf-8") as f:
            f.write(sdk_api.lock_entry(t))
        p = sdk_api.problems(t)
        check(any("ascending" in x for x in p), "a version locked twice is red: %s" % p)

        # base_api of the version file differs from libfmt.h: red
        t2 = make_tree()
        try:
            edit(t2, sdk_api.VERSION_TOML, lambda s: s.replace('base_api = "%s"' % ver, 'base_api = "9.9"'))
            p = sdk_api.problems(t2)
            check(len(p) == 1 and "base_api is 9.9" in p[0], "a base_api that is not the libfmt.h one is red: %s" % p)
            edit(t2, sdk_api.VERSION_TOML, lambda s: re.sub(r"^base_api.*$", "", s, flags=re.M))
            p = sdk_api.problems(t2)
            check(len(p) == 1 and "no base_api" in p[0], "no base_api line is red: %s" % p)
        finally:
            shutil.rmtree(t2)

        # the list of public headers: a header the closure cannot find is an error
        t3 = make_tree()
        try:
            edit(t3, "coreapp/src/svctab.h", lambda s: s.replace("#include", '#include "nowhere_at_all.h"\n#include', 1) if "#include" in s else s + '\n#include "nowhere_at_all.h"\n')
            check(any("nowhere_at_all.h" in x for x in sdk_api.problems(t3)), "a missing public include is red")
        finally:
            shutil.rmtree(t3)
    finally:
        shutil.rmtree(t)
    sdk_api.selftest()
    if FAILS:
        print("test_sdk_api: %d failed" % len(FAILS))
        return 1
    print("test_sdk_api: ok")
    return 0


if __name__ == "__main__":
    sys.exit(main())
