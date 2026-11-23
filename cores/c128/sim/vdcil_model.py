#!/usr/bin/env python3
# T257 fault 2: the picture of the ruler program vdc_il_ruler.asm (the same lines, built the same way), for tb_c128_vdcil.sv.
#   python3 vdcil_model.py gen <dir>      writes <dir>/lines_<m>.hex, m = 0..3 (the mode letters A..D): 480 lines, one line of 160 hex
#                                         digits each (byte 0 first), picture line p as the VDC RAM holds it (p even at $4000 + 80 (p / 2),
#                                         p odd at $8B00 + 80 (p / 2), Busted Boulders' layout)
#   python3 vdcil_model.py png <ppm> <png> converts a dump of the bench to PNG (needs PIL; for looking only)
#   python3 vdcil_model.py check <log> <dir> <m>   run_ruler.sh: the program run on the whole machine in mode m. Checks the registers read through the read-back
#                                         port (tb_c128.v "vk|run|" lines) against the mode's table, the whole VDC RAM ("vramall|") against the 480 picture lines,
#                                         and every captured field (<dir>/vdcfield<k>.txt) against the lines it must show; writes <dir>/vdcfield<k>.png when PIL
#                                         is there. Final line PASS or FAIL.
# Written for this project (TREX).
import os
import sys

GLYPHS = [[0x18, 0x3c, 0x66, 0x66, 0x7e, 0x66, 0x66, 0x00], [0x7c, 0x66, 0x66, 0x7c, 0x66, 0x66, 0x7c, 0x00],
          [0x3c, 0x66, 0x60, 0x60, 0x60, 0x66, 0x3c, 0x00], [0x78, 0x6c, 0x66, 0x66, 0x66, 0x6c, 0x78, 0x00]]


def line_bytes(p, mode):
    """the 80 bytes of picture line p (0..479) with the letter of mode (0..3): vdc_il_ruler.asm `build` and `setmode`"""
    b = [0] * 80
    if p in (0, 479):
        b = [0xff] * 80
    else:
        b[0] |= 0x80
        b[79] |= 0x01
        if p % 48 < 2:
            for i in range(10):
                b[i] = 0xff
                b[70 + i] = 0xff
        if 96 <= p < 224:
            for i in range(12):
                b[(2 if p % 2 == 0 else 66) + i] = 0xff
        x0 = p & 63
        for j in range(10):
            for x in (x0 + 64 * j, x0 + 64 * j + 1):
                if x < 640:
                    b[x >> 3] |= 0x80 >> (x & 7)
        for k in range(9):
            b[38 - k] = 0x7e if (p >> k) & 1 else 0x00
    if 16 <= p < 48:
        g = GLYPHS[mode][(p - 16) >> 2]
        for k in range(4):
            v = 0
            if g & (0x80 >> (2 * k)):
                v |= 0xf0
            if g & (0x40 >> (2 * k)):
                v |= 0x0f
            b[72 + k] = v
    return b


def gen(d):
    os.makedirs(d, exist_ok=True)
    for m in range(4):
        with open(os.path.join(d, 'lines_%d.hex' % m), 'w') as f:
            for p in range(480):
                f.write(''.join('%02x' % v for v in line_bytes(p, m)) + '\n')


def png(src, dst):
    from PIL import Image
    with open(src, 'rb') as f:
        data = f.read()
    hdr = data.split(b'\n', 3)
    w, h = (int(v) for v in hdr[1].split())
    Image.frombytes('RGB', (w, h), hdr[3]).save(dst)


# the registers each mode writes (vdc_il_ruler.asm m_il, m_c, m_d) and the bits the VDC reads back as 1 (vdc_top.sv regval)
MODE_REGS = {0: {0: 0x7e, 1: 0x50, 2: 0x66, 3: 0x49, 4: 0x33, 5: 0x07, 6: 0x30, 7: 0x32, 8: 0x03, 9: 0x09, 12: 0x40, 13: 0x00, 20: 0x10, 21: 0x00,
                 23: 0x09, 24: 0x00, 25: 0x80, 26: 0xf0, 27: 0x00, 28: 0x30}}
MODE_REGS[1] = MODE_REGS[0]
MODE_REGS[2] = {**MODE_REGS[0], 5: 0x03, 8: 0x01, 9: 0x04, 23: 0x04}
MODE_REGS[3] = {**MODE_REGS[2], 8: 0x00}
ONES = {5: 0xe0, 8: 0xfc, 9: 0xe0, 10: 0x80, 11: 0xe0, 23: 0xe0, 28: 0x0f, 29: 0xe0, 36: 0xf0}


