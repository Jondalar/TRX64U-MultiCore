#!/usr/bin/env python3
"""Fixtures of tests/coreapp/test_unzip.cc (T180 D9): everything is made here, nothing third-party.

  unzip.u2a, unzip2.u2a   the loader's fixture ELF (tests/loader/fixtures) with its entry renamed to app_main, packed by tools/u2a_pack.py with the
                          real apps/unzip/app.toml as the menu record, versions 1 and 2 (the image is not run: the test runs the Mac build of the sources)
  t.zip, manifest.txt, files/<name>   a zip of synthetic files (deflated and stored, a folder, a nested folder, names of 100, 200 and 255 characters)
                          and the files it must give back

  gen_unzip.py <outdir> <repo root>
"""
import os
import subprocess
import sys
import zipfile

out, root = sys.argv[1], sys.argv[2]
os.makedirs(os.path.join(out, "files"), exist_ok=True)

elf = open(os.path.join(root, "tests/loader/fixtures/testlib.at0.elf"), "rb").read()
assert elf.count(b"lib_register\0") == 1
open(os.path.join(out, "testlib_fix.elf"), "wb").write(elf)
open(os.path.join(out, "fix.elf"), "wb").write(elf.replace(b"lib_register\0", b"app_main\0\0\0\0\0"))
needs = ["--needs", "base>=3.1", "--needs", "miniz>=1.0"]      # as apps/unzip/Makefile (T197)
for name, ver in (("unzip.u2a", "1"), ("unzip2.u2a", "2")):
    subprocess.check_call([sys.executable, os.path.join(root, "tools/u2a_pack.py"), os.path.join(out, "fix.elf"), "-o", os.path.join(out, name),
                           "--kind", "app", "--residence", "transient", "--name", "unzip", "--version", ver] + needs +
                          ["--menu", os.path.join(root, "apps/unzip/app.toml")], stdout=subprocess.DEVNULL)
# a second application that uses the library (S2) and the library itself: the fixture image under the real names, the code is the Mac build of the sources
subprocess.check_call([sys.executable, os.path.join(root, "tools/u2a_pack.py"), os.path.join(out, "fix.elf"), "-o", os.path.join(out, "ziplist.u2a"),
                       "--kind", "app", "--residence", "transient", "--name", "ziplist", "--version", "1"] + needs, stdout=subprocess.DEVNULL)
subprocess.check_call([sys.executable, os.path.join(root, "tools/u2a_pack.py"), os.path.join(out, "testlib_fix.elf"), "-o", os.path.join(out, "miniz.u2a"),
                       "--kind", "lib", "--name", "miniz", "--version", "1", "--needs", "base>=3.1"], stdout=subprocess.DEVNULL)
# D8 (eviction): a second library nobody here needs and an application that needs no library but the base
subprocess.check_call([sys.executable, os.path.join(root, "tools/u2a_pack.py"), os.path.join(out, "testlib_fix.elf"), "-o", os.path.join(out, "otherlib.u2a"),
                       "--kind", "lib", "--name", "otherlib", "--version", "1", "--needs", "base>=3.1"], stdout=subprocess.DEVNULL)
subprocess.check_call([sys.executable, os.path.join(root, "tools/u2a_pack.py"), os.path.join(out, "fix.elf"), "-o", os.path.join(out, "plain.u2a"),
                       "--kind", "app", "--residence", "transient", "--name", "plain", "--version", "1", "--needs", "base>=3.1"], stdout=subprocess.DEVNULL)

# T239.3 D20: a library that exports the name "ring" (its file lists it), and an application that needs "ring>=1": the library is found by the export name, not its own
subprocess.check_call([sys.executable, os.path.join(root, "tools/u2a_pack.py"), os.path.join(out, "testlib_fix.elf"), "-o", os.path.join(out, "ringlib.u2a"),
                       "--kind", "lib", "--name", "ringlib", "--version", "1", "--export", "ring=1"], stdout=subprocess.DEVNULL)
