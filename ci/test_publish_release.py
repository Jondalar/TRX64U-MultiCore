#!/usr/bin/env python3
"""T239.1 D1, S8: ci/publish_release.py on a fake builds repository (ci/fakegh.py). Happy path: a draft, every file uploaded, digests compared, then published as a pre-release.
Refused: a SHA256SUMS that disagrees with the files, a digest GitHub reports differently (the draft is deleted again), a missing token on main, a fork's run, a pull request,
a ref or commit the run is not for. A feature branch publishes nothing and ends green; a flaky upload is retried; a published release of the same files is left alone, one
with other content is an error. Unit and happy path."""
import hashlib
import os
import sys
import tempfile
import time
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from fakegh import FakeGH  # noqa: E402
import publish_release as P  # noqa: E402

SHA = "b" * 40
sha = lambda b: hashlib.sha256(b).hexdigest()
FILES = {"c64_basic.bit": b"bit", "manifest.json": b"{}", "vc20.cor": b"cor", "trx64host_update_x.cfw": b"cfw"}


def make_dir(files=None, sums=None):
    d = tempfile.mkdtemp()
    files = FILES if files is None else files
    for n, b in files.items():
        open(os.path.join(d, n), "wb").write(b)
    text = sums if sums is not None else "".join("%s  %s\n" % (sha(b), n) for n, b in sorted(files.items()))
    open(os.path.join(d, "SHA256SUMS"), "w").write(text)
    return d


class PublishTest(unittest.TestCase):
    def setUp(self):
        self.gh = FakeGH().start()
        self.sleeps = []
        self.real_sleep = P.time.sleep
        P.time.sleep = lambda s: self.sleeps.append(s)
        self.env = {"BUILDS_TOKEN": "tok", "PUBLISH_API": self.gh.base, "MC_BUILDS_REPO": self.gh.repo, "CI_BRANCH": "main", "CI_REF_TYPE": "branch"}

    def tearDown(self):
        P.time.sleep = self.real_sleep
        self.gh.stop()

    def run_p(self, d, tag="main-bbbbbbbb", env=None, ok=True):
        e = dict(self.env, **(env or {}))
        try:
            rc = P.main(["--dir", d, "--tag", tag, "--sha", SHA], e)
        except P.PublishError as ex:
            self.assertFalse(ok, "unexpected refusal: %s" % ex)
            return str(ex)
        self.assertTrue(ok, "a refused call was accepted")
        return rc

    def test_publish(self):
        self.assertEqual(self.run_p(make_dir()), 0)
        r = self.gh.releases["main-bbbbbbbb"]
        self.assertFalse(r["draft"])
        self.assertTrue(r["prerelease"])
        self.assertEqual(r["files"], dict(FILES, SHA256SUMS=r["files"]["SHA256SUMS"]))
        self.assertIn(SHA, r["body"])
        order = [(m, p) for m, p in self.gh.log if m in ("POST", "PATCH")]
        self.assertEqual(order[0], ("POST", "/repos/o/builds/releases"))
        self.assertEqual(order[-1][0], "PATCH")
        self.assertEqual(order[-2][0], "POST")                                              # SHA256SUMS goes last
        self.assertEqual(self.gh.auth[0], "Bearer tok")

    def test_retry(self):
        self.gh.fail_uploads = 2
        self.run_p(make_dir())
        self.assertFalse(self.gh.releases["main-bbbbbbbb"]["draft"])
        self.assertTrue(self.sleeps)

    def test_digest_differs_deletes_the_draft(self):
        self.gh.corrupt_upload = True
        self.run_p(make_dir(), ok=False)
        self.assertNotIn("main-bbbbbbbb", self.gh.releases)

    def test_sums_disagree_before_any_request(self):
        d = make_dir(sums="%s  c64_basic.bit\n" % ("0" * 64))
        self.run_p(d, ok=False)
        d2 = make_dir(sums="".join("%s  %s\n" % ("0" * 64, n) for n in sorted(FILES)))
        self.run_p(d2, ok=False)
        self.assertEqual(self.gh.log, [])

    def test_bad_names(self):
        self.run_p(make_dir({"a b.bit": b"x"}), ok=False)
        self.run_p(make_dir({}), ok=False)

    def test_token_and_ref(self):
        env = dict(self.env)
        del env["BUILDS_TOKEN"]
        self.assertIn("BUILDS_TOKEN", P_main(env, make_dir(), self))
        for ref, kind in (("t239.1", "branch"), ("v1.2.3", "tag")):
            self.assertIn("BUILDS_TOKEN", P_main(dict(env, CI_BRANCH=ref, CI_REF_TYPE=kind), make_dir(), self))
        self.assertEqual(self.run_p(make_dir(), env={"CI_BRANCH": "feature/x", "BUILDS_TOKEN": ""}), 0)       # nothing published, green
        self.assertEqual(self.gh.log, [])
        self.assertEqual(self.run_p(make_dir(), tag="v1.2.3-bbbbbbbb", env={"CI_BRANCH": "v1.2.3", "CI_REF_TYPE": "tag"}), 0)
        self.assertIn("v1.2.3-bbbbbbbb", self.gh.releases)

    def test_actions_guard(self):
        ok = {"GITHUB_ACTIONS": "true", "GITHUB_EVENT_NAME": "push", "GITHUB_REPOSITORY": "Jondalar/TRX64U-MultiCore", "GITHUB_REF": "refs/heads/main", "GITHUB_SHA": SHA}
        for bad in ({"GITHUB_EVENT_NAME": "pull_request"}, {"GITHUB_EVENT_NAME": "workflow_dispatch"}, {"GITHUB_REPOSITORY": "someone/TRX64U-MultiCore"},
                    {"GITHUB_REF": "refs/heads/other"}, {"GITHUB_SHA": "c" * 40}):
            self.run_p(make_dir(), env=dict(ok, **bad), ok=False)
        self.assertEqual(self.gh.log, [])
        self.run_p(make_dir(), env=ok)
        self.assertIn("main-bbbbbbbb", self.gh.releases)

    def test_already_published(self):
        d = make_dir()
        self.run_p(d)
        n = len(self.gh.log)
        self.run_p(d)                                                                       # a re-run of the job: same files, nothing changes
        self.assertEqual([m for m, p in self.gh.log[n:]], ["GET"])
        d2 = make_dir(dict(FILES, **{"vc20.cor": b"other"}))
        self.run_p(d2, ok=False)                                                            # immutable: other content is an error
        self.assertEqual(self.gh.releases["main-bbbbbbbb"]["files"]["vc20.cor"], b"cor")

    def test_leftover_draft(self):
        self.gh.add_release("main-bbbbbbbb", {"old": b"x"}, draft=True)
        old = self.gh.releases["main-bbbbbbbb"]["id"]
        self.run_p(make_dir())
        r = self.gh.releases["main-bbbbbbbb"]
        self.assertNotEqual(r["id"], old)
        self.assertNotIn("old", r["files"])
        self.assertFalse(r["draft"])


def P_main(env, d, t):
    try:
        P.main(["--dir", d, "--tag", "main-bbbbbbbb", "--sha", SHA], env)
    except P.PublishError as ex:
        return str(ex)
    t.fail("accepted")


if __name__ == "__main__":
    unittest.main(verbosity=1)
