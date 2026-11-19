#!/usr/bin/env python3
"""Public-tree check (T239.1 D24, S1, D6). GitHub cannot refuse a push by path or content and a pushed file stays in the history,
so this runs before every push (.githooks/pre-push) and in CI on every push and pull request.

  tools/check_public_tree.py                     tree mode: every tracked file of the working tree
  tools/check_public_tree.py --rev REV           tree mode on the commit REV (what the pre-push hook checks)
  tools/check_public_tree.py --messages "RANGE"   message mode: the commit messages of `git rev-list RANGE` (one quoted argument) (internal hosts and paths;
                                                 with --trailers also the Claude attribution lines the maintainer's sessions never write)
  tools/check_public_tree.py --files PATH...     files mode: package directories, .cor files and the members of .zip and .tar.gz
                                                 against the windows of ci/private.txt (--allow-trx: the fourth exception of CLAUDE.md,
                                                 the BASIC ROM and module.bin inside trx64g.cor and trx64u.cor only)
  tools/check_public_tree.py --selftest          plants one hit per rule; a rule that does not fire fails the selftest
  tools/check_public_tree.py --gen-private SRC   write ci/private.txt from the private files of the checkout SRC (their bytes stay there)

Tree mode refuses a path of the private list (ROM images, bench media, licence folder, MOD player image, session material outside the
D5 whitelist), a file of a known size with a known SHA-256 under any name, and in every blob without a NUL in its first 8000 bytes an
internal host, the maintainer's home path, the Gitea host, the builder login or a link to the claude web host (D6). Exit 1 on any hit,
every hit printed."""
import argparse, hashlib, io, os, random, re, shlex, subprocess, sys, tarfile, tempfile, zipfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PRIVATE = os.path.join(ROOT, "ci", "private.txt")

# ---- path rules (the export's rules of tools/github_snapshot.py, D5 whitelist) ----
DROP_RE = re.compile(r"^(roms/.*\.bin|cores/[^/]+/roms/.*\.rom|cores/[^/]+/sim/media/.*)$")
ROM_EXT = re.compile(r"\.(rom|img|adf|adz|d64|d71|d81|g64|crt|tap|tzx|t64|dsk|hdf|nib|po)$", re.I)
MODPLAYER = "release/imports/trx64u_files/modplayer/module.bin"
DROP_DIRS = ("<removed>/",)
DROP_FILES = ("<removed>", "<removed>", "<removed>", MODPLAYER)
CLAUDE_PUBLIC = ("CLAUDE.md", ".claude/agents/", ".claude/skills/", ".claude/trx64-site/")     # D5: everything else under .claude/ is private

# ---- text rules (D6); the patterns are written so that this file does not match itself ----
TEXT_RULES = (
    ("internal host", re.compile(rb"<board-ip>|<removed>|<removed>|<removed>", re.I)),
    ("home path", re.compile(rb"~/|<removed>", re.I)),
    ("gitea host", re.compile(rb"<gitea>", re.I)),
    ("builder login", re.compile(rb"<removed>", re.I)),
    ("claude link", re.compile(rb"claude\.ai/", re.I)),
)
TRAILER = ("claude trailer", re.compile(rb"^(co-authored-by:\s*claude|claude-session:)", re.I | re.M))


def path_hits(path):
    hits = []
    if DROP_RE.match(path) or ROM_EXT.search(path):
        hits.append("private file type (ROM image or bench medium)")
    if path in DROP_FILES or any(path.startswith(d) for d in DROP_DIRS):
        hits.append("private file (licence folder, T120, MOD player image)")
    if path.startswith(".claude/") and not any(path.startswith(w) for w in CLAUDE_PUBLIC):
        hits.append("session material outside the D5 whitelist")
    for name, rx in TEXT_RULES:
        if rx.search(path.encode()):
            hits.append("%s in the path" % name)
    return hits


def text_hits(data, rules=TEXT_RULES):
    if b"\0" in data[:8000]:
        return []
    return [name for name, rx in rules if rx.search(data)]


# ---- ci/private.txt: size + SHA-256 per private binary, windows of 64 bytes (SHA-256 plus an 8-byte anchor to find them) ----
WIN, ANCHOR, NWIN = 64, 8, 4


