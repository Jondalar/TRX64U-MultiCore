#!/usr/bin/env python3
"""The .cfw of a monolithic core (T113 Q12, T115): what goes into the updater and what the host's parser takes out.

A core of delivery = "cfw" (tools/core_toml.py) is one full bitstream - the host and the core flat in a single design - and
the application that runs on it. tools/make_update.sh makes the .cfw from the two (the updater of the TRX64 Ultimate, the same
file layout as the TRX64 firmware's); this tool does the two things around it:

  prepare   the .bit gets the core's id as its design name (field a of the header) and the application becomes a record
            {load 0x30000, length, start 0x30000} + data out of the raw image (coreapp.bin)
  check     the .cfw is read the way host-app/software/hostapp/cfw.cc cfw_probe reads it (T115 D1): pre-header, the first
            .bit whose header holds, its IDCODE against the 50T, the AXSS write (the map of D3), the application record
            behind the last bitstream, the limits of the staging area; and the design name picks the generic logo (D7)

  tools/cfw_core.py prepare --core c128 --bit c128_mono.bit --app-bin coreapp.bin --out-bit x.bit --out-app x.app
  tools/cfw_core.py check [--design c128] c128.cfw

Why the design name: Vivado writes the top module's name into field a, and ours is `top` - the name of the TRX64 firmware, whose
.cfw row gets the TRX64 logo (cfwicon.cc). A core of its own would show that logo. Any other name gets the generic firmware
mark, or the core's own picture from a `<name>.icon` beside the .cfw (tools/pack_cfw_mc.sh writes it).
"""
import argparse
import os
import re
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import trx_pkg as P  # noqa: E402

# the limits of host-app/software/hostapp/cfw.h; a test compares them with that file
CFW_APP_ADDR = 0x00030000
CFW_APP_MAX = 0x00400000
CFW_BIT_MAX = 0x00800000
CFW_SCAN_MAX = 0x00400000
CFW_IDCODE_XC7A50T = 0x0362C093
CFW_IDCODE_MASK = 0x0FFFFFFF
PRE_HEADER = b"\xDE\xAD\x15\x41U64MK2"
BIT_MAGIC = bytes([0x00, 0x09, 0x0F, 0xF0, 0x0F, 0xF0, 0x0F, 0xF0, 0x0F, 0xF0, 0x00, 0x00, 0x01])
REG_IDCODE = 0x0C
FIELD_MAX = 48                  # BitHdr.f[i] in cfw.cc: 47 characters and the NUL
NAME_RE = re.compile(r"^[a-z][a-z0-9_]{0,19}$")


class CfwError(Exception):
    pass


# ---- the .bit header ---------------------------------------------------------------------------------------------

def bit_fields(bit, pos=0):
    """([text a..d], header length, data length) of the .bit header at pos, or None when the bytes there are not one
    (cfw.cc bit_header: the magic, the fields a-d each terminated by a NUL, then e and its length)."""
    b = bit[pos:pos + 512]
    if len(b) < 64 or b[:13] != BIT_MAGIC:
        return None
    p = 13
    out = []
    for i in range(4):
        if p + 3 > len(b) or b[p] != ord("a") + i:
            return None
        ln = struct.unpack_from(">H", b, p + 1)[0]
        p += 3
        if ln == 0 or p + ln > len(b) or b[p + ln - 1] != 0:
            return None
        out.append(bytes(b[p:p + ln - 1]).decode("ascii", "replace"))
        p += ln
    if p + 5 > len(b) or b[p] != ord("e"):
        return None
    dlen = struct.unpack_from(">I", b, p + 1)[0]
    if dlen < 64:
        return None
    return out, p + 5, dlen


def design_kind(name):
    """The logo cfwicon.cc cfw_icon_kind picks for a design name: 'gideon', 'trx64' or 'generic'."""
    word = name.split(" ")[0]
    if word.startswith("u64_mk2_artix"):
        return "gideon"
    if word == "top":
        return "trx64"
    return "generic"


