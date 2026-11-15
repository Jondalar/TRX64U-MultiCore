#!/usr/bin/env python3
"""SemVer 2.0 helpers (T221): read a version file, validate, form the build string, compare, propose a bump.

  semver.py get <file.toml>             the `version` of a version file or core.toml (error when it is not SemVer 2.0)
  semver.py check <string>              exit 0 when the string is SemVer 2.0
  semver.py dev <x.y.z> <sha8>          x.y.z-dev+<sha8> (D3: the form of a branch / integration build)
  semver.py cmp <a> <b>                 -1, 0 or 1 (SemVer 2.0 precedence, build metadata ignored)
  semver.py major <string>              the MAJOR number
  semver.py propose <title>             the bump level a merge commit title asks for (D9: a proposal, the human approves)
  semver.py selftest
"""
import re
import sys

SEMVER = re.compile(r"^(0|[1-9]\d*)\.(0|[1-9]\d*)\.(0|[1-9]\d*)"
                    r"(?:-((?:0|[1-9]\d*|\d*[a-zA-Z-][0-9a-zA-Z-]*)(?:\.(?:0|[1-9]\d*|\d*[a-zA-Z-][0-9a-zA-Z-]*))*))?"
                    r"(?:\+([0-9a-zA-Z-]+(?:\.[0-9a-zA-Z-]+)*))?$")


def parse(s):
    m = SEMVER.match(s)
    if not m:
        raise ValueError("not SemVer 2.0: %r" % s)
    return int(m.group(1)), int(m.group(2)), int(m.group(3)), m.group(4), m.group(5)


def read_file(path):
    for line in open(path, encoding="utf-8"):
        m = re.match(r'\s*version\s*=\s*"([^"]*)"\s*(#.*)?$', line)
        if m:
            parse(m.group(1))
            return m.group(1)
    raise ValueError("%s: no version = \"x.y.z\" line" % path)


def dev(base, sha8):
    parse(base)
    if not re.fullmatch(r"[0-9a-f]{8}", sha8):
        raise ValueError("sha8: %r" % sha8)
    return "%s-dev+%s" % (base, sha8)


def _pre_key(pre):
    return [(0, int(p), "") if p.isdigit() else (1, 0, p) for p in pre.split(".")]


def cmp(a, b):
    x, y = parse(a), parse(b)
    if x[:3] != y[:3]:
        return -1 if x[:3] < y[:3] else 1
    if x[3] == y[3]:
        return 0
    if x[3] is None or y[3] is None:
        return 1 if x[3] is None else -1          # a release is above its pre-release
    kx, ky = _pre_key(x[3]), _pre_key(y[3])
    return -1 if kx < ky else (1 if kx > ky else 0)


def propose(title):
    t = title.strip().lower()
    if re.match(r"^[a-z]+(\([^)]*\))?!:", t) or "breaking" in t:
        return "major"
    if re.match(r"^(fix|docs|ci|chore|test|refactor)\b", t):
        return "patch"
    return "minor"


def selftest():
    for ok in ("0.2.0", "1.4.0-dev+1a2b3c4d", "10.0.1", "1.0.0-rc.1"):
        parse(ok)
    for bad in ("1.4", "01.2.3", "1.2.3-", "v1.2.3", "main@1a2b3c4d", "8f1dc184", "1.2.3+"):
        try:
            parse(bad)
            raise SystemExit("selftest: %r accepted" % bad)
        except ValueError:
            pass
    assert cmp("1.4.0-dev+aaaaaaaa", "1.4.0") == -1 and cmp("1.4.0", "1.4.0-dev+aaaaaaaa") == 1
    assert cmp("0.9.0", "1.0.0") == -1 and cmp("1.2.3+a", "1.2.3+b") == 0 and cmp("1.0.0-alpha.2", "1.0.0-alpha.10") == -1
    assert dev("1.4.0", "1a2b3c4d") == "1.4.0-dev+1a2b3c4d"
    assert propose("fix: x") == "patch" and propose("feat: x") == "minor" and propose("feat!: x") == "major" and propose("T99 core") == "minor"
    print("semver selftest ok")


def main(a):
    try:
        if a[:1] == ["get"] and len(a) == 2:
            print(read_file(a[1]))
        elif a[:1] == ["check"] and len(a) == 2:
            parse(a[1])
        elif a[:1] == ["dev"] and len(a) == 3:
            print(dev(a[1], a[2]))
        elif a[:1] == ["cmp"] and len(a) == 3:
            print(cmp(a[1], a[2]))
        elif a[:1] == ["major"] and len(a) == 2:
            print(parse(a[1])[0])
        elif a[:1] == ["propose"] and len(a) == 2:
            print(propose(a[1]))
        elif a == ["selftest"]:
            selftest()
        else:
            print(__doc__, file=sys.stderr)
            return 2
    except ValueError as e:
        print("semver: %s" % e, file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
