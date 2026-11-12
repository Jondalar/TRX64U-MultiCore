#!/usr/bin/env python3
"""REST driver for turbomeas.prg on a real Ultimate (Gideon's core).

    tm.py run                      start turbomeas.prg from USB
    tm.py cfg <item> <value>       set a "U64 Specific Settings" item (memory only)
    tm.py measure d031|- d011 tenths k
    tm.py irq d031|- d011 line samples
    tm.py speeds d011 tenths       $D031 index 0..15, bit 7 = 0 and 1
"""
import json, os, sys, time, urllib.parse, urllib.request

IP = os.environ.get("TM_IP", "192.0.2.189")
BASE = f"http://{IP}/v1"
PRG = "/USB1/xander/turbomeas.prg"
CIA_HZ = 985248.0


def req(method, path, **q):
    url = BASE + path + ("?" + urllib.parse.urlencode(q) if q else "")
    r = urllib.request.Request(url, method=method)
    with urllib.request.urlopen(r, timeout=20) as f:
        return f.read()


def rd(addr, n):
    return req("GET", "/machine:readmem", address=f"{addr:04X}", length=str(n))


def wr(addr, data):
    req("PUT", "/machine:writemem", address=f"{addr:04X}", data=bytes(data).hex())


def cfg(item, value):
    p = "/configs/" + urllib.parse.quote("U64 Specific Settings") + "/" + urllib.parse.quote(item)
    return json.loads(req("PUT", p, value=value))


def run():
    wr(0xC00E, [0])
    local = os.environ.get("TM_PRG_LOCAL")      # a file from this Mac: sent as the request body
    if local:
        r = urllib.request.Request(BASE + "/runners:run_prg", data=open(local, "rb").read(), method="POST",
                                   headers={"Content-Type": "application/octet-stream"})
        urllib.request.urlopen(r, timeout=30).read()
    else:
        req("PUT", "/runners:run_prg", file=PRG)
    for _ in range(50):
        time.sleep(0.2)
        if rd(0xC00E, 1)[0] == 0xA5:
            break
    else:
        raise SystemExit("turbomeas did not start")
    time.sleep(float(os.environ.get("TM_HOLD_WAIT", "2.5")))  # past the post-reset 1 MHz hold (2^22 PHI2 cycles = 4.26 s: TM_HOLD_WAIT=5)


def command(cmd, timeout=60, **p):
    seq = rd(0xC00F, 1)[0]
    params = [p.get("d031") or 0, 1 if p.get("d031") is not None else 0, p.get("d011", 0x1B),
              p.get("tenths", 20), p.get("k", 1), p.get("line", 0x35), p.get("samples", 32)]
    wr(0xC001, params)
    wr(0xC000, [cmd])
    t0 = time.time()
    while time.time() - t0 < timeout:
        time.sleep(0.1)
        if rd(0xC00F, 1)[0] != seq:
            return time.time() - t0
    raise SystemExit("command timed out")


def ticks(t0, t1):
    a = (t0[0] << 24) | (t0[1] << 16) | (t0[2] << 8) | t0[3]
    b = (t1[0] << 24) | (t1[1] << 16) | (t1[2] << 8) | t1[3]
    return (a - b) & 0xFFFFFFFF


def measure(d031, d011, tenths, k):
    wall = command(1, d031=d031, d011=d011, tenths=tenths, k=k)
    r = rd(0xC010, 0x13)
    outer = r[0] | r[1] << 8 | r[2] << 16
    frames = r[3] | r[4] << 8
    t = ticks(r[5:9], r[9:13])
    secs = tenths / 10.0
    cyc = outer * (k * 1286 + 30)  # approximate CPU cycles per outer iteration
    res = dict(d031=None if d031 is None else f"{d031:02X}", d011=f"{d011:02X}", tenths=tenths, k=k,
               outer=outer, frames=frames, frames_per_s=frames / secs, cia2_ticks=t,
               cia2_hz=t / secs, mhz_est=cyc / secs / 1e6,
               d031_before=f"{r[13]:02X}", d031_after=f"{r[14]:02X}", d030=f"{r[15]:02X}",
               wall_s=round(wall, 2))
    return res


def irq(d031, d011, line, samples):
    command(2, d031=d031, d011=d011, line=line, samples=samples)
    rl, rh = rd(0xC100, samples), rd(0xC200, samples)
    il, ih, ln = rd(0xC300, samples), rd(0xC400, samples), rd(0xC500, samples)
    out = []
    for i in range(samples):
        ref = rh[i] << 8 | rl[i]
        ir = ih[i] << 8 | il[i]
        out.append(((ref - ir) % 19656, ln[i], ref, ir))
    return out


def main():
    a = sys.argv[1:]
    h = lambda s: None if s == "-" else int(s, 16)
    if a[0] == "run":
        run()
    elif a[0] == "cfg":
        print(cfg(a[1], a[2]))
    elif a[0] == "measure":
        print(json.dumps(measure(h(a[1]), int(a[2], 16), int(a[3]), int(a[4]))))
    elif a[0] == "irq":
        res = irq(h(a[1]), int(a[2], 16), int(a[3], 16), int(a[4]))
        d = [x[0] for x in res]
        print(json.dumps(dict(lat=d, lines=[x[1] for x in res], min=min(d), max=max(d),
                              ref=[x[2] for x in res[:4]], irq=[x[3] for x in res[:4]])))


if __name__ == "__main__":
    main()


def timeline(tenths=2, k=16, secs=6.0, pokes=(), cfgs=()):
    """Run the monitor; pokes/cfgs = [(t_seconds, value)] applied during the run.
    Returns [(x_speed, d031_read)] per period."""
    wr(0xC020, [0, 0])
    wr(0xC004, [tenths, k])
    wr(0xC008, [0, 0])
    wr(0xC000, [3])
    t0 = time.time()
    ev = sorted([(t, "poke", v) for t, v in pokes] + [(t, "cfg", v) for t, v in cfgs])
    log = []
    while time.time() - t0 < secs:
        if ev and time.time() - t0 >= ev[0][0]:
            t, kind, v = ev.pop(0)
            if kind == "poke":
                wr(0xC008, [1, v])
            else:
                cfg(*v)
            log.append((round(time.time() - t0, 2), kind, v, rd(0xC021, 1)[0]))
        time.sleep(0.05)
    wr(0xC000, [4])
    time.sleep(tenths / 10.0 * 2 + 0.3)
    n = rd(0xC021, 1)[0]
    ring = rd(0xC600, 256)
    out = []
    for i in range(min(n, 64)):
        e = ring[4 * i:4 * i + 4]
        o = e[0] | e[1] << 8 | e[2] << 16
        out.append((round(o * (1286 * k + 32) / (tenths / 10.0) / 985248, 2), f"{e[3]:02X}"))
    return out, log
