#!/usr/bin/env python3
"""T74: what does a CPU read of an address nobody answers return? Same script for Gideon's core and ours.

    TM_IP=<board> tests/openbus/openbus.py [--speed N]

Setup: no cartridge in the port; Command Interface and Ultimate Audio off (they answer $DF1C-$DFFF), REU off for the
first run and on for a second one (then $DF00-$DF0A are the REU's; $DF0B, $DF80, $DFFF tell whether the REU mirrors
or leaves the rest open). --speed N: Turbo Control = Manual and CPU Speed N MHz (U64 Specific Settings, memory only) for the run,
restored to Turbo Registers / 1 at the end (a machine reset is not needed; the post-reset hold is waited out).

Runs openbus.prg (REST runners:run_prg). It switches the display off (DEN) and puts a known byte at $3FFF (the VIC's
idle fetch), another at $3F00-$3FFE (the 5 refresh reads per line) and a third at $07F8-$07FF (the 8 sprite pointer
reads per line, also with no sprite on), then reads $DE00, $DEFF, $DF00, $DF0B, $DF80, $DFFF and $D800 (colour RAM)
256 times each, with and without a write of $A5 to the same address before every read, for two fixture sets. A real
C64 returns the VIC's byte of the phi1 before the read (one of the three bytes); a core that echoes the CPU's write
returns $A5. Prints one histogram and a verdict per case."""
import os, sys, time, json, urllib.parse, urllib.request

IP = os.environ.get("TM_IP", "192.0.2.189")
BASE = f"http://{IP}/v1"
HERE = os.path.dirname(os.path.abspath(__file__))
PRG = os.path.join(HERE, "openbus.prg")


def req(method, path, body=None, **q):
    url = BASE + path + ("?" + urllib.parse.urlencode(q) if q else "")
    r = urllib.request.Request(url, method=method, data=body,
                               headers={"Content-Type": "application/octet-stream"} if body else {})
    with urllib.request.urlopen(r, timeout=30) as f:
        return f.read()


def rd(addr, n):
    return req("GET", "/machine:readmem", address=f"{addr:04X}", length=str(n))


def wr(addr, data):
    req("PUT", "/machine:writemem", address=f"{addr:04X}", data=bytes(data).hex())


def cfg(item, value):
    p = "/configs/" + urllib.parse.quote("U64 Specific Settings") + "/" + urllib.parse.quote(item)
    return json.loads(req("PUT", p, value=value))


def start():
    wr(0xC0FE, [0])
    req("POST", "/runners:run_prg", open(PRG, "rb").read())
    for _ in range(100):
        time.sleep(0.2)
        if rd(0xC0FE, 1)[0] == 0xA5:
            return
    raise SystemExit("openbus.prg did not start")


def measure(idle, refresh, ptr, target, pre):
    wr(0xC0F0, [idle, refresh, target & 0xFF, target >> 8, pre])
    wr(0xC0F6, [ptr])
    wr(0xC0FF, [0])
    wr(0xC0F5, [1])
    for _ in range(100):
        time.sleep(0.1)
        if rd(0xC0FF, 1)[0] == 1:
            break
    else:
        raise SystemExit("no result")
    h = {}
    for v in rd(0xC200, 256):
        h[v] = h.get(v, 0) + 1
    return h


# (idle $3FFF, refresh $3F00-$3FFE, sprite pointers $07F8-$07FF): two fixtures so a constant cannot pass by luck
FIXTURES = ((0x5C, 0x11, 0x22), (0xC3, 0x4E, 0x77))
TARGETS = (0xDE00, 0xDEFF, 0xDF00, 0xDF0B, 0xDF80, 0xDFFF, 0xD800)


def verdict(h, fx, target, pre):
    vic = set(fx)
    if target == 0xD800:            # colour RAM: 4 bits of its own, the upper nibble is the bus
        up = {v >> 4 for v in vic}
        return "upper nibble VIC" if all(v >> 4 in up for v in h) else "upper nibble NOT the VIC byte (%s)" % \
            ("echo of the write" if pre and all(v >> 4 == 0xA for v in h) else "other")
    if set(h) <= vic and fx[0] in h:
        return "VIC byte"
    if pre and h.get(0xA5, 0) > 128:
        return "ECHO of the write"
    if len(h) == 1:
        return "constant $%02X (a device answers)" % next(iter(h))
    return "other"


def main():
    speed = None
    if "--speed" in sys.argv:
        speed = int(sys.argv[sys.argv.index("--speed") + 1])
        cfg("Turbo Control", "Manual")
        cfg("CPU Speed", f"{speed:>2}")
    start()
    time.sleep(float(os.environ.get("TM_HOLD_WAIT", "5")))   # past the post-reset 1 MHz hold
    bad = 0
    for fx in FIXTURES:
        for target in TARGETS:
            for pre in (0, 1):
                h = measure(fx[0], fx[1], fx[2], target, pre)
                v = verdict(h, fx, target, pre)
                bad += v not in ("VIC byte", "upper nibble VIC")
                print("idle $%02X refresh $%02X ptr $%02X  $%04X write-first %d: %-48s -> %s" % (
                    fx + (target, pre, " ".join("%02X:%d" % kv for kv in sorted(h.items())), v)))
    if speed:
        cfg("CPU Speed", " 1")
        cfg("Turbo Control", "U64 Turbo Registers")
    print("openbus: %s" % ("every case is the VIC byte" if not bad else "%d case(s) are not the VIC byte" % bad))


if __name__ == "__main__":
    main()
