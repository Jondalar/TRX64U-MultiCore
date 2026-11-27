#!/usr/bin/env python3
"""T239.1 D19, S5, S8: ci/fetch_bits.sh on a fake builds repository (ci/fakegh.py). Happy path: the entries are restored the way the builder's cache holds them
(DONE last, reports unpacked, no static_routed.dcp) and bit-releases.json records the releases. The refusals: a draft, a missing release, an asset whose digest GitHub lists
differently, SHA256SUMS that disagrees with the assets, a reports.tar.gz that leaves its directory, a manifest that does not match, a host.bit that is not the host_sha, a core routed
against another host. A refused entry leaves nothing in the cache. D40: a second fetch takes every entry from the cache without a request, an entry
whose file changed is fetched again, a shorter list removes the other entries, and --names prints the list. Unit and happy path."""
import hashlib
import io
import json
import os
import shutil
import subprocess
import sys
import tarfile
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from fakegh import FakeGH  # noqa: E402

REF = [l.split()[1] for l in open(os.path.join(os.path.dirname(HERE), "host", "layers.txt")) if l.startswith("reference:")][0]
K = lambda c: "%016x" % c
H, A, R, V, Z, M = "host-" + K(1), "aux-" + K(2), "core-%s-%s" % (REF, K(3)), "core-vc20-" + K(4), "core-zx_next-" + K(5), "mono-amiga1200-" + K(6)
sha = lambda b: hashlib.sha256(b).hexdigest()


def tar_gz(members):
    buf = io.BytesIO()
    with tarfile.open(fileobj=buf, mode="w:gz") as t:
        for name, data, kind in members:
            ti = tarfile.TarInfo(name)
            ti.size = len(data) if kind == "file" else 0
            if kind == "symlink":
                ti.type, ti.linkname = tarfile.SYMTYPE, "/etc/passwd"
            t.addfile(ti, io.BytesIO(data) if kind == "file" else None)
    return buf.getvalue()


def release_files(plain, reports, manifest=None, tar=None):
    """The assets the builder would publish for an entry: plain files, reports.tar.gz, manifest.json, SHA256SUMS."""
    files = dict(plain)
    if manifest is not None:
        m = dict(manifest)
        m["files"] = {n: sha(b) for n, b in sorted({**plain, **reports}.items()) if n != "manifest.json"}
        m["files"].update(manifest.get("extra_files", {}))
        m.pop("extra_files", None)
        files["manifest.json"] = json.dumps(m).encode()
    files["reports.tar.gz"] = tar if tar is not None else tar_gz([(n, b, "file") for n, b in reports.items()])
    files["SHA256SUMS"] = "".join("%s  %s\n" % (sha(b), n) for n, b in sorted(files.items())).encode()
    return files


