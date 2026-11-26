#!/usr/bin/env python3
"""T265 P2: the recovery program's image for its slot: the .app record {dest, length, run} and its data (hex2bin -r), padded to 4, then a CRC-32 (zlib, little endian)
over all of it. The boot ROM checks the trailer before it starts the program (docs/specs/T265/p1-p3-guard-and-recovery.md 1.3 step 6 and 2.6).

    tools/rec_trailer.py in.app out.app [limit]      limit: the most data the slot takes (default 0x3EFF0: the 0x3F000 slot less the record and the trailer); exit 1 above it
"""
import struct
import sys
import zlib

SLOT = 0x3F000
HEADER = 12
TRAILER = 4
LIMIT = SLOT - HEADER - TRAILER


def make(app, limit=LIMIT):
    """(image, message). image is None when the program does not fit."""
    if len(app) < HEADER:
        return None, "%d bytes: not even the %d byte record" % (len(app), HEADER)
    dest, length, run = struct.unpack("<III", app[:HEADER])
    if len(app) != HEADER + length:
        return None, "the record says %d bytes of data but the file holds %d" % (length, len(app) - HEADER)
    padded = (length + 3) & ~3
    if padded > limit:
        return None, "the recovery program is %d bytes of data, %d over its slot's %d" % (padded, padded - limit, limit)
    body = app + b"\0" * (padded - length)
    img = body + struct.pack("<I", zlib.crc32(body) & 0xFFFFFFFF)
    return img, "%d bytes of data (dest 0x%X, run 0x%X), %d left in the slot, image %d bytes" % (padded, dest, run, limit - padded, len(img))


def main(argv):
    if len(argv) < 3:
        print(__doc__)
        return 2
    limit = int(argv[3], 0) if len(argv) > 3 else LIMIT
    with open(argv[1], "rb") as f:
        img, msg = make(f.read(), limit)
    if img is None:
        print("%s: %s" % (argv[1], msg), file=sys.stderr)
        return 1
    with open(argv[2], "wb") as f:
        f.write(img)
    print("%s: %s" % (argv[2], msg))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
