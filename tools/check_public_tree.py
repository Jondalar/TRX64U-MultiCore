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
  tools/check_public_tree.py --selftest          plants one hit per rule with generic examples; a rule that does not fire fails the selftest
  tools/check_public_tree.py --gen-private SRC [--side DIR]
                                                 write ci/private.txt from the private files of the checkout SRC (their bytes stay there); with
                                                 --side every other tracked file of the side repository DIR is listed by size and SHA-256 under a
                                                 label made of the hash, so that no public file names a private file (the check refuses it by content)

Tree mode refuses a path of the private list (ROM images, bench media, MOD player image, session material outside the
D5 whitelist), a file of a known size with a known SHA-256 under any name, a 64-byte window of a private file anywhere in a blob, and in every
blob without a NUL in its first 8000 bytes a private-network address, home path, internal host name, login on a host without a dot or claude.ai link
(D6). The text rules are generic forms that need no list of values. A symlink counts with its path and its target. Exit 1 on any hit, every hit printed."""
import argparse, hashlib, io, os, random, re, shlex, subprocess, sys, tarfile, tempfile, zipfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PRIVATE = os.path.join(ROOT, "ci", "private.txt")

# ---- path rules (the rules of the former GitHub export, D5 whitelist) ----
DROP_RE = re.compile(r"^(roms/.*\.bin|cores/[^/]+/roms/.*\.rom|cores/[^/]+/sim/media/.*)$")
ROM_EXT = re.compile(r"\.(rom|img|adf|adz|d64|d71|d81|g64|crt|tap|tzx|t64|dsk|hdf|nib|po)$", re.I)
MODPLAYER = "release/imports/trx64u_files/modplayer/module.bin"
DROP_FILES = (MODPLAYER,)
CLAUDE_PUBLIC = ("CLAUDE.md", ".claude/agents/", ".claude/skills/", ".claude/trx64-site/")     # D5: everything else under .claude/ is private

# ---- text rules (D6): generic forms, no list of values ----
# Not private: the documentation examples (192.0.2.0/24, 198.51.100.0/24, 203.0.113.0/24 are outside every private range) and the export's
# placeholders: /path/to/ for a home path, gitea.example for a host, builder for a login's host (the bare word; <name>@builder is a hit).
M32 = 0xFFFFFFFF
_oct = r"(?:25[0-5]|2[0-4]\d|1\d\d|[1-9]?\d)"
_EDGE_L, _EDGE_R = rb"(?<![\w.])", rb"(?![\w-])(?!\.\d)"
RULES = (   # (name in the report, regex, exclusions, kind)
    ("private address", re.compile(_EDGE_L + (r"(?:10\.%s\.%s\.%s|172\.(?:1[6-9]|2\d|3[01])\.%s\.%s|192\.168\.%s\.%s|169\.254\.%s\.%s)" % (
        (_oct,) * 3 + (_oct,) * 2 + (_oct,) * 2 + (_oct,) * 2)).encode() + _EDGE_R), (), None),
    ("home path", re.compile(rb"(?<![\w.-])/(?:Users|home)/([A-Za-z0-9._-]+)/"), (b"x", b"user", b"you", b"name", b"username", b"runner"), None),
    ("internal host", re.compile(rb"(?<![\w.-])((?:[a-z0-9-]+\.)+(?:box|lan|local|home\.arpa))(?![\w-])(?!\.[A-Za-z0-9])", re.I), (), None),
    ("login on an internal host", re.compile(rb"(?<![\w.@-])[a-z][a-z0-9._-]{2,}@(?![0-9a-f]{6,}(?![\w-]))([A-Za-z][A-Za-z0-9-]{2,})(?![\w-])(?!\.[A-Za-z0-9])"),
     (b"host", b"hostname", b"localhost", b"sha", b"sha256", b"address", b"refs", b"example"), None),
    ("claude.ai link", re.compile(rb"\bclaude\.ai/", re.I), (), None),
)
TRAILER = ("claude trailer", re.compile(rb"^(co-authored-by:\s*claude|claude-session:)", re.I | re.M), (), None)
# Narrow, named exceptions of the whole-tree run: (reason, rule, paths). Each path is one file; nothing is allowed by folder or by pattern. A new
# hit in any other file is red.
_ADDR = "private address"
_TEXT_ALLOW = (
    ("vendor default of the ESP32 network settings (192.168.2.x) and its documentation, mock-ups and golden pages of the settings screens", _ADDR, (
        "docs/lvgl_ui_project/screens/settings_network.xml", "docs/lvgl_ui_terminal/screens/settings_network.xml",
        "docs/mockups/T198-config-overlay.html", "docs/ultimate-app-hw-spec.md", "host-app/software/hostapp/host_core_flow_wifi.cc",
        "host-app/software/io/network/network_esp32.cc", "host-app/software/io/network/network_interface.cc",
        "vendor/gideon/software/io/network/network_esp32.cc", "vendor/gideon/software/io/network/network_interface.cc",
        "vendor/gideon/software/api/route_streams.cc", "tests/hostapp/golden/hostwifi.txt",
        "tests/hostapp/render/golden/hostrows.txt")),
    ("example addresses typed into the unit tests, the renderers and the specs of the network screens and the FTP client", _ADDR, (
        "host-app/software/hostapp/hostui.cc", "tests/coreapp/render/render.cc", "tests/coreapp/test_ftp.cc", "tests/coreapp/test_hostwifi.cc",
        "tests/coreapp/test_wifistate.cc", "tests/hostapp/render/render.cc", "tests/hostapp/test_hostwifi.cc", "tests/hostapp/test_nettransport.cc",
        "tests/hostapp/test_report.cc", "tests/hostapp/test_webnet.cc", "tests/hostapp/test_webui.cc", "docs/specs/done/T186-ftp-client-fixes.md")),
    ("QEMU/SLIRP guest and gateway addresses (10.0.2.x) of the emulator and the simulation", _ADDR, (
        "docs/specs/done/T164-host-package-2.md", "docs/ui/trx64u-row-audit-2026-11-18.md", "tests/hostapp/emu/parity_walk.py", "host/sim/tb_udp_audio.v")),
    ("link-local and mDNS constants and examples of the lwIP sources", _ADDR, (
        "vendor/gideon/software/lwip/src/apps/mdns/mdns_domain.c", "vendor/gideon/software/lwip/src/core/ipv4/autoip.c",
        "vendor/gideon/software/lwip/src/include/lwip/prot/autoip.h")),
    ("a struct member or variable (in.local, x.local, r.box) that has the shape of a host name", "internal host", (
        "coreapp/src/uiapp.cc", "host-app/software/hostapp/hostdomain.cc", "host-app/software/hostapp/hostui.cc", "tests/uicore/test_uicore.cc",
        "uicore/uic_net.cc", "uicore/uic_svc.cc")),
    ("host names of an example URL in a unit test (nas.local) and of the lwIP sources (hostname.local, msg.ad.local)", "internal host", (
        "tests/coreapp/test_corereport.cc", "tests/coreapp/test_ftp.cc", "vendor/gideon/software/lwip/src/apps/mdns/mdns_out.c",
        "vendor/gideon/software/lwip/src/api/api_msg.c")),
    ("a redaction test: user:password@host of an example FTP URL", "login on an internal host", ("tests/hostapp/test_report.cc",)),
    ("the obfuscated contact line of the stb_image author (name@company, no dot)", "login on an internal host",
     ("vendor/gideon/software/lvgl/src/libs/gltf/stb_image/stb_image.h",)),
    ("this tool and its test: the planted examples of the selftest (private ranges, made-up account, host names) are generic values",
     "*", ("tools/check_public_tree.py", "tools/test_check_public_tree.py")),
)
TEXT_ALLOW = {(p, r): why for why, r, ps in _TEXT_ALLOW for p in ps}


def rule_fires(rule, data, path=None):
    """Whether the rule matches data. rule[2] lists the values of the rule's first group that are placeholders or the GitHub runner account."""
    name, rx, allowed, _ = rule
    if (path, name) in TEXT_ALLOW or (path, "*") in TEXT_ALLOW:
        return False
    return any(not (rx.groups and m.group(1).lower() in allowed) for m in rx.finditer(data))