subprocess.check_call([sys.executable, os.path.join(root, "tools/u2a_pack.py"), os.path.join(out, "fix.elf"), "-o", os.path.join(out, "ringapp.u2a"),
                       "--kind", "app", "--residence", "transient", "--name", "ringapp", "--version", "1", "--needs", "base>=3.2", "--needs", "ring>=1"], stdout=subprocess.DEVNULL)

# a library that declares a poll in its menu record and one that declares a file action: neither is evicted when memory is short
open(os.path.join(out, "pollib.toml"), "w").write('[[node]]\nanchor = "F7/settings"\nkind = "op"\nlabel = "P"\nop = "p.go"\n[poll]\nop = "p.go"\nperiod_ms = 10\n')
open(os.path.join(out, "menulib.toml"), "w").write('[[file_action]]\next = [".zip"]\nlabel = "Menu lib action"\napp = "unzip"\naction = "here"\n')
for lib in ("pollib", "menulib"):
    subprocess.check_call([sys.executable, os.path.join(root, "tools/u2a_pack.py"), os.path.join(out, "testlib_fix.elf"), "-o", os.path.join(out, lib + ".u2a"),
                           "--kind", "lib", "--name", lib, "--version", "1", "--needs", "base>=3.1", "--menu", os.path.join(out, lib + ".toml")], stdout=subprocess.DEVNULL)

# the export "corehooks" (core_hooks.h): the base keeps a pointer into the table for the whole run, so a library that only exports it (no poll, no menu) is never evicted
subprocess.check_call([sys.executable, os.path.join(root, "tools/u2a_pack.py"), os.path.join(out, "testlib_fix.elf"), "-o", os.path.join(out, "hooklib.u2a"),
                       "--kind", "lib", "--name", "hooklib", "--version", "1", "--export", "corehooks=1"], stdout=subprocess.DEVNULL)

# D8 (holds): a library that imports miniz, and a resident application that needs miniz
subprocess.check_call([sys.executable, os.path.join(root, "tools/u2a_pack.py"), os.path.join(out, "testlib_fix.elf"), "-o", os.path.join(out, "outerlib.u2a"),
                       "--kind", "lib", "--name", "outerlib", "--version", "1", "--needs", "base>=3.1", "--needs", "miniz>=1.0"], stdout=subprocess.DEVNULL)
subprocess.check_call([sys.executable, os.path.join(root, "tools/u2a_pack.py"), os.path.join(out, "fix.elf"), "-o", os.path.join(out, "resapp.u2a"),
                       "--kind", "app", "--residence", "resident", "--name", "resapp", "--version", "1"] + needs, stdout=subprocess.DEVNULL)

def blob(seed, n):
    x = seed
    b = bytearray()
    for _ in range(n):
        x = (x * 1103515245 + 12345) & 0x7FFFFFFF
        b.append((x >> 16) & 0xFF)
    return bytes(b)


files = [
    ("readme.txt", b"synthetic text for the unzip bench\n" * 40, zipfile.ZIP_DEFLATED),
    ("sub/data.bin", blob(1, 3000), zipfile.ZIP_STORED),
    ("sub/deep/more.bin", blob(2, 70000), zipfile.ZIP_DEFLATED),
    ("n100_" + "a" * 91 + ".txt", b"a name of 100 characters", zipfile.ZIP_DEFLATED),
    ("n200_" + "b" * 191 + ".txt", b"a name of 200 characters", zipfile.ZIP_STORED),
    ("n255_" + "c" * 246 + ".txt", b"a name of 255 characters", zipfile.ZIP_STORED),
]
with zipfile.ZipFile(os.path.join(out, "t.zip"), "w") as z:
    z.writestr(zipfile.ZipInfo("sub/"), b"")
    for name, data, how in files:
        z.writestr(zipfile.ZipInfo(name), data, compress_type=how)
        p = os.path.join(out, "files", name)
        os.makedirs(os.path.dirname(p), exist_ok=True)
        open(p, "wb").write(data)
open(os.path.join(out, "manifest.txt"), "w").write("\n".join(n for n, _, _ in files) + "\n")
for n, _, _ in files:
    assert len(n) in (len("readme.txt"), len("sub/data.bin"), len("sub/deep/more.bin"), 100, 200, 255), (n, len(n))
