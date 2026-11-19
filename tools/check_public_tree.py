#!/usr/bin/env python3
"""Public-tree check (T239.1 D24, S1, D6). GitHub cannot refuse a push by path or content and a pushed file stays in the history,
so this runs before every push (.githooks/pre-push) and in CI on every push and pull request.

  tools/check_public_tree.py                     tree mode: every tracked file of the working tree
  tools/check_public_tree.py --rev REV           tree mode on the commit REV (what the pre-push hook checks)
  tools/check_public_tree.py --messages "RANGE"   message mode: the commit messages of `git rev-list RANGE` (one quoted argument) (internal hosts and paths;
                                                 with --trailers also the Claude attribution lines the maintainer's sessions never write)
  tools/check_public_tree.py --commits "RANGE"   commits mode: every (path, blob) that a commit of `git rev-list RANGE` adds or changes, so a
                                                 private file added in one commit and removed in a later one is found, too
  tools/check_public_tree.py --files PATH...     files mode: package directories, .cor files and the members of .zip and .tar.gz
                                                 against the file and window lines of ci/private.txt (--allow-trx: the fourth exception of
                                                 CLAUDE.md, a file only inside the .cor files its cor= tag names)
  tools/check_public_tree.py --selftest          plants one hit per rule; a rule that does not fire fails the selftest (T2391_PLANTS: see there)
  tools/check_public_tree.py --gen-private SRC   write ci/private.txt from the private files of the checkout SRC (their bytes stay there)
  tools/check_public_tree.py --gen-hashes FILE   print HASHED from a file of "kind<TAB>value" lines that stays outside the public tree

Tree mode refuses a path of the private list (ROM images, bench media, licence folder, MOD player image, session material outside the
D5 whitelist), a file of a known size with a known SHA-256 under any name, a 64-byte window of a private file anywhere in a blob, and in every
blob without a NUL in its first 8000 bytes an internal address, host, home path, builder login, web link or NAS path (D6). Those text rules are
generic forms; a match counts only when the SHA-256 of its normalised key is in HASHED. Exit 1 on any hit, every hit printed."""
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

# ---- text rules (D6): generic forms; a match is refused only when the SHA-256 of its normalised key is in HASHED ----
# No internal value stands in this file in readable form. HASHED holds SHA-256 of the lowercase keys, written by --gen-hashes FILE from a
# list of "kind<TAB>value" lines that stays outside the public tree. A guessable key (a short user name, a private address prefix) can be found by
# trying candidates, so this hides the values from a casual reader of the repository, it is no secret store.
M32 = 0xFFFFFFFF
HASHED = {
    "ip3": {"<removed>"},
    "host": {"<removed>"},
    "home": {"<removed>"},
    "appsup": {"<removed>"},
    "login": {"<removed>"},
    "url": {"<removed>"},
    "nas": {"<removed>"},
    "caps": {"<removed>"},
}
_dec = lambda b: b.decode("latin-1").lower()
_labels = lambda host: _dec(host).split(".")
_suffixes = lambda host: [".".join(_labels(host)[i:]) for i in range(len(_labels(host)) - 1)]
RULES = (   # (name in the report, regex, kind, keys of a match)
    ("internal address", re.compile(rb"\b(\d{1,3})\.(\d{1,3})\.(\d{1,3})\.\d{1,3}\b"), "ip3", lambda m: [".".join(_dec(g) for g in m.groups())]),
    ("internal host", re.compile(rb"\b((?:[a-z0-9-]+\.)+(?:box|lan|local|internal|home|corp|intranet))\b", re.I), "host", lambda m: _suffixes(m.group(1))),
    ("home path", re.compile(rb"/Users/([A-Za-z0-9._-]+)"), "home", lambda m: [_dec(m.group(1))]),
    ("home path", re.compile(rb"Application Support/([A-Za-z0-9._-]+)", re.I), "appsup", lambda m: [_dec(m.group(1))]),
    ("builder login", re.compile(rb"\b([A-Za-z0-9._-]+)@[A-Za-z0-9]"), "login", lambda m: [_dec(m.group(1))]),
    ("web link", re.compile(rb"(?:https?://|\b)((?:[a-z0-9-]+\.)+[a-z]{2,})/", re.I), "url", lambda m: [_dec(m.group(1))] + _suffixes(m.group(1))[-1:]),
    ("NAS path", re.compile(rb"/share/([A-Za-z0-9_]+)/"), "nas", lambda m: [_dec(m.group(1))]),
    ("NAS volume", re.compile(rb"\b([A-Z]{6,})"), "caps", lambda m: [_dec(m.group(1))]),
)
TRAILER = ("claude trailer", re.compile(rb"^(co-authored-by:\s*claude|claude-session:)", re.I | re.M), None, None)
_known = {}


