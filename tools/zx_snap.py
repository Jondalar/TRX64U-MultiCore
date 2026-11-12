#!/usr/bin/env python3
"""T91 D10 / S11: ZX Spectrum snapshots (SNA 48K and 128K, Z80 v1/v2/v3): the formats' reader and writer, written for this project from the
published file layouts (hardware facts: the formats are documented by their authors), and the load sequence the app has to perform on
the core's registers (cores/zx/trx/zx_regs.v). The app is C++ and not built yet; this file is its reference and the bench's loader.

  tools/zx_snap.py make <dir>      the bench's snapshot files (SNA 48K/128K, Z80 v2/v3), deterministic
  tools/zx_snap.py corpus <dir>    every container (SNA, Z80 v1/v2/v3) with its expected register and RAM image, for tests/coreapp (temporary files)
  tools/zx_snap.py bench <file> <prefix>   what tb_zx.v loads from one snapshot file (RAM image, register-window writes)
  tools/zx_snap.py build <progdir> / check <progdir>   the oracle's expected result of snap48 (needs the z80 package) / that it is still the one for this generator
  tools/zx_snap.py selftest        writer -> reader round trips on random states, the refusal of snapshots that are no 48K/128K machine
state: dict(model=48|128, a f bc de hl a2 f2 bc2 de2 hl2 ix iy sp pc i r iff1 iff2 im border p7ffd ay=[16] ay_sel banks=[8 x 16 KB]
(model 48: banks[5] = $4000, banks[2] = $8000, banks[0] = $C000)).
"""
import random
import sys


class SnapError(Exception):
    pass


def u16(b, o):
    return b[o] | (b[o + 1] << 8)


# ---------------------------------------------------------------- SNA
def sna_read(data):
    if len(data) == 49179:
        model = 48
    elif len(data) in (131103, 147487):
        model = 128
    else:
        raise SnapError("SNA of %d bytes is neither a 48K nor a 128K snapshot" % len(data))
    h = data
    st = dict(model=model, i=h[0], hl2=u16(h, 1), de2=u16(h, 3), bc2=u16(h, 5), f2=h[7], a2=h[8], hl=u16(h, 9), de=u16(h, 11), bc=u16(h, 13),
              iy=u16(h, 15), ix=u16(h, 17), iff2=(h[19] >> 2) & 1, r=h[20], f=h[21], a=h[22], sp=u16(h, 23), im=h[25], border=h[26] & 7)
    st["iff1"] = st["iff2"]
    banks = [bytes(16384) for _ in range(8)]
    ay = [0] * 16
    if model == 48:
        banks[5] = bytes(data[27:27 + 16384]); banks[2] = bytes(data[27 + 16384:27 + 32768]); banks[0] = bytes(data[27 + 32768:27 + 49152])
        sp = st["sp"]
        ram = lambda a: banks[{1: 5, 2: 2, 3: 0}[a >> 14]][a & 0x3FFF]
        st["pc"] = ram(sp) | (ram((sp + 1) & 0xFFFF) << 8)
        st["sp"] = (sp + 2) & 0xFFFF
        st["p7ffd"] = 0
    else:
        pc = u16(data, 27 + 49152)
        p7 = data[27 + 49152 + 2]
        banks[5] = bytes(data[27:27 + 16384]); banks[2] = bytes(data[27 + 16384:27 + 32768]); cur = p7 & 7
        banks[cur] = bytes(data[27 + 32768:27 + 49152])
        rest = [b for b in range(8) if b not in (5, 2, cur)]
        o = 27 + 49152 + 4
        for b in rest:
            banks[b] = bytes(data[o:o + 16384]); o += 16384
        st["pc"], st["p7ffd"] = pc, p7
    st["banks"], st["ay"], st["ay_sel"] = banks, ay, 0
    return st


