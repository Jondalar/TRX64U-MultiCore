#!/usr/bin/env python3
"""Host test for tools/libtab.py (T239.3 P3-1): the TOML B of the shared app loads and every file it names exists, its library table has one row per library with the entries
the TOML names, the build image's TOML reader (the subset) reads it as tomllib does, and the two checks (the generic sources name no library, the objects of a build use no
library symbol outside the table) find a planted breach and pass a clean tree.

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
        for need in ("c64cart", "cbm1541", "uci", "sid", "tempdisk", "cbmprg", "cbmrun", "dma", "t64", "online"):
            self.assertIn(need, names)
        run = [x for x in libs if x["name"] == "cbmrun"][0]
        self.assertIn("c64_go.cc", run["sources"])                   # c64_go is part of cbmrun (P1-8)

    def test_table_has_a_row_per_library_with_its_entries(self):
        libs = L.load(SHARED)
        t = L.table_text(libs)
        self.assertEqual(t.count('\n    { "'), len(libs))
        self.assertIn('{ "cbm1541", cbm1541_bind, cbm1541_register, "drive_a", 0, 0, 0 },', t)
        self.assertIn('{ "uci", uci_bind, 0, 0, 0, 0, uci_take_freeze_request },', t)
        self.assertIn('{ "dma", 0, 0, 0, 0, dmasvc_start, 0 },', t)
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


class Refusals(unittest.TestCase):
    def refused(self, body, word):
        with self.assertRaises(L.LibTabError) as e:
            L.load(toml_b(body))
        self.assertIn(word, str(e.exception))

    def test_refusals(self):
        self.refused('[[library]]\nname = "a"\nsources = ["a.cc"]\nbogus = 1\n', "unknown key")
        self.refused('[[library]]\nname = "a"\nsources = ["a.cc"]\n[[library]]\nname = "b"\nsources = ["a.cc"]\n', "a.cc")
        self.refused('[[library]]\nname = "a"\nsources = ["a.cc"]\nneeds = ["zz"]\n', "needs 'zz'")
        self.refused('[[library]]\nname = "a"\nsources = ["a.cc"]\ncapability = "x"\n', "capability")
        self.refused('[[library]]\nname = "a"\nsources = ["a.cc"]\nbind = "not a name"\n', "function name")


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
        write(os.path.join(d, "coreapp/src/dirty.cc"), '#include "libfoo.h"\nint c(void) { return foo_value(); }\n')
        write(os.path.join(d, "uicore/bind.c"), "void g(void) { foo_bind(); }\n")
        found = " ".join(L.lint(libs, d))
        self.assertIn("dirty.cc:1 includes libfoo.h", found)
        self.assertIn("bind.c:1 calls foo_bind", found)

    @unittest.skipUnless(HAVE_CC, "needs cc and nm")
    def test_symcheck(self):
        d = self.tree()
        libs = self.libs(d)
        o = os.path.join(d, "obj")
        os.makedirs(o)
        write(os.path.join(d, "coreapp/src/uses.c"), "extern int foo_value(void);\nint u(void) { return foo_value(); }\n")
        write(os.path.join(d, "coreapp/src/plain.c"), "int p(void) { return 2; }\n")
        write(os.path.join(d, "libfoo.c"), "int foo_value(void) { return 7; }\nvoid foo_bind(void) {}\n")
        for src, obj in (("libfoo.c", "libfoo.o"), ("coreapp/src/uses.c", "uses.o"), ("coreapp/src/plain.c", "plain.o")):
            subprocess.check_call(["cc", "-c", "-o", os.path.join(o, obj), os.path.join(d, src)])
        missing, found = L.symcheck(libs, o, d)
        self.assertEqual(missing, [])
        self.assertEqual(len(found), 1)
        self.assertIn("uses.o uses", found[0])
        os.remove(os.path.join(o, "uses.o"))
        self.assertEqual(L.symcheck(libs, o, d), ([], []))
        os.remove(os.path.join(o, "libfoo.o"))
        self.assertEqual(L.symcheck(libs, o, d)[0], ["libfoo.cc"])           # a source TOML B names with no object in the build


if __name__ == "__main__":
    unittest.main()
