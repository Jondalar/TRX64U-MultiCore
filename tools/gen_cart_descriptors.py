#!/usr/bin/env python3
"""Cartridge mapping descriptors (TREX-PROP-002, T33) from JSON to the bytes the
hardware takes (trx_io page 4, 0x101A4040-0x101A406A; the field map is in
vendor/icepi/c64_cartridge.v) and to the app's flash profile.

One JSON object per file, keyed by the app's CART_TYPE byte ("cart_type"; the
CRT type is informational: the app's CRT table maps CRT types to CART_TYPE).

    gen_cart_descriptors.py [--dir DIR ...] [--c-header F] [--verilog F] [--list]

--c-header: the table for the app (software/io/c64/trx_cart_desc.h) and the C++
bench; --verilog: a function cart_desc(cart_type) for the iverilog benches.
"""
import argparse
import glob
import json
import os
import sys

DESC_BYTES = 43

here = os.path.dirname(os.path.abspath(__file__))
default_dir = os.path.join(here, "cart_descriptors")


def num(v):
    if isinstance(v, str):
        return int(v, 0)
    return int(v)


def size(v):
    if isinstance(v, str):
        s = v.upper()
        if s.endswith("K"):
            return int(s[:-1]) * 1024
        if s.endswith("M"):
            return int(s[:-1]) * 1024 * 1024
    return num(v)


def reg(name):
    return {"R0": 0, "R1": 1, "R2": 2}[name]


def seg(f, maxw):
    w = num(f.get("width", 0))
    lsb = num(f.get("lsb", 0))
    if not 0 <= w <= maxw or not 0 <= lsb <= 7:
        raise ValueError("field %r" % f)
    return (w << 4) | (lsb << 1) | (reg(f.get("reg", "R0")) & 1)


IO_READ = {"OPEN": 0, "REG": 1, "RAM": 2, "DEV": 3}
LAYOUT = {  # b0 ROMH upper half, b1 linear flash chip, b2 ROMH = chip 1
    "L8": 0x01,         # ROML lower half of the bank's 16 KiB slot (ROMH, if ever seen: upper)
    "L8_MIRROR": 0x00,  # ROMH = the same 8 KiB as ROML
    "L16": 0x03,        # ROMH upper half; one flash chip, linear {BANK[6:0], ROMH, A12-A0}
    "SPLIT": 0x05,      # ROMH upper half = flash chip 1
}
FLASH_SEL = {"ROML_R": 1, "ROML_W": 2, "ROMH_R": 4, "ROMH_W": 8}


