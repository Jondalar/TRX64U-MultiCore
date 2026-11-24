#!/usr/bin/env python3
"""T239.3 S5: the public headers of the SDK, the base API version, and the lock that ties the two.

The base API version has one source, coreapp/src/libfmt.h (U2A_ABI_MAJOR, U2A_BASE_MINOR). The public headers are what the SDK package ships
in include/: the include closure (quote includes, found in coreapp/src or app/sdk) of ROOTS. coreapp/api_lock.toml holds, for every version, the
hash of that closure with comments and white space taken out, so a comment fix is no API change. A header change under the same version, a
version with no lock, a lock for a version the headers do not carry, and a lock list that is not in ascending order are errors.

One base minor per SDK release, not one per change: an entry of the lock says released = true or released = false. The hash of a released version
is fixed (a change of the headers under it is red). The hash of an unreleased version may change, so what is added before the release stays under
its minor; only the newest entry can be unreleased, and `update` rewrites its hash. To release a version, run `update` once and set released = true.

  sdk_api.py headers      the shipped headers, one repository path a line (tools/sdk_pack.sh copies exactly these)
  sdk_api.py version      major.minor of the base API (libfmt.h)
  sdk_api.py hash         the hash of the public headers
  sdk_api.py hash-dir <d> the hash of the closure headers found in a folder (other files ignored), as the package's include/ (tools/sdk_check.sh)
  sdk_api.py lock         the lock entry for the current headers (released = false), to append to coreapp/api_lock.toml after a version change
  sdk_api.py update       rewrite the hash of the newest entry in coreapp/api_lock.toml to the current headers; refused for a released one
  sdk_api.py check        the lint: version, lock and headers agree; coreapp/version.toml shows the same base_api
  sdk_api.py selftest
"""
import hashlib
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
# the headers of the call tables of base 3.3 that libapi.h does not include (svc_sys.h: the members of SvcSys; the rest: what the libraries of this base export, T239.3 D17)
ROOTS = ["libapi.h", "libfmt.h", "c64_api.h", "glue.h", "u2p_app.h", "u2p_ui.h", "hidden.h", "svc_sys.h", "textgrid.h", "c64cart_api.h", "cbm1541_api.h", "sid_api.h",
         "tempdisk_api.h", "core_hooks.h", "cbmprg_api.h", "mouse_api.h", "pad_api.h", "padkeys_api.h"]
DIRS = ["coreapp/src", "app/sdk"]
LOCK = "coreapp/api_lock.toml"
VERSION_TOML = "coreapp/version.toml"
LIBFMT = "coreapp/src/libfmt.h"

_TOKEN = re.compile(r'"(?:\\.|[^"\\\n])*"|\'(?:\\.|[^\'\\\n])*\'|//[^\n]*|/\*.*?\*/', re.S)
_INCLUDE = re.compile(r'^[ \t]*#[ \t]*include[ \t]+"([^"]+)"', re.M)


def strip(text):
    """Comments out, white space to one blank: what the hash sees. String and character literals are kept whole."""
    def sub(m):
        t = m.group(0)
        return " " if t[0] == "/" else t
    return " ".join(_TOKEN.sub(sub, text).split())


def _find(name, root):
    for d in DIRS:
        p = os.path.join(root, d, name)
        if os.path.isfile(p):
            return os.path.join(d, name)
    return None


def headers(root=ROOT):
    """The repository paths of the public headers, sorted. A quote include that no directory holds is an error: the package would not build."""
    seen, todo = {}, list(ROOTS)
    while todo:
        name = todo.pop()
        if name in seen:
            continue
        rel = _find(name, root)
        if rel is None:
            raise ValueError("the public headers include \"%s\", which is in neither %s" % (name, " nor ".join(DIRS)))
        seen[name] = rel
        with open(os.path.join(root, rel), encoding="utf-8") as f:
            # includes inside a comment are not includes: strip first, the include line itself survives (it has no comment)
            todo.extend(_INCLUDE.findall("\n".join(strip(line) for line in f.read().split("\n"))))
    clash = [n for n in seen if sum(1 for v in seen.values() if os.path.basename(v) == n) > 1]
    if clash:
        raise ValueError("two public headers named %s" % clash[0])
    return sorted(seen.values())


def hash_files(paths):
    """The hash of a set of files by their base names in order, so the tree and the flat include/ of the package give the same."""
    h = hashlib.sha256()
    for path in sorted(paths, key=os.path.basename):
        with open(path, encoding="utf-8") as f:
            body = strip(f.read())
        h.update(("%s\n%s\n" % (os.path.basename(path), body)).encode("utf-8"))
    return h.hexdigest()


def api_hash(root=ROOT):
    return hash_files([os.path.join(root, rel) for rel in headers(root)])


def version(root=ROOT):
    with open(os.path.join(root, LIBFMT), encoding="utf-8") as f:
        src = f.read()
    got = {}
    for key in ("U2A_ABI_MAJOR", "U2A_BASE_MINOR"):
        m = re.search(r"^#define\s+%s\s+(\d+)\b" % key, src, re.M)
        if not m:
            raise ValueError("%s: no #define %s" % (LIBFMT, key))
        got[key] = int(m.group(1))
    return "%d.%d" % (got["U2A_ABI_MAJOR"], got["U2A_BASE_MINOR"])