class FetchTest(unittest.TestCase):
    def setUp(self):
        self.gh = FakeGH().start()
        self.tmp = tempfile.mkdtemp()
        self.cache, self.hand = os.path.join(self.tmp, "bit"), os.path.join(self.tmp, "handoff")
        self.host_bit = b"host bitstream"
        self.hsha = sha(self.host_bit)
        self.names = [H, A, R, V, Z, M]
        open(os.path.join(self.tmp, "keys"), "w").write("\n".join(self.names) + "\n")
        self.publish_all()

    def tearDown(self):
        self.gh.stop()
        shutil.rmtree(self.tmp, ignore_errors=True)

    def publish_all(self):
        host = release_files({"host.bit": self.host_bit, "key": b"k", "from": b"built abc"}, {"host.steps.txt": b"steps", "host.timing.rpt": b"timing"},
                             {"host_sha": self.hsha, "extra_files": {"static_routed.dcp": "f" * 64}})
        self.gh.add_release("bit-" + H, host)
        self.gh.add_release("bit-" + A, release_files({"ddr2_test.bit": b"aux"}, {}, {"wns_ns": "0.1"}))
        for n, c in ((R, REF), (V, "vc20"), (Z, "zx_next")):
            self.gh.add_release("bit-" + n, release_files({c + "_partial.bit": b"p " + c.encode(), c + "_full.bit": b"f " + c.encode()}, {c + ".steps.txt": b"s"}, {"host_sha": self.hsha}))
        self.gh.add_release("bit-" + M, release_files({"amiga1200_mono.bit": b"mono"}, {}, {"wns_ns": "0.1"}))

    def fetch(self, target=None, ok=True, names_only=False):
        env = {"PATH": os.environ["PATH"], "CI_BIT_CACHE": self.cache, "CI_HANDOFF": self.hand, "FETCH_KEYS": os.path.join(self.tmp, "keys"), "MC_BUILDS_REPO": self.gh.repo, "BUILDS_API": self.gh.base,
               "FETCH_FILLERS": "zx_next"}
        r = subprocess.run([os.path.join(HERE, "fetch_bits.sh")] + (["--names"] if names_only else []) + ([target] if target else []), env=env, capture_output=True, text=True)
        self.assertEqual(r.returncode == 0, ok, r.stdout + r.stderr)
        return r

    def entries(self):
        return sorted(n for n in os.listdir(self.cache) if not n.startswith(".")) if os.path.isdir(self.cache) else []

    def test_cache_reused(self):
        self.fetch()
        first = open(os.path.join(self.hand, "bit-releases.json")).read()
        self.gh.log.clear()
        r = self.fetch()
        self.assertEqual(self.gh.log, [], "a cached entry needs no request")
        self.assertIn("(%d from the cache, 0 fetched)" % len(self.names), r.stdout)
        self.assertEqual(open(os.path.join(self.hand, "bit-releases.json")).read(), first)

    def test_cache_changed_file_fetched_again(self):
        self.fetch()
        open(os.path.join(self.cache, V, "vc20_partial.bit"), "wb").write(b"changed")
        self.gh.log.clear()
        r = self.fetch()
        self.assertIn("fetched bit-" + V, r.stdout)
        self.assertEqual({p for _, p in self.gh.log if "/releases/tags/" in p}, {"/repos/%s/releases/tags/bit-%s" % (self.gh.repo, V)})
        self.assertEqual(open(os.path.join(self.cache, V, "vc20_partial.bit"), "rb").read(), b"p vc20")

    def test_cache_pruned_to_the_list(self):
        self.fetch()
        open(os.path.join(self.tmp, "keys"), "w").write("\n".join([H, R, V]) + "\n")
        self.fetch()
        self.assertEqual(self.entries(), sorted([H, R, V]))
        self.assertEqual(sorted(os.listdir(os.path.join(self.cache, ".releases"))), sorted(n + ".json" for n in (H, R, V)))

    def test_names(self):
        self.assertEqual(self.fetch(names_only=True).stdout.split(), self.names)
        self.assertEqual(self.fetch("core:vc20", names_only=True).stdout.split(), [H, R, V])
        self.assertEqual(self.gh.log, [])
        self.assertEqual(self.entries(), [])

    def test_all_entries(self):
        self.fetch()
        self.assertTrue(os.path.exists(os.path.join(self.cache, ".fetched")))      # ci/lib.sh cache_gc_if_free reads it
        self.assertEqual(self.entries(), sorted(self.names))
        d = os.path.join(self.cache, H)
        self.assertEqual(sorted(os.listdir(d)), ["DONE", "from", "host.bit", "host.steps.txt", "host.timing.rpt", "key", "manifest.json"])
        self.assertEqual(open(os.path.join(d, "host.bit"), "rb").read(), self.host_bit)
        self.assertEqual(json.load(open(os.path.join(d, "manifest.json")))["host_sha"], self.hsha)
        rec = json.load(open(os.path.join(self.hand, "bit-releases.json")))
        self.assertEqual(sorted(rec["entries"]), sorted(self.names))
        self.assertEqual(rec["entries"][H]["tag"], "bit-" + H)
        self.assertEqual(rec["entries"][H]["assets"]["host.bit"], self.hsha)
        self.assertEqual(rec["entries"][H]["sha256sums"], sha(self.gh.releases["bit-" + H]["files"]["SHA256SUMS"]))
        self.assertFalse([n for n in os.listdir(self.cache) if ".tmp." in n])

    def test_targets(self):
        self.fetch("core:vc20")
        self.assertEqual(self.entries(), sorted([H, R, V]))
        shutil.rmtree(self.cache)
        self.fetch("large:amiga1200")
        self.assertEqual(self.entries(), [M])
        self.fetch("core:nosuch", ok=False)
        shutil.rmtree(self.cache)
        self.fetch("host")                                                               # T265 P5: host, aux, the reference core and the flash filler (here zx_next)
        self.assertEqual(self.entries(), sorted(n for n in self.names if n in (H, A, R, Z)))
        self.assertEqual(self.fetch("host", names_only=True).stdout.split(), [n for n in self.names if n in (H, A, R, Z)])

    def refused(self, entry_name=None):
        self.fetch(ok=False)
        self.assertEqual([n for n in self.entries() if ".tmp." in n], [])
        if entry_name:
            self.assertNotIn(entry_name, self.entries())

    def test_draft_and_missing(self):
        self.gh.releases["bit-" + V]["draft"] = True
        self.refused(V)
        self.gh.releases["bit-" + V]["draft"] = False
        del self.gh.releases["bit-" + Z]
        self.refused(Z)

    def test_digest_differs(self):
        self.gh.releases["bit-" + V]["digests"]["vc20_partial.bit"] = "0" * 64
        self.refused(V)

    def test_no_digest_listed(self):
        self.gh.releases["bit-" + V]["digests"]["vc20_partial.bit"] = ""
        self.refused(V)

    def test_sums_disagree(self):
        r = self.gh.releases["bit-" + V]
        r["files"]["SHA256SUMS"] = r["files"]["SHA256SUMS"].replace(b"vc20_full.bit", b"other.bit")
        r["digests"]["SHA256SUMS"] = sha(r["files"]["SHA256SUMS"])
        self.refused(V)

    def test_sums_asset_differs_from_listed_digest(self):
        self.gh.releases["bit-" + V]["digests"]["SHA256SUMS"] = "1" * 64
        self.refused(V)

    def test_reports_leave_directory(self):
        for bad in ((("../escape", b"x", "file"),), (("sub/x.rpt", b"x", "file"),), (("link.rpt", b"", "symlink"),)):
            self.gh.releases["bit-" + V] = {}
            self.gh.releases.pop("bit-" + V)
            self.gh.add_release("bit-" + V, release_files({"vc20_partial.bit": b"p"}, {}, {"host_sha": self.hsha}, tar=tar_gz(list(bad))))
            self.refused(V)
            self.assertFalse(os.path.exists(os.path.join(self.tmp, "escape")))

    def test_manifest_mismatch(self):
        plain = {"vc20_partial.bit": b"p"}
        self.gh.releases.pop("bit-" + V)
        self.gh.add_release("bit-" + V, release_files(plain, {}, {"host_sha": self.hsha, "extra_files": {"vc20_partial.bit": "2" * 64}}))
        self.refused(V)
        self.gh.releases.pop("bit-" + V)
        self.gh.add_release("bit-" + V, release_files(plain, {}, {"host_sha": self.hsha, "extra_files": {"vc20_full.bit": "2" * 64}}))   # listed, not published, not a .dcp
        self.refused(V)

    def test_host_bit_is_not_host_sha(self):
        self.gh.releases.pop("bit-" + H)
        self.gh.add_release("bit-" + H, release_files({"host.bit": self.host_bit}, {}, {"host_sha": "3" * 64}))
        self.refused(H)

    def test_core_against_another_host(self):
        self.gh.releases.pop("bit-" + Z)
        self.gh.add_release("bit-" + Z, release_files({"zx_next_partial.bit": b"p"}, {}, {"host_sha": "4" * 64}))
        self.refused()
        self.assertEqual(self.entries(), [])
        self.fetch("core:vc20")                                                       # the other cores are not in this fetch


if __name__ == "__main__":
    unittest.main(verbosity=1)
