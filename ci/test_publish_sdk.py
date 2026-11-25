#!/usr/bin/env python3
"""T261 P2, D4, S4: ci/publish_sdk.py on a fake builds repository (ci/fakegh.py). Happy path: a package with no release of its version is published as
sdk-<version>-<sha8> (pre-release) with the tarball, the .u2a files and a SHA256SUMS; the lane's coreapp.map and SHA256SUMS are not. The same package again publishes
nothing (the newest release of the version lists the same tarball); a changed tarball publishes; another version is compared with its own releases only; a ref that is
not integration, main or t239.3, and a tag, publish nothing. Unit and happy path."""
import hashlib
import os
import sys
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from fakegh import FakeGH  # noqa: E402
import publish_release as R  # noqa: E402
import publish_sdk as S  # noqa: E402

SHA = "c" * 40
sha = lambda b: hashlib.sha256(b).hexdigest()


def lane_dir(tar=b"sdk-one", version="0.4.7", miniz=b"mz"):
    d = tempfile.mkdtemp()
    files = {"trx64-sdk-%s.tar.gz" % version: tar, "miniz.u2a": miniz, "unzip.u2a": b"un", "coreapp.map": b"map"}
    for n, b in files.items():
        open(os.path.join(d, n), "wb").write(b)
    open(os.path.join(d, "SHA256SUMS"), "w").write("".join("%s  ./%s\n" % (sha(b), n) for n, b in sorted(files.items())))
    return d


class SdkTest(unittest.TestCase):
    def setUp(self):
        self.gh = FakeGH().start()
        R.time.sleep = lambda s: None
        vf = tempfile.NamedTemporaryFile("w", suffix="SDK_VERSION", delete=False)
        vf.write("0.4.7\n")
        vf.close()
        self.vf = vf.name
        self.env = {"BUILDS_TOKEN": "t", "PUBLISH_API": self.gh.base, "MC_BUILDS_REPO": self.gh.repo, "CI_BRANCH": "t239.3", "CI_REF_TYPE": "branch"}
        self.log = []

    def tearDown(self):
        self.gh.stop()
        os.unlink(self.vf)

    def run_s(self, d, sha_=SHA, env=None):
        return S.main(["--dir", d, "--sha", sha_, "--version-file", self.vf], dict(self.env, **(env or {})), log=self.log.append)

    def test_publish_then_unchanged(self):
        self.assertEqual(self.run_s(lane_dir()), 0)
        r = self.gh.releases["sdk-0.4.7-cccccccc"]
        self.assertFalse(r["draft"])
        self.assertTrue(r["prerelease"])
        self.assertEqual(sorted(r["files"]), ["SHA256SUMS", "miniz.u2a", "trx64-sdk-0.4.7.tar.gz", "unzip.u2a"])
        self.assertEqual(S.read_sums(r["files"]["SHA256SUMS"].decode())["trx64-sdk-0.4.7.tar.gz"], sha(b"sdk-one"))
        n = len(self.gh.releases)
        self.log.clear()
        self.assertEqual(self.run_s(lane_dir(), sha_="d" * 40), 0)           # a later commit, the same package
        self.assertEqual(len(self.gh.releases), n, "an unchanged package publishes nothing")
        self.assertTrue(any("unchanged" in l and "sdk-0.4.7-cccccccc" in l for l in self.log), self.log)
        self.assertEqual(self.run_s(lane_dir(tar=b"sdk-two"), sha_="e" * 40), 0)   # the package changed
        self.assertIn("sdk-0.4.7-eeeeeeee", self.gh.releases)
        self.run_s(lane_dir(tar=b"sdk-two"), sha_="f" * 40)                    # equal to the newest only: still nothing
        self.assertNotIn("sdk-0.4.7-ffffffff", self.gh.releases)
        self.run_s(lane_dir(tar=b"sdk-two", miniz=b"mz2"), sha_="9" * 40)      # the same tarball, a changed library: published
        self.assertIn("sdk-0.4.7-99999999", self.gh.releases)

    def test_other_version_and_refs(self):
        self.gh.add_release("sdk-0.4.6-6e280710", {"trx64-sdk-0.4.6.tar.gz": b"sdk-one", "SHA256SUMS": ("%s  trx64-sdk-0.4.6.tar.gz\n" % sha(b"sdk-one")).encode()})
        self.run_s(lane_dir())
        self.assertIn("sdk-0.4.7-cccccccc", self.gh.releases, "another version's release does not count")
        n = len(self.gh.releases)
        for env in ({"CI_BRANCH": "feature"}, {"CI_BRANCH": "v1.0.0", "CI_REF_TYPE": "tag"}):
            self.assertEqual(self.run_s(lane_dir(tar=b"sdk-three"), sha_="a" * 40, env=env), 0)
            self.assertEqual(len(self.gh.releases), n)
        for b in ("integration", "main"):
            self.run_s(lane_dir(tar=b"sdk-" + b.encode()), sha_="1" * 40 if b == "main" else "2" * 40, env={"CI_BRANCH": b})
        self.assertEqual(len(self.gh.releases), n + 2)

    def test_wrong_package_is_refused(self):
        with self.assertRaises(R.PublishError):
            self.run_s(lane_dir(version="0.4.6"))


if __name__ == "__main__":
    unittest.main()