def rename_design(bit, name):
    """The .bit with field a rewritten to `name` (what stands after a ';' - UserID, Version - stays). The configuration
    data is untouched, byte for byte."""
    if not NAME_RE.match(name):
        raise CfwError("design name %r: lower case letters, digits and _, at most 20, starting with a letter" % name)
    if design_kind(name) != "generic":
        raise CfwError("design name %r would pick the logo of another firmware (cfwicon.cc)" % name)
    f = bit_fields(bit)
    if f is None:
        raise CfwError("not a .bit file")
    old = f[0][0]
    new = name + (old[old.index(";"):] if ";" in old else "")
    if len(new) >= FIELD_MAX:                   # Vivado's "<top>;COMPRESS=TRUE;UserID=0XFFFFFFFF;Version=2024.1" is 50 characters with `top`; the
        new = new[:FIELD_MAX - 1]               # host reads 47 of them (strncpy) and the name is what stands before the first ';': cut the tail
    enc = new.encode("ascii") + b"\0"
    start = 13 + 3                              # magic, the 'a', its 16 bit length
    old_len = struct.unpack_from(">H", bit, 14)[0]
    out = bit[:14] + struct.pack(">H", len(enc)) + enc + bit[start + old_len:]
    if bit_fields(out) is None or bit_fields(out)[2] != f[2] or out[bit_fields(out)[1]:] != bit[f[1]:]:
        raise CfwError("internal: the renamed header does not leave the configuration data alone")
    return out


def app_record(raw, load=CFW_APP_ADDR):
    """{load, length, start} and the data, padded to 4 bytes: an .app of format 1 (what hex2bin -r writes) from the raw image
    the core app is linked to (coreapp/linker.x: ORIGIN 0x30000). Load and start are the same, as in a .cor (cor_pack --load
    0x30000 --entry 0x30000)."""
    data = raw + b"\0" * (-len(raw) % 4)
    return struct.pack("<III", load, len(data), load) + data


# ---- reading it the way the host does ----------------------------------------------------------------------------

def scan_config(data):
    """(IDCODE or 0, AXSS written) from the configuration packets of a .bit's data (cfw.cc scan_config, which stops
    once both are known - a compressed stream is not walked to its end)."""
    i = 0
    while True:
        if i >= 1024 or i + 4 > len(data):
            raise CfwError("no sync word in the first 1 KiB of the bitstream")
        if struct.unpack_from(">I", data, i)[0] == P.SYNC_WORD:
            break
        i += 4
    i += 4
    idcode, axss = 0, False
    while i + 4 <= len(data):
        if axss and idcode:
            break
        v = struct.unpack_from(">I", data, i)[0]
        i += 4
        t = v >> 29
        if t == 1:
            op, reg, cnt = (v >> 27) & 3, (v >> 13) & 0x3FFF, v & 0x7FF
            if op == 2:
                val = 0
                if cnt == 1:
                    if i + 4 > len(data):
                        break
                    val = struct.unpack_from(">I", data, i)[0]
                if reg == P.REG_AXSS:
                    axss = True
                elif reg == REG_IDCODE and cnt == 1:
                    idcode = val
                elif reg == P.REG_CMD and cnt == 1 and val == P.CMD_DESYNC:
                    break
            i += 4 * cnt
        elif t == 2:
            i += 4 * (v & 0x7FFFFFF)
        else:
            break
    return idcode, axss


