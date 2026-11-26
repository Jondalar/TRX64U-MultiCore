#!/usr/bin/env python3
"""T239.5 D24: the .cor of the core lanes a package run does not start come from the package of the base the branch builds on.

  ci/base_cor.py <base commit> <out dir> <core>...
      Looks for the runs of the pipeline (.github/workflows/pipeline.yml) on <base commit>, newest first, and takes the first one that kept
      the artifact `package` (the matrix's whole package: the package job uploads it only when its package step is green). From it it writes
      <out>/<core>.cor and <out>/SD/cores/<core>/ (when the package has them) for each <core> named, and <out>/FROM (run id and commit).
      Prints one line per core it took. A core the base's package does not have is not written: the dispatcher starts its lane (D24, Alex:
      "a base run without a package: that core is built after all"). No base, no run with a package, an API error: nothing is written, rc 0.
  env: GH_TOKEN (actions: read), GITHUB_REPOSITORY, GITHUB_API_URL (default https://api.github.com)
"""
import io
import json
import os
import sys
import urllib.error
import urllib.request
import zipfile


class NoRedirect(urllib.request.HTTPRedirectHandler):
    def redirect_request(self, req, fp, code, msg, headers, newurl):
        return None


def api(url, token):
    req = urllib.request.Request(url, headers={"Authorization": "Bearer " + token, "Accept": "application/vnd.github+json"})
    with urllib.request.urlopen(req, timeout=60) as r:
        return json.load(r)


def download(url, token):
    """the artifact's zip: the API answers with a redirect to a signed address, which is fetched without the token"""
    opener = urllib.request.build_opener(NoRedirect)
    req = urllib.request.Request(url, headers={"Authorization": "Bearer " + token, "Accept": "application/vnd.github+json"})
    try:
        with opener.open(req, timeout=60) as r:
            return r.read()
    except urllib.error.HTTPError as e:
        if e.code not in (301, 302, 303, 307, 308):
            raise
        loc = e.headers.get("Location")
        if not loc:
            raise OSError("a redirect without a Location")
    with urllib.request.urlopen(loc, timeout=600) as r:
        return r.read()


def main(argv):
    if len(argv) < 3:
        print(__doc__, file=sys.stderr)
        return 2
    base, out, cores = argv[0], argv[1], argv[2:]
    token, repo = os.environ.get("GH_TOKEN", ""), os.environ.get("GITHUB_REPOSITORY", "")
    root = os.environ.get("GITHUB_API_URL", "https://api.github.com").rstrip("/")
    if not base or base == "none" or not token or not repo:
        print("base_cor: no base commit, token or repository: every core lane runs")
        return 0
    try:
        runs = api("%s/repos/%s/actions/workflows/pipeline.yml/runs?head_sha=%s&per_page=30" % (root, repo, base), token).get("workflow_runs", [])
        runs.sort(key=lambda r: r.get("created_at", ""), reverse=True)
        art = run = None
        for r in runs:
            arts = api("%s/repos/%s/actions/runs/%d/artifacts?name=package&per_page=10" % (root, repo, r["id"]), token).get("artifacts", [])
            arts = [a for a in arts if not a.get("expired")]
            if arts:
                art, run = arts[0], r
                break
        if not art:
            print("base_cor: no run of %s kept a package (%d runs): every core lane runs" % (base[:8], len(runs)))
            return 0
        z = zipfile.ZipFile(io.BytesIO(download(art["archive_download_url"], token)))
    except (urllib.error.URLError, OSError, ValueError, KeyError, zipfile.BadZipFile) as e:
        print("base_cor: the base's package could not be read (%s): every core lane runs" % e)
        return 0
    names = z.namelist()
    os.makedirs(out, exist_ok=True)
    took = []
    for c in cores:
        if c + ".cor" not in names:
            continue
        for n in [c + ".cor"] + [n for n in names if n.startswith("SD/cores/%s/" % c) and not n.endswith("/")]:
            dst = os.path.join(out, n)
            os.makedirs(os.path.dirname(dst), exist_ok=True)
            with open(dst, "wb") as fh:
                fh.write(z.read(n))
        took.append(c)
        print(c)
    with open(os.path.join(out, "FROM"), "w") as fh:
        fh.write("run %d (%s), commit %s, %d of %d cores\n" % (run["id"], run.get("html_url", ""), base, len(took), len(cores)))
    print("base_cor: %d of %d core .cor from run %d of %s" % (len(took), len(cores), run["id"], base[:8]), file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
