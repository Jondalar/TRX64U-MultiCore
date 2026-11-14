#!/usr/bin/env python3
"""Files for tests/hostapp/test_espinspect.cc (T152 D8/S9): vendor-style update files that carry (or do not carry) an ESP32-S3 image set in the format the
vendor updater embeds (TRX-64-Ultimate TRX64-Ultimate/docs/specs/T26.0-esp32-facts.md section 2: bootloader, partition table, application `u64ctrl`; the layout of the
images was derived from the real c64u_v1.1.0.ue2 and update_v3.14.ue2 - header, segment table, checksum byte, appended SHA-256, esp_app_desc).

  mkesptest.py <outdir>

Files (ue2 = junk + images, no real vendor code or data in them): v18.ue2 (V1.8), v111.ue2 (V1.11), boundary.ue2 (the bootloader's header straddles the
scan block), ident_split.ue2 (the IDENT_STRING straddles the hash block), noident.ue2 (a set whose application has no IDENT_STRING), app_only.ue2,
no_ptable.ue2, damaged.ue2 (one bit of the application flipped), lookalike.cfw (bytes that look like image headers: no ESP32 part), plain.cfw (no ESP32 part
at all), empty.cfw (0 bytes)."""
import hashlib
import os
import random
import struct
import sys


def segment_blob(load, data):
    return struct.pack("<II", load, len(data)) + data


def esp_image(segments, entry=0x40375460, hash_appended=True, chip=9):
    """An ESP32-S3 image: header (24 bytes), segments, padding so the checksum byte ends a 16-byte block, the XOR checksum, the SHA-256."""
    hdr = bytearray(24)
    hdr[0] = 0xE9
    hdr[1] = len(segments)
    hdr[2] = 2
    hdr[3] = 0x3F
    struct.pack_into("<I", hdr, 4, entry)
    hdr[8] = 0xEE
    struct.pack_into("<H", hdr, 12, chip)
    hdr[17] = 0x63
    hdr[23] = 1 if hash_appended else 0
    body = bytes(hdr)
    cs = 0xEF
    for load, data in segments:
        assert len(data) % 4 == 0
        body += segment_blob(load, data)
        for b in data:
            cs ^= b
    pad = (-(len(body) + 1)) % 16
    body += b"\0" * pad + bytes([cs])
    if hash_appended:
        body += hashlib.sha256(body).digest()
    return body


def app_desc(version=b"v3.14-test", project=b"u64ctrl"):
    d = struct.pack("<I", 0xABCD5432) + b"\0" * 12
    d += version.ljust(32, b"\0") + project.ljust(32, b"\0") + b"12:00:00".ljust(16, b"\0") + b"Jan  1 2026".ljust(16, b"\0") + b"v5.3.1".ljust(32, b"\0")
    return d.ljust(256, b"\0")


def app_image(ident, ident_at=None, rnd=None, size=60000, chip=9):
    """The application: segment 0 starts with esp_app_desc; the IDENT_STRING lies in a later segment (at `ident_at` bytes from the start of the image when given)."""
    seg0 = app_desc() + bytes(rnd.randrange(256) for _ in range(2000))
    seg0 = seg0.ljust(len(seg0) + (-len(seg0)) % 4, b"\0")
    seg1 = bytearray(rnd.randrange(256) for _ in range(size))
    if ident is not None:
        text = b"ESP32 WiFi Bridge " + ident + b"\0"
        # image offset of segment 1's data: 24 header + (8 + len(seg0)) + 8
        base = 24 + 8 + len(seg0) + 8
        at = (ident_at - base) if ident_at is not None else 20000
        assert 0 <= at < len(seg1) - len(text)
        seg1[at:at + len(text)] = text
    return esp_image([(0x3C000020, seg0), (0x3FC91000, bytes(seg1))], chip=chip)


