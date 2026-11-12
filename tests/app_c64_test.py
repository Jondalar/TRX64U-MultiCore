#!/usr/bin/env python3
"""The Ultimate app's C64 control on c64_basic, from the Mac (step C).

    tests/run_app_c64_test.sh <board ip>

Needs c64_basic with the app running (tools/trxdbg.sh riscv ultimate.bin) and
on the network. Checks, each against the C64 RAM read over JTAG as well:
  1. REST writemem / readmem round trip at $C000;
  2. REST run_prg (POST, a BASIC program that prints a marker): the marker on
     the text screen, read by REST readmem and by trxdbg;
  3. keyboard buffer over the socket API (port 64, SOCKET_CMD_KEYB):
     PRINT 6*7 typed into BASIC, 42 on the screen;
  4. REST machine:reset: the screen cleared, the BASIC banner again;
  5. REST machine:pause / resume: the jiffy clock stops and runs again.
"""
import os
import random
import socket
import struct
import sys
import time
import urllib.request

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(os.path.dirname(HERE), "tools"))
import trxdbg  # noqa: E402

MARKER = "TRX64 RUN OK"


def http(host, method, path, body=None, ctype=None):
    req = urllib.request.Request(f"http://{host}{path}", data=body, method=method)
    if ctype:
        req.add_header("Content-Type", ctype)
    with urllib.request.urlopen(req, timeout=20) as r:
        return r.read()


def screen_text(raw):
    return "\n".join("".join(trxdbg.screen_char(c) for c in raw[r * 40:r * 40 + 40]).rstrip()
                     for r in range(25))


def rest_screen(host):
    return screen_text(http(host, "GET", "/v1/machine:readmem?address=0400&length=1000"))


def jtag_screen(d):
    return screen_text(d.read_many([0x0400 + i for i in range(1000)]))


def basic_prg(text):
    """10 PRINT"<text>" as a PRG at $0801."""
    line = bytes([0x0A, 0x00, 0x99, 0x22]) + text.encode() + bytes([0x22, 0x00])
    nxt = 0x0801 + 2 + len(line)
    return bytes([0x01, 0x08]) + struct.pack("<H", nxt) + line + b"\0\0"


def keyb(host, text):
    data = text.upper().replace("\n", "\r").encode()
    with socket.create_connection((host, 64), timeout=10) as s:
        s.sendall(struct.pack("<HH", 0xFF03, len(data)) + data)


def wait_for(fn, what, timeout=10.0):
    t0 = time.time()
    while time.time() - t0 < timeout:
        v = fn()
        if v:
            return v
        time.sleep(0.5)
    return None


def main(argv):
    host = argv[0]
    results = []
    print("version:", http(host, "GET", "/v1/version").decode().strip())
    d = trxdbg.Dbg()
    try:
        # 1. writemem / readmem
        pattern = bytes(random.randrange(256) for _ in range(32))
        http(host, "PUT", f"/v1/machine:writemem?address=C000&data={pattern.hex()}")
        back = http(host, "GET", "/v1/machine:readmem?address=C000&length=32")
        jtag = bytes(d.read(0xC000 + i) & 0xFF for i in range(32))
        ok = back == pattern and jtag == pattern
        results.append(("writemem/readmem $C000 (REST and JTAG)", ok))
        print(f"1. wrote {pattern.hex()}\n   REST {back.hex()}\n   JTAG {jtag.hex()}")

        # 2. run_prg
        http(host, "POST", "/v1/runners:run_prg", basic_prg(MARKER), "application/octet-stream")
        rs = wait_for(lambda: (lambda t: t if MARKER in t else None)(rest_screen(host)), "marker")
        js = jtag_screen(d)
        ok = rs is not None and MARKER in js
        results.append(("run_prg: marker on the screen (REST readmem and JTAG)", ok))
        print("2. screen after run_prg (JTAG):\n" + js)

        # 3. keyboard buffer
        keyb(host, "PRINT 6*7\n")
        ok = wait_for(lambda: " 42" in jtag_screen(d), "42") is not None
        results.append(("socket 64 keyboard buffer: PRINT 6*7 -> 42", ok))
        print("3. screen after typing (JTAG):\n" + jtag_screen(d))

        # 5. pause / resume: the jiffy clock ($A2) stands still while paused
        http(host, "PUT", "/v1/machine:pause")
        a = d.read(0xA2) & 0xFF
        time.sleep(0.5)
        b = d.read(0xA2) & 0xFF
        http(host, "PUT", "/v1/machine:resume")
        time.sleep(0.5)
        c = d.read(0xA2) & 0xFF
        results.append(("machine:pause / resume (jiffy clock $A2)", a == b and c != b))
        print(f"5. $A2 paused {a:02X} {b:02X}, resumed {c:02X}")

        # 4. reset: a row of X on the bottom line must be gone, the banner there
        http(host, "PUT", "/v1/machine:writemem?address=07C0&data=" + "18" * 40)
        http(host, "PUT", "/v1/machine:reset")
        ok = wait_for(lambda: (lambda t: "COMMODORE 64 BASIC V2" in t and "X" * 40 not in t)(
            rest_screen(host)), "banner") is not None
        results.append(("machine:reset: BASIC banner", ok))
        print("4. screen after reset (REST):\n" + rest_screen(host))
    finally:
        d.close()
    for name, ok in results:
        print(f"{'PASS' if ok else 'FAIL'}  {name}")
    return 0 if all(ok for _, ok in results) else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
