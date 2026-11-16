#!/usr/bin/env python3
"""T213 D4 / T192 D24: the per-core code gate. Core-specific code and data do not grow in the generic core app. A ratchet over tools/d24_ledger.txt
(the baseline of 2026-11-16). Red when

  P1  a file appears in coreapp/cores/ that the ledger does not list
  P2  a ledger file has more code lines than its ledger value (+ the allowance of an `exception` line for it)
  P3  a core id (cores/**/core.toml `id`) or a machine word appears as a string literal in generic code (coreapp/src files that are not in the ledger, uicore/)
      more often than the ledger's `ids` line says; only the new ones fail
  P4  data generated from cores/ (a generated file whose generator names cores/, or a file in coreapp/cores/ included by generic code) sits in the app
      image outside the ledger: a new .inc/.h of that kind is red, a ledger one is counted like any other file
  P5  an exception line names a file that is not in the ledger (stale)

Code lines = lines left after comments are cut (C-like: // and /* */, string and char literals respected; toml/python: #; 6502 asm: ;) and blanks dropped.
This is a small lexer of its own (ci/code_hash.py needs tree-sitter, which the lint image does not carry); only the baseline has to agree with itself.
Shrinking is fine: "ledger can shrink: file N -> M" is printed, the ledger is never edited by CI (--write-ledger prints a fresh one).

Ledger lines:   file <path> <code lines>
                ids <path> <id> <count>
                exception <path> +<lines> <spec ref> (<decision>)      the file may hold that many lines more; every run prints it

Usage: tools/check_d24.py [--root DIR] [--ledger FILE]   exit 1 and a line per breach
       tools/check_d24.py --selftest                      plants each breach in a copy of the tree; each must be reported, the real tree must be green
       tools/check_d24.py --write-ledger [--root DIR]     prints the file/ids lines for the tree (exceptions are kept by hand)
       tools/check_d24.py --measure DIR                   prints, for another tree (a branch worktree), what it adds against the ledger"""
import os
import re
import shutil
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT0 = os.path.abspath(os.path.join(HERE, ".."))
LEDGER0 = os.path.join(HERE, "d24_ledger.txt")

# Core-specific files of coreapp/src, decided by name and content on 2026-11-16: the monitor and its server (the TRX64 bus monitor, trace ring), the trace ring
# files, the TRX64U ops and menu glue, the U64 configuration (U64 register map and resampler), the Port Diagnostics tests (diag core), the Amiga filesystem,
# the C64 tape container. Everything else in coreapp/src is generic (menu, files, network, update ...) and is scanned for core ids.
SRC_PER_CORE = [
    r"coreapp/src/trx64u_ops\.(cc|h)", r"coreapp/src/ring[a-z]*\.(cc|h)", r"coreapp/src/trx_(c64re|ring)\.h",
    r"coreapp/src/monitor\.(cc|h)", r"coreapp/src/monsrv\.(cc|h)", r"coreapp/src/u64cfg\.(cc|h)", r"coreapp/src/u64_resample\.inc",
    r"coreapp/src/diagtest\.(cc|h)", r"coreapp/src/amiga_ffs\.(cc|h)", r"coreapp/src/t64\.(cc|h)", r"coreapp/src/trxmon/.*",
]
GENERIC_DIRS = ["coreapp/src", "uicore"]
CODE_EXT = (".cc", ".c", ".h", ".inc", ".asm", ".toml", ".py")
QUIET = False
EXTRA_WORDS = ["c64", "c64u", "vic20", "spectrum", "atari", "trx64u"]


def strip_comments(text, ext):
    """text -> text without comments (strings kept)."""
    out, i, n = [], 0, len(text)
    if ext in (".toml", ".py", ".asm"):
        mark = ";" if ext == ".asm" else "#"
        for line in text.split("\n"):
            q, j = None, 0
            while j < len(line):
                c = line[j]
                if q:
                    if c == "\\" and ext != ".asm":
                        j += 1
                    elif c == q:
                        q = None
                elif c in "\"'":
                    q = c
                elif c == mark:
                    line = line[:j]
                    break
                j += 1
            out.append(line)
        return "\n".join(out)
    while i < n:
        c = text[i]
        two = text[i:i + 2]
        if two == "//":
            j = text.find("\n", i)
            while j > 0 and text[j - 1] == "\\":
                j = text.find("\n", j + 1)
            i = n if j < 0 else j
        elif two == "/*":
            j = text.find("*/", i + 2)
            j = n if j < 0 else j + 2
            out.append(" " + "\n" * text.count("\n", i, j))
            i = j
        elif c == '"' or c == "'":
            j = i + 1
            while j < n and text[j] != c and text[j] != "\n":
                j += 2 if text[j] == "\\" else 1
            out.append(text[i:j + 1])
            i = j + 1
        else:
            out.append(c)
            i += 1
    return "".join(out)


def read(path):
    with open(path, "rb") as f:
        return f.read().decode("utf-8", "replace")


