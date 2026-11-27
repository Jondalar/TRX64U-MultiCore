#!/usr/bin/env python3
"""T239.1 D43: ci/imports.py - the entry names of the pins (a PIPELINE=false pin has none), a key that follows the pin and not the presence of a private
file, a pack that refuses an input the store lacks, the package's finish (version form, a pin with APP_FILE keeping its own application) and the pin of a finished
.cor (COR_SHA256: taken byte for byte, fetched and checked when the store lacks it). Unit and happy path only; the real packing is proven with the pinned inputs (T239.1 §5)."""
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
        cor_b = os.path.join(self.root, "b.cor")
        open(cor_b, "wb").write(C.pack_large(cid="b", name="B", version="1.2.3", bit=full_bit(), app=APP))
        out = os.path.join(self.root, "out_b.cor")
        self.assertEqual(I.finish(cor_b, out, root=self.root), "1.2.3-dev+deadbeef")        # a pin with APP_FILE keeps its application, gets the version form of the run
        m, recs = C.parse(open(out, "rb").read())
        self.assertEqual((m["version"], recs[C.T_APP]), ("1.2.3-dev+deadbeef", APP))
        cor_a = os.path.join(self.root, "a.cor")
        open(cor_a, "wb").write(C.pack_large(cid="a", name="A", version="1.2.3", bit=full_bit(), app=APP))
        with self.assertRaises(I.ImportError_):
            I.finish(cor_a, os.path.join(self.root, "out_a.cor"), root=self.root)          # neither COR_SHA256 nor APP_FILE: nothing to finish

    def test_cor_pin(self):
        """T239.3 P1-8: a COR_SHA256 pin is taken as it is: the store's file, byte for byte, in pack and in finish; a file that is not the pinned one is refused."""
        blob = bytes((i * 7) & 0xFF for i in range(5000))
        sha = hashlib.sha256(blob).hexdigest()
        self.write("release/imports/d.pin", "ID=d\nPACKAGE=pkg\nVERSION=v1\nCOR_FILE=d_1.cor\nCOR_SHA256=%s\nCOR_SIZE=%d\n" % (sha, len(blob)))
        entry = [n for n in I.names(self.root) if n.startswith("imp-d-")][0]
        store = os.path.join(self.root, "store")
        os.makedirs(store)
        with self.assertRaises(I.ImportError_) as e:
            I.pack(entry, store, os.path.join(self.root, "out_d"), root=self.root)
        self.assertIn("TRX_FW_PKG_URL", str(e.exception), "a file the store lacks and no URL: refused")
        open(os.path.join(store, sha), "wb").write(blob)
        cor = I.pack(entry, store, os.path.join(self.root, "out_d"), root=self.root)
        self.assertEqual(open(cor, "rb").read(), blob)
        out = os.path.join(self.root, "pkg_d.cor")
        self.assertEqual(I.finish(cor, out, root=self.root), "as pinned")
        self.assertEqual(open(out, "rb").read(), blob)
        open(cor, "ab").write(b"x")
        with self.assertRaises(I.ImportError_):
            I.finish(cor, out, root=self.root)
        self.write("release/imports/d.pin", "ID=d\nPACKAGE=pkg\nVERSION=v1\nCOR_FILE=d_1.cor\nCOR_SHA256=%s\nCOR_SIZE=%d\n" % (sha, len(blob) + 1))
        with self.assertRaises(I.ImportError_):
            I.pack(I.names(self.root)[2], store, os.path.join(self.root, "out_d2"), root=self.root)   # the size is the pin's too

    def test_fetch_checks_what_it_gets(self):
        blob = b"cor bytes " * 100
        sha = hashlib.sha256(blob).hexdigest()
        pin = {"ID": "d", "PACKAGE": "pkg", "VERSION": "v1", "COR_FILE": "d_1.cor", "COR_SHA256": sha, "COR_SIZE": str(len(blob))}
        seen = []

        class Resp:
            def __init__(self, data):
                self.data = data
            def __enter__(self):
                return self
            def __exit__(self, *a):
                return False
            def read(self, n):
                d, self.data = self.data[:n], self.data[n:]
                return d

        def good(url, timeout):
            seen.append(url)
            return Resp(blob)
        store = os.path.join(self.root, "store2")
        I.fetch(pin, store, "x", base="https://registry.invalid/p/o/generic/", opener=good)
        self.assertEqual(seen, ["https://registry.invalid/p/o/generic/pkg/v1/d_1.cor"])
        self.assertEqual(open(os.path.join(store, sha), "rb").read(), blob)
        os.unlink(os.path.join(store, sha))
        with self.assertRaises(I.ImportError_):
            I.fetch(pin, store, "x", base="https://registry.invalid/g", opener=lambda u, timeout: Resp(blob[:-1] + b"?"))
        self.assertEqual(os.listdir(store), [], "another file is not kept")

    def test_both_pin_forms(self):
        """T261 D5: the Gitea form (PACKAGE, VERSION) and the release form (RELEASE) take the same COR_FILE, SHA-256 and size, and fetch from their own place."""
        blob = b"cor bytes " * 100
        sha = hashlib.sha256(blob).hexdigest()
        common = {"ID": "d", "COR_SHA256": sha, "COR_SIZE": str(len(blob))}
        gitea = dict(common, PACKAGE="pkg", VERSION="v1", COR_FILE="d_1.cor")
        rel = dict(common, RELEASE="fw-main-0123abcd", COR_FILE="trx64u.cor")
        self.assertEqual(I.source_url(gitea, "s", "x", base="https://r.invalid/g")[0], "https://r.invalid/g/pkg/v1/d_1.cor")
        self.assertEqual(I.source_url(rel, "s", "x", base="https://r.invalid/dl")[0], "https://r.invalid/dl/fw-main-0123abcd/trx64u.cor")
        pkg_repo = I._env_file(os.path.join(ROOT, "ci", "pkg.env"))["MC_BUILDS_REPO"]
        self.assertEqual(I.source_url(rel, "s", "x")[0], "https://github.com/%s/releases/download/fw-main-0123abcd/trx64u.cor" % pkg_repo)
        self.assertEqual(I.source_url(dict(gitea, RELEASE="fw-main-0123abcd", COR_FILE="trx64u.cor"), "s", "x")[0].split("/")[-2:], ["fw-main-0123abcd", "trx64u.cor"], "RELEASE wins")
        with self.assertRaises(I.ImportError_):
            I.source_url(dict(rel, COR_FILE="../x"), "s", "x")
        seen = []

        class Resp:
            def __init__(self, data):
                self.data = data
            def __enter__(self):
                return self
            def __exit__(self, *a):
                return False
            def read(self, n):
                d, self.data = self.data[:n], self.data[n:]
                return d

        def good(url, timeout):
            seen.append(url)
            return Resp(blob)
        store = os.path.join(self.root, "store3")
        I.fetch(rel, store, "x", base="https://r.invalid/dl", opener=good)
        self.assertEqual(seen, ["https://r.invalid/dl/fw-main-0123abcd/trx64u.cor"])
        self.assertEqual(open(os.path.join(store, sha), "rb").read(), blob)
        # a pin file of the release form is read and packed like the Gitea form: same fields, the key follows the pin's bytes
        self.write("release/imports/e.pin", "ID=e\nRELEASE=fw-main-0123abcd\nCOR_FILE=trx64u.cor\nCOR_SHA256=%s\nCOR_SIZE=%d\n" % (sha, len(blob)))
        self.write("release/imports/f.pin", "ID=f\nPACKAGE=pkg\nVERSION=v1\nCOR_FILE=f_1.cor\nCOR_SHA256=%s\nCOR_SIZE=%d\n" % (sha, len(blob)))
        os.environ.pop("TRX_FW_PKG_URL", None)
        out = I.pack([n for n in I.names(self.root) if n.startswith("imp-e-")][0], store, os.path.join(self.root, "out_e"), root=self.root)
        self.assertEqual(open(out, "rb").read(), blob, "a store that holds the file needs no fetch in either form")
        out = I.pack([n for n in I.names(self.root) if n.startswith("imp-f-")][0], store, os.path.join(self.root, "out_f"), root=self.root)
        self.assertEqual(open(out, "rb").read(), blob)

    def test_u2a_of_the_release(self):
        """T261 D7: a release-form pin with U2A fetches each file from the same release, checks size and SHA-256, and the entry holds the .cor and both .u2a; a pin without U2A is unchanged."""
        cor, crt, onl = b"cor bytes " * 100, b"crt app" * 50, b"online app" * 40
        h = lambda b: hashlib.sha256(b).hexdigest()
        files = {"trx64u.cor": cor, "trex_crt.u2a": crt, "online.u2a": onl}
        seen = []

        class Resp:
            def __init__(self, data):
                self.data = data
            def __enter__(self):
                return self
            def __exit__(self, *a):
                return False
            def read(self, n):
                d, self.data = self.data[:n], self.data[n:]
                return d

        def opener(url, timeout):
            seen.append(url)
            return Resp(files[url.rsplit("/", 1)[1]])
        u2a = "trex_crt.u2a:%s:%d online.u2a:%s:%d" % (h(crt), len(crt), h(onl), len(onl))
        head = "ID=g\nRELEASE=fw-main-0123abcd\nCOR_FILE=trx64u.cor\nCOR_SHA256=%s\nCOR_SIZE=%d\n" % (h(cor), len(cor))
        self.write("release/imports/g.pin", head + 'U2A="%s"\n' % u2a)
        pin = [p for i, _, p in I.pins(self.root) if i == "g"][0]
        self.assertEqual(I.u2a_list(pin), [("trex_crt.u2a", h(crt), len(crt)), ("online.u2a", h(onl), len(onl))])
        store = os.path.join(self.root, "store_g")
        I.fetch(pin, store, "x", base="https://r.invalid/dl", opener=opener)
        for it in I.u2a_list(pin):
            I.fetch_u2a(pin, it, store, base="https://r.invalid/dl", opener=opener)
        self.assertEqual(seen, ["https://r.invalid/dl/fw-main-0123abcd/" + n for n in ("trx64u.cor", "trex_crt.u2a", "online.u2a")], "the same release")
        out = os.path.join(self.root, "out_g")
        I.pack([n for n in I.names(self.root) if n.startswith("imp-g-")][0], store, out, root=self.root)
        self.assertEqual(sorted(os.listdir(out)), ["g.cor", "online.u2a", "trex_crt.u2a"])
        self.assertEqual(open(os.path.join(out, "trex_crt.u2a"), "rb").read(), crt)
        self.assertEqual(open(os.path.join(out, "online.u2a"), "rb").read(), onl)
        # a wrong SHA-256 (or size) is refused and not kept
        bad = dict(pin, U2A="online.u2a:%s:%d" % ("0" * 64, len(onl)))
        store2 = os.path.join(self.root, "store_g2")
        with self.assertRaises(I.ImportError_):
            I.fetch_u2a(bad, I.u2a_list(bad)[0], store2, base="https://r.invalid/dl", opener=opener)
        self.assertEqual(os.listdir(store2), [])
        # the key follows the list
        k1 = I.names(self.root)
        self.write("release/imports/g.pin", head + 'U2A="online.u2a:%s:%d"\n' % (h(onl), len(onl)))
        self.assertNotEqual(k1, I.names(self.root), "a changed U2A list is a new entry")
        # malformed lists are refused
        for badlist in ("x.u2a:%s" % ("1" * 64), "../x.u2a:%s:5" % ("1" * 64), "X.u2a:%s:5" % ("1" * 64), "a.bin:%s:5" % ("1" * 64), "a.u2a:short:5",
                        "a.u2a:%s:0" % ("1" * 64), "a.u2a:%s:5 a.u2a:%s:5" % ("1" * 64, "1" * 64)):
            self.write("release/imports/g.pin", head + 'U2A="%s"\n' % badlist)
            with self.assertRaises(I.ImportError_, msg=badlist):
                I.names(self.root)
        # reserved names, a size that is no ASCII digits, the same name in two pins
        for badlist in ("unzip.u2a:%s:5" % ("1" * 64), "a.u2a:%s:\u0665" % ("1" * 64), "a.u2a:%s:+5" % ("1" * 64)):
            self.write("release/imports/g.pin", head + 'U2A="%s"\n' % badlist)
            with self.assertRaises(I.ImportError_, msg=badlist):
                I.names(self.root)
        self.write("release/imports/g.pin", head + 'U2A="a.u2a:%s:5"\n' % ("1" * 64))
        self.write("release/imports/h.pin", head.replace("ID=g", "ID=h") + 'U2A="a.u2a:%s:5"\n' % ("1" * 64))
        with self.assertRaises(I.ImportError_):
            I.names(self.root)
        os.unlink(os.path.join(self.root, "release/imports/h.pin"))
        # U2A needs the release form
        self.write("release/imports/g.pin", "ID=g\nPACKAGE=p\nVERSION=v\nCOR_FILE=g.cor\nCOR_SHA256=%s\nCOR_SIZE=5\nU2A=\"a.u2a:%s:5\"\n" % ("1" * 64, "1" * 64))
        with self.assertRaises(I.ImportError_):
            I.names(self.root)
        # without U2A: no extra file
        self.write("release/imports/g.pin", head)
        out = os.path.join(self.root, "out_g0")
        I.pack([n for n in I.names(self.root) if n.startswith("imp-g-")][0], store, out, root=self.root)
        self.assertEqual(os.listdir(out), ["g.cor"])


if __name__ == "__main__":
    unittest.main()
