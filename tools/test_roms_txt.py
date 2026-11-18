#!/usr/bin/env python3
"""T148 D8, S5: ROMS.txt of a package and the rule that a package holds no core folder.

ROMS.txt: every core with a descriptor has its section and its folder cores/<id>/roms/; every ROM file name of the descriptors' table (the one
tests/coreapp/run.sh compares with the RomSpecs) is in it; no link leaves the legal domains. Controls: a table with a name taken out and a manual
with another link in 12.3 each have to change the result in the way the test looks for.

The layout rule: ci/package.sh's output and a release made by tools/make_release_mc.sh pass; the same with a core folder added is refused
(the negative control the lint stage cannot do without).

  python3 tools/test_roms_txt.py
"""
import os
import shutil
import subprocess
import sys
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)
import gen_roms_txt as G  # noqa: E402
import check_package_layout as L  # noqa: E402
import release_notes_mc as N  # noqa: E402


class RomsTxt(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.text = G.render()

    def test_every_core_has_its_section(self):
        for cid, name in G.cores():
            self.assertIn("%s  (cores/%s/roms/)" % (name, cid), self.text)

    def test_every_rom_name_of_the_table_is_there(self):
        off = G.left_out()
        for r in G.rows():
            for n in r["names"]:
                if r["core"].lower() not in off:                  # pipeline = false cores are not listed (test below)
                    self.assertIn(n, self.text)

    def test_a_core_out_of_the_pipeline_is_not_listed(self):
        for cid in G.left_out():                                  # pipeline = false (T92 D-17e) or an import pin with PIPELINE=false
            self.assertNotIn("cores/%s/roms/" % cid, self.text)

    def test_no_link_leaves_the_legal_domains(self):
        self.assertEqual(N.bad_links(self.text), [])
        self.assertIn("vice-emu.sourceforge.io", self.text)

    def test_the_folder_is_the_core_folder_not_the_old_one(self):
        self.assertIn("<medium>/cores/<name>/roms/", self.text)
        self.assertNotIn("roms/c16", self.text.replace("roms/<folder>/", ""))

    def test_control_a_table_row_missing_is_seen(self):
        rows = G.rows()
        gone = [r for r in rows if r["names"][0] != "apple2e-video.rom"]
        self.assertEqual(len(gone), len(rows) - 1)
        old = G.rows
        G.rows = lambda: gone
        try:
            text = G.render()
        finally:
            G.rows = old
        self.assertNotIn("apple2e-video.rom", text)               # the test above would have failed for this table

    def test_control_another_link_is_dropped_from_the_legal_part(self):
        old = G.MANUAL
        d = tempfile.mkdtemp()
        try:
            f = os.path.join(d, "12-roms.md")
            src = open(old, encoding="utf-8").read().replace("https://www.amigaforever.com/", "https://roms.example/x")
            open(f, "w", encoding="utf-8").write(src)
            G.MANUAL = f
            text = G.render()
        finally:
            G.MANUAL = old
            shutil.rmtree(d)
        self.assertNotIn("roms.example", text)


class PackageLayout(unittest.TestCase):
    def test_a_flat_package_passes_and_a_core_folder_does_not(self):
        d = tempfile.mkdtemp()
        try:
            pkg = os.path.join(d, "pkg")
            os.makedirs(pkg)
            for f in ("vc20.cor", "amiga.cor", "ROMS.txt", "INSTALL.md"):
                open(os.path.join(pkg, f), "w").write("x")
            r = subprocess.run([sys.executable, os.path.join(HERE, "check_package_layout.py"), pkg], capture_output=True, text=True)
            self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
            os.makedirs(os.path.join(pkg, "vc20", "roms"))                     # the control: the folder of a core
            r = subprocess.run([sys.executable, os.path.join(HERE, "check_package_layout.py"), pkg], capture_output=True, text=True)
            self.assertEqual(r.returncode, 1, r.stdout + r.stderr)
            self.assertIn("vc20/", r.stdout)
        finally:
            shutil.rmtree(d)

    def test_the_release_of_make_release_mc_passes(self):
        import test_make_release_mc as M
        t = M.ReleaseBundleTest("test_takes_the_pipeline_update")
        t.setUp()
        r = t.run_it("--update", t.cfw, "--update-ue2", t.ue2)
        self.assertEqual(r.returncode, 0, r.stderr[-2000:])
        d = os.path.join(t.out, "trx64mc-main-deadbeef")
        self.assertTrue(os.path.exists(os.path.join(d, "ROMS.txt")))
        self.assertEqual(L.violations(L.names_of(d)), [])
        self.assertEqual(L.violations(L.names_of(os.path.join(t.out, "trx64mc-main-deadbeef.zip"))), [])


if __name__ == "__main__":
    unittest.main()
