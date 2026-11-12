#!/usr/bin/env python3
"""T67 board test: the app registry follows the files (app doc/AppLoader_Spec.md section 3.5).

    python3 tests/t67_board.py [--host 192.0.2.189] [--no-boot]

Needs the board booted with an app that has the T67 fix, ethernet REST + FTP, and tools/trxdbg.sh
(JTAG, to restart the app: "boot staged" of --app). Only /Flash/apps, /Flash/config/apps.json and
/Temp are written, through FTP as normal app use does; no raw flash, no configs:save_to_flash.
The original /Flash/apps/*.u2a and apps.json are saved to --backup first and put back at the end.

Test apps are built from the board's own restcheck.u2a (same payload, other manifest), so they
run (action "echo") but do nothing.
"""
import argparse, ftplib, io, json, os, struct, subprocess, sys, time, urllib.request, urllib.error, urllib.parse

BITSTREAM = "/tmp/b/c64_basic.bit"   # the c64_basic.bit of the package the app comes from
HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)


def rebuild(base, manifest):
    """base .u2a bytes with another manifest: header | variant table | manifest | payloads."""
    hdr = bytearray(base[:64])
    n = struct.unpack_from("<H", hdr, 12)[0]
    vt = [bytearray(base[64 + 24 * i:64 + 24 * (i + 1)]) for i in range(n)]
    man = json.dumps(manifest, separators=(",", ":")).encode() + b"\0"
    off = 64 + 24 * n
    struct.pack_into("<H", hdr, 14, len(man))
    struct.pack_into("<I", hdr, 20, off)
    name = manifest["name"].encode()[:27]
    hdr[28:56] = name.ljust(28, b"\0")
    off += len(man)
    out_payloads = b""
    for v in vt:
        po, ps = struct.unpack_from("<II", v, 12)
        payload = base[po:po + ps]
        struct.pack_into("<I", v, 12, off + len(out_payloads))
        out_payloads += payload
    return bytes(hdr) + b"".join(bytes(v) for v in vt) + man + out_payloads


def manifest_of(base):
    mo, ms = struct.unpack_from("<I", base, 20)[0], struct.unpack_from("<H", base, 14)[0]
    return json.loads(base[mo:mo + ms - 1])


class Board:
    def __init__(self, host):
        self.host = host

    def rest(self, method, path, timeout=20):
        req = urllib.request.Request(f"http://{self.host}/v1/{path}", method=method)
        try:
            with urllib.request.urlopen(req, timeout=timeout) as r:
                return r.status, json.loads(r.read() or b"{}")
        except urllib.error.HTTPError as e:
            body = e.read()
            try:
                return e.code, json.loads(body)
            except ValueError:
                return e.code, {"raw": body.decode(errors="replace")}

    def apps(self):
        return {a["id"]: a for a in self.rest("GET", "apps")[1]["apps"]}

    def ftp(self):
        f = ftplib.FTP(self.host, timeout=20)
        f.login()
        return f

    def put(self, path, data):
        f = self.ftp()
        f.storbinary("STOR " + path, io.BytesIO(data))
        f.quit()

    def get(self, path):
        f = self.ftp()
        b = io.BytesIO()
        f.retrbinary("RETR " + path, b.write)
        f.quit()
        return b.getvalue()

    def delete(self, path):
        f = self.ftp()
        try:
            f.delete(path)
        except ftplib.error_perm:
            pass
        f.quit()

    def ls(self, d):
        f = self.ftp()
        try:
            return f.nlst(d)
        except ftplib.error_perm:
            return []
        finally:
            f.quit()


