#!/usr/bin/env python3
"""T239.1 S8: tools/builds.py against the fake builds repository (ci/fakegh.py, no network). Happy path: `latest` takes the newest published main-<sha8> on the first-parent line
(a newer commit without a release, a draft and a side-branch commit are skipped), `files` lists the assets, `fetch` writes a verified file. The refusals: a digest that is
not the download's, SHA256SUMS that disagrees, a release that lists no SHA-256, a missing asset, a draft. A refused fetch leaves no file. Unit and happy path."""
import hashlib
import os
import subprocess
import sys
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(os.path.dirname(HERE), "ci"))
import builds as B  # noqa: E402
from fakegh import FakeGH  # noqa: E402

sha = lambda b: hashlib.sha256(b).hexdigest()
BIN = b"bitstream bytes" * 100


def git(d, *a):
    return subprocess.run(["git", "-C", d] + list(a), capture_output=True, text=True, check=True, env=dict(os.environ, GIT_AUTHOR_NAME="t", GIT_AUTHOR_EMAIL="t@e.org",
                          GIT_COMMITTER_NAME="t", GIT_COMMITTER_EMAIL="t@e.org")).stdout.strip()


class T(unittest.TestCase):
    def setUp(self):
        self.gh = FakeGH(repo="Jondalar/trx64mc-builds").start()
        self.api = B.Api(self.gh.base, "Jondalar/trx64mc-builds", None)
        self.tmp = tempfile.mkdtemp()

    def tearDown(self):
        self.gh.stop()

    def test_latest_follows_first_parent(self):
        d = os.path.join(self.tmp, "repo")
        os.mkdir(d)
        git(d, "init", "-q", "-b", "main")
        sh = []
        for i in range(3):
            open(os.path.join(d, "f"), "w").write(str(i))
            git(d, "add", "f")
            git(d, "commit", "-q", "-m", "c%d" % i)
            sh.append(git(d, "rev-parse", "--short=8", "HEAD"))
        git(d, "checkout", "-q", "-b", "side")
        open(os.path.join(d, "g"), "w").write("x")
        git(d, "add", "g")
        git(d, "commit", "-q", "-m", "side")
        side = git(d, "rev-parse", "--short=8", "HEAD")
        git(d, "checkout", "-q", "main")
        git(d, "merge", "-q", "--no-ff", "-m", "merge", "side")
        merge = git(d, "rev-parse", "--short=8", "HEAD")
        B.ROOT = d
        self.gh.add_release("main-" + side, {"SHA256SUMS": b""})                       # on the side branch only: never on the first-parent line
        self.gh.add_release("main-" + sh[2], {"a.bit": BIN}, draft=True)               # a draft does not count
        self.gh.add_release("main-" + sh[1], {"a.bit": BIN})
        self.gh.add_release("main-" + sh[0], {"a.bit": BIN})
        self.assertNotEqual(merge, sh[2])
        self.assertEqual(B.latest(self.api, "main"), "main-" + sh[1])
        self.gh.releases.pop("main-" + sh[1])
        self.assertEqual(B.latest(self.api, "main"), "main-" + sh[0])
        self.gh.releases.clear()
        with self.assertRaises(B.BuildsError):
            B.latest(self.api, "main")

    def fetch(self, tag, name="a.bit"):
        dest = os.path.join(self.tmp, "out")
        os.makedirs(dest, exist_ok=True)
        return B.fetch(self.api, tag, name, dest), dest

    def sums(self, files):
        return ("".join("%s  %s\n" % (sha(b), n) for n, b in sorted(files.items()))).encode()

    def test_fetch_happy(self):
        files = {"a.bit": BIN, "b.txt": b"x"}
        self.gh.add_release("main-0000000a", dict(files, SHA256SUMS=self.sums(files)))
        (out, got), dest = self.fetch("main-0000000a")
        self.assertEqual(open(out, "rb").read(), BIN)
        self.assertEqual(got, sha(BIN))
        self.assertEqual(sorted(os.listdir(dest)), ["a.bit"])
        self.assertEqual([a["name"] for a in self.api.release("main-0000000a")["assets"]], ["a.bit", "b.txt", "SHA256SUMS"])

    def test_refusals(self):
        files = {"a.bit": BIN}
        self.gh.add_release("main-0000000b", dict(files, SHA256SUMS=self.sums(files)), digests={"a.bit": "0" * 64})
        with self.assertRaises(B.BuildsError):
            self.fetch("main-0000000b")                                                   # GitHub's digest is not the download's
        bad = {"SHA256SUMS": ("%s  a.bit\n" % ("1" * 64)).encode()}
        self.gh.add_release("main-0000000c", dict(files, **bad))
        with self.assertRaises(B.BuildsError):
            self.fetch("main-0000000c")                                                   # SHA256SUMS differs
        self.gh.add_release("main-0000000d", files, digests={"a.bit": ""})
        with self.assertRaises(B.BuildsError):
            self.fetch("main-0000000d")                                                   # nothing to check against
        self.gh.add_release("main-0000000e", dict(files, SHA256SUMS=self.sums(files)))
        with self.assertRaises(B.BuildsError):
            self.fetch("main-0000000e", "nope.bit")                                       # no such asset
        self.gh.add_release("main-0000000f", dict(files, SHA256SUMS=self.sums(files)), draft=True)
        with self.assertRaises(B.BuildsError):
            self.fetch("main-0000000f")                                                   # a draft
        with self.assertRaises(B.BuildsError):
            self.fetch("../x")                                                            # not a tag
        self.assertEqual(os.listdir(os.path.join(self.tmp, "out")), [])                    # nothing left behind, not even a temporary file

    def sh(self, *cmd):
        env = dict(os.environ, BUILDS_API=self.gh.base)
        env.pop("GH_TOKEN", None)
        return subprocess.run(list(cmd), capture_output=True, text=True, env=env, cwd=self.tmp, timeout=120)

    def test_cli_and_release_tool(self):
        files = {"a.bit": BIN}
        self.gh.add_release("main-0000000a", dict(files, SHA256SUMS=self.sums(files)))
        out = os.path.join(self.tmp, "x.bit")
        r = self.sh(os.path.join(HERE, "builds.sh"), "fetch", "main-0000000a", "a.bit", out)
        self.assertEqual((r.returncode, open(out, "rb").read()), (0, BIN), r.stderr)
        r = self.sh(os.path.join(HERE, "builds.sh"), "files", "main-0000000a")
        self.assertEqual(r.stdout.split(), ["a.bit", "SHA256SUMS"])
        r = self.sh(os.path.join(HERE, "builds.sh"), "fetch", "main-0000000a", "nope", out)
        self.assertEqual(r.returncode, 1)
        # tools/release_mc.sh (D47) takes a pre-release with trx64u.cor over: it passes the download, SHA256SUMS and the ROM check, and stops at the
        # host check because the pre-release has no .txu
        bad = {"trx64u.cor": b"x"}
        self.gh.add_release("main-0000000b", dict(bad, SHA256SUMS=self.sums(bad)))
        r = self.sh(os.path.join(HERE, "release_mc.sh"), "main-0000000b", "--out", os.path.join(self.tmp, "dist"))
        self.assertEqual(r.returncode, 1)
        self.assertIn("2 files downloaded, SHA256SUMS ok", r.stdout)
        self.assertIn("no trx64host_update_*.txu", r.stderr)


if __name__ == "__main__":
    unittest.main(verbosity=1)
