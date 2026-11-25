"""A small stand-in for the parts of GitHub's REST API that ci/order.py, ci/fetch_bits.py and ci/publish_release.py use, for their unit tests (no network).

  gh = FakeGH(repo="o/builds"); gh.start(); ... gh.base ... gh.stop()
  gh.add_release(tag, {name: bytes}, draft=False)   a release with its assets (digest = SHA-256 of the bytes unless digests={name: hex} says otherwise)
  gh.releases                                       {tag: record}; gh.log: list of (method, path); gh.fail_uploads: answer that many uploads with 502 first
  gh.corrupt_upload: the digest the API reports for the next uploaded asset is wrong
"""
import hashlib
import http.server
import json
import re
import threading
import urllib.parse


class FakeGH:
    def __init__(self, repo="o/builds"):
        self.repo = repo
        self.releases = {}
        self.next_id = 100
        self.log = []
        self.auth = []
        self.fail_uploads = 0
        self.corrupt_upload = False
        self.default_branch = "main"
        gh = self

        class H(http.server.BaseHTTPRequestHandler):
            def log_message(self, *a):
                pass

            def _send(self, code, obj=None, raw=None):
                body = raw if raw is not None else (json.dumps(obj).encode() if obj is not None else b"")
                self.send_response(code)
                self.send_header("Content-Length", str(len(body)))
                self.end_headers()
                self.wfile.write(body)

            def _handle(self, method):
                u = urllib.parse.urlparse(self.path)
                path, q = u.path, urllib.parse.parse_qs(u.query)
                gh.log.append((method, path))
                gh.auth.append(self.headers.get("Authorization"))
                n = int(self.headers.get("Content-Length") or 0)
                data = self.rfile.read(n) if n else b""
                base = "/repos/%s" % gh.repo
                m = re.match(r"^/dl/(\d+)/(.+)$", path)
                if m and method == "GET":
                    r = gh.by_id(int(m.group(1)))
                    if r and m.group(2) in r["files"]:
                        return self._send(200, raw=r["files"][m.group(2)])
                    return self._send(404, {})
                if path == base and method == "GET":
                    return self._send(200, {"default_branch": gh.default_branch})
                m = re.match(r"^%s/releases/tags/(.+)$" % re.escape(base), path)
                if m and method == "GET":
                    r = gh.releases.get(urllib.parse.unquote(m.group(1)))
                    if r and not r["draft"]:
                        return self._send(200, gh.json_of(r))
                    return self._send(404, {"message": "Not Found"})
                if path == base + "/releases" and method == "GET":
                    page = int(q.get("page", ["1"])[0])
                    items = [gh.json_of(r) for r in reversed(list(gh.releases.values()))] if page == 1 else []      # newest first, as GitHub lists
                    return self._send(200, items)
                if path == base + "/releases" and method == "POST":
                    d = json.loads(data)
                    gh.next_id += 1
                    r = {"id": gh.next_id, "tag": d["tag_name"], "draft": d.get("draft", False), "prerelease": d.get("prerelease", False), "files": {}, "digests": {}, "body": d.get("body")}
                    gh.releases[d["tag_name"]] = r
                    return self._send(201, gh.json_of(r))
                m = re.match(r"^%s/releases/(\d+)$" % re.escape(base), path)
                if m:
                    r = gh.by_id(int(m.group(1)))
                    if not r:
                        return self._send(404, {})
                    if method == "GET":
                        return self._send(200, gh.json_of(r))
                    if method == "PATCH":
                        d = json.loads(data)
                        r["draft"] = d.get("draft", r["draft"])
                        return self._send(200, gh.json_of(r))
                    if method == "DELETE":
                        del gh.releases[r["tag"]]
                        return self._send(204)
                m = re.match(r"^/upload/(\d+)$", path)
                if m and method == "POST":
                    if gh.fail_uploads > 0:
                        gh.fail_uploads -= 1
                        return self._send(502, {})
                    r = gh.by_id(int(m.group(1)))
                    name = q["name"][0]
                    r["files"][name] = data
                    dig = hashlib.sha256(data).hexdigest()
                    if gh.corrupt_upload:
                        gh.corrupt_upload = False
                        dig = "0" * 64
                    r["digests"][name] = dig
                    return self._send(201, {"name": name, "digest": "sha256:" + dig, "size": len(data)})
                return self._send(404, {"message": "no route %s %s" % (method, path)})

            def do_GET(self):
                self._handle("GET")

            def do_POST(self):
                self._handle("POST")

            def do_PATCH(self):
                self._handle("PATCH")

            def do_DELETE(self):
                self._handle("DELETE")

        self.server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), H)
        self.base = "http://127.0.0.1:%d" % self.server.server_address[1]

    def by_id(self, i):
        for r in self.releases.values():
            if r["id"] == i:
                return r
        return None

    def json_of(self, r):
        assets = []
        for n, b in r["files"].items():
            assets.append({"name": n, "size": len(b), "digest": ("sha256:" + r["digests"][n]) if r["digests"].get(n) else None,
                           "browser_download_url": "%s/dl/%d/%s" % (self.base, r["id"], n)})
        return {"id": r["id"], "tag_name": r["tag"], "draft": r["draft"], "prerelease": r["prerelease"], "assets": assets,
                "upload_url": "%s/upload/%d{?name,label}" % (self.base, r["id"])}

    def add_release(self, tag, files, draft=False, digests=None):
        self.next_id += 1
        r = {"id": self.next_id, "tag": tag, "draft": draft, "prerelease": True, "files": dict(files),
             "digests": {n: hashlib.sha256(b).hexdigest() for n, b in files.items()}, "body": ""}
        r["digests"].update(digests or {})
        self.releases[tag] = r
        return r

    def start(self):
        self.thread = threading.Thread(target=self.server.serve_forever, daemon=True)
        self.thread.start()
        return self

    def stop(self):
        self.server.shutdown()
        self.server.server_close()
