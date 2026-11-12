#!/usr/bin/env python3
"""The cartridge-type tests of the simulation (host/sim/cart/carts.py)
on the board (T33 §4.2 B1/B3): every CRT is copied to the board over FTP, started
with PUT /v1/runners:run_crt, and its result block ($0400.., done when $07E7 =
$AA) read with REST readmem and compared with what the simulation expects.

    carts.py --emit DIR [name ...]          (once, in host/sim/cart)
    carts_board.py DIR [name ...]

TRX64_HOST (default 192.0.2.189), TRX64_CART_DIR (default /USB1/t33). Uploads
before every run: a flash test changes its image, and the app writes it back.
Needs no JTAG.
"""
import ftplib
import io
import os
import sys
import time
import urllib.parse
import urllib.request

HOST = os.environ.get("TRX64_HOST", "192.0.2.189")
BOARD_DIR = os.environ.get("TRX64_CART_DIR", "/USB1/t33")


def http(method, path, body=None):
    req = urllib.request.Request("http://%s%s" % (HOST, path), data=body, method=method)
    with urllib.request.urlopen(req, timeout=30) as r:
        return r.read()


def readmem(addr, n):
    return http("GET", "/v1/machine:readmem?address=%04X&length=%d" % (addr, n))


def upload(name, data):
    ftp = ftplib.FTP(HOST, timeout=30)
    ftp.login()
    try:
        try:
            ftp.mkd(BOARD_DIR)
        except ftplib.error_perm:
            pass
        ftp.storbinary("STOR %s/%s" % (BOARD_DIR, name), io.BytesIO(data))
    finally:
        ftp.quit()


def run(d, name, timeout=20.0):
    if name.startswith("easyflash3"):
        timeout = 90.0                      # an 8 MiB CRT: the upload, the load, and the menu start's reset
    crt = open(os.path.join(d, name + ".crt"), "rb").read()
    want = bytes.fromhex(open(os.path.join(d, name + ".expect")).read().strip())
    upload(name + ".crt", crt)
    http("PUT", "/v1/machine:writemem?address=07E7&data=00")
    http("PUT", "/v1/runners:run_crt?file=" + urllib.parse.quote("%s/%s.crt" % (BOARD_DIR, name)))
    end = time.monotonic() + timeout
    while readmem(0x07E7, 1)[0] != 0xAA:
        if time.monotonic() > end:
            print("%-14s FAILED: no end marker; $0400: %s" % (name, readmem(0x0400, len(want) or 16).hex()))
            return False
        time.sleep(0.5)
    got = readmem(0x0400, len(want))
    ok = got == want
    print("%-14s %s" % (name, "ok" if ok else "FAILED\n  got  %s\n  want %s" % (got.hex(), want.hex())))
    return ok


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return 2
    d = sys.argv[1]
    names = sys.argv[2:] or sorted(f[:-4] for f in os.listdir(d) if f.endswith(".crt"))
    bad = [n for n in names if not run(d, n)]
    print("all passed" if not bad else "failed: " + " ".join(bad))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
