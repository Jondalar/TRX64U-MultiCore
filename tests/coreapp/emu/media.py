#!/usr/bin/env python3
"""Test media for the ue2emu bench of T104.2 (run.sh): the files of the emulated USB stick. No ROM is written here.
   media.py <stick dir> <drive rom file> <reference file for big.prg>"""
import struct, sys, os

def prg(path, addr, body):
    open(path, 'wb').write(struct.pack('<H', addr) + bytes(body))

def basic_print():                      # 10 PRINT "T1042 OK" : 20 GOTO 10
    l1 = bytes([0x99, 0x20, 0x22]) + b"T1042 OK" + bytes([0x22, 0])
    n1 = 0x0801 + 4 + len(l1)
    b = bytes([n1 & 255, n1 >> 8, 10, 0]) + l1
    l2 = bytes([0x89, 0x20]) + b"10" + bytes([0])
    n2 = n1 + 4 + len(l2)
    return b + bytes([n2 & 255, n2 >> 8, 20, 0]) + l2 + bytes([0, 0])

def big():                              # $0801-$FFF7, byte(a) = a*7+3, a SYS 2061 stub and JMP * at $080D
    b = bytearray(((a * 7 + 3) & 255) for a in range(0x0801, 0xFFF8))
    stub = bytes([0x0B, 0x08, 0x00, 0x00, 0x9E, 0x32, 0x30, 0x36, 0x31, 0, 0, 0, 0x4C, 0x0D, 0x08])
    b[:len(stub)] = stub
    return b

def low():                              # $0300-$100D: zeros, then SEI / border / $2000 = $42 / JMP *  at $1000 (not startable by SYS)
    return bytes(0x1000 - 0x0300) + bytes([0x78, 0xA9, 0x05, 0x8D, 0x20, 0xD0, 0xA9, 0x42, 0x8D, 0x00, 0x20, 0x4C, 0x0B, 0x10])

def d64(prgbytes):                      # one file HELLO on track 1
    spt = lambda t: 21 if t <= 17 else 19 if t <= 24 else 18 if t <= 30 else 17
    off, o = {}, 0
    for t in range(1, 36):
        off[t] = o; o += spt(t) * 256
    d = bytearray(o)
    sec = lambda t, s: off[t] + s * 256
    b = sec(1, 0); d[b] = 0; d[b + 1] = len(prgbytes) + 1; d[b + 2:b + 2 + len(prgbytes)] = prgbytes
    b = sec(18, 0); d[b] = 18; d[b + 1] = 1; d[b + 2] = 0x41
    for t in range(1, 36):
        n = spt(t); free = n; bits = (1 << n) - 1
        if t == 1: bits &= ~1; free -= 1
        if t == 18: bits &= ~3; free -= 2
        d[b + 4 * t:b + 4 * t + 4] = bytes([free, bits & 255, (bits >> 8) & 255, (bits >> 16) & 255])
    d[b + 0x90:b + 0xA0] = b'T1042 DISK'.ljust(16, b'\xa0'); d[b + 0xA2:b + 0xA4] = b'42'; d[b + 0xA5:b + 0xA7] = b'2A'
    b = sec(18, 1); d[b] = 0; d[b + 1] = 255; d[b + 2] = 0x82; d[b + 3] = 1; d[b + 4] = 0
    d[b + 5:b + 21] = b'HELLO'.ljust(16, b'\xa0'); d[b + 30] = 1
    return d

stick, rom, ref = sys.argv[1], sys.argv[2], sys.argv[3]
os.makedirs(stick + '/roms', exist_ok=True)
prg(stick + '/test.prg', 0x0801, basic_print())
prg(stick + '/big.prg', 0x0801, big()[0:])   # the stub is part of big()
open(ref, 'wb').write(bytes(big()))
prg(stick + '/low.prg', 0x0300, low())
open(stick + '/t1042.d64', 'wb').write(d64(struct.pack('<H', 0x0801) + basic_print()))
open(stick + '/roms/1541.rom', 'wb').write(open(rom, 'rb').read())