def bits(b):
    return ''.join('1' if v & (0x80 >> k) else '0' for v in b for k in range(8))


def check(log, d, mode):
    bad = 0
    regs = {'run': {}, 'stop': {}}
    vram = bytearray(65536)
    have = False
    for line in open(log, errors='replace'):
        f = line.rstrip('\n').split('|')
        if f[0] == 'vk' and len(f) == 4 and f[1] in regs:
            regs[f[1]][int(f[2])] = int(f[3], 16)
        elif f[0] == 'vramall' and len(f) == 3:
            a = int(f[1], 16)
            vram[a:a + 64] = bytes.fromhex(f[2])
            have = True
    for k in ('run', 'stop'):
        print('regs|%s|' % k + ' '.join('%d=%02x' % (r, v) for r, v in sorted(regs[k].items())))
    nr = 0
    for r, v in sorted(MODE_REGS[mode].items()):
        want = v | ONES.get(r, 0)
        if regs['run'].get(r) != want:
            print('FAIL: R%d reads back %s, mode %s wrote $%02x (it reads $%02x)' % (r, '-' if r not in regs['run'] else '$%02x' % regs['run'][r], 'ABCD'[mode], v, want))
            nr += 1
    print('regs: %d of %d registers the mode writes read back wrong' % (nr, len(MODE_REGS[mode])))
    bad += nr > 0 or not regs['run']
    if not have:
        print('FAIL: no VRAM dump')
        bad += 1
    else:
        wrong = [p for p in range(480) if list(vram[(0x4000 if p % 2 == 0 else 0x8b00) + 80 * (p // 2):][:80]) != line_bytes(p, mode)]
        print('vram: %d of 480 picture lines wrong' % len(wrong) + (' (first %s)' % ' '.join(str(p) for p in wrong[:12]) if wrong else ''))
        for p in wrong[:3]:
            a = (0x4000 if p % 2 == 0 else 0x8b00) + 80 * (p // 2)
            print('  line %d at $%04x: got  %s' % (p, a, bytes(vram[a:a + 80]).hex()))
            print('  line %d at $%04x: want %s' % (p, a, bytes(line_bytes(p, mode)).hex()))
        bad += len(wrong) > 0
    k = 0
    while os.path.exists(os.path.join(d, 'vdcfield%d.txt' % k)):
        rows = open(os.path.join(d, 'vdcfield%d.txt' % k)).read().split()
        hdr, rows = rows[:8], rows[8:]
        il = int(hdr[3])
        firsts = [r.find('f') for r in rows if 'f' in r]
        x0 = max(set(firsts), key=firsts.count) if firsts else 0
        fld = il & 1
        wrong = []
        for y, r in enumerate(rows):
            p = 2 * y + fld if mode < 2 else 2 * y
            if p >= 480:
                break
            got = ''.join('1' if c == 'f' else '0' for c in r[x0:x0 + 640])
            if got != bits(line_bytes(p, mode)):
                wrong.append(y)
        print('field %d (il %d, %d lines, %d dots, picture from dot %d): %d lines unlike picture lines %s' % (k, il, len(rows), len(rows[0]) if rows else 0, x0,
              len(wrong), '2y + %d' % fld if mode < 2 else '2y') + (' (first rows %s)' % ' '.join(str(y) for y in wrong[:12]) if wrong else ''))
        bad += len(wrong) > 0 or not rows
        try:
            from PIL import Image
            pal = {c: (255, 255, 255) if c == 'f' else (0, 0, 0) if c == '0' else (128, 128, 255) for c in '0123456789abcdef'}
            im = Image.new('RGB', (max(len(r) for r in rows), len(rows)))
            im.putdata([pal[c] for r in rows for c in r.ljust(im.size[0], '0')])
            im.save(os.path.join(d, 'vdcfield%d.png' % k))
        except ImportError:
            pass
        k += 1
    if k == 0:
        print('FAIL: no field captured')
        bad += 1
    print('PASS ruler' if bad == 0 else 'FAIL ruler')
    return bad


if __name__ == '__main__':
    if len(sys.argv) == 3 and sys.argv[1] == 'gen':
        gen(sys.argv[2])
    elif len(sys.argv) == 4 and sys.argv[1] == 'png':
        png(sys.argv[2], sys.argv[3])
    elif len(sys.argv) == 5 and sys.argv[1] == 'check':
        sys.exit(1 if check(sys.argv[2], sys.argv[3], int(sys.argv[4])) else 0)
    else:
        sys.exit('usage: vdcil_model.py gen <dir> | png <ppm> <png> | check <log> <dir> <mode>')