def path_hits(path):
    hits = []
    if DROP_RE.match(path) or ROM_EXT.search(path):
        hits.append("private file type (ROM image or bench medium)")
    if path in DROP_FILES:
        hits.append("private file (MOD player image)")
    if path == "CLAUDE.local.md":
        hits.append("private file (the maintainer's local session notes)")
    if path.startswith(".claude/") and not any(path.startswith(w) for w in CLAUDE_PUBLIC):
        hits.append("session material outside the D5 whitelist")
    hits += ["%s in the path" % r[0] for r in RULES if rule_fires(r, path.encode("utf-8", "surrogateescape"), path)]
    return hits


def text_hits(data, path=None, rules=RULES):
    if b"\0" in data[:8000]:
        return []
    return [r[0] for r in rules if rule_fires(r, data, path)]


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


# D22 / CLAUDE.md, fourth exception: which .cor may carry which file. The BASIC ROM and (T239.1 D44) the character ROM only trx64g.cor (Gideon's
# application holds them), the MOD player image the same: trx64u.cor is TRX64-Ultimate's file, taken unchanged (T239.3 D21).
COR_ALLOWED = (("basic.901226-01.bin", ("trx64g.cor",)), ("characters.901225-01.bin", ("trx64g.cor",)), ("module.bin", ("trx64g.cor",)))


