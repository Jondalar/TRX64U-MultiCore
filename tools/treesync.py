#!/usr/bin/env python3
"""T174 D1: keep a persistent build tree equal to a working tree, touching only the files whose content changed.

A fresh `git archive` / clone gives every file a new mtime, so `make` rebuilds everything. This copies a file only when its
sha256 differs from the one the previous sync recorded, so the mtimes of the unchanged files stay and `make` rebuilds only what
changed. There is no rsync on the builder (nor in the build images), so the diff is made here against a manifest the previous
sync left in the destination (.treesync.manifest: sha256, mode, path; a symlink as "L:<target>").

  tools/treesync.py SRC DEST                       local copy (CI: the job's checkout into /cache/multicore/build/<branch>)
  tools/treesync.py SRC DEST --host builder        DEST is a directory on that ssh host (the dev loop from the Mac)
  tools/treesync.py ... --clean                    forget the manifest and empty DEST first (a build from scratch)

What is synced: every file git lists in SRC that is tracked or untracked and not ignored (git ls-files -co --exclude-standard), so a
working tree with uncommitted edits is what lands. Build outputs are ignored files: they are neither copied nor deleted, they live
on in DEST. A file removed from SRC is removed from DEST (only files the manifest knows). `.git` is not copied; the build reads
the version from the checkout it was started for (GIT_DIR, ci/trxapps.sh).
"""
import argparse
import hashlib
import io
import os
import shlex
import shutil
import subprocess
import sys
import tarfile

MANIFEST = ".treesync.manifest"


def tracked(src):
    out = subprocess.run(["git", "-C", src, "ls-files", "-co", "--exclude-standard", "-z"], check=True, capture_output=True).stdout
    return sorted(p for p in out.decode("utf-8", "surrogateescape").split("\0") if p)


def describe(src, rel):
    """(token, mode) for a path of SRC: sha256 of a file, 'L:<target>' for a symlink, None when it is gone or a submodule dir."""
    p = os.path.join(src, rel)
    if os.path.islink(p):
        return "L:" + os.readlink(p), 0
    if not os.path.isfile(p):
        return None
    h = hashlib.sha256()
    with open(p, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest(), os.stat(p).st_mode & 0o777


def build_manifest(src):
    m = {}
    for rel in tracked(src):
        d = describe(src, rel)
        if d is not None:
            m[rel] = d
    return m


def parse(text):
    m = {}
    for line in text.splitlines():
        parts = line.split("\t", 2)
        if len(parts) == 3:
            m[parts[2]] = (parts[0], int(parts[1], 8))
    return m


def dump(m):
    return "".join("%s\t%o\t%s\n" % (tok, mode, rel) for rel, (tok, mode) in sorted(m.items()))


class Local:
    def __init__(self, root):
        self.root = root

    def read_manifest(self):
        try:
            with open(os.path.join(self.root, MANIFEST), encoding="utf-8", errors="surrogateescape") as f:
                return parse(f.read())
        except OSError:
            return {}

    def apply(self, src, changed, removed, manifest, clean):
        if clean and os.path.isdir(self.root):
            shutil.rmtree(self.root)
        os.makedirs(self.root, exist_ok=True)
        for rel in removed:
            try:
                os.remove(os.path.join(self.root, rel))
            except OSError:
                pass
        for rel in changed:
            s, d = os.path.join(src, rel), os.path.join(self.root, rel)
            os.makedirs(os.path.dirname(d), exist_ok=True)
            if os.path.lexists(d):
                os.remove(d)
            if os.path.islink(s):
                os.symlink(os.readlink(s), d)
            else:
                shutil.copyfile(s, d)
                os.chmod(d, manifest[rel][1])
        with open(os.path.join(self.root, MANIFEST), "w", encoding="utf-8", errors="surrogateescape") as f:
            f.write(dump(manifest))


class Remote:
    def __init__(self, host, root):
        self.host, self.root = host, root

    def ssh(self, cmd, data=None):
        return subprocess.run(["ssh", self.host, cmd], input=data, check=True, capture_output=True).stdout

    def read_manifest(self):
        out = self.ssh("cat %s 2>/dev/null || true" % shlex.quote(os.path.join(self.root, MANIFEST)))
        return parse(out.decode("utf-8", "surrogateescape"))

    def apply(self, src, changed, removed, manifest, clean):
        q = shlex.quote(self.root)
        if clean:
            self.ssh("rm -rf %s" % q)
        self.ssh("mkdir -p %s" % q)
        buf = io.BytesIO()
        with tarfile.open(fileobj=buf, mode="w") as tar:
            for rel in changed:
                s = os.path.join(src, rel)
                ti = tar.gettarinfo(s, arcname=rel)
                ti.uid = ti.gid = 0
                ti.uname = ti.gname = ""
                if ti.issym():
                    tar.addfile(ti)
                else:
                    with open(s, "rb") as f:
                        tar.addfile(ti, f)
        # the removed list and the tar are two ssh calls (one stdin each); tar --overwrite replaces what changed
        if removed:
            self.ssh("cd %s && xargs -0 rm -f --" % q, ("\0".join(removed) + "\0").encode("utf-8", "surrogateescape"))
        if changed:
            self.ssh("cd %s && tar -x --no-same-owner --overwrite -f -" % q, buf.getvalue())
        self.ssh("cat > %s" % shlex.quote(os.path.join(self.root, MANIFEST)), dump(manifest).encode("utf-8", "surrogateescape"))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("src")
    ap.add_argument("dest")
    ap.add_argument("--host", help="ssh host that holds DEST")
    ap.add_argument("--clean", action="store_true", help="empty DEST first")
    a = ap.parse_args()
    src = os.path.abspath(a.src)
    dst = Remote(a.host, a.dest) if a.host else Local(os.path.abspath(a.dest))
    new = build_manifest(src)
    old = {} if a.clean else dst.read_manifest()
    changed = [r for r, v in new.items() if old.get(r) != v]
    removed = [r for r in old if r not in new]
    dst.apply(src, changed, removed, new, a.clean)
    print("treesync: %d file(s), %d changed, %d removed -> %s%s" % (len(new), len(changed), len(removed), (a.host + ":") if a.host else "", a.dest))


if __name__ == "__main__":
    sys.exit(main())
