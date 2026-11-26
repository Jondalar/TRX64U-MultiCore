#!/usr/bin/env python3
"""Host test for tools/make_release_mc.sh (T94 P5/P6): the bundle takes the update file the pipeline
built, and refuses half a pair.

The negative control is `--update` without `--update-ue2`: a release that ships a .cfw and no .ue2
would install on our firmware and not on stock, and nothing downstream would notice.

  python3 tools/test_make_release_mc.py     (also collected by python3 -m pytest)
"""
import os
import struct
import subprocess
import sys
import re
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)
import cor_pack as C  # noqa: E402
import release_notes_mc as N  # noqa: E402

KEY = 0x820EDED2


def bitstream(key=KEY):
    """A .bit the AXSS walk of cor_pack.host_key_from_bit accepts: header, sync, one type-1 write of
    USR_ACCESS, then DESYNC."""
    hdr = C.BIT_MAGIC
    for k in b"abcd":
        txt = b"x" * 5 + b"\0"
        hdr += bytes([k]) + struct.pack(">H", len(txt)) + txt
    body = struct.pack(">I", C.AXSS_SYNC)
    body += struct.pack(">I", (1 << 29) | (2 << 27) | (C.REG_AXSS << 13) | 1) + struct.pack(">I", key)
    body += struct.pack(">I", (1 << 29) | (2 << 27) | (C.REG_CMD << 13) | 1) + struct.pack(">I", C.CMD_DESYNC)
    return hdr + b"e" + struct.pack(">I", len(body)) + body