def _h(key):
    return hashlib.sha256(key.encode()).hexdigest()


def rule_fires(rule, data, hashed):
    name, rx, kind, keys = rule
    if kind is None:
        return rx.search(data) is not None
    ok = hashed.get(kind)
    if not ok:
        return False
    for m in rx.finditer(data):
        for k in keys(m):
            v = _known.get((kind, k))
            if v is None:
                v = _known[(kind, k)] = _h(k) in ok
            if v:
                return True
    return False


def path_hits(path):
    hits = []
    if DROP_RE.match(path) or ROM_EXT.search(path):
        hits.append("private file type (ROM image or bench medium)")
    if path in DROP_FILES or any(path.startswith(d) for d in DROP_DIRS):
        hits.append("private file (licence folder, T120, MOD player image)")
    if path.startswith(".claude/") and not any(path.startswith(w) for w in CLAUDE_PUBLIC):
        hits.append("session material outside the D5 whitelist")
    hits += ["%s in the path" % r[0] for r in RULES if rule_fires(r, path.encode("utf-8", "surrogateescape"), HASHED)]
    return hits


def text_hits(data, rules=RULES, hashed=None):
    if b"\0" in data[:8000]:
        return []
    hashed = HASHED if hashed is None else hashed
    return [r[0] for r in rules if rule_fires(r, data, hashed)]


# ---- ci/private.txt: size + SHA-256 per private binary, windows of 64 bytes as hashes only (no file bytes) ----
# A window line is "win <rolling hash, 8 hex> <SHA-256 of the 64 bytes> <file> [cor=a.cor,b.cor]". The rolling hash is a 32-bit polynomial
# hash over every offset of a scanned blob (prefilter), the SHA-256 of the 64 bytes at a candidate offset confirms it.
WIN, NWIN = 64, 4
RP = 0x9E3779B1                      # odd, so it has an inverse modulo 2**32
RPINV, RPW = pow(RP, -1, 1 << 32), pow(RP, WIN - 1, 1 << 32)


RPOWS = [pow(RP, k, 1 << 32) for k in range(WIN)]


def roll_of(w):
    return sum(b * RPOWS[k] for k, b in enumerate(w)) & M32


def _cors(f):
    for x in f:
        if x.startswith("cor="):
            return tuple(x[4:].split(","))
    return ()


def load_private(path=PRIVATE):
    files, wins = {}, {}          # files: size -> [(sha, name, cors)], wins: rolling hash -> [(sha, name, cors)]
    if not os.path.exists(path):
        return files, wins
    for ln in open(path):
        f = ln.split()
        if not f or f[0].startswith("#"):
            continue
        if f[0] == "file":
            files.setdefault(int(f[1]), []).append((f[2], f[3], _cors(f[4:])))
        elif f[0] == "win":
            wins.setdefault(int(f[1], 16), []).append((f[2], f[3], _cors(f[4:])))
        else:
            raise SystemExit("%s: bad line: %s" % (path, ln.strip()))
    return files, wins


def windows_of(data, taken=lambda w: False):
    """NWIN windows of 64 bytes spread over the file, each with at least 16 distinct byte values and not taken(w) (a window that a public file
    holds, too, would flag that file)."""
    out, n = [], len(data)
    if n < WIN:
        return out
    for k in range(NWIN):
        start = (n - WIN) * (2 * k + 1) // (2 * NWIN)
        for off in range(start, n - WIN + 1):
            w = data[off:off + WIN]
            if len(set(w)) >= 16 and not taken(w) and all(off != o for o, _ in out):
                out.append((off, w))
                break
    return out


