#!/usr/bin/env python3
"""T239.1 D19, S5, S8: restore cache entries of $CI_BIT_CACHE from the bit-<entry> releases of the builds repository, so ci/collect.sh, ci/pack_one.sh and
ci/bitstream*.sh read them as they read the builder's cache.

  ci/fetch_bits.sh [core:<id> | large:<id>]    no argument: every entry of ci/keys.sh and of ci/imports.py (imp-<id>-<key>: trx64u.cor, trx64g.cor, D43);
                                               core:<id>: the host, the reference core and that core; large:<id>: its flat entry
  ci/fetch_bits.sh --names [core:<id> | large:<id>]   print the entry names only (the collect job keys GitHub's cache with them, D40)
    env  CI_BIT_CACHE   the directory the entries are restored into (default $RUNNER_TEMP/bit)
         CI_HANDOFF     gets bit-releases.json: per entry the release tag and id and the SHA-256 of SHA256SUMS and of every asset (ci/package.sh puts it into manifest.json)
         MC_BUILDS_REPO default from ci/pkg.env; BUILDS_API default https://api.github.com; GH_TOKEN optional (rate limit only; the assets are public)
         FETCH_KEYS     a file with the entry names, instead of ci/keys.sh (tests; the collect job passes the list it printed with --names)

Per entry, nothing is trusted before it has passed these checks: the release is published (not a draft); it has SHA256SUMS; every asset has the SHA-256 that GitHub lists
for it (`digest`) and the one SHA256SUMS lists, and SHA256SUMS lists no asset that is missing; reports.tar.gz holds plain files with plain names only; manifest.json (when
the entry has one) lists files whose SHA-256 are the ones restored - a *.dcp is the one kind that may be absent (the host's static_routed.dcp is never published, the host
checkpoint stays in the builder's cache); host.bit, when present, has the SHA-256 the manifest names as host_sha; every core entry names the host_sha of the host entry fetched with
it. The directory is made beside the cache and renamed into place with DONE as its last file, so a failed fetch leaves no entry.

D40: the collect job restores $CI_BIT_CACHE from GitHub's cache. An entry found there is used again when it has DONE, its record in .releases/<entry>.json
(written after its fetch) and every asset file the record lists with that SHA-256 (reports.tar.gz is unpacked at the fetch, so it is not on disk); any other entry
is fetched. Without a target, the entries that are not in the list are removed with their records, so the cache holds one set.
"""
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
import tarfile
import tempfile
import urllib.error
import urllib.parse
import urllib.request

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
ENTRY = re.compile(r"^(host|aux|core-[A-Za-z0-9_]+(?:-[A-Za-z0-9_]+)*|mono-[A-Za-z0-9_]+(?:-[A-Za-z0-9_]+)*|imp-[A-Za-z0-9_]+)-([0-9a-f]{16})$")
NAME_OK = re.compile(r"^[A-Za-z0-9][A-Za-z0-9._+-]{0,99}$")
MAX_FILE = 1900 * 1024 * 1024
MAX_MEMBERS = 200


class FetchError(Exception):
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
    raise FetchError("host/layers.txt names no reference core")


def entry_names():
    override = os.environ.get("FETCH_KEYS")
    if override:
        text = open(override).read()
    else:
        r = subprocess.run([os.path.join(HERE, "keys.sh")], capture_output=True, text=True)
        if r.returncode != 0:
            raise FetchError("ci/keys.sh failed: " + r.stderr.strip()[-300:])
        text = r.stdout
        # T239.1 D43: trx64u.cor and trx64g.cor, packed by the builder from release/imports/*.pin (ci/imports.py); a target never selects them
        r = subprocess.run([sys.executable, "-I", os.path.join(HERE, "imports.py"), "names"], capture_output=True, text=True)
        if r.returncode != 0:
            raise FetchError("ci/imports.py names failed: " + r.stderr.strip()[-300:])
        text += r.stdout
    names = [l.strip() for l in text.splitlines() if l.strip()]
    for n in names:
        if not ENTRY.match(n):
            raise FetchError("not an entry name: %r" % n[:60])
    return names