def partial_bit():
    data = b"\xFF" * 8 + C.SYNC + bytes(range(256)) * 5
    return data[: len(data) // 4 * 4]


def core(path, name):
    app = bytes((i * 7) & 0xFF for i in range(4096))
    blob = C.pack(name, "0.1", partial_bit(), app, 0x30000, 0x30000, "%08x" % KEY)
    open(path, "wb").write(blob)


def dotapp(n=2048):
    body = bytes((i * 3) & 0xFF for i in range(n))
    return struct.pack("<III", 0x220000, len(body), 0x220000) + body


class ReleaseBundleTest(unittest.TestCase):
    def setUp(self):
        self.d = tempfile.mkdtemp()
        self.bit = os.path.join(self.d, "c64_basic.bit")
        open(self.bit, "wb").write(bitstream())
        self.app = os.path.join(self.d, "hostapp.app")
        open(self.app, "wb").write(dotapp())
        self.cor = os.path.join(self.d, "vc20.cor")
        core(self.cor, "VIC-20")
        self.cfw = os.path.join(self.d, "trx64host_update_deadbeef_20261015.cfw")
        self.ue2 = os.path.join(self.d, "trx64host_update_deadbeef_20261015.ue2")
        open(self.cfw, "wb").write(b"CFW" * 100)
        open(self.ue2, "wb").write(b"UE2" * 100)
        self.out = os.path.join(self.d, "dist")

    def run_it(self, *extra):
        cmd = ["bash", os.path.join(HERE, "make_release_mc.sh"), "--id", "main-deadbeef",
               "--bit", self.bit, "--app", self.app, "--bit-commit", "a" * 40,
               "--app-commit", "b" * 40, "--cor", self.cor, "--out", self.out] + list(extra)
        return subprocess.run(cmd, capture_output=True, text=True)

    def test_takes_the_pipeline_update(self):
        r = self.run_it("--update", self.cfw, "--update-ue2", self.ue2)
        self.assertEqual(r.returncode, 0, r.stderr[-2000:])
        d = os.path.join(self.out, "trx64mc-main-deadbeef")
        for f in ("trx64host_update_deadbeef_20261015.cfw", "trx64host_update_deadbeef_20261015.ue2",
                  "vc20.cor", "INSTALL.md", "SHA256SUMS"):
            self.assertTrue(os.path.exists(os.path.join(d, f)), f)
        # the name in INSTALL.md follows the file, not the default of the day
        self.assertIn("trx64host_update_deadbeef_20261015.cfw", open(os.path.join(d, "INSTALL.md")).read())
        self.assertEqual(open(os.path.join(d, "trx64host_update_deadbeef_20261015.cfw"), "rb").read(),
                         open(self.cfw, "rb").read())

    def test_unzip_ships_and_the_install_text_says_where_it_goes(self):
        # T180 D9: unzip.u2a rides along beside the .cor files; INSTALL.md names /cores/apps/ and lists it in the checksums
        u2a = os.path.join(self.d, "unzip.u2a")
        open(u2a, "wb").write(b"U2A" * 50)
        r = self.run_it("--update", self.cfw, "--update-ue2", self.ue2, "--u2a", u2a)
        self.assertEqual(r.returncode, 0, r.stderr[-2000:])
        d = os.path.join(self.out, "trx64mc-main-deadbeef")
        self.assertEqual(open(os.path.join(d, "unzip.u2a"), "rb").read(), b"U2A" * 50)
        txt = open(os.path.join(d, "INSTALL.md"), encoding="utf-8").read()
        self.assertIn("/cores/apps/unzip.u2a", txt)
        self.assertIn("| `unzip.u2a` |", txt)
        self.assertRegex(txt, r"[0-9a-f]{64}  unzip\.u2a")
        self.assertIn("unzip.u2a", open(os.path.join(d, "SHA256SUMS")).read())

    def test_no_u2a_no_unzip_text(self):
        # the control: without --u2a the bundle says nothing about Unzip, so the line above comes from the file and not from the template
        r = self.run_it("--update", self.cfw, "--update-ue2", self.ue2)
        self.assertEqual(r.returncode, 0, r.stderr[-2000:])
        d = os.path.join(self.out, "trx64mc-main-deadbeef")
        self.assertFalse(os.path.exists(os.path.join(d, "unzip.u2a")))
        self.assertNotIn("unzip.u2a", open(os.path.join(d, "INSTALL.md"), encoding="utf-8").read())

    def test_missing_u2a_is_refused(self):
        r = self.run_it("--update", self.cfw, "--update-ue2", self.ue2, "--u2a", os.path.join(self.d, "nope.u2a"))
        self.assertNotEqual(r.returncode, 0)

    def _diag_inputs(self):
        # T239.5 D21: a package as ci/package.sh makes it: diag.cor next to vc20.cor and the SD/ tree the core lanes delivered (diag's, and one of another core)
        diag = os.path.join(self.d, "diag.cor")
        core(diag, "Port Diagnostics")
        sd = os.path.join(self.d, "SD")
        for rel, body in (("cores/diag/tests/avprobe.toml", b"a=1\n"), ("cores/diag/presets/p.toml", b"b=2\n"), ("cores/vc20/extra/x.toml", b"c=3\n")):
            os.makedirs(os.path.dirname(os.path.join(sd, rel)), exist_ok=True)
            open(os.path.join(sd, rel), "wb").write(body)
        return diag, sd

    def _sd_names(self, d):
        import zipfile
        z = os.path.join(d, "SD.zip")
        return sorted(zipfile.ZipFile(z).namelist()) if os.path.exists(z) else []

    def test_pre_release_carries_diag_and_its_sd_folder(self):
        diag, sd = self._diag_inputs()
        r = self.run_it("--update", self.cfw, "--update-ue2", self.ue2, "--cor", diag, "--sd", sd)
        self.assertEqual(r.returncode, 0, r.stderr[-2000:])
        d = os.path.join(self.out, "trx64mc-main-deadbeef")
        self.assertTrue(os.path.exists(os.path.join(d, "diag.cor")))
        self.assertIn("SD/cores/diag/tests/avprobe.toml", self._sd_names(d))
        self.assertIn("SD/cores/vc20/extra/x.toml", self._sd_names(d))
        txt = open(os.path.join(d, "INSTALL.md"), encoding="utf-8").read()
        self.assertIn("SD.zip", txt)
        self.assertNotIn("by FTP", txt.split("Port Diagnostics", 1)[-1][:3000])

    def test_release_bundle_has_neither_diag_nor_its_sd_folder(self):
        diag, sd = self._diag_inputs()
        r = self.run_it("--update", self.cfw, "--update-ue2", self.ue2, "--cor", diag, "--sd", sd, "--release")
        self.assertEqual(r.returncode, 0, r.stderr[-2000:])
        d = os.path.join(self.out, "trx64mc-main-deadbeef")
        self.assertFalse(os.path.exists(os.path.join(d, "diag.cor")))
        self.assertTrue(os.path.exists(os.path.join(d, "vc20.cor")))                 # another core is unchanged
        self.assertEqual(self._sd_names(d), ["SD/cores/vc20/extra/x.toml"])         # the other core's SD files stay, diag's go
        self.assertNotIn("avprobe", open(os.path.join(d, "SHA256SUMS")).read())
        self.assertNotIn("diag.cor", open(os.path.join(d, "INSTALL.md"), encoding="utf-8").read())

    def test_release_bundle_without_any_sd_file_has_no_sd_zip(self):
        diag, sd = self._diag_inputs()
        import shutil
        shutil.rmtree(os.path.join(sd, "cores", "vc20"))
        r = self.run_it("--update", self.cfw, "--update-ue2", self.ue2, "--cor", diag, "--sd", sd, "--release")
        self.assertEqual(r.returncode, 0, r.stderr[-2000:])
        self.assertFalse(os.path.exists(os.path.join(self.out, "trx64mc-main-deadbeef", "SD.zip")))

    def test_diag_is_the_pre_release_only_core_and_no_other(self):
        r = subprocess.run([sys.executable, "-I", os.path.join(HERE, "core_toml.py"), "pre-release-only"], capture_output=True, text=True)
        self.assertEqual(r.stdout.split(), ["diag"], r.stderr)

    def test_half_a_pair_is_refused(self):
        # the negative control: it must fail, and before anything is written
        r = self.run_it("--update", self.cfw)
        self.assertEqual(r.returncode, 2, r.stdout[-500:])
        self.assertIn("needs both", r.stderr)
        self.assertFalse(os.path.exists(os.path.join(self.out, "trx64mc-main-deadbeef")))

    def test_missing_update_file_is_refused(self):
        r = self.run_it("--update", os.path.join(self.d, "nope.cfw"), "--update-ue2", self.ue2)
        self.assertEqual(r.returncode, 2, r.stdout[-500:])


def core_toml(path):
    """id and name of a cores/<id>/core.toml (the two keys the notes are matched on)."""
    out = {}
    for line in open(path, encoding="utf-8"):
        m = re.match(r'^(id|name)\s*=\s*"([^"]*)"', line)
        if m:
            out[m.group(1)] = m.group(2)
        if re.match(r'^flash_filler\s*=\s*true\b', line):
            out["flash_filler"] = True      # T123: the null core is no core a user starts: no .cor, no notes line
        if re.match(r'^pipeline\s*=\s*false\b', line):
            out["off"] = True               # T92 D-17e: out of the pipeline, shipped nowhere
        if re.match(r'^build_only\s*=\s*true\b', line):
            out["build_only"] = True        # T115: the reference core (c64ref) is built to route against, never shipped
    return out


class InstallNotesTest(unittest.TestCase):
    """INSTALL.md of the bundle: every core has its ROM paragraph and no link leaves the legal domains."""

    @classmethod
    def setUpClass(cls):
        cls.d = tempfile.mkdtemp()
        cls.cores = []
        cls.off = set()                              # T92 D-17e: pipeline = false, no .cor in a bundle; its table line in the notes stays (code stays)
        for t in sorted(os.listdir(os.path.join(ROOT, "cores"))):
            f = os.path.join(ROOT, "cores", t, "core.toml")
            if not os.path.exists(f):                # a large core (T115 7) has its descriptor under mono/
                f = os.path.join(ROOT, "cores", t, "mono", "core.toml")
            if os.path.exists(f) and core_toml(f).get("off"):
                cls.off.add(core_toml(f)["id"])
            elif os.path.exists(f) and not core_toml(f).get("flash_filler") and not core_toml(f).get("build_only"):
                cls.cores.append(core_toml(f))
        cls.cord = os.path.join(cls.d, "bundle")
        os.makedirs(cls.cord)
        for c in cls.cores:
            core(os.path.join(cls.cord, c["id"] + ".cor"), c["name"])
        bit = os.path.join(cls.d, "c64_basic.bit")
        open(bit, "wb").write(bitstream())
        app = os.path.join(cls.d, "hostapp.app")
        open(app, "wb").write(dotapp())
        cls.out = os.path.join(cls.d, "INSTALL.md")
        r = subprocess.run([sys.executable, os.path.join(HERE, "release_notes_mc.py"), "--id", "main-deadbeef",
                            "--name", "trx64host_update_x", "--bit", bit, "--app", app, "--bit-commit", "a" * 40,
                            "--app-commit", "b" * 40, "--cor-dir", cls.cord, "-o", cls.out],
                           capture_output=True, text=True)
        cls.rc, cls.err = r.returncode, r.stderr
        cls.text = open(cls.out, encoding="utf-8").read() if r.returncode == 0 else ""

    def table_line(self, c):
        return next(x for x in N.CORES if x["id"] == c["id"])

    def test_generated(self):
        self.assertEqual(self.rc, 0, self.err[-2000:])
        self.assertEqual(self.err, "", "a core without ROM notes warns on stderr")

    def test_every_repo_core_has_its_own_table_line(self):
        self.assertEqual({x["id"] for x in N.CORES}, {c["id"] for c in self.cores} | self.off)
        for c in self.cores:
            self.assertIs(N.core_of(c["name"]), self.table_line(c), "core.toml name %r" % c["name"])

    def test_sections_exist(self):
        for h in ("## 1. What you need", "## 3. Install the host", "## 4. The stick", "## 5. ROMs per core",
                  "## 6. First start", "## 7. In a core", "## 8. Full firmwares (.cfw)", "## 9. Known problems",
                  "## 10. Reporting", "## 11. Checksums"):
            self.assertIn(h, self.text)
        self.assertIn("no ROM, Kickstart or disk image", self.text)
        self.assertIn("/Flash/legacy/fullcore.bit", self.text)
        self.assertIn("Run Update", self.text)

    def test_every_core_has_a_rom_paragraph(self):
        for c in self.cores:
            self.assertIn("### " + self.table_line(c)["title"], self.text, c["id"])
            self.assertTrue(N.PARAS[c["id"]])

    def test_dynamic_tables(self):
        for c in self.cores:
            self.assertIn("| `%s.cor` | %s 0.1," % (c["id"], c["name"]), self.text)

    def test_links_stay_in_the_allow_list(self):
        self.assertTrue(re.findall(r"https?://\S+", self.text))
        self.assertEqual(N.bad_links(self.text), [])

    def test_a_forbidden_domain_is_caught(self):
        # the negative control: the check has to be able to fail
        self.assertEqual(N.bad_links("at https://romhunt.example/kick13.rom now"), ["https://romhunt.example/kick13.rom"])
        self.assertEqual(N.bad_links("(https://vice-emu.sourceforge.io/)"), [])
        self.assertEqual(N.bad_links("http://vice-emu.sourceforge.io.evil.example/x"),
                         ["http://vice-emu.sourceforge.io.evil.example/x"])
        # and the generator refuses to write such a text
        N.PARAS["nes"].append("see https://roms.example/x")
        try:
            class A:
                id, name, bit_commit, app_commit, cor_dir = "i", "n", "b", "a", self.cord
            meta = {"name": "Nintendo Entertainment System", "version": "1", "partial_size": 1, "app_size": 1}
            with self.assertRaises(SystemExit) as cm:
                N.render(A, [("nes.cor", meta)], 1, 1, 0)
            self.assertIn("allow-list", str(cm.exception))
        finally:
            N.PARAS["nes"].pop()

    def test_txu_ships_with_its_install_step(self):
        # Alex 2026-11-05: the tested .txu ships, a board on a MultiCore host installs it from F3 (T152); without --txu no such text
        meta = {"name": "Nintendo Entertainment System", "version": "1", "partial_size": 1, "app_size": 1}
        class A:
            id, name, bit_commit, app_commit, cor_dir = "i", "n", "b", "a", self.cord
        off, _ = N.render(A, [("nes.cor", meta)], 1, 1, 0)
        self.assertNotIn(".txu", off)
        A.txu = "trx64host_update_x.txu"
        on, _ = N.render(A, [("nes.cor", meta)], 1, 1, 0)
        self.assertIn("| `trx64host_update_x.txu` |", on)
        self.assertIn("### 3a. Already on a TRX64 MultiCore host", on)
        self.assertIn("**F3**, open `trx64host_update_x.txu`", on)

    def test_txu_text_tells_where_unzip_goes(self):
        # #43 (a tester, main-2bd67963: no "Unzip here"): the .txu copies no file, the 3a text must say where unzip.u2a goes; the installer (section 3) does it itself
        meta = {"name": "Nintendo Entertainment System", "version": "1", "partial_size": 1, "app_size": 1}
        cord = os.path.join(self.d, "withapp")
        os.makedirs(cord)
        open(os.path.join(cord, "nes.cor"), "wb").write(b"x")
        open(os.path.join(cord, "unzip.u2a"), "wb").write(b"x")
        class A:
            id, name, bit_commit, app_commit, txu = "i", "n", "b", "a", "trx64host_update_x.txu"
        A.cor_dir = cord
        on, _ = N.render(A, [("nes.cor", meta)], 1, 1, 0)
        three_a = on.split("### 3a.")[1].split("## 4.")[0]
        self.assertIn("copy `unzip.u2a` into `/cores/apps/`", three_a)
        self.assertIn("every `.u2a` (Unzip) into `/cores/apps`", on.split("## 3.")[1].split("### 3a.")[0])
        os.remove(os.path.join(cord, "unzip.u2a"))
        off, _ = N.render(A, [("nes.cor", meta)], 1, 1, 0)
        self.assertNotIn("/cores/apps/` (create", off.split("### 3a.")[1].split("## 4.")[0])

    def test_unknown_core_is_named(self):
        self.assertIsNone(N.core_of("Sinclair QL"))


if __name__ == "__main__":
    unittest.main()