def sna_write(st):
    h = bytearray(27)
    h[0] = st["i"]
    for off, k in ((1, "hl2"), (3, "de2"), (5, "bc2"), (9, "hl"), (11, "de"), (13, "bc"), (15, "iy"), (17, "ix")):
        h[off], h[off + 1] = st[k] & 255, st[k] >> 8
    h[7], h[8] = st["f2"], st["a2"]
    h[19] = 4 if st["iff2"] else 0
    h[20] = st["r"]
    h[21], h[22] = st["f"], st["a"]
    h[25] = st["im"]
    h[26] = st["border"]
    b = st["banks"]
    if st["model"] == 48:
        ram = bytearray(b[5] + b[2] + b[0])
        sp = (st["sp"] - 2) & 0xFFFF                       # PC is pushed on the stack
        idx = lambda a: {1: 0, 2: 1, 3: 2}[a >> 14] * 16384 + (a & 0x3FFF)
        ram[idx(sp)], ram[idx((sp + 1) & 0xFFFF)] = st["pc"] & 255, st["pc"] >> 8
        h[23], h[24] = sp & 255, sp >> 8
        return bytes(h) + bytes(ram)
    h[23], h[24] = st["sp"] & 255, st["sp"] >> 8
    cur = st["p7ffd"] & 7
    out = bytes(h) + b[5] + b[2] + b[cur] + bytes([st["pc"] & 255, st["pc"] >> 8, st["p7ffd"], 0])
    for k in range(8):
        if k not in (5, 2, cur):
            out += b[k]
    return out


# ---------------------------------------------------------------- Z80
def z80_unpack(data, size):
    out = bytearray()
    i = 0
    while len(out) < size:
        if i + 4 <= len(data) and data[i] == 0xED and data[i + 1] == 0xED:
            out += bytes([data[i + 3]]) * data[i + 2]
            i += 4
        else:
            out.append(data[i]); i += 1
    if len(out) != size:
        raise SnapError("bad Z80 compression")
    return bytes(out), i


def z80_pack(data):
    out = bytearray()
    i = 0
    n = len(data)
    while i < n:
        j = i
        while j < n and data[j] == data[i] and j - i < 255:
            j += 1
        run = j - i
        if run >= 5 or (data[i] == 0xED and run >= 2):
            out += bytes([0xED, 0xED, run, data[i]]); i = j
        else:
            out.append(data[i]); i += 1
            if data[i - 1] == 0xED and i < n:          # a single ED must not be followed by another ED being taken as a run
                out.append(data[i]); i += 1
    return bytes(out)


def z80_read(data):
    if len(data) < 30:
        raise SnapError("Z80 file too short")
    h = data
    flags = h[12]
    if flags == 255:
        flags = 1
    st = dict(a=h[0], f=h[1], bc=u16(h, 2), hl=u16(h, 4), pc=u16(h, 6), sp=u16(h, 8), i=h[10], r=(h[11] & 0x7F) | ((flags & 1) << 7),
              border=(flags >> 1) & 7, de=u16(h, 13), bc2=u16(h, 15), de2=u16(h, 17), hl2=u16(h, 19), a2=h[21], f2=h[22], iy=u16(h, 23),
              ix=u16(h, 25), iff1=1 if h[27] else 0, iff2=1 if h[28] else 0, im=h[29] & 3, p7ffd=0, ay=[0] * 16, ay_sel=0)
    banks = [bytes(16384) for _ in range(8)]
    if st["pc"] != 0:                                      # version 1: 48K, one block
        body = data[30:]
        if flags & 0x20:
            if body[-4:] != b"\x00\xed\xed\x00":
                raise SnapError("Z80 v1 without end marker")
            ram, _ = z80_unpack(body[:-4], 49152)
        else:
            ram = bytes(body[:49152])
        banks[5], banks[2], banks[0] = ram[:16384], ram[16384:32768], ram[32768:]
        st.update(model=48, banks=banks)
        return st
    elen = u16(h, 30)
    if elen not in (23, 54, 55):
        raise SnapError("unknown Z80 extended header length %d" % elen)
    ver = 2 if elen == 23 else 3
    st["pc"] = u16(h, 32)
    hw = h[34]
    if ver == 2:
        model = {0: 48, 1: 48, 3: 128, 4: 128}.get(hw)
    else:
        model = {0: 48, 1: 48, 4: 128, 5: 128}.get(hw)
    if model is None:
        raise SnapError("Z80 hardware mode %d (v%d) is not a 48K or 128K Spectrum" % (hw, ver))
    if h[37] & 0x80:
        pass                                               # "modify hardware" bit: 16K / +2 variants, treated as the listed model
    st["p7ffd"] = h[35] if model == 128 else 0
    st["ay"] = list(h[39:55])
    st["ay_sel"] = h[38] & 15
    o = 32 + elen
    pages = {}
    while o + 3 <= len(data):
        ln, pg = u16(data, o), data[o + 2]
        o += 3
        if ln == 0xFFFF:
            blk = bytes(data[o:o + 16384]); o += 16384
        else:
            blk, _ = z80_unpack(data[o:o + ln], 16384); o += ln
        pages[pg] = blk
    if model == 48:
        for pg, b in ((8, 5), (4, 2), (5, 0)):
            if pg not in pages:
                raise SnapError("Z80 48K snapshot without page %d" % pg)
            banks[b] = pages[pg]
    else:
        for pg in range(3, 11):
            if pg not in pages:
                raise SnapError("Z80 128K snapshot without page %d" % pg)
            banks[pg - 3] = pages[pg]
    st.update(model=model, banks=banks)
    return st


