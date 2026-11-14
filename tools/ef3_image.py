#!/usr/bin/env python3
"""EasyFlash 3 images (CRT type 90, TRX-PROP-002 sec. 8.4; TRX64-Ultimate/docs/specs/T51-ef3.md).

An EF3 image is the 8 MiB of the cartridge: 8 slots of 64 banks, slot 0 the system area (KERNALs, menu,
EasyProg, directory, freezers). In the CRT the CHIP packets are 8 KiB each, slot-major, bank field =
slot * 64 + bank; slots 1-7 are plain EasyFlash (type 32) images.

    ef3_image.py info IMAGE.crt
    ef3_image.py extract IMAGE.crt SLOT OUT.crt            slot 1-7 as an EasyFlash CRT (type 32)
    ef3_image.py insert IMAGE.crt SLOT EF.crt -o OUT.crt [--name NAME]
                                                           an EasyFlash CRT into a slot; the EAPI is replaced by
                                                           the MX29LV640EB driver as EasyProg does
    ef3_image.py new -o OUT.crt --system SYSTEM.crt        an image with slot 0 from SYSTEM.crt (type 90, or the
                                                           64 banks of a type 32 CRT) and 1-7 erased
    ef3_image.py cmp A.crt B.crt                           compare two type 90 images byte for byte (slot, bank, half);
                                                           exit 0 when equal
    ef3_image.py system IMAGE.crt SYSTEM.crt -o OUT.crt    recovery after a chip erase: slot 0 of IMAGE replaced by
                                                           the base/system image (slot 0 of a type 90, or the 64
                                                           banks of a type 32 CRT), slots 1-7 kept as they are

The EAPI driver comes from the app's apps/trex_crt/src/eapi_drivers.c (--eapi-src, or next to this repo's
app worktree); the driver is skoe's eapi-mx29640b.s (zlib licence, notice in that file).
"""
import argparse
import hashlib
import os
import re
import struct
import sys

BANKS_PER_SLOT = 64
SLOTS = 8
CHIP = 0x2000
EAPI_OFFSET = 0x1800
EAPI_SIZE = 768
EAPI_SIG = b"eapi"
EAPI_MX29640B_SHA256 = "0d8921d4ba67ee3148563c8f175b5fc47c4de41432f007cf2e6447a7264a7d60"   # the 768 bytes of acme(skoe's eapi-mx29640b.s) after its load address; equal to eapi_drivers.c
DIR_BANK = 0x10          # slot 0, ROML: "EF-Directory V1:" + 16 slot names + 8 KERNAL names + checksum word


def petscii(text):
    """acme's !pet: upper case letters become $C1-$DA, lower case $41-$5A (the directory is written that way)"""
    return bytes((ord(c) + 0x80) if "A" <= c <= "Z" else (ord(c) - 0x20) if "a" <= c <= "z" else ord(c) for c in text)


def from_petscii(b):
    return "".join(chr(x - 0x80) if 0xC1 <= x <= 0xDA else chr(x + 0x20) if 0x41 <= x <= 0x5A else chr(x) for x in b)


DIR_SIG = petscii("EF-Directory V1:")


def crt_header(hw, name, exrom, game):
    h = bytearray(b"C64 CARTRIDGE   ")
    h += struct.pack(">IHHBBB", 0x40, 0x0100, hw, exrom, game, 0)
    h += bytes(5)
    h += name.encode("ascii", "replace")[:32].ljust(32, b"\0")
    return bytes(h)


def read_crt(path):
    d = open(path, "rb").read()
    if d[:16] != b"C64 CARTRIDGE   ":
        raise SystemExit("%s: not a C64 CRT" % path)
    hw = struct.unpack(">H", d[0x16:0x18])[0]
    pos = struct.unpack(">I", d[0x10:0x14])[0] or 0x40
    chips = {}
    while pos + 16 <= len(d) and d[pos:pos + 4] == b"CHIP":
        ln, typ, bank, load, size = struct.unpack(">IHHHH", d[pos + 4:pos + 16])
        chips[(bank, load)] = d[pos + 16:pos + 16 + size]
        pos += ln
    return hw, d[:0x40], chips


def image_from_chips(chips, banks):
    """a flat image, 16 KiB per bank: ROML then ROMH, erased = $FF"""
    img = bytearray(b"\xFF" * (banks * 0x4000))
    for (bank, load), data in chips.items():
        if load in (0x8000, 0xA000, 0xE000) and bank < banks and len(data) == CHIP:
            o = bank * 0x4000 + (0 if load == 0x8000 else CHIP)
            img[o:o + CHIP] = data
    return img