def code_lines(path):
    ext = os.path.splitext(path)[1].lower()
    if ext not in CODE_EXT:
        return len([1 for l in read(path).split("\n") if l.strip()])
    return len([1 for l in strip_comments(read(path), ext).split("\n") if l.strip()])


def walk(root, sub):
    base = os.path.join(root, sub)
    for d, _, fs in os.walk(base):
        for f in sorted(fs):
            yield os.path.relpath(os.path.join(d, f), root)


def per_core_files(root):
    """Every file of coreapp/cores plus the core-specific files of coreapp/src."""
    res = set(walk(root, "coreapp/cores"))
    pats = [re.compile(p + r"$") for p in SRC_PER_CORE]
    for p in walk(root, "coreapp/src"):
        if any(r.match(p) for r in pats):
            res.add(p)
    return sorted(res)


def core_words(root):
    ids = set(EXTRA_WORDS)
    for d, _, fs in os.walk(os.path.join(root, "cores")):
        if "core.toml" in fs:
            m = re.search(r'^id\s*=\s*"([^"]+)"', read(os.path.join(d, "core.toml")), re.M)
            if m:
                ids.add(m.group(1))
    return sorted(ids)


STR = re.compile(r'"(?:[^"\\\n]|\\.)*"')


def id_counts(root, files, words):
    """{(path, word): count} of string literals holding the word (not touching letters or digits) in the generic files."""
    res = {}
    rx = {w: re.compile(r"(?<![A-Za-z0-9])" + re.escape(w) + r"(?![A-Za-z0-9])", re.I) for w in words}
    for p in files:
        s = strip_comments(read(os.path.join(root, p)), os.path.splitext(p)[1].lower())
        lits = STR.findall(s)
        for w, r in rx.items():
            n = sum(len(r.findall(l)) for l in lits)
            if n:
                res[(p, w)] = n
    return res


def generic_files(root, pc):
    out = []
    for g in GENERIC_DIRS:
        for p in walk(root, g):
            if p not in pc and p.endswith((".cc", ".c", ".h", ".inc")):
                out.append(p)
    return sorted(out)


def generated_from_cores(root, p):
    """A generated file whose generator names cores/ (the header line the generator writes is the only handle; the check is on the file's first lines)."""
    head = "\n".join(read(os.path.join(root, p)).split("\n")[:3])
    return bool(re.search(r"[Gg]enerated by .*\bcores/", head))


def parse_ledger(path):
    files, ids, exc = {}, {}, []
    for ln in read(path).split("\n"):
        ln = ln.strip()
        if not ln or ln.startswith("#"):
            continue
        t = ln.split()
        if t[0] == "file":
            files[t[1]] = int(t[2])
        elif t[0] == "ids":
            ids[(t[1], t[2])] = int(t[3])
        elif t[0] == "exception":
            m = re.match(r"exception (\S+) \+(\d+) (\S+) \((.+)\)\s*$", ln)
            if not m:
                raise SystemExit("ledger: malformed exception line: " + ln)
            exc.append((m.group(1), int(m.group(2)), m.group(3), m.group(4)))
        else:
            raise SystemExit("ledger: unknown line: " + ln)
    return files, ids, exc


def current(root):
    pc = per_core_files(root)
    files = {p: code_lines(os.path.join(root, p)) for p in pc}
    ids = id_counts(root, generic_files(root, set(pc)), core_words(root))
    return pc, files, ids


def check(root, ledger):
    lf, li, exc = parse_ledger(ledger)
    pc, files, ids = current(root)
    bad = []
    allow = {}
    for p, n, spec, dec in exc:
        allow[p] = allow.get(p, 0) + n
        if not QUIET:
            print("exception: %s +%d lines, %s (%s)" % (p, n, spec, dec))
        if p not in lf:
            bad.append("P5 exception for a file that is not in the ledger: %s" % p)
    for p in pc:
        if p not in lf:
            if p.startswith("coreapp/cores/"):
                bad.append("P1 new file in coreapp/cores/: %s (core-specific code belongs in the core's TOML, a service module or the core's .cor; T192 D24)" % p)
            else:
                bad.append("P1 new per-core file: %s" % p)
        elif files[p] > lf[p] + allow.get(p, 0):
            bad.append("P2 %s grew: %d code lines, ledger %d%s" % (p, files[p], lf[p], " + exception %d" % allow[p] if p in allow else ""))
        elif files[p] < lf[p]:
            if not QUIET:
                print("ledger can shrink: %s %d -> %d" % (p, lf[p], files[p]))
    for p in lf:
        if p not in pc:
            print("ledger can shrink: %s %d -> 0 (file gone, delete its line)" % (p, lf[p]))
    for (p, w), n in sorted(ids.items()):
        if n > li.get((p, w), 0):
            bad.append("P3 core name/id %r in a string literal of generic code %s: %d now, baseline %d" % (w, p, n, li.get((p, w), 0)))
    for (p, w), n in sorted(li.items()):
        if ids.get((p, w), 0) < n:
            print("ledger can shrink: ids %s %s %d -> %d" % (p, w, n, ids.get((p, w), 0)))
    for g in GENERIC_DIRS:
        for p in walk(root, g):
            if p not in lf and p.endswith((".cc", ".c", ".h", ".inc")) and generated_from_cores(root, p):
                bad.append("P4 data generated from cores/ is embedded in the app image: %s" % p)
    for g in GENERIC_DIRS:
        for p in walk(root, g):
            if p in lf or not p.endswith((".cc", ".c", ".h", ".inc")):
                continue
            for m in re.finditer(r'#\s*include\s+"([^"]+)"', strip_comments(read(os.path.join(root, p)), ".cc")):
                inc = m.group(1)
                if inc.startswith("../cores/") or inc.startswith("cores/") or os.path.basename(inc) in {os.path.basename(x) for x in pc if x.startswith("coreapp/cores/")}:
                    if os.path.basename(inc) not in {os.path.basename(x) for x in lf}:
                        bad.append("P4 %s includes core data %s that the ledger does not list" % (p, inc))
    return bad