def z80_write(st, ver=3, compress=True):
    h = bytearray(30)
    h[0], h[1] = st["a"], st["f"]
    for off, k in ((2, "bc"), (4, "hl"), (8, "sp"), (13, "de"), (15, "bc2"), (17, "de2"), (19, "hl2"), (23, "iy"), (25, "ix")):
        h[off], h[off + 1] = st[k] & 255, st[k] >> 8
    h[10] = st["i"]
    h[11] = st["r"] & 0x7F
    h[12] = ((st["r"] >> 7) & 1) | (st["border"] << 1) | (0x20 if compress else 0)
    h[21], h[22] = st["a2"], st["f2"]
    h[27], h[28] = (0xFF if st["iff1"] else 0), (0xFF if st["iff2"] else 0)
    h[29] = st["im"]
    b = st["banks"]
    if ver == 1:
        if st["model"] != 48:
            raise SnapError("version 1 holds a 48K machine only")
        h[6], h[7] = st["pc"] & 255, st["pc"] >> 8
        ram = b[5] + b[2] + b[0]
        return bytes(h) + (z80_pack(ram) + b"\x00\xed\xed\x00" if compress else ram)
    ext = bytearray(23 if ver == 2 else 54)
    ext[0], ext[1] = st["pc"] & 255, st["pc"] >> 8
    ext[2] = (0 if st["model"] == 48 else 3) if ver == 2 else (0 if st["model"] == 48 else 4)
    if st["model"] == 128:
        ext[3] = st["p7ffd"]
        ext[6] = st["ay_sel"]
        ext[7:23] = bytes(st["ay"])
    h += bytes(2)
    h[30], h[31] = len(ext), 0
    out = bytes(h) + bytes(ext)
    pages = [(8, b[5]), (4, b[2]), (5, b[0])] if st["model"] == 48 else [(3 + k, b[k]) for k in range(8)]
    for pg, blk in pages:
        if compress:
            c = z80_pack(blk)
            if len(c) < 16384:
                out += bytes([len(c) & 255, len(c) >> 8, pg]) + c
                continue
        out += bytes([0xFF, 0xFF, pg]) + blk
    return out