def select(names, target, ref):
    if not target:
        return list(names)
    if target.startswith("core:"):
        want = {"host", "core-" + ref, "core-" + target[5:]}
    elif target.startswith("large:"):
        want = {"mono-" + target[6:]}
    else:
        raise FetchError("target is core:<id> or large:<id>, not %r" % target[:40])
    out = [n for n in names if ENTRY.match(n).group(1) in want]
    if len(out) != len(want):
        raise FetchError("ci/keys.sh has no entry for %s (found %s)" % (target, " ".join(out) or "none"))
    return out


class Api:
    def __init__(self, base, repo, token):
        self.base, self.repo, self.token = base.rstrip("/"), repo, token

    def _open(self, url, headers, tries=3):
        last = None
        for _ in range(tries):
            try:
                return urllib.request.urlopen(urllib.request.Request(url, headers=headers), timeout=60)
            except urllib.error.HTTPError as e:
                if e.code == 404:
                    raise
                last = "HTTP %d" % e.code
                if e.code < 500 and e.code != 429:
                    raise FetchError("%s: %s" % (url.split("?")[0], last))
            except (urllib.error.URLError, OSError) as e:
                last = str(e)
        raise FetchError("%s: %s" % (url.split("?")[0], last))

    def release(self, tag):
        hdr = {"Accept": "application/vnd.github+json", "User-Agent": "trx64-fetch"}
        if self.token:
            hdr["Authorization"] = "Bearer " + self.token
        try:
            with self._open("%s/repos/%s/releases/tags/%s" % (self.base, self.repo, urllib.parse.quote(tag)), hdr) as r:
                return json.load(r)
        except urllib.error.HTTPError:
            raise FetchError("no release %s in %s: the builder has not published this entry" % (tag, self.repo))

    def download(self, url, dest, limit=MAX_FILE):
        """The file at url (a public asset address: no credential is sent) into dest; returns its SHA-256."""
        h = hashlib.sha256()
        n = 0
        with self._open(url, {"User-Agent": "trx64-fetch"}) as r, open(dest, "wb") as f:
            while True:
                b = r.read(1 << 20)
                if not b:
                    break
                n += len(b)
                if n > limit:
                    raise FetchError("%s is larger than %d MiB" % (url.rsplit("/", 1)[-1], limit >> 20))
                h.update(b)
                f.write(b)
        return h.hexdigest()


def parse_sums(path):
    sums = {}
    for line in open(path):
        m = re.match(r"^([0-9a-f]{64})  (\S+)$", line.rstrip("\n"))
        if not m or m.group(2) in sums:
            raise FetchError("SHA256SUMS is malformed: %r" % line[:80])
        sums[m.group(2)] = m.group(1)
    return sums


