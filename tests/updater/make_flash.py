#!/usr/bin/env python3
"""A 16 MiB SPI flash image for ue2emu with a firmware in it, as the board's flash holds it: bitstream at 0x000000, application at 0x220000.

  make_flash.py vendor <vendor.ue2> <out.bin>          the bitstream and the application of a Commodore/Gideon .ue2 (the file a person has)
  make_flash.py ours <bitstream.bit> <app> <out.bin>   a package's own parts (our firmware installed: the bitstream carries our stamp)
  make_flash.py stamped <bitstream.bit> <vendor.ue2> <out.bin>   a self-built firmware (T135 B1, issue #6): a bitstream that writes the stamp and the
                                                       vendor's application, which names no TRX64

The .ue2 of Commodore's 1.1.0 is one updater record whose blobs are the swapped bitstreams and the application; the bitstream is found by the
.bit header, the application by the version string it carries ("Software version:") and its own 12 byte record header (load, length, entry).
The ue2emu run formats the flash drive (0x400000) itself the first time the firmware boots, as the board's firmware does."""
import struct
import sys

SIZE = 16 << 20


def bit_len(b, at):
    p = at + 13
    for key in b"abcd":
        p += 3 + struct.unpack(">H", b[p + 1:p + 3])[0]
    return p + 5 + struct.unpack(">I", b[p + 1:p + 5])[0] - at


def vendor(ue2):
    d = open(ue2, "rb").read()
    magic = bytes([0, 9, 0xF, 0xF0, 0xF, 0xF0, 0xF, 0xF0, 0xF, 0xF0, 0, 0, 1])
    at = d.find(magic)
    assert at > 0, "no .bit header"
    bit = d[at:at + bit_len(d, at)]
    v = d.find(b"Software version:")
    assert v > 0, "no version string"
    # the application's record header: the nearest 12 bytes in front of the version string that read as (load 0x30000, length, entry 0x30000)
    for s in range(v - 12, max(v - 0x400000, 0), -4):
        load, length, entry = struct.unpack_from("<III", d, s)
        if load == 0x30000 and entry == 0x30000 and 0 < length < 0x1E0000 and s + 12 + length > v:
            return bit, d[s:s + 12 + length]
    raise SystemExit("no application record in front of the version string")


def main():
    if len(sys.argv) == 4 and sys.argv[1] == "vendor":
        bit, app = vendor(sys.argv[2])
        out = sys.argv[3]
    elif len(sys.argv) == 5 and sys.argv[1] == "stamped":
        bit, app = open(sys.argv[2], "rb").read(), vendor(sys.argv[3])[1]
        out = sys.argv[4]
    elif len(sys.argv) == 5 and sys.argv[1] == "ours":
        bit, app, out = open(sys.argv[2], "rb").read(), open(sys.argv[3], "rb").read(), sys.argv[4]
    else:
        raise SystemExit(__doc__)
    assert len(bit) <= 0x220000 and len(app) <= 0x1E0000
    f = bytearray(b"\xFF" * SIZE)
    f[:len(bit)] = bit
    f[0x220000:0x220000 + len(app)] = app
    open(out, "wb").write(f)
    print("%s: bitstream %d bytes, application %d bytes" % (out, len(bit), len(app)))


if __name__ == "__main__":
    main()
