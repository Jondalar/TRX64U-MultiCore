#!/usr/bin/env python3
"""mk6805.py <echo|count> <out.hex>: the two 6805 programs of the card bench (a stand-in for the AppleMouse card's MCU ROM, which is Apple's code and
not here), assembled by hand: a 2 KB image, one byte a line, programs from $0080, the reset vector at $07FE, the mask option register ($0784) zero.
  echo   copies the nibble the 6502 side writes to the PIA's port B bits 7:4 (the MCU's port C input) to the MCU's port A output (the PIA's port A input)
  count  counts the changes of the quadrature pair on port B bits 1:0 (the cursor movement the card is given) in a register, and reports it with the button
         (port B bit 7) on port A: bit 7 = 1 button up, bits 6:0 = the count"""
import sys

OPS = {"lda#": 0xA6, "ldadir": 0xB6, "stadir": 0xB7, "and#": 0xA4, "ora#": 0xAA, "oradir": 0xBA, "cmpdir": 0xB1, "beq": 0x27, "bne": 0x26,
       "bra": 0x20, "incdir": 0x3C, "cldir": 0x3F, "jmp": 0xCC, "sei": 0x9B, "nop": 0x9D}

def asm(prog, base=0x80):
    labels, out, fix = {}, [], []
    pc = base
    for item in prog:
        if isinstance(item, str) and item.endswith(":"):
            labels[item[:-1]] = pc
            continue
        if isinstance(item, str):
            item = (item,)
        op = item[0]
        if op in ("beq", "bne", "bra"):
            out.append(("rel", OPS[op], item[1], pc)); pc += 2
        elif op in ("lda#", "ldadir", "stadir", "and#", "ora#", "oradir", "cmpdir", "incdir", "cldir"):
            out.append(("b", [OPS[op], item[1]])); pc += 2
        else:
            out.append(("b", [OPS[op]])); pc += 1
    img = bytearray()
    for k in out:
        if k[0] == "b":
            img += bytes(k[1])
        else:
            _, op, lab, at = k
            img += bytes([op, (labels[lab] - (at + 2)) & 255])
    return bytes(img)

PA, PB, PC, DDRA = 0x00, 0x01, 0x02, 0x04
PROGS = {
    "echo": ["sei", ("lda#", 0xFF), ("stadir", DDRA), "loop:", ("ldadir", PC), ("stadir", PA), ("bra", "loop")],
    "count": ["sei", ("lda#", 0xFF), ("stadir", DDRA), ("cldir", 0x20), ("ldadir", PB), ("and#", 0x03), ("stadir", 0x21),
              "loop:", ("ldadir", PB), ("and#", 0x03), ("cmpdir", 0x21), ("beq", "same"), ("stadir", 0x21), ("incdir", 0x20),
              "same:", ("ldadir", 0x20), ("and#", 0x7F), ("stadir", 0x22), ("ldadir", PB), ("and#", 0x80), ("oradir", 0x22), ("stadir", PA), ("bra", "loop")],
}
rom = bytearray(2048)
code = asm(PROGS[sys.argv[1]])
rom[0x80:0x80 + len(code)] = code
rom[0x7FE], rom[0x7FF] = 0x00, 0x80
open(sys.argv[2], "w").write("".join("%02x\n" % b for b in rom))
