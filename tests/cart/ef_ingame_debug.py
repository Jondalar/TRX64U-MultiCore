#!/usr/bin/env python3
"""Timo's Castle (EasyFlash 1, real cart) in-game save crash on our core: probes through trxmon (T57, 2026-10-09).

    ef_ingame_debug.py io2          EasyFlash RAM test: what does the CPU read back at $DF80.. after writing there?
    ef_ingame_debug.py cfg          the live configs that can map devices into $DE00-$DFFF
    ef_ingame_debug.py set NAME VAL runtime config change (C64 and Cartridge Settings), never saved

Needs trxmon serving (PUT /v1/apps/trxmon:run?action=serve) and Timo's Castle at its menu. The `io2` probe
presses 4 (Reset Highscore), stops at the game's first STA $DE02, then runs a test routine instead of the
game: the cartridge flash is never written. The game is lost afterwards (reset the machine).
"""
import json
import os
import sys
import time
import urllib.parse
import urllib.request

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
from trxmon_client import Client  # noqa: E402

HOST = "192.0.2.189"
CFG = "/v1/configs/C64%20and%20Cartridge%20Settings"


def rest(method, path, **kw):
    url = "http://%s%s" % (HOST, path)
    if kw:
        url += "?" + urllib.parse.urlencode(kw)
    return json.loads(urllib.request.urlopen(urllib.request.Request(url, method=method), timeout=30).read().decode())


def setcfg(name, val):
    r = rest("PUT", CFG + "/" + urllib.parse.quote(name), value=val)
    time.sleep(4)
    return r


TEST = """asm c800 <<T
sei
lda #$87
sta $de02
ldx #0
l1: lda $c900,x
sta $df80,x
inx
cpx #124
bne l1
ldx #0
l2: lda $df80,x
sta $c980,x
inx
cpx #124
bne l2
ldx #0
l3: lda $df00,x
sta $ca00,x
inx
bne l3
brk
T"""


def probe_io2():
    c = Client(HOST, timeout=10)
    m = lambda s: c.mon(s, timeout=30)  # noqa: E731
    m("g")
    rest("PUT", "/v1/machine:reset")
    time.sleep(8)
    m("pause")
    m("obs * del")
    m("del")
    m(TEST)
    pat = bytes((i * 7 + 0x13) & 0xFF for i in range(124))
    m("obs e1 when store de02 do break")
    m("wr 0277 34")
    m("wr c6 01")
    m("g")
    for _ in range(50):
        time.sleep(0.2)
        if c.call("session/state")["runState"] != "running":
            break
    print("stopped:", m("r").splitlines()[1])
    m("z")
    m("r pc=c800")
    m("obs * del")
    for o in range(0, 124, 16):
        m("wr ram %04x " % (0xC900 + o) + " ".join("%02x" % b for b in pat[o:o + 16]))
    m("bk c82b")
    m("g")
    time.sleep(1.0)
    print("test end:", m("r").splitlines()[1])
    raw = c.read(0xC980, 124, "ram")
    print("wrote :", pat.hex())
    print("read  :", raw.hex())
    bad = [i for i in range(124) if raw[i] != pat[i]]
    print("$DF80..$DFFB written then read back by the CPU: %d of 124 bytes wrong, first %s" % (len(bad), bad[:8]))
    print("$DF00-$DFFF as the CPU reads it afterwards:", c.read(0xCA00, 256, "ram").hex())


if __name__ == "__main__":
    cmd = sys.argv[1] if len(sys.argv) > 1 else "io2"
    if cmd == "io2":
        probe_io2()
    elif cmd == "cfg":
        print(json.dumps(rest("GET", CFG), indent=1))
    elif cmd == "set":
        print(setcfg(sys.argv[2], sys.argv[3]))
