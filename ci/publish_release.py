#!/usr/bin/env python3
"""T239.1 D1, S8: the pre-release of one run in the builds repository: tag <branch>-<sha8> (main-<sha8>, t239.1-<sha8>, v<x.y.z>-<sha8>), made as a draft, filled one file at
a time with a retry, every asset's SHA-256 compared with the file and with SHA256SUMS, and only then published. A published release is immutable (D28), so a half-made one
must never be public; any failure after the draft exists deletes the draft.

  ci/publish_release.py --dir DIR --tag TAG --sha FULL-SHA
    env  BUILDS_TOKEN     the token of the environment `builds` (write access to the builds repository only)
         MC_BUILDS_REPO   default from ci/pkg.env;  MC_SRC_REPO the repository whose runs may publish (ci/pkg.env)
         PUBLISH_API      default https://api.github.com (tests)
         CI_BRANCH, CI_REF_TYPE (else GITHUB_REF_NAME, GITHUB_REF_TYPE): the ref of the run

ci/publish_sdk.py (T261) publishes the SDK package through publish() and guard() of this file, on its own branches (SDK_BRANCHES), with its own body text.

DIR must hold SHA256SUMS naming every other file with its SHA-256 and nothing else (ci/prerelease.sh makes it). On main, t239.1 and a v* tag a missing token is an error; on
any other ref nothing is published and the script ends green. Inside GitHub Actions it publishes only for a push of MC_SRC_REPO itself, to one of those refs, for the commit
the run is for (D1: a pre-release is made only by the job that runs on the repository's own main, never for a fork or a dispatch).
A release of that tag that is already published is left alone when it holds exactly these files; any other content is an error (immutable).
"""
import argparse
import hashlib
import json
import os
import re
import sys
import time
import urllib.error
import urllib.request

HERE = os.path.dirname(os.path.abspath(__file__))
NAME_OK = re.compile(r"^[A-Za-z0-9][A-Za-z0-9._+-]{0,99}$")
TAG_OK = re.compile(r"^[A-Za-z0-9][A-Za-z0-9._+-]{0,99}$")
MAX_ASSET = 1900 * 1024 * 1024
MAX_ASSETS = 200
UPLOADS = "https://uploads.github.com/"


class PublishError(Exception):
    pass


def env_file(path):
    d = {}
    for line in open(path):
        m = re.match(r"^(\w+)=(\S+)", line)
        if m:
            d[m.group(1)] = m.group(2)
    return d