# ---------------------------------------------------------------- the load sequence of the app
def load_script(st):
    """the writes the app performs, as (what, address, value): 'reg' = a byte of the core's register window (offsets of zx_regs.v),
    'ram' = a byte of the RAM window (linear address bank * 16384 + offset), 'rom' is not part of a snapshot"""
    s = []
    s.append(("reg", 0x0A0, 0x01))                         # hold the reset
    s.append(("reg", 0x0A1, 0x01 if st["model"] == 128 else 0x00))
    for bank in range(8):
        if st["model"] == 48 and bank not in (5, 2, 0):
            continue
        for off, v in enumerate(st["banks"][bank]):
            s.append(("ram", bank * 16384 + off, v))
    dir_ = bytearray(27)
    put = lambda o, v, n: dir_.__setitem__(slice(o, o + n), int(v).to_bytes(n, "little"))
    put(0, st["a"], 1); put(1, st["f"], 1); put(2, st["a2"], 1); put(3, st["f2"], 1); put(4, st["i"], 1); put(5, st["r"], 1)
    put(6, st["sp"], 2); put(8, st["pc"], 2)
    put(10, st["bc"], 2); put(12, st["de"], 2); put(14, st["hl"], 2); put(16, st["ix"], 2)
    put(18, st["bc2"], 2); put(20, st["de2"], 2); put(22, st["hl2"], 2); put(24, st["iy"], 2)
    put(26, (st["im"] & 3) | (st["iff1"] << 2) | (st["iff2"] << 3), 1)         # DIR[209:208] IM, [210] IFF1, [211] IFF2 -> bits 17:16 of byte 26 (see dir_bits)
    return s, bytes(dir_)


def dir_bits(st):
    """the 212-bit register image T80pa takes (DIR), as an integer: A F A' F' I R SP PC | BC DE HL IX BC' DE' HL' IY | IM IFF1 IFF2"""
    v = 0
    v |= st["a"] | (st["f"] << 8) | (st["a2"] << 16) | (st["f2"] << 24) | (st["i"] << 32) | (st["r"] << 40) | (st["sp"] << 48) | (st["pc"] << 64)
    v |= st["bc"] << 80
    v |= st["de"] << 96
    v |= st["hl"] << 112
    v |= st["ix"] << 128
    v |= st["bc2"] << 144
    v |= st["de2"] << 160
    v |= st["hl2"] << 176
    v |= st["iy"] << 192
    v |= (st["im"] & 3) << 208
    v |= st["iff1"] << 210
    v |= st["iff2"] << 211
    return v


def dir_bytes(st):
    return dir_bits(st).to_bytes(27, "little")


def random_state(rnd, model):
    banks = [bytes(rnd.randrange(256) for _ in range(16384)) for _ in range(8)]
    st = dict(model=model, a=rnd.randrange(256), f=rnd.randrange(256), bc=rnd.randrange(65536), de=rnd.randrange(65536), hl=rnd.randrange(65536),
              a2=rnd.randrange(256), f2=rnd.randrange(256), bc2=rnd.randrange(65536), de2=rnd.randrange(65536), hl2=rnd.randrange(65536),
              ix=rnd.randrange(65536), iy=rnd.randrange(65536), sp=rnd.randrange(0x4002, 0xFFF0), pc=rnd.randrange(0x4000, 0xFFFF), i=rnd.randrange(256),
              r=rnd.randrange(256), iff1=rnd.randrange(2), im=rnd.randrange(3), border=rnd.randrange(8), p7ffd=(rnd.randrange(32) if model == 128 else 0),
              ay=[rnd.randrange(256) for _ in range(16)], ay_sel=rnd.randrange(16), banks=banks)
    st["iff2"] = st["iff1"]
    if model == 48:
        st["ay"] = [0] * 16; st["ay_sel"] = 0
    return st


def same(a, b, keys):
    return all(a[k] == b[k] for k in keys)


