#!/usr/bin/env python3
"""T104.2 S12: does an application image fit the flash slot the TRX64 Ultimate's boot ROM starts it from?

The boot ROM (TRX64-Ultimate software/bootrom/bootrom.c, MAX_APPL_SIZE) copies the record at 0x220000 - {dest, length, run}, little
endian, 12 bytes, then `length` bytes of data - and refuses a length above 0x1E0000 - 12 (the slot less the record). An `.app` is that
record and its data, so the file is 12 + length bytes. Checked here: the header and the file agree, and the length fits.

    tools/check_app_size.py coreapp.app [limit]      exit 0 and a line "... N bytes of data, M left in the slot" / exit 1
"""
import struct
import sys

SLOT = 0x1E0000
HEADER = 12
LIMIT = SLOT - HEADER


def check(data, limit=LIMIT):
    """(ok, message)."""
    if len(data) < HEADER:
        return False, "%d bytes: not even the %d byte record" % (len(data), HEADER)
    dest, length, run = struct.unpack("<III", data[:HEADER])
    if len(data) != HEADER + length:
        return False, "the record says %d bytes of data but the file holds %d" % (length, len(data) - HEADER)
    if length > limit:
        return False, "%d bytes of data, %d over the slot's %d" % (length, length - limit, limit)
    return True, "%d bytes of data (dest 0x%X, run 0x%X); the slot takes %d, %d of it unused in this image" % (
        length, dest, run, limit, limit - length)


def main(argv):
    if len(argv) < 2:
        print(__doc__)
        return 2
    limit = int(argv[2], 0) if len(argv) > 2 else LIMIT
    with open(argv[1], "rb") as f:
        ok, msg = check(f.read(), limit)
    print("%s: %s" % (argv[1], msg), file=sys.stdout if ok else sys.stderr)
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))