def public_blobs(src, private):
    ls = subprocess.run(["git", "-C", src, "ls-files", "-z"], capture_output=True, check=True).stdout.decode().split("\0")
    for p in sorted(x for x in ls if x and x not in private):
        fp = os.path.join(src, p)
        if os.path.isfile(fp) and not os.path.islink(fp):
            yield open(fp, "rb").read()


def side_rows(side, taken):
    """Every tracked file of the side repository that the path rules above do not list: the text files whose names the public tree must not carry.
    The label is made of the hash; the README of the side repository is not a file of the checkout."""
    ls = subprocess.run(["git", "-C", side, "ls-files", "-z"], capture_output=True, check=True).stdout.decode().split("\0")
    rows = []
    for p in sorted(x for x in ls if x and x != "README.md" and x not in taken):
        d = open(os.path.join(side, p), "rb").read()
        rows.append(("side-" + hashlib.sha256(d).hexdigest()[:8], d))
    return rows


def gen_private(src, dest, side=None):
    rows = private_rows(src)
    nbin = len(rows)
    if side:
        rows += side_rows(side, {p for p, _ in rows})
    others = list(public_blobs(src, {p for p, _ in rows}))
    taken = lambda w: any(w in b for b in others)
    out = ["# Private binary files of the Gitea checkout: size and SHA-256 per file, and per file a few windows of 64 bytes as hashes only",
           "# (a 32-bit rolling hash the scan finds at any offset, and the SHA-256 of the 64 bytes that confirms it). No file bytes are stored here.",
           "# Generated by tools/check_public_tree.py --gen-private SRC. cor= lists the .cor files that may carry the file (--allow-trx;"
           " CLAUDE.md, fourth exception)."]
    for i, (p, d) in enumerate(rows):
        cors = next((c for suffix, c in COR_ALLOWED if p.endswith(suffix)), ())
        tag = " cor=" + ",".join(cors) if cors else ""
        out.append("file %d %s %s%s" % (len(d), hashlib.sha256(d).hexdigest(), p, tag))
        for off, w in (windows_of(d, taken) if i < nbin else ()):     # a text of the side repository: the whole file only, the text rules cover the rest
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
        return [(r.split("\t", 1)[1], r.split()[2], r.split()[0]) for r in out.split("\0") if r and r.split()[1] == "blob"]
    out = subprocess.run(["git", "-C", ROOT, "ls-files", "-z"], capture_output=True, check=True).stdout.decode()
    return [(p, None, None) for p in out.split("\0") if p]


def tree_items(rev):
    """(path, bytes[, link target]) of every tracked file; with --rev from the git objects through one cat-file process. A symlink gives its
    target as the bytes and as the third member, so its path and its target are both checked."""
    ents = tracked(rev)
    if not rev:
        for p, _, _ in ents:
            fp = os.path.join(ROOT, p)
            if os.path.islink(fp):
                t = os.readlink(fp)
                yield p, t.encode("utf-8", "surrogateescape"), t
            elif os.path.isfile(fp):
                yield p, open(fp, "rb").read()
        return
    pr = subprocess.Popen(["git", "-C", ROOT, "cat-file", "--batch"], stdin=subprocess.PIPE, stdout=subprocess.PIPE)
    for p, sha, mode in ents:
        pr.stdin.write(sha.encode() + b"\n"); pr.stdin.flush()
        h = pr.stdout.readline().split()
        d = pr.stdout.read(int(h[2])); pr.stdout.read(1)
        yield (p, d, d.decode("utf-8", "surrogateescape")) if mode == "120000" else (p, d)
    pr.stdin.close(); pr.wait()


def link_path(p, target):
    """The path a symlink at p points to, relative to the tree root when the target is relative."""
    return os.path.normpath(target if os.path.isabs(target) else os.path.join(os.path.dirname(p), target))


