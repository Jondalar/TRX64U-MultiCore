#!/usr/bin/env python3
"""Builds the directory tree the render bench runs over (T101 D8): real .cor bundles from tools/cor_pack.py.
   mktree.py <root>      (root/Flash, root/Usb0, root/SD)"""
import os, struct, sys
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "..", "tools"))
import cor_pack as C

root = sys.argv[1]
hdr = C.BIT_MAGIC
for key in b"abcd":
    hdr += bytes([key]) + struct.pack(">H", 6) + b"xxxxx\0"
bit = hdr + b"e" + struct.pack(">I", 4096 + 8 + 4) + b"\xFF" * 8 + C.SYNC + bytes(range(256)) * 16
app = bytes((i * 5) & 0xFF for i in range(3000))
# a 32x32 RGB565 little endian icon whose first pixel is 0xF800 (red)
icon = b"".join(struct.pack("<H", 0xF800 if (x, y) == (0, 0) else (x << 11) | ((y * 2) << 5) | (31 - x)) for y in range(32) for x in range(32))
HOST = "820eded2"

def put(path, data):
    p = os.path.join(root, path)
    os.makedirs(os.path.dirname(p), exist_ok=True)
    open(p, "wb").write(data)

def core(name, ver="1.0", icon=None, contract=C.CONTRACT, host=HOST, short=None):
    return C.pack(name, ver, bit, app, 0x30000, 0x30010, host, contract=contract, icon=icon, short=short)

put("Flash/cores/tiny.cor", core("Tiny", "0.2"))
put("SD/cores/trx64.cor", core("TRX64 Ultimate (C64)", "1.0-rc1"))
put("SD/readme.txt", b"not a bundle\n")
put("Usb0/cores/vc20.cor", core("VC-20 (PAL)", "0.1", icon=icon))
# T209 D15: logo-less cores for the text tile: a short name from the manifest, the widest one the format allows, none (the first letters of the name)
put("Usb0/tiles/pet.cor", core("Commodore PET", "0.1", short="PET"))
put("Usb0/tiles/wide.cor", core("Wide tile", "0.1", short="MMMMM"))
put("Usb0/tiles/plain.cor", core("VIC-20 (PAL)", "0.1"))
put("Usb0/cores/c2.cor", core("Future", "9.0", contract=3))
bad = bytearray(core("Bad icon", "0.3", icon=icon))
bad[-100] ^= 0xFF                      # a byte inside the icon record: its SHA-256 no longer matches
put("Usb0/cores/badicon.cor", bytes(bad))
# T111: the File Browser lists every file. A sparse file of 9,008,283,648 bytes (the A1200's image): its size column has to say 8.3 GiB, not the 4.0 GiB
# of a size saturated at 32 bit; and a note beside it that is no bundle.
put("Usb0/images/notes.txt", b"hello\n")
with open(os.path.join(root, "Usb0/images/MegaAGS.hdf"), "wb") as f:
    f.truncate(9008283648)
put("Usb0/old/trx64-0.9.cor", core("TRX64 Ultimate (old host)", "0.9", contract=3))
put("Usb0/old/h2.cor", core("Other host", "1", host="9ae17385"))
put("Usb0/old/garbage.cor", b"this is not a bundle at all, just text\n" * 4)
for i in range(1, 10):
    put("Usb0/many/m%d.cor" % i, core("Core number %d" % i, "1.%d" % i, icon=icon if i % 3 == 0 else None))
# T101: a folder longer than the 13 rows of the Add core list, with a name that does not fit its column (the .cfw of the
# board photo of 2026-10-22); the files are not bundles, so no scan count changes
for i in range(1, 19):
    put("Usb0/scroll/s%02d.cor" % i, core("Scroll core %d" % i, "2.%d" % i, icon=icon if i % 2 else None))
put("Usb0/scroll/trx64_update_692f643d_20261010.cfw", b"not a firmware\n")
# T101 areas: a folder of 20 bundles whose sizes all differ and whose manifests differ: a scrolling key changes the 13 names, the 13
# sizes and the manifest panel, more than the 32 invalid areas LVGL keeps by default
for i in range(1, 21):
    put("Usb0/sized/z%02d.cor" % i, C.pack("Sized %d" % i, "3.%d" % i, bit, app[:1000 + i * 97], 0x30000, 0x30010, HOST, contract=C.CONTRACT))
