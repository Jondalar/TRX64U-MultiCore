#!/usr/bin/env python3
"""T239.1 D11, D13, D19: the work order. The plan job asks which gateware entries this run needs and which of them the builds repository does not hold yet.

  ci/order.py
    env  PLAN_JSON_FILE   the run's plan.json (checked to be one)
         WORK_ORDER_OUT   where work-order.json goes
         GITHUB_OUTPUT    gets `missing=true|false` (optional)
         CHILDREN         JSON list of the children the plan asked for (the step `children` output); COR_TARGETS the JSON list of the cor child's targets.
                          Without CHILDREN every entry is needed.
         GH_TOKEN         optional: only lifts the rate limit of the read (the builds repository is public)
         MC_BUILDS_REPO   default from ci/pkg.env; BUILDS_API default https://api.github.com
         ORDER_SHA        the commit of the run (default: HEAD of the checkout)

The entries are the cache entry names ci/keys.sh prints (host-<H>, aux-<A>, core-<id>-<C>, mono-<id>-<K>); the release of an entry is `bit-<entry>` (D19). An entry is
missing when that release does not exist or is still a draft. work-order.json is `{"version": 1, "sha": ..., "lanes": [...], "entries": [...]}`; entries lists the missing
ones, in the order of keys.sh, and the builder reads nothing else of it (builder pull/order.py). An empty list means no gateware is built for this run.

What a run needs: a package (or update) child needs every entry, because the package carries every core; a feature branch's cor child needs the host, the
reference core and its core (or the flat bitstream of a large core); the target `host` (T265 P5) the host, aux, the reference core and the flash filler; a run with neither needs none.
"""
import json
import os
import re
import subprocess
import sys
import urllib.error
import urllib.parse
import urllib.request

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
# the same shape the builder accepts (builder pull/order.py ENTRY)
ENTRY = re.compile(r"^(host|aux|core-[A-Za-z0-9_]+(?:-[A-Za-z0-9_]+)*|mono-[A-Za-z0-9_]+(?:-[A-Za-z0-9_]+)*)-([0-9a-f]{16})$")


class OrderError(Exception):
    pass


def env_file(path):
    d = {}
    for line in open(path):
        m = re.match(r"^(\w+)=(\S+)", line)
        if m:
            d[m.group(1)] = m.group(2)
    return d


def reference_core():
    for line in open(os.path.join(ROOT, "host", "layers.txt")):
        m = re.match(r"^reference:\s*(\S+)", line)
        if m:
            return m.group(1)
    raise OrderError("host/layers.txt names no reference core")


def entry_names():
    """The entry names of this tree, in the order of ci/keys.sh."""
    override = os.environ.get("ORDER_KEYS")
    if override:
        text = open(override).read()
    else:
        r = subprocess.run([os.path.join(HERE, "keys.sh")], capture_output=True, text=True)
        if r.returncode != 0:
            raise OrderError("ci/keys.sh failed: " + r.stderr.strip()[-300:])
        text = r.stdout
    names = [l.strip() for l in text.splitlines() if l.strip()]
    for n in names:
        if not ENTRY.match(n):
            raise OrderError("not an entry name: %r" % n[:60])
    if len(set(names)) != len(names):
        raise OrderError("ci/keys.sh printed an entry twice")
    return names


def flash_fillers():
    """T265 P5: the cores whose bitstream the host's flash image carries (core.toml flash_filler; the entry core-<id>-*). ORDER_FILLERS (tests) replaces the read."""
    override = os.environ.get("ORDER_FILLERS")
    if override is not None:
        return override.split()
    r = subprocess.run([sys.executable, "-I", os.path.join(ROOT, "tools", "core_toml.py"), "flash-fillers"], capture_output=True, text=True)
    if r.returncode != 0:
        raise OrderError("tools/core_toml.py flash-fillers failed: " + r.stderr.strip()[-300:])
    return r.stdout.split()


