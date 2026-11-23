#!/usr/bin/env python3
"""Host test for tools/libtab.py (T239.3 P3-1): the TOML B of the shared app loads and every file it names exists, its library table has one row per library with the entries
the TOML names, the build image's TOML reader (the subset) reads it as tomllib does, and the two checks (the generic sources name no library, the objects of a build use no
library symbol outside the table) pass a clean tree.

  python3 tools/test_libtab.py        (also collected by python3 -m pytest tools/test_libtab.py)
"""
import os
import shutil
import subprocess
import sys
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import descrec as D  # noqa: E402
import libtab as L  # noqa: E402

ROOT = os.path.abspath(os.path.join(HERE, ".."))
SHARED = os.path.join(ROOT, "coreapp", "libs", "shared.toml")
HAVE_CC = shutil.which("cc") is not None and shutil.which("nm") is not None


def write(path, text):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    open(path, "w").write(text)


def toml_b(body):
    d = tempfile.mkdtemp()
    p = os.path.join(d, "b.toml")
    write(p, '[app]\nname = "t"\n' + body)
    return p


class SharedApp(unittest.TestCase):
    def test_loads_and_files_exist(self):
        libs = L.load(SHARED)
        L.check_files(libs, ROOT)
        names = [x["name"] for x in libs]
        for need in ("c64cart", "cbm1541", "uci", "sid", "tempdisk", "cbmprg", "cbmrun", "dma", "t64", "online",
                     "newfile", "newdisk", "folderhd", "fastseek", "ftp"):
            self.assertIn(need, names)
        run = [x for x in libs if x["name"] == "cbmrun"][0]
        self.assertIn("c64_go.cc", run["sources"])                   # c64_go is part of cbmrun (P1-8)

    def test_the_new_file_and_folder_libraries_are_whole(self):
        libs = {x["name"]: x for x in L.load(SHARED)}
        self.assertEqual(libs["newdisk"]["needs"], ["newfile"])                  # the image builders place their file by newfile
        self.assertIn("newfile", libs["cbm1541"]["needs"])                       # so does the 1541's Create and Save
        self.assertEqual(sorted(libs["folderhd"]["sources"]), ["amiga_ffs.cc", "folderhd.cc", "folderhd_exports.cc", "folderhd_fold.cc"])
        self.assertEqual(libs["ftp"]["sources"], ["ftp_client.cc", "ftp_exports.cc"])
        srcs = [f for x in libs.values() for f in x["sources"]]
        for generic in ("textcopy.cc", "sectors.cc", "winslot.cc", "netfs.cc", "fm_fs.cc"):   # the files that generic code needs stay out of every library
            self.assertNotIn(generic, srcs)

    def test_table_has_a_row_per_library_with_its_entries(self):
        libs = L.load(SHARED)
        t = L.table_text(libs)
        self.assertEqual(t.count('\n    { "'), len(libs))
        self.assertIn('{ "cbm1541", cbm1541_bind, cbm1541_register, "drive_a", 0, 0, 0 },', t)
        self.assertIn('{ "uci", uci_bind, 0, 0, 0, 0, uci_take_freeze_request },', t)
        self.assertIn('{ "dma", 0, 0, 0, 0, dmasvc_start, 0 },', t)
        self.assertIn('{ "newfile", 0, 0, 0, 0, 0, 0 },', t)                      # a library without an entry: its code is called by the libraries that need it
        self.assertIn('{ "newdisk", 0, newdisk_register, "floppy", 0, 0, 0 },', t)
        self.assertIn('{ "folderhd", folderhd_bind, 0, 0, 0, 0, 0 },', t)
        self.assertIn('{ "fastseek", fastseek_bind, 0, 0, 0, 0, 0 },', t)
        self.assertIn('{ "ftp", ftp_bind, 0, 0, 0, 0, 0 },', t)
        self.assertIn("void tempdisk_lib_init(const BoardProfile &);", t)
        self.assertIn("const unsigned g_libtab_n = %d;" % len(libs), t)
        self.assertEqual(L.mk_text(libs).split()[0:2], ["LIB_SRCS_CC", "="])

    def test_subset_reader_agrees_with_tomllib(self):
        text = open(SHARED).read()
        try:
            import tomllib
        except ImportError:
            self.skipTest("no tomllib")
        self.assertEqual(D._Toml(text).parse(), tomllib.loads(text))

    def test_the_generic_sources_name_no_library(self):
        self.assertEqual(L.lint(L.load(SHARED), ROOT), [])


class Planted(unittest.TestCase):
    def tree(self):
        d = tempfile.mkdtemp()
        write(os.path.join(d, "coreapp/src/libfoo.cc"), "int foo_value(void) { return 7; }\nvoid foo_bind(void) {}\n")
        write(os.path.join(d, "coreapp/src/libfoo.h"), "int foo_value(void);\n")
        write(os.path.join(d, "uicore/ui.c"), "int ui_x(void) { return 1; }\n")
        return d

    def libs(self, d):
        p = os.path.join(d, "b.toml")
        write(p, '[app]\nname = "t"\n[[library]]\nname = "foo"\nsources = ["libfoo.cc"]\nheaders = ["libfoo.h"]\nbind = "foo_bind"\n')
        return L.load(p)

    def test_lint(self):
        d = self.tree()
        libs = self.libs(d)
        write(os.path.join(d, "coreapp/src/clean.cc"), '#include "other.h"\nint c(void) { return 1; }\n')
        self.assertEqual(L.lint(libs, d), [])

    @unittest.skipUnless(HAVE_CC, "needs cc and nm")
    def test_symcheck(self):
        d = self.tree()
        libs = self.libs(d)
        o = os.path.join(d, "obj")
        os.makedirs(o)
        write(os.path.join(d, "coreapp/src/plain.c"), "int p(void) { return 2; }\n")
        write(os.path.join(d, "libfoo.c"), "int foo_value(void) { return 7; }\nvoid foo_bind(void) {}\n")
        for src, obj in (("libfoo.c", "libfoo.o"), ("coreapp/src/plain.c", "plain.o")):
            subprocess.check_call(["cc", "-c", "-o", os.path.join(o, obj), os.path.join(d, src)])
        self.assertEqual(L.symcheck(libs, o, d), ([], []))


if __name__ == "__main__":
    unittest.main()
