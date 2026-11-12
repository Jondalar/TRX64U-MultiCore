#!/usr/bin/env python3
"""tools/release_check.py under test: a complete zip passes, and each way of being short of the bundle fails."""
import os
import shutil
import tempfile
import unittest
import zipfile

import release_check


def bundle(d, files):
    for name, data in files.items():
        with open(os.path.join(d, name), "wb") as f:
            f.write(data)


def zip_of(d, zp, names, grow=None):
    with zipfile.ZipFile(zp, "w") as z:
        for n in names:
            data = open(os.path.join(d, n), "rb").read()
            z.writestr("trx64mc-x/" + n, data + (b"!" if n == grow else b""))


class ZipCheck(unittest.TestCase):
    def setUp(self):
        self.d = tempfile.mkdtemp()
        self.b = os.path.join(self.d, "trx64mc-x")
        os.mkdir(self.b)
        bundle(self.b, {"a.cor": b"a" * 100, "b.cor": b"b" * 50, "INSTALL.md": b"notes"})
        self.zp = os.path.join(self.d, "trx64mc-x.zip")

    def tearDown(self):
        shutil.rmtree(self.d)

    def test_complete(self):
        zip_of(self.b, self.zp, ["a.cor", "b.cor", "INSTALL.md"])
        self.assertEqual(release_check.check_zip(self.b, self.zp), [])

    def test_missing_file_fails(self):
        zip_of(self.b, self.zp, ["a.cor", "INSTALL.md"])              # b.cor left out
        bad = release_check.check_zip(self.b, self.zp)
        self.assertEqual(len(bad), 1)
        self.assertIn("missing b.cor", bad[0])

    def test_wrong_size_fails(self):
        zip_of(self.b, self.zp, ["a.cor", "b.cor", "INSTALL.md"], grow="a.cor")
        bad = release_check.check_zip(self.b, self.zp)
        self.assertEqual(len(bad), 1)
        self.assertIn("a.cor is 101 bytes in the zip and 100 in the bundle", bad[0])

    def test_extra_file_fails(self):
        zip_of(self.b, self.zp, ["a.cor", "b.cor", "INSTALL.md"])
        with zipfile.ZipFile(self.zp, "a") as z:
            z.writestr("trx64mc-x/stray.bin", b"x")
        bad = release_check.check_zip(self.b, self.zp)
        self.assertEqual(len(bad), 1)
        self.assertIn("stray.bin", bad[0])


if __name__ == "__main__":
    unittest.main()
