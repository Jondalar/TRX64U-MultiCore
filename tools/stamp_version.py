#!/usr/bin/env python3
"""Stamp the version string of a build into an application image (T172).

The host app and core app are cached by the content of their trees (ci/trxapps.sh), so the version string compiled in by `make gitinfo` is the one of
the build that filled the cache. host-app/target/common/rules.mk therefore emits the string as a fixed field `TRXVSTAMP:<string>` (64 bytes, 54 after
the marker) and this tool overwrites it in the copy that goes into a package, with the version of the run (ci/version.sh). The image keeps its length;
every instance of the field is patched (one per translation unit that includes gitinfo.h and uses it); none found is an error.

  tools/stamp_version.py stamp <file>... --version <string> [--marker TRXVSTAMP|TRXHSTAMP|TRXBSTAMP]
  tools/stamp_version.py read <file> [--marker ...]

T221: three fields of the same size, each stamped with its own string: TRXVSTAMP the version of the application itself (host menu or core app),
TRXHSTAMP the host gateware version the host app shows in its "host" chip, TRXBSTAMP the bundle version (the update file the host app belongs to).
  tools/stamp_version.py selftest
"""
import sys

MARK = b"TRXVSTAMP:"
MARKERS = ("TRXVSTAMP", "TRXHSTAMP", "TRXBSTAMP")
FIELD = 54


def find(data, mark=MARK):
    i, out = 0, []
    while True:
        i = data.find(mark, i)
        if i < 0:
            return out
        out.append(i + len(mark))
        i += len(mark)


def stamp(data, version, mark=MARK):
    v = version.encode("ascii")
    if len(v) >= FIELD:
        raise ValueError("version %r does not fit %d bytes (NUL terminated)" % (version, FIELD))
    at = find(data, mark)
    if not at:
        raise ValueError("no %s field in the image" % mark.decode())
    b = bytearray(data)
    for o in at:
        b[o:o + FIELD] = v + b"\0" * (FIELD - len(v))
    return bytes(b), len(at)


def read(data, mark=MARK):
    return [data[o:o + FIELD].split(b"\0")[0].decode("ascii") for o in find(data, mark)]


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
    hs = b"TRXHSTAMP:"
    two = MARK + b"a".ljust(FIELD, b"\0") + hs + b"b".ljust(FIELD, b"\0")
    new, n = stamp(two, "0.2.0", hs)
    assert n == 1 and read(new) == ["a"] and read(new, hs) == ["0.2.0"], "one marker must leave the other alone"
    print("stamp_version selftest ok")


def main(a):
    mark = MARK
    if "--marker" in a:
        k = a.index("--marker")
        if a[k + 1] not in MARKERS:
            sys.exit("stamp_version: marker %r, one of %s" % (a[k + 1], ", ".join(MARKERS)))
        mark = a[k + 1].encode() + b":"
        a = a[:k] + a[k + 2:]
    if a[:1] == ["selftest"]:
        return selftest()
    if a[:1] == ["read"] and len(a) == 2:
        print("\n".join(read(open(a[1], "rb").read(), mark)))
        return
    if a[:1] == ["stamp"] and "--version" in a:
        k = a.index("--version")
        ver, files = a[k + 1], a[1:k] + a[k + 2:]
        for f in files:
            new, n = stamp(open(f, "rb").read(), ver, mark)
            open(f, "wb").write(new)
            print("%s: version %s stamped into %d field(s)" % (f, ver, n))
        return
    sys.exit(__doc__)


if __name__ == "__main__":
    try:
        main(sys.argv[1:])
    except ValueError as e:
        sys.exit("stamp_version: %s" % e)