def write_crt(path, hw, name, exrom, game, img, first_bank=0):
    out = bytearray(crt_header(hw, name, exrom, game))
    for b in range(len(img) // 0x4000):
        for half, load in ((0, 0x8000), (1, 0xA000)):
            data = img[b * 0x4000 + half * CHIP:b * 0x4000 + (half + 1) * CHIP]
            out += b"CHIP" + struct.pack(">IHHHH", 16 + CHIP, 2, first_bank + b, load, CHIP) + bytes(data)
    open(path, "wb").write(out)


def load_ef3(path):
    hw, hdr, chips = read_crt(path)
    if hw != 90:
        raise SystemExit("%s: CRT type %d, not an EasyFlash 3 image (90)" % (path, hw))
    return image_from_chips(chips, SLOTS * BANKS_PER_SLOT)


def eapi_driver(src):
    cands = [src] if src else []
    here = os.path.dirname(os.path.abspath(__file__))
    for rel in ("../../TRX64-Ultimate-app-wt-t51/apps/trex_crt/src/eapi_drivers.c",
                "../vendor/gideon/apps/trex_crt/src/eapi_drivers.c"):
        cands.append(os.path.join(here, rel))
    for c in cands:
        if c and os.path.exists(c):
            t = open(c).read()
            m = re.search(r"eapi_mx29640b\[EAPI_DRIVER_SIZE\]\s*=\s*\{(.*?)\};", t, re.S)
            if m:
                body = bytes(int(x, 16) for x in re.findall(r"0x([0-9A-Fa-f]{1,2})", m.group(1)))
                if len(body) == EAPI_SIZE and hashlib.sha256(body).hexdigest() == EAPI_MX29640B_SHA256:
                    return body
    raise SystemExit("EAPI driver MX29LV640EB not found: give --eapi-src apps/trex_crt/src/eapi_drivers.c")


def slot_names(img):
    d = img[DIR_BANK * 0x4000:DIR_BANK * 0x4000 + 0x2000]
    if d[:16] != DIR_SIG:
        return None
    return [from_petscii(bytes(d[16 + 16 * i:32 + 16 * i]).split(b"\0")[0]) for i in range(16)]


def set_slot_name(img, slot, name):
    o = DIR_BANK * 0x4000
    if img[o:o + 16] != DIR_SIG:
        return False
    img[o + 16 + 16 * slot:o + 32 + 16 * slot] = petscii(name)[:15].ljust(16, b"\0")
    img[o + 16 + 16 * 16 + 16 * 8:o + 16 + 16 * 16 + 16 * 8 + 2] = b"\x11\x47"   # $4711: no checksum (EasyProg's "none")
    return True


def cmd_info(a):
    img = load_ef3(a.image)
    names = slot_names(img)
    print("EasyFlash 3 image, 8 slots of 64 banks; directory: %s" % ("yes" if names else "none (no EF-Directory V1: at bank $10)"))
    for s in range(SLOTS):
        base = s * BANKS_PER_SLOT * 0x4000
        used = sum(1 for b in range(BANKS_PER_SLOT) if any(x != 0xFF for x in img[base + b * 0x4000:base + (b + 1) * 0x4000]))
        h = img[base + CHIP + EAPI_OFFSET:base + CHIP + EAPI_OFFSET + 20]
        eapi = bytes(h[4:19]).split(b"\0")[0].decode("latin-1") if h[:4] == EAPI_SIG else ""
        print("slot %d  %-16s  %2d of 64 banks not erased  EAPI: %s" % (s, names[s] if names else "", used, eapi or "-"))
    return 0


def cmd_extract(a):
    img = load_ef3(a.image)
    s = a.slot
    if not 0 <= s < SLOTS:
        raise SystemExit("slot 0-7")
    names = slot_names(img)
    base = s * BANKS_PER_SLOT * 0x4000
    write_crt(a.out, 32, (names[s] if names and names[s] else "EF3 SLOT %d" % s), 1, 0,
              img[base:base + BANKS_PER_SLOT * 0x4000])
    return 0


def cmd_insert(a):
    img = load_ef3(a.image)
    s = a.slot
    if not 1 <= s < SLOTS:
        raise SystemExit("slot 1-7 (slot 0 is the system area)")
    hw, hdr, chips = read_crt(a.ef)
    if hw != 32:
        raise SystemExit("%s: CRT type %d, not an EasyFlash (32)" % (a.ef, hw))
    slot = image_from_chips(chips, BANKS_PER_SLOT)
    # EasyProg: the EAPI of the CRT becomes the driver of the detected flash
    if bytes(slot[CHIP + EAPI_OFFSET:CHIP + EAPI_OFFSET + 4]) == EAPI_SIG:
        slot[CHIP + EAPI_OFFSET:CHIP + EAPI_OFFSET + EAPI_SIZE] = eapi_driver(a.eapi_src)
    base = s * BANKS_PER_SLOT * 0x4000
    img[base:base + BANKS_PER_SLOT * 0x4000] = slot
    if a.name:
        if not set_slot_name(img, s, a.name):
            print("no EF-Directory in the image: the name was not written", file=sys.stderr)
    write_crt(a.o, 90, "EASYFLASH 3", 1, 0, img)
    return 0


def system_area(path):
    """slot 0 (64 banks) of a type 90 image or of a type 32 CRT (the base image as an EasyFlash CRT)"""
    hw, hdr, chips = read_crt(path)
    if hw == 90:
        sysimg = image_from_chips(chips, SLOTS * BANKS_PER_SLOT)
    elif hw == 32:
        sysimg = image_from_chips(chips, BANKS_PER_SLOT)
    else:
        raise SystemExit("%s: CRT type %d, want the system image as type 90 or 32" % (path, hw))
    return bytearray(sysimg[:BANKS_PER_SLOT * 0x4000])


def cmd_new(a):
    img = system_area(a.system) + bytearray(b"\xFF" * ((SLOTS - 1) * BANKS_PER_SLOT * 0x4000))
    write_crt(a.o, 90, "EASYFLASH 3", 1, 0, img)
    return 0


def cmd_system(a):
    img = load_ef3(a.image)
    img[:BANKS_PER_SLOT * 0x4000] = system_area(a.system)
    write_crt(a.o, 90, "EASYFLASH 3", 1, 0, img)
    return 0


def cmd_cmp(a):
    x, y = load_ef3(a.a), load_ef3(a.b)
    diff = []
    for b in range(SLOTS * BANKS_PER_SLOT):
        for half in (0, 1):
            o = b * 0x4000 + half * CHIP
            if x[o:o + CHIP] != y[o:o + CHIP]:
                diff.append((b // BANKS_PER_SLOT, b % BANKS_PER_SLOT, "ROMH" if half else "ROML"))
    if not diff:
        print("equal: 8 MiB, %d CHIP packets" % (SLOTS * BANKS_PER_SLOT * 2))
        return 0
    per_slot = {}
    for s, b, h in diff:
        per_slot[s] = per_slot.get(s, 0) + 1
    print("%d of %d 8 KiB halves differ; per slot: %s" % (len(diff), SLOTS * BANKS_PER_SLOT * 2,
          ", ".join("slot %d: %d" % kv for kv in sorted(per_slot.items()))))
    for s, b, h in diff[:20]:
        print("  slot %d bank $%02X %s" % (s, b, h))
    return 1


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sp = ap.add_subparsers(dest="cmd", required=True)
    p = sp.add_parser("info"); p.add_argument("image"); p.set_defaults(fn=cmd_info)
    p = sp.add_parser("extract"); p.add_argument("image"); p.add_argument("slot", type=int); p.add_argument("out"); p.set_defaults(fn=cmd_extract)
    p = sp.add_parser("insert"); p.add_argument("image"); p.add_argument("slot", type=int); p.add_argument("ef")
    p.add_argument("-o", required=True); p.add_argument("--name"); p.add_argument("--eapi-src"); p.set_defaults(fn=cmd_insert)
    p = sp.add_parser("new"); p.add_argument("-o", required=True); p.add_argument("--system", required=True); p.set_defaults(fn=cmd_new)
    p = sp.add_parser("system"); p.add_argument("image"); p.add_argument("system"); p.add_argument("-o", required=True); p.set_defaults(fn=cmd_system)
    p = sp.add_parser("cmp"); p.add_argument("a"); p.add_argument("b"); p.set_defaults(fn=cmd_cmp)
    a = ap.parse_args(argv)
    return a.fn(a)


if __name__ == "__main__":
    sys.exit(main())
