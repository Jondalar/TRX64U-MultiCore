#!/usr/bin/env python3
"""Decode tb_udp_audio's audio payloads (sim_audio.hex) with the 1541ultimate
e2e AudioTimeline and check the PCM: no loss, L counts up, R = ~L.
Set U64_E2E_LIB if the decoder is not in ~/Development/1541ultimate."""
import os, struct, sys
sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "..", "..", "tools"))
from stream_shot import load_streams

s = load_streams()
tl = s.AudioTimeline()
pcm = b""
for line in open(sys.argv[1] if len(sys.argv) > 1 else "sim_audio.hex"):
    pcm += tl.push(bytes.fromhex(line.strip())).pcm
c = tl.counts()
frames = [struct.unpack_from("<2H", pcm, i) for i in range(0, len(pcm), s.FRAME_BYTES)]
ok = (c["packets"] >= 6 and c["packets_written"] == c["packets"] and c["malformed"] == 0
      and c["packets_lost"] == 0
      and all(r == l ^ 0xFFFF for l, r in frames)
      and all((b[0] - a[0]) & 0xFFFF == 1 for a, b in zip(frames, frames[1:])))
print(c["packets"], "packets,", len(frames), "frames:", "PASS" if ok else "FAIL")
sys.exit(0 if ok else 1)