def group_of(entry):
    return ENTRY.match(entry).group(1)


def needed(names, children, cor_targets, ref):
    """The entries this run needs (see the module text)."""
    if children is None or "package" in children or "update" in children:
        return list(names)
    want = set()
    for t in cor_targets:
        if t == "host":       # T265 P5: the host gateware, its aux designs, the reference core and the flash filler
            want |= {"host", "aux", "core-" + ref} | {"core-" + c for c in flash_fillers()}
        elif t.startswith("core:"):
            want |= {"host", "core-" + ref, "core-" + t[5:]}
        elif t.startswith("large:"):
            want.add("mono-" + t[6:])
        else:
            raise OrderError("cor target %r" % t[:40])
    return [n for n in names if group_of(n) in want]


def lane_of(entry, ref):
    g = group_of(entry)
    if g in ("host", "aux") or g == "core-" + ref:
        return "host"
    if g.startswith("core-"):
        return "core:" + g[5:]
    return "large:" + g[5:]


class Releases:
    def __init__(self, base, repo, token):
        self.base, self.repo, self.token = base.rstrip("/"), repo, token

    def published(self, tag):
        url = "%s/repos/%s/releases/tags/%s" % (self.base, self.repo, urllib.parse.quote(tag))
        hdr = {"Accept": "application/vnd.github+json", "User-Agent": "trx64-order"}
        if self.token:
            hdr["Authorization"] = "Bearer " + self.token
        last = None
        for attempt in range(3):
            try:
                with urllib.request.urlopen(urllib.request.Request(url, headers=hdr), timeout=30) as r:
                    return not json.load(r).get("draft", False)
            except urllib.error.HTTPError as e:
                if e.code == 404:
                    return False
                last = "HTTP %d" % e.code
                if e.code < 500 and e.code != 429:
                    break
            except (urllib.error.URLError, OSError) as e:
                last = str(e)
        raise OrderError("%s: cannot read the release %s (%s)" % (self.repo, tag, last))


def main():
    plan = os.environ.get("PLAN_JSON_FILE", "")
    out = os.environ.get("WORK_ORDER_OUT", "")
    if not plan or not out:
        raise OrderError("PLAN_JSON_FILE and WORK_ORDER_OUT are needed")
    try:
        if json.load(open(plan)).get("version") != 1:
            raise ValueError("version")
    except (OSError, ValueError, AttributeError):
        raise OrderError("%s is no plan.json" % plan)
    sha = os.environ.get("ORDER_SHA") or subprocess.run(["git", "-C", ROOT, "rev-parse", "HEAD"], capture_output=True, text=True, check=True).stdout.strip()
    pkg = env_file(os.path.join(HERE, "pkg.env"))
    repo = os.environ.get("MC_BUILDS_REPO") or pkg["MC_BUILDS_REPO"]
    children = os.environ.get("CHILDREN")
    children = json.loads(children) if children else None
    cors = json.loads(os.environ.get("COR_TARGETS") or "[]")
    ref = reference_core()
    names = needed(entry_names(), children, cors, ref)
    rel = Releases(os.environ.get("BUILDS_API", "https://api.github.com"), repo, os.environ.get("GH_TOKEN", ""))
    missing = [n for n in names if not rel.published("bit-" + n)]
    lanes = []
    for n in missing:
        ln = lane_of(n, ref)
        if ln not in lanes:
            lanes.append(ln)
    json.dump({"version": 1, "sha": sha, "lanes": lanes, "entries": missing}, open(out, "w"), indent=2)
    print("work order: %d of %d needed entries are missing: %s" % (len(missing), len(names), " ".join(missing) or "none"))
    if os.environ.get("GITHUB_OUTPUT"):
        with open(os.environ["GITHUB_OUTPUT"], "a") as f:
            f.write("missing=%s\n" % ("true" if missing else "false"))
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except OrderError as e:
        print("ci/order.py: %s" % e, file=sys.stderr)
        sys.exit(1)