def restart_app(app, host):
    # A second app start on the same bitstream asserts in the heap (heap_4.c) during init: load the bitstream again.
    subprocess.run([os.path.join(ROOT, "tools", "load.sh"), BITSTREAM], check=True, stdout=subprocess.DEVNULL)
    sh = os.path.join(ROOT, "tools", "trxdbg.sh")
    subprocess.run([sh, "boot", "staged", app], check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    t = time.time()
    time.sleep(8)
    while time.time() - t < 120:
        try:
            urllib.request.urlopen(f"http://{host}/v1/version", timeout=3).read()
            time.sleep(4)
            return True
        except Exception:
            time.sleep(2)
    return False


def alive(host):
    try:
        urllib.request.urlopen(f"http://{host}/v1/version", timeout=4).read()
        return True
    except Exception:
        return False


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--host", default="192.0.2.189")
    ap.add_argument("--app", default="/tmp/b/ultimate.app")
    ap.add_argument("--backup", default=os.path.expanduser("~/.claude/jobs/259f244b/tmp/t67-backup"))
    ap.add_argument("--bit", default=BITSTREAM)
    ap.add_argument("--restore-only", action="store_true", help="only put the saved state back (after an aborted run)")
    ap.add_argument("--only", help="comma list of case ids to run: 1,2,3,4b,4c,5")
    ap.add_argument("--no-boot", action="store_true", help="do not restart the app (boot cases will FAIL)")
    a = ap.parse_args()
    b = Board(a.host)
    res = []

    def check(name, ok, detail=""):
        res.append(ok)
        print(("PASS " if ok else "FAIL ") + name + (("  [" + detail + "]") if detail else ""))

    def reboot():
        if a.no_boot:
            return
        if not restart_app(a.app, a.host):
            print("board did not come back"); sys.exit(2)

    # ---- backup
    os.makedirs(a.backup, exist_ok=True)
    # An existing backup is the state to restore (an aborted run may have left test files on the board).
    if not os.path.exists(os.path.join(a.backup, "apps.json")):
        for n in b.ls("/Flash/apps"):
            open(os.path.join(a.backup, n), "wb").write(b.get("/Flash/apps/" + n))
        open(os.path.join(a.backup, "apps.json"), "wb").write(b.get("/Flash/config/apps.json"))
    flash_files = [n for n in os.listdir(a.backup) if n.endswith(".u2a")]
    apps_json = open(os.path.join(a.backup, "apps.json"), "rb").read()
    base = open(os.path.join(a.backup, "restcheck.u2a"), "rb").read()
    m0 = manifest_of(base)
    print("saved", len(flash_files), "apps + apps.json to", a.backup, "; base manifest", m0)

    def mk(id_, ver, rest=("echo",)):
        m = dict(m0); m["id"] = id_; m["name"] = "T67 " + id_; m["version"] = ver; m["rest"] = list(rest)
        return rebuild(base, m)

    try:
        # ---- case 1/3/2 prep: replace restcheck, add t67new + t67gone, add non-/Flash entries to apps.json
        v2 = dict(m0); v2["version"] = "9.9"; v2["rest"] = ["echo", "config", "fail", "t67b"]
        b.put("/Flash/apps/restcheck.u2a", rebuild(base, v2))
        b.put("/Flash/apps/t67new.u2a", mk("t67new", "1.0"))
        b.put("/Flash/apps/t67gone.u2a", mk("t67gone", "1.0"))
        reg = json.loads(apps_json)
        def entry(id_, path):
            m = dict(m0); m["id"] = id_; m["name"] = "T67 " + id_; m["version"] = "1.0"; m["rest"] = ["echo"]
            return {"id": id_, "name": m["name"], "version": "1.0", "path": path, "filetypes": [], "tasks": [], "rest": ["echo"]}
        reg["apps"] += [entry("t67tmp", "/Temp/t67tmp.u2a"), entry("t67abs", "/USB9/t67abs.u2a"),
                        entry("t67gone", "/Flash/apps/t67gone.u2a")]
        old_rc = [x for x in reg["apps"] if x["id"] == "restcheck"][0]
        print("registry entry of restcheck before reboot:", old_rc["version"], old_rc["path"])
        b.put("/Flash/config/apps.json", json.dumps(reg).encode())
        reboot()
        r = b.apps()
        check("1 replaced /Flash file: entry keeps the path", r.get("restcheck", {}).get("path") == "/Flash/apps/restcheck.u2a")
        check("1 replaced /Flash file: registry shows the new manifest", r.get("restcheck", {}).get("version") == "9.9"
              and "t67b" in r.get("restcheck", {}).get("rest", []), json.dumps(r.get("restcheck")))
        s, j = b.rest("PUT", "apps/restcheck:run?action=echo")
        check("1 replaced app runs (echo)", s == 200, f"{s} {j.get('errors')} {str(j.get('output'))[:60]}")
        check("3 new .u2a in /Flash/apps registered at boot", "t67new" in r, "")
        check("4a non-/Flash entries are left alone at boot", "t67tmp" in r and "t67abs" in r, "")

        # ---- case 2: delete the file of a registered /Flash entry, reboot
        check("2 pre: t67gone registered", "t67gone" in r)
        b.delete("/Flash/apps/t67gone.u2a")
        reboot()
        r = b.apps()
        check("2 deleted /Flash file: entry dropped at boot", "t67gone" not in r and "t67new" in r and "restcheck" in r)

        # ---- case 4: entry on another medium (/Temp is mounted, wiped at boot): the file is replaced
        b.put("/Temp/t67tmp.u2a", mk("t67tmp", "1.0"))
        s, j = b.rest("PUT", "apps/t67tmp:run?action=echo")
        check("4b /Temp entry, file there: runs", s == 200, f"{s} {j.get('errors')}")
        s, j = b.rest("PUT", "apps/t67abs:run?action=echo")
        r = b.apps()
        check("4c medium absent: 404 'medium not present', entry kept", s == 404 and "medium not present" in json.dumps(j) and "t67abs" in r,
              f"{s} {j.get('errors')}")

        # ---- case 5: truncated file of a registered app
        full = mk("t67new", "1.0")
        b.put("/Flash/apps/t67new.u2a", full[:len(full) // 2])
        s, j = b.rest("PUT", "apps/t67new:run?action=echo")
        r = b.apps()
        check("5 truncated .u2a: REST run 409, entry kept", s == 409 and "t67new" in r, f"{s} {j.get('errors')}")
        b.put("/Flash/apps/t67new.u2a", full)
        s, j = b.rest("PUT", "apps/t67new:run?action=echo")
        check("5 restored file runs again", s == 200, f"{s}")

        # ---- case 4b, last: a launch whose entry is stale refreshes the registry with a changed manifest
        # (found 2026-10-09: the app then crashes seconds later, so this case runs after all others)
        b.put("/Temp/t67tmp.u2a", mk("t67tmp", "2.0", ("echo", "t67b")))
        try:
            s, j = b.rest("PUT", "apps/t67tmp:run?action=t67b")
            r = b.apps()
            check("4b /Temp entry, file replaced: launch uses the new file (new action accepted, manifest 2.0)",
                  s == 200 and r.get("t67tmp", {}).get("version") == "2.0", f"{s} {j.get('errors')} {r.get('t67tmp', {}).get('version')}")
        except Exception as e:
            check("4b /Temp entry, file replaced: launch uses the new file", False, "no answer: " + repr(e)[:80])
        time.sleep(10)
        check("4b board still alive 10 s after the refresh", alive(a.host))
    finally:
        # ---- clean up: originals back, test files gone, registry as it was (a crashed board is restarted first)
        if not alive(a.host):
            print("board is down: restarting it before the clean-up")
            reboot()
        for n in ("t67new.u2a", "t67gone.u2a"):
            b.delete("/Flash/apps/" + n)
        b.delete("/Temp/t67tmp.u2a")
        for n in flash_files:
            b.put("/Flash/apps/" + n, open(os.path.join(a.backup, n), "rb").read())
        b.put("/Flash/config/apps.json", apps_json)
        reboot()
        r = b.apps()
        orig = {x["id"]: x for x in json.loads(apps_json)["apps"]}
        same = set(r) == set(orig) and all(r[i]["version"] == orig[i]["version"] and r[i]["path"] == orig[i]["path"] for i in r)
        files_ok = sorted(b.ls("/Flash/apps")) == sorted(flash_files)
        check("cleanup: registry and /Flash/apps as before", same and files_ok, f"{sorted(r)}")
    print(f"{sum(res)}/{len(res)} PASS")
    return 0 if all(res) else 1


if __name__ == "__main__":
    sys.exit(main())
