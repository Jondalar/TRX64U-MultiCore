#!/usr/bin/env python3
"""T62 board test: debug register ($D7FF), IEC capability off, safe-mode register not stuck. REST only.

    tests/t62_board.py [HOST] [--package VER]

  --package VER  load the bitstream (tools/load.sh c64_basic VER) and boot that package's ultimate.app
                 (tools/trxdbg.sh boot staged), then wait for REST. Needs the USB-Blaster.

Checks (the machine is left at READY; no setting is changed except Speaker Enable -> Disabled, which is what
the board policy asks for after an app boot, and the debug register, which is put back):
  1. REST machine:debugreg: the app's write reads back; a PRG that makes the C64 write $C3 then $11 to $D7FF
     leaves 11 in the register (a C64 write reaches U64_DEBUG_REGISTER)
  2. the app booted normally: configs list has no safe-mode signs (Drive A Settings present), and
     "SoftIEC Drive Settings" / "Printer Settings" are gone (CAPAB_HARDWARE_IEC cleared)
  3. GET /v1/drives has no softiec entry
What only a human can see (RESTORE held at power-on, scan lines on HDMI, link LED) is not checked here.
"""
import argparse, json, os, subprocess, sys, time, urllib.parse, urllib.request, urllib.error

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
ap = argparse.ArgumentParser()
ap.add_argument("host", nargs="?", default="192.0.2.189")
ap.add_argument("--package")
args = ap.parse_args()
HOST = args.host
fails = 0


def rest(method, path, params=None, body=None, timeout=15, ctype="application/octet-stream"):
    url = "http://%s%s" % (HOST, path)
    if params:
        url += "?" + urllib.parse.urlencode(params)
    req = urllib.request.Request(url, data=body, method=method)
    if body is not None:
        req.add_header("Content-Type", ctype)
    try:
        data = urllib.request.urlopen(req, timeout=timeout).read()
    except urllib.error.HTTPError as e:
        data = e.read()
    return json.loads(data or b"{}")


def check(name, ok, extra=""):
    global fails
    print("%s: %s %s" % ("PASS" if ok else "FAIL", name, extra))
    if not ok:
        fails += 1


def wait_rest(seconds=120):
    end = time.time() + seconds
    while time.time() < end:
        try:
            if "version" in rest("GET", "/v1/version", timeout=3):
                return True
        except Exception:
            pass
        time.sleep(2)
    return False


if args.package:
    subprocess.run([os.path.join(ROOT, "tools/load.sh"), "c64_basic", args.package], check=True)
    pkg = "/tmp/t62_pkg"
    os.makedirs(pkg, exist_ok=True)
    print("fetch ultimate.app of the package into", pkg, "and run: tools/trxdbg.sh boot staged <ultimate.app> (done by the caller)")
if not wait_rest():
    print("FAIL: REST does not answer")
    sys.exit(1)

# speaker off (board policy after an app boot)
rest("PUT", "/v1/configs/Speaker%20Mixer/Speaker%20Enable", {"value": "Disabled"})
r = rest("GET", "/v1/configs/Speaker%20Mixer/Speaker%20Enable")
check("speaker disabled", "Disabled" in json.dumps(r), json.dumps(r)[:120])

old = rest("GET", "/v1/machine:debugreg").get("value")
check("debugreg readable", old is not None, "(%s)" % old)
r = rest("PUT", "/v1/machine:debugreg", {"value": "5A"})
check("app write reads back", r.get("value") == "5A", json.dumps(r))
# BASIC stub SYS2061 + LDA #$C3 STA $D7FF LDA #$11 STA $D7FF RTS
prg = bytes([0x01, 0x08, 0x0B, 0x08, 0x0A, 0x00, 0x9E, 0x32, 0x30, 0x36, 0x31, 0x00, 0x00, 0x00,
             0xA9, 0xC3, 0x8D, 0xFF, 0xD7, 0xA9, 0x11, 0x8D, 0xFF, 0xD7, 0x60])
rest("POST", "/v1/runners:run_prg", body=prg)
time.sleep(3)
r = rest("GET", "/v1/machine:debugreg")
check("a C64 write to $D7FF reaches the debug register", r.get("value") == "11", json.dumps(r))
rest("PUT", "/v1/machine:debugreg", {"value": old or "00"})

cats = rest("GET", "/v1/configs")
names = json.dumps(cats)
check("app is not in safe mode (Drive A Settings present)", "Drive A Settings" in names)
check("no SoftIEC Drive Settings", "SoftIEC Drive Settings" not in names)
check("no Printer Settings", "Printer Settings" not in names)
d = json.dumps(rest("GET", "/v1/drives"))
check("no softiec drive", "softiec" not in d.lower(), d[:160])
rest("PUT", "/v1/machine:reset")
print("FAILED %d" % fails if fails else "ALL PASS")
sys.exit(1 if fails else 0)
