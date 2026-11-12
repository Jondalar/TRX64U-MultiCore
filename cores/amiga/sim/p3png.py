import sys, zlib, struct
t = open(sys.argv[1]).read().split()
w, h = int(t[1]), int(t[2]); v = bytes(int(x) for x in t[4:4 + w*h*3])
raw = b''.join(b'\0' + v[y*w*3:(y+1)*w*3] for y in range(h))
def ch(k, b): return struct.pack('>I', len(b)) + k + b + struct.pack('>I', zlib.crc32(k + b))
open(sys.argv[2], 'wb').write(b'\x89PNG\r\n\x1a\n' + ch(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 2, 0, 0, 0)) + ch(b'IDAT', zlib.compress(raw)) + ch(b'IEND', b''))
