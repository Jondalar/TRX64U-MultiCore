#!/usr/bin/env python3
"""The USB stick for tests/hostapp/emu_txu.sh (T152): the .txu files of mktxutest.py in upd/, vendor-style .ue2/.cfw with and without an ESP32 image set in esp/,
three partial cores routed for the emulated host and two large cores in cores/ (the question of a host update counts them).

  mkemustick.py <outdir> <host key of the emulated host, 8 hex digits>

The host key is what the host app prints at start ("host key xxxxxxxx") in the emulator's log."""
import os
import subprocess
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "..", "tools"))
import cor_pack as C          # noqa: E402
import test_cor_pack as TC    # noqa: E402


def main(out, key):
    for d in ("upd", "esp", "cores"):
        os.makedirs(os.path.join(out, d), exist_ok=True)
    subprocess.check_call([sys.executable, os.path.join(HERE, "mktxutest.py"), os.path.join(out, "upd")])
    subprocess.check_call([sys.executable, os.path.join(HERE, "mkesptest.py"), os.path.join(out, "esp")])
    for name in os.listdir(os.path.join(out, "upd")):
        if name not in ("both.txu", "iface_bad.txu", "iface_fixed.txu", "newkey.txu", "payload_app.txu", "not_txu.cfw"):     # the list of the script of the screenshots
            os.remove(os.path.join(out, "upd", name))
    for name in os.listdir(os.path.join(out, "esp")):
        if name not in ("v111.ue2", "v18.ue2", "app_only.ue2", "damaged.ue2", "plain.cfw"):
            os.remove(os.path.join(out, "esp", name))
    hdr = C.BIT_MAGIC
    for k in b"abcd":
        hdr += bytes([k]) + struct.pack(">H", 6) + b"xxxxx\0"
    data = b"\xFF" * 8 + C.SYNC + bytes(range(256)) * 16
    bit = hdr + b"e" + struct.pack(">I", len(data)) + data
    app = bytes((i * 5) & 0xFF for i in range(3000))
    for i in (1, 2, 3):
        open(os.path.join(out, "cores", "partial%d.cor" % i), "wb").write(C.pack("Partial core %d" % i, "1.%d" % i, bit, app, 0x30000, 0x30010, key))
    icon = bytes(2048)
    for i in (1, 2):
        open(os.path.join(out, "cores", "large%d.cor" % i), "wb").write(
            C.pack_large(cid="large%d" % i, name="Large core %d" % i, version="2.%d" % i, bit=TC.full_bit(frames=2000),
                         app=bytes((j * 3) & 0xFF for j in range(70001)), icon=icon))


if __name__ == "__main__":
    main(sys.argv[1], sys.argv[2])