def private_rows(src):
    ls = subprocess.run(["git", "-C", src, "ls-files", "-z"], capture_output=True, check=True).stdout.decode().split("\0")
    return [(p, open(os.path.join(src, p), "rb").read()) for p in sorted(x for x in ls if x)
            if DROP_RE.match(p) or ROM_EXT.search(p) or p == MODPLAYER]


# D22 / CLAUDE.md, fourth exception: which .cor may carry which file. The BASIC ROM only trx64g.cor, the MOD player image both.
COR_ALLOWED = (("basic.901226-01.bin", ("trx64g.cor",)), ("module.bin", ("trx64g.cor", "trx64u.cor")))


def public_blobs(src, private):
    ls = subprocess.run(["git", "-C", src, "ls-files", "-z"], capture_output=True, check=True).stdout.decode().split("\0")
    for p in sorted(x for x in ls if x and x not in private):
        fp = os.path.join(src, p)
        if os.path.isfile(fp) and not os.path.islink(fp):
            yield open(fp, "rb").read()


def gen_private(src, dest):
    rows = private_rows(src)
    others = list(public_blobs(src, {p for p, _ in rows}))
    taken = lambda w: any(w in b for b in others)
    out = ["# Private binary files of the Gitea checkout: size and SHA-256 per file, and per file a few windows of 64 bytes as hashes only",
           "# (a 32-bit rolling hash the scan finds at any offset, and the SHA-256 of the 64 bytes that confirms it). No file bytes are stored here.",
           "# Generated by tools/check_public_tree.py --gen-private SRC. cor= lists the .cor files that may carry the file (--allow-trx;"
           " CLAUDE.md, fourth exception)."]
    for p, d in rows:
        cors = next((c for suffix, c in COR_ALLOWED if p.endswith(suffix)), ())
        tag = " cor=" + ",".join(cors) if cors else ""
        out.append("file %d %s %s%s" % (len(d), hashlib.sha256(d).hexdigest(), p, tag))
        for off, w in windows_of(d, taken):
            out.append("win %08x %s %s%s" % (roll_of(w), hashlib.sha256(w).hexdigest(), p, tag))
    open(dest, "w").write("\n".join(out) + "\n")
    return len(rows)


def known_file(data, files):
    return [(name, cors) for sha, name, cors in files.get(len(data), []) if hashlib.sha256(data).hexdigest() == sha]


def roll_candidates(data, wins):
    """Offsets whose 64-byte rolling hash is one of the windows' (numpy when it is there, else a plain loop)."""
    n = len(data)
    if n < WIN or not wins:
        return []
    try:
        import numpy as np
    except ImportError:
        np = None
    if np is not None:
        h = np.frombuffer(data, dtype=np.uint8).astype(np.uint32)
        m = 1
        while m < WIN:                                   # h_2m[i] = h_m[i] + RP**m * h_m[i+m]
            h = h[:len(h) - m] + np.uint32(pow(RP, m, 1 << 32)) * h[m:]
            m *= 2
        return np.nonzero(np.isin(h, np.fromiter(wins, dtype=np.uint32, count=len(wins))))[0].tolist()
    out, cur = [], roll_of(data[:WIN])
    for i in range(n - WIN + 1):
        if cur in wins:
            out.append(i)
        if i + WIN < n:
            cur = ((cur - data[i]) * RPINV + data[i + WIN] * RPW) & M32
    return out


def window_hits(data, wins, allow_trx=False, base=""):
    hits, leaf = [], os.path.basename(base)
    for i in roll_candidates(data, wins):
        w = data[i:i + WIN]
        sha = hashlib.sha256(w).hexdigest()
        for wsha, name, cors in wins.get(roll_of(w), ()):
            if wsha == sha and not (allow_trx and leaf in cors):
                hits.append("%s at offset %d" % (name, i))
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
    rules = RULES + ((TRAILER,) if trailers else ())
    bad = []
    for r in revs:
        m = subprocess.run(["git", "-C", root, "log", "-1", "--format=%an <%ae>%n%B", r], capture_output=True, check=True).stdout
        bad += ["commit %s: %s in the message" % (r[:10], rule[0]) for rule in rules if rule_fires(rule, m, HASHED)]
    return bad, len(revs)