def load_private(path=PRIVATE):
    files, wins = {}, []          # files: size -> [(sha, name, trx)], wins: [(sha, anchor bytes, name, trx)]
    if not os.path.exists(path):
        return files, wins
    for ln in open(path):
        f = ln.split()
        if not f or f[0].startswith("#"):
            continue
        if f[0] == "file":
            files.setdefault(int(f[1]), []).append((f[2], f[3], len(f) > 4 and f[4] == "trx"))
        elif f[0] == "win":
            wins.append((f[1], bytes.fromhex(f[2]), f[3], len(f) > 4 and f[4] == "trx"))
        else:
            raise SystemExit("%s: bad line: %s" % (path, ln.strip()))
    return files, wins


def windows_of(data):
    """NWIN windows of 64 bytes spread over the file: varied (>= 16 distinct bytes, >= 4 in the anchor) and with an anchor that occurs once in the file."""
    out, n = [], len(data)
    if n < WIN:
        return out
    for k in range(NWIN):
        start = (n - WIN) * (2 * k + 1) // (2 * NWIN)
        for off in range(start, n - WIN + 1):
            w = data[off:off + WIN]
            if len(set(w)) >= 16 and len(set(w[:ANCHOR])) >= 4 and data.count(w[:ANCHOR]) == 1 and all(off != o for o, _ in out):
                out.append((off, w))
                break
    return out


def private_rows(src):
    ls = subprocess.run(["git", "-C", src, "ls-files", "-z"], capture_output=True, check=True).stdout.decode().split("\0")
    return [(p, open(os.path.join(src, p), "rb").read()) for p in sorted(x for x in ls if x)
            if DROP_RE.match(p) or ROM_EXT.search(p) or p == MODPLAYER]


def gen_private(src, dest):
    rows = private_rows(src)
    out = ["# Private binary files of the Gitea checkout: size, SHA-256 and windows. Generated by tools/check_public_tree.py --gen-private;",
           "# no file bytes here, only 64-byte window hashes with an %d-byte anchor to find them. trx = may sit in trx64g.cor and trx64u.cor"
           " (CLAUDE.md, fourth exception, --allow-trx)." % ANCHOR]
    for p, d in rows:
        trx = " trx" if p.endswith(("basic.901226-01.bin", "module.bin")) else ""
        out.append("file %d %s %s%s" % (len(d), hashlib.sha256(d).hexdigest(), p, trx))
        for off, w in windows_of(d):
            out.append("win %s %s %s%s" % (hashlib.sha256(w).hexdigest(), w[:ANCHOR].hex(), p, trx))
    open(dest, "w").write("\n".join(out) + "\n")
    return len(rows)


def known_file(data, files):
    return [(name, trx) for sha, name, trx in files.get(len(data), []) if hashlib.sha256(data).hexdigest() == sha]


def window_hits(data, wins, allow_trx=False, base=""):
    hits = []
    trx_ok = allow_trx and os.path.basename(base) in ("trx64g.cor", "trx64u.cor")
    for sha, anchor, name, trx in wins:
        if trx_ok and trx:
            continue
        i = data.find(anchor)
        while i >= 0:
            if hashlib.sha256(data[i:i + WIN]).hexdigest() == sha:
                hits.append("%s at offset %d" % (name, i))
                break
            i = data.find(anchor, i + 1)
    return hits


# ---- tree mode ----
def tracked(rev):
    if rev:
        out = subprocess.run(["git", "-C", ROOT, "ls-tree", "-r", "-z", rev], capture_output=True, check=True).stdout.decode()
        return [(r.split("\t", 1)[1], r.split()[2]) for r in out.split("\0") if r and r.split()[1] == "blob"]
    out = subprocess.run(["git", "-C", ROOT, "ls-files", "-z"], capture_output=True, check=True).stdout.decode()
    return [(p, None) for p in out.split("\0") if p]


def tree_items(rev):
    """(path, bytes) of every tracked file; with --rev from the git objects through one cat-file process."""
    ents = tracked(rev)
    if not rev:
        for p, _ in ents:
            fp = os.path.join(ROOT, p)
            if os.path.isfile(fp) and not os.path.islink(fp):
                yield p, open(fp, "rb").read()
        return
    pr = subprocess.Popen(["git", "-C", ROOT, "cat-file", "--batch"], stdin=subprocess.PIPE, stdout=subprocess.PIPE)
    for p, sha in ents:
        pr.stdin.write(sha.encode() + b"\n"); pr.stdin.flush()
        h = pr.stdout.readline().split()
        d = pr.stdout.read(int(h[2])); pr.stdout.read(1)
        yield p, d
    pr.stdin.close(); pr.wait()