def encode(d):
    b = bytearray(DESC_BYTES)
    regs = d.get("regs", {})
    b[0] = num(regs.get("R0", 0))
    b[1] = num(regs.get("R1", 0))
    wm = d.get("wmask", {})
    b[2] = num(wm.get("R0", 0xFF))
    b[3] = num(wm.get("R1", 0xFF))
    # the third register (EasyFlash 3's slot, T51): off unless the entry names it
    b[38] = num(regs.get("R2", 0))
    b[39] = num(wm.get("R2", 0)) if "R2" in regs or "R2" in wm else 0
    rules = d.get("rules", [])
    if len(rules) > 4:
        raise ValueError("more than 4 rules")
    io2_write_rule = False
    for i, r in enumerate(rules):
        area = {"IO1": 0, "IO2": 1}[r["area"]]
        on = {"WRITE": 0, "READ": 1}[r.get("on", "WRITE")]
        src = {"DATA": 0, "ADDR": 1, "CONST": 2}[r.get("src", "DATA")]
        tgt = reg(r.get("target", "R0"))
        b[4 + 4 * i] = 0x80 | area | (on << 1) | ((tgt & 1) << 2) | (src << 3)
        b[37] |= (tgt >> 1) << (4 + i)
        b[5 + 4 * i] = num(r.get("match", 0))
        b[6 + 4 * i] = num(r.get("mask", 0))
        b[7 + 4 * i] = num(r.get("value", 0))
        io2_write_rule |= area == 1 and on == 0
    bank = d.get("bank", [])
    if len(bank) > 2:
        raise ValueError("more than 2 bank segments")
    wide = d.get("bank9", False)
    if sum(num(s.get("width", 0)) for s in bank) > (9 if wide else 8):
        raise ValueError("bank wider than 8 bits (9 with bank9)")
    if len(bank) == 1:
        b[21] = seg(bank[0], 8)
        b[37] |= (reg(bank[0].get("reg", "R0")) >> 1) << 1
    elif len(bank) == 2:
        b[20] = seg(bank[0], 8)
        b[21] = seg(bank[1], 8)
        b[37] |= (reg(bank[0].get("reg", "R0")) >> 1) << 0
        b[37] |= (reg(bank[1].get("reg", "R0")) >> 1) << 1
    mode = d.get("mode", {"width": 0})
    b[22] = seg(mode, 3)
    b[37] |= (reg(mode.get("reg", "R0")) >> 1) << 2
    rd = d.get("read_r2")
    if rd:                                  # an IO1 read of R2 at (A7-A0 ^ match) & mask == 0
        b[40] = num(rd["match"])
        b[41] = num(rd.get("mask", 0xFF))
        b[42] |= 0x01
    if wide:
        b[42] |= 0x02
    fs = num(d.get("flash_split", 0))      # 3: ROMH above the lowest 3 bank bits in the flash chip address
    if fs not in (0, 3):
        raise ValueError("flash_split 0 or 3")
    if fs:
        b[42] |= 0x04
    io1 = d.get("io1", {})
    io2 = d.get("io2", {})
    io = IO_READ[io1.get("read", "OPEN")] | (IO_READ[io2.get("read", "OPEN")] << 2)
    if "RAM" in io1.get("write", []):
        io |= 0x10
    if "RAM" in io2.get("write", []):
        io |= 0x20
    if io2.get("we_valid", io2_write_rule or "RAM" in io2.get("write", [])):
        io |= 0x40
    b[23] = io
    table = d.get("table", [{}])
    n = 1 << (mode.get("width", 0) and num(mode["width"]))
    if len(table) != n:
        raise ValueError("table has %d entries, the mode field selects %d" % (len(table), n))
    for i in range(8):
        e = table[i] if i < len(table) else {"off": 1}
        off = num(e.get("off", 0))
        ex = 1 if off else num(e.get("exrom", 1))
        ga = 1 if off else num(e.get("game", 1))
        gamew = "gamew" in e
        if gamew and num(e["gamew"]) != 0:
            raise ValueError("gamew can only be 0 (GAME low in CPU writes)")
        b[24 + i] = ex | (ga << 1) | (num(e.get("hold", 0)) << 2) | (gamew << 3) | (num(e.get("kill", 0)) << 4)
    lay = LAYOUT[d.get("layout", "L8")]
    ee = d.get("eeprom")
    if ee:
        lay |= 0x08
        b[33] = num(ee["cs"]) | (num(ee["clk"]) << 3)
        b[34] = num(ee["di"]) | (num(ee["do"]) << 3)
    if d.get("ar"):
        lay |= 0x10
    b[32] = lay
    fl = d.get("flash")
    if fl:
        for s in fl["select"]:
            b[35] |= FLASH_SEL[s]
    b[36] = 1  # valid
    return bytes(b)


def flash_profile(d):
    fl = d.get("flash")
    if not fl:
        return None
    runs = [(num(s["count"]), size(s["size"])) for s in fl["sectors"]]
    chip = size(fl["size"])
    if sum(c * s for c, s in runs) != chip:
        raise ValueError("%s: sectors do not add up to the chip" % d["name"])
    if len(runs) > 6:
        raise ValueError("more than 6 sector runs")
    return {
        "part": fl["part"], "magic1": num(fl["magic1"]), "magic2": num(fl["magic2"]),
        "mask": num(fl["mask"]), "id": [num(x) for x in fl["id"]], "id_ofs": num(fl["id_ofs"]),
        "size": chip, "chips": num(fl.get("chips", 1)), "runs": runs,
        "linear": 2 if d.get("flash_split") else int(d.get("layout") == "L16"),
    }


def load(dirs):
    out = []
    for dd in dirs:
        for f in sorted(glob.glob(os.path.join(dd, "*.json"))):
            d = json.load(open(f))
            d["_file"] = os.path.basename(f)
            d["_bytes"] = encode(d)
            d["_flash"] = flash_profile(d)
            out.append(d)
    keys = {}
    for d in out:
        k = num(d["cart_type"])
        if k in keys:
            raise ValueError("cart_type 0x%02X twice: %s, %s" % (k, keys[k], d["_file"]))
        keys[k] = d["_file"]
    return sorted(out, key=lambda d: num(d["cart_type"]))