# T180 D7 S6: a folder of 3 pages in the File Browser (".." and 26 files = 27 rows, 13 + 13 + 1); the files are no bundles
for i in range(1, 27):
    put("Usb0/pages/p%02d.txt" % i, b"page test\n")
# T114: one bundle per core id carrying that core's real logo (cores/<id>/logo/icon.rgb565), and a twin of one
# whose icon record is damaged (its SHA-256 no longer matches the manifest)
cores = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "..", "cores")
# TRX64 has no core directory since T115 D12: its logo (the trx64u/trx64g .cor carry it) is an extra in tools/core_logos/trx64
logos = {cid: os.path.join(cores, cid, "logo", "icon.rgb565") for cid in os.listdir(cores)}
logos["trx64"] = os.path.join(cores, "..", "tools", "core_logos", "trx64", "icon.rgb565")
for cid in sorted(logos):
    f = logos[cid]
    if not os.path.exists(f):
        continue
    logo = open(f, "rb").read()
    put("Usb0/logos/%s.cor" % cid, core(cid, "1.0", icon=logo))
    if cid == "amiga":
        b = bytearray(core(cid, "1.0", icon=logo))
        i = bytes(b).find(logo)
        b[i + 1000] ^= 0xFF
        put("Usb0/logos/bad_%s.cor" % cid, bytes(b))

# T115: a second tree for the full-firmware scenario (<root>_fw): .cfw images of the two measured layouts, in miniature
# (the packets are the real ones, the frame data and the application are short), one for a 100T only, and a core for contrast.
PRE = bytes([0xDE, 0xAD, 0x15, 0x41]) + b"U64MK2"

def bit_file(a, b, c, d, idcode, axss):
    def f(k, s):
        s = s.encode() + b"\0"
        return bytes([k]) + struct.pack(">H", len(s)) + s
    w = [0xFFFFFFFF, 0xFFFFFFFF, 0xBB, 0x11220044, 0xFFFFFFFF, 0xFFFFFFFF, 0xAA995566, 0x20000000,
         0x30008001, 0, 0x30018001, idcode]
    if axss:
        w += [0x3001A001, 0x553525D3]
    n = 64
    w += [0x30004000, 0x50000000 | n] + [0x12345678] * n + [0x30008001, 0xD] + [0x20000000] * 6
    data = b"".join(struct.pack(">I", x) for x in w)
    return C.BIT_MAGIC + f(b"a"[0], a) + f(b"b"[0], b) + f(b"c"[0], c) + f(b"d"[0], d) + b"e" + struct.pack(">I", len(data)) + data

def cfw(bits, load=0x30000):
    body = PRE + b"A" * (0x2000 - len(PRE))
    for b in bits:
        body += b
        body += b"\0" * (-len(body) % 8)
    app = bytes((i * 5) & 0xFF for i in range(0x10100))
    return body + struct.pack("<III", load, len(app), load) + app

fw_root = root + "_fw"
def putf(path, data):
    p = os.path.join(fw_root, path)
    os.makedirs(os.path.dirname(p), exist_ok=True)
    open(p, "wb").write(data)

G50 = bit_file("u64_mk2_artix;UserID=0XFFFFFFFF;Version=2024.1", "7a50tfgg484", "2026/09/10", "11:00:57", 0x0362C093, False)
G100 = bit_file("u64_mk2_artix;UserID=0XFFFFFFFF;Version=2024.1", "7a100tfgg484", "2026/09/10", "11:05:11", 0x03631093, False)
T50 = bit_file("top;COMPRESS=TRUE;UserID=0XFFFFFFFF;Version=2024.1", "7a50tfgg484", "2026/10/10", "18:23:21", 0x0362C093, True)
putf("Usb0/cores/gideon.cfw", cfw([G50, G100]))
putf("Usb0/cores/trx64_large.cfw", cfw([T50]))
putf("Usb0/cores/only100.cfw", cfw([G100]))
putf("Usb0/cores/vc20.cor", core("VC-20 (PAL)", "0.1"))

