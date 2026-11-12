#!/usr/bin/env python3
"""Stamp the version string of a build into an application image (T172).

The host app and core app are cached by the content of their trees (ci/trxapps.sh), so the version string compiled in by `make gitinfo` is the one of
the build that filled the cache. host-app/target/common/rules.mk therefore emits the string as a fixed field `TRXVSTAMP:<string>` (64 bytes, 54 after
the marker) and this tool overwrites it in the copy that goes into a package, with the version of the run (ci/version.sh). The image keeps its length;
every instance of the field is patched (one per translation unit that includes gitinfo.h and uses it); none found is an error.

  tools/stamp_version.py stamp <file>... --version <string>
  tools/stamp_version.py read <file>
  tools/stamp_version.py selftest
"""
import sys

MARK = b"TRXVSTAMP:"
FIELD = 54


def find(data):
    i, out = 0, []
    while True:
        i = data.find(MARK, i)
        if i < 0:
            return out
        out.append(i + len(MARK))
        i += len(MARK)


def stamp(data, version):
    v = version.encode("ascii")
    if len(v) >= FIELD:
        raise ValueError("version %r does not fit %d bytes (NUL terminated)" % (version, FIELD))
    at = find(data)
    if not at:
        raise ValueError("no TRXVSTAMP field in the image")
    b = bytearray(data)
    for o in at:
        b[o:o + FIELD] = v + b"\0" * (FIELD - len(v))
    return bytes(b), len(at)


def read(data):
    return [data[o:o + FIELD].split(b"\0")[0].decode("ascii") for o in find(data)]


def selftest():
    img = b"\x13" * 8 + MARK + b"old@1234abcd".ljust(FIELD, b"\0") + b"\x77" * 4 + MARK + b"old@1234abcd".ljust(FIELD, b"\0") + b"\x55" * 3
    new, n = stamp(img, "0.2.0+deadbeef")
    assert n == 2 and read(new) == ["0.2.0+deadbeef"] * 2 and len(new) == len(img)
    assert new[:8] == img[:8] and new.endswith(b"\x55" * 3), "bytes outside the field changed"
    for bad, what in ((b"no field here", "no field"), (None, "too long")):
        try:
            stamp(bad if bad else img, "x" * FIELD)
            raise SystemExit("selftest: %s accepted" % what)
        except ValueError:
            pass
    print("stamp_version selftest ok")


def main(a):
    if a[:1] == ["selftest"]:
        return selftest()
    if a[:1] == ["read"] and len(a) == 2:
        print("\n".join(read(open(a[1], "rb").read())))
        return
    if a[:1] == ["stamp"] and "--version" in a:
        k = a.index("--version")
        ver, files = a[k + 1], a[1:k] + a[k + 2:]
        for f in files:
            new, n = stamp(open(f, "rb").read(), ver)
            open(f, "wb").write(new)
            print("%s: version %s stamped into %d field(s)" % (f, ver, n))
        return
    sys.exit(__doc__)


if __name__ == "__main__":
    try:
        main(sys.argv[1:])
    except ValueError as e:
        sys.exit("stamp_version: %s" % e)
