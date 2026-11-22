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
        nlocks = len(sdk_api.read_lock(t))
        check(sdk_api.version(t) == sdk_api.version(REAL), "the version is the real one")

        # a comment and a blank line in a public header are no API change
        edit(t, "coreapp/src/libapi.h", lambda s: s.replace("struct LibPollDecl {", "// a new sentence about the poll\n\n\nstruct LibPollDecl {", 1))
        check(sdk_api.problems(t) == [], "a comment and blank lines change nothing: %s" % sdk_api.problems(t))

        locks0 = sdk_api.read_lock(t)
        check(all(x[2] for x in locks0[:-1]), "the real lock: every version before the newest is released: %s" % locks0)
        if locks0[-1][2]:
            # the newest is released (the release step): the steps below need an unreleased newest entry, so the copy gets one
            edit(t, sdk_api.LOCK, lambda s: s[:s.rindex("released = true")] + "released = false" + s[s.rindex("released = true") + len("released = true"):])
            locks0 = sdk_api.read_lock(t)

        # an unreleased version: a member added to a public header is no error, and update brings the hash along
        add_member = lambda s: re.sub(r"(    bool when_frozen;[^\n]*\n)(\};)", r"\1    uint8_t spare;\n\2", s, count=1)
        edit(t, "coreapp/src/libapi.h", add_member)
        check(sdk_api.problems(t) == [], "a header change under an unreleased version is green: %s" % sdk_api.problems(t))
        stale = sdk_api.read_lock(t)[-1][1]
        check(stale != sdk_api.api_hash(t), "the lock of the unreleased version is stale now")
        check(sdk_api.update(t) == ver and sdk_api.read_lock(t)[-1][1] == sdk_api.api_hash(t), "update writes the new hash into the newest entry")
        check(len(sdk_api.read_lock(t)) == nlocks and sdk_api.read_lock(t)[:-1] == locks0[:-1], "update leaves the other entries alone")

        # released: the hash is fixed, a header change is red naming the version and the way out
        edit(t, sdk_api.LOCK, lambda s: s[:s.rindex("released = false")] + "released = true" + s[s.rindex("released = false") + len("released = false"):])
        check(sdk_api.problems(t) == [], "the released version with its current hash is green: %s" % sdk_api.problems(t))
        try:
            sdk_api.update(t)
            check(False, "update refuses a released version")
        except ValueError as e:
            check("is released" in str(e), "update refuses a released version, saying why: %s" % e)
        edit(t, "coreapp/src/libapi.h", lambda s: s.replace("    uint8_t spare;\n", "    uint8_t spare;\n    uint8_t spare2;\n", 1))
        p = sdk_api.problems(t)
        check(len(p) == 1 and "changed under base API " + ver in p[0] and "U2A_BASE_MINOR" in p[0], "a header change under a released version is red: %s" % p)
        edit(t, "coreapp/src/libapi.h", lambda s: s.replace("    uint8_t spare2;\n", "", 1))
        check(sdk_api.problems(t) == [], "the header put back is green again: %s" % sdk_api.problems(t))

        # the minor raised and no lock for it: red, naming the lock to add
        edit(t, sdk_api.LIBFMT, lambda s: re.sub(r"(#define U2A_BASE_MINOR )(\d+)", lambda m: m.group(1) + str(int(m.group(2)) + 1), s, 1))
        edit(t, sdk_api.VERSION_TOML, lambda s: re.sub(r'base_api = "3\.\d+"', 'base_api = "3.%d"' % (int(ver.split(".")[1]) + 1), s))
        p = sdk_api.problems(t)
        check(len(p) == 1 and "add the lock of this version" in p[0], "a new version with no lock is red: %s" % p)

        # the lock of the new version appended (unreleased) after a released one: green
        with open(os.path.join(t, sdk_api.LOCK), "a", encoding="utf-8") as f:
            f.write(sdk_api.lock_entry(t))
        check(sdk_api.problems(t) == [], "the new version with its lock passes: %s" % sdk_api.problems(t))
        check(sdk_api.read_lock(t)[-2][0] == ver and len(sdk_api.read_lock(t)) == nlocks + 1, "the old lock stays")
        check(sdk_api.read_lock(t)[-1][2] is False, "a new entry is unreleased")

        # a lock that is not newest, or the same version twice: red
        with open(os.path.join(t, sdk_api.LOCK), "a", encoding="utf-8") as f:
            f.write(sdk_api.lock_entry(t))
        p = sdk_api.problems(t)
        check(any("ascending" in x for x in p), "a version locked twice is red: %s" % p)
        check(any("only the newest version can be unreleased" in x for x in p), "an unreleased version with a newer one behind it is red: %s" % p)

        # a lock entry without released = ...: an error that names it
        t4 = make_tree()
        try:
            edit(t4, sdk_api.LOCK, lambda s: re.sub(r"released = (true|false)\n", "", s, count=1))
            p = sdk_api.problems(t4)
            check(len(p) == 1 and "released = true or false" in p[0], "a lock entry with no released line is red: %s" % p)
        finally:
            shutil.rmtree(t4)

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
