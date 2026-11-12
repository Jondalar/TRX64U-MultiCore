#!/usr/bin/env python3
"""Cache janitor (T87 D14/D15): what stays in the Vivado cache, by pin and by history.

An entry of $CI_BIT_CACHE (default /cache/multicore/bit) is a directory <group>-<16 hex>: host-<H>,
core-<core>-<C>, aux-<A>, c64u-<K>, ue2-<K>. It stays if
  1. a pin of a branch that still exists says so, or a permanent pin (main, integ-m1, v* tags),
  2. its DONE is younger than --days, or
  3. it is among the --history newest unpinned entries of its group.
A host also stays while a kept core entry was routed against it; a core entry whose host is gone
goes. The out-of-context checkpoints (ooc_*.dcp + .stub.v in $TRX_CACHE, default /cache/multicore) follow 2 and 3 per kind.
Over --cap-gb (entries + OOC files) the oldest UNPINNED entries go first, whatever the history rule
says; pinned entries are never dropped for the cap: if they alone are over it, the exit code is 2.
Hand-over directories of runs (runs/) older than 2 days go too.

  cache_gc.py [--dry-run] [--bit DIR] [--ooc DIR] [--runs DIR] [--cap-gb 30] [--days 14] [--history 4]
Branches that exist: env CI_GC_BRANCHES (a file, one name per line) or `git ls-remote --heads origin`;
when neither can be read nothing is deleted.
"""
import argparse, json, os, re, shutil, subprocess, sys, time

PERMANENT_BRANCHES = {"main", "integ-m1"}
ENTRY = re.compile(r"^(?P<group>.+)-(?P<key>[0-9a-f]{16})$")
OOC = re.compile(r"^(?P<group>ooc_.+)_(?P<key>[0-9a-f]{16})\.(?P<ext>dcp|stub\.v)$")


def du(path):
    if os.path.isfile(path):
        return os.path.getsize(path)
    n = 0
    for r, _, fs in os.walk(path):
        for f in fs:
            try:
                n += os.path.getsize(os.path.join(r, f))
            except OSError:
                pass
    return n


def branches():
    f = os.environ.get("CI_GC_BRANCHES")
    if f:
        return {l.strip().replace("/", "_") for l in open(f) if l.strip()}
    try:
        out = subprocess.run(["git", "ls-remote", "--heads", "origin"], capture_output=True, text=True, timeout=60, check=True).stdout
    except Exception:
        return None
    return {l.split("refs/heads/", 1)[1].strip().replace("/", "_") for l in out.splitlines() if "refs/heads/" in l}


def pin_state(entry, alive):
    """'permanent', 'branch' or None."""
    pd = os.path.join(entry, "pins")
    state = None
    for p in (os.listdir(pd) if os.path.isdir(pd) else []):
        if p.startswith("tag."):
            return "permanent"
        if p.startswith("branch."):
            slug = p[len("branch."):]
            if slug in PERMANENT_BRANCHES:
                return "permanent"
            if slug in alive:
                state = "branch"
    return state


