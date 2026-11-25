#!/usr/bin/env python3
"""T261 P2, D4: the SDK lane's package as a release of the builds repository: tag sdk-<sdk_version>-<sha8>, the assets trx64-sdk-<version>.tar.gz, the lane's .u2a
files under their own names and SHA256SUMS (written here). A pre-release, never latest (ci/publish_release.py makes every release of the builds repository so).

  ci/publish_sdk.py --dir DIR --sha FULL-SHA [--version-file app/sdkpkg/SDK_VERSION]
    DIR is the SDK lane's artifact `sdk` (ci/lane.sh sdk package): trx64-sdk-<version>.tar.gz, *.u2a, coreapp.map, SHA256SUMS. coreapp.map and the lane's SHA256SUMS
    are not published.
    env  BUILDS_TOKEN, MC_BUILDS_REPO, MC_SRC_REPO, PUBLISH_API, CI_BRANCH, CI_REF_TYPE: as ci/publish_release.py; only a push to integration, main or t239.3 publishes.

It publishes only when the package differs from the newest published release of the same SDK version: the releases tagged sdk-<version>-<8 hex> are listed (newest
first, as GitHub lists them), the first one's SHA256SUMS is read, and the tarball's SHA-256 is compared with its line. Equal: nothing is published and the log says so.
No such release yet, or none with that line: publish. The tarball is reproducible (tools/sdk_pack.sh), so equal bytes mean an unchanged package.
"""
import argparse
import hashlib
import os
import re
import shutil
import sys
import tempfile
import urllib.error
import urllib.request

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import publish_release as P  # noqa: E402

VERSION_OK = re.compile(r"^[0-9]+\.[0-9]+\.[0-9]+$")


def read_version(path):
    v = open(path).read().strip()
    if not VERSION_OK.match(v):
        raise P.PublishError("%s: %r is no x.y.z" % (path, v[:40]))
    return v


def stage(src, version, out):
    """The files of the release in out, with their SHA256SUMS; returns the tarball's SHA-256."""
    tar = "trx64-sdk-%s.tar.gz" % version
    if not os.path.isfile(os.path.join(src, tar)):
        raise P.PublishError("%s holds no %s (app/sdkpkg/SDK_VERSION says %s)" % (src, tar, version))
    others = [n for n in os.listdir(src) if re.match(r"^trx64-sdk-.*\.tar\.gz$", n) and n != tar]
    if others:
        raise P.PublishError("%s holds another SDK package than %s: %s" % (src, tar, sorted(others)))
    names = [tar] + sorted(n for n in os.listdir(src) if n.endswith(".u2a"))
    for n in names:
        shutil.copyfile(os.path.join(src, n), os.path.join(out, n))
    sums = {n: P.sha256_file(os.path.join(out, n)) for n in names}
    with open(os.path.join(out, "SHA256SUMS"), "w") as f:
        for n in names:
            f.write("%s  %s\n" % (sums[n], n))
    return sums[tar], names


def read_sums(text):
    return {m.group(2): m.group(1) for m in (re.match(r"^([0-9a-f]{64})  (\S+)$", l) for l in text.splitlines()) if m}


def fetch_text(url, token=""):
    hdr = {"User-Agent": "trx64-publish"}
    if token:
        hdr["Authorization"] = "Bearer " + token
    with urllib.request.urlopen(urllib.request.Request(url, headers=hdr), timeout=120) as r:
        return r.read().decode("utf-8", "replace")


def newest_of_version(api, repo, version, pages=5):
    """The newest published release of repo tagged sdk-<version>-<8 hex> (the listing is newest first), or None."""
    pat = re.compile(r"^sdk-%s-[0-9a-f]{8}$" % re.escape(version))
    for page in range(1, pages + 1):
        items = api.call("GET", "repos/%s/releases?per_page=100&page=%d" % (repo, page)) or []
        for r in sorted(items, key=lambda x: x.get("created_at") or "", reverse=True):      # created_at when GitHub gives it; the sort is stable
            if not r.get("draft") and pat.match(r.get("tag_name") or ""):
                return r
        if len(items) < 100:
            break
    return None


def unchanged(api, repo, version, tar_sha, fetch=fetch_text):
    """The tag of the newest release of this version when its SHA256SUMS lists this tarball, else None."""
    rel = newest_of_version(api, repo, version)
    if rel is None:
        return None
    tar = "trx64-sdk-%s.tar.gz" % version
    for a in rel.get("assets", []):
        if a.get("name") == "SHA256SUMS":
            try:
                return rel["tag_name"] if read_sums(fetch(a["browser_download_url"]) or "").get(tar) == tar_sha else None
            except (urllib.error.URLError, OSError):
                raise P.PublishError("could not read SHA256SUMS of %s" % rel["tag_name"])
    return None


def main(argv=None, env=None, log=print):
    env = os.environ if env is None else env
    pkg = P.env_file(os.path.join(HERE, "pkg.env"))
    ap = argparse.ArgumentParser()
    ap.add_argument("--dir", required=True)
    ap.add_argument("--sha", required=True)
    ap.add_argument("--version-file", default=os.path.join(os.path.dirname(HERE), "app", "sdkpkg", "SDK_VERSION"))
    ap.add_argument("--repo", default=env.get("MC_BUILDS_REPO") or pkg["MC_BUILDS_REPO"])
    ap.add_argument("--src-repo", default=env.get("MC_SRC_REPO") or pkg["MC_SRC_REPO"])
    args = ap.parse_args(argv)
    if not re.match(r"^[0-9a-f]{40}$", args.sha):
        raise P.PublishError("commit is malformed")
    if not P.guard(args, env, branches=P.SDK_BRANCHES, tags=False):
        return 0
    version = read_version(args.version_file)
    tag = "sdk-%s-%s" % (version, args.sha[:8])
    out = tempfile.mkdtemp()
    try:
        tar_sha, names = stage(args.dir, version, out)
        api = P.Api(env.get("PUBLISH_API", "https://api.github.com"), env["BUILDS_TOKEN"])
        same = unchanged(api, args.repo, version, tar_sha)
        if same:
            log("publish_sdk: the package of SDK %s is unchanged (trx64-sdk-%s.tar.gz %s... is in %s): nothing published" % (version, version, tar_sha[:12], same))
            return 0
        body = ("SDK package %s of commit `%s` of the source repository, made by the SDK lane of its pipeline (T261 D4): the package, the sub-apps (.u2a) "
                "of the lane and SHA256SUMS.\n\nA release is published only when the package differs from the last release of its version." % (version, args.sha))
        P.publish(api, args.repo, tag, args.sha, out, log=log, body=body)
        log("publish_sdk: assets %s" % ", ".join(names + ["SHA256SUMS"]))
    finally:
        shutil.rmtree(out, ignore_errors=True)
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except P.PublishError as e:
        print("ci/publish_sdk.py: %s" % e, file=sys.stderr)
        sys.exit(1)
