#!/usr/bin/env python3
"""T239.1 D1, D19, S8: the readers' side of the builds repository (MC_BUILDS_REPO of ci/pkg.env). No token is needed: the releases are public.

  tools/builds.sh latest [--ref main]         the tag <slug>-<sha8> of the newest published pre-release on the first-parent line of REF (default main) of this checkout
  tools/builds.sh files <tag>                 the asset names of a published release
  tools/builds.sh fetch <tag> <asset> <dest>  one asset into <dest> (a file name, or a directory: it keeps the asset's name)

  env  MC_BUILDS_REPO  default from ci/pkg.env;  BUILDS_API  default https://api.github.com;  GH_TOKEN  optional (the rate limit only; it is sent to the API, never to an asset address)

`latest` reads the commits of REF with git (first parent only, newest first) and takes the first one that has a published, non-draft release named <slug>-<sha8> (slug of the
branch: main -> main). A later commit whose package is not built yet is skipped; so is a package that was never published.
`fetch` writes nothing but a verified file: the SHA-256 of the download must be the `digest` GitHub lists for the asset and, when the release has a SHA256SUMS, the one that file lists
(a release that lists neither is refused). The file is renamed into place after the check; a refused download leaves nothing behind."""
import hashlib
import json
import os
import re
import subprocess
import sys
import tempfile
import urllib.error
import urllib.parse
import urllib.request

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
TAG = re.compile(r"^[A-Za-z0-9][A-Za-z0-9._+-]{0,99}$")
NAME_OK = re.compile(r"^[A-Za-z0-9][A-Za-z0-9._+-]{0,99}$")
MAX_FILE = 1900 * 1024 * 1024
MAX_COMMITS = 5000


class BuildsError(Exception):
    pass


def pkg_env():
    d = {}
    for line in open(os.path.join(ROOT, "ci", "pkg.env")):
        m = re.match(r"^(\w+)=(\S+)", line)
        if m:
            d[m.group(1)] = m.group(2)
    return d


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
                    raise BuildsError("%s: %s" % (url.split("?")[0], last))
            except (urllib.error.URLError, OSError) as e:
                last = str(e)
        raise BuildsError("%s: %s" % (url.split("?")[0], last))

    def _api_headers(self):
        h = {"Accept": "application/vnd.github+json", "User-Agent": "trx64-builds"}
        if self.token:
            h["Authorization"] = "Bearer " + self.token
        return h

    def releases(self):
        """Every published (non-draft) release of the repository, newest page first."""
        out, page = [], 1
        while True:
            try:
                with self._open("%s/repos/%s/releases?per_page=100&page=%d" % (self.base, self.repo, page), self._api_headers()) as r:
                    items = json.load(r)
            except urllib.error.HTTPError:
                raise BuildsError("the repository %s has no release list" % self.repo)
            out += [i for i in items if not i.get("draft")]
            if len(items) < 100:
                return out
            page += 1

    def release(self, tag):
        if not TAG.match(tag):
            raise BuildsError("not a release tag: %r" % tag[:60])
        try:
            with self._open("%s/repos/%s/releases/tags/%s" % (self.base, self.repo, urllib.parse.quote(tag)), self._api_headers()) as r:
                rel = json.load(r)
        except urllib.error.HTTPError:
            raise BuildsError("no release %s in %s" % (tag, self.repo))
        if rel.get("draft"):
            raise BuildsError("release %s is a draft" % tag)
        return rel

    def download(self, url, dest):
        """The bytes at an asset address (no credential is sent) into dest; returns the SHA-256."""
        h, n = hashlib.sha256(), 0
        with self._open(url, {"User-Agent": "trx64-builds"}) as r, open(dest, "wb") as f:
            for b in iter(lambda: r.read(1 << 20), b""):
                n += len(b)
                if n > MAX_FILE:
                    raise BuildsError("%s is larger than %d MiB" % (url.rsplit("/", 1)[-1], MAX_FILE >> 20))
                h.update(b)
                f.write(b)
        return h.hexdigest()


