import sys, struct, random
import os; sys.path.insert(0, os.path.dirname(os.path.abspath(__file__))); from g4 import G4
def g2o(g): return (g & ~0x78) | (((g >> 6) & 1) << 3) | (((g >> 3) & 7) << 4)
x = 0x1B0B5EED; idx = {}
for i in range(0x200000 // 4):
    x ^= (x << 13) & 0xFFFFFFFF; x ^= x >> 17; x ^= (x << 5) & 0xFFFFFFFF
    idx.setdefault(x, i)
g = G4(); random.seed(7); bad = 0; n = 300
for _ in range(n):
    ga = 0x100000 + random.randrange(0, 0x200000, 4)
    v = struct.unpack("<L", g.read(ga, 4))[0]
    o = 0x100000 + 4 * idx[v] if v in idx else None
    bad += (o != g2o(ga))
print("random addresses %d, wrong %d" % (n, bad))
blk = g.read(0x2FC000, 0x4000); miss = sum(1 for k in range(0, 0x4000, 4) if struct.unpack_from("<L", blk, k)[0] not in idx)
print("last 16 KB of the pattern: words missing", miss)
g.close()
