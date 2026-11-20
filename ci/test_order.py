#!/usr/bin/env python3
"""T239.1 D13, S15: ci/order.py on a fake builds repository (ci/fakegh.py): the work order names exactly the needed entries whose bit-<entry> release is missing or a draft,
an application-only run orders nothing, a feature branch's cor child orders only its own entries, and a bad plan or an unreadable release is an error. Unit and happy path."""
import json
import os
import subprocess
import sys
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from fakegh import FakeGH  # noqa: E402

REF = None
for line in open(os.path.join(os.path.dirname(HERE), "host", "layers.txt")):
    if line.startswith("reference:"):
        REF = line.split()[1]
K = lambda c: "%016x" % c
NAMES = ["host-" + K(1), "aux-" + K(2), "core-%s-%s" % (REF, K(3)), "core-vc20-" + K(4), "core-zx_next-" + K(5), "mono-amiga1200-" + K(6)]


class OrderTest(unittest.TestCase):
    def setUp(self):
        self.gh = FakeGH().start()
        self.tmp = tempfile.mkdtemp()
        self.keys = os.path.join(self.tmp, "keys")
        open(self.keys, "w").write("\n".join(NAMES) + "\n")
        self.plan = os.path.join(self.tmp, "plan.json")
        json.dump({"version": 1}, open(self.plan, "w"))

    def tearDown(self):
        self.gh.stop()

    def run_order(self, children=None, cors=None, plan=None, expect_rc=0):
        env = {"PATH": os.environ["PATH"], "PLAN_JSON_FILE": plan or self.plan, "WORK_ORDER_OUT": os.path.join(self.tmp, "wo.json"), "GITHUB_OUTPUT": os.path.join(self.tmp, "out"),
               "ORDER_KEYS": self.keys, "MC_BUILDS_REPO": self.gh.repo, "BUILDS_API": self.gh.base, "ORDER_SHA": "a" * 40, "GH_TOKEN": "tok"}
        if children is not None:
            env["CHILDREN"] = json.dumps(children)
        if cors is not None:
            env["COR_TARGETS"] = json.dumps(cors)
        open(env["GITHUB_OUTPUT"], "w").close()
        r = subprocess.run([sys.executable, os.path.join(HERE, "order.py")], env=env, capture_output=True, text=True)
        self.assertEqual(r.returncode, expect_rc, r.stdout + r.stderr)
        if expect_rc:
            return r
        return json.load(open(env["WORK_ORDER_OUT"])), open(env["GITHUB_OUTPUT"]).read().strip()

    def test_all_missing_then_some_published(self):
        wo, out = self.run_order(children=["apptests", "package"])
        self.assertEqual(wo["entries"], NAMES)
        self.assertEqual(wo["sha"], "a" * 40)
        self.assertEqual(wo["lanes"], ["host", "core:vc20", "core:zx_next", "large:amiga1200"])
        self.assertEqual(out, "missing=true")
        for n in NAMES[:3]:
            self.gh.add_release("bit-" + n, {"SHA256SUMS": b"x"})
        self.gh.add_release("bit-" + NAMES[3], {"SHA256SUMS": b"x"}, draft=True)        # a draft is not published
        wo, out = self.run_order(children=["package"])
        self.assertEqual(wo["entries"], NAMES[3:])
        self.assertEqual(wo["lanes"], ["core:vc20", "core:zx_next", "large:amiga1200"])
        self.assertIn("Bearer tok", self.gh.auth)

    def test_nothing_missing_is_app_only(self):                                          # S15
        for n in NAMES:
            self.gh.add_release("bit-" + n, {"SHA256SUMS": b"x"})
        wo, out = self.run_order(children=["apptests", "images", "package", "update"])
        self.assertEqual((wo["entries"], wo["lanes"], out), ([], [], "missing=false"))
        self.assertEqual(json.load(open(os.path.join(self.tmp, "wo.json")))["version"], 1)

    def test_no_children_orders_nothing(self):
        wo, out = self.run_order(children=[])
        self.assertEqual((wo["entries"], out), ([], "missing=false"))
        self.assertEqual([m for m, p in self.gh.log], [])                                 # nothing was even asked

    def test_cor_branch(self):
        wo, _ = self.run_order(children=["apptests", "images", "cor"], cors=["core:vc20", "large:amiga1200"])
        self.assertEqual(wo["entries"], [NAMES[0], NAMES[2], NAMES[3], NAMES[5]])
        self.assertEqual(wo["lanes"], ["host", "core:vc20", "large:amiga1200"])

    def test_unknown_children_order_everything(self):
        wo, _ = self.run_order()
        self.assertEqual(wo["entries"], NAMES)

    def test_bad_plan_and_unreadable_release(self):
        bad = os.path.join(self.tmp, "bad.json")
        open(bad, "w").write("{}")
        self.run_order(children=["package"], plan=bad, expect_rc=1)
        self.gh.releases = None                                                          # the fake answers 500 for this
        self.run_order(children=["package"], expect_rc=1)


if __name__ == "__main__":
    unittest.main(verbosity=1)