def read_lock(root=ROOT):
    """[(version, hash, released)] in file order."""
    with open(os.path.join(root, LOCK), encoding="utf-8") as f:
        src = f.read()
    out = []
    for blk in re.split(r"^\[\[lock\]\]\s*$", src, flags=re.M)[1:]:
        v = re.search(r'^version\s*=\s*"(\d+\.\d+)"\s*(#.*)?$', blk, re.M)
        h = re.search(r'^headers\s*=\s*"([0-9a-f]{64})"\s*(#.*)?$', blk, re.M)
        rel = re.search(r'^released\s*=\s*(true|false)\s*(#.*)?$', blk, re.M)
        if not v or not h or not rel:
            raise ValueError("%s: a [[lock]] needs version = \"x.y\", released = true or false, and headers = \"<sha256>\"" % LOCK)
        out.append((v.group(1), h.group(1), rel.group(1) == "true"))
    return out


def _key(v):
    return tuple(int(x) for x in v.split("."))


def lock_entry(root=ROOT):
    return '[[lock]]\nversion = "%s"\nreleased = false\nheaders = "%s"\n' % (version(root), api_hash(root))


def update(root=ROOT):
    """The hash of the newest, unreleased entry follows the headers. Returns the version."""
    locks = read_lock(root)
    if not locks or locks[-1][0] != version(root):
        raise ValueError("%s: the newest lock is not the base API %s of %s: add its lock first (lock)" % (LOCK, version(root), LIBFMT))
    if locks[-1][2]:
        raise ValueError("%s: base API %s is released, its hash never changes: raise U2A_BASE_MINOR for an addition" % (LOCK, locks[-1][0]))
    p = os.path.join(root, LOCK)
    with open(p, encoding="utf-8") as f:
        src = f.read()
    head, _, tail = src.rpartition("[[lock]]")
    tail = re.sub(r'^(headers\s*=\s*")[0-9a-f]{64}(")', lambda m: m.group(1) + api_hash(root) + m.group(2), tail, count=1, flags=re.M)
    with open(p, "w", encoding="utf-8") as f:
        f.write(head + "[[lock]]" + tail)
    return locks[-1][0]


def problems(root=ROOT):
    errs = []
    try:
        ver, now = version(root), api_hash(root)
    except (ValueError, OSError) as e:
        return [str(e)]
    try:
        locks = read_lock(root)
    except (ValueError, OSError) as e:
        return [str(e)]
    vs = [v for v, _, _ in locks]
    if any(_key(a) >= _key(b) for a, b in zip(vs, vs[1:])):
        errs.append("%s: the locks are not in ascending order of version, or a version is there twice: %s" % (LOCK, ", ".join(vs)))
    if not locks or locks[-1][0] != ver:
        errs.append("%s: the newest lock is %s, the base API of %s is %s: add the lock of this version "
                    "(python3 tools/sdk_api.py lock) or put the version back" % (LOCK, vs[-1] if vs else "(none)", LIBFMT, ver))
    else:
        # an unreleased version is not held to its hash (update brings it along); a released one is
        if locks[-1][2] and locks[-1][1] != now:
            errs.append("the public headers of the SDK changed under base API %s (hash %s, locked %s): raise U2A_BASE_MINOR in %s for an "
                        "addition or U2A_ABI_MAJOR for anything else, then add the lock of the new version (python3 tools/sdk_api.py lock). "
                        "A comment or a blank is no change. The version is released: an addition belongs to the next one." % (ver, now[:12], locks[-1][1][:12], LIBFMT))
    for v, _, rel in locks[:-1]:
        if not rel:
            errs.append("%s: base API %s is not released, and a newer version is locked: only the newest version can be unreleased (one minor per SDK release)" % (LOCK, v))
    # what the generic app's version file shows
    try:
        with open(os.path.join(root, VERSION_TOML), encoding="utf-8") as f:
            m = re.search(r'^base_api\s*=\s*"([^"]*)"\s*(#.*)?$', f.read(), re.M)
        if not m:
            errs.append("%s: no base_api = \"%s\" line" % (VERSION_TOML, ver))
        elif m.group(1) != ver:
            errs.append("%s: base_api is %s, the base API of %s is %s" % (VERSION_TOML, m.group(1), LIBFMT, ver))
    except OSError as e:
        errs.append(str(e))
    return errs


def selftest():
    assert strip('int a; // c\n/* x\n y */ int b;  "//kept" \'/\'') == 'int a; int b; "//kept" \'/\''
    assert strip("a\n\n   b") == "a b"
    assert _key("3.10") > _key("3.9") and _key("4.0") > _key("3.99")
    hs = headers()
    assert "coreapp/src/libapi.h" in hs and "coreapp/src/svctab.h" in hs and "coreapp/src/libfmt.h" in hs, hs
    assert re.fullmatch(r"\d+\.\d+", version())
    return 0


def main(argv):
    cmd = argv[1] if len(argv) > 1 else "check"
    try:
        if cmd == "headers":
            print("\n".join(headers()))
        elif cmd == "version":
            print(version())
        elif cmd == "hash":
            print(api_hash())
        elif cmd == "hash-dir":
            d = argv[2]
            # only the closure, read from the directory by base name: a header that rides along (online_api.h) is outside the lock
            print(hash_files([os.path.join(d, os.path.basename(rel)) for rel in headers()]))
        elif cmd == "lock":
            sys.stdout.write(lock_entry())
        elif cmd == "update":
            print("sdk api: the lock of base API %s now holds %s" % (update(), api_hash()[:12]))
        elif cmd == "check":
            errs = problems()
            for e in errs:
                print("FAIL: " + e)
            if not errs:
                rel = read_lock()[-1][2]
                print("sdk api ok: base API %s (%s), %d public headers, hash %s" % (version(), "released" if rel else "not released, its lock follows the headers", len(headers()), api_hash()[:12]))
            return 1 if errs else 0
        elif cmd == "selftest":
            selftest()
            print("sdk_api selftest ok")
        else:
            print(__doc__)
            return 2
    except (ValueError, OSError) as e:
        print("FAIL: %s" % e)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