# T115 D7: a third tree (<root>_ic) for the logos of .cfw rows: the built-in ones by design name, a sidecar of 2048 bytes
# beside the .cfw (`gideon.icon`, replaces the built-in one) and one of 2049 bytes (`wrong.icon`, ignored).
ic_root = root + "_ic"
def puti(path, data):
    p = os.path.join(ic_root, path)
    os.makedirs(os.path.dirname(p), exist_ok=True)
    open(p, "wb").write(data)

M50 = bit_file("mystery_design;UserID=0XFFFFFFFF;Version=2024.1", "7a50tfgg484", "2026/10/01", "10:00:00", 0x0362C093, False)
puti("Usb0/cores/gideon.cfw", cfw([G50, G100]))
puti("Usb0/cores/gideon.icon", bytes(((i * 37 + 11) ^ (i >> 3)) & 0xFF for i in range(2048)))
puti("Usb0/cores/gideon_plain.cfw", cfw([G50, G100]))
puti("Usb0/cores/mystery.cfw", cfw([M50]))
puti("Usb0/cores/trx64.cfw", cfw([T50]))
puti("Usb0/cores/wrong.cfw", cfw([T50]))
puti("Usb0/cores/wrong.icon", bytes((i * 29 + 7) & 0xFF for i in range(2049)))

# T115 §7: a fourth tree (<root>_lc) for the LARGE .cor: a package with its own icon, one with a damaged bitstream record (the hash fails on
# adding, the probe does not hash), one for the 100T, and a layer-2 core beside them.
lc_root = root + "_lc"
def putl(path, data):
    p = os.path.join(lc_root, path)
    os.makedirs(os.path.dirname(p), exist_ok=True)
    open(p, "wb").write(data)

def fbit(idcode, axss=True, frames=64):
    return bit_file("top;UserID=0XFFFFFFFF;Version=2024.1", "7a50tfgg484", "2026/10/25", "12:00:00", idcode, axss)

lapp = bytes((i * 11 + 3) & 0xFF for i in range(0x10100))
good = C.pack_large("amiga1200", "Amiga 1200 (AGA)", "0.3.0", fbit(0x0362C093), lapp, icon=icon)
putl("Usb0/cores/amiga1200.cor", good)
dam = bytearray(good)
dam[-50] ^= 0xFF                                           # inside the bitstream record (the last record)
putl("Usb0/cores/damaged.cor", bytes(dam))
C.IDCODE_XC7A50T = 0x03631093                              # the packer refuses a 100T; this file is made around it
putl("Usb0/cores/big100t.cor", C.pack_large("big100t", "Big for 100T", "1", fbit(0x03631093), lapp))
C.IDCODE_XC7A50T = 0x0362C093
putl("Usb0/cores/vc20.cor", core("VC-20 (PAL)", "0.1"))

# T101 S10: the boot.cfg the start-up bench loads: 13 entries of an older boot.cfg (no stored check)
put("Flash/boot.cfg", ("countdown=5\ndefault=0\n" + "".join("core=%s|%s||0\n" % (p, n) for p, n in [
    ("/Usb0/cores/vc20.cor", "VC-20 (PAL)"), ("/SD/cores/trx64.cor", "TRX64 Ultimate (C64)"), ("/Flash/cores/tiny.cor", "Tiny"),
    ("/Usb0/cores/c2.cor", "Future"), ("/Usb0/cores/gone.cor", "Gone")] + [("/Usb0/many/m%d.cor" % i, "Core number %d" % i) for i in range(1, 9)])).encode())

# T137: a fifth tree (<root>_cv) for the conversion of a .cfw/.ue2 to a .cor (T in the File Browser): a Gideon-style .cfw, a .ue2 as the vendors ship them
# (the updater's record first, loading at 0x03000000; the bench builds with the hash of this file in its table), a file that only has the name, a firmware on
# the internal drive, and a layer-2 core. There is no cores folder on the stick: the conversion makes it.
cv_root = root + "_cv"
def putc(path, data):
    p = os.path.join(cv_root, path)
    os.makedirs(os.path.dirname(p), exist_ok=True)
    open(p, "wb").write(data)