def first_parent_tags(ref, slug):
    """The pre-release tags <slug>-<sha8> of the commits on the first-parent line of ref, newest first."""
    r = subprocess.run(["git", "-C", ROOT, "rev-list", "--first-parent", "--abbrev-commit", "--abbrev=8", "-n", str(MAX_COMMITS), ref],
                       capture_output=True, text=True)
    if r.returncode != 0:
        raise BuildsError("git rev-list %s: %s" % (ref, r.stderr.strip()[-200:]))
    return ["%s-%s" % (slug, c) for c in r.stdout.split()]


def latest(api, ref):
    published = {r["tag_name"] for r in api.releases()}
    slug = ref.rsplit("/", 1)[-1]
    for tag in first_parent_tags(ref, slug):
        if tag in published:
            return tag
    raise BuildsError("no published %s-<sha8> release on the first-parent line of %s (%d releases listed)" % (slug, ref, len(published)))


def parse_sums(text):
    sums = {}
    for line in text.splitlines():
        m = re.match(r"^([0-9a-f]{64})  (\S+)$", line)
        if not m or m.group(2) in sums:
            raise BuildsError("SHA256SUMS is malformed: %r" % line[:80])
        sums[m.group(2)] = m.group(1)
    return sums


def fetch(api, tag, name, dest):
    if not NAME_OK.match(name):
        raise BuildsError("not an asset name: %r" % name[:60])
    rel = api.release(tag)
    assets = {a["name"]: a for a in rel.get("assets", [])}
    if name not in assets:
        raise BuildsError("release %s has no asset %s" % (tag, name))
    a = assets[name]
    want = []
    if a.get("digest"):
        if not str(a["digest"]).startswith("sha256:"):
            raise BuildsError("%s: digest %r is not a SHA-256" % (name, a["digest"]))
        want.append(("the digest GitHub lists", a["digest"][7:]))
    if "SHA256SUMS" in assets and name != "SHA256SUMS":
        with tempfile.TemporaryDirectory() as t:
            p = os.path.join(t, "sums")
            api.download(assets["SHA256SUMS"]["browser_download_url"], p)
            sums = parse_sums(open(p).read())
        if name not in sums:
            raise BuildsError("SHA256SUMS of %s does not list %s" % (tag, name))
        want.append(("SHA256SUMS", sums[name]))
    if not want:
        raise BuildsError("release %s lists no SHA-256 for %s: refused" % (tag, name))
    out = os.path.join(dest, name) if os.path.isdir(dest) else dest
    tmp = tempfile.NamedTemporaryFile(dir=os.path.dirname(os.path.abspath(out)), prefix=".builds-", delete=False)
    tmp.close()
    try:
        got = api.download(a["browser_download_url"], tmp.name)
        for what, w in want:
            if got != w:
                raise BuildsError("%s of %s is %s, %s says %s: refused" % (name, tag, got, what, w))
        os.replace(tmp.name, out)
    finally:
        if os.path.exists(tmp.name):
            os.unlink(tmp.name)
    return out, got


def main(argv):
    args = list(argv)
    ref = "main"
    if "--ref" in args:
        i = args.index("--ref")
        ref = args[i + 1]
        del args[i:i + 2]
    if not args or args[0] not in ("latest", "files", "fetch") or len(args) != {"latest": 1, "files": 2, "fetch": 4}[args[0]]:
        print(__doc__.split("\n\n")[0], file=sys.stderr)
        return 2
    env = pkg_env()
    api = Api(os.environ.get("BUILDS_API", "https://api.github.com"), os.environ.get("MC_BUILDS_REPO") or env["MC_BUILDS_REPO"], os.environ.get("GH_TOKEN"))
    try:
        if args[0] == "latest":
            print(latest(api, ref))
        elif args[0] == "files":
            for a in api.release(args[1]).get("assets", []):
                print(a["name"])
        else:
            out, got = fetch(api, args[1], args[2], args[3])
            print("fetched %s of %s (%s) -> %s" % (args[2], args[1], got[:16], out), file=sys.stderr)
    except BuildsError as e:
        print("builds: %s" % e, file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
