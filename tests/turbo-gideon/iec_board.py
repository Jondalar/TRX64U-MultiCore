#!/usr/bin/env python3
"""LOAD"$",8 and LIST at CPU speeds 1, 4, 16 and 64 on a real Ultimate (T29 IEC window, T29-gap check).

    TM_IP=<board> tests/turbo-gideon/iec_board.py [speed ...]      default: 1 4 16 64

Mounts /USB1/t29.d64 on drive A (device 8), sets Turbo Control = Manual and CPU Speed (memory only), resets the
machine, waits out the 4.26 s post-reset hold, types LOAD"$",8 and LIST through the keyboard buffer and reads the
screen. Restores Turbo Control = U64 Turbo Registers, CPU Speed 1 at the end. Exit code 0 = every speed ok.
"""
import json, os, sys, time, urllib.parse, urllib.request

IP = os.environ.get("TM_IP", "192.0.2.189")
BASE = f"http://{IP}/v1"
SCR = 0x0400


def req(method, path, body=None, **q):
    url = BASE + path + ("?" + urllib.parse.urlencode(q) if q else "")
    r = urllib.request.Request(url, method=method, data=body)
    with urllib.request.urlopen(r, timeout=20) as f:
        return f.read()


def cfg(item, value):
    p = "/configs/" + urllib.parse.quote("U64 Specific Settings") + "/" + urllib.parse.quote(item)
    return json.loads(req("PUT", p, value=value))


def rd(addr, n):
    return req("GET", "/machine:readmem", address=f"{addr:04X}", length=str(n))


def wr(addr, data):
    req("PUT", "/machine:writemem", address=f"{addr:04X}", data=bytes(data).hex())


def screen():
    raw = rd(SCR, 1000)
    rows = []
    for r in range(25):
        s = ""
        for c in raw[r * 40:(r + 1) * 40]:
            c &= 0x7F
            s += chr(c + 64) if 0 < c < 27 else (chr(c) if 32 <= c < 64 else " ")
        rows.append(s.rstrip())
    return rows


def type_line(text):
    wr(0x0277, [ord(ch) for ch in text] + [13])
    wr(0x00C6, [len(text) + 1])


def wait_for(pred, timeout):
    t = time.time()
    while time.time() - t < timeout:
        rows = screen()
        if pred(rows):
            return rows
        time.sleep(0.5)
    return screen()


def one(speed):
    cfg("Turbo Control", "Manual")
    cfg("CPU Speed", f"{speed:>2}")
    req("PUT", "/drives/a:mount", image="/USB1/t29.d64", type="d64", mode="readwrite")
    req("PUT", "/machine:reset")
    time.sleep(6.5)                                   # past the 2^22 PHI2 cycle hold (4.26 s) and the boot
    rows = wait_for(lambda r: any(x.startswith("READY.") for x in r), 10)
    type_line('LOAD"$",8')
    rows = wait_for(lambda r: any(x == "READY." for x in r[r.index(next(y for y in r if y.startswith('LOAD"$"'))) + 1:]) or any("?" in x for x in r), 20)
    ok_load = any("?" in x for x in rows) is False and any(x == "READY." for x in rows[3:])
    type_line("LIST")
    rows2 = wait_for(lambda r: any("BLOCKS FREE" in x for x in r) or any("?" in x for x in r), 20)
    ok_list = any("BLOCKS FREE" in x for x in rows2)
    print(f"{speed:>2} MHz: LOAD {'ok' if ok_load else 'FAILED'}, LIST {'ok' if ok_list else 'FAILED'}")
    for x in rows2:
        if x:
            print("   |", x)
    return ok_load and ok_list


speeds = [int(a) for a in sys.argv[1:]] or [1, 4, 16, 64]
bad = 0
try:
    for s in speeds:
        if not one(s):
            bad += 1
finally:
    cfg("CPU Speed", " 1")
    cfg("Turbo Control", "U64 Turbo Registers")
sys.exit(1 if bad else 0)