def c_header(descs, srcs):
    L = []
    L.append("// Generated by TRX64-Ultimate tools/gen_cart_descriptors.py from")
    for s in srcs:
        L.append("// %s/*.json. Do not edit: change the JSON and run the generator." % s)
    L.append("//")
    L.append("// Per CART_TYPE byte: the cartridge mapping descriptor (TREX-PROP-002, T33),")
    L.append("// written to trx_io page 4 at 0x040-0x06A, and the flash part behind it.")
    L.append("#ifndef TRX_CART_DESC_H")
    L.append("#define TRX_CART_DESC_H")
    L.append("")
    L.append("#include <stdint.h>")
    L.append("")
    L.append("#define TRX_CART_DESC_BYTES %d" % DESC_BYTES)
    L.append("")
    L.append("typedef struct {")
    L.append("    uint16_t count;          // sectors of this size, from the chip's start upwards")
    L.append("    uint32_t size;")
    L.append("} trx_sector_run_t;")
    L.append("")
    L.append("typedef struct {")
    L.append("    const char *part;")
    L.append("    uint8_t  chips;          // 2: ROML is chip 0, ROMH chip 1 (interleaved per bank in the image)")
    L.append("    uint8_t  linear;         // 1: one chip across ROML and ROMH, chip address = image offset; 2: EasyFlash 3 (flash_split 3), chip order {slot, bank_hi, ROMH, bank_lo, A12-A0}")
    L.append("    uint16_t magic1, magic2, mask;")
    L.append("    uint8_t  id_mfr, id_dev, id_ofs;")
    L.append("    uint32_t chip_size;")
    L.append("    uint8_t  runs;")
    L.append("    trx_sector_run_t run[6];")
    L.append("} trx_flash_part_t;")
    L.append("")
    L.append("typedef struct {")
    L.append("    uint8_t  cart_type;      // the app's CART_TYPE byte")
    L.append("    const char *name;")
    L.append("    const trx_flash_part_t *flash;   // NULL: no flash the C64 can program")
    L.append("    uint8_t  desc[TRX_CART_DESC_BYTES];")
    L.append("} trx_cart_desc_t;")
    L.append("")
    for d in descs:
        p = d["_flash"]
        if not p:
            continue
        runs = ", ".join("{ %d, 0x%X }" % r for r in p["runs"])
        L.append("static const trx_flash_part_t c_flash_%02x = { \"%s\", %d, %d, 0x%04X, 0x%04X, 0x%04X, 0x%02X, 0x%02X, %d, 0x%X, %d, { %s } };"
                 % (num(d["cart_type"]), p["part"], p["chips"], p["linear"], p["magic1"], p["magic2"], p["mask"],
                    p["id"][0], p["id"][1], p["id_ofs"], p["size"], len(p["runs"]), runs))
    L.append("")
    L.append("static const trx_cart_desc_t c_cart_descs[] = {")
    for d in descs:
        k = num(d["cart_type"])
        fl = "&c_flash_%02x" % k if d["_flash"] else "0"
        by = ", ".join("0x%02X" % x for x in d["_bytes"])
        L.append("    { 0x%02X, \"%s\", %s,  // %s" % (k, d["name"], fl, d["_file"]))
        L.append("      { %s } }," % by)
    L.append("};")
    L.append("")
    L.append("#endif")
    return "\n".join(L) + "\n"


def verilog(descs):
    L = ["// Generated by tools/gen_cart_descriptors.py: the descriptor per CART_TYPE byte.",
         "function [%d:0] cart_desc(input [7:0] t);" % (DESC_BYTES * 8 - 1),
         "    case (t)"]
    for d in descs:
        v = int.from_bytes(d["_bytes"], "little")
        L.append("    8'h%02X: cart_desc = %d'h%0*X;  // %s" % (num(d["cart_type"]), DESC_BYTES * 8, DESC_BYTES * 2, v, d["name"]))
    L.append("    default: cart_desc = %d'd0;" % (DESC_BYTES * 8))
    L.append("    endcase")
    L.append("endfunction")
    return "\n".join(L) + "\n"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--dir", action="append")
    ap.add_argument("--c-header")
    ap.add_argument("--verilog")
    ap.add_argument("--list", action="store_true")
    a = ap.parse_args()
    dirs = a.dir or [default_dir]
    descs = load(dirs)
    if a.c_header:
        srcs = [os.path.relpath(os.path.abspath(x), os.path.dirname(here)) for x in dirs]
        open(a.c_header, "w").write(c_header(descs, srcs))
    if a.verilog:
        open(a.verilog, "w").write(verilog(descs))
    if a.list or not (a.c_header or a.verilog):
        for d in descs:
            print("%02X %-36s %s" % (num(d["cart_type"]), d["name"], d["_bytes"].hex()))
    return 0


if __name__ == "__main__":
    sys.exit(main())