def bootloader(rnd, chip=9):
    return esp_image([(0x3FCD0000, bytes(rnd.randrange(256) for _ in range(3000) if True)[:3000]), (0x403CC710, bytes(rnd.randrange(256) for _ in range(9000))[:9000]),
                      (0x403AE000, bytes(rnd.randrange(256) for _ in range(4000))[:4000])], entry=0x403CC71C, chip=chip)


def ptable():
    def e(typ, sub, off, size, label, flags=0):
        return b"\xAA\x50" + bytes([typ, sub]) + struct.pack("<II", off, size) + label.ljust(16, b"\0") + struct.pack("<I", flags)
    t = e(1, 2, 0x9000, 0x6000, b"nvs") + e(1, 1, 0xF000, 0x1000, b"phy_init") + e(0, 0, 0x10000, 0x300000, b"factory")
    t += b"\xEB\xEB" + b"\xFF" * 14 + hashlib.md5(t).digest()
    return t.ljust(3072, b"\xFF")


def junk(rnd, n):
    return bytes(rnd.randrange(256) for _ in range(n))


def lookalike(rnd, n):
    """Random bytes with many header look-alikes: 0xE9 + chip id bytes, but no consistent segment table; and 0xAA 0x50 pairs."""
    b = bytearray(junk(rnd, n))
    for i in range(0, n - 64, 997):
        b[i] = 0xE9
        b[i + 1] = 2
        b[i + 2] = 2
        b[i + 12] = 9
        b[i + 13] = 0
        b[i + 32 + 4:i + 32 + 8] = struct.pack("<I", 0x7FFFFFFF)         # a segment that runs far past the file
    for i in range(500, n - 64, 1993):
        b[i] = 0xAA
        b[i + 1] = 0x50
    return bytes(b)


def main(out):
    os.makedirs(out, exist_ok=True)
    rnd = random.Random(2610)

    def w(name, data):
        open(os.path.join(out, name), "wb").write(data)

    boot, pt = bootloader(rnd), ptable()

    def ue2(ident, pre=100000, post=30000, **kw):
        app = app_image(ident, rnd=rnd, **kw)
        return junk(rnd, pre) + boot + b"\xFF" * 32 + pt + junk(rnd, 40) + app + junk(rnd, post)

    w("v18.ue2", ue2(b"V1.8"))
    w("v111.ue2", ue2(b"V1.11"))
    # the bootloader's header begins 8 bytes before the end of a 32 KiB scan block
    w("boundary.ue2", ue2(b"V1.14", pre=65536 - 8))
    # the IDENT_STRING straddles the 16 KiB hash block: its first 10 bytes in the first block
    w("ident_split.ue2", ue2(b"V1.11", ident_at=16384 - 10))
    w("ident_split_digits.ue2", ue2(b"V1.11", ident_at=16384 - 21))     # the block ends inside the version: "...Bridge V1." | "11"
    w("noident.ue2", ue2(None))
    app = app_image(b"V1.8", rnd=rnd)
    w("app_only.ue2", junk(rnd, 70000) + app + junk(rnd, 3000))
    w("no_ptable.ue2", junk(rnd, 70000) + boot + junk(rnd, 5000) + app + junk(rnd, 3000))
    bad = bytearray(junk(rnd, 100000) + boot + b"\xFF" * 32 + pt + junk(rnd, 40) + app + junk(rnd, 3000))
    bad[100000 + len(boot) + 32 + len(pt) + 40 + 5000] ^= 1
    w("damaged.ue2", bytes(bad))
    # a complete image set for another chip (ESP32-S2, chip id 2): not ours
    w("other_chip.cfw", junk(rnd, 50000) + bootloader(rnd, chip=2) + b"\xFF" * 32 + pt + junk(rnd, 40) + app_image(b"V1.8", rnd=rnd, chip=2) + junk(rnd, 2000))
    w("lookalike.cfw", lookalike(rnd, 400000))
    w("plain.cfw", junk(rnd, 300000))
    w("empty.cfw", b"")


if __name__ == "__main__":
    main(sys.argv[1])