def manifest(entry):
    try:
        return json.load(open(os.path.join(entry, "manifest.json")))
    except Exception:
        return {}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--dry-run", action="store_true")
    ap.add_argument("--bit", default=os.environ.get("CI_BIT_CACHE", "/cache/multicore/bit"))
    ap.add_argument("--ooc", default=os.environ.get("TRX_CACHE", "/cache/multicore"))
    ap.add_argument("--runs", default=None)
    ap.add_argument("--cap-gb", type=float, default=30.0)
    ap.add_argument("--days", type=float, default=14.0)
    ap.add_argument("--history", type=int, default=4)
    ap.add_argument("--now", type=float, default=None)
    a = ap.parse_args()
    now = a.now if a.now is not None else time.time()
    alive = branches()
    if alive is None:
        print("cache_gc: cannot read the branch list; deleting nothing")
        return 0
    young = now - a.days * 86400
    dead = []  # (path, reason)

    # --- entries
    ents = {}
    for name in sorted(os.listdir(a.bit)) if os.path.isdir(a.bit) else []:
        p = os.path.join(a.bit, name)
        if not os.path.isdir(p):
            continue
        if ".tmp." in name:
            if os.path.getmtime(p) < now - 86400:
                dead.append((p, "stale temporary directory"))
            continue
        m = ENTRY.match(name)
        if not m:
            continue
        done = os.path.join(p, "DONE")
        if not os.path.exists(done):
            if os.path.getmtime(p) < now - 86400:
                dead.append((p, "incomplete entry (no DONE)"))
            continue
        ents[name] = {"path": p, "group": m["group"], "mtime": os.path.getmtime(done), "pin": pin_state(p, alive), "size": du(p), "man": manifest(p)}

    keep = {}
    def decide(names):
        by_group = {}
        for n in names:
            by_group.setdefault(ents[n]["group"], []).append(n)
        for g, ns in by_group.items():
            ns.sort(key=lambda n: -ents[n]["mtime"])
            unpinned = [n for n in ns if not ents[n]["pin"]]
            newest = set(unpinned[: a.history])
            for n in ns:
                e = ents[n]
                if e["pin"]:
                    keep[n] = "pinned (%s)" % e["pin"]
                elif e["mtime"] >= young:
                    keep[n] = "younger than %g days" % a.days
                elif n in newest:
                    keep[n] = "among the %d newest of %s" % (a.history, g)
    hosts = {n: e for n, e in ents.items() if n.startswith("host-")}
    decide(list(hosts))
    # a pinned core entry keeps the host build it was routed against ...
    for n, e in ents.items():
        if n.startswith("core-") and e["pin"]:
            h = "host-" + str(e["man"].get("host_key"))
            if h in hosts:
                keep.setdefault(h, "host of pinned %s" % n)
            else:
                print("cache_gc: WARNING pinned %s has no host entry %s" % (n, h))
    # ... and an unpinned core entry goes with its host (it is only valid against that build)
    decide([n for n, e in ents.items() if not n.startswith("host-") and not n.startswith("core-")])
    decide([n for n, e in ents.items() if n.startswith("core-") and (e["pin"] or "host-" + str(e["man"].get("host_key")) in keep)])
    for n, e in ents.items():
        if n not in keep:
            if n.startswith("core-") and not e["pin"] and "host-" + str(e["man"].get("host_key")) not in keep:
                dead.append((e["path"], "its host entry is deleted or missing"))
            else:
                dead.append((e["path"], "not pinned, older than %g days, beyond the %d newest" % (a.days, a.history)))

    # --- out-of-context checkpoints
    oocs = {}
    for name in sorted(os.listdir(a.ooc)) if os.path.isdir(a.ooc) else []:
        m = OOC.match(name)
        if m:
            oocs.setdefault((m["group"], m["key"]), []).append(os.path.join(a.ooc, name))
    by_group = {}
    for (g, k), fs in oocs.items():
        by_group.setdefault(g, []).append((max(os.path.getmtime(f) for f in fs), k, fs))
    ooc_keep = {}
    for g, items in by_group.items():
        items.sort(reverse=True)
        for i, (mt, k, fs) in enumerate(items):
            if i < a.history or mt >= young:
                ooc_keep[(g, k)] = fs
            else:
                for f in fs:
                    dead.append((f, "out-of-context checkpoint beyond the %d newest, older than %g days" % (a.history, a.days)))

    # --- the cap
    dead_paths = {p for p, _ in dead}
    total = sum(e["size"] for n, e in ents.items() if e["path"] not in dead_paths) + sum(du(f) for fs in ooc_keep.values() for f in fs)
    cap = a.cap_gb * 1024 ** 3
    rc = 0
    if total > cap:
        cand = [(e["mtime"], e["path"], n) for n, e in ents.items() if e["path"] not in dead_paths and not e["pin"]]
        cand += [(os.path.getmtime(fs[0]), fs[0], None) for fs in ooc_keep.values()]
        cand.sort()
        for mt, p, n in cand:
            if total <= cap:
                break
            group = [p]
            if n is not None and n.startswith("host-"):
                group += [e["path"] for c, e in ents.items() if c.startswith("core-") and "host-" + str(e["man"].get("host_key")) == n and not e["pin"] and e["path"] not in dead_paths]
            for g in group:
                if g in dead_paths:
                    continue
                total -= du(g)
                dead.append((g, "size cap %g GB" % a.cap_gb))
                dead_paths.add(g)
        if total > cap:
            print("cache_gc: ERROR the pinned entries alone take %.1f GB, over the %g GB cap" % (total / 1024 ** 3, a.cap_gb))
            rc = 2

    # --- runs/
    if a.runs and os.path.isdir(a.runs):
        for name in sorted(os.listdir(a.runs)):
            p = os.path.join(a.runs, name)
            if os.path.isdir(p) and os.path.getmtime(p) < now - 2 * 86400:
                dead.append((p, "hand-over directory older than 2 days"))

    freed = 0
    for p, why in dead:
        sz = du(p) if os.path.exists(p) else 0
        freed += sz
        print("%s %s (%s, %.1f MB)" % ("would delete" if a.dry_run else "delete", p, why, sz / 1e6))
        if not a.dry_run:
            if os.path.isdir(p):
                shutil.rmtree(p, ignore_errors=True)
            elif os.path.exists(p):
                os.remove(p)
    print("cache_gc: %d kept entries, %d removed (%.1f MB), cache now about %.2f GB of %g GB" % (len(keep), len(dead), freed / 1e6, total / 1024 ** 3, a.cap_gb))
    return rc


if __name__ == "__main__":
    sys.exit(main())
