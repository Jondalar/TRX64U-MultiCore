#!/usr/bin/env python3
"""Checks on a built release bundle (T94 P5), used by tools/release_mc.sh before anything is published.

    tools/release_check.py zip <bundle dir> <zip>

`zip` is the one that matters for the archive Alex asked for: the zip has to hold **every** file of the
bundle, each with the size it has on disk. A release whose archive is short of one core is worse than one
with no archive at all, because nobody looks inside it before copying it to a stick.
"""
import os
import sys
import zipfile


def check_zip(d, zp):
    """Returns a list of complaints; empty means the zip holds the whole bundle."""
    want = {f: os.path.getsize(os.path.join(d, f)) for f in os.listdir(d) if not f.startswith(".")}
    bad = []
    with zipfile.ZipFile(zp) as z:
        have = {}
        for i in z.infolist():
            if not i.is_dir():
                have[os.path.basename(i.filename)] = i.file_size
        for f in sorted(want):
            if f not in have:
                bad.append("the zip is missing %s" % f)
            elif have[f] != want[f]:
                bad.append("%s is %d bytes in the zip and %d in the bundle" % (f, have[f], want[f]))
        for f in sorted(have):
            if f not in want:
                bad.append("the zip holds %s, which is not part of the bundle" % f)
    return bad


def main(argv):
    if len(argv) != 4 or argv[1] != "zip":
        print(__doc__)
        return 2
    bad = check_zip(argv[2], argv[3])
    for b in bad:
        print("FAIL %s" % b)
    if bad:
        return 1
    print("    zip holds all %d files of the bundle" % len([f for f in os.listdir(argv[2]) if not f.startswith(".")]))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
