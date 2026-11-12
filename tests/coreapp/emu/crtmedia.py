#!/usr/bin/env python3
"""Test cartridges for the ue2emu bench of T104.2 (run.sh, "Run Cartridge"): four small .crt files, hand-assembled, no ROM of anyone's in them.
   crtmedia.py <stick dir>
     cart8.crt   normal 8 KB (EXROM 0, GAME 1): the CBM80 start, KERNAL init, "CART8 OK" on the screen
     cart16.crt  normal 16 KB (EXROM 0, GAME 0): the same code, "CART16 OK"
     ocean.crt   Ocean, two 8 KB banks: "OCEAN B0", then a jump through $DE00 = 1 into bank 1, which prints "OCEAN B1" below
     umax.crt    Ultimax 8 KB at $E000: the reset vector, border 5, $42 into RAM $0800 (no KERNAL, no screen)"""
import struct
import sys
import os


def screen(text):
    out = bytearray()
    for ch in text:
        if 'A' <= ch <= 'Z':
            out.append(ord(ch) - 64)
        else:
            out.append(ord(ch))
    return bytes(out)


def header(name, hwtype, exrom, game):
    h = b'C64 CARTRIDGE   ' + struct.pack('>IHHBB', 0x40, 0x0100, hwtype, exrom, game) + bytes(6)
    return h + name.encode().ljust(32, b'\0')


def chip(bank, load, data):
    return b'CHIP' + struct.pack('>IHHHH', 16 + len(data), 0, bank, load, len(data)) + data


def printer(text_addr, n, dest):
    # LDX #0 / LDA text,X / STA dest,X / INX / CPX #n / BNE -11
    return bytes([0xA2, 0x00, 0xBD, text_addr & 255, text_addr >> 8, 0x9D, dest & 255, dest >> 8, 0xE8, 0xE0, n, 0xD0, 0xF5])


def start(code_len_hint=0):
    # cold and warm vector $8009, "CBM80", then IOINIT / RAMTAS / RESTOR / CINT
    return (bytes([0x09, 0x80, 0x09, 0x80, 0xC3, 0xC2, 0xCD, 0x38, 0x30]) +
            bytes([0x20, 0x84, 0xFF, 0x20, 0x87, 0xFF, 0x20, 0x8A, 0xFF, 0x20, 0x81, 0xFF]))


def normal(text, size):
    t = screen(text)
    code = start()                                    # 21 bytes: next is $8015
    body_len = 13 + 3                                 # printer + JMP *
    text_addr = 0x8000 + len(code) + body_len
    code += printer(text_addr, len(t), 0x0400)
    jmp = 0x8000 + len(code)
    code += bytes([0x4C, jmp & 255, jmp >> 8])
    code += t
    return code.ljust(size, b'\xEA')


def ocean():
    t0, t1 = screen("OCEAN B0"), screen("OCEAN B1")
    code = start()
    tr = bytes([0xA9, 0x01, 0x8D, 0x00, 0xDE, 0x4C, 0x00, 0x81])              # in RAM at $0334: bank 1, then its entry at $8100
    # layout: start (21) + printer (13) + copy loop (13) + JMP $0334 (3) + trampoline (8) + text
    tr_addr = 0x8000 + len(code) + 13 + 13 + 3
    text_addr = tr_addr + len(tr)
    code += printer(text_addr, 8, 0x0400)
    code += bytes([0xA2, 0x00, 0xBD, tr_addr & 255, tr_addr >> 8, 0x9D, 0x34, 0x03, 0xE8, 0xE0, len(tr), 0xD0, 0xF5])
    code += bytes([0x4C, 0x34, 0x03])
    assert 0x8000 + len(code) == tr_addr
    code += tr + t0
    b0 = code.ljust(0x2000, b'\xEA')
    b1 = bytearray(b'\xEA' * 0x2000)
    c1 = printer(0x8100 + 13 + 3, 8, 0x0428)
    c1 += bytes([0x4C, (0x8100 + 13) & 255, (0x8100 + 13) >> 8]) + t1
    b1[0x100:0x100 + len(c1)] = c1
    return bytes(b0), bytes(b1)


def umax():
    rom = bytearray(b'\xFF' * 0x2000)
    code = bytes([0x78, 0xA9, 0x05, 0x8D, 0x20, 0xD0, 0xA9, 0x42, 0x8D, 0x00, 0x08, 0x4C, 0x0B, 0xE0])   # SEI / border 5 / $0800 = $42 / JMP *
    rom[:len(code)] = code
    rom[0x1FFA:0x2000] = bytes([0x00, 0xE0, 0x00, 0xE0, 0x00, 0xE0])                                      # NMI, RESET, IRQ -> $E000
    return bytes(rom)


stick = sys.argv[1]
os.makedirs(stick, exist_ok=True)
open(stick + '/cart8.crt', 'wb').write(header('CART8', 0, 0, 1) + chip(0, 0x8000, normal("CART8 OK", 0x2000)))
open(stick + '/cart16.crt', 'wb').write(header('CART16', 0, 0, 0) + chip(0, 0x8000, normal("CART16 OK", 0x4000)))
b0, b1 = ocean()
open(stick + '/ocean.crt', 'wb').write(header('OCEAN', 5, 0, 0) + chip(0, 0x8000, b0) + chip(1, 0x8000, b1))
open(stick + '/umax.crt', 'wb').write(header('UMAX', 0, 1, 0) + chip(0, 0xE000, umax()))
