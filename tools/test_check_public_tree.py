#!/usr/bin/env python3
"""Unit tests of tools/check_public_tree.py (T239.1 D24): its selftest, tree mode on the git objects of a scratch repository (a renamed
copy of a private file, a planted path, a clean tree) and the pre-push hook run by hand with the arguments git passes."""
import os, shutil, subprocess, sys, tempfile, unittest

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
TOOL = os.path.join(HERE, "check_public_tree.py")
sys.path.insert(0, HERE)
import check_public_tree as cpt


def run(*a, cwd=None, inp=None):
    return subprocess.run(a, cwd=cwd, input=inp, capture_output=True, text=True)


class T(unittest.TestCase):
    def test_selftest(self):
        r = run(sys.executable, "-I", TOOL, "--selftest")
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)

    def test_private_list_matches_generator_shape(self):
        files, wins = cpt.load_private()
        self.assertEqual(sum(len(v) for v in files.values()), 20)
        self.assertTrue(all(len(sha) == 64 for sha, _, _ in sum(files.values(), [])))
        self.assertGreaterEqual(len(wins), 20)

    def test_scratch_repo_and_hook(self):
        td = tempfile.mkdtemp()
        try:
            g = lambda *a: run("git", "-c", "user.name=t", "-c", "user.email=t@example.org", *a, cwd=td)
            os.makedirs(os.path.join(td, "tools")); os.makedirs(os.path.join(td, "ci")); os.makedirs(os.path.join(td, ".githooks"))
            for f in ("tools/check_public_tree.py", ".githooks/pre-push"):
                shutil.copy(os.path.join(ROOT, f), os.path.join(td, f))
            open(os.path.join(td, "ci/private.txt"), "w").write(open(os.path.join(ROOT, "ci/private.txt")).read())
            open(os.path.join(td, "README.md"), "w").write("clean\n")
            g("init", "-q"); g("add", "."); g("commit", "-q", "-m", "clean")
            clean = g("rev-parse", "HEAD").stdout.strip()
            zero = "0" * 40
            hook = lambda sha: run("bash", os.path.join(td, ".githooks/pre-push"), "origin", "x", cwd=td,
                                   inp="refs/heads/b %s refs/heads/b %s\n" % (sha, zero))
            h = hook(clean); self.assertEqual(h.returncode, 0, h.stdout + h.stderr)
            # a renamed copy of a private file, found by its size and SHA-256 (the file is rebuilt only if a copy is at hand)
            src = os.environ.get("T2391_PRIVATE_SRC")
            if src and os.path.exists(os.path.join(src, "roms/kernal.901227-03.bin")):
                shutil.copy(os.path.join(src, "roms/kernal.901227-03.bin"), os.path.join(td, "docs.dat"))
                g("add", "docs.dat"); g("commit", "-q", "-m", "renamed rom")
                r = hook(g("rev-parse", "HEAD").stdout.strip())
                self.assertEqual(r.returncode, 1); self.assertIn("copy of the private file", r.stdout)
                g("reset", "-q", "--hard", clean)
            os.makedirs(os.path.join(td, "cores/a/roms")); open(os.path.join(td, "cores/a/roms/x.rom"), "w").write("x")
            g("add", "-f", "."); g("commit", "-q", "-m", "planted path")
            r = hook(g("rev-parse", "HEAD").stdout.strip())
            self.assertEqual(r.returncode, 1); self.assertIn("PUBLIC-TREE: cores/a/roms/x.rom", r.stdout)
            g("reset", "-q", "--hard", clean)
            open(os.path.join(td, "m"), "w").write("m"); g("add", "m"); g("commit", "-q", "-m", "x\n\nClaude-Session: abc")
            r = hook(g("rev-parse", "HEAD").stdout.strip())
            self.assertEqual(r.returncode, 1); self.assertIn("claude trailer", r.stdout)
        finally:
            shutil.rmtree(td, ignore_errors=True)


if __name__ == "__main__":
    unittest.main(verbosity=2)
