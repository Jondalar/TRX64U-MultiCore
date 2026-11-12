#!/usr/bin/env python3
"""T104.2 S12 on the script: an image at the limit passes, one byte over fails, a header that disagrees with the file fails."""
import os
import struct
import subprocess
import sys
import tempfile

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
SCRIPT = os.environ.get("TRX_SIZE_SCRIPT") or os.path.join(ROOT, "tools", "check_app_size.py")
LIMIT = 0x1E0000 - 12


def image(n, header_len=None):
    return struct.pack("<III", 0x30000, n if header_len is None else header_len, 0x30000) + bytes(n)


def run(data):
    with tempfile.NamedTemporaryFile(suffix=".app", delete=False) as f:
        f.write(data)
    try:
        p = subprocess.run([sys.executable, "-I", SCRIPT, f.name], capture_output=True, text=True)
        return p.returncode, p.stdout + p.stderr
    finally:
        os.unlink(f.name)


fails = 0


def check(cond, what):
    global fails
    if not cond:
        print("FAIL %s" % what)
        fails += 1


rc, out = run(image(LIMIT))
check(rc == 0 and "0 of it unused" in out, "an image of exactly the slot passes (%s)" % out.strip())
rc, out = run(image(LIMIT - 1000))
check(rc == 0 and "1000 of it unused" in out, "a smaller one passes and says what is left")
rc, out = run(image(LIMIT + 1))
check(rc == 1 and "1 over" in out, "one byte over the slot fails (%s)" % out.strip())
rc, out = run(image(LIMIT + 100000))
check(rc == 1, "far over fails")
rc, out = run(image(1000, header_len=900))
check(rc == 1 and "record says 900" in out, "a header that disagrees with the file fails")
rc, out = run(b"\x00" * 8)
check(rc == 1 and "record" in out, "a file shorter than the record fails")
print("test_appsize: %d failed" % fails)
sys.exit(1 if fails else 0)