def write_ledger(root):
    pc, files, ids = current(root)
    for p in pc:
        print("file %s %d" % (p, files[p]))
    for (p, w), n in sorted(ids.items()):
        print("ids %s %s %d" % (p, w, n))


def measure(other, ledger):
    lf, li, _ = parse_ledger(ledger)
    pc, files, ids = current(other)
    for p in pc:
        if p not in lf:
            print("NEW FILE %s %d lines" % (p, files[p]))
        elif files[p] != lf[p]:
            print("%+d  %s (%d -> %d)" % (files[p] - lf[p], p, lf[p], files[p]))
    for p in lf:
        if p not in pc:
            print("GONE %s (-%d)" % (p, lf[p]))
    for (p, w), n in sorted(ids.items()):
        if n > li.get((p, w), 0):
            print("NEW ID %s %r %d -> %d" % (p, w, li.get((p, w), 0), n))
    for g in GENERIC_DIRS:
        for p in walk(other, g):
            if p not in lf and p.endswith((".cc", ".c", ".h", ".inc")) and generated_from_cores(other, p):
                print("EMBEDDED %s" % p)


def selftest():
    global QUIET
    QUIET = True
    fails = []
    with tempfile.TemporaryDirectory() as t:
        tree = os.path.join(t, "tree")
        os.makedirs(os.path.join(tree, "coreapp"))
        os.makedirs(os.path.join(tree, "uicore"))
        shutil.copytree(os.path.join(ROOT0, "coreapp"), os.path.join(tree, "coreapp"), dirs_exist_ok=True)
        shutil.copytree(os.path.join(ROOT0, "uicore"), os.path.join(tree, "uicore"), dirs_exist_ok=True)
        shutil.copytree(os.path.join(ROOT0, "cores"), os.path.join(tree, "cores"), dirs_exist_ok=True,
                        ignore=lambda d, fs: [f for f in fs if f not in ("core.toml",) and not os.path.isdir(os.path.join(d, f))])
        led = LEDGER0
        out = check(tree, led)
        if out:
            fails.append("the real tree is not green: " + out[0])

        def expect(tag, mutate, undo):
            mutate()
            r = check(tree, led)
            undo()
            if not any(x.startswith(tag) for x in r):
                fails.append("%s not reported" % tag)
        newf = os.path.join(tree, "coreapp/cores/zz_new.cc")
        expect("P1", lambda: open(newf, "w").write("int x;\n"), lambda: os.remove(newf))
        grow = os.path.join(tree, "coreapp/cores/snes.cc")
        orig = read(grow)
        expect("P2", lambda: open(grow, "a").write("int zz_grown_line;\n"), lambda: open(grow, "w").write(orig))
        gen = os.path.join(tree, "coreapp/src/svc.cc")
        orig2 = read(gen)
        expect("P3", lambda: open(gen, "a").write('static const char *zz = "snes";\n'), lambda: open(gen, "w").write(orig2))
        emb = os.path.join(tree, "coreapp/src/zz_data.inc")
        expect("P4", lambda: open(emb, "w").write("// Generated by tools/gen_x.py from cores/foo/data - do not edit\nstatic int d[1];\n"),
               lambda: os.remove(emb))
        # a comment must not count
        open(grow, "a").write("// a long comment\n/* block\n comment */\n")
        if check(tree, led):
            fails.append("a comment counted as code")
        open(grow, "w").write(orig)
        if check(tree, led):
            fails.append("the tree is not green after the controls")
    return fails


def main(argv):
    root, ledger = ROOT0, LEDGER0
    if "--root" in argv:
        root = os.path.abspath(argv[argv.index("--root") + 1])
    if "--ledger" in argv:
        ledger = os.path.abspath(argv[argv.index("--ledger") + 1])
    if "--selftest" in argv:
        f = selftest()
        for x in f:
            print("SELFTEST FAIL", x)
        return 1 if f else 0
    if "--write-ledger" in argv:
        write_ledger(root)
        return 0
    if "--measure" in argv:
        measure(os.path.abspath(argv[argv.index("--measure") + 1]), ledger)
        return 0
    bad = check(root, ledger)
    for b in bad:
        print("D24: " + b)
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