# ---- commits mode: every (path, blob) a commit of the range adds or changes, also when a later commit removes it again ----
def commit_blobs(root, c):
    out = subprocess.run(["git", "-C", root, "diff-tree", "-r", "--root", "-m", "-z", "--no-commit-id", "--no-renames", "--diff-filter=AMRT", c],
                         capture_output=True, check=True).stdout.decode("utf-8", "surrogateescape").split("\0")
    ents = []
    for meta, path in zip(out[0::2], out[1::2]):
        f = meta.lstrip(":").split()
        if f[1] != "160000":
            ents.append((path, f[3]))
    return ents


def check_commits(rng, files, wins, root=None):
    root = root or ROOT
    revs = subprocess.run(["git", "-C", root, "rev-list", "--reverse"] + rng, capture_output=True, check=True).stdout.decode().split()
    seen, todo = set(), []
    for c in revs:
        for path, blob in commit_blobs(root, c):
            if (path, blob) not in seen:
                seen.add((path, blob)); todo.append((c, path, blob))
    bad = []
    pr = subprocess.Popen(["git", "-C", root, "cat-file", "--batch"], stdin=subprocess.PIPE, stdout=subprocess.PIPE)
    for c, path, blob in todo:
        pr.stdin.write(blob.encode() + b"\n"); pr.stdin.flush()
        h = pr.stdout.readline().split()
        d = pr.stdout.read(int(h[2])); pr.stdout.read(1)
        bad += ["commit %s: %s" % (c[:10], b) for b in check_items([(path, d)], files, wins)]
    pr.stdin.close(); pr.wait()
    return bad, len(revs), len(todo)


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
            bad += ["%s: is a copy of the private file %s" % (name, n) for n, cors in known_file(d, files)
                    if not (allow_trx and os.path.basename(leaf) in cors)]
            bad += ["%s: holds %s" % (name, h) for h in window_hits(d, wins, allow_trx, leaf)]
            bad += ["%s: %s" % (name, h) for h in text_hits(d)]
    return bad


# ---- selftest ----
# The planted values of the text rules are synthetic (their hashes are put into HASHED for the run). The real list is exercised only when
# T2391_PLANTS names a file of "kind<TAB>value" lines (the list's source, kept outside the public tree): then every kind of HASHED must be
# backed by a value and each value must fire its rule. gen_hashes() reads the same file.
SYNTH = (("ip3", "10.77.5", "see 10.77.5.9\n"), ("host", "box.lan", "see a.box.lan\n"), ("home", "someuser", "at /Users/someuser/x\n"),
         ("appsup", "sometool", "Library/Application Support/sometool/c\n"), ("login", "someuser", "ssh someuser@build\n"),
         ("url", "example.test", "see https://example.test/p\n"), ("nas", "vault", "mount /share/vault/x\n"), ("caps", "zzzzzz", "ZZZZZZ9_DATA\n"))
KIND_RULE = {r[2]: r for r in RULES}


def plants_file():
    p = os.environ.get("T2391_PLANTS")
    return [ln.rstrip("\n").split("\t", 1) for ln in open(p) if "\t" in ln] if p and os.path.exists(p) else None


def gen_hashes(src):
    h = {k: [] for k in HASHED}
    for kind, val in (ln.rstrip("\n").split("\t", 1) for ln in open(src) if "\t" in ln):
        h[kind].append(_h(val.lower()))
    out = ["HASHED = {"]
    for k, v in h.items():
        out.append('    "%s": {%s},' % (k, ", ".join('"%s"' % x for x in sorted(v))))
    return "\n".join(out) + "\n}"


def plant_text(kind, val):
    return {"ip3": "see %s.9\n", "host": "see a.%s\n", "home": "at /Users/%s/x\n", "appsup": "Library/Application Support/%s/c\n",
            "login": "ssh %s@build\n", "url": "see https://%s/p\n", "nas": "mount /share/%s/x\n", "caps": "%s9_DATA\n"}[kind] % val