def check_items(items, files, wins=()):
    bad = []
    for p, d in items:
        bad += ["%s: %s" % (p, h) for h in path_hits(p)]
        bad += ["%s: %s" % (p, h) for h in text_hits(d)]
        bad += ["%s: is a copy of the private file %s (size and SHA-256)" % (p, n) for n, _ in known_file(d, files)]
        bad += ["%s: holds a window of %s" % (p, h) for h in (window_hits(d, wins) if wins else [])]
    return bad


# ---- message mode ----
def check_messages(rng, trailers, root=None):
    root = root or ROOT
    revs = subprocess.run(["git", "-C", root, "rev-list"] + rng, capture_output=True, check=True).stdout.decode().split()
    rules = TEXT_RULES + ((TRAILER,) if trailers else ())
    bad = []
    for r in revs:
        m = subprocess.run(["git", "-C", root, "log", "-1", "--format=%an <%ae>%n%B", r], capture_output=True, check=True).stdout
        bad += ["commit %s: %s in the message" % (r[:10], name) for name, rx in rules if rx.search(m)]
    return bad, len(revs)


# ---- files mode: directories, .cor, .zip, .tar.gz members ----
def members(path):
    low = path.lower()
    if os.path.isdir(path):
        for d, _, fs in os.walk(path):
            for f in sorted(fs):
                yield from members(os.path.join(d, f))
    elif low.endswith(".zip"):
        with zipfile.ZipFile(path) as z:
            for i in z.infolist():
                if not i.is_dir():
                    yield "%s!%s" % (path, i.filename), z.read(i)
    elif low.endswith((".tar.gz", ".tgz")):
        with tarfile.open(path, "r:gz") as t:
            for m in t:
                if m.isfile():
                    yield "%s!%s" % (path, m.name), t.extractfile(m).read()
    else:
        yield path, open(path, "rb").read()


def check_files(paths, files, wins, allow_trx):
    bad = []
    for p in paths:
        for name, d in members(p):
            leaf = name.split("!")[-1]
            ok = allow_trx and os.path.basename(leaf) in ("trx64g.cor", "trx64u.cor")
            bad += ["%s: is a copy of the private file %s" % (name, n) for n, trx in known_file(d, files) if not (ok and trx)]
            bad += ["%s: holds %s" % (name, h) for h in window_hits(d, wins, allow_trx, leaf)]
            bad += ["%s: %s" % (name, h) for h in text_hits(d)]
    return bad