def selftest():
    rnd = random.Random(7)
    common = ["a", "f", "bc", "de", "hl", "a2", "f2", "bc2", "de2", "hl2", "ix", "iy", "sp", "pc", "i", "r", "iff2", "im", "border"]
    for model in (48, 128):
        st = random_state(rnd, model)
        keep = list(range(8)) if model == 128 else [5, 2, 0]
        # SNA
        if model == 48:
            st["sp"] = rnd.randrange(0x4002, 0xFFF0)
        r = sna_read(sna_write(st))
        assert same(st, r, common + ["model"]), "SNA %d round trip" % model
        want = [bytearray(x) for x in st["banks"]]
        if model == 48:                                    # the 48K SNA keeps the pushed PC in RAM, two bytes below SP
            for k in (0, 1):
                a = (st["sp"] - 2 + k) & 0xFFFF
                want[{1: 5, 2: 2, 3: 0}[a >> 14]][a & 0x3FFF] = (st["pc"] >> (8 * k)) & 255
        assert all(bytes(want[b]) == r["banks"][b] for b in keep), "SNA %d RAM" % model
        # Z80
        for ver in (1, 2, 3):
            if ver == 1 and model != 48:
                continue
            for comp in (False, True):
                z = z80_read(z80_write(st, ver, comp))
                assert same(st, z, common + ["model", "iff1"]), "Z80 v%d %d comp=%s" % (ver, model, comp)
                assert all(st["banks"][b] == z["banks"][b] for b in keep), "Z80 v%d RAM" % ver
                if model == 128 and ver > 1:
                    assert z["ay"] == st["ay"] and z["p7ffd"] == st["p7ffd"], "Z80 v%d AY/7FFD" % ver
    # compression corner cases: ED ED runs and single ED at block ends
    for blk in (bytes([0xED] * 16384), bytes([0xED, 0x01] * 8192), bytes(16383) + b"\xed", b"\xed" + bytes(16383),
                bytes(rnd.choice((0, 0xED, 0xED, 1)) for _ in range(16384))):
        out, n = z80_unpack(z80_pack(blk), 16384)
        assert out == blk, "pack/unpack"
    # refusals
    for bad in (b"", bytes(100), bytes(49178)):
        try:
            sna_read(bad); assert False
        except SnapError:
            pass
    st = random_state(rnd, 128)
    z = bytearray(z80_write(st, 3, True))
    z[34] = 7                                             # hardware mode 7 of v3: Spectrum +3
    try:
        z80_read(bytes(z)); assert False
    except SnapError as e:
        print("selftest: refused as expected: %s" % e)
    print("selftest: SNA 48K/128K, Z80 v1/v2/v3 (plain and compressed) round trips and refusals ok")
    return 0


# ---------------------------------------------------------------- the bench's snapshots (S11): made by us, loaded through the parsers above
SNAP48 = """\
        org 0x8000
start:  ld (0x9000), bc
        ld (0x9002), de
        ld (0x9004), hl
        ld (0x9006), sp
        ld (0x9008), ix
        ld (0x900A), iy
        push af
        pop hl
        ld (0x900C), hl
        ex af, af'
        push af
        pop hl
        ld (0x900E), hl
        ex af, af'
        exx
        ld (0x9010), bc
        ld (0x9012), de
        ld (0x9014), hl
        exx
        ld a, i
        ld (0x9016), a
        push af
        pop hl
        ld (0x9018), hl
        ld a, r
        ld (0x901A), a
        ld sp, 0xBFF0
        ld a, 0xA5
        ld (0x9FFF), a
spin:   jr spin
"""

SNAPINT = """\
        org 0x8000
        ds 0x100, 0x82
        db 0x82
        org 0x8282
isr:    push af
        ld a, (0x9100)
        inc a
        ld (0x9100), a
        pop af
        ei
        reti
        org 0x8300
main:   jr main
"""

SNAP128 = """\
        org 0x8000
start:  ld a, (0xC000)
        ld (0x9020), a
        ld a, (0x0000)
        ld (0x9021), a
        ld hl, 0x9030
        ld d, 0
nxt:    ld bc, 0xFFFD
        out (c), d
        in a, (c)
        ld (hl), a
        inc hl
        inc d
        ld a, d
        cp 14
        jr nz, nxt
        ld a, 0xA5
        ld (0x9FFF), a
spin:   jr spin
"""


def assemble(text):
    sys.path.insert(0, __file__.rsplit("/", 1)[0])
    import zx_asm
    return zx_asm.assemble(text)


def put_ram(st, addr, data):
    for k, v in enumerate(data):
        a = addr + k
        if st["model"] == 48:
            bank = {1: 5, 2: 2, 3: 0}[a >> 14]
        else:
            bank = {1: 5, 2: 2, 3: st["p7ffd"] & 7}[a >> 14]
        b = bytearray(st["banks"][bank]); b[a & 0x3FFF] = v; st["banks"][bank] = bytes(b)


def base_state(model, rnd):
    st = random_state(rnd, model)
    st["banks"] = [bytes(16384) for _ in range(8)]
    st["pc"], st["iff1"], st["iff2"], st["im"] = 0x8000, 0, 0, 1
    st["sp"] = rnd.randrange(0xB000, 0xBF00) & 0xFFFE
    return st