def selftest():
    saved = {k: set(v) for k, v in HASHED.items()}
    for k in HASHED:
        HASHED[k].clear()
    _known.clear()
    try:
        return _selftest(saved)
    finally:
        for k, v in saved.items():
            HASHED[k].clear(); HASHED[k].update(v)
        _known.clear()


def _selftest(saved):
    errs = []

    def want(label, cond):
        if not cond:
            errs.append(label)

    for kind, val, txt in SYNTH:
        HASHED[kind].add(_h(val))
    rom = random.Random(7).randbytes(8192)
    sha = hashlib.sha256(rom).hexdigest()
    win = windows_of(rom)
    want("windows_of gives %d windows" % NWIN, len(win) == NWIN)
    both, gonly = ("trx64g.cor", "trx64u.cor"), ("trx64g.cor",)
    files = {len(rom): [(sha, "roms/fake.bin", ())]}
    wins = {}
    for _, w in win:
        wins.setdefault(roll_of(w), []).append((hashlib.sha256(w).hexdigest(), "roms/fake.bin", ()))
    tag = lambda cors: ({len(rom): [(sha, "roms/fake.bin", cors)]}, {k: [(a, b, cors) for a, b, _ in v] for k, v in wins.items()})
    bothfiles, bothwins = tag(both)
    gfiles, gwins = tag(gonly)

    clean = [("README.md", b"hello 1.2.3.4 v1.2 a@b.c\n"), ("coreapp/a.cc", b"int x;\n"), (".claude/skills/a/SKILL.md", b"x\n"),
             ("CLAUDE.md", b"rules\n"), ("docs/img/p.png", b"\x89PNG\0\0" + os.urandom(100))]
    want("clean tree passes", check_items(clean, files, wins) == [])
    for path in ("roms/k.bin", "cores/a/roms/x.rom", "cores/a/sim/media/m.bin", "x/y/disk.d81", "<removed>", MODPLAYER,
                 ".claude/context.md", ".claude/handover-1.md", ".claude/settings.local.json", "<removed>"):
        want("path " + path, check_items([(path, b"x")], files, wins) != [])
    for kind, val, txt in SYNTH:
        want("text " + kind, check_items([("docs/a.md", txt.encode())], files, wins) != [])
    want("host in a path", path_hits("docs/a.box.lan.md") != [])
    want("binary skipped by the text rules", check_items([("a.dat", b"\0\0" + SYNTH[0][2].encode())], files, wins) == [])
    want("renamed copy (size and SHA-256)", check_items([("docs/img/blob.dat", rom)], files) != [])
    want("window in a larger blob", check_items([("a.dat", os.urandom(300) + rom[1000:5000] + os.urandom(99))], files, wins) != [])
    want("unrelated binary passes", check_items([("a.dat", os.urandom(5000))], files, wins) == [])
    big = os.urandom(1000) + rom[2048:6144] + os.urandom(1000)
    cands = roll_candidates(big, wins)
    want("rolling hash finds a window at any offset", any(roll_of(big[i:i + WIN]) in wins for i in cands) and len(cands) >= 1)
    try:
        import numpy
        sys.modules["numpy"], saved_np = None, sys.modules["numpy"]
        try:
            want("rolling hash without numpy agrees", roll_candidates(big, wins) == cands)
        finally:
            sys.modules["numpy"] = saved_np
    except ImportError:
        pass
    with tempfile.TemporaryDirectory() as td:
        pk = os.path.join(td, "pkg"); os.makedirs(pk)
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
        for nm, okb, okg in (("trx64g.cor", True, True), ("trx64u.cor", True, False), ("other.cor", False, False)):
            fp = os.path.join(td, nm); open(fp, "wb").write(big)
            want("cor tag %s (both)" % nm, (check_files([fp], bothfiles, bothwins, True) == []) == okb)
            want("cor tag %s (trx64g only)" % nm, (check_files([fp], gfiles, gwins, True) == []) == okg)
        fp = os.path.join(td, "trx64u.cor")
        want("the exception needs the flag", check_files([fp], bothfiles, bothwins, False) != [])
        want("the exception only for tagged entries", check_files([fp], files, wins, True) != [])
        open(fp, "wb").write(rom)
        want("whole file copy in trx64u.cor, tagged for both, allowed", check_files([fp], bothfiles, bothwins, True) == [])
        want("whole file copy in trx64u.cor, tagged trx64g only, refused", check_files([fp], gfiles, gwins, True) != [])
        want("whole file copy, not tagged, refused", check_files([fp], files, wins, True) != [])
    with tempfile.TemporaryDirectory() as td:        # message and commits mode against a scratch repository
        g = lambda *a: subprocess.run(("git", "-C", td, "-c", "user.name=t", "-c", "user.email=t@example.org") + a, capture_output=True, check=True)
        g("init", "-q")
        for msg in ("fine", "see " + SYNTH[0][2], "x\n\nCo-Authored-By: Claude <n@example.org>"):
            open(os.path.join(td, "f"), "w").write(msg); g("add", "f"); g("commit", "-q", "-m", msg)
        both = check_messages(["HEAD"], True, td)[0]
        plain = check_messages(["HEAD"], False, td)[0]
        want("message: address", any("internal address" in b for b in both))
        want("message: trailer with --trailers", any("claude trailer" in b for b in both))
        want("message: trailer ignored without --trailers", not any("claude trailer" in b for b in plain))
        want("message: address without --trailers", any("internal address" in b for b in plain))
        first = g("rev-list", "--max-parents=0", "HEAD").stdout.decode().strip()
        open(os.path.join(td, "leak.dat"), "wb").write(rom); g("add", "leak.dat"); g("commit", "-q", "-m", "adds a private copy")
        g("rm", "-q", "leak.dat"); g("commit", "-q", "-m", "removes it again")
        bad, nc, nb = check_commits([first + "..HEAD"], files, wins, td)
        want("commits mode: a file added and removed later is found", any("leak.dat" in b and "copy of the private file" in b for b in bad))
        want("commits mode: counts", nc == 4 and nb >= 1)
    real = plants_file()
    for k in HASHED:
        HASHED[k].clear()
    HASHED.update({k: set(v) for k, v in saved.items()})
    _known.clear()
    if real is None:
        print("note: T2391_PLANTS is not set, the real list of HASHED was not exercised")
    else:
        for kind in HASHED:
            vals = [v for k, v in real if k == kind]
            want("real list: kind %s has hashes and a source value" % kind, bool(vals) and bool(HASHED[kind]))
            want("real list: kind %s hashes are exactly the source" % kind, {_h(v.lower()) for v in vals} == HASHED[kind])
        for kind, val in real:
            want("real list: %s fires" % kind, check_items([("docs/a.md", plant_text(kind, val).encode())], {}, {}) != [])
        want("real list: clean text passes", check_items([("docs/a.md", b"hello 1.2.3.4 someone@example.org /Users/x/y\n")], {}, {}) == [])
    if errs:
        print("selftest FAILED: " + "; ".join(errs))
        return 1
    print("selftest ok: every rule fires on its planted hit, the clean input passes")
    return 0


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--rev"); ap.add_argument("--messages", metavar="RANGE"); ap.add_argument("--trailers", action="store_true")
    ap.add_argument("--files", nargs="+"); ap.add_argument("--allow-trx", action="store_true")
    ap.add_argument("--commits", metavar="RANGE")
    ap.add_argument("--selftest", action="store_true"); ap.add_argument("--gen-private", metavar="SRC"); ap.add_argument("--gen-hashes", metavar="FILE")
    ap.add_argument("--private", default=PRIVATE)
    a = ap.parse_args()
    if a.selftest:
        return selftest()
    if a.gen_hashes:
        print(gen_hashes(a.gen_hashes))
        return 0
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
    elif a.commits:
        bad, n, nb = check_commits(shlex.split(a.commits), files, wins)
        what = "%d commits, %d added or changed files" % (n, nb)
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
