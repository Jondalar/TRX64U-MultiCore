#!/usr/bin/env python3
"""What a bench read (T174 D5): the record behind the key of a bench in ci/sims.sh.

ci/simreads.c (LD_PRELOAD) logs, for the whole process tree of a bench, one line per path below the repository root:
  <path>   a file opened for reading        ?<path>  a path looked for that does not exist        @<path>  a directory that was listed
This script turns such a log into a record and a record into a key:
  normalize LOG            the log reduced to what counts: tracked files, looked-for paths and listed directories that lie in tracked
                           directories (build outputs and the toolchain are not the repository's), sorted, once each
  lines BENCH              the record of BENCH: ci/sims.reads united with the runner's cache copy (--cache DIR)
  specs BENCH              its plain files (the pathspecs of the key); exit 3 when BENCH has no record
  extra BENCH              what the key adds besides file contents: whether each looked-for path is tracked now and the names tracked
                           directly in each listed directory
  missing BENCH LOG        lines of the normalized LOG that the record does not hold
  put BENCH                (stdin: normalized lines) replaces BENCH's block in ci/sims.reads
  cache BENCH              (stdin: normalized lines) united into the runner's cache copy of the record
  benches                  the benches that have a block in ci/sims.reads
Nothing here opens a file's content; keys hash contents through git (ci/lib.sh files_key)."""
import os, re, subprocess, sys

ROOT = os.environ.get("SIMS_ROOT") or os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
READS = os.environ.get("SIMS_READS_FILE") or os.path.join(ROOT, "ci", "sims.reads")
GLOB = re.compile(r"[*?\[\]\\]|^:")


def tracked():
    out = subprocess.run(["git", "-C", ROOT, "ls-files", "-z"], capture_output=True, check=True).stdout.decode("utf-8", "surrogateescape")
    files = set(x for x in out.split("\0") if x)
    dirs = set()
    for f in files:
        d = f
        while "/" in d:
            d = d.rsplit("/", 1)[0]
            if d in dirs:
                break
            dirs.add(d)
    return files, dirs


def parse(text):
    blocks, cur = {}, None
    for line in text.split("\n"):
        if not line.strip() or line.lstrip().startswith("#"):
            continue
        if line[0] not in " \t":
            cur = line.rstrip().rstrip(":")
            blocks.setdefault(cur, [])
        elif cur is not None:
            blocks[cur].append(line.strip())
    return blocks


def committed():
    try:
        return parse(open(READS, encoding="utf-8").read())
    except FileNotFoundError:
        return {}


def cache_file(bench, cache):
    return os.path.join(cache, bench) if cache else None


def cache_lines(bench, cache):
    p = cache_file(bench, cache)
    if p and os.path.isfile(p):
        return [l for l in open(p, encoding="utf-8").read().split("\n") if l]
    return []


def lines(bench, cache=None):
    b = committed().get(bench)
    c = cache_lines(bench, cache)
    if b is None and not c:
        return None
    return sorted(set(b or []) | set(c))


def pinned():
    """the paths ci/roms.txt pins: fetched, never tracked; a read of one is recorded as ci/roms.txt (T239.1 D12)"""
    try:
        return set(l.split()[2] for l in open(os.path.join(ROOT, "ci", "roms.txt"), encoding="utf-8").read().split("\n")
                   if l.strip() and not l.lstrip().startswith("#"))
    except FileNotFoundError:
        return set()


def normalize(log):
    files, dirs = tracked()
    pins = pinned()
    out = set()
    for l in open(log, encoding="utf-8", errors="surrogateescape").read().split("\n"):
        if not l:
            continue
        tag, p = (l[0], l[1:]) if l[0] in "?@" else ("", l)
        p = p.rstrip("/")
        if not p or GLOB.search(p):
            continue
        if p in pins:
            out.add("ci/roms.txt")
        elif tag == "" and p in files:
            out.add(p)
        elif tag == "?" and p not in files and os.path.dirname(p) in dirs:
            out.add("?" + p)
        elif tag == "@" and p in dirs:
            out.add("@" + p)
    return sorted(out)


def extra(rec):
    files, dirs = tracked()
    out = []
    for l in rec:
        if l.startswith("?"):
            out.append("absent %s %s" % (l[1:], "TRACKED" if l[1:] in files else "-"))
        elif l.startswith("@"):
            d = l[1:] + "/"
            names = sorted(set(f[len(d):].split("/", 1)[0] for f in files if f.startswith(d)))
            out.append("dir %s %s" % (l[1:], " ".join(names)))
    return out


def write_reads(blocks):
    head = ("# What each recorded bench read (T174 D5, ci/simreads.py, written by `ci/sims.sh --record NAME`): the files below are its key, and\n"
            "# ci/sims.deps says `@reads` for it. Lines: <file> a tracked file it opened | ?<path> looked for, not there (adding it changes the run)\n"
            "# | @<dir> listed (adding or removing a file there changes the run). A run that reads more extends the runner's copy of the record in\n"
            "# its cache, so the key follows the run; this file is what the plan and the lint see. Do not edit by hand.\n")
    with open(READS, "w", encoding="utf-8") as f:
        f.write(head)
        for b in sorted(blocks):
            f.write("%s:\n" % b)
            for l in sorted(blocks[b]):
                f.write("  %s\n" % l)


def main(a):
    cmd = a[0]
    cache = None
    if "--cache" in a:
        i = a.index("--cache"); cache = a[i + 1]; a = a[:i] + a[i + 2:]
    if cmd == "normalize":
        print("\n".join(normalize(a[1]))); return 0
    if cmd == "benches":
        print("\n".join(sorted(committed()))); return 0
    if cmd in ("lines", "specs", "extra"):
        r = lines(a[1], cache)
        if r is None:
            return 3
        if cmd == "lines":
            print("\n".join(r))
        elif cmd == "specs":
            print("\n".join(l for l in r if l[0] not in "?@"))
        else:
            print("\n".join(extra(r)))
        return 0
    if cmd == "missing":
        r = set(lines(a[1], cache) or [])
        print("\n".join(l for l in normalize(a[2]) if l not in r)); return 0
    if cmd == "put":
        blocks = committed()
        blocks[a[1]] = [l for l in sys.stdin.read().split("\n") if l]
        write_reads(blocks); return 0
    if cmd == "cache":
        p = cache_file(a[1], cache)
        new = sorted(set(cache_lines(a[1], cache)) | set(l for l in sys.stdin.read().split("\n") if l))
        os.makedirs(cache, exist_ok=True)
        tmp = p + ".tmp%d" % os.getpid()
        open(tmp, "w", encoding="utf-8").write("\n".join(new) + "\n")
        os.replace(tmp, p); return 0
    sys.stderr.write(__doc__); return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