def ue2(bits, load=0x03000000):
    img = b"A" * 0x2000
    for b in bits:
        img += b
        img += b"\0" * (-len(img) % 8)
    app = bytes((i * 5) & 0xFF for i in range(0x10100))
    img += struct.pack("<III", 0x30000, len(app), 0x30000) + app
    return struct.pack("<III", load, len(img), load) + img

putc("Usb0/fw/gideon.cfw", cfw([G50, G100]))
putc("Usb0/fw/vendor.ue2", ue2([G50]))
putc("Usb0/fw/junk.cfw", b"not a firmware\n")
putc("Usb0/fw/notes.txt", b"not a bundle\n")
putc("Usb0/fw/ours.cor", core("VC-20 (PAL)", "0.1"))
putc("Flash/fw/gideon.cfw", cfw([G50, G100]))
putc("SD/readme.txt", b"x\n")

# T137: a sixth tree (<root>_fwr) for the restore of the original firmware (R in the File Browser): every kind of file the host tells apart, from the same
# writers as tests/hostapp/test_fwrestore (mkrestoretest.py): a backup of this board's chip, a backup of another chip, a converted (foreign) .cor, a .cor of ours,
# a vendor-style .cfw that is not on the table, a MultiCore update package, a tester .cfw
import subprocess
fwr_dir = os.path.join(root + "_fwr", "Usb0", "fw")
os.makedirs(fwr_dir, exist_ok=True)
subprocess.check_call([sys.executable, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "mkrestoretest.py"), fwr_dir])
for junk in ("facts.txt", "icon.bin"):
    os.remove(os.path.join(fwr_dir, junk))

# T152: a seventh tree (<root>_txu) for the MultiCore update (U on a .txu in the File Browser), the flash protection and the ESP32 inspection (F8): .txu files from the
# real writer (tests/hostapp/mktxutest.py), vendor-style files with and without an ESP32 image set (mkesptest.py), three partial cores for the running host and two
# large cores on the SD card (the S2 question counts them).
import test_cor_pack as TC
here = os.path.dirname(os.path.abspath(__file__))
txu_dir = os.path.join(root + "_txu", "Usb0", "upd")
os.makedirs(txu_dir, exist_ok=True)
subprocess.check_call([sys.executable, os.path.join(here, "..", "mktxutest.py"), txu_dir])
for junk in os.listdir(txu_dir):                      # the browser shows 13 rows: only the files the bench uses stay
    if junk not in ("both.txu", "newkey.txu", "iface_bad.txu", "iface_fixed.txu", "crc.txu", "payload_app.txu", "foreign_origin.txu", "not_txu.cfw", "not_txu.ue2"):
        os.remove(os.path.join(txu_dir, junk))
esp_dir = os.path.join(root + "_txu", "Usb0", "esp")
os.makedirs(esp_dir, exist_ok=True)
subprocess.check_call([sys.executable, os.path.join(here, "..", "mkesptest.py"), esp_dir])
for name in sorted(os.listdir(esp_dir)):
    if name not in ("v111.ue2", "v18.ue2", "app_only.ue2", "damaged.ue2", "plain.cfw"):
        os.remove(os.path.join(esp_dir, name))

def put_t(path, data):
    p = os.path.join(root + "_txu", path)
    os.makedirs(os.path.dirname(p), exist_ok=True)
    open(p, "wb").write(data)

for i in (1, 2, 3):
    put_t("SD/cores/partial%d.cor" % i, core("Partial core %d" % i, "1.%d" % i))
for i in (1, 2):
    put_t("SD/cores/large%d.cor" % i, C.pack_large(cid="large%d" % i, name="Large core %d" % i, version="2.%d" % i, bit=TC.full_bit(frames=2000),
                                                   app=bytes((j * 3) & 0xFF for j in range(3000)), icon=icon))
put_t("Usb0/upd/notes.txt", b"not an update\n")
put_t("Usb0/upd/gideon.cfw", cfw([G50, G100]))