# ---- selftest ----
def selftest():
    j = lambda *a: "".join(a)
    host, home, gitea = j("<board-ip>"), j("~/x"), j("<gitea>")
    login, link = j("<removed>"), j("https://claude", ".ai/artifact/x")
    rom = random.Random(7).randbytes(8192)
    sha = hashlib.sha256(rom).hexdigest()
    win = windows_of(rom)
    errs = []

    def want(label, cond):
        if not cond:
            errs.append(label)

    want("windows_of gives %d windows" % NWIN, len(win) == NWIN)
    files = {len(rom): [(sha, "roms/fake.bin", False)]}
    wins = [(hashlib.sha256(w).hexdigest(), w[:ANCHOR], "roms/fake.bin", False) for _, w in win]
    trxfiles = {len(rom): [(sha, "roms/fake.bin", True)]}
    trxwins = [(a, b, c, True) for a, b, c, _ in wins]

    clean = [("README.md", b"hello\n"), ("coreapp/a.cc", b"int x;\n"), (".claude/skills/a/SKILL.md", b"x\n"), ("CLAUDE.md", b"rules\n"),
             ("docs/img/p.png", b"\x89PNG\0\0" + os.urandom(100))]
    want("clean tree passes", check_items(clean, files, wins) == [])
    for path in ("roms/k.bin", "cores/a/roms/x.rom", "cores/a/sim/media/m.bin", "x/y/disk.d81", "<removed>", MODPLAYER,
                 ".claude/context.md", ".claude/handover-1.md", ".claude/settings.local.json", "<removed>"):
        want("path " + path, check_items([(path, b"x")], files, wins) != [])
    for label, txt in (("host", host), ("home", home), ("gitea", gitea), ("login", login), ("link", link)):
        want("text " + label, check_items([("docs/a.md", ("see " + txt + "\n").encode())], files, wins) != [])
    want("host in a path", path_hits("docs/" + host + ".md") != [])
    want("binary skipped by the text rules", check_items([("a.dat", b"\0\0" + host.encode())], files, wins) == [])
    want("renamed copy (size and SHA-256)", check_items([("docs/img/blob.dat", rom)], files) != [])
    want("window in a larger blob", check_items([("a.dat", os.urandom(300) + rom[1000:5000] + os.urandom(99))], files, wins) != [])
    want("unrelated binary passes", check_items([("a.dat", os.urandom(5000))], files, wins) == [])
    with tempfile.TemporaryDirectory() as td:
        pk = os.path.join(td, "pkg"); os.makedirs(pk)
        big = os.urandom(1000) + rom[2048:6144] + os.urandom(1000)
        open(os.path.join(pk, "a.cor"), "wb").write(big)
        open(os.path.join(pk, "ok.bin"), "wb").write(os.urandom(500))
        want("directory member", check_files([pk], files, wins, False) != [])
        want("clean file", check_files([os.path.join(pk, "ok.bin")], files, wins, False) == [])
        z = os.path.join(td, "p.zip")
        with zipfile.ZipFile(z, "w") as zf:
            zf.writestr("sub/inner.cor", big)
        want("zip member", check_files([z], files, wins, False) != [])
        tg = os.path.join(td, "p.tar.gz")
        with tarfile.open(tg, "w:gz") as tf:
            ti = tarfile.TarInfo("x/inner.cor"); ti.size = len(big); tf.addfile(ti, io.BytesIO(big))
        want("tar.gz member", check_files([tg], files, wins, False) != [])
        for nm, ok in (("trx64g.cor", True), ("trx64u.cor", True), ("other.cor", False)):
            fp = os.path.join(td, nm); open(fp, "wb").write(big)
            want("trx exception %s" % nm, (check_files([fp], trxfiles, trxwins, True) == []) == ok)
        fp = os.path.join(td, "trx64u.cor")
        want("trx exception needs the flag", check_files([fp], trxfiles, trxwins, False) != [])
        want("trx exception only for trx-tagged entries", check_files([fp], files, wins, True) != [])
        open(fp, "wb").write(rom)
        want("whole file copy in trx64u.cor, trx-tagged, allowed", check_files([fp], trxfiles, trxwins, True) == [])
        want("whole file copy, not tagged, refused", check_files([fp], files, wins, True) != [])
    with tempfile.TemporaryDirectory() as td:        # message mode against a scratch repository
        g = lambda *a: subprocess.run(("git", "-C", td, "-c", "user.name=t", "-c", "user.email=t@example.org") + a, capture_output=True, check=True)
        g("init", "-q")
        for msg in ("fine", "see " + host, "x\n\nCo-Authored-By: Claude <n@example.org>"):
            open(os.path.join(td, "f"), "w").write(msg); g("add", "f"); g("commit", "-q", "-m", msg)
        both = check_messages(["HEAD"], True, td)[0]
        plain = check_messages(["HEAD"], False, td)[0]
        want("message: host", any("internal host" in b for b in both))
        want("message: trailer with --trailers", any("claude trailer" in b for b in both))
        want("message: trailer ignored without --trailers", not any("claude trailer" in b for b in plain))
        want("message: host without --trailers", any("internal host" in b for b in plain))
    if errs:
        print("selftest FAILED: " + "; ".join(errs))
        return 1
    print("selftest ok: every rule fires on its planted hit, the clean input passes")
    return 0


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--rev"); ap.add_argument("--messages", metavar="RANGE"); ap.add_argument("--trailers", action="store_true")
    ap.add_argument("--files", nargs="+"); ap.add_argument("--allow-trx", action="store_true")
    ap.add_argument("--selftest", action="store_true"); ap.add_argument("--gen-private", metavar="SRC")
    ap.add_argument("--private", default=PRIVATE)
    a = ap.parse_args()
    if a.selftest:
        return selftest()
    if a.gen_private:
        print("wrote %s: %d private binary files" % (a.private, gen_private(a.gen_private, a.private)))
        return 0
    files, wins = load_private(a.private)
    if not files:
        print("public tree: %s is missing or empty, the known-file check cannot run" % a.private)
        return 1
    if a.messages:
        bad, n = check_messages(shlex.split(a.messages), a.trailers)
        what = "%d commit messages" % n
    elif a.files:
        bad, what = check_files(a.files, files, wins, a.allow_trx), "package files"
    else:
        items = list(tree_items(a.rev))
        bad, what = check_items(items, files, wins), "%d files" % len(items)
    for b in bad:
        print("PUBLIC-TREE: " + b)
    print("public tree check (%s): %s" % (what, "%d hits" % len(bad) if bad else "clean"))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
