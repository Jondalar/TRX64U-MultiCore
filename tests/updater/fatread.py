"""A small read-only FAT12/16/32 reader (superfloppy image, no MBR) for the updater test: list a directory and read a file by short or long name."""
import struct
class Fat:
    def __init__(self, data):
        self.d = data
        bs = data[:512]
        self.bps, self.spc, self.rsv, self.nfat, self.rootn, tot16, _, self.fatsz16 = struct.unpack_from("<HBHBHHBH", bs, 11)
        tot32 = struct.unpack_from("<I", bs, 32)[0]
        self.fatsz = self.fatsz16 or struct.unpack_from("<I", bs, 36)[0]
        self.tot = tot16 or tot32
        self.rootsec = (self.rootn * 32 + self.bps - 1) // self.bps
        self.datastart = self.rsv + self.nfat * self.fatsz + self.rootsec
        n = (self.tot - self.datastart) // self.spc
        self.type = 12 if n < 4085 else 16 if n < 65525 else 32
        self.fat = data[self.rsv * self.bps:(self.rsv + self.fatsz) * self.bps]
        self.rootcl = struct.unpack_from("<I", bs, 44)[0] if self.type == 32 else 0
    def nxt(self, c):
        if self.type == 12:
            o = c + c // 2
            v = struct.unpack_from("<H", self.fat, o)[0]
            return (v >> 4) if c & 1 else (v & 0xFFF)
        if self.type == 16:
            return struct.unpack_from("<H", self.fat, c * 2)[0]
        return struct.unpack_from("<I", self.fat, c * 4)[0] & 0x0FFFFFFF
    def eoc(self, v):
        return v >= {12: 0xFF8, 16: 0xFFF8, 32: 0x0FFFFFF8}[self.type]
    def chain(self, c):
        out = []
        while 2 <= c and not self.eoc(c) and len(out) < 100000:
            out.append(c); c = self.nxt(c)
        return out
    def clus(self, c):
        o = (self.datastart + (c - 2) * self.spc) * self.bps
        return self.d[o:o + self.spc * self.bps]
    def dirdata(self, c):
        if c == 0 and self.type != 32:
            o = (self.rsv + self.nfat * self.fatsz) * self.bps
            return self.d[o:o + self.rootn * 32]
        return b"".join(self.clus(x) for x in self.chain(c or self.rootcl))
    def entries(self, c=0):
        raw = self.dirdata(c); lfn = []; out = []
        for i in range(0, len(raw), 32):
            e = raw[i:i + 32]
            if e[0] == 0: break
            if e[0] == 0xE5: lfn = []; continue
            if e[11] == 0x0F:
                part = e[1:11] + e[14:26] + e[28:32]
                s = part.decode("utf-16le").split("\x00")[0].replace("￿", "")
                lfn.insert(0, s); continue
            if e[11] & 0x08: lfn = []; continue
            sn = e[:8].decode("latin1").rstrip() + ("." + e[8:11].decode("latin1").rstrip() if e[8:11].strip() else "")
            hi = struct.unpack_from("<H", e, 20)[0]; lo = struct.unpack_from("<H", e, 26)[0]
            out.append(dict(name="".join(lfn) or sn, short=sn, dir=bool(e[11] & 0x10), size=struct.unpack_from("<I", e, 28)[0], cl=(hi << 16) | lo))
            lfn = []
        return out
    def read(self, ent):
        b = b"".join(self.clus(x) for x in self.chain(ent["cl"]))
        return b[:ent["size"]]
    def find(self, path):
        c = 0; ent = None
        for part in [p for p in path.split("/") if p]:
            ent = next((e for e in self.entries(c) if e["name"].lower() == part.lower() or e["short"].lower() == part.lower()), None)
            if not ent: return None
            c = ent["cl"]
        return ent