def probe(cfw, running_idcode=CFW_IDCODE_XC7A50T):
    """cfw_probe in Python. Returns a dict (bit_off, bit_len, app_off, app_len, idcode, map, design, name); raises CfwError
    with the reason cfw.cc would give."""
    if len(cfw) < 0x1000 or cfw[:len(PRE_HEADER)] != PRE_HEADER:
        raise CfwError("not a .cfw: no pre-header DE AD 15 41 U64MK2")
    limit = min(len(cfw), CFW_SCAN_MAX)
    pos = -1
    at = cfw.find(BIT_MAGIC, len(PRE_HEADER), limit)
    while at >= 0:
        if bit_fields(cfw, at):
            pos = at
            break
        at = cfw.find(BIT_MAGIC, at + 1, limit)
    if pos < 0:
        raise CfwError("no .bit header in the first %d bytes" % CFW_SCAN_MAX)
    have = None
    last_end = 0
    app_at = None
    for _ in range(8):
        f = bit_fields(cfw, pos)
        if f is None:
            raise CfwError("a .bit header is broken at +0x%X" % pos)
        fields, hlen, dlen = f
        total = hlen + dlen
        if pos + total > len(cfw):
            raise CfwError("the .cfw is truncated inside the bitstream at +0x%X" % pos)
        idcode, axss = scan_config(cfw[pos + hlen:pos + total])
        if have is None and idcode and not ((idcode ^ running_idcode) & CFW_IDCODE_MASK):
            have = dict(bit_off=pos, bit_len=total, idcode=idcode, axss=axss, fields=fields)
        end = pos + total
        last_end = end
        exact = next_bit = wrong = None
        for a in range((end + 3) & ~3, end + 17, 4):
            if a + 12 > len(cfw):
                continue
            load, ln, start = struct.unpack_from("<III", cfw, a)
            plausible = 0x10000 <= ln <= 0x08000000 and not load & 3 and load <= start < load + ln
            if plausible and load == CFW_APP_ADDR and start == CFW_APP_ADDR:
                exact = a
                break
            if next_bit is None and bit_fields(cfw, a):
                next_bit = a
            elif wrong is None and plausible and load != 0:
                wrong = a
        if exact is not None or (next_bit is None and wrong is not None):
            app_at = exact if exact is not None else wrong
            break
        if next_bit is None:
            break
        pos = next_bit
    if have is None:
        raise CfwError("no bitstream for the XC7A50T (IDCODE %08X)" % running_idcode)
    if app_at is None:
        raise CfwError("no application record behind the last bitstream (+0x%X)" % last_end)
    load, ln, start = struct.unpack_from("<III", cfw, app_at)
    if load != CFW_APP_ADDR or start != CFW_APP_ADDR:
        raise CfwError("the application does not load and start at 0x30000 (load %08X, start %08X)" % (load, start))
    if ln > CFW_APP_MAX or have["bit_len"] > CFW_BIT_MAX:
        raise CfwError("out of the staging area: application %d, bitstream %d bytes" % (ln, have["bit_len"]))
    if app_at + 12 + ln > len(cfw):
        raise CfwError("the .cfw is truncated inside the application")
    design = have["fields"][0].split(";")[0]
    return dict(bit_off=have["bit_off"], bit_len=have["bit_len"], app_off=app_at, app_len=ln, idcode=have["idcode"],
                map="identity" if have["axss"] else "gideon_bl4", design=design,
                name="%s %s %s" % (design[:20], have["fields"][2][:10], have["fields"][3][:8]))


def check(cfw, design=None):
    """probe() plus what a core's .cfw needs on top: the identity map (our DDR2 controller; the other map would show noise),
    the flash limits of the updater, the generic logo. Returns (info, notes); raises CfwError."""
    info = probe(cfw)
    notes = []
    if info["map"] != "identity":
        raise CfwError("the bitstream writes no AXSS register: the host would stage the application under Gideon's DDR2 map "
                       "(host/pins.xdc sets BITSTREAM.CONFIG.USR_ACCESS; the flat build must not drop it)")
    notes.append("map identity (AXSS written), IDCODE %08X" % info["idcode"])
    if info["bit_len"] > P.BIT_MAX:
        raise CfwError("bitstream %d bytes, the updater's slot is %d" % (info["bit_len"], P.BIT_MAX))
    if info["app_len"] > P.APP_DATA_MAX:
        raise CfwError("application %d bytes, the updater's slot takes %d" % (info["app_len"], P.APP_DATA_MAX))
    notes.append("bitstream %d bytes (slot %d), application %d bytes (slot %d)" % (info["bit_len"], P.BIT_MAX, info["app_len"], P.APP_DATA_MAX))
    if design is not None and info["design"] != design:
        raise CfwError("design name %r, expected %r" % (info["design"], design))
    if design_kind(info["design"]) != "generic":
        raise CfwError("design name %r gets the logo of another firmware (cfwicon.cc)" % info["design"])
    notes.append("row \"%s\", generic logo unless a .icon lies beside it" % info["name"])
    return info, notes


def main(argv):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    p = sub.add_parser("prepare")
    p.add_argument("--core", required=True)
    p.add_argument("--bit", required=True)
    p.add_argument("--app-bin", required=True)
    p.add_argument("--out-bit", required=True)
    p.add_argument("--out-app", required=True)
    c = sub.add_parser("check")
    c.add_argument("--design")
    c.add_argument("file")
    a = ap.parse_args(argv)
    try:
        if a.cmd == "prepare":
            bit = rename_design(open(a.bit, "rb").read(), a.core)
            app = app_record(open(a.app_bin, "rb").read())
            P.check_app_record(app)
            open(a.out_bit, "wb").write(bit)
            open(a.out_app, "wb").write(app)
            print("prepared %s: design %s, bitstream %d bytes, application %d bytes" %
                  (a.core, bit_fields(bit)[0][0], len(bit), len(app)))
            return 0
        info, notes = check(open(a.file, "rb").read(), a.design)
    except (CfwError, ValueError) as e:
        print("%s: FAIL %s" % (getattr(a, "file", a.cmd), e), file=sys.stderr)
        return 1
    print("%s: PASS" % a.file)
    for n in notes:
        print("  ok   %s" % n)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
