#!/usr/bin/env python3
"""T40 on the board: System Mode switched over REST (memory only, never saved).

    tests/t40_modes.py [modes ...]      default: PAL NTSC PAL-60 NTSC-50 PAL

For every mode, while a probe at $C000 runs (started once, with SYS 49152):
  - the app's config item "System Mode" (U64 Specific Settings) is set;
  - JTAG (tools/trxdbg.sh mode): mode_ctl's mode, the VIC's lines and dot4x
    clocks per line and frame;
  - the probe goes on (its loop counter), the KERNAL's jiffy clock runs, and
    $D011/$D018/$D020 as the probe last copied them are what they were;
  - SID voice 1 (triangle, frequency register 0x1CD6 = 440 Hz at PAL's phi2)
    in the UDP audio stream: the pitch follows phi2 (x 1.038 at 60 Hz modes);
  - one UDP video frame: its height (272 at 50 Hz, 240 at 60 Hz).
Needs the board on TRX64_HOST (default 192.0.2.189) with a test
bitstream and the app; numpy for the pitch.
"""
import json
import os
import socket
import subprocess
import sys
import time
import urllib.parse
import urllib.request

HOST = os.environ.get("TRX64_HOST", "192.0.2.189")
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FS = 48003.07                       # our audio frame rate (T25.3, mode independent)
PHI2 = {"PAL": 50e6 * 56.75 / 90 / 32, "NTSC": 50e6 * 47.125 / 72 / 32,
        "PAL-60": 50e6 * 43.25 / 66 / 32, "NTSC-50": 50e6 * 35.375 / 56 / 32}
SID_F = 0x1CD6
V60 = {"NTSC", "PAL-60"}

PROBE = bytes([
    0xA9, 0x00, 0x8D, 0x0B, 0xDC, 0x8D, 0x0A, 0xDC, 0x8D, 0x09, 0xDC, 0x8D, 0x08, 0xDC,
    0xAD, 0x08, 0xDC, 0x8D, 0x00, 0xC1, 0xAD, 0x09, 0xDC, 0x8D, 0x01, 0xC1,
    0xEE, 0x02, 0xC1, 0xD0, 0x03, 0xEE, 0x03, 0xC1,
    0xAD, 0x18, 0xD0, 0x8D, 0x04, 0xC1, 0xAD, 0x20, 0xD0, 0x8D, 0x05, 0xC1,
    0xAD, 0x11, 0xD0, 0x29, 0x7F, 0x8D, 0x06, 0xC1, 0x4C, 0x0E, 0xC0])


def req(method, path, **q):
    url = f"http://{HOST}/v1" + path + ("?" + urllib.parse.urlencode(q) if q else "")
    with urllib.request.urlopen(urllib.request.Request(url, method=method), timeout=20) as f:
        return f.read()


def rd(addr, n):
    return req("GET", "/machine:readmem", address=f"{addr:04X}", length=str(n))


def wr(addr, data):
    req("PUT", "/machine:writemem", address=f"{addr:04X}", data=bytes(data).hex())


def set_mode(name):
    p = "/configs/" + urllib.parse.quote("U64 Specific Settings") + "/" + urllib.parse.quote("System Mode")
    return json.loads(req("PUT", p, value=name))


def jtag_mode():
    out = subprocess.run([os.path.join(ROOT, "tools", "trxdbg.sh"), "mode"], capture_output=True, text=True)
    return out.stdout.strip()


def my_ip():
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    s.connect((HOST, 80))
    ip = s.getsockname()[0]
    s.close()
    return ip


def pitch(seconds=1.0):
    import numpy as np
    port = 11001
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    sock.bind(("", port))
    sock.settimeout(3.0)
    req("PUT", "/streams/audio:start", ip=f"{my_ip()}:{port}")
    pcm = []
    try:
        while len(pcm) < FS * seconds:
            data, sender = sock.recvfrom(2048)
            if sender[0] != HOST or len(data) != 770:
                continue
            pcm += list(np.frombuffer(data[2:], dtype="<i2")[0::2])
    finally:
        req("PUT", "/streams/audio:stop")
        sock.close()
    x = np.array(pcm, dtype=float)
    x -= x.mean()
    spec = np.abs(np.fft.rfft(x * np.hanning(len(x)), n=1 << 20))
    k = int(np.argmax(spec[1:])) + 1
    return k * FS / (1 << 20)


def frame_height():
    out = subprocess.run([sys.executable, os.path.join(ROOT, "tools", "stream_shot.py"), HOST,
                          "/tmp/t40_shot.png"], capture_output=True, text=True)
    for line in out.stdout.splitlines():
        if line.startswith("frame "):
            return line
    return out.stdout.strip() or out.stderr.strip()


def main(modes):
    fails = 0
    wr(0xC000, PROBE)
    wr(0x0277, b"SYS49152\r")
    wr(0x00C6, [9])
    time.sleep(1.0)
    regs0 = rd(0xC104, 3)
    # SID voice 1: triangle at SID_F, full volume, sustain
    wr(0xD400, [SID_F & 0xFF, SID_F >> 8, 0, 0, 0x11, 0x00, 0xF0])
    wr(0xD418, [0x0F])
    for m in modes:
        t0 = time.time()
        print(f"== {m}: {set_mode(m)}")
        time.sleep(2.0)
        print(jtag_mode())
        j0, l0 = rd(0xA2, 1)[0], rd(0xC103, 1)[0]
        time.sleep(1.0)
        j1, l1 = rd(0xA2, 1)[0], rd(0xC103, 1)[0]
        regs = rd(0xC104, 3)
        f = pitch()
        want = SID_F * PHI2[m] / 2 ** 24
        print(f"jiffies/s {(j1 - j0) & 0xFF}, probe loop +{(l1 - l0) & 0xFF}, VIC regs {regs.hex()} (was {regs0.hex()}), "
              f"SID {f:.1f} Hz (want {want:.1f}), {frame_height()}  [{time.time() - t0:.1f} s]")
        ok = (j1 - j0) & 0xFF > 40 and (l1 - l0) & 0xFF and regs == regs0 and abs(f / want - 1) < 0.003
        if not ok:
            fails += 1
            print("FAIL")
    wr(0xD418, [0])
    print("PASS" if not fails else f"FAIL {fails}")
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:] or ["PAL", "NTSC", "PAL-60", "NTSC-50", "PAL"]))