def make_tests(outdir):
    """snapshot files of our own and what the bench needs from them; the oracle (z80 package) gives the expected results of snap48"""
    import os
    rnd = random.Random(11)
    # (a) 48K, interrupts off: all registers are saved by the program, compared with the emulator's run
    st = base_state(48, rnd)
    st["r"] = rnd.randrange(0, 0x7F)
    put_ram(st, 0x8000, assemble(SNAP48)[0x8000:])
    open(os.path.join(outdir, "snap48.sna"), "wb").write(sna_write(st))
    open(os.path.join(outdir, "snap48.z80"), "wb").write(z80_write(st, 3, True))
    # (b) 48K, IM 2 with I = 0x80 and interrupts on
    st = base_state(48, rnd)
    st["i"], st["im"], st["iff1"], st["iff2"], st["pc"] = 0x80, 2, 1, 1, 0x8300
    put_ram(st, 0x8000, assemble(SNAPINT)[0x8000:])
    open(os.path.join(outdir, "snapint.z80"), "wb").write(z80_write(st, 2, True))
    # (c) 128K: bank 3 at $C000, ROM 1, AY registers, border 5
    st = base_state(128, rnd)
    st["p7ffd"] = 0x13
    st["border"] = 5
    st["ay"] = [rnd.randrange(256) for _ in range(16)]
    st["ay"][7] = 0x38
    st["ay_sel"] = 9
    st["banks"][3] = bytes([0x33]) + bytes(16383)
    put_ram(st, 0x8000, assemble(SNAP128)[0x8000:])
    open(os.path.join(outdir, "snap128.sna"), "wb").write(sna_write(st))          # the SNA has no AY registers: the Z80 file below has
    open(os.path.join(outdir, "snap128.z80"), "wb").write(z80_write(st, 3, True))


def make_corpus(outdir):
    """every container the app has to read, from random states: SNA 48K, SNA 128K (the paged bank 3 and 5, where the file holds one bank
    more), Z80 v1/v2/v3 plain and packed; next to each file its expected result as bench_inputs() makes it (the C++ reader of the application,
    coreapp/cores/zx_snap.cc, is compared against it by tests/coreapp/test_zx_snap.cc). Temporary files only: no snapshot is ever committed."""
    import os
    rnd = random.Random(23)
    out = []

    def runs(st):
        """random bytes with the cases of the packer in them: long runs, runs of ED, a single ED, a run that ends the bank"""
        banks = []
        for b in st["banks"]:
            d = bytearray(b)
            d[100:400] = bytes([0xED]) * 300
            d[1000:2000] = bytes(1000)
            d[3000:3001] = b"\xed"
            d[3001] = 0x07
            d[5000:5005] = bytes([0x41]) * 5
            d[9000:9004] = bytes([0x99]) * 4
            d[-40:] = bytes([0xED]) * 40
            banks.append(bytes(d))
        st["banks"] = banks
        return st

    st = runs(random_state(rnd, 48))
    out.append(("sna48.sna", sna_write(st)))
    for ver in (1, 2, 3):
        for comp in (False, True):
            out.append(("z80v%d%s_48.z80" % (ver, "p" if comp else "r"), z80_write(st, ver, comp)))
    for p7 in (0x13, 0x15, 0x1A):
        st = runs(random_state(rnd, 128))
        st["p7ffd"] = p7
        out.append(("sna128_%02x.sna" % p7, sna_write(st)))
        for ver in (2, 3):
            for comp in (False, True):
                out.append(("z80v%d%s_128_%02x.z80" % (ver, "p" if comp else "r", p7), z80_write(st, ver, comp)))
    for name, data in out:
        path = os.path.join(outdir, name)
        open(path, "wb").write(data)
        bench_inputs(path, os.path.join(outdir, name.rsplit(".", 1)[0]))
    open(os.path.join(outdir, "INDEX"), "w").write("\n".join(n for n, _ in out) + "\n")