def check_items(items, files, wins=()):
    bad = []
    for it in items:
        p, d = it[0], it[1]
        if len(it) > 2:                                  # a symlink: its target is a path of the tree, too
            bad += ["%s: symlink target %s: %s" % (p, it[2], h) for h in path_hits(link_path(p, it[2])) + path_hits(it[2])]
        bad += ["%s: %s" % (p, h) for h in path_hits(p)]
        bad += ["%s: %s" % (p, h) for h in text_hits(d, p)]
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
        bad += ["commit %s: %s in the message" % (r[:10], rule[0]) for rule in rules if rule_fires(rule, m)]
    return bad, len(revs)


# ---- commits mode: every (path, blob) a commit of the range adds or changes, also when a later commit removes it again ----
def commit_blobs(root, c):
    out = subprocess.run(["git", "-C", root, "diff-tree", "-r", "--root", "-m", "-z", "--no-commit-id", "--no-renames", "--diff-filter=AMRT", c],
                         capture_output=True, check=True).stdout.decode("utf-8", "surrogateescape").split("\0")
    ents = []
    for meta, path in zip(out[0::2], out[1::2]):
        f = meta.lstrip(":").split()
        if f[1] != "160000":
            ents.append((path, f[3], f[1] == "120000"))
    return ents


def check_commits(rng, files, wins, root=None):
    root = root or ROOT
    revs = subprocess.run(["git", "-C", root, "rev-list", "--reverse"] + rng, capture_output=True, check=True).stdout.decode().split()
    seen, todo = set(), []
    for c in revs:
        for path, blob, link in commit_blobs(root, c):
            if (path, blob) not in seen:
                seen.add((path, blob)); todo.append((c, path, blob, link))
    bad = []
    pr = subprocess.Popen(["git", "-C", root, "cat-file", "--batch"], stdin=subprocess.PIPE, stdout=subprocess.PIPE)
    for c, path, blob, link in todo:
        pr.stdin.write(blob.encode() + b"\n"); pr.stdin.flush()
        h = pr.stdout.readline().split()
        d = pr.stdout.read(int(h[2])); pr.stdout.read(1)
        item = (path, d, d.decode("utf-8", "surrogateescape")) if link else (path, d)
        bad += ["commit %s: %s" % (c[:10], b) for b in check_items([item], files, wins)]
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
            bad += ["%s: %s" % (name, h) for h in text_hits(d, name)]
    return bad


# ---- selftest ----
# The planted values are generic examples of the rules' forms (private ranges of RFC 1918 and 3927, a made-up account and host names), never a
# value of this project. The documentation ranges and the placeholders of the export must pass.
SYNTH = (("private address", "see 10.0.0.5\n"), ("private address", "http://172.16.4.4:8080/x\n"),
         ("private address", "ssh 192.168.1.20\n"), ("private address", "link 169.254.10.1.\n"),
         ("home path", "at /Users/jdoe/src/x\n"), ("home path", "at /home/jdoe/src/x\n"),
         ("internal host", "see devhost.lan\n"), ("internal host", "see nas.box/share\n"), ("internal host", "ping printer.local.\n"),
         ("internal host", "see ci.home.arpa\n"), ("login on an internal host", "ssh jdoe@devhost\n"), ("login on an internal host", "ssh someone@builder\n"),
         ("claude.ai link", "see https://claude.ai/code/session_x\n"))
SYNTH_CLEAN = (b"docs 192.0.2.7 198.51.100.7 203.0.113.7\n", b"public 11.1.2.3 172.15.0.1 172.32.0.1 192.169.1.1 1.10.0.0.1\n",
               b"version 10.1.2.3.4 v10.0.0.1\n", b"cp /path/to/file /Users/x/y /home/runner/work\n", b"a@example.org git@github.com ssh builder\n",
               b"gitea.example/owner/repo settings.local.json CLAUDE.local.md x.local.y\n", b"https://claude.com/x https://docs.claude.com/\n",
               b"npm i @types/node image@sha256:abc\n")
SYNTH_LINKS = (("a.dat", "/Users/jdoe/x"), ("docs/l", "../roms/k.bin"), ("docs/l", "/home/jdoe/x"))


def selftest():
    return _selftest()


