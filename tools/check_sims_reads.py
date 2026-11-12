#!/usr/bin/env python3
"""T174 D5 (Alex, 2026-11-08: "Segment the caches better"): a bench that has a record in ci/sims.reads is keyed by that record and by nothing else.

  tools/check_sims_reads.py             check the tree
  tools/check_sims_reads.py --selftest  the check against generated trees: a clean one passes, each way the coarse key could come back fails

Fails when
  - a recorded bench has anything but `@reads` in its line of ci/sims.deps (a directory macro such as `@a8`, a directory, a glob: the coarse key
    would be back, and the record only a comment beside it),
  - a bench says `@reads` and has no (or an empty) record,
  - a record names a file that is no longer tracked (the key of that bench would fall back to the whole tree) or a path that is a directory,
  - ci/sims.reads names a bench that is not in ci/sims.txt."""
import os, subprocess, sys, tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, os.path.join(ROOT, "ci"))
import plan, simreads  # noqa: E402


def raw_lines(text):
    """bench -> the raw tokens of its line in ci/sims.deps (macros NOT resolved)."""
    out, cur = {}, None
    for line in text.split("\n"):
        if not line.strip() or line.lstrip().startswith("#"):
            continue
        if line[0] in " \t":
            if cur is not None:
                out[cur] += line.split()
            continue
        key = line.split()[0]
        if key.startswith("@"):
            cur = None
            continue
        key = key.rstrip(":")
        out[key], cur = line[line.index(":") + 1:].split(), key
    return out


def check(deps_text, reads_text, benches, tracked):
    errs = []
    raw = raw_lines(deps_text)
    rec = simreads.parse(reads_text)
    for b, lines in sorted(rec.items()):
        if b not in benches:
            errs.append("ci/sims.reads has a record for %s, which is no bench of ci/sims.txt" % b)
        if not lines:
            errs.append("%s: its record in ci/sims.reads is empty" % b)
        toks = raw.get(b)
        if toks is None:
            errs.append("%s: has a record and no line in ci/sims.deps (the line is `%s: @reads`)" % (b, b))
        elif toks != ["@reads"]:
            errs.append("%s: has a record in ci/sims.reads, so its line in ci/sims.deps must be `@reads` only; it still has: %s" % (b, " ".join(t for t in toks if t != "@reads")))
        for l in lines:
            p = l.lstrip("?@")
            if l[0] not in "?@" and p not in tracked:
                errs.append("%s: the record names %s, which is not a tracked file (re-record: ci/sims.sh --record %s)" % (b, p, b))
    for b, toks in sorted(raw.items()):
        if "@reads" in toks and not rec.get(b):
            errs.append("%s: says @reads and has no record in ci/sims.reads" % b)
    return errs


def tree_check():
    tracked = set(subprocess.run(["git", "-C", ROOT, "ls-files", "-z"], capture_output=True, check=True).stdout.decode().split("\0"))
    sims = plan.parse_sims(open(os.path.join(ROOT, "ci", "sims.txt"), encoding="utf-8").read())
    rp = os.path.join(ROOT, "ci", "sims.reads")
    return check(open(os.path.join(ROOT, "ci", "sims.deps"), encoding="utf-8").read(),
                 open(rp, encoding="utf-8").read() if os.path.exists(rp) else "", set(sims), tracked)


def selftest():
    deps = "@a8 = cores/atari800 vendor/icepi\nbench_a: @reads\nbench_b: @a8\nbench_c: @reads\n"
    reads = "bench_a:\n  host/a.v\n  ?host/x.v\n  @host\nbench_c:\n  host/c.v\n"
    tracked = {"host/a.v", "host/c.v", "host/x2.v"}
    benches = {"bench_a", "bench_b", "bench_c"}
    fails = []

    def expect(name, d, r, t, must):
        errs = check(d, r, benches, t)
        got = bool(errs)
        print("selftest %-34s %s%s" % (name, "FAIL as required" if got else "pass", ": " + errs[0] if errs else ""))
        if got != must:
            fails.append(name)
    expect("clean tree", deps, reads, tracked, False)
    expect("directory macro beside a record", deps.replace("bench_a: @reads", "bench_a: @a8"), reads, tracked, True)
    expect("directory spec next to @reads", deps.replace("bench_c: @reads", "bench_c: @reads cores/atari800"), reads, tracked, True)
    expect("@reads without a record", deps, reads.split("bench_c:")[0], tracked, True)
    expect("record without a deps line", deps.replace("bench_c: @reads\n", ""), reads, tracked, True)
    expect("record names an untracked file", deps, reads, tracked - {"host/c.v"}, True)
    expect("record of an unknown bench", deps, reads + "bench_z:\n  host/a.v\n", tracked, True)
    expect("bench without a record keeps its macro", deps.replace("bench_c: @reads", "bench_c: @a8"), reads.split("bench_c:")[0], tracked, False)
    if fails:
        print("selftest FAILED: " + ", ".join(fails))
        return 1
    print("selftest ok")
    return 0


if __name__ == "__main__":
    if "--selftest" in sys.argv:
        sys.exit(selftest())
    errs = tree_check()
    for e in errs:
        print("FAIL " + e)
    sys.exit(1 if errs else 0)