def bench_inputs(path, outprefix):
    """what tb_zx.v loads for a snapshot file: <prefix>.ram.hex (the banks it holds, one byte per line, in the order of <prefix>.meta),
    <prefix>.regs.hex (pairs 'offset value' of the register window)"""
    data = open(path, "rb").read()
    st = z80_read(data) if path.endswith(".z80") else sna_read(data)
    banks = [5, 2, 0] if st["model"] == 48 else list(range(8))
    with open(outprefix + ".ram.hex", "w") as f:
        for b in banks:
            for v in st["banks"][b]:
                f.write("%02x\n" % v)
    d = dir_bytes(st)
    regs = [(0x100 + k, d[k]) for k in range(27)]
    regs.append((0x11B, st["p7ffd"]))
    regs.append((0x11C, st["border"]))
    for k in range(16):
        regs.append((0x120 + k, st["ay"][k]))
    regs.append((0x130, st["ay_sel"]))
    regs.append((0x0A1, 1 if st["model"] == 128 else 0))
    with open(outprefix + ".regs.hex", "w") as f:
        for a, v in regs:
            f.write("%03x %02x\n" % (a, v))
    open(outprefix + ".meta", "w").write("MODEL=%d\nBANKS=%s\n" % (st["model"], ",".join(map(str, banks))))
    return st


def oracle_snap48(st):
    import z80
    m = z80.Z80Machine()
    mem = bytearray(65536)
    for a in range(0x4000, 0x10000):
        mem[a] = st["banks"][{1: 5, 2: 2, 3: 0}[a >> 14]][a & 0x3FFF]
    m.memory[:] = mem
    for k in ("a", "f", "bc", "de", "hl", "ix", "iy", "sp", "pc", "i", "r"):
        setattr(m, k, st[k])
    m.alt_a, m.alt_f, m.alt_bc, m.alt_de, m.alt_hl = st["a2"], st["f2"], st["bc2"], st["de2"], st["hl2"]
    m.iff1, m.iff2 = st["iff1"], st["iff2"]
    for _ in range(50):
        m.ticks_to_stop = 100000
        m.run()
        if m.memory[0x9FFF] == 0xA5:
            break
    else:
        raise SystemExit("zx_snap: the oracle did not finish snap48")
    return bytes(m.memory[0x9000:0x9100])


def snap48_sha():
    import hashlib, tempfile, os
    d = tempfile.mkdtemp()
    make_tests(d)
    return hashlib.sha256(open(os.path.join(d, "snap48.sna"), "rb").read()).hexdigest()


def build(progdir):
    """the oracle's result for snap48 and the hash of the snapshot it was made for (the other files are made at run time)"""
    import os, tempfile
    d = tempfile.mkdtemp()
    make_tests(d)
    st = bench_inputs(os.path.join(d, "snap48.sna"), os.path.join(d, "snap48_sna"))
    exp = oracle_snap48(st)
    with open(os.path.join(progdir, "snap48.exp.hex"), "w") as f:
        for v in exp:
            f.write("%02x\n" % v)
    open(os.path.join(progdir, "snap48.sha256"), "w").write(snap48_sha() + "\n")
    print("snap48: oracle result 256 bytes")


def check(progdir):
    import os
    p = os.path.join(progdir, "snap48.sha256")
    if not os.path.exists(p) or open(p).read().strip() != snap48_sha():
        print("zx_snap: snap48.exp.hex was not made for the snapshot this file generates (run tools/zx_progs.py build)")
        return 1
    print("zx_snap: snap48 matches its expected result")
    return 0


if __name__ == "__main__":
    if sys.argv[1:2] == ["build"]:
        build(sys.argv[2])
        sys.exit(0)
    if sys.argv[1:2] == ["make"]:
        make_tests(sys.argv[2])
        sys.exit(0)
    if sys.argv[1:2] == ["corpus"]:
        make_corpus(sys.argv[2])
        sys.exit(0)
    if sys.argv[1:2] == ["bench"]:
        bench_inputs(sys.argv[2], sys.argv[3])
        sys.exit(0)
    if sys.argv[1:2] == ["check"]:
        sys.exit(check(sys.argv[2]))
    sys.exit(selftest() if sys.argv[1:] == ["selftest"] else (print(__doc__) or 2))