def sha256_file(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for b in iter(lambda: f.read(1 << 20), b""):
            h.update(b)
    return h.hexdigest()


class Api:
    def __init__(self, base, token):
        self.base, self.token = base.rstrip("/"), token

    def call(self, method, path_or_url, body=None, data_path=None, tries=3):
        url = path_or_url if path_or_url.startswith("http") else self.base + "/" + path_or_url
        last = None
        for attempt in range(tries):
            hdr = {"Accept": "application/vnd.github+json", "Authorization": "Bearer " + self.token, "User-Agent": "trx64-publish"}
            data = None
            if body is not None:
                data = json.dumps(body).encode()
                hdr["Content-Type"] = "application/json"
            fh = None
            if data_path is not None:
                fh = open(data_path, "rb")
                data = fh
                hdr["Content-Type"] = "application/octet-stream"
                hdr["Content-Length"] = str(os.path.getsize(data_path))
            try:
                with urllib.request.urlopen(urllib.request.Request(url, data=data, headers=hdr, method=method), timeout=300) as r:
                    raw = r.read()
                    return json.loads(raw) if raw else {}
            except urllib.error.HTTPError as e:
                last = "HTTP %d" % e.code
                if e.code == 404 and method == "GET":
                    return None
                if e.code < 500 and e.code != 429:
                    # the whole answer: GitHub names the refused field and why only at its end (run 44760578882 cut it after 200 bytes)
                    raise PublishError("%s %s: %s %s" % (method, url.split("?")[0], last, e.read()[:4000].decode("utf-8", "replace")))
            except (urllib.error.URLError, OSError) as e:
                last = str(e)
            finally:
                if fh:
                    fh.close()
            time.sleep(2 * (attempt + 1))
        raise PublishError("%s %s: %s after %d tries" % (method, url.split("?")[0], last, tries))


def check_dir(d):
    """{name: sha256} of the files of DIR, after SHA256SUMS has been compared with them."""
    if not os.path.isdir(d):
        raise PublishError("%s is no directory" % d)
    files = {}
    for n in sorted(os.listdir(d)):
        p = os.path.join(d, n)
        if os.path.islink(p) or not os.path.isfile(p):
            raise PublishError("%s is no plain file" % n)
        if not NAME_OK.match(n):
            raise PublishError("file name %r would be renamed by GitHub" % n[:60])
        if os.path.getsize(p) > MAX_ASSET:
            raise PublishError("%s is larger than %d MiB" % (n, MAX_ASSET >> 20))
        files[n] = p
    if len(files) > MAX_ASSETS:
        raise PublishError("too many files")
    if "SHA256SUMS" not in files:
        raise PublishError("no SHA256SUMS in %s" % d)
    listed = {}
    for line in open(files["SHA256SUMS"]):
        m = re.match(r"^([0-9a-f]{64})  (\S+)$", line.rstrip("\n"))
        if not m or m.group(2) in listed:
            raise PublishError("SHA256SUMS is malformed: %r" % line[:80])
        listed[m.group(2)] = m.group(1)
    others = set(files) - {"SHA256SUMS"}
    if not others or set(listed) != others:
        raise PublishError("SHA256SUMS lists %s, the directory holds %s" % (sorted(listed), sorted(others)))
    sums = {}
    for n in sorted(files):
        sums[n] = sha256_file(files[n])
        if n != "SHA256SUMS" and sums[n] != listed[n]:
            raise PublishError("%s: SHA-256 %s, SHA256SUMS lists %s" % (n, sums[n][:16], listed[n][:16]))
    return files, sums


PRERELEASE_BRANCHES = ("main", "t239.1")
SDK_BRANCHES = ("integration", "main", "t239.3")        # T261 D4: ci/publish_sdk.py, no tag may publish an SDK


def guard(args, env, branches=PRERELEASE_BRANCHES, tags=True):
    """Whether this run may publish: True, or False for a ref that publishes nothing; raises for a run that must not. Reads only the arguments and the environment.
    branches: the branches that publish; tags: whether a v* tag does."""
    ref = env.get("CI_BRANCH") or env.get("GITHUB_REF_NAME") or ""
    kind = env.get("CI_REF_TYPE") or env.get("GITHUB_REF_TYPE") or "branch"
    protected = tags and kind == "tag" and ref.startswith("v") or kind != "tag" and ref in branches
    if env.get("GITHUB_ACTIONS") == "true":
        if env.get("GITHUB_EVENT_NAME") != "push":
            raise PublishError("a pre-release is made for a push only, not for %r" % env.get("GITHUB_EVENT_NAME"))
        if env.get("GITHUB_REPOSITORY") != args.src_repo:
            raise PublishError("the run belongs to %r, not to %s" % (env.get("GITHUB_REPOSITORY"), args.src_repo))
        want_ref = "refs/tags/" + ref if kind == "tag" else "refs/heads/" + ref
        if env.get("GITHUB_REF") != want_ref:
            raise PublishError("GITHUB_REF %r is not the ref %s of the run" % (env.get("GITHUB_REF"), want_ref))
        if env.get("GITHUB_SHA") != args.sha:
            raise PublishError("the commit of the run is %s, the package is of %s" % (env.get("GITHUB_SHA"), args.sha))
    if not protected:
        print("publish_release: %s %r is none of %s%s: nothing is published" % (kind, ref, ", ".join(branches), " or a v* tag" if tags else ""))
        return False
    if not env.get("BUILDS_TOKEN"):
        raise PublishError("BUILDS_TOKEN is not set: the pre-release of %s cannot be published" % ref)
    return True


def publish(api, repo, tag, sha, d, log=print, body=None):
    files, sums = check_dir(d)
    cur = api.call("GET", "repos/%s/releases/tags/%s" % (repo, tag))
    if cur and not cur.get("draft"):
        have = {a["name"]: (a.get("digest") or "")[7:] for a in cur.get("assets", [])}
        if have == sums:
            log("publish_release: %s is already published with exactly these files" % tag)
            return cur
        raise PublishError("%s is already published with other content (releases are immutable); not changed" % tag)
    for page in range(1, 6):
        items = api.call("GET", "repos/%s/releases?per_page=100&page=%d" % (repo, page)) or []
        for r in items:
            if r.get("draft") and r.get("tag_name") == tag:
                api.call("DELETE", "repos/%s/releases/%d" % (repo, r["id"]))      # a half-made release of an earlier try
        if len(items) < 100:
            break
    branch = (api.call("GET", "repos/%s" % repo) or {}).get("default_branch", "main")
    if body is None:
        body = ("Release bundle of commit `%s` of the source repository, made by its pipeline (T239.1 D42): the .cor files, the update files, INSTALL.md, "
                "ROMS.txt and the zip of all of them.\n\nSHA256SUMS lists every file; GitHub shows the same digests." % sha)
    rel = api.call("POST", "repos/%s/releases" % repo, {"tag_name": tag, "target_commitish": branch, "name": tag, "body": body,
                                                         "draft": True, "prerelease": True, "make_latest": "false"})
    try:
        upload = re.sub(r"\{.*$", "", rel["upload_url"])
        if not (upload.startswith(UPLOADS) or upload.startswith(api.base + "/")):
            raise PublishError("upload address %r is not GitHub's" % upload[:60])
        for n in sorted(files, key=lambda x: (x == "SHA256SUMS", x)):          # SHA256SUMS last
            a = api.call("POST", upload + "?name=" + n, data_path=files[n])
            if (a.get("digest") or "") not in ("", "sha256:" + sums[n]):
                raise PublishError("%s: uploaded as %s, the file is %s" % (n, a["digest"][7:19], sums[n][:12]))
            log("publish_release: uploaded %s" % n)
        back = api.call("GET", "repos/%s/releases/%d" % (repo, rel["id"]))
        have = {a["name"]: (a.get("digest") or "")[7:] for a in back.get("assets", [])}
        if have != sums:
            raise PublishError("the draft holds %s, expected %s (digests compared against SHA256SUMS)" % (
                {n: h[:8] for n, h in sorted(have.items())}, {n: h[:8] for n, h in sorted(sums.items())}))
        out = api.call("PATCH", "repos/%s/releases/%d" % (repo, rel["id"]), {"draft": False, "prerelease": True, "make_latest": "false"})
        if out.get("draft"):
            raise PublishError("%s is still a draft after publishing" % tag)
    except BaseException:
        try:
            api.call("DELETE", "repos/%s/releases/%d" % (repo, rel["id"]), tries=1)
        except PublishError:
            pass
        raise
    log("publish_release: published %s in %s (%d files)" % (tag, repo, len(files)))
    return out


def main(argv=None, env=None):
    env = os.environ if env is None else env
    pkg = env_file(os.path.join(HERE, "pkg.env"))
    ap = argparse.ArgumentParser()
    ap.add_argument("--dir", required=True)
    ap.add_argument("--tag", required=True)
    ap.add_argument("--sha", required=True)
    ap.add_argument("--repo", default=env.get("MC_BUILDS_REPO") or pkg["MC_BUILDS_REPO"])
    ap.add_argument("--src-repo", default=env.get("MC_SRC_REPO") or pkg["MC_SRC_REPO"])
    args = ap.parse_args(argv)
    if not TAG_OK.match(args.tag) or not re.match(r"^[0-9a-f]{40}$", args.sha):
        raise PublishError("tag or commit is malformed")
    if not guard(args, env):
        return 0
    api = Api(env.get("PUBLISH_API", "https://api.github.com"), env["BUILDS_TOKEN"])
    publish(api, args.repo, args.tag, args.sha, args.dir)
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except PublishError as e:
        print("ci/publish_release.py: %s" % e, file=sys.stderr)
        sys.exit(1)