def _selftest():
    errs = []

    def want(label, cond):
        if not cond:
            errs.append(label)

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
    for path in ("roms/k.bin", "cores/a/roms/x.rom", "cores/a/sim/media/m.bin", "x/y/disk.d81", MODPLAYER,
                 ".claude/context.md", ".claude/handover-1.md", ".claude/settings.local.json"):
        want("path " + path, check_items([(path, b"x")], files, wins) != [])
    for name, txt in SYNTH:
        want("text " + txt.strip(), name in text_hits(txt.encode(), "docs/a.md"))
    for txt in SYNTH_CLEAN:
        want("clean text " + txt.decode().strip(), text_hits(txt, "docs/a.md") == [])
    want("host in a path", path_hits("docs/devhost.lan") != [])
    want("CLAUDE.local.md at the root", path_hits("CLAUDE.local.md") != [] and path_hits("docs/CLAUDE.local.md") == [])
    want("binary skipped by the text rules", check_items([("a.dat", b"\0\0" + SYNTH[0][1].encode())], files, wins) == [])
    for path, target in SYNTH_LINKS:
        want("symlink %s -> %s" % (path, target), check_items([(path, target.encode(), target)], files, wins) != [])
    want("clean symlink", check_items([("docs/l", b"../docs/b.md", "../docs/b.md")], files, wins) == [])
    want("symlink target is a private file path", any("symlink target" in b for b in check_items([("docs/l", b"../cores/a/roms/x.rom", "../cores/a/roms/x.rom")], files, wins)))
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
    with tempfile.TemporaryDirectory() as td:        # a text file of the side repository: refused by its content, listed under a label without its name
        gt = lambda d, *a: subprocess.run(("git", "-C", d, "-c", "user.name=t", "-c", "user.email=t@example.org") + a, capture_output=True, check=True)
        sd, sr = os.path.join(td, "side"), os.path.join(td, "src")
        for d in (sd, sr):
            os.makedirs(d); gt(d, "init", "-q")
        note = b"a private note, some words of a text that is no rule's form\n"
        os.makedirs(os.path.join(sd, "notes")); open(os.path.join(sd, "notes", "n.md"), "wb").write(note); open(os.path.join(sd, "README.md"), "w").write("x\n")
        open(os.path.join(sr, "a.txt"), "w").write("public\n")
        gt(sd, "add", "-A"); gt(sd, "commit", "-q", "-m", "s"); gt(sr, "add", "-A"); gt(sr, "commit", "-q", "-m", "s")
        lst = os.path.join(td, "private.txt")
        gen_private(sr, lst, sd)
        sf, sw = load_private(lst)
        listing = open(lst).read()
        want("side file: listed by hash, its name is not in the list", "notes" not in listing and "n.md" not in listing and "side-" in listing)
        want("side file: a copy under any name is refused", check_items([("docs/x/other.md", note)], sf, sw) != [])
        want("side file: another text passes", check_items([("docs/x/other.md", note + b"more\n")], sf, sw) == [])
        want("side file: the side README is not listed", len(sf.get(len(b"x\n"), [])) == 0)
    with tempfile.TemporaryDirectory() as td:        # message and commits mode against a scratch repository
        g = lambda *a: subprocess.run(("git", "-C", td, "-c", "user.name=t", "-c", "user.email=t@example.org") + a, capture_output=True, check=True)
        g("init", "-q")
        for msg in ("fine", "see " + SYNTH[0][1], "x\n\nCo-Authored-By: Claude <n@example.org>"):
            open(os.path.join(td, "f"), "w").write(msg); g("add", "f"); g("commit", "-q", "-m", msg)
        both = check_messages(["HEAD"], True, td)[0]
        plain = check_messages(["HEAD"], False, td)[0]
        want("message: address", any("private address" in b for b in both))
        want("message: trailer with --trailers", any("claude trailer" in b for b in both))
        want("message: trailer ignored without --trailers", not any("claude trailer" in b for b in plain))
        want("message: address without --trailers", any("private address" in b for b in plain))
        first = g("rev-list", "--max-parents=0", "HEAD").stdout.decode().strip()
        open(os.path.join(td, "leak.dat"), "wb").write(rom); g("add", "leak.dat"); g("commit", "-q", "-m", "adds a private copy")
        g("rm", "-q", "leak.dat"); g("commit", "-q", "-m", "removes it again")
        bad, nc, nb = check_commits([first + "..HEAD"], files, wins, td)
        want("commits mode: a file added and removed later is found", any("leak.dat" in b and "copy of the private file" in b for b in bad))
        want("commits mode: counts", nc == 4 and nb >= 1)
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
    ap.add_argument("--selftest", action="store_true"); ap.add_argument("--gen-private", metavar="SRC")
    ap.add_argument("--private", default=PRIVATE); ap.add_argument("--side", metavar="DIR")
    a = ap.parse_args()
    if a.selftest:
        return selftest()
    if a.gen_private:
        print("wrote %s: %d private binary files" % (a.private, gen_private(a.gen_private, a.private, a.side)))
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
