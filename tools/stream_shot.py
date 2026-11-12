#!/usr/bin/env python3
"""Screenshot of the C64 picture from the Ultimate app's UDP video stream.

    tools/stream_shot.py <board ip> [out.png] [--keep]

Starts the VIC stream over REST to this Mac (unicast, port 11000), receives
packets until one frame is complete, decodes it with the 1541ultimate e2e
decoder (tests/e2e/lib/streams.py: FrameAssembler, unpack) and writes a PNG
with the Pepto PAL palette; then stops the stream again unless --keep.
Set U64_E2E_LIB if the decoder is not in ~/Development/1541ultimate.
"""
import importlib.util
import os
import socket
import subprocess
import sys

LIB = os.environ.get("U64_E2E_LIB",
                     os.path.expanduser("~/Development/1541ultimate/tests/e2e/lib/streams.py"))
PORT = 11000
# Pepto's PAL palette (VICE "pepto-pal")
PALETTE = [
    0x000000, 0xFFFFFF, 0x68372B, 0x70A4B2, 0x6F3D86, 0x588D43, 0x352879, 0xB8C76F,
    0x6F4F25, 0x433900, 0x9A6759, 0x444444, 0x6C6C6C, 0x9AD284, 0x6C5EB5, 0x959595,
]


def load_streams():
    # streams.py imports the e2e test helpers (report, targets) at module
    # level; give it their directories, the module itself is used standalone.
    lib_dir = os.path.dirname(LIB)
    sys.path.insert(0, lib_dir)
    sys.path.insert(0, os.path.join(os.path.dirname(os.path.dirname(lib_dir)), "lib"))
    spec = importlib.util.spec_from_file_location("u64_streams", LIB)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


def my_ip_towards(host):
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        s.connect((host, 80))
        return s.getsockname()[0]
    finally:
        s.close()


def rest(host, path):
    # curl, not urllib: on macOS a Python started from a sandboxed tool may lack Local Network access ("No route to host")
    r = subprocess.run(["curl", "-s", "-m", "10", "-X", "PUT", f"http://{host}{path}"], capture_output=True, text=True)
    if r.returncode:
        raise OSError(f"curl PUT {path}: rc {r.returncode}")
    return r.stdout


def main(argv):
    if not argv:
        print(__doc__)
        return 1
    host = argv[0]
    out = argv[1] if len(argv) > 1 and not argv[1].startswith("--") else "stream_shot.png"
    keep = "--keep" in argv
    streams = load_streams()
    from PIL import Image

    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    sock.bind(("", PORT))
    sock.settimeout(3.0)
    me = my_ip_towards(host)
    print("start:", rest(host, f"/v1/streams/video:start?ip={me}:{PORT}").strip())
    try:
        asm = streams.FrameAssembler()
        frame = None
        packets = 0
        while frame is None and packets < 1000:
            data, sender = sock.recvfrom(2048)
            if sender[0] != host:
                continue
            packets += 1
            frame = asm.push(data[0:])
        if frame is None:
            print(f"no complete frame in {packets} packets")
            return 1
        pixels = streams.unpack(frame.packed)
        img = Image.frombytes("P", (frame.width, frame.height), pixels)
        img.putpalette([c >> s & 0xFF for c in PALETTE for s in (16, 8, 0)])
        img.save(out)
        print(f"frame {frame.number}: {frame.width}x{frame.height} after {packets} packets -> {out}")
        return 0
    finally:
        if not keep:
            print("stop:", rest(host, "/v1/streams/video:stop").strip())
        sock.close()


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