def sha256_file(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for b in iter(lambda: f.read(1 << 20), b""):
            h.update(b)
    return h.hexdigest()


def fetch_entry(api, entry, cache):
    """Restore one entry; returns (record for bit-releases.json, manifest or None)."""
    tag = "bit-" + entry
    rel = api.release(tag)
    if rel.get("draft"):
        raise FetchError("%s is a draft" % tag)
    assets = {a["name"]: a for a in rel.get("assets", [])}
    if "SHA256SUMS" not in assets:
        raise FetchError("%s has no SHA256SUMS" % tag)
    if len(assets) > MAX_MEMBERS:
        raise FetchError("%s: too many assets" % tag)
    for n in assets:
        if n != "SHA256SUMS" and not NAME_OK.match(n):
            raise FetchError("%s: asset name %r" % (tag, n[:60]))
    final = os.path.join(cache, entry)
    tmp = tempfile.mkdtemp(prefix=entry + ".tmp.", dir=cache)
    try:
        sums_path = os.path.join(tmp, ".SHA256SUMS")
        sums_sha = api.download(assets["SHA256SUMS"]["browser_download_url"], sums_path, limit=1 << 20)
        if sums_sha != (assets["SHA256SUMS"].get("digest") or "")[7:]:
            raise FetchError("%s: SHA256SUMS differs from the SHA-256 GitHub lists for it" % tag)
        sums = parse_sums(sums_path)
        os.unlink(sums_path)
        if set(sums) != set(assets) - {"SHA256SUMS"}:
            raise FetchError("%s: SHA256SUMS lists %s, the release holds %s" % (tag, sorted(sums), sorted(set(assets) - {"SHA256SUMS"})))
        got = {}
        for n in sorted(sums):
            a = assets[n]
            digest = a.get("digest") or ""
            if not digest.startswith("sha256:"):
                raise FetchError("%s: GitHub lists no SHA-256 for %s" % (tag, n))
            dest = os.path.join(tmp, n)
            sha = api.download(a["browser_download_url"], dest)
            if sha != digest[7:]:
                raise FetchError("%s: %s has SHA-256 %s, GitHub lists %s" % (tag, n, sha[:16], digest[7:23]))
            if sha != sums[n]:
                raise FetchError("%s: %s has SHA-256 %s, SHA256SUMS lists %s" % (tag, n, sha[:16], sums[n][:16]))
            got[n] = sha
        # the reports: plain files with plain names, nothing else
        if "reports.tar.gz" in got:
            rt = os.path.join(tmp, "reports.tar.gz")
            with tarfile.open(rt, "r:gz") as t:
                members = t.getmembers()
                if len(members) > MAX_MEMBERS:
                    raise FetchError("%s: reports.tar.gz has too many members" % tag)
                for m in members:
                    if not m.isreg() or not NAME_OK.match(m.name) or m.size > MAX_FILE or os.path.lexists(os.path.join(tmp, m.name)):
                        raise FetchError("%s: reports.tar.gz member %r" % (tag, m.name[:60]))
                for m in members:
                    with t.extractfile(m) as src, open(os.path.join(tmp, m.name), "wb") as dst:
                        shutil.copyfileobj(src, dst)
            os.unlink(rt)
        manifest = None
        mp = os.path.join(tmp, "manifest.json")
        if os.path.isfile(mp):
            try:
                manifest = json.load(open(mp))
                files = manifest.get("files", {})
                if not isinstance(manifest, dict) or not isinstance(files, dict):
                    raise ValueError("not an object")
            except ValueError as e:
                raise FetchError("%s: manifest.json: %s" % (tag, e))
            for n, want in files.items():
                p = os.path.join(tmp, n)
                if not os.path.isfile(p):
                    if n.endswith(".dcp"):
                        continue
                    raise FetchError("%s: manifest.json lists %s, the release lacks it" % (tag, n))
                if sha256_file(p) != want:
                    raise FetchError("%s: %s differs from manifest.json" % (tag, n))
            hb = os.path.join(tmp, "host.bit")
            if entry.startswith("host-") and os.path.isfile(hb) and manifest.get("host_sha") and sha256_file(hb) != manifest["host_sha"]:
                raise FetchError("%s: host.bit is not the host_sha of its manifest" % tag)
        open(os.path.join(tmp, "DONE"), "w").close()
        shutil.rmtree(final, ignore_errors=True)
        os.chmod(tmp, 0o755)
        os.rename(tmp, final)
    except BaseException:
        shutil.rmtree(tmp, ignore_errors=True)
        raise
    rec = {"tag": tag, "release_id": rel.get("id"), "sha256sums": sums_sha, "assets": got}
    os.makedirs(os.path.join(cache, ".releases"), exist_ok=True)
    with open(os.path.join(cache, ".releases", entry + ".json"), "w") as f:
        json.dump(rec, f, indent=2, sort_keys=True)
    return rec, manifest


def cached_entry(entry, cache):
    """D40: (record, manifest) of an entry that is in the cache with its record and unchanged asset files, else None."""
    d, rp = os.path.join(cache, entry), os.path.join(cache, ".releases", entry + ".json")
    if not (os.path.isfile(os.path.join(d, "DONE")) and os.path.isfile(rp)):
        return None
    try:
        rec = json.load(open(rp))
        assets = rec["assets"]
        if rec.get("tag") != "bit-" + entry or not isinstance(assets, dict):
            return None
        for n, want in assets.items():
            if n == "reports.tar.gz":
                continue
            p = os.path.join(d, n)
            if not NAME_OK.match(n) or not os.path.isfile(p) or sha256_file(p) != want:
                return None
        mp = os.path.join(d, "manifest.json")
        return rec, (json.load(open(mp)) if os.path.isfile(mp) else None)
    except (ValueError, KeyError, TypeError, OSError):
        return None


def prune(cache, keep):
    """D40: remove the entries that are not in keep, with their records, and half-made directories."""
    for n in os.listdir(cache):
        p = os.path.join(cache, n)
        if ".tmp." in n or (ENTRY.match(n) and n not in keep):
            shutil.rmtree(p, ignore_errors=True)
            try:
                os.unlink(os.path.join(cache, ".releases", n + ".json"))
            except OSError:
                pass


def main():
    args = sys.argv[1:]
    names_only = bool(args) and args[0] == "--names"
    if names_only:
        args = args[1:]
    target = args[0] if args else ""
    if len(args) > 1:
        raise FetchError("usage: ci/fetch_bits.sh [--names] [core:<id>|large:<id>]")
    if names_only:
        print("\n".join(select(entry_names(), target, reference_core())))
        return 0
    cache = os.environ.get("CI_BIT_CACHE") or (os.path.join(os.environ["RUNNER_TEMP"], "bit") if os.environ.get("RUNNER_TEMP") else "")
    if not cache:
        raise FetchError("CI_BIT_CACHE is not set")
    os.makedirs(cache, exist_ok=True)
    open(os.path.join(cache, ".fetched"), "w").close()       # ci/lib.sh cache_gc_if_free leaves a directory that holds restored entries alone
    pkg = env_file(os.path.join(HERE, "pkg.env"))
    repo = os.environ.get("MC_BUILDS_REPO") or pkg["MC_BUILDS_REPO"]
    api = Api(os.environ.get("BUILDS_API", "https://api.github.com"), repo, os.environ.get("GH_TOKEN", ""))
    names = select(entry_names(), target, reference_core())
    if not target:
        prune(cache, set(names))
    records, manifests = {}, {}
    reused = 0
    for n in names:
        hit = cached_entry(n, cache)
        if hit:
            records[n], manifests[n] = hit
            reused += 1
            print("cached  %s (%d assets)" % (hit[0]["tag"], len(hit[0]["assets"])))
            continue
        rec, man = fetch_entry(api, n, cache)
        records[n], manifests[n] = rec, man
        print("fetched %s (%d assets)" % (rec["tag"], len(rec["assets"])))
    hosts = [n for n in names if n.startswith("host-")]
    if hosts:
        hsha = (manifests[hosts[0]] or {}).get("host_sha")
        if not hsha:
            raise FetchError("%s: manifest.json names no host_sha" % hosts[0])
        for n in names:
            if n.startswith("core-") and (manifests[n] or {}).get("host_sha") != hsha:
                for e in names:
                    shutil.rmtree(os.path.join(cache, e), ignore_errors=True)       # a set that does not belong together is not left for collect
                raise FetchError("%s was routed against another host than %s (host_sha differs)" % (n, hosts[0]))
    if os.environ.get("CI_HANDOFF"):
        os.makedirs(os.environ["CI_HANDOFF"], exist_ok=True)
        json.dump({"repository": repo, "entries": records}, open(os.path.join(os.environ["CI_HANDOFF"], "bit-releases.json"), "w"), indent=2, sort_keys=True)
    print("fetch_bits: %d entries restored into %s (%d from the cache, %d fetched)" % (len(names), cache, reused, len(names) - reused))
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except FetchError as e:
        print("ci/fetch_bits.sh: %s" % e, file=sys.stderr)
        sys.exit(1)
