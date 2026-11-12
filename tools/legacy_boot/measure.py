import sys, time, struct
import os; sys.path.insert(0, os.path.dirname(os.path.abspath(__file__))); from g4 import G4
def remap(g): return (g & ~0x3FFF) | (((g >> 4) & 0x7F) << 7) | (((g >> 11) & 7) << 4) | (g & 0xF)
x = 0x1B0B5EED; idx = {}
for i in range(0x200000 // 4):
    x ^= (x << 13) & 0xFFFFFFFF; x ^= x >> 17; x ^= (x << 5) & 0xFFFFFFFF
    idx.setdefault(x, i)
for _ in range(60):
    try:
        g = G4(); ok = g.user_id() == 0xDEAD1541
        if ok: break
        g.close()
    except Exception: pass
    time.sleep(1)
print("Gideon design up:", ok)
t = time.time()
print("console:", repr(g.console()[-200:]))
print("FFF0:", g.read(0xFFF0, 16).hex(" "))
N = int(sys.argv[1], 0) if len(sys.argv) > 1 else 0x10000
data = g.read(0x100000, N); print("read %d bytes in %.1f s" % (N, time.time() - t))
g.close()
hit = same = ident = 0; miss = []; pairs = []
for k in range(N // 4):
    ga = 0x100000 + 4 * k; v = struct.unpack_from("<L", data, 4 * k)[0]
    if v in idx:
        o = 0x100000 + 4 * idx[v]; hit += 1; same += (o == remap(ga)); ident += (o == ga)
        if len(pairs) < 40: pairs.append((ga, o))
    else: miss.append(ga)
print("words %d found %d (remap ok %d, identity %d) missing %d" % (N // 4, hit, same, ident, len(miss)))
for ga, o in pairs[:24]: print("  g %06X -> o %06X  remap %06X" % (ga, o, remap(ga)))
print("first missing:", [hex(m) for m in miss[:10]])
