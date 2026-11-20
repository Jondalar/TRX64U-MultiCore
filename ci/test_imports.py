#!/usr/bin/env python3
"""T239.1 D43: ci/imports.py - the entry names of the pins (a PIPELINE=false pin has none), a key that follows the pin and not the presence of a private
file, a pack that refuses an input the store lacks, and the package's finish (version form, this run's application for a pin without APP_FILE, a pin
with APP_FILE keeping its own). Unit and happy path only; the real packing is proven with the pinned inputs (T239.1 §5)."""
import hashlib
import os
import shutil
import struct
import sys
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(ROOT, "tools"))
import imports as I  # noqa: E402
import cor_pack as C  # noqa: E402

APP = bytes((i * 5) & 0xFF for i in range(4096))
NEW_APP = bytes((i * 3) & 0xFF for i in range(5000))


def full_bit(frames=200):
    """A synthetic full 7-series .bit (as tools/test_cor_pack.py makes it)."""
    def w(*v):
        return b"".join(struct.pack(">I", x) for x in v)
    hdr = C.BIT_MAGIC
    for key, txt in zip(b"abcd", (b"top;UserID=0XFFFFFFFF;Version=2024.1\0", b"7a50tfgg484\0", b"2026/10/25\0", b"12:00:00\0")):
        hdr += bytes([key]) + struct.pack(">H", len(txt)) + txt
    cfg = w(0xFFFFFFFF, 0xFFFFFFFF, 0x000000BB, 0x11220044, 0xFFFFFFFF, 0xFFFFFFFF, 0xAA995566, 0x20000000, 0x30008001, 0, 0x30018001, 0x0362C093)
    cfg += w(0x3001A001, 0x553525D3)
    cfg += w(0x30004000, 0x50000000 | frames) + b"".join(struct.pack(">I", (i * 2654435761) & 0xFFFFFFFF) for i in range(frames))
    cfg += w(0x30008001, 0x0000000D, 0x20000000, 0x20000000)
    return hdr + b"e" + struct.pack(">I", len(cfg)) + cfg


class ImportsTest(unittest.TestCase):
    def setUp(self):
        self.root = tempfile.mkdtemp()
        self.addCleanup(shutil.rmtree, self.root, True)
        for rel in I.CODE + ("tools/descrec.py",):
            dst = os.path.join(self.root, rel)
            os.makedirs(os.path.dirname(dst), exist_ok=True)
            shutil.copy2(os.path.join(ROOT, rel), dst)
        self.write("release/imports/a.pin", 'ID=a\nNAME="A"\nSEMVER=1.2.3\nBIT_SHA256=%s\nFILES=release/imports/a_files\n' % ("1" * 64))
        self.write("release/imports/b.pin", 'ID=b\nNAME="B"\nSEMVER=1.2.3\nBIT_SHA256=%s\nAPP_FILE=x.app\nAPP_SHA256=%s\n' % ("1" * 64, "2" * 64))
        self.write("release/imports/c.pin", 'ID=c\nPIPELINE=false\n')
        self.write("release/imports/a_files/README", "public\n")
        self.write("ci/private.txt", "file 4 %s release/imports/a_files/p.bin cor=a.cor\n" % ("3" * 64))
        os.environ["GITHUB_REF_TYPE"] = "branch"
        os.environ["TRX_VERSION_SHA8"] = "deadbeef"

    def write(self, rel, text):
        p = os.path.join(self.root, rel)
        os.makedirs(os.path.dirname(p), exist_ok=True)
        with open(p, "w") as f:
            f.write(text)

    def test_names_and_key(self):
        n = I.names(self.root)
        self.assertEqual([x.rsplit("-", 1)[0] for x in n], ["imp-a", "imp-b"], "a PIPELINE=false pin has no entry")
        self.assertEqual(n, I.names(self.root), "the same tree, the same names")
        self.write("release/imports/a_files/p.bin", "priv")     # the private file present (Gitea's tree) or not (GitHub's): the same key
        self.assertEqual(n, I.names(self.root))
        self.write("release/imports/a.pin", 'ID=a\nNAME="A2"\nSEMVER=1.2.3\nBIT_SHA256=%s\nFILES=release/imports/a_files\n' % ("1" * 64))
        self.assertNotEqual(n[0], I.names(self.root)[0], "a changed pin is a new entry")
        self.assertEqual(n[1], I.names(self.root)[1])

    def test_pack_refuses_what_the_store_lacks(self):
        store = os.path.join(self.root, "store")
        os.makedirs(store)
        with self.assertRaises(I.ImportError_) as e:
            I.pack(I.names(self.root)[0], store, os.path.join(self.root, "out"), root=self.root)
        self.assertIn("not in the store", str(e.exception))
        with self.assertRaises(I.ImportError_):
            I.pack("imp-a-0000000000000000", store, os.path.join(self.root, "out"), root=self.root)

    def test_finish(self):
        cor = os.path.join(self.root, "a.cor")
        open(cor, "wb").write(C.pack_large(cid="a", name="A", version="1.2.3", bit=full_bit(), app=APP))
        app = os.path.join(self.root, "coreapp_a.bin")
        open(app, "wb").write(NEW_APP)
        out = os.path.join(self.root, "out_a.cor")
        self.assertEqual(I.finish(cor, out, app=app, app_commit="cafe1234", root=self.root), "1.2.3-dev+deadbeef")
        m, recs = C.parse(open(out, "rb").read())
        self.assertEqual((m["version"], m["app_commit"], m["app_sha256"]), ("1.2.3-dev+deadbeef", "cafe1234", hashlib.sha256(NEW_APP).hexdigest()))
        self.assertEqual(recs[C.T_APP], NEW_APP)
        with self.assertRaises(I.ImportError_):
            I.finish(cor, out, root=self.root)                    # a pin without APP_FILE needs the run's application
        cor_b = os.path.join(self.root, "b.cor")
        open(cor_b, "wb").write(C.pack_large(cid="b", name="B", version="1.2.3", bit=full_bit(), app=APP))
        with self.assertRaises(I.ImportError_):
            I.finish(cor_b, out, app=app, root=self.root)         # a pin with APP_FILE keeps its own
        I.finish(cor_b, out, root=self.root)
        m, recs = C.parse(open(out, "rb").read())
        self.assertEqual((m["version"], recs[C.T_APP]), ("1.2.3-dev+deadbeef", APP))


if __name__ == "__main__":
    unittest.main()
